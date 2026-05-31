#!/usr/bin/env python3
"""One-time loudness normalizer for already-extracted Rocksmith cab IRs.

A cab IR's playback loudness tracks its **L2 norm** (sqrt of total energy),
which is the broadband convolution gain — NOT its peak. Rocksmith's IRs, even
after peak-normalizing, have roughly half the L2 of a tone3000 cab IR
(~1.1 vs ~2.4), so the guitar/bass comes out noticeably quieter with a
Rocksmith cab engaged than with a tone3000 one (or bypassed).

This script scales every Rocksmith IR so its L2 matches a tone3000-like target
(2.4) — equalizing cab loudness across the set and against tone3000 — while
capping the peak so an IR can't blow past `PEAK_CAP` (clip safety). Scaling is
uniform across (interleaved) channels, so it changes only the level, never the
frequency response. Idempotent: a file already at the target is skipped.

Usage:
    python normalize_irs.py <dir>     # defaults to ./rocksmith if omitted
"""

import glob
import math
import os
import struct
import sys

from common import PLUGIN_ROOT

TARGET_L2 = 2.4    # match tone3000 cab IRs' broadband convolution gain
PEAK_CAP = 2.0     # never let an IR's peak exceed this (clip safety)


def _read_wav(path):
    with open(path, "rb") as f:
        b = f.read()
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
    return afmt, ch, sr, bits, data


def _write_f32(path, samples, sr, ch):
    bits = 32
    block_align = ch * 4
    byte_rate = sr * block_align
    data_size = len(samples)
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 4 + 8 + 16 + 8 + data_size))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))
        f.write(struct.pack("<HHIIHH", 3, ch, sr, byte_rate, block_align, bits))
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        f.write(samples)


def _ir_scale(vals, target_l2=TARGET_L2, peak_cap=PEAK_CAP):
    """Scale factor to bring `vals` to the target L2, capped so the peak can't
    exceed peak_cap. Returns None if the IR is silent."""
    peak = max((abs(v) for v in vals), default=0.0)
    l2 = math.sqrt(sum(v * v for v in vals))
    if l2 <= 0.0 or peak <= 0.0:
        return None
    scale = target_l2 / l2
    if peak * scale > peak_cap:
        scale = peak_cap / peak
    return scale, l2


def normalize_dir(root, target_l2=TARGET_L2, peak_cap=PEAK_CAP):
    files = sorted(glob.glob(os.path.join(root, "*.wav")))
    changed = skipped = 0
    examples = []
    for p in files:
        r = _read_wav(p)
        if not r:
            skipped += 1
            continue
        afmt, ch, sr, bits, data = r
        if afmt != 3 or bits != 32:
            skipped += 1
            continue
        n = len(data) // 4
        if n == 0:
            skipped += 1
            continue
        vals = struct.unpack("<%df" % n, data)
        sc = _ir_scale(vals, target_l2, peak_cap)
        if sc is None:
            skipped += 1
            continue
        scale, l2 = sc
        if abs(scale - 1.0) < 0.02:   # already at target — idempotent
            skipped += 1
            continue
        out = struct.pack("<%df" % n, *(v * scale for v in vals))
        _write_f32(p, out, sr, ch)
        changed += 1
        if len(examples) < 6:
            examples.append((os.path.basename(p), l2, l2 * scale))
    return files, changed, skipped, examples


def _default_ir_dir():
    """Find where the extracted Rocksmith IRs actually live on this OS.

    The extractor (`extract_irs.py`) writes them to the Slopsmith config
    dir, not next to this script. We try a small set of well-known
    paths in priority order so `python3 normalize_irs.py` Just Works on
    every machine without the user having to type the full path.
    """
    candidates = [
        # 1. Slopsmith config dir on macOS.
        os.path.expanduser(
            "~/Library/Application Support/slopsmith-desktop/"
            "slopsmith-config/nam_irs/rocksmith"),
        # 2. Slopsmith config dir on Linux (XDG default).
        os.path.expanduser(
            "~/.config/slopsmith-desktop/slopsmith-config/nam_irs/rocksmith"),
        # 3. Windows app-data — best effort, only relevant when run
        #    from WSL or similar.
        os.path.expanduser(
            "~/AppData/Roaming/slopsmith-desktop/slopsmith-config/nam_irs/rocksmith"),
        # 4. Legacy fallback: relative to the script (developer setup
        #    with the dir copied in).
        os.path.join(str(PLUGIN_ROOT), "rocksmith"),
    ]
    for path in candidates:
        if os.path.isdir(path):
            return path
    return candidates[0]   # surface the most-likely path in the error


def main(argv):
    root = argv[1] if len(argv) > 1 else _default_ir_dir()
    if not os.path.isdir(root):
        print("not a directory: %s" % root, file=sys.stderr)
        print("usage: python3 normalize_irs.py [<dir>]", file=sys.stderr)
        print("If your IRs live elsewhere, pass the path explicitly.",
              file=sys.stderr)
        return 1
    files, changed, skipped, examples = normalize_dir(root)
    print("normalized %d files, skipped %d (of %d) in %s"
          % (changed, skipped, len(files), root))
    for name, before, after in examples:
        print("  %-40s L2 %6.3f -> %.3f" % (name[:38], before, after))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
