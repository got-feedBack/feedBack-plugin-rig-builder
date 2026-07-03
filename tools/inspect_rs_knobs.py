#!/usr/bin/env python3
"""Inspect the game knob names that real songs actually use, per gear.

Scans every `preset_pieces.params_json` row in the user's `nam_tone.db` and
prints, per `rs_gear_type`, the distinct knob names observed plus how many
songs use that gear. Optionally shows sample knob VALUES for one gear so a
curator can see the range RS sends.

This solves the recurring "I don't know what knob names RS exposes for
Pedal_X" problem when writing `rs_knob_to_vst_param.json` entries — the
mapping is keyed by the EXACT knob name RS sends, and guessing wrong means
the mapping silently does nothing.

Usage:
    # All gears with knob names (sorted by usage count)
    python3 inspect_rs_knobs.py

    # Only gears matching a substring (case-insensitive)
    python3 inspect_rs_knobs.py --filter eq
    python3 inspect_rs_knobs.py --filter "Bass_Pedal"

    # Detail mode: show sample knob values for one specific gear
    python3 inspect_rs_knobs.py --gear Pedal_EQ8

    # Point at a non-default DB
    python3 inspect_rs_knobs.py --db /path/to/nam_tone.db

Output is grouped per `rs_gear_type`, with each knob name's distinct value
count, min/max, and one example value. The example helps a curator decide
the right scale/offset for the param mapping.
"""

import argparse
import json
import os
import platform
import sqlite3
import sys
from collections import Counter, defaultdict
from pathlib import Path

from common import default_db_path


_default_db_path = default_db_path


def _collect(db_path: Path, gear_filter: str | None) -> dict:
    """Walk preset_pieces and bucket knob names per gear_type.

    Returns: {rs_gear_type: {"songs": int, "knobs": {knob_name: [values]}}}
    """
    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    rows = conn.execute(
        "SELECT rs_gear_type, params_json FROM preset_pieces"
    ).fetchall()
    conn.close()

    out: dict = defaultdict(lambda: {"songs": 0, "knobs": defaultdict(list)})
    for gear_type, params_json in rows:
        if not gear_type:
            continue
        if gear_filter and gear_filter.lower() not in gear_type.lower():
            continue
        try:
            knobs = json.loads(params_json) if params_json else {}
        except (ValueError, TypeError):
            continue
        if not isinstance(knobs, dict):
            continue
        out[gear_type]["songs"] += 1
        for k, v in knobs.items():
            out[gear_type]["knobs"][k].append(v)
    return out


def _summarize_value(values: list) -> str:
    """Compact one-line summary of a knob's observed values."""
    if not values:
        return "(no values)"
    numeric = [v for v in values if isinstance(v, (int, float))]
    if numeric and len(numeric) == len(values):
        return (
            f"min={min(numeric):g}  max={max(numeric):g}  "
            f"n_distinct={len(set(numeric))}  e.g.={numeric[0]:g}"
        )
    # Non-numeric (string / mixed)
    distinct = list(dict.fromkeys(values))[:3]
    return f"n_distinct={len(set(map(str, values)))}  e.g.={distinct}"


def _print_overview(data: dict) -> None:
    """Per-gear listing, sorted by song count desc, then by gear name."""
    if not data:
        print("No preset_pieces rows matched.")
        return
    sorted_gears = sorted(
        data.items(), key=lambda kv: (-kv[1]["songs"], kv[0])
    )
    for gear, info in sorted_gears:
        knob_names = sorted(info["knobs"].keys())
        print(f"\n{gear}    ({info['songs']} song(s))")
        if not knob_names:
            print("  (no knobs)")
            continue
        for kn in knob_names:
            vs = info["knobs"][kn]
            print(f"  {kn:24s}  {_summarize_value(vs)}")


def _print_detail(data: dict, gear: str) -> None:
    info = data.get(gear)
    if not info:
        # try case-insensitive match
        for k, v in data.items():
            if k.lower() == gear.lower():
                gear, info = k, v
                break
    if not info:
        available = sorted(data.keys())
        print(f"No data for gear '{gear}'.")
        if available:
            print(f"Did you mean one of:\n  " + "\n  ".join(available[:20]))
        return
    print(f"\n{gear}  —  {info['songs']} song(s), {len(info['knobs'])} knob(s)")
    for kn in sorted(info["knobs"]):
        vs = info["knobs"][kn]
        print(f"\n  {kn}")
        print(f"    {_summarize_value(vs)}")
        sample = vs[:10]
        print(f"    sample values (first 10): {sample}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", type=Path, default=None,
                    help="Path to nam_tone.db (default: auto-detect)")
    ap.add_argument("--filter", default=None,
                    help="Case-insensitive substring filter on rs_gear_type")
    ap.add_argument("--gear", default=None,
                    help="Show sample knob VALUES for one specific gear")
    args = ap.parse_args()

    db_path = args.db or _default_db_path()
    if not db_path or not db_path.exists():
        print(f"nam_tone.db not found at {db_path}.", file=sys.stderr)
        print("Pass --db <path> if Slopsmith is installed elsewhere.", file=sys.stderr)
        return 1

    print(f"Reading {db_path}")
    data = _collect(db_path, args.filter)
    print(f"Found {len(data)} distinct rs_gear_type(s) in preset_pieces.")

    if args.gear:
        _print_detail(data, args.gear)
    else:
        _print_overview(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
