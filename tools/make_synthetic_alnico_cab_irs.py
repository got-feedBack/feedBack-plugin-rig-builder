#!/usr/bin/env python3
"""Generate Slopsmith-owned AC30-style 2x12 alnico cabinet IRs.

These IRs are synthesized from a simple speaker/cab/mic response model. The
script does not read, analyze, deconvolve, fit, or copy any captured/proprietary
IR. The intent is a redistributable "alnico chime 2x12 open-back" fallback for
Rocksmith's Cab_EN212C override and the BOX AC30 workflow.
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

import numpy as np


SR = 48_000
IR_LEN = 2048
FFT_LEN = 8192
ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "assets" / "cab_irs"
PREFIX = "Box_2x12_Alnico"


def db_to_amp(db: np.ndarray | float) -> np.ndarray | float:
    return np.power(10.0, np.asarray(db) / 20.0)


def shelf_db(freq: np.ndarray, corner: float, gain_db: float, slope: float = 2.0) -> np.ndarray:
    x = np.maximum(freq, 1.0) / corner
    return gain_db * (x ** slope / (1.0 + x ** slope))


def bell_db(freq: np.ndarray, center: float, width_oct: float, gain_db: float) -> np.ndarray:
    x = np.log2(np.maximum(freq, 1.0) / center) / width_oct
    return gain_db * np.exp(-0.5 * x * x)


def highpass_mag(freq: np.ndarray, fc: float, order: float) -> np.ndarray:
    x = np.maximum(freq, 1.0) / fc
    return (x ** order) / np.sqrt(1.0 + x ** (2.0 * order))


def lowpass_mag(freq: np.ndarray, fc: float, order: float) -> np.ndarray:
    x = np.maximum(freq, 1.0) / fc
    return 1.0 / np.sqrt(1.0 + x ** (2.0 * order))


def minimum_phase_from_mag(mag: np.ndarray) -> np.ndarray:
    """Return a minimum-phase FIR whose magnitude follows `mag`."""
    mag = np.maximum(mag, 1.0e-5)
    cep = np.fft.irfft(np.log(mag), n=FFT_LEN)
    min_cep = np.zeros_like(cep)
    min_cep[0] = cep[0]
    min_cep[1:FFT_LEN // 2] = 2.0 * cep[1:FFT_LEN // 2]
    min_cep[FFT_LEN // 2] = cep[FFT_LEN // 2]
    spec = np.exp(np.fft.rfft(min_cep, n=FFT_LEN))
    return np.fft.irfft(spec, n=FFT_LEN)[:IR_LEN].astype(np.float64)


def add_open_back_reflections(ir: np.ndarray, pos: str, mic: str) -> np.ndarray:
    """Add small synthetic early reflections from an open-back 2x12 cab."""
    out = ir.copy()
    pos_scale = {"cone": 0.75, "edge": 0.95, "offaxis": 1.18}[pos]
    mic_scale = {"dyn": 0.80, "cond": 1.00, "ribbon": 1.12}[mic]
    taps = [
        (int(0.58e-3 * SR), -0.034 * pos_scale),
        (int(1.15e-3 * SR),  0.025 * mic_scale),
        (int(2.35e-3 * SR), -0.014 * pos_scale * mic_scale),
        (int(4.20e-3 * SR),  0.010 * (0.8 if mic == "dyn" else 1.0)),
    ]
    for delay, gain in taps:
        if delay < len(out):
            out[delay:] += gain * ir[:len(out) - delay]
    # Fast synthetic room/cab tail, independent of any measured IR.
    n = np.arange(len(out))
    tail = np.sin(2.0 * math.pi * 118.0 * n / SR) * np.exp(-n / (0.028 * SR))
    tail += 0.35 * np.sin(2.0 * math.pi * 235.0 * n / SR + 0.7) * np.exp(-n / (0.018 * SR))
    out += (0.0028 * pos_scale * mic_scale) * tail
    return out


def target_response(mic: str, pos: str) -> np.ndarray:
    f = np.fft.rfftfreq(FFT_LEN, 1.0 / SR)

    # Common 2x12 open-back alnico voice:
    # - 12-inch alnico speaker Fs around 75 Hz
    # - open-back low-mid cancellation
    # - upper-mid chime, then cone rolloff above the guitar-speaker band
    db = np.zeros_like(f)
    db += bell_db(f, 75.0, 0.22, 3.5)
    db += bell_db(f, 145.0, 0.55, 1.1)
    db += bell_db(f, 410.0, 0.42, -3.4)
    db += bell_db(f, 780.0, 0.55, -0.4)
    db += bell_db(f, 1600.0, 0.42, 1.1)
    db += bell_db(f, 2350.0, 0.46, 4.8)
    db += bell_db(f, 3000.0, 0.30, 9.2)
    db += bell_db(f, 3800.0, 0.32, 10.0)
    db += bell_db(f, 4850.0, 0.36, 5.0)
    db += bell_db(f, 6900.0, 0.42, -0.7)
    db += shelf_db(f, 3100.0, 4.2, 2.0)

    if mic == "dyn":
        db += bell_db(f, 950.0, 0.46, 1.3)
        db += bell_db(f, 3300.0, 0.26, 3.0)
        db += shelf_db(f, 7600.0, 1.6, 2.2)
        hp_fc = 76.0
        lp_fc = 9400.0
    elif mic == "cond":
        db += bell_db(f, 180.0, 0.58, 1.0)
        db += bell_db(f, 2600.0, 0.34, 2.2)
        db += shelf_db(f, 8200.0, 4.0, 2.0)
        hp_fc = 68.0
        lp_fc = 11200.0
    else:  # ribbon
        db += bell_db(f, 155.0, 0.60, 1.4)
        db += bell_db(f, 2500.0, 0.40, 0.7)
        db += shelf_db(f, 6000.0, 0.2, 2.0)
        hp_fc = 62.0
        lp_fc = 8200.0

    if pos == "cone":
        db += bell_db(f, 3100.0, 0.28, 2.7)
        db += bell_db(f, 5200.0, 0.34, 2.8)
        lp_fc *= 1.05
    elif pos == "edge":
        db += bell_db(f, 250.0, 0.54, 1.2)
        db += bell_db(f, 3000.0, 0.33, 0.6)
        db += shelf_db(f, 5200.0, -0.4, 2.0)
    else:  # offaxis
        db += bell_db(f, 220.0, 0.62, 1.7)
        db += bell_db(f, 2900.0, 0.35, -0.6)
        db += shelf_db(f, 4300.0, -1.9, 2.0)
        lp_fc *= 0.92

    mag = db_to_amp(db)
    mag *= highpass_mag(f, hp_fc, 3.2)
    mag *= lowpass_mag(f, lp_fc, 4.2)
    mag[0] = 0.0
    return mag


def write_float_wav(path: Path, samples: np.ndarray) -> None:
    raw = samples.astype("<f4").tobytes()
    with path.open("wb") as fh:
        fh.write(b"RIFF")
        fh.write(struct.pack("<I", 4 + 8 + 16 + 8 + len(raw)))
        fh.write(b"WAVE")
        fh.write(b"fmt ")
        fh.write(struct.pack("<I", 16))
        fh.write(struct.pack("<HHIIHH", 3, 1, SR, SR * 4, 4, 32))
        fh.write(b"data")
        fh.write(struct.pack("<I", len(raw)))
        fh.write(raw)


def synth_ir(mic: str, pos: str) -> np.ndarray:
    ir = minimum_phase_from_mag(target_response(mic, pos))
    ir = add_open_back_reflections(ir, pos, mic)
    # Keep the shipped cab level consistent with the rest of assets/cab_irs.
    peak = float(np.max(np.abs(ir))) or 1.0
    ir = ir * (0.95 / peak)
    return ir.astype(np.float32)


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for mic in ("dyn", "cond", "ribbon"):
        for pos in ("cone", "edge", "offaxis"):
            ir = synth_ir(mic, pos)
            write_float_wav(OUT_DIR / f"{PREFIX}_{mic}_{pos}.wav", ir)
    print(f"wrote {PREFIX}_{{dyn,cond,ribbon}}_{{cone,edge,offaxis}}.wav -> {OUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
