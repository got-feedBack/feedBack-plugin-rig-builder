#!/usr/bin/env python3
"""Bulk-populate `preset_pieces.vst_state` from each piece's RS knob values.

After `apply_vst_suggestions.py` migrates `kind` to `vst`, every piece has
its target plugin assigned but `vst_state` is NULL — so the plugin plays
with its DEFAULTS (chorus at default rate/depth, EQ flat, etc). The user
has to either click ⇶ Apply RS settings on every slot, or open every
editor by hand.

This script walks every `kind='vst'` row, reads its `params_json` (the
parsed Rocksmith knob values for that gear instance), looks the gear up
in `rs_knob_to_vst_param.json` for the loaded VST's stem, translates each
RS knob → VST param using `scale` + `offset` + `invert`, and writes back
into `vst_state` as the JSON envelope `{"params": {paramId: value, ...}}`.

Why this works for real song playback (not just preview):
  `screen.js::rbReapplyVstParamsAfterLoad` is called inside the fetch
  interceptor that handles every chain load — including the bundle's
  song-load path. After the chain is in the engine, it walks every VST
  stage and calls `api.setParameter(slotId, paramId, value)` for each
  entry in our `{"params": ...}` dict. So a populated `vst_state` IS the
  source of truth for both preview AND real song playback.

Slopsmith does NOT need to be running; UPDATEs are plain SQLite. Restart
Slopsmith (or reload affected songs) to see the change.

Usage:
    # Dry-run: show what would be written and skipped
    python3 apply_vst_state.py

    # Apply (per-category scoping mirrors apply_vst_suggestions.py)
    python3 apply_vst_state.py --category pedal --apply
    python3 apply_vst_state.py --category rack --apply
    python3 apply_vst_state.py --apply        # all categories

    # Re-write even rows that already have a vst_state (default skips them)
    python3 apply_vst_state.py --overwrite --apply

    # Scope to one rs_gear_type (handy for testing a single VST mapping)
    python3 apply_vst_state.py --rs-gear Pedal_EQ8 --apply

What this script does NOT do:
  - Generate the `opaque` blob (only `savePreset()` in the live engine
    can). That's needed if your build of Slopsmith disables the fetch
    interceptor or the post-load setParameter walk. Default rig_builder
    builds include the walk, so `{"params": ...}` is enough.
  - Touch `kind != 'vst'` rows. NAM/IR pieces aren't VSTs.
  - Override a piece whose vst_state already has an `opaque` blob — that
    came from an explicit user 📸 Capture state click and is sacred.
"""

import argparse
import json
import math
import os
import platform
import sqlite3
import sys
from pathlib import Path

from common import PLUGIN_ROOT, DATA_DIR, default_db_path


_PLUGIN_DIR = PLUGIN_ROOT
_default_db_path = default_db_path


