#!/usr/bin/env python3
"""Extract every Rocksmith gear photo from gears.psarc → PNG.

Generic over the four gear categories Rocksmith ships art for:

    amp    → guitar + bass amps     (gfxassets/tone_designer/amp/)
    pedal  → guitar + bass pedals   (gfxassets/tone_designer/effect/, gear_*pedal_* manifests)
    rack   → rack-mount effects     (gfxassets/tone_designer/effect/, gear_rack_* manifests)
    cab    → speaker cabinets       (gfxassets/tone_designer/cab/)

Looks up each gear's `manifest` field in rs_to_real.json, finds the
matching DDS inside the psarc, converts to PNG via Pillow, and dumps
into a folder named `<rs_gear_type> - <name>.png` so the files sort
alphabetically by codename family (Pedal_BT*, Pedal_CH*, …) with the
human name right next to it.

Rocksmith ships each gear's art at multiple resolutions
(64/128/256/hero) and multiple variants (0/1/2 — typically front /
back / detail). We pull the highest-res primary view by default
(`_0_hero`). Pass `--variant 1` for alternate angles, `--size 256` to
drop to a smaller texture if `_hero` is missing for a gear.

Usage:

    # Extract every category in one go (default):
    python3 extract_gear_photos.py /path/to/gears.psarc

    # Just pedals into a custom folder:
    python3 extract_gear_photos.py /path/to/gears.psarc \\
        --category pedal --out ~/Desktop/rs_pedals

    # Alternate angle:
    python3 extract_gear_photos.py /path/to/gears.psarc --variant 1
"""

import argparse
import io
import json
import re
import sys
from pathlib import Path


# Which psarc subdir(s) hold the art for each category. Some categories
# split across multiple subdirs in Rocksmith's tree: guitar amps live in
# `amp/` but bass amps live in `bass_amp/` (and similarly for cabs).
# We try every listed subdir per entry and pick the first hit.
_PSARC_SUBDIRS = {
    "amp":   ("amp", "bass_amp"),
    "pedal": ("effect", "bass_effect"),
    "rack":  ("effect", "bass_effect"),
    "cab":   ("cab", "bass_cab"),
}

# Filesystem subdir per category — what we name the output folder when
# --out is left at its default. Mirrors the in-app Manage/Gear buckets.
_OUT_SUBDIR = {
    "amp":   "amp_photos",
    "pedal": "pedal_photos",
    "rack":  "rack_photos",
    "cab":   "cab_photos",
}


