#!/usr/bin/env python3
"""Bulk-assign the curated PRIMARY VST suggestion to every preset_piece.

The catalog files (`rs_gear_to_vst.json` + `rs_knob_to_vst_param.json`)
only SUGGEST plugins — they don't change the actual chain. Each Rocksmith
tone in `nam_tone.db` keeps `kind='nam'` on its pedal slots until the
user manually clicks ⚙ VST + Assign in the Gear catalog. For 27 pedals ×
hundreds of songs, that's not workable.

This script walks the chain table and, for every preset_pieces row that:
    rs_gear_type ∈ <a pedal/rack/...whatever scope you choose>
    kind = 'nam'                          (don't touch existing VST/IR)
    assigned_mode NOT IN ('manual','manual_vst')   (don't overwrite a
                                          deliberate user pick)

…replaces it with the PRIMARY entry from `rs_gear_to_vst.json` if the
recommended VST is actually installed on this machine. Marks the new
row `assigned_mode='manual_vst'` so future auto-batches treat it as
sacred. Mirrors the `/vst/assign` endpoint's UPDATE exactly.

Slopsmith does NOT need to be running — the UPDATE is plain SQLite.
Slopsmith should be RESTARTED (or the song re-loaded) after running.

What this script does NOT do:
  - Capture each plugin's opaque state blob. Real-song playback restores
    a VST's params from `vst_state.opaque` (a `savePreset()` blob) — we
    can only generate that by actually loading the plugin in the engine.
    Until you open the inline editor once per piece (or click ⇶ Apply
    RS settings), the VSTs play with their DEFAULTS. The bulk-assign
    is still a strict upgrade vs the NAM that came before for pedals
    (where NAM doesn't model the FX at all).
  - Recompute `presets.model_file`. Pedals are never the primary
    `model_file` (only amp/rack slots are — see `_MODEL_SLOT_PRIORITY`)
    so this is a no-op for the pedal scope. If you ever extend the
    script to amp/rack slots, add a recompute pass.

Usage:
    # See what WOULD change (default, no writes)
    python3 apply_vst_suggestions.py

    # Limit to a specific category from rs_to_real.json
    python3 apply_vst_suggestions.py --category pedal

    # Scope to one specific gear (handy for testing)
    python3 apply_vst_suggestions.py --rs-gear Pedal_Tremolo

    # Actually write the changes (after a dry-run review)
    python3 apply_vst_suggestions.py --category pedal --apply

    # Point at a non-default DB / config dir
    python3 apply_vst_suggestions.py --db /path/to/nam_tone.db
"""

import argparse
import json
import os
import platform
import sqlite3
import sys
from pathlib import Path

from common import PLUGIN_ROOT, DATA_DIR, default_db_path


_PLUGIN_DIR = PLUGIN_ROOT
_default_db_path = default_db_path


# ── Path helpers ────────────────────────────────────────────────────────

def _vst_search_roots() -> list[Path]:
    """Standard VST3 + AU install locations per platform."""
    system = platform.system()
    if system == "Darwin":
        return [
            Path("/Library/Audio/Plug-Ins/VST3"),
            Path.home() / "Library/Audio/Plug-Ins/VST3",
            Path("/Library/Audio/Plug-Ins/Components"),
            Path.home() / "Library/Audio/Plug-Ins/Components",
        ]
    if system == "Windows":
        common = Path(os.environ.get("CommonProgramFiles", r"C:\Program Files\Common Files"))
        return [common / "VST3"]
    return [Path.home() / ".vst3"]


def _find_vst(name: str, roots: list[Path]) -> tuple[Path, str] | None:
    """Locate a plugin file matching `name` (e.g. 'MTremolo' or 'kHs Chorus').

    Returns (absolute_path, format). format ∈ {'VST3', 'AudioUnit'}.
    Search is recursive (depth 3) — Melda nests under category subdirs.
    """
    candidates_vst3 = [f"{name}.vst3"]
    candidates_au = [f"{name}.component"]
    for root in roots:
        if not root.exists():
            continue
        # Recursive walk, capped depth
        for entry in root.rglob("*"):
            try:
                rel_parts = entry.relative_to(root).parts
            except ValueError:
                continue
            if len(rel_parts) > 3:
                continue
            n = entry.name
            if n in candidates_vst3:
                return (entry, "VST3")
            if n in candidates_au:
                return (entry, "AudioUnit")
    return None


# ── Catalog loaders ─────────────────────────────────────────────────────

def _load_rs_map() -> dict:
    with open(DATA_DIR / "rs_to_real.json") as f:
        return json.load(f)


def _load_vst_catalog() -> dict:
    with open(DATA_DIR / "rs_gear_to_vst.json") as f:
        return json.load(f)


def _primary_vst_for(rs_gear: str, catalog: dict) -> dict | None:
    """First entry in the catalog's list for `rs_gear` (the curated primary)."""
    entry = catalog.get(rs_gear)
    if not isinstance(entry, list) or not entry:
        return None
    return entry[0]


