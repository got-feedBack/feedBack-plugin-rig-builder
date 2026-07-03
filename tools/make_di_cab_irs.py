#!/usr/bin/env python3
"""Precompute the DI+Cab blend for bass cabs (the "70% DI / 30% cab" feature).

The native engine is series-only (no parallel dry/wet on an IR stage), so a
DI+cab blend is baked into a SINGLE impulse response:

    blend = di * delta(@cab peak)  +  cab_w * (cab_IR / ||cab_IR||2)

Convolving with this = di*(dry/DI) + cab_w*(level-matched cab), i.e. the DI and
the cab sit at the SAME broadband level and are then weighted 70/30. The cab
path is convolved, the DI path is the dry signal (a delta is identity), aligned
to the cab IR's main peak to minimise comb filtering.

Because the blend is brighter than the cab alone (it carries the full-range DI),
matching it to the cab by *broadband* RMS would make BASS tones ~4 dB quieter
(the cab alone has a big low-end resonance the flat DI lacks). So per cab we
measure the makeup that keeps the *bass-band* loudness equal to the cab-alone
path, by directly convolving a reference bass signal through each and taking the
RMS ratio (cross-validated to <0.5 dB across different bass signals). That
makeup is what `routes._ir_stage` sets as the stage's `cab_rms_makeup`.

  cab_rms_makeup(blend) = ir_rms_makeup(cab) * rms(cab*bass) / rms(blend*bass)

Outputs:
  * <config>/nam_irs/rocksmith_dicab/<name>.wav   — the blended IRs (pre-warm;
    routes.py regenerates any missing one at runtime with the same formula)
  * data/di_cab_makeup.json                        — {name: makeup} + _meta

Run with a Python that has numpy (e.g. /usr/bin/python3). The RUNTIME blend
generation in routes.py is pure-python (no numpy) and uses the identical
formula, so the makeup stays valid regardless of who wrote the IR file.
"""
from __future__ import annotations

import glob
import json
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np

DI = 0.7
CAB_W = 0.3
IR_REF_L2 = 2.4
IR_MAKEUP_MAX = 2.818
HERE = Path(__file__).resolve().parent
DATA_DIR = HERE.parent / "data"


def _config_dir() -> Path:
    # macOS app-data; allow override for other OSes / testing.
    env = os.environ.get("SLOPSMITH_CONFIG_DIR")
    if env:
        return Path(env)
    return Path.home() / "Library/Application Support/slopsmith-desktop/slopsmith-config"


def read_ir(path: Path):
    b = path.read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return None
    i = 12
    fmt = data = None
    while i + 8 <= len(b):
        cid = b[i:i + 4]
        sz = struct.unpack("<I", b[i + 4:i + 8])[0]
        body = b[i + 8:i + 8 + sz]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        i += 8 + sz + (sz & 1)
    if not fmt or data is None:
        return None
    afmt, ch, sr, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if afmt == 3 and bits == 32:
        xs = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif afmt == 1 and bits == 16:
        xs = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        return None
    if ch > 1:
        xs = xs[0::ch]
    return xs, sr


def write_f32(path: Path, samples: np.ndarray, sr: int):
    raw = samples.astype("<f4").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 4 + 8 + 16 + 8 + len(raw)))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))
        f.write(struct.pack("<HHIIHH", 3, 1, sr, sr * 4, 4, 32))
        f.write(b"data")
        f.write(struct.pack("<I", len(raw)))
        f.write(raw)


def make_blend(cab: np.ndarray) -> np.ndarray:
    """IDENTICAL formula to routes._di_cab_blend_samples (pure-python there)."""
    l2 = math.sqrt(float(np.sum(cab ** 2))) or 1.0
    blend = cab * (CAB_W / l2)
    peak = int(np.argmax(np.abs(cab)))
    blend = blend.copy()
    blend[peak] += DI
    return blend


def ir_rms_makeup(cab: np.ndarray) -> float:
    l2 = math.sqrt(float(np.sum(cab ** 2)))
    if l2 <= 0:
        return 1.0
    return max(1.0, min(IR_MAKEUP_MAX, IR_REF_L2 / l2))


def _bass_ref(seed: int, n: int = 96000, sr: int = 48000) -> np.ndarray:
    rng = np.random.default_rng(seed)
    f = np.fft.rfftfreq(n, 1 / sr)
    S = np.fft.rfft(rng.standard_normal(n))
    S[f > 450] *= 0.07            # bass-weighted: roll off above ~450 Hz
    return np.fft.irfft(S, n)


def main() -> int:
    cfg = _config_dir()
    src = cfg / "nam_irs" / "rocksmith"
    out_dir = cfg / "nam_irs" / "rocksmith_dicab"
    out_dir.mkdir(parents=True, exist_ok=True)
    files = sorted(glob.glob(str(src / "bass_cab_*.wav")))
    if not files:
        print(f"No bass cab IRs under {src}", file=sys.stderr)
        return 1

    refA, refB = _bass_ref(0), _bass_ref(7)
    rms = lambda x: float(np.sqrt(np.mean(x ** 2)))
    conv = lambda s, ir: np.convolve(s, ir)[:len(s)]

    makeup = {}
    deltas = []
    for fp in files:
        p = Path(fp)
        r = read_ir(p)
        if not r:
            continue
        cab, sr = r
        blend = make_blend(cab)
        mk = ir_rms_makeup(cab) * rms(conv(refA, cab)) / rms(conv(refA, blend))
        makeup[p.stem] = round(mk, 5)
        write_f32(out_dir / p.name, blend, sr)
        # cross-val on a different bass signal: blend bass loudness vs cab-alone
        cab_out = conv(refB, cab) * ir_rms_makeup(cab)
        bl_out = conv(refB, blend) * mk
        deltas.append(20 * math.log10(rms(bl_out) / rms(cab_out)))

    out = {
        "_meta": {"di": DI, "cab": CAB_W, "ref_l2": IR_REF_L2,
                  "note": "cab_rms_makeup for DI+cab blend bass cabs; see make_di_cab_irs.py"},
        "makeup": makeup,
    }
    (DATA_DIR / "di_cab_makeup.json").write_text(json.dumps(out, indent=1))

    print(f"cabs: {len(makeup)} | blended IRs -> {out_dir}")
    print(f"makeup JSON -> {DATA_DIR / 'di_cab_makeup.json'}")
    print(f"cross-val bass loudness Δ vs cab-alone: mean {np.mean(deltas):+.2f} dB, "
          f"max |Δ| {max(abs(x) for x in deltas):.2f} dB, "
          f"within ±1dB {sum(1 for x in deltas if abs(x) <= 1)}/{len(deltas)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
