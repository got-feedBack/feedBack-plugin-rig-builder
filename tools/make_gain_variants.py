#!/usr/bin/env python3
"""Build a ready-to-paste `gain_variants` JSON block from a tone3000 URL.

Sibling of `curate_amp.py`. Where `curate_amp.py` just lists every
capture (so you can decide by eye), THIS script goes one step further:

  1. Lists every capture in the tone (same table as curate_amp.py).
  2. Auto-picks clean / crunch / dist by parsing the gain knob value
     (`Gn`) from each capture's title.
  3. Lets you override any level with `--clean`/`--crunch`/`--dist
     <model_id>`.
  4. Prints a JSON block you copy-paste into `rs_to_real.json` under
     the amp's `rs_gear_type`.

Usage:

    python3 make_gain_variants.py \\
        https://www.tone3000.com/tones/gallien-krueger-rb800-070-2694 \\
        --curator Nacho

    # With manual overrides (e.g. you preferred G4.5 over the auto-pick
    # for crunch on a bass amp where the auto landed on something
    # buzzier):
    python3 make_gain_variants.py <url> --crunch 72428 --curator Nacho

    # Pipe straight to clipboard on macOS:
    python3 make_gain_variants.py <url> --curator Nacho | pbcopy

Auto-mapping rules (when titles encode `Gn`):
  - lowest gain  → clean
  - middle gain  → crunch
  - highest gain → dist

If the titles don't have a parseable `G` value, the script falls back
to model_id order and warns you to verify by ear.

Auth: reuses the API key (or OAuth tokens) already in the plugin's
`rig_builder_settings.json`.
"""

import argparse
import json
import re
import sys
from pathlib import Path

from common import (
    PLUGIN_ROOT, extract_tone_id, capture_title, load_tone3000_client,
)


# Maps each level to the Rocksmith Gain knob range that triggers it.
# Matches the curation CSV / HANDOFF schema; change here if the engine
# ever reads the knob on a different scale.
LEVEL_RANGES = {
    "clean":  [0.0, 35.0],
    "crunch": [35.0, 70.0],
    "dist":   [70.0, 100.0],
}


def parse_gain_from_title(title: str) -> float | None:
    """Parse the Gain knob value from a capture title.

    Common formats: "G7", "G7.5", "G10.0". We allow a decimal point
    because tone3000 community uploads use halves (G1.5, G5.5, etc.).
    The negative lookbehind on \\w prevents matching "G" inside an
    unrelated word (e.g. "Greg's", "GT-3").
    """
    if not isinstance(title, str):
        return None
    m = re.search(r"(?<!\w)G(\d{1,2}(?:\.\d+)?)(?!\d)", title)
    return float(m.group(1)) if m else None


