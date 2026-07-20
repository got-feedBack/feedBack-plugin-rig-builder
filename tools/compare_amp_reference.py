#!/usr/bin/env python3
"""Compare an amp or pedal render with a DI-linked reference.

The script intentionally separates level from distortion shape.  It aligns the
reference and candidate from their short-time envelopes, selects active windows
from the DI, and reports crest, compression, peak distribution and broad-band
spectral balance for the same musical events.

Run through ``uv`` so NumPy/SciPy do not become project runtime dependencies:

  uv run --with numpy --with scipy python tools/compare_amp_reference.py \
      di.wav reference.wav candidate.wav
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from scipy import signal


EPS = 1.0e-12


def decode_mono(path: Path, sample_rate: int = 48_000) -> np.ndarray:
    command = [
        "ffmpeg", "-v", "error", "-i", str(path), "-af", "pan=mono|c0=c0",
        "-ar", str(sample_rate), "-f", "f32le", "-acodec", "pcm_f32le", "-",
    ]
    raw = subprocess.check_output(command)
    return np.frombuffer(raw, dtype="<f4").astype(np.float64)


def db(value: float) -> float:
    return 20.0 * math.log10(max(float(value), EPS))


def percentile(values: np.ndarray, points=(10, 50, 90)) -> dict[str, float]:
    if values.size == 0:
        return {f"p{p}": float("nan") for p in points}
    return {f"p{p}": float(np.percentile(values, p)) for p in points}


def envelope(x: np.ndarray, hop: int) -> np.ndarray:
    usable = x[: (x.size // hop) * hop]
    if usable.size == 0:
        return np.empty(0)
    return np.sqrt(np.mean(usable.reshape(-1, hop) ** 2, axis=1) + EPS)


def align_candidate(reference: np.ndarray, candidate: np.ndarray, sr: int) -> tuple[np.ndarray, int]:
    hop = max(1, sr // 200)
    ref_env = np.log(envelope(reference, hop) + 1.0e-7)
    cand_env = np.log(envelope(candidate, hop) + 1.0e-7)
    ref_env -= np.mean(ref_env)
    cand_env -= np.mean(cand_env)
    corr = signal.correlate(cand_env, ref_env, mode="full", method="fft")
    lags = signal.correlation_lags(cand_env.size, ref_env.size, mode="full")
    max_lag = 2 * 200
    keep = np.abs(lags) <= max_lag
    lag_frames = int(lags[keep][np.argmax(corr[keep])])
    lag_samples = lag_frames * hop

    if lag_samples > 0:
        candidate = candidate[lag_samples:]
    elif lag_samples < 0:
        candidate = np.pad(candidate, (-lag_samples, 0))
    return candidate, lag_samples


def band_levels(x: np.ndarray, sr: int) -> dict[str, float]:
    if x.size < 4096:
        return {}
    frequencies, psd = signal.welch(x, sr, window="hann", nperseg=4096,
                                    noverlap=2048, scaling="spectrum")
    total = max(float(np.sum(psd)), EPS)
    bands = {
        "50-100": (50, 100),
        "100-180": (100, 180),
        "180-320": (180, 320),
        "320-600": (320, 600),
        "80-250": (80, 250),
        "250-800": (250, 800),
        "800-2000": (800, 2000),
        "2000-5000": (2000, 5000),
        "5000-10000": (5000, 10000),
    }
    result = {}
    for name, (low, high) in bands.items():
        mask = (frequencies >= low) & (frequencies < high)
        result[name] = 10.0 * math.log10(max(float(np.sum(psd[mask])) / total, EPS))
    return result


def coherence_by_band(di: np.ndarray, audio: np.ndarray, sr: int) -> dict[str, float]:
    """Measure the part of the output linearly predictable from the exact DI.

    A lower value is not automatically better: the candidate must follow the
    reference.  This catches the common failure where RMS, EQ and crest match
    but the modeled gain control still produces substantially less distortion.
    """
    if di.size < 512 or audio.size < 512:
        return {}
    nperseg = min(8192, di.size, audio.size)
    frequencies, values = signal.coherence(
        di, audio, fs=sr, window="hann", nperseg=nperseg,
        noverlap=nperseg // 2,
    )
    bands = {
        "80-800": (80, 800),
        "800-2500": (800, 2500),
        "2500-8000": (2500, 8000),
    }
    result = {}
    for name, (low, high) in bands.items():
        mask = (frequencies >= low) & (frequencies < high)
        result[name] = float(np.mean(values[mask])) if np.any(mask) else float("nan")
    return result


def analyze(di: np.ndarray, audio: np.ndarray, sr: int, window_ms: float) -> dict:
    window = max(32, int(round(sr * window_ms / 1000.0)))
    count = min(di.size, audio.size) // window
    di = di[: count * window].reshape(count, window)
    audio = audio[: count * window].reshape(count, window)

    di_rms = np.sqrt(np.mean(di * di, axis=1) + EPS)
    out_rms = np.sqrt(np.mean(audio * audio, axis=1) + EPS)
    out_peak = np.max(np.abs(audio), axis=1) + EPS
    active = di_rms > max(np.percentile(di_rms, 15), 10.0 ** (-55.0 / 20.0))

    di_rms = di_rms[active]
    out_rms = out_rms[active]
    out_peak = out_peak[active]
    active_di = di[active].reshape(-1)
    active_audio = audio[active].reshape(-1)
    crest = 20.0 * np.log10(out_peak / out_rms)
    window_gain = 20.0 * np.log10(out_rms / di_rms)
    normalized_peaks = np.abs(active_audio) / max(
        math.sqrt(float(np.mean(active_audio * active_audio))), EPS
    )

    di_std = float(np.std(active_di))
    audio_std = float(np.std(active_audio))
    waveform_correlation = (
        float(np.corrcoef(active_di, active_audio)[0, 1])
        if di_std > EPS and audio_std > EPS else float("nan")
    )

    return {
        "active_windows": int(np.sum(active)),
        "rms_dbfs": db(math.sqrt(float(np.mean(active_audio * active_audio)))),
        "peak_dbfs": db(float(np.max(np.abs(active_audio)))),
        "crest_db": percentile(crest),
        "window_gain_db": percentile(window_gain),
        "normalized_abs_peak": percentile(normalized_peaks, (90, 99, 99.9)),
        "flat_top_fraction": float(np.mean(np.abs(active_audio) > 0.985 * np.max(np.abs(active_audio)))),
        "near_zero_fraction": float(np.mean(np.abs(active_audio) < 1.0e-6)),
        "bands_db_of_total": band_levels(active_audio, sr),
        "coherence_by_band": coherence_by_band(active_di, active_audio, sr),
        "waveform_correlation": waveform_correlation,
    }


def deltas(reference: dict, candidate: dict) -> dict:
    result = {
        "rms_db": candidate["rms_dbfs"] - reference["rms_dbfs"],
        "peak_db": candidate["peak_dbfs"] - reference["peak_dbfs"],
        "crest_p50_db": candidate["crest_db"]["p50"] - reference["crest_db"]["p50"],
        "crest_p90_db": candidate["crest_db"]["p90"] - reference["crest_db"]["p90"],
        "gain_spread_db": (
            candidate["window_gain_db"]["p90"] - candidate["window_gain_db"]["p10"]
        ) - (
            reference["window_gain_db"]["p90"] - reference["window_gain_db"]["p10"]
        ),
        "waveform_correlation": (
            candidate["waveform_correlation"] - reference["waveform_correlation"]
        ),
    }
    result["bands_db"] = {
        name: candidate["bands_db_of_total"].get(name, float("nan")) - value
        for name, value in reference["bands_db_of_total"].items()
    }
    result["coherence_by_band"] = {
        name: candidate["coherence_by_band"].get(name, float("nan")) - value
        for name, value in reference["coherence_by_band"].items()
    }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("di", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--sample-rate", type=int, default=48_000)
    parser.add_argument("--window-ms", type=float, default=50.0)
    args = parser.parse_args()

    di = decode_mono(args.di, args.sample_rate)
    reference = decode_mono(args.reference, args.sample_rate)
    candidate = decode_mono(args.candidate, args.sample_rate)
    candidate, lag = align_candidate(reference, candidate, args.sample_rate)
    length = min(di.size, reference.size, candidate.size)
    di, reference, candidate = di[:length], reference[:length], candidate[:length]

    ref_result = analyze(di, reference, args.sample_rate, args.window_ms)
    candidate_result = analyze(di, candidate, args.sample_rate, args.window_ms)
    report = {
        "alignment_samples": lag,
        "alignment_ms": 1000.0 * lag / args.sample_rate,
        "reference": ref_result,
        "candidate": candidate_result,
        "candidate_minus_reference": deltas(ref_result, candidate_result),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