# ── Main pass ───────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--db", type=Path, default=None,
                    help="Path to nam_tone.db (default: auto-detect)")
    ap.add_argument("--category", default="pedal",
                    help="rs_to_real.json category to scope to ('pedal' [default], 'rack', 'amp', or 'all')")
    ap.add_argument("--rs-gear", default=None,
                    help="Scope to a single rs_gear_type (for testing)")
    ap.add_argument("--apply", action="store_true",
                    help="Actually write the changes (otherwise dry-run only)")
    ap.add_argument("--force", action="store_true",
                    help="Override rows marked assigned_mode='manual' (manual NAM picks). "
                         "Never overrides assigned_mode='manual_vst' — a deliberate VST pick "
                         "is always preserved.")
    ap.add_argument("--include-none", action="store_true", default=True,
                    help="Also migrate kind='none' rows (pending slots with no file). "
                         "Default true — purely additive, no NAM is lost.")
    args = ap.parse_args()

    db_path = args.db or _default_db_path()
    if not db_path or not db_path.exists():
        print(f"nam_tone.db not found at {db_path}.", file=sys.stderr)
        return 1

    rs_map = _load_rs_map()
    vst_catalog = _load_vst_catalog()
    vst_roots = _vst_search_roots()

    # Build the working list: every rs_gear_type matching the scope filter.
    if args.rs_gear:
        scope = [args.rs_gear]
    else:
        scope = []
        cat = args.category.lower()
        for k, v in rs_map.items():
            if not isinstance(v, dict):
                continue
            if cat != "all" and v.get("category", "").lower() != cat:
                continue
            scope.append(k)

    print(f"Scanning {len(scope)} {args.category} gear type(s) against the catalog.")

    plan = []  # list of (rs_gear, vst_path, vst_format, suggested_name, candidate_rows)
    missing = []  # (rs_gear, suggested_name) where the plugin isn't installed
    no_suggestion = []  # rs_gears with no entry in vst_catalog

    conn = sqlite3.connect(f"file:{db_path}?{'mode=ro' if not args.apply else ''}", uri=True)

    for rs_gear in scope:
        primary = _primary_vst_for(rs_gear, vst_catalog)
        if not primary:
            no_suggestion.append(rs_gear)
            continue
        name = primary.get("name") or ""
        found = _find_vst(name, vst_roots)
        if not found:
            missing.append((rs_gear, name))
            continue
        vst_path, vst_format = found
        # Build the WHERE clause once for this gear's preview + write.
        kind_clause = "kind IN ('nam','none','')" if args.include_none else "kind = 'nam'"
        # `manual_vst` is ALWAYS preserved; `manual` (NAM picks) is only
        # preserved without --force.
        mode_clause = (
            "(assigned_mode IS NULL OR assigned_mode != 'manual_vst')"
            if args.force else
            "(assigned_mode IS NULL OR assigned_mode NOT IN ('manual','manual_vst'))"
        )
        where = f"rs_gear_type = ? AND {kind_clause} AND {mode_clause}"
        rows = conn.execute(
            f"SELECT COUNT(*) FROM preset_pieces WHERE {where}", (rs_gear,)
        ).fetchone()
        candidate_rows = rows[0] if rows else 0
        if candidate_rows == 0:
            continue  # nothing to do for this gear
        plan.append((rs_gear, str(vst_path), vst_format, name, candidate_rows, where))

    # ── Report ──
    print()
    print(f"{'rs_gear_type':30s}  {'suggested VST':28s}  {'fmt':10s}  rows")
    print("-" * 80)
    total = 0
    for rs_gear, vst_path, vst_format, name, n, _w in sorted(plan):
        print(f"{rs_gear:30s}  {name:28s}  {vst_format:10s}  {n}")
        total += n
    print("-" * 80)
    print(f"{'TOTAL':30s}  {'':28s}  {'':10s}  {total}")
    if args.force:
        print("\n⚠ --force is ON: rows with assigned_mode='manual' (manual NAM picks) "
              "are also being overridden.")
    if missing:
        print(f"\nNot installed on this machine ({len(missing)} gears skipped):")
        for rs_gear, name in sorted(missing):
            print(f"  {rs_gear:30s}  needs: {name}")
    if no_suggestion:
        print(f"\nNo entry in rs_gear_to_vst.json ({len(no_suggestion)} gears):")
        for rs_gear in sorted(no_suggestion)[:20]:
            print(f"  {rs_gear}")
        if len(no_suggestion) > 20:
            print(f"  …and {len(no_suggestion) - 20} more")

    if not args.apply:
        print("\n(dry-run; pass --apply to actually write the changes)")
        return 0

    if not plan:
        print("\nNothing to apply.")
        return 0

    # ── Write ──
    print("\nApplying…")
    updated = 0
    with conn:  # transaction
        for rs_gear, vst_path, vst_format, _name, _n, where in plan:
            cur = conn.execute(
                f"UPDATE preset_pieces SET "
                f"  kind = 'vst', file = NULL, "
                f"  vst_path = ?, vst_format = ?, vst_state = NULL, "
                f"  assigned_mode = 'manual_vst' "
                f"WHERE {where}",
                (vst_path, vst_format, rs_gear),
            )
            updated += cur.rowcount
    print(f"Done: {updated} preset_pieces row(s) updated.")
    print("Restart Slopsmith (or reload the affected songs) to see the change.")
    print("VSTs play at plugin defaults until you open each editor once "
          "(or click ⇶ Apply RS settings per piece) to capture opaque state.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