def auto_assign_levels(captures: list[dict]) -> dict[str, dict]:
    """Pick one capture per level based on parsed `G` values.

    Strategy when titles have `Gn`:
      - sort all captures by gain ascending,
      - lowest → clean, highest → dist,
      - middle → crunch (closest to G=5 wins ties).

    Fallback when no titles parse: sort by model_id and use first /
    middle / last as a last resort. Caller warns about reliability.
    """
    with_g = [(c, parse_gain_from_title(c["title"])) for c in captures]
    has_g = [(c, g) for c, g in with_g if g is not None]
    if has_g:
        has_g.sort(key=lambda cg: (cg[1], cg[0]["model_id"] or 0))
        out: dict[str, dict] = {}
        out["clean"] = has_g[0][0]
        if len(has_g) >= 2:
            out["dist"] = has_g[-1][0]
        if len(has_g) >= 3:
            middles = has_g[1:-1]
            middles.sort(key=lambda cg: abs(cg[1] - 5.0))
            out["crunch"] = middles[0][0]
        return out

    # Fallback path. model_id order is roughly upload order; not great
    # but the only signal left.
    captures_sorted = sorted(captures, key=lambda c: c["model_id"] or 0)
    out: dict[str, dict] = {}
    if captures_sorted:
        out["clean"] = captures_sorted[0]
    if len(captures_sorted) >= 2:
        out["dist"] = captures_sorted[-1]
    if len(captures_sorted) >= 3:
        out["crunch"] = captures_sorted[len(captures_sorted) // 2]
    return out


def normalize_capture(m: dict) -> dict:
    """Pluck the bits we use from a raw tone3000 model object."""
    return {
        "model_id": m.get("id"),
        "size": (m.get("size") or "").lower(),
        "license": m.get("license") or "",
        "title": capture_title(m),
    }


def emit_json_block(tone_id: int,
                    assigned: dict[str, dict],
                    curator: str) -> str:
    """Build the `gain_variants` JSON snippet.

    Output is the `gain_variants` object only, wrapped in a single-key
    parent so it's easy to spot where to splice. The rs_gear_type owns
    the rest of the entry — we can't generate that.
    """
    block: dict[str, dict] = {}
    for level in ("clean", "crunch", "dist"):
        cap = assigned.get(level)
        if not cap:
            continue
        spec: dict = {
            "tone3000_id": tone_id,
            "model_id": cap["model_id"],
            "rs_gain_range": LEVEL_RANGES[level],
            "notes": cap["title"],
        }
        if curator:
            spec["curator"] = curator
        block[level] = spec
    return json.dumps({"gain_variants": block}, indent=2, ensure_ascii=False)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url_or_id",
                    help="tone3000 URL or bare tone_id.")
    ap.add_argument("--plugin-dir", default=str(PLUGIN_ROOT),
                    help="rig_builder plugin directory (default: cwd).")
    ap.add_argument("--curator", default="",
                    help="Stamp each variant's `curator` field with this name.")
    # Per-level overrides. The auto-picker is OK as a starting point,
    # but after auditioning you'll often want a specific model_id for
    # a level — pass it here and the script splices it in.
    for level in LEVEL_RANGES:
        ap.add_argument(f"--{level}", dest=level, type=int, default=None,
                        metavar="MODEL_ID",
                        help=f"Force a specific model_id for the {level} variant.")
    args = ap.parse_args()

    tone_id = extract_tone_id(args.url_or_id)
    if tone_id is None:
        print(f"error: no numeric id in {args.url_or_id!r}", file=sys.stderr)
        sys.exit(1)

    client = load_tone3000_client(args.plugin_dir, cache_name="rig_builder_make_gain_variants")

    try:
        payload = client.list_models(tone_id)
    except Exception as e:
        print(f"error: list_models({tone_id}) failed: {e}", file=sys.stderr)
        sys.exit(1)

    raw_models = (payload or {}).get("data") or []
    if not raw_models:
        print(f"tone3000_id {tone_id} returned no captures.", file=sys.stderr)
        sys.exit(1)

    captures = [normalize_capture(m) for m in raw_models]

    tone_title = (payload or {}).get("title") or ""
    print(f"\ntone3000_id: {tone_id}" + (f" — {tone_title}" if tone_title else ""))
    print("─" * 90)
    print(f"{'model_id':<10}  {'size':<10}  {'G':<5}  title")
    print("─" * 90)
    for c in captures:
        g = parse_gain_from_title(c["title"])
        g_disp = f"{g:g}" if g is not None else "?"
        print(f"{c['model_id']:<10}  {c['size']:<10}  {g_disp:<5}  {c['title']}")
    print("─" * 90)

    assigned = auto_assign_levels(captures)
    has_g = any(parse_gain_from_title(c["title"]) is not None for c in captures)
    if not has_g:
        print("\n⚠  No `Gn` parsed from titles — fell back to model_id "
              "order. Verify by ear.", file=sys.stderr)

    # Apply per-level overrides.
    by_id = {c["model_id"]: c for c in captures}
    for level in LEVEL_RANGES:
        forced = getattr(args, level, None)
        if forced is None:
            continue
        if forced not in by_id:
            print(f"⚠  --{level} {forced}: model_id not in this tone — "
                  f"keeping auto-pick.", file=sys.stderr)
            continue
        assigned[level] = by_id[forced]

    print("\nAuto-mapped levels:")
    for level in ("clean", "crunch", "dist"):
        cap = assigned.get(level)
        if cap:
            g = parse_gain_from_title(cap["title"])
            g_disp = f"G{g:g}" if g is not None else "?"
            print(f"  {level:7} → model_id {cap['model_id']:<10}  "
                  f"({g_disp}, {cap['title']})")
        else:
            print(f"  {level:7} → (not enough captures)")

    print("\nPaste this into rs_to_real.json under the amp's entry:\n")
    print(emit_json_block(tone_id, assigned, args.curator))
    print()


if __name__ == "__main__":
    main()