# VST param display-value ranges, used to convert curated dB/Hz/etc values to
# the engine's normalized [0,1] range that `api.setParameter` actually
# expects. Engine setParameter ROUND-TRIPS captured 0..1 values from
# `getParameters` (confirmed by inspecting opaque captures in the DB — every
# value is 0..1). So if our curated mapping outputs raw dB (e.g. Compress=48
# × scale=-1 → Threshold=-48), we MUST normalize before writing or the
# engine clamps the out-of-range value to 0 → param sits at its minimum
# instead of the intended display value.
#
# Each entry: vst_stem (lowercased file basename without .vst3/.component)
# → {param_name: (kind, min_display, max_display)} where kind is "linear" or
# "log" (logarithmic mapping for frequency/Q/time params per VST3 convention).
#
# Ranges are sourced from Melda free plugin manuals (MCompressor, MEqualizer,
# MTremolo, MFlanger, MChorus, MFreqShifter, MReverb). Params absent from this
# table fall back to "treat the translated value as already-normalized" =
# clamp to [0,1] — same as the manual ⇶ Apply RS settings path in screen.js.
_VST_PARAM_RANGES: dict[str, dict[str, tuple[str, float, float]]] = {
    "mcompressor": {
        "Gain":        ("linear", -24.0, 24.0),
        "Output gain": ("linear", -24.0, 24.0),
        "Threshold":   ("linear", -80.0,  0.0),
        # NB: Attack / Release / RMS length intentionally omitted. The curator's
        # `scale=0.01` mappings (RS 0-100 → 0-1) already produce normalized
        # values; applying a log range here would re-normalize and yield
        # nonsense (e.g. Attack=0.5 treated as 0.5 ms display → re-norm via
        # log 0.01..1000 → 0.175 actual → ≈0.5 ms display = wrong direction).
        # Threshold + Gain stay because the curator's scale=-1.0 / scale=1.0
        # produces dB values that DO need display→normalized conversion.
        "Ratio":       ("log",     1.0, 100.0),
        "Knee size":   ("linear",  0.0, 100.0),
    },
    "mequalizer": {
        # Top-level (band-agnostic) params
        "Gain":          ("linear", -24.0, 24.0),
        "Dry/Wet":       ("linear",  0.0, 100.0),
        "Soft saturation": ("linear", 0.0, 100.0),
        # Per-band params. Verified Melda free MEqualizer naming via runtime
        # `getParameters` log: each band exposes "<Field> N (EQ N)" — NOT
        # "Band N <Field>". 16 bands total (6 used by curated mappings;
        # remaining 7..16 covered defensively).
        **{f"Gain {i} (EQ {i})":      ("linear", -24.0, 24.0)    for i in range(1, 17)},
        **{f"Frequency {i} (EQ {i})": ("log",     20.0, 20000.0) for i in range(1, 17)},
        **{f"Q {i} (EQ {i})":         ("log",      0.1,   100.0) for i in range(1, 17)},
    },
    "mtremolo": {
        # Rate is the ONE mtremolo param using display-domain (Hz) scaling:
        # the curated Speed→Rate rule outputs Hz (scale 0.09, offset 1.0) and
        # this range normalizes it. Reverse-engineered from the in-plugin
        # readout (normalized 0.60 → 0.9564 Hz ⇒ log[0.01, 20] Hz). Depth /
        # Dry-Wet stay scale=0.01 (already normalized) — deliberately NOT here.
        "Rate": ("log", 0.01, 20.0),
    },
    # AutoSweep (bundled envelope filter): NO ranges — its curated rules emit
    # already-normalized [0,1] param values directly (Attack/Release use the
    # empirical RS-value/1000 scale, not ms; verified: RS Attack 128 → 0.128).
    # Other Melda effects (MFlanger, MChorus, MFreqShifter, MReverb) —
    # intentionally NO entries here. Every curated rs_knob_to_vst_param.json
    # rule for these uses `scale: 0.01` (RS 0-100 → 0-1) which already
    # produces normalized values. Adding a display-range here would
    # double-normalize: e.g. RS Depth=78 × 0.01 = 0.78, then linear-norm
    # via (0..100) → 0.78/100 = 0.0078 → editor shows ≈1%. Verified bug,
    # fixed by removing these entries. If a future curator switches to
    # display-domain scaling (e.g. scale=1.0 for Hz direct), re-add the
    # relevant param ranges then — but the table here MUST stay in sync
    # with whatever the curator's scale outputs.
    # Kilohearts free Essentials — KHS plugins expose almost everything as
    # 0..100% sliders already, so most curated mappings (scale=0.01) land in
    # [0,1] without help. Only the dB/Hz params need ranges here.
    "khs compressor": {
        # Ranges reverse-engineered from in-plugin readouts (RS value with the
        # given scale → displayed value), so a literal RS value maps 1:1 to the
        # display when the curator scale is 1.0:
        #   Threshold linear: norm 0.166→-32.33 dB, 0.7→-7.80 dB ⇒ [-40, +6].
        #   Attack/Release log: norm 0.22→3.92 ms, 0.50→22.4 ms ⇒ [1, 500] ms.
        #   Ratio log [1,100]: literal RS ratio (1→1:1, was passing 1.0→"Inf:1").
        "Threshold":   ("linear", -40.0, 6.0),
        "Makeup gain": ("linear", -24.0, 24.0),
        "Ratio":       ("log",     1.0, 100.0),
        "Attack":      ("log",     1.0, 500.0),
        "Release":     ("log",     1.0, 500.0),
    },
    "khs 3-band eq": {
        "Low Gain":    ("linear", -24.0, 24.0),
        "Mid Gain":    ("linear", -24.0, 24.0),
        "High Gain":   ("linear", -24.0, 24.0),
        "Low Freq":    ("log",    20.0, 1000.0),
        "High Freq":   ("log",   1000.0, 20000.0),
    },
    # No khs chorus entries — RS Rate maps directly via curator scale=0.01
    # (RS 0-100 → 0-1 normalized). User wants the slider POSITION to track
    # the RS Rate value (e.g. RS=4 → slider at 4%). The plugin's internal
    # Rate→display conversion handles the rest.
}


