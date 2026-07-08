#!/usr/bin/env python3
"""Offline per-cab loudness measurement -> data/cab_loudness_makeup.json.

Every cab IR colours the signal differently, so after the (already loudness-
normalized) amp the cab shifts perceived loudness by a cab-dependent amount.
The flat _RS_IR_MAKEUP left that spread for the final leveler to chase per song,
which it can't do consistently -> songs came out at different volumes.

This tool measures each cab's loudness the perceptual way (BS.1770-4 K-weighted,
like the amp loudness model) by convolving a fixed broadband reference (pink
noise) through the cab's IRs, then writes a per-cab makeup factor that lands
every cab at a common target loudness. routes.py `_cab_loudness_makeup` bakes it
into the engine IR gain (see _ir_stage), so every cab reaches the leveler matched.

Key = the IR folder name = clone_slug(catalog name) (tools/generate_real_cab_irs
`clone_slug`), which at runtime is Path(ir_path).parent.name -> exact match.

Pure numpy (pyloudnorm/scipy aren't available in the bundled env). K-weighting is
applied as a frequency-domain multiply (its biquads' transfer function on the FFT
grid) folded into the same FFT convolution, so there's no per-sample IIR loop.
Pink noise is stationary, so integrated == momentary (gating removes nothing) ->
we use the ungated mean-square loudness. The reference level is arbitrary but
identical for every cab, so the RELATIVE loudness differences (what the makeup
needs) are exact.

Run:  python3 tools/measure_cab_loudness.py            # all cabs, write the table
      python3 tools/measure_cab_loudness.py --dry      # measure + print, no write
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent.parent          # plugins/rig_builder
IR_ROOT = HERE / "assets" / "cab_irs"
OUT = HERE / "data" / "cab_loudness_makeup.json"

SR = 48000
REF_SECONDS = 4.0
PINK_SEED = 20260708
# Clamp the makeup so a pathologically quiet/loud cab can't demand an extreme
# lift (clipping) or cut. The final leveler mops up whatever is left.
MAKEUP_MIN_DB = -9.0
MAKEUP_MAX_DB = 9.0

# BS.1770-4 K-weighting biquads @ 48 kHz (stage 1 high-shelf, stage 2 RLB HPF).
_K_STAGE1_B = np.array([1.53512485958697, -2.69169618940638, 1.19839281085285])
_K_STAGE1_A = np.array([1.0, -1.69065929318241, 0.73248077421585])
_K_STAGE2_B = np.array([1.0, -2.0, 1.0])
_K_STAGE2_A = np.array([1.0, -1.99004745483398, 0.99007225036621])


def _read_ir(path: Path) -> np.ndarray | None:
    """Mono float32 cab IR -> numpy array (or None if not the shape we ship)."""
    try:
        b = path.read_bytes()
    except OSError:
        return None
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return None
    pos, fmt, ch, bps, do, ds = 12, None, None, None, None, None
    while pos < len(b) - 8:
        cid = b[pos:pos + 4]
        cs = struct.unpack("<I", b[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt, ch, _sr, _, _, bps = struct.unpack("<HHIIHH", b[pos + 8:pos + 8 + 16])
        elif cid == b"data":
            do, ds = pos + 8, cs
        pos += 8 + cs + (cs & 1)
    if (fmt, bps, ch) != (3, 32, 1) or do is None:
        return None
    return np.frombuffer(b[do:do + ds], dtype="<f4").astype(np.float64)


def _pink_noise(n: int) -> np.ndarray:
    """Stationary pink noise (−3 dB/oct) via 1/sqrt(f) shaping of white noise."""
    rng = np.random.default_rng(PINK_SEED)
    white = rng.standard_normal(n)
    X = np.fft.rfft(white)
    f = np.arange(X.size)
    shape = np.ones_like(f, dtype=np.float64)
    shape[1:] = 1.0 / np.sqrt(f[1:])
    x = np.fft.irfft(X * shape, n=n)
    return x / np.sqrt(np.mean(x * x))          # unit-RMS reference


def _biquad_response(b, a, w):
    z = np.exp(-1j * w)
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def _k_weight_response(nfft: int) -> np.ndarray:
    w = 2.0 * np.pi * np.arange(nfft // 2 + 1) / nfft
    return (_biquad_response(_K_STAGE1_B, _K_STAGE1_A, w)
            * _biquad_response(_K_STAGE2_B, _K_STAGE2_A, w))


def measure() -> dict:
    n = int(SR * REF_SECONDS)
    pink = _pink_noise(n)
    cabs = sorted(d for d in IR_ROOT.iterdir() if d.is_dir() and not d.name.startswith("_"))
    # One shared FFT size for pink*IR (IRs are short, ~2048 taps).
    nfft = 1 << int(math.ceil(math.log2(n + 8192)))
    P = np.fft.rfft(pink, n=nfft)
    K = _k_weight_response(nfft)
    PK = P * K                                   # pink, K-weighted, once
    results = {}
    for d in cabs:
        ms_list = []
        for wav in sorted(d.glob("*.wav")):
            ir = _read_ir(wav)
            if ir is None or ir.size == 0:
                continue
            H = np.fft.rfft(ir, n=nfft)
            y = np.fft.irfft(PK * H, n=nfft)[:n + ir.size]
            ms_list.append(float(np.mean(y * y)))
        if ms_list:
            # Average across the cab's mic/pos variants in the LINEAR (power)
            # domain, so a cab's per-cab loudness isn't skewed by one hot variant.
            mean_ms = float(np.mean(ms_list))
            results[d.name] = -0.691 + 10.0 * math.log10(mean_ms + 1e-30)
    return results


def main():
    dry = "--dry" in sys.argv
    lufs = measure()
    if not lufs:
        print("no cab IRs found under", IR_ROOT)
        sys.exit(1)
    vals = sorted(lufs.values())
    # Target = median so guitar cabs (the majority) stay near their current level
    # and the correction is balanced; the leveler re-normalizes the absolute level.
    target = float(np.median(vals))
    makeup = {}
    for name, l in lufs.items():
        db = max(MAKEUP_MIN_DB, min(MAKEUP_MAX_DB, target - l))
        makeup[name] = round(10.0 ** (db / 20.0), 5)
    spread = vals[-1] - vals[0]
    print(f"{len(lufs)} cabs measured | LUFS(rel) spread = {spread:.1f} dB "
          f"| target(median) = {target:.1f}")
    order = sorted(lufs.items(), key=lambda kv: kv[1])
    for name, l in order[:5] + order[-5:]:
        print(f"  {name:26s} {l:6.1f} LUFSrel  -> makeup {20*math.log10(makeup[name]):+5.1f} dB")
    if dry:
        print("(--dry: not writing)")
        return
    OUT.write_text(json.dumps({
        "_comment": "Per-cab BS.1770-4 loudness makeup (pink-noise reference). "
                    "Key = IR folder name = clone_slug. Baked into the engine IR "
                    "gain by routes.py _cab_loudness_makeup. Regenerate with "
                    "tools/measure_cab_loudness.py.",
        "sr": SR, "ref_seconds": REF_SECONDS, "target_lufs_rel": round(target, 3),
        "clamp_db": [MAKEUP_MIN_DB, MAKEUP_MAX_DB],
        "makeup": dict(sorted(makeup.items())),
    }, indent=2) + "\n", encoding="utf-8")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
