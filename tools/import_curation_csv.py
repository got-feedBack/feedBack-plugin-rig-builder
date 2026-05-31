#!/usr/bin/env python3
"""Merge a curation CSV into rs_to_real.json.

Workflow:

1. Maintain a shared Google Sheet with curated tone3000 picks per
   Rocksmith gear. Columns (header row required, order flexible):

       rs_gear_type, name, category, level, tone3000_id,
       tone3000_url, rs_gain_lo, rs_gain_hi, notes, curator

2. Export it to CSV (File → Download → Comma Separated Values).

3. Run this script:

       python3 import_curation_csv.py path/to/curation.csv

   It updates `rs_to_real.json` in place:
     - For amps with multiple `level` rows, builds `gain_variants`
       with the ranges from `rs_gain_lo`/`rs_gain_hi` columns.
     - For gear without `level`, sets `tone3000_id` as the single
       default (writes to default_captures.json instead — that's
       the shape rs_gear → {tone3000_id} already used by the
       auto-download flow for non-variant captures).
     - Idempotent: re-running with the same CSV produces the same
       output. Rows with empty `tone3000_id` are skipped (they're
       reminders for the curator, not actionable data).

Safety:
  - Always writes a backup to `<file>.bak` before overwriting.
  - Validates that the rs_gear_type already exists in rs_to_real.json
    (typo guard — won't invent entries from CSV typos).
  - Prints a summary of what changed.

Run from the rig_builder plugin directory or pass --plugin-dir.
"""

import argparse
import csv
import json
import sys
from pathlib import Path

from common import PLUGIN_ROOT


def parse_int(s):
    try:
        return int(str(s).strip())
    except (ValueError, TypeError):
        return None


