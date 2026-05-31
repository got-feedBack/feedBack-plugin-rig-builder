#!/usr/bin/env python3
"""Extract every Rocksmith guitar amp photo from gears.psarc → PNG.

Walks `rs_to_real.json` for entries that are guitar amps (category=amp,
key not prefixed `Bass_Amp_`), looks up the matching art DDS inside
gears.psarc using the `manifest` field, converts each to PNG with PIL,
and dumps them into a single folder named by `<rs_gear_type> - <name>`
so they sort/group naturally in Finder.

Rocksmith ships each amp art at multiple resolutions (64/128/256/hero)
and multiple variants (0/1/2 — typically front / back / detail). We pull
the highest-res primary view by default (`_0_hero`); pass `--variant 1`
or `--variant 2` to grab alternate angles, and `--size 256` to drop to a
smaller texture if `_hero` is missing for an amp.

Usage:

    python3 extract_amp_photos.py /path/to/gears.psarc

    # Different output dir + alternate angle:
    python3 extract_amp_photos.py /path/to/gears.psarc \\
        --out ~/Desktop/rs_amps --variant 1
"""

import argparse
import io
import json
import re
import sys
from pathlib import Path

from common import PLUGIN_ROOT, DATA_DIR, safe_filename


_safe_filename = safe_filename


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("psarc_path",
                    help="Path to Rocksmith's gears.psarc.")
    ap.add_argument("--out", default="assets/guitar_amp_photos",
                    help="Output folder (default: ./assets/guitar_amp_photos).")
    ap.add_argument("--rs-map", default=str(DATA_DIR / "rs_to_real.json"),
                    help="Path to rs_to_real.json (default: ./rs_to_real.json).")
    ap.add_argument("--variant", type=int, default=0, choices=(0, 1, 2),
                    help="Art variant (0=primary, 1/2=alternate angles).")
    ap.add_argument("--size", default="hero",
                    choices=("64", "128", "256", "hero"),
                    help="Texture size to extract (default: hero = largest).")
    ap.add_argument("--include-bass", action="store_true",
                    help="Also extract bass amps (Bass_Amp_*).")
    args = ap.parse_args()

    psarc_path = Path(args.psarc_path).expanduser().resolve()
    if not psarc_path.exists():
        print(f"error: {psarc_path} not found", file=sys.stderr)
        sys.exit(1)
    rs_map_path = Path(args.rs_map).expanduser().resolve()
    if not rs_map_path.exists():
        print(f"error: {rs_map_path} not found", file=sys.stderr)
        sys.exit(1)

    out_dir = Path(args.out).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    # Lazy imports so the --help text works without slopsmith libs in path.
    try:
        from PIL import Image
    except ImportError:
        print("error: Pillow not installed (pip install Pillow)",
              file=sys.stderr)
        sys.exit(1)

    # The psarc reader needs pycryptodome; the bundled Slopsmith Python
    # has it. The system python typically does too, but if not we fall
    # back to telling the user.
    try:
        sys.path.insert(
            0, "/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib")
        from psarc import read_psarc_entries
    except ImportError as e:
        print(f"error: can't import psarc reader: {e}", file=sys.stderr)
        print("Run with the bundled Slopsmith Python:", file=sys.stderr)
        print("  PY=/Applications/Slopsmith.app/Contents/Resources/python/"
              "runtime/bin/python3.12", file=sys.stderr)
        print("  PYTHONPATH=/Applications/Slopsmith.app/Contents/Resources/"
              "slopsmith/lib:. $PY extract_amp_photos.py ...", file=sys.stderr)
        sys.exit(1)

    rs_map = json.loads(rs_map_path.read_text())

    # Pick the amps we care about. Bass amps live under `Bass_Amp_*`
    # keys; everything else with category=amp is a guitar amp.
    targets = []
    for k, v in rs_map.items():
        if v.get("category") != "amp":
            continue
        is_bass = k.startswith("Bass_Amp_")
        if is_bass and not args.include_bass:
            continue
        manifest = v.get("manifest", "")
        if not manifest:
            continue
        # Skip duplicates by manifest (e.g. Amp_AT120 + Amp_MarshallJCM800
        # are two RS gear codes pointing at the same JCM800 art — extract
        # once, both PNGs point at the same image but with both names so
        # the user sees both codenames in Finder).
        targets.append((k, v, manifest, is_bass))

    if not targets:
        print("No matching amps in rs_to_real.json.", file=sys.stderr)
        sys.exit(1)

    # Build the expected DDS path for each amp. We try `_<variant>_<size>`
    # then progressively fall back: smaller sizes, then variant 0.
    size_fallbacks = [args.size]
    if args.size != "hero":
        size_fallbacks.append("hero")
    for s in ("256", "128", "64"):
        if s not in size_fallbacks:
            size_fallbacks.append(s)
    variant_fallbacks = [args.variant]
    if args.variant != 0:
        variant_fallbacks.append(0)

    # Collect every DDS path we might possibly need, then read them all
    # in one psarc pass (it's a 400MB file — opening it 42 times would
    # be wasteful).
    wanted_paths = set()
    target_paths: list[tuple[str, list[str]]] = []
    for k, v, manifest, is_bass in targets:
        candidates = []
        for var in variant_fallbacks:
            for sz in size_fallbacks:
                p = f"gfxassets/tone_designer/amp/{manifest}_{var}_{sz}.dds"
                candidates.append(p)
                wanted_paths.add(p)
        target_paths.append((k, candidates))

    print(f"Reading {len(wanted_paths)} candidate paths from {psarc_path.name}…",
          file=sys.stderr)
    # read_psarc_entries returns dict[name → bytes]. Use a wildcard then
    # filter — simpler than crafting a giant glob list.
    entries = read_psarc_entries(
        str(psarc_path), ["gfxassets/tone_designer/amp/*.dds"])
    print(f"  got {len(entries)} amp DDS entries total.", file=sys.stderr)

    written = []
    missing = []
    for k, candidates in target_paths:
        info = rs_map[k]
        name = info.get("name", k)
        # Take the first candidate that actually exists in the psarc.
        chosen = next((c for c in candidates if c in entries), None)
        if chosen is None:
            missing.append((k, name, candidates[0]))
            continue
        dds_bytes = entries[chosen]
        try:
            img = Image.open(io.BytesIO(dds_bytes))
            # Force-decode to RGBA — DDS often opens lazily and might
            # error out on .save() if the codec hadn't run yet.
            img = img.convert("RGBA")
        except Exception as e:
            missing.append((k, name, f"{chosen} (decode failed: {e})"))
            continue

        # Filename: "Amp_AT120 - Marshall JCM 800.png" — sorts by codename
        # so families (AT*, BT*, MS*) group together in Finder, but the
        # human name is visible next to it.
        fname = f"{k} - {_safe_filename(name)}.png"
        out_path = out_dir / fname
        img.save(out_path, format="PNG", optimize=True)
        written.append((k, name, out_path.name))

    # Summary
    print()
    print(f"── Wrote {len(written)} PNGs to {out_dir} ──")
    for k, name, fname in written:
        print(f"  {fname}")
    if missing:
        print()
        print(f"── {len(missing)} amps with no art in gears.psarc ──",
              file=sys.stderr)
        for k, name, expected in missing:
            print(f"  {k}  ({name}) — expected {expected}", file=sys.stderr)


if __name__ == "__main__":
    main()