def _safe_filename(s: str) -> str:
    """Convert a free-text gear name to a Finder-friendly filename.

    Strips path separators and collapses runs of weird chars to a single
    space. Accented letters survive (macOS/APFS handles them fine and
    they're more readable than ASCII transliteration).
    """
    s = re.sub(r"[\\/:*?\"<>|]+", " ", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s or "unnamed"


def _gear_matches_category(rs_gear_type: str, rs_info: dict,
                           category: str) -> bool:
    """Decide whether this gear entry belongs to `category`.

    For amp / cab the `category` field in rs_to_real.json is the source
    of truth. For pedal / rack we fall back to a codename prefix check:
    extract_gear_map.py doesn't always populate `category` for cab/pedal
    entries, but the rs_gear_type prefix (`Pedal_*`, `Bass_Pedal_*`,
    `Rack_*`) is reliable.
    """
    info_cat = rs_info.get("category")
    if info_cat == category:
        return True
    g = rs_gear_type.lower()
    if category == "pedal":
        return g.startswith("pedal_") or g.startswith("bass_pedal_")
    if category == "rack":
        return g.startswith("rack_")
    if category == "cab":
        return g.startswith("cab_") or g.startswith("bass_cab_")
    return False


def extract_category(category: str, psarc_path: Path, rs_map: dict,
                     out_dir: Path, variant: int, size: str,
                     read_psarc_entries) -> tuple[list, list]:
    """Pull every DDS for one category, convert to PNG, write to disk.

    Returns (written, missing) for the caller to report.
    """
    from PIL import Image

    subdirs = _PSARC_SUBDIRS[category]
    targets: list[tuple[str, dict, str]] = []
    for k, v in rs_map.items():
        if not isinstance(v, dict):
            continue
        if not _gear_matches_category(k, v, category):
            continue
        manifest = v.get("manifest", "")
        if not manifest:
            continue
        targets.append((k, v, manifest))

    if not targets:
        return [], []

    # Build candidate paths — variant fallback chain so a missing
    # `_<v>_hero` falls back to lower sizes / variant 0 rather than
    # silently dropping the gear. Subdir fallback handles bass amps
    # (which live under `bass_amp/` rather than `amp/` in gears.psarc).
    size_fallbacks = [size]
    if size != "hero":
        size_fallbacks.append("hero")
    for s in ("256", "128", "64"):
        if s not in size_fallbacks:
            size_fallbacks.append(s)
    variant_fallbacks = [variant]
    if variant != 0:
        variant_fallbacks.append(0)

    # Cabs in Rocksmith are 1 base manifest × ~12 mic-position variants
    # (`_5c`, `_5e`, `_co`, `_rc`, …). All variants of one cab share the
    # SAME texture — the mic position is an audio-only thing. The art
    # ships once under the base manifest, so for cabs we strip the
    # suffix when building candidate paths. Example:
    #   gear_bass_cab_at1150bc_5c → also try gear_bass_cab_at1150bc
    mic_suffix_re = re.compile(r"_(?:[0-9]?[a-z]{1,2})$") if category == "cab" else None

    target_paths: list[tuple[str, dict, list[str]]] = []
    for k, v, manifest in targets:
        manifests_to_try = [manifest]
        if mic_suffix_re is not None:
            base = mic_suffix_re.sub("", manifest)
            if base and base != manifest and base not in manifests_to_try:
                manifests_to_try.append(base)
        candidates = []
        for m in manifests_to_try:
            for sd in subdirs:
                for var in variant_fallbacks:
                    for sz in size_fallbacks:
                        candidates.append(
                            f"gfxassets/tone_designer/{sd}/{m}_{var}_{sz}.dds")
        target_paths.append((k, v, candidates))

    # One psarc read per category × subdir. The file is ~400 MB so we
    # batch the globs into a single call when possible.
    globs = [f"gfxassets/tone_designer/{sd}/*.dds" for sd in subdirs]
    print(f"[{category}] Reading {', '.join(globs)} from "
          f"{psarc_path.name}…", file=sys.stderr)
    entries = read_psarc_entries(str(psarc_path), globs)
    print(f"[{category}]   got {len(entries)} DDS entries total.",
          file=sys.stderr)

    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    missing = []
    for k, v, candidates in target_paths:
        name = v.get("name") or k
        chosen = next((c for c in candidates if c in entries), None)
        if chosen is None:
            missing.append((k, name, candidates[0]))
            continue
        try:
            img = Image.open(io.BytesIO(entries[chosen])).convert("RGBA")
        except Exception as e:
            missing.append((k, name, f"{chosen} (decode failed: {e})"))
            continue
        fname = f"{k} - {_safe_filename(name)}.png"
        out_path = out_dir / fname
        img.save(out_path, format="PNG", optimize=True)
        written.append((k, name, out_path.name))
    return written, missing


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("psarc_path",
                    help="Path to Rocksmith's gears.psarc.")
    ap.add_argument("--category", default="all",
                    choices=("all", "amp", "pedal", "rack", "cab"),
                    help="Which gear category to extract (default: all).")
    ap.add_argument("--out", default=None,
                    help="Output folder. Default: <category>_photos/ per "
                         "category (e.g. pedal_photos/). With --category all, "
                         "each category gets its own subfolder.")
    ap.add_argument("--rs-map",
                    default=str(Path(__file__).parent / "rs_to_real.json"),
                    help="Path to rs_to_real.json (default: rs_to_real.json "
                         "next to this script — works regardless of CWD).")
    ap.add_argument("--variant", type=int, default=0, choices=(0, 1, 2),
                    help="Art variant (0=primary, 1/2=alternate angles).")
    ap.add_argument("--size", default="hero",
                    choices=("64", "128", "256", "hero"),
                    help="Texture size to extract (default: hero = largest).")
    args = ap.parse_args()

    psarc_path = Path(args.psarc_path).expanduser().resolve()
    if not psarc_path.exists():
        print(f"error: {psarc_path} not found", file=sys.stderr)
        sys.exit(1)
    rs_map_path = Path(args.rs_map).expanduser().resolve()
    if not rs_map_path.exists():
        print(f"error: {rs_map_path} not found", file=sys.stderr)
        sys.exit(1)

    try:
        from PIL import Image  # noqa: F401 — sanity check
    except ImportError:
        print("error: Pillow not installed (pip install Pillow)",
              file=sys.stderr)
        sys.exit(1)

    try:
        sys.path.insert(
            0, "/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib")
        from psarc import read_psarc_entries
    except ImportError as e:
        print(f"error: can't import psarc reader: {e}", file=sys.stderr)
        print("Run with the bundled Slopsmith Python.", file=sys.stderr)
        sys.exit(1)

    rs_map = json.loads(rs_map_path.read_text())
    categories = (["amp", "pedal", "rack", "cab"]
                  if args.category == "all" else [args.category])

    all_written, all_missing = [], []
    for cat in categories:
        # Resolve output directory. For --category all we want per-cat
        # subdirs under whatever --out the user passed (or under the
        # default `gear_photos/`).
        if args.category == "all":
            base = (Path(args.out).expanduser().resolve()
                    if args.out else Path("gear_photos").resolve())
            out_dir = base / _OUT_SUBDIR[cat]
        else:
            out_dir = (Path(args.out).expanduser().resolve()
                       if args.out else Path(_OUT_SUBDIR[cat]).resolve())
        written, missing = extract_category(
            cat, psarc_path, rs_map, out_dir, args.variant, args.size,
            read_psarc_entries)
        all_written.append((cat, out_dir, written))
        all_missing.extend((cat, *m) for m in missing)

    print()
    for cat, out_dir, written in all_written:
        print(f"── {cat}: wrote {len(written)} PNGs to {out_dir}")

    if all_missing:
        print()
        print(f"── {len(all_missing)} gear entries with no art found ──",
              file=sys.stderr)
        for cat, k, name, expected in all_missing[:20]:
            print(f"  [{cat:5}] {k} ({name}) — expected {expected}",
                  file=sys.stderr)
        if len(all_missing) > 20:
            print(f"  …and {len(all_missing) - 20} more",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