def parse_range_bound(s):
    """Parse a gain range bound (0-100). Empty/invalid → None."""
    if s is None:
        return None
    s = str(s).strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def merge_csv_into_rs_map(csv_path: Path, rs_map_path: Path, defaults_path: Path) -> dict:
    """Merge curation CSV → rs_to_real.json + default_captures.json.

    Returns a summary dict counting what changed.
    """
    rs_map = json.loads(rs_map_path.read_text())
    defaults = json.loads(defaults_path.read_text()) if defaults_path.exists() else {}

    summary = {
        "rows_read": 0,
        "rows_skipped_empty": 0,
        "rows_skipped_unknown_gear": [],
        "amps_with_variants": [],
        "defaults_added": [],
        "errors": [],
    }

    # Group rows by rs_gear_type so amps with multiple variant rows
    # build a single `gain_variants` block.
    by_gear: dict[str, list[dict]] = {}

    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            summary["rows_read"] += 1
            rs_gear = (row.get("rs_gear_type") or "").strip()
            # Skip comment lines: a row whose first cell starts with `#`
            # is treated as a sheet annotation, not data. CSV doesn't
            # have native comments but `#` at start is a common
            # convention and Google Sheets users like writing notes
            # this way.
            if rs_gear.startswith("#"):
                summary["rows_skipped_empty"] += 1
                continue
            tid = parse_int(row.get("tone3000_id"))
            if not rs_gear or tid is None:
                summary["rows_skipped_empty"] += 1
                continue
            if rs_gear not in rs_map:
                summary["rows_skipped_unknown_gear"].append(rs_gear)
                continue
            by_gear.setdefault(rs_gear, []).append({
                "level": (row.get("level") or "").strip().lower(),
                "tone3000_id": tid,
                "model_id": parse_int(row.get("model_id")),     # optional — pins a SPECIFIC capture inside the tone
                "rs_gain_lo": parse_range_bound(row.get("rs_gain_lo")),
                "rs_gain_hi": parse_range_bound(row.get("rs_gain_hi")),
                "notes": (row.get("notes") or "").strip(),
                "curator": (row.get("curator") or "").strip(),
            })

    # Apply per-gear updates.
    for rs_gear, rows in by_gear.items():
        # Decide: variant amp (any row has a `level`) or single-NAM gear.
        has_levels = any(r["level"] for r in rows)
        if has_levels:
            # Build gain_variants block.
            variants = {}
            for r in rows:
                lvl = r["level"]
                if not lvl:
                    summary["errors"].append(
                        f"{rs_gear}: row without `level` mixed with variant rows — skipped")
                    continue
                lo = r["rs_gain_lo"] if r["rs_gain_lo"] is not None else 0
                hi = r["rs_gain_hi"] if r["rs_gain_hi"] is not None else 100
                variants[lvl] = {
                    "tone3000_id": r["tone3000_id"],
                    "rs_gain_range": [lo, hi],
                }
                if r["model_id"] is not None:
                    variants[lvl]["model_id"] = r["model_id"]
                if r["notes"]:
                    variants[lvl]["notes"] = r["notes"]
                if r["curator"]:
                    variants[lvl]["curator"] = r["curator"]
            if variants:
                rs_map[rs_gear]["gain_variants"] = variants
                summary["amps_with_variants"].append(rs_gear)
        else:
            # Single-NAM gear → write to default_captures.json.
            # One row only (extras are ignored — last one wins).
            r = rows[-1]
            defaults[rs_gear] = {
                "tone3000_id": r["tone3000_id"],
                "kind": "ir" if rs_map[rs_gear].get("category") == "cab" else "nam",
                "model_id": r["model_id"],   # optional; None lets pick_best_model decide by size
            }
            if r["notes"]:
                defaults[rs_gear]["notes"] = r["notes"]
            if r["curator"]:
                defaults[rs_gear]["curator"] = r["curator"]
            summary["defaults_added"].append(rs_gear)

    return summary, rs_map, defaults


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_path", help="Path to the exported curation CSV.")
    ap.add_argument("--plugin-dir", default=str(PLUGIN_ROOT),
                    help="rig_builder plugin directory (default: current dir).")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print what would change without writing.")
    args = ap.parse_args()

    csv_path = Path(args.csv_path)
    if not csv_path.exists():
        print(f"error: {csv_path} not found", file=sys.stderr)
        sys.exit(1)

    plugin_dir = Path(args.plugin_dir).resolve()
    rs_map_path = plugin_dir / "data" / "rs_to_real.json"
    defaults_path = plugin_dir / "data" / "default_captures.json"
    if not rs_map_path.exists():
        print(f"error: {rs_map_path} not found (run with --plugin-dir?)", file=sys.stderr)
        sys.exit(1)

    summary, rs_map, defaults = merge_csv_into_rs_map(csv_path, rs_map_path, defaults_path)

    print("── Summary ──")
    print(f"rows read:             {summary['rows_read']}")
    print(f"rows skipped (empty):  {summary['rows_skipped_empty']}")
    if summary["rows_skipped_unknown_gear"]:
        print(f"unknown rs_gear_type:  {', '.join(set(summary['rows_skipped_unknown_gear']))}")
        print("  (check spelling against rs_to_real.json keys)")
    if summary["amps_with_variants"]:
        print(f"amps with variants ({len(summary['amps_with_variants'])}): {', '.join(summary['amps_with_variants'])}")
    if summary["defaults_added"]:
        print(f"single-NAM defaults ({len(summary['defaults_added'])}): {', '.join(summary['defaults_added'])}")
    if summary["errors"]:
        print("errors:")
        for e in summary["errors"]:
            print(f"  - {e}")

    if args.dry_run:
        print("\n(dry run — nothing written)")
        return

    # Backup + write.
    (plugin_dir / "data").mkdir(parents=True, exist_ok=True)
    if summary["amps_with_variants"]:
        rs_map_path.rename(rs_map_path.with_suffix(".json.bak"))
        rs_map_path.write_text(json.dumps(rs_map, indent=2, ensure_ascii=False) + "\n")
        print(f"wrote {rs_map_path} (backup: {rs_map_path.with_suffix('.json.bak')})")
    if summary["defaults_added"]:
        if defaults_path.exists():
            defaults_path.rename(defaults_path.with_suffix(".json.bak"))
        defaults_path.write_text(json.dumps(defaults, indent=2, ensure_ascii=False) + "\n")
        print(f"wrote {defaults_path}")


if __name__ == "__main__":
    main()