def _normalize_display(value: float, kind: str, lo: float, hi: float) -> float:
    """Map a display-domain value to the engine's normalized [0,1].

    `linear`: (v - lo) / (hi - lo) — clamped.
    `log`:    log(v/lo) / log(hi/lo) — clamped; v <= 0 falls to 0.
    """
    if kind == "log":
        if value <= 0 or lo <= 0 or hi <= lo:
            return 0.0
        v = max(lo, min(hi, value))
        return math.log(v / lo) / math.log(hi / lo)
    # linear
    if hi == lo:
        return 0.0
    return (value - lo) / (hi - lo)


def _vst_stem(vst_path: str) -> str:
    """Mirror the case-insensitive stem the runtime uses for table lookup.

    `/Library/.../MTremolo.vst3` → `mtremolo`
    `/Library/.../kHs Chorus.vst3` → `khs chorus`
    """
    name = Path(vst_path).name
    for ext in (".vst3", ".component"):
        if name.lower().endswith(ext):
            name = name[:-len(ext)]
            break
    return name.lower()


def _translate_one_knob(rs_value, mapping: dict, vst_stem: str) -> tuple[str, float] | None:
    """Apply one mapping entry (param/scale/offset/invert) to an RS value.

    Returns (param_name, normalized_value) or None if the entry is malformed.
    The `param` field in the mapping is the human-readable VST param NAME
    (e.g. "Rate"). The runtime's setParameter walker
    (`rbReapplyVstParamsToChain` in screen.js) resolves NAME → numeric
    paramId via `getParameters()` per-slot at apply time — so storing
    the name (durable across plugin versions) is the right pivot.

    Value normalization (NEW): the engine's `setParameter` takes a [0,1]
    normalized value, not the display-domain value. If the (vst_stem,
    param_name) pair appears in _VST_PARAM_RANGES we map display →
    normalized here. Otherwise we treat the translated value as already
    normalized and clamp to [0,1] — mirrors the manual ⇶ Apply RS settings
    path in screen.js which has the same clamp.
    """
    try:
        v = float(rs_value)
    except (ValueError, TypeError):
        return None
    scale = float(mapping.get("scale", 1.0))
    offset = float(mapping.get("offset", 0.0))
    out = v * scale + offset
    if mapping.get("invert"):
        out = 1.0 - out
    param_name = mapping.get("param")
    if not isinstance(param_name, str):
        return None
    # If the curator declared a display-domain range, normalize. Else
    # assume `out` is already normalized (curator chose scale=0.01 etc.).
    rng = _VST_PARAM_RANGES.get(vst_stem, {}).get(param_name)
    if rng:
        kind, lo, hi = rng
        out = _normalize_display(out, kind, lo, hi)
    out = max(0.0, min(1.0, out))
    return (param_name, out)


def _build_params_for_piece(
    rs_gear: str, vst_path: str, params_json: str,
    knob_table: dict,
) -> tuple[dict, list] | None:
    """Return ({param_name: value, ...}, [skipped_rs_knobs])
    for one preset_piece, using the curated translation table.
    """
    stem = _vst_stem(vst_path)
    gear_block = knob_table.get(rs_gear) or {}
    vst_block = gear_block.get(stem) or {}
    if not vst_block:
        return None  # no translation for this (rs_gear × vst) pair
    try:
        knobs = json.loads(params_json) if params_json else {}
    except (ValueError, TypeError):
        return None
    if not isinstance(knobs, dict):
        return None
    out: dict = {}
    skipped: list = []
    # Graphic-EQ fold — MEqualizer (free) has only 6 bands, but RS graphic EQs
    # (EQ8 / Bass EQ8) carry up to 8 fixed-frequency bands. A `_graphic_eq`
    # block folds them into <=6 target bands: each target pins a center
    # Frequency (Hz) and takes the AVERAGE gain of the RS knobs assigned to it
    # (so a merged pair lands at their geometric-mean freq with mean gain).
    # This fully defines the MEqualizer output; the per-knob loop is skipped.
    geq = vst_block.get("_graphic_eq")
    if isinstance(geq, list) and geq:
        frng = _VST_PARAM_RANGES.get(stem, {}).get("Frequency 1 (EQ 1)") or ("log", 20.0, 20000.0)
        grng = _VST_PARAM_RANGES.get(stem, {}).get("Gain 1 (EQ 1)") or ("linear", -24.0, 24.0)
        for i, band in enumerate(geq[:16], 1):
            try:
                freq = float(band.get("freq"))
            except (ValueError, TypeError):
                continue
            gains = []
            for k in (band.get("rs") or []):
                try:
                    gains.append(float(knobs[k]))
                except (KeyError, ValueError, TypeError):
                    pass
            avg = sum(gains) / len(gains) if gains else 0.0
            out[f"Frequency {i} (EQ {i})"] = _normalize_display(freq, *frng)
            out[f"Gain {i} (EQ {i})"] = _normalize_display(avg, *grng)
            out[f"Enable {i} (EQ {i})"] = 1.0
        return (out, skipped)
    # Static defaults first — `_static` block in the mapping holds
    # curator-pinned params applied regardless of RS knobs (e.g.
    # kHs Distortion Mode + Dynamics, so every fuzz pedal sounds fuzzy
    # without needing per-pedal Mode knob mapping). Already-normalized
    # values in [0,1] — clamped defensively. RS-knob translations below
    # may override these (rare, but explicit win).
    static_block = vst_block.get("_static")
    if isinstance(static_block, dict):
        for pname, pvalue in static_block.items():
            try:
                v = float(pvalue)
            except (ValueError, TypeError):
                continue
            # If the param has a declared display-domain range (e.g. a graphic
            # EQ band Frequency in Hz), normalize it the same way RS-knob values
            # are. Otherwise it's already a normalized [0,1] value (Enable,
            # Mode, Dynamics…) and passes through clamped.
            rng = _VST_PARAM_RANGES.get(stem, {}).get(pname)
            if rng:
                kind, lo, hi = rng
                v = _normalize_display(v, kind, lo, hi)
            out[pname] = max(0.0, min(1.0, v))
    for rs_knob, rs_value in knobs.items():
        m = vst_block.get(rs_knob)
        if not isinstance(m, dict) or "param" not in m:
            skipped.append(rs_knob)
            continue
        translated = _translate_one_knob(rs_value, m, stem)
        if translated is None:
            skipped.append(rs_knob)
            continue
        param_name, value = translated
        out[param_name] = value
    return (out, skipped)


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--db", type=Path, default=None,
                    help="Path to nam_tone.db (default: auto-detect)")
    ap.add_argument("--category", default=None,
                    help="Filter by rs_to_real.json category ('pedal' / 'rack' / 'amp'); "
                         "default = all VST pieces regardless of category")
    ap.add_argument("--rs-gear", default=None,
                    help="Scope to one specific rs_gear_type (for testing)")
    ap.add_argument("--overwrite", action="store_true",
                    help="Re-write rows that already have a vst_state. Skipped by default "
                         "(an existing state likely came from 📸 Capture or a prior run).")
    ap.add_argument("--apply", action="store_true",
                    help="Actually write the changes (otherwise dry-run only)")
    args = ap.parse_args()

    db_path = args.db or _default_db_path()
    if not db_path or not db_path.exists():
        print(f"nam_tone.db not found at {db_path}.", file=sys.stderr)
        return 1

    knob_table = json.loads((DATA_DIR / "rs_knob_to_vst_param.json").read_text())
    knob_table = {k: v for k, v in knob_table.items() if not k.startswith("_")}
    rs_map = json.loads((DATA_DIR / "rs_to_real.json").read_text())

    # Filter to gears matching --category (or --rs-gear)
    if args.rs_gear:
        scope_gears = {args.rs_gear}
    elif args.category:
        scope_gears = {
            k for k, v in rs_map.items()
            if isinstance(v, dict) and v.get("category", "").lower() == args.category.lower()
        }
    else:
        scope_gears = None  # all

    conn = sqlite3.connect(f"file:{db_path}?{'' if args.apply else 'mode=ro'}", uri=True)
    rows = conn.execute(
        "SELECT id, rs_gear_type, vst_path, params_json, vst_state "
        "FROM preset_pieces "
        "WHERE kind = 'vst' AND vst_path IS NOT NULL AND vst_path != ''"
    ).fetchall()

    by_gear_vst: dict = {}      # (rs_gear, vst_stem) -> {written, skipped_no_knobs, skipped_no_mapping, skipped_has_state}
    plan: list = []              # (row_id, rs_gear, vst_path, new_vst_state)
    no_mapping: dict = {}        # (rs_gear, vst_stem) -> count

    for row_id, rs_gear, vst_path, params_json, existing_state in rows:
        if scope_gears is not None and rs_gear not in scope_gears:
            continue
        stem = _vst_stem(vst_path or "")
        key = (rs_gear, stem)
        bucket = by_gear_vst.setdefault(
            key, {"written": 0, "no_knobs": 0, "no_mapping": 0, "has_state": 0, "no_params": 0},
        )

        # Decide whether to skip based on what the existing vst_state contains.
        #   - opaque blob present → ALWAYS preserve (a 📸 Capture click; sacred,
        #     even with --overwrite — that blob is the only thing that
        #     round-trips perfectly through the engine)
        #   - params-only         → skip by default, rewrite with --overwrite
        #     (legacy params written before the dB→normalized fix are wrong)
        if existing_state:
            try:
                ex = json.loads(existing_state)
                if isinstance(ex, dict):
                    if ex.get("opaque"):
                        bucket["has_state"] += 1
                        continue
                    if ex.get("params") and not args.overwrite:
                        bucket["has_state"] += 1
                        continue
            except (ValueError, TypeError):
                pass

        result = _build_params_for_piece(rs_gear, vst_path or "", params_json or "", knob_table)
        if result is None:
            no_mapping[key] = no_mapping.get(key, 0) + 1
            bucket["no_mapping"] += 1
            continue
        params_by_name, skipped = result
        if not params_by_name:
            bucket["no_params"] += 1
            continue
        new_state = json.dumps({"params": params_by_name})
        plan.append((row_id, rs_gear, vst_path, new_state))
        bucket["written"] += 1

    # ── Report ──
    print(f"\n{'rs_gear_type':30s}  {'vst_stem':24s}  written  has_state  no_mapping  no_params")
    print("-" * 102)
    total_w = total_h = total_nm = total_np = 0
    for (rs_gear, stem), b in sorted(by_gear_vst.items()):
        if not (b["written"] or b["has_state"] or b["no_mapping"] or b["no_params"]):
            continue
        print(f"{rs_gear:30s}  {stem:24s}  {b['written']:7d}  {b['has_state']:9d}  {b['no_mapping']:10d}  {b['no_params']:9d}")
        total_w += b["written"]; total_h += b["has_state"]
        total_nm += b["no_mapping"]; total_np += b["no_params"]
    print("-" * 102)
    print(f"{'TOTAL':30s}  {'':24s}  {total_w:7d}  {total_h:9d}  {total_nm:10d}  {total_np:9d}")
    print()
    print("Legend:")
    print("  written     — would write a {\"params\": ...} state")
    print("  has_state   — already has a state, preserved (use --overwrite to replace)")
    print("  no_mapping  — no entry in rs_knob_to_vst_param.json for this (gear, vst) pair")
    print("  no_params   — gear/vst mapping exists but the piece's params_json has no matching knobs")

    if not args.apply:
        print("\n(dry-run; pass --apply to actually write the changes)")
        return 0

    if not plan:
        print("\nNothing to apply.")
        return 0

    print("\nApplying…")
    with conn:
        for row_id, _rs_gear, _vst_path, new_state in plan:
            conn.execute(
                "UPDATE preset_pieces SET vst_state = ? WHERE id = ?",
                (new_state, row_id),
            )
    print(f"Done: {len(plan)} preset_pieces row(s) updated.")
    print("Reload the affected songs in Slopsmith to see the change (the post-load")
    print("setParameter walk runs each time the chain is built).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
