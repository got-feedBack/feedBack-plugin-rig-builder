"""NAM Rig Builder — map the game tones to NAM presets via tone3000.

Sits between the `tones` plugin (which parses gear chains out of CDLC)
and the bundled `nam_tone` plugin (which owns the preset database and
audio engine bindings). This plugin:

  1. Parses the game tone chains from sloppak files.
  2. Translates each gear piece to a real make/model via rs_to_real.json
     (ships pre-generated with the plugin in data/).
  3. Either suggests tone3000 captures (if an API key is present) or
     gives the user a deep-link to tone3000's web search.
  4. Persists the chosen amp .nam / cab IR into nam_tone.db so the
     NAM runtime picks them up automatically when playing the song.

A `preset_pieces` table extends nam_tone.db with the full the game
chain (pedals/racks/etc.) — informational in v0; the runtime today
only consumes the primary amp/cab pair encoded in `presets`.
"""

import base64
import html
import json
import logging
import math
import os
import re
import secrets
import shutil
import sqlite3
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

from fastapi import Body, Request, UploadFile, File
from fastapi.responses import HTMLResponse, JSONResponse, Response

log = logging.getLogger("slopsmith.plugin.rig_builder")

_plugin_dir = Path(__file__).parent

# the game gear photos live in per-category subdirs under `assets/`
# (e.g. `assets/amp_photos/`). We accept several naming conventions
# because the layout has changed over time — `amp_photos/` (current),
# `guitar_amp_photos/`
# (when guitar + bass amps are kept separate), plus an optional
# `bass_amp_photos/`. The lookup falls through to whichever exists.
#
# `_GEAR_PHOTO_BASES` are tried in order: the new `assets/` location
# first, then the legacy flat plugin-folder location so older installs
# with photos in the flat layout keep working.
_GEAR_PHOTO_DIRS = (
    "amp_photos", "guitar_amp_photos", "bass_amp_photos",
    "pedal_photos", "rack_photos", "cab_photos",
)
_GEAR_PHOTO_ASSETS_SUBDIR = "assets"


def _gear_photo_write_base() -> Path:
    """Writable per-user dir used for gear photos.

    In a packaged build the plugin dir is read-only (AppImage squashfs /
    asar mount), so writing into the bundled `assets/` raises
    OSError(Errno 30) and silently drops every written photo (issue #34).
    setup() points RIG_BUILDER_DATA_DIR at a writable per-user dir (its
    `data/` subdir); we put photos in a sibling `assets/` under the same
    per-user root. When the override isn't set (source / standalone tool
    runs) the bundled `assets/` is itself writable, so keep using it.
    """
    override = os.environ.get("RIG_BUILDER_DATA_DIR")
    if override:
        return Path(override).parent / _GEAR_PHOTO_ASSETS_SUBDIR
    return _plugin_dir / _GEAR_PHOTO_ASSETS_SUBDIR


def _gear_photo_bases() -> tuple[Path, ...]:
    """Ordered, de-duplicated dirs `_find_gear_photo` scans for photos:
    the writable per-user `assets/` first (per-user photos win),
    then the bundled `assets/`, then the legacy flat plugin-folder layout."""
    ordered = (
        _gear_photo_write_base(),
        _plugin_dir / _GEAR_PHOTO_ASSETS_SUBDIR,
        _plugin_dir,
    )
    seen: set[Path] = set()
    out: list[Path] = []
    for b in ordered:
        if b not in seen:
            seen.add(b)
            out.append(b)
    return tuple(out)


def _find_gear_photo(rs_gear: str) -> Path | None:
    """Locate the PNG for a game gear, if a photo is present on disk.
    Photos are named `<rs_gear> - <name>.png`, so the primary lookup is
    a prefix match across every known photo dir.

    Two layers of fuzziness needed because the rs_gear "spelling" differs
    between data sources:

    1. **Case-insensitive comparison**. The DB stores brand-prefixed
       codenames in ALL CAPS (`Bass_Cab_EDEND212XLT`,
       `Cab_MARSHALL1960A`) but the photo filenames and rs_to_real
       use the proper case (`Bass_Cab_EdenD212XLT`,
       `Cab_Marshall1960A`). The mismatch silently dropped every
       branded cab from the photo lookup.
    2. **Cab base-form fallback**. Songs reference cabs by the BASE
       rs_gear (e.g. `Bass_Cab_AT1150BC`) but the photos are per
       mic-position variant (`Bass_Cab_AT1150BC_5c - ...png`, `_5e`, …
       — same texture). When the exact prefix has no hit, accept the
       first `<rs_gear>_<short>` variant we find. The "short" guard
       (a ` - ` separator within 3 chars of the underscore) keeps
       `Cab_TW40` from accidentally matching `Cab_TW400_5c`.

    Returns None when no photo exists (UI falls back to a placeholder).
    """
    if not rs_gear:
        return None
    prefix = f"{rs_gear} - ".lower()
    variant_prefix = f"{rs_gear}_".lower()
    fallback: Path | None = None
    # Candidate dirs: the writable per-user `assets/<sub>` first, then the
    # bundled `assets/<sub>`, then the legacy flat `<sub>` for older installs.
    bases = _gear_photo_bases()
    for d in (base / sub for base in bases for sub in _GEAR_PHOTO_DIRS):
        if not d.is_dir():
            continue
        try:
            for p in d.iterdir():
                if not p.is_file() or p.suffix.lower() != ".png":
                    continue
                name_lc = p.name.lower()
                if name_lc.startswith(prefix):
                    return p
                if fallback is None and name_lc.startswith(variant_prefix):
                    rest = name_lc[len(variant_prefix):]
                    sep = rest.find(" - ")
                    if 0 < sep <= 3:
                        fallback = p
        except OSError:
            continue
    return fallback


# Module-level singletons populated by setup(). They start as None so
# tests / linting on import don't blow up; _require_*() helpers turn
# accidental pre-setup access into clear errors instead of AttributeError.
_config_dir: Path | None = None
_get_dlc_dir = None
_get_sloppak_cache_dir = None
_db_path: str | None = None
_conn: sqlite3.Connection | None = None
_lock = threading.Lock()
# Guards the one-time connection creation + schema migrations in
# _get_conn(). Separate from `_lock` because the migrations themselves
# take `_lock` (via _get_master_preset_id) — see _get_conn.
_conn_init_lock = threading.Lock()

# Rig Builder reads open song packs — `.sloppak` (original) and `.feedpak`
# (newer name for the same on-disk layout: a zip / directory holding
# manifest.yaml + arrangements/*.json). The host `sloppak` module loads both
# identically (it resolves a pack by structure, not by extension), so every
# song-reading path here accepts either extension. Per-song gear→tone mapping
# then works for feedpak songs once the format carries the tone descriptors
# (until then the default-tone fallback still applies). See issue #48.
_SONG_PACK_SUFFIXES = frozenset((".sloppak", ".feedpak"))


def _is_song_pack(path) -> bool:
    """True when `path` is a readable song pack (sloppak or feedpak)."""
    try:
        return path.suffix.lower() in _SONG_PACK_SUFFIXES
    except AttributeError:
        return False

# rs_to_real / default_captures / rs_cab_to_ir / rs_cab_mic_map are now
# cached by filename in `_json_cache` via `_load_cached_json` (see below).
_settings: dict | None = None  # tone3000_api_key, min_downloads, aggressive
# Serializes the load-merge-write cycle on the settings file so a
# concurrent _save_settings / _persist_tokens pair can't drop updates.
_settings_lock = threading.Lock()

# tone3000 client is recreated when the user updates settings, so we
# wrap construction in a lock to avoid two threads racing during a
# concurrent batch + settings change.
_t3k_client = None
_t3k_lock = threading.Lock()

# Pending OAuth authorizations, keyed by the `state` we send to tone3000.
# Each entry holds the PKCE verifier + the exact redirect_uri used (the
# token exchange must echo it back). Pruned by age on each /oauth/start.
# In-memory is fine: the backend is a single uvicorn worker, and an
# interrupted flow just means the user clicks "Connect" again.
_oauth_pending: dict[str, dict] = {}
_oauth_lock = threading.Lock()
_OAUTH_PENDING_TTL = 600  # seconds a started auth stays valid

# Background batch job state. The web layer polls _batch_state via
# /batch_status; only the worker thread mutates it (under _batch_lock).
_batch_state: dict = {
    "running": False,
    "progress": 0,
    "total": 0,
    "log": [],
    "assigned": 0,
    "skipped": 0,
    "pending": [],
    "started_at": None,
    "finished_at": None,
}
_batch_lock = threading.Lock()
_batch_thread: threading.Thread | None = None

# Serializes read-modify-write cycles on rs_to_real.json (amp_variants
# CRUD, override_query) so concurrent writes can't corrupt the file.
_rs_map_lock = threading.Lock()

# Live state for the curated-variants preload. Lives outside the batch
# worker because it runs against rs_to_real.json directly, not the
# library's songs. Mutated only by the preload thread (under
# _preload_lock); the polling GET /preload_status returns a snapshot.
_preload_state: dict = {
    "running": False,
    "total": 0,
    "done": 0,
    "current": "",          # human label of the variant being fetched
    "downloaded": 0,
    "already_present": 0,
    "wired": 0,
    "failed": [],           # human strings, each ending in a reason
    "failed_permanent": 0,  # subset of failed that re-running won't fix (404)
    "errors": [],
    "started_at": None,
    "finished_at": None,
}
_preload_lock = threading.Lock()
_preload_thread: threading.Thread | None = None


# ── Constants ────────────────────────────────────────────────────────

# Settings file inside the slopsmith config dir. Kept separate from
# nam_tone.db because it's user preferences (with secrets) rather than
# tone data, and so a corrupted DB doesn't lose the API key.
_SETTINGS_FILENAME = "rig_builder_settings.json"
# Legacy filename — migrated to the new one on first init so users coming
# from the old `nam_rig_builder` plugin keep their tone3000 API key,
# preferred_size, disk budget, etc. without having to re-enter them.
_LEGACY_SETTINGS_FILENAME = "nam_rig_builder_settings.json"

_DEFAULT_SETTINGS = {
    "tone3000_api_key": "",             # secret key (advanced/legacy Bearer)
    # OAuth tokens — minted by the "Connect with tone3000" login flow.
    # Preferred over the secret key when present.
    "tone3000_access_token": "",
    "tone3000_refresh_token": "",
    "tone3000_token_expires_at": 0,
    "tone3000_username": "",
    "min_downloads": 50,
    "aggressive": False,
    # Curated-only mode. When True, the batch + per-song auto-assign
    # ONLY pull captures from the user's curated 1-to-1 mappings
    # (rs_to_real.json `gain_variants` for amps + default_captures.json
    # for everything else). Existing assignments are still reused so a
    # manual swap propagates across the library. Anything without a
    # curated pick lands in "pending" instead of triggering a tone3000
    # search — the curator picks it later via Gear → 📚 Library or
    # 🎚 Variants. Default True now that the shipped rs_to_real.json
    # has full coverage; users who prefer the legacy tone3000 fuzzy
    # fallback can flip it off from Setup.
    "curated_only": True,
    # v3 auto-download knobs.
    "preferred_size": "standard",       # standard | lite | feather | nano
    "auto_download": True,              # batch downloads files instead of just recording IDs
    "disk_budget_mb": 2000,             # stop batch downloads once we cross this many MB
    "auto_watch": True,                 # background watcher auto-downloads songs as they materialize
    # Experimental — see screen.js RbMegaChain. When true, rig_builder takes
    # over the entire tone-switching flow: at song load it pre-builds a
    # mega-chain holding every tone's stages, then switches via setBypass
    # instead of clearChain+loadPreset. Eliminates the tone-change transient
    # at the cost of higher steady-state memory and CPU (every NAM stays
    # loaded). Requires the bundle's AMP to be off (we drive the engine
    # ourselves). Roll out only after the cooperative mute-parche flow is
    # confirmed stable on the user's hardware.
    # Chain preloader is now the default playback path (2026-05-28).
    # Eliminates the tone-change spike entirely by holding every tone's
    # stages in memory and toggling bypass between them, instead of
    # reload + clearChain on every switch. Memory cost is a few MB per
    # extra NAM but on M-series Macs it's a clear win. Users on weak
    # x86 hardware can flip it off in Settings.
    "mega_chain_mode": True,
    # When on, the user's default tone (assembled in Gear → Default tone) is
    # loaded into the engine at startup and re-loaded whenever they leave a
    # song or stop a Listen preview — so the idle/menu sound is the chosen
    # default rather than whatever tone happened to load last. On by default
    # (seeded with the Sampleg SBT-CL amp), so a fresh install plays an idle rig.
    "default_tone_enabled": True,
    # Master on/off for the whole Rig Builder engine integration. When False,
    # Rig Builder loads NOTHING into the shared audio engine — no per-song
    # mega-chain, no idle default tone, no preview — so a user who doesn't want
    # to use Rig Builder can turn it off from Gear and their own Audio-menu
    # signal chain is left completely untouched. On by default.
    "rig_builder_enabled": True,
    # NAM loudness normalization. Each .nam carries an integrated LUFS
    # value in its JSON header; we read it and apply a per-stage
    # `outputLevel` so every NAM lands at `target_lufs`, eliminating
    # the volume jumps when switching between amps captured at
    # different levels. Capped to ±12 dB inside `_nam_normalized_output_level`.
    # Target −6 LUFS (was −18): pushes the instrument up so it sits well against
    # the backing. Baked per-stage into the chain (mega_chain / native_preset_full),
    # so unlike the live chain gain (engine-clamped at +12 dB) it isn't clamped.
    # A typical −18 LUFS capture reaches −6 (+12 dB = ×4 cap); quieter ones top
    # out at ×4.
    "normalize_nam_loudness": True,
    "nam_loudness_target_lufs": -6.0,
    # Asymmetric makeup cap. The library spans ~17 dB of LUFS variance
    # (clean amps near -10, high-gain amps near -27); a tight symmetric
    # ±12 cap squashed all the high-gain amps at +12 while leaving the
    # cleans largely untouched, flattening the chain. Allow up to +20 dB
    # boost so a -27 LUFS NAM can reach the -6 target; keep cut tighter
    # at -9 dB so the loudest captures don't drop below audibility.
    "nam_normalize_max_boost_db": 20.0,
    "nam_normalize_max_cut_db": 9.0,
    # NAM input drive. The amp model is a function: same input → same
    # output. Captures are typically taken with a calibrated ~-3 dBFS
    # signal driving the amp into its saturation range; live guitar
    # arrives at the engine around -18 to -12 dBFS peak. Without a
    # boost the NAM operates in its CLEAN region for every note — so
    # even high-gain amps come out sounding clean. We feed each NAM
    # stage an inputLevel of ~2.5 (≈+8 dB) so the amp's nonlinearity
    # actually kicks in. The accompanying outputLevel adjustment
    # (`_nam_normalized_output_level`) divides this back out so the
    # perceived volume stays at the loudness target.
    # Per-NAM-stage input drive (state.inputLevel). Verified empirically
    # to be IGNORED by the current audio engine — left at 1.0 so we
    # don't double-drive on a future engine version that does honour it.
    # The actual drive that reaches the NAMs is the chain-input gain
    # below (set via setGain('input', X) from the frontend).
    "nam_input_drive": 1.0,
    # Engine input gain (pre-NAM) applied via setGain('input', X) every time a
    # chain loads. Default is now 1.0 = NO boost: the live signal enters the NAM
    # as-is. The old 8.0 (≈+18 dB) over-drove most captures — both guitar and
    # bass players reported amps sounding over-distorted. Users who WANT more
    # amp saturation can raise it with the "Amp drive" slider in Settings.
    "nam_chain_input_drive": 1.0,
    # Clean input-level calibration trim (linear ×, default 1.0 = 0 dB = no trim).
    # Distinct from nam_chain_input_drive (the amp-DRIVE baseline, which has a
    # per-amp clean floor): this is a flat multiplier applied ON TOP of the drive
    # so the engine input = drive × calibration. The note-detect Calibration
    # Wizard writes it (raw DI normalized to −12 dBFS), and the Rig Builder "Input"
    # fader shows/edits it. A reduction here (e.g. 0.7× ≈ −3 dB) actually lowers
    # the level even when the guitar drive floor would otherwise pin the drive.
    "nam_input_calibration": 1.0,
    # Tone override: play EVERY song with one specific user tone instead of the
    # song's own tone. `tone_override_name` = "" (the Default tone) or a saved
    # Studio tone's name. Used by the Setup-tab "specific tone" control.
    "tone_override_enabled": False,
    "tone_override_name": "",
    # DEPRECATED / always OFF. Cabs are always on now that the modeled cabs
    # (rb_cab_overrides + _resolve_cab_for_effect) replaced the weak game cab IRs.
    # The Setup "Bypass all cabs" toggle was removed and `_load_settings` forces
    # this False even for settings files that still carry a stale True, so cabs
    # can never be bypassed globally again. Kept as a key so old files load clean.
    "bypass_all_cabs": False,
    # Bass DI + Cab blend (real bass rigs run mostly DI with a little mic'd cab).
    # When on, every BASS cab IR is replaced by a single IR that bakes a fixed
    # 70% DI (dry) + 30% cab blend, level-matched so the cab is audible and the
    # bass-band loudness is preserved (see _ir_stage / tools/make_di_cab_irs.py).
    "bass_di_cab": True,

    # Final chain normalizer.
    # Applied after the WHOLE chain, independent of whether the stages are
    # NAM, VST or IR. The frontend uses the native engine's output meter,
    # then adjusts setGain('chain', X) so every complete chain lands near
    # the same perceived level.
    "final_chain_normalize": True,
    # -15.5 (was -14): sits the leveled tone slightly UNDER the song's backing
    # (normalized to -12 LUFS, ×0.8 backing fader ≈ -13.9 heard) so the tone
    # doesn't dominate the mix (tone-vs-backing balance pass).
    "final_chain_target_rms_db": -15.5,
    "final_chain_min_gain_db": -20.0,
    "final_chain_max_gain_db": 20.0,
    "final_chain_gate_db": -45.0,
    "final_chain_attack_ms": 12,
    "final_chain_release_ms": 120,
}

# Tone3000 platform value to request per the game category. Amps and
# (most) pedals/racks get NAM captures; cabinets are convolved with IRs.
_PLATFORM_FOR_CATEGORY = {
    "amp": "nam",
    "pedal": "nam",
    "rack": "nam",
    "cab": "ir",
}


# ── Helpers ──────────────────────────────────────────────────────────

def _final_leveler_vst_path() -> Path | None:
    """Bundled final loudness normalizer VST.

    This VST lives at the very end of every generated chain and levels the
    complete result, independent of whether the previous stages are NAM, VST
    or IR.
    """
    p = (_plugin_dir / _FINAL_LEVELER_REL).resolve()
    return p if p.exists() else None


# Gear auditions play a single RAW amp/pedal at an unknown level. The leveler's
# AGC NORMALISES it to a fixed target, so a loud amp can't deafen you (it gets
# pulled down) and a quiet one is still audible (pulled up). Target sits a touch
# ABOVE the −14 LUFS song loudness so previews read clearly. Tunable knobs (only
# applied when audition=True):
_AUDITION_TARGET_LUFS = -12.0   # a bit above the −14 song target — clearly audible, still controlled
_AUDITION_MAX_CUT_DB = 30.0     # enough cut for the AGC to pull very loud gear down to that target


def _final_leveler_params_state(gate_db_override: float | None = None,
                                audition: bool = False) -> str:
    """State envelope consumed by both the native restore and the JS reapply.

    CRITICAL: the params dict MUST be keyed by the VST's parameter DISPLAY
    NAMES, not by positional indices. Both apply paths resolve params BY NAME:
    the native engine restores `pluginState` params by name, and the JS
    `rbReapplyVstParamsToChain` builds a name->id map from getParameters().
    Numeric string keys ("0".."7") match no name, so they fell through to the
    JS numeric-paramId fallback — which hit the engine's prepended "Buffer
    Size"/"Sample Rate" meta params and applied EVERY value off-by-two (e.g.
    Max Cut got the Attack value ≈ 0.7 dB, so the leveler could no longer
    attenuate loud amps). Keying by name fixes both paths.

    Names must match RBFinalLeveler's createParameterLayout() exactly:
      Target RMS dB / Max Boost dB / Max Cut dB / Gate dB /
      Attack ms / Release ms / Ceiling dB / Output Trim dB
    """
    s = _load_settings()

    def norm(value, lo, hi):
        try:
            v = float(value)
        except (TypeError, ValueError):
            v = lo
        return max(0.0, min(1.0, (v - lo) / (hi - lo)))

    # As of the K-weighting upgrade the leveler measures BS.1770 LUFS, so this
    # "Target RMS dB" is really a target LUFS (param name kept for state compat).
    target_rms = float(s.get("final_chain_target_rms_db", -14.0))
    max_boost = float(s.get("final_chain_max_gain_db", 20.0))
    max_cut = abs(float(s.get("final_chain_min_gain_db", -20.0)))
    gate = float(s.get("final_chain_gate_db", -45.0))
    # Bare-cab chains (no amp) run much quieter than amp chains even after the
    # pre-leveler boost stage — the caller lowers the gate so their playing
    # still opens it (the leveler's NAC pitch test keeps noise out regardless).
    if gate_db_override is not None:
        gate = float(gate_db_override)
    # Older settings shipped with 800/2500 ms here, which made the final
    # leveler behave like a slow auto-volume fade-in. Clamp the effective
    # leveler times to musical limiter values so stale local settings do not
    # reintroduce that bloom.
    attack = min(float(s.get("final_chain_attack_ms", 12)), 80.0)
    release = min(float(s.get("final_chain_release_ms", 120)), 250.0)
    ceiling = float(s.get("final_leveler_ceiling_db", -1.0))
    # The user "Chain volume" knob (chain_makeup, a 0-5x multiplier) is applied
    # POST-leveler via this Output Trim so the AGC can't cancel it. Bake it in
    # as dB here (live changes also poke this param via RbMegaChain in the UI).
    makeup = max(0.0, float(s.get("chain_makeup", 1.0)))
    chain_vol_db = 20.0 * math.log10(makeup) if makeup > 1.0e-4 else -24.0
    trim = chain_vol_db + float(s.get("final_leveler_trim_db", 0.0))

    if audition:
        target_rms = _AUDITION_TARGET_LUFS      # FIXED preview level (not the tone target)
        max_cut = max(max_cut, _AUDITION_MAX_CUT_DB)
        trim = min(trim, 0.0)                   # never let the makeup/output trim BOOST a preview

    params = {
        "Target RMS dB":  norm(target_rms, -30.0, -6.0),
        "Max Boost dB":   norm(max_boost, 0.0, 24.0),
        "Max Cut dB":     norm(max_cut, 0.0, 24.0),
        "Gate dB":        norm(gate, -80.0, -30.0),
        "Attack ms":      norm(attack, 1.0, 250.0),
        "Release ms":     norm(release, 20.0, 1000.0),
        "Ceiling dB":     norm(ceiling, -12.0, -0.1),
        "Output Trim dB": norm(trim, -24.0, 18.0),
    }

    return json.dumps({"params": params})

def _final_leveler_stage(missing: list | None = None,
                         gate_db_override: float | None = None,
                         audition: bool = False) -> dict | None:
    """Build the final VST stage, or None if disabled/missing. `audition=True`
    clamps it harder (see _AUDITION_* knobs) so raw single-gear previews in the
    Gear menu can't deafen you."""
    s = _load_settings()

    if not s.get("final_chain_normalize", True):
        return None

    if s.get("final_leveler_enabled", True) is False:
        return None

    p = _final_leveler_vst_path()
    if not p:
        if missing is not None:
            missing.append(str(_FINAL_LEVELER_REL))
        return None

    return _vst_stage(
        p,
        "VST3",
        bypassed=False,
        state=_vst_stage_state(str(p), "VST3",
                               _final_leveler_params_state(gate_db_override, audition=audition)),
        slot="master_post",
        rs_gear=_FINAL_LEVELER_RS_GEAR,
    )


# Bare game-cab chains (RS cab IR, no amp in front — acoustic/DI songs) run far
# quieter than amp chains. This lift used to live POST-leveler on the engine
# 'chain' bus (rbBareCabBoostFor, 2.5x) — but anything after the leveler defeats
# the leveling (bare-cab tones played ~+8 dB over every other tone once the AGC
# had already normalized them). It now goes IN FRONT of the leveler as a clean
# unit-impulse gain stage, plus a lower baked gate, so the AGC both hears the
# signal and owns the final level like for every other tone.
_BARE_CAB_BOOST = 2.5              # ~+8 dB, same lift the bus-side fix used
_BARE_CAB_GATE_DB = -58.0          # baked leveler gate for these quiet chains
_BARE_CAB_RS_GEAR = "__rb_bare_cab_boost"


def _chain_is_bare_rs_cab(chain: list[dict]) -> bool:
    """True when the chain has an active game-cab IR and NO active amp stage
    (mirrors screen.js rbBareCabBoostFor's detection)."""
    has_rs_cab = has_amp = False
    for st in chain:
        if not st or st.get("bypassed"):
            continue
        if st.get("slot") == "amp" and st.get("type") in (0, 1):
            has_amp = True
        if st.get("type") == 2 and _is_rocksmith_ir_file(st.get("path")):
            has_rs_cab = True
    return has_rs_cab and not has_amp


def _bare_cab_boost_stage() -> dict | None:
    ir = _unit_impulse_ir_path()
    if not ir:
        return None
    # state.gain = x8 only (cancels the engine's IR normalization on the
    # impulse); postGain carries the actual lift — same split as
    # _amp_trim_stage so the gain sums right on fixed AND old engines.
    st = _ir_stage(ir, bypassed=False, gain=8.0,
                   rs_gear=_BARE_CAB_RS_GEAR)
    st["postGain"] = round(_BARE_CAB_BOOST, 4)
    return st


def _append_final_leveler(chain: list[dict], missing: list | None = None,
                          audition: bool = False) -> None:
    """Append RB Final Leveler as the very last stage (with the pre-leveler
    bare-cab lift when the chain needs it). `audition=True` clamps it harder
    (safe level for previewing raw gear)."""
    if any((s or {}).get("rs_gear") == _FINAL_LEVELER_RS_GEAR for s in chain):
        return
    gate_override = None
    if _chain_is_bare_rs_cab(chain):
        boost = _bare_cab_boost_stage()
        if boost:
            chain.append(boost)
        gate_override = _BARE_CAB_GATE_DB
    stage = _final_leveler_stage(missing, gate_db_override=gate_override, audition=audition)
    if stage:
        chain.append(stage)

# Bundled VSTs whose .vst3 file was renamed to its copyright-free parody name.
# Maps OLD bundle filename -> NEW. Single source of truth for: (a) the DB
# migration in _get_conn that rewrites stored preset_pieces.vst_path so existing
# per-song assignments survive, and (b) the CI build workflows' alias_of (built
# binary keeps the old NAME; injected into the renamed committed dir). Grows as
# more pedals are renamed, category by category.
_RENAMED_VST_BUNDLES = {
    # distortion
    "StandardDistortion.vst3": "DS-1.vst3",
    "AlloyDistortion.vst3":    "HM-2.vst3",
    "ShredZone.vst3":          "MT-2.vst3",
    "VintageDistortion.vst3":  "Vintage Distortion.vst3",
    "BassDistortion.vst3":     "Mouse.vst3",
    # fuzz
    "BassFuzz.vst3":           "Bass Big Buzz.vst3",
    # drive / overdrive / boost
    "CustomDrive.vst3":        "CDO.vst3",
    "GermaniumDrive.vst3":     "Germanium Drive.vst3",
    "RangeBooster.vst3":       "Range Booster.vst3",
    "LineDrive.vst3":          "OS-2.vst3",
    "SuperDrive.vst3":         "SD-1.vst3",
    "MarshallGuvnorPlus.vst3": "GM-2.vst3",
    "BassOverdrive.vst3":      "BLACKBRASS.vst3",
    # modulation
    "Chorus.vst3":               "CH-2.vst3",
    "DigitalChorus.vst3":        "CH-5.vst3",
    "Chorus20.vst3":             "Deja Chorus.vst3",
    "AnalogChorus.vst3":         "134 Stereo Chorus.vst3",
    "Analog Chorus.vst3":        "134 Stereo Chorus.vst3",
    "BassChorus.vst3":           "CB-3.vst3",
    "SendInTheClones.vst3":      "Attack of the Clones.vst3",
    "ClassicFlanger.vst3":       "FL-2.vst3",
    "BassFlanger.vst3":          "FL-3.vst3",
    "ModernFlanger.vst3":        "FM107.vst3",
    "VintageFlanger.vst3":       "Deluxe Servant.vst3",
    "EightiesFlanger.vst3":      "N117R Flanger.vst3",
    "ShaverPhaser.vst3":         "PH-1.vst3",
    "Phaser363.vst3":            "phase 90.vst3",
    "BassPhase.vst3":            "phase 99.vst3",
    "PlanePhase.vst3":           "Rocket Phase.vst3",
    "MultiTrem.vst3":            "TR-2.vst3",
    "AmpTrem.vst3":              "Mega-Trem.vst3",
    "TremOle.vst3":              "Dyna-Trem.vst3",
    "MultiVibe.vst3":            "VB-2.vst3",
    "MarshallSupervibe.vst3":    "UV-1.vst3",
    "OmniMod.vst3":              "UniMod.vst3",
    "AmpVibe.vst3":              "Multi-Vibe.vst3",
    "AutoVibe.vst3":             "Oceanduct.vst3",
    "BakedRotatoe.vst3":         "RT-2.vst3",
    # delay / echo
    "BassFilterDelay.vst3":      "DL-3.vst3",
    "BassFilterEcho.vst3":       "SE-3.vst3",
    "AnalogDelay.vst3":          "FM104.vst3",
    "CosmicEcho.vst3":           "Galaxy Echo.vst3",
    "ModDelay.vst3":             "DL9.vst3",
    "NpnDelay.vst3":             "DM-2.vst3",
    "NoFiEcho.vst3":             "No Fi Echo.vst3",
    "OilCanEcho.vst3":           "Oil Can Echo.vst3",
    "ValveEcho.vst3":            "Valve Echo.vst3",
    # reverb
    "DigitalVerb.vst3":          "RV-2.vst3",
    "PlateVerb.vst3":            "VOODOO.vst3",
    "SpringReverb.vst3":         "Holy Spring.vst3",
    "TubeSpring.vst3":           "Real Spring.vst3",
    # dynamics
    "Limiter.vst3":              "LM-2.vst3",
    "NoiseGate.vst3":            "NF-1.vst3",
    "DynamicsCompression.vst3":  "dyna comp.vst3",
    "BassMultiComp.vst3":        "Multi Comp.vst3",
    "StudioComp.vst3":           "HZX.vst3",
    "Swole.vst3":                "Beta Fist.vst3",
    # wah
    "USWah.vst3":                "Cry Man.vst3",
    "cry man.vst3":              "Cry Man.vst3",
    "UKWah.vst3":                "BOX B847.vst3",
    "ModernWah.vst3":            "Jockey Bad.vst3",
    "BassWah.vst3":              "Bass Wah.vst3",
    # filter / envelope
    "AutoSweep.vst3":            "Qtrix.vst3",
    "AutoFilter.vst3":           "BU-TRON III.vst3",
    "BobFilter.vst3":            "FM101.vst3",
    "LoFiFilter.vst3":           "Lo Fi Filter.vst3",
    # eq
    "BassEQ8.vst3":              "GEB-8.vst3",
    "AmpEQ.vst3":                "FBM-1.vst3",
    "EQ8.vst3":                  "GE-8.vst3",
    "StudioEQ.vst3":             "LNG.vst3",
    "StudioGraphicEQ.vst3":      "G-550.vst3",
    # pitch / octave
    "OctaveUp.vst3":             "OCTUP.vst3",
    "Octavius.vst3":             "OC-5.vst3",
    "MultiPitch.vst3":           "Multi Pitch.vst3",
    "BassSubOctave.vst3":        "SO-2.vst3",
    # emulator / enhancer / preamp
    "AcousticSimulator.vst3":    "Acoustic Emulator.vst3",
    "BassEmulator.vst3":         "Bass Emulator.vst3",
    "Enbiggenator.vst3":         "MIME.vst3",
    "BassEnbig.vst3":            "ENBIGGEN.vst3",
    "EdenWTDI.vst3":             "WT-DX.vst3",
    "BOX DC30.vst3":             "BOX AC30.vst3",
    "EN30.vst3":                 "BOX AC30.vst3",
    "TW22.vst3":                 "BENDER SUPERNOVA 22.vst3",
    "TW26.vst3":                 "BENDER DELUXE.vst3",
}

_FINAL_LEVELER_RS_GEAR = "__rb_final_leveler__"
_FINAL_LEVELER_NAME = "RB Final Leveler.vst3"
_FINAL_LEVELER_REL = Path("vst") / "racks" / _FINAL_LEVELER_NAME

def _require_db_path() -> str:
    if _db_path is None:
        raise RuntimeError("rig_builder plugin not initialized")
    return _db_path


def _chunked(seq: list, size: int = 400):
    """Yield `seq` in slices of at most `size` items. SQLite caps host
    parameters per statement (999 in older builds), so IN (...) lists
    built from arbitrary file inventories must be chunked."""
    for i in range(0, len(seq), size):
        yield seq[i:i + size]


def _get_conn() -> sqlite3.Connection:
    """Open (and migrate, if needed) the shared nam_tone.db.

    The nam_tone plugin already creates `presets` and `tone_mappings`;
    we additionally ensure `preset_pieces` exists. CREATE TABLE IF NOT
    EXISTS is idempotent, so running this every cold start is safe.
    """
    if _conn is not None:
        return _conn
    # Double-checked locking: concurrent first requests must not race the
    # connection creation + migrations. A DEDICATED init lock (not `_lock`)
    # — the migrations inside call _get_master_preset_id(), which takes
    # `_lock` itself, so holding `_lock` across init would deadlock.
    # Nested _get_conn() calls during the migrations hit the fast path
    # above because `_conn` is assigned before they run.
    with _conn_init_lock:
        return _get_conn_locked()


def _get_conn_locked() -> sqlite3.Connection:
    global _conn
    if _conn is None:
        _conn = sqlite3.connect(_require_db_path(), check_same_thread=False)
        _conn.execute("PRAGMA journal_mode=WAL")
        _conn.execute("PRAGMA foreign_keys=ON")
        _conn.execute(
            """
            CREATE TABLE IF NOT EXISTS preset_pieces (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                preset_id INTEGER NOT NULL,
                slot_order INTEGER NOT NULL,
                slot TEXT NOT NULL,
                rs_gear_type TEXT NOT NULL,
                kind TEXT NOT NULL,
                file TEXT,
                params_json TEXT NOT NULL DEFAULT '{}',
                tone3000_id INTEGER,
                assigned_mode TEXT,
                bypassed INTEGER NOT NULL DEFAULT 0,
                FOREIGN KEY (preset_id) REFERENCES presets(id) ON DELETE CASCADE
            )
            """
        )
        # Migrate older DBs that predate the `bypassed` column. ALTER TABLE
        # ADD COLUMN is not idempotent, so guard on the column list.
        cols = {r[1] for r in _conn.execute("PRAGMA table_info(preset_pieces)")}
        if "bypassed" not in cols:
            _conn.execute(
                "ALTER TABLE preset_pieces ADD COLUMN bypassed INTEGER NOT NULL DEFAULT 0"
            )
        # v0.1.0: VST3/AU plugin support. A piece with kind='vst' references a
        # plugin on disk by absolute path, plus an optional opaque state blob
        # captured from the engine via savePreset() (whole-chain state — only
        # safe to restore when the chain in question is a single VST). For
        # multi-stage chains we currently rely on the VST loading with its
        # internal defaults and the user re-tweaking in the editor; this is
        # documented as a v1 limitation in memory/12-rig-builder-vst.md.
        if "vst_path" not in cols:
            _conn.execute("ALTER TABLE preset_pieces ADD COLUMN vst_path TEXT")
        if "vst_format" not in cols:
            _conn.execute("ALTER TABLE preset_pieces ADD COLUMN vst_format TEXT")
        if "vst_state" not in cols:
            _conn.execute("ALTER TABLE preset_pieces ADD COLUMN vst_state TEXT")
        _conn.execute(
            "CREATE INDEX IF NOT EXISTS idx_preset_pieces_preset "
            "ON preset_pieces(preset_id)"
        )
        _conn.execute(
            "CREATE INDEX IF NOT EXISTS idx_preset_pieces_rs_gear "
            "ON preset_pieces(rs_gear_type)"
        )
        # Per-tone noise gate: the `presets` table (created by nam_tone) already
        # carries `gate_threshold`; add the rest of the gate's parameters so a
        # tone can store a full gate (enabled/threshold/release/depth) mirroring
        # the app's global noise gate. ALTER isn't idempotent, so guard on the
        # column list; tolerate the presets table not existing yet.
        try:
            pcols = {r[1] for r in _conn.execute("PRAGMA table_info(presets)")}
            if pcols:  # table exists
                if "gate_enabled" not in pcols:
                    _conn.execute("ALTER TABLE presets ADD COLUMN gate_enabled INTEGER NOT NULL DEFAULT 0")
                if "gate_release" not in pcols:
                    _conn.execute("ALTER TABLE presets ADD COLUMN gate_release REAL NOT NULL DEFAULT 100.0")
                if "gate_depth" not in pcols:
                    _conn.execute("ALTER TABLE presets ADD COLUMN gate_depth REAL NOT NULL DEFAULT -60.0")
        except Exception:
            log.exception("presets gate-column migration failed")
        # Re-home bundled-VST absolute paths to the CURRENT plugin dir FIRST —
        # before the slash-only segment rewrites below and before any
        # existence-checking migration (e.g. _migrate_assign_bundled_primary_
        # once, which would otherwise treat a stale AppImage mount path as
        # "broken" and clobber a manual pick). Re-home also normalizes '\'→'/'
        # and emits forward-slash paths, so the segment rewrites (which match
        # only '/vst/<name>' style segments) can still fix flat→subdir and
        # renamed-bundle tails on Windows-authored/cross-platform DBs. Unlike the
        # other migrations this is NOT one-shot: the AppImage plugin dir is a
        # per-launch mount that moves each start, so it runs on every open — see
        # the docstring.
        try:
            _migrate_rehome_bundled_vst_paths()
        except Exception:
            log.exception("bundled-VST path re-home failed")
        # Plugin rename: old installs stored bundled VST absolute paths under
        # plugins/nam_rig_builder/. The active plugin dir is rig_builder, and
        # the old directory may no longer exist, so normalize those paths before
        # applying bundle/subdir migrations below.
        try:
            _conn.execute(
                "UPDATE preset_pieces "
                "SET vst_path = REPLACE(vst_path, ?, ?) "
                "WHERE vst_path LIKE ?",
                ("/plugins/nam_rig_builder/",
                 "/plugins/rig_builder/",
                 "%/plugins/nam_rig_builder/%"),
            )
        except Exception:
            log.exception("plugin-dir VST path migration failed")
        # IR/cab rows must not carry a stale VST path. Older assign flows could
        # leave vst_path populated while changing the row back to kind='ir' or
        # kind='rs_ir', which made the Gear tab try to open an unrelated VST for
        # cabinets.
        try:
            _conn.execute(
                "UPDATE preset_pieces "
                "SET vst_path = NULL, vst_format = NULL, vst_state = NULL "
                "WHERE kind IN ('ir', 'rs_ir') "
                "AND vst_path IS NOT NULL AND vst_path != ''"
            )
        except Exception:
            log.exception("stale cab/IR VST cleanup failed")
        # Renamed bundled VSTs: rewrite stored vst_path basenames old->new so
        # per-song assignments survive the rename. Idempotent — REPLACE matches
        # the full trailing "/<old>.vst3" segment, so it's a no-op once migrated.
        try:
            for old, new in _RENAMED_VST_BUNDLES.items():
                _conn.execute(
                    "UPDATE preset_pieces SET vst_path = REPLACE(vst_path, ?, ?) "
                    "WHERE vst_path LIKE ?", (f"/{old}", f"/{new}", f"%/{old}"))
        except Exception:
            log.exception("renamed-VST path migration failed")
        # vst/ folder reorg: bundles moved from a flat vst/<name>.vst3 into
        # category subdirs (vst/amps|pedals|racks/<name>.vst3). Rewrite stored
        # absolute paths so existing per-song assignments survive the move.
        # Driven by the seed catalog's now-subdir'd `bundled` paths and
        # idempotent (the flat "/vst/<name>" segment no longer matches once a
        # path has been moved under a subdir).
        try:
            seed = _load_vst_seed_catalog() or {}
            seen = set()
            for arr in seed.values():
                if not isinstance(arr, list):
                    continue
                for e in arr:
                    b = (isinstance(e, dict) and e.get("bundled")) or ""
                    parts = b.split("/")
                    if (len(parts) == 3 and parts[0] == "vst"
                            and parts[2].endswith((".vst3", ".component"))):
                        fn = parts[2]
                        if fn in seen:
                            continue
                        seen.add(fn)
                        _conn.execute(
                            "UPDATE preset_pieces SET vst_path = REPLACE(vst_path, ?, ?) "
                            "WHERE vst_path LIKE ?",
                            (f"/vst/{fn}", f"/{b}", f"%/vst/{fn}"))
        except Exception:
            log.exception("vst subdir path migration failed")
        _conn.commit()
        # v1.2 storage migration. Idempotent — guarded by sentinel file
        # so re-running on subsequent restarts is a no-op. Done here
        # (after the schema migrations) so the migration can join on
        # rs_gear_type to classify each flat NAM into a category subdir.
        try:
            _migrate_nam_storage_to_subdirs()
        except Exception:
            log.exception("nam storage migration failed — keeping flat layout")
        # v1.3.2 double-attenuation fix. Songs persisted by older versions
        # (batch worker, watcher, save_preset) defaulted to output_gain=0.5,
        # which the engine then applied twice: once on `chainOutputGain`
        # (via applyPresetGainLevels) and once inside the IR stage's own
        # `gain` (via _state_b64 in native_preset_full). Net result was a
        # ~−12 dB drop when a song's preset replaced the user's idle chain.
        # Bump every untouched song preset to unity so the OUT slider is the
        # single source of truth. Skip presets the user explicitly nudged
        # (anything other than the legacy 0.5 default).
        # Guarded by a sentinel row in `settings_json` of the master_pre
        # sentinel preset so the migration only runs once per DB.
        try:
            _migrate_output_gain_to_unity()
        except Exception:
            log.exception("output_gain unity migration failed")
        # v2.0.1: point pedal/rack pieces at their BUNDLED VST primary. DBs
        # mapped before the bundled effects existed (or before a pedal rename)
        # still reference external plugins (kHs/Melda) or a removed bundle name,
        # so the bundled DSP never loads. One-shot, sentinel-guarded.
        try:
            _migrate_assign_bundled_primary_once()
        except Exception:
            log.exception("bundled-primary migration failed")
        # NOTE: the auto gear-global consolidation was DISABLED — its
        # "most-recent VST wins, applied to every instance" rule flattened
        # per-song picks and mis-mapped gears (e.g. a comp pedal globbed onto
        # a distortion plugin). Correct per-gear defaults ship via
        # rs_gear_to_vst.json instead. `_consolidate_gear_assignments` is kept
        # for reference / a possible safer reimplementation but is not run.
    return _conn


def _migrate_rehome_bundled_vst_paths() -> None:
    """Re-home stored bundled-VST absolute paths to the CURRENT plugin dir.

    Bundled effect VST3s live under `<_plugin_dir>/vst/<subdir>/<name>.vst3`,
    but `preset_pieces.vst_path` persists that location as an ABSOLUTE string.
    That breaks under the AppImage: the plugin dir is a per-launch FUSE mount
    (`/tmp/.mount_feedbaXXXXXX/resources/.../plugins/rig_builder`) that is
    deleted on quit and remounted at a *different* random path next launch. A
    tone saved in one session then points at a mount that no longer exists, so
    the amp/rack VST3 cannot be found and the rig "tries to load but fails" on
    the highway (#46).

    Fix: on every open, rewrite any stored path carrying a bundled-plugin
    marker (`plugins/rig_builder/vst/`, or the legacy `nam_rig_builder`) so its
    prefix points at the live `_plugin_dir`, preserving the relative tail
    (`amps/DSL100.vst3`). This is deliberately NOT sentinel-guarded — the mount
    moves every launch, so it must re-run each start. The user's own external
    VST3s live at stable paths without the marker and are left untouched. The
    engine locates each stage from `vst_path` (the type-0 stage `path`, and the
    legacy `pluginPath` wrapper that native_preset_full rebuilds from it — see
    `_vst_stage_state`), so repairing this column fixes both the path and the
    emitted state blob.

    Runs FIRST in `_get_conn` — before the slash-only segment rewrites (nam→rig,
    renamed bundles, flat→subdir) and before existence-checking migrations. We
    emit forward-slash paths (even from a Windows `\\`-separated original) so
    those downstream rewrites, which match only `/vst/<name>`-style segments,
    can still repair a flat or renamed tail on Windows-authored/cross-platform
    DBs. Forward slashes are valid paths on every OS the engine runs on.
    """
    conn = _conn
    if conn is None:
        return
    vst_root = _plugin_dir / "vst"
    root_norm = str(vst_root).replace("\\", "/")
    markers = ("/plugins/rig_builder/vst/", "/plugins/nam_rig_builder/vst/")
    try:
        rows = conn.execute(
            "SELECT id, vst_path FROM preset_pieces "
            "WHERE vst_path IS NOT NULL AND vst_path != ''"
        ).fetchall()
    except sqlite3.OperationalError as e:
        # Only swallow the expected "no vst_path column yet" case (brand-new DB
        # before the ALTER); re-raise anything else (locked/corrupt DB, missing
        # table) so the outer _get_conn() handler logs it instead of a silent
        # no-op.
        if "vst_path" in str(e).lower():
            return
        raise
    fixed = 0
    for piece_id, vp in rows:
        if not vp:
            continue
        norm = vp.replace("\\", "/")
        rel = None
        if norm.startswith(root_norm + "/"):
            # Already anchored at the live plugin dir — but if the STORED value
            # still uses '\' (a Windows-authored row), fall through so we rewrite
            # it to forward slashes; otherwise the slash-only rename/subdir
            # migrations below still can't match its flat/renamed tail.
            rel = norm[len(root_norm) + 1:]
        else:
            for mk in markers:
                idx = norm.find(mk)
                if idx != -1:
                    rel = norm[idx + len(mk):]
                    break
        if not rel:
            continue  # not a bundled path (external/user VST) — leave alone
        # Emit a forward-slash path (not str(vst_root / rel), which would use
        # '\' on Windows) so the slash-only segment rewrites that run after this
        # can still match and repair a flat/renamed tail.
        new_path = root_norm + "/" + rel
        if new_path != vp:
            conn.execute(
                "UPDATE preset_pieces SET vst_path = ? WHERE id = ?",
                (new_path, piece_id),
            )
            fixed += 1
    if fixed:
        conn.commit()
        log.info("re-homed %d bundled VST path(s) to %s", fixed, str(vst_root))


def _migrate_output_gain_to_unity() -> None:
    """One-shot backfill: any user-song preset still at the legacy
    output_gain=0.5 default (set silently by older rig_builder versions
    that piggybacked on nam_tone's −6 dB makeup) is bumped to 1.0. Sentinel
    presets (`__rig_builder_master_*`) and anything the user nudged off
    0.5 are left alone. Re-runs are no-ops thanks to the sentinel marker."""
    conn = _conn
    if conn is None:
        return
    marker_row = conn.execute(
        "SELECT settings_json FROM presets WHERE name = ?",
        ("__rig_builder_master_pre__",),
    ).fetchone()
    marker = json.loads(marker_row[0] or "{}") if marker_row else {}
    if marker.get("output_gain_unity_migrated"):
        return
    cur = conn.execute(
        "UPDATE presets SET output_gain = 1.0 "
        "WHERE output_gain = 0.5 AND name NOT LIKE '__rig_builder_%'"
    )
    bumped = cur.rowcount
    # Mark done — get/create the master_pre sentinel just so it has a
    # settings_json blob to host the marker. _get_master_preset_id ensures
    # the row exists; we set the marker even if no rows needed bumping
    # (idempotency on a fresh install where everything is already unity).
    pid = _get_master_preset_id("pre")
    if pid is not None:
        marker["master_role"] = "pre"
        marker["output_gain_unity_migrated"] = True
        conn.execute(
            "UPDATE presets SET settings_json = ? WHERE id = ?",
            (json.dumps(marker), pid),
        )
    conn.commit()
    if bumped:
        log.info("output_gain unity migration: %d preset(s) bumped to 1.0", bumped)


def _migrate_consolidate_gear_assignments_once() -> None:
    """One-shot: collapse per-song gear divergence into ONE global assignment
    (NAM = curated, VST = most recent, cabs keep their per-song mic). Backs up
    the DB to `<db>.pre-consolidate.bak` first, then runs the same routine the
    Settings preview used. Sentinel-guarded in the master_pre preset so it runs
    exactly ONCE — it never re-runs on later launches, so deliberate edits the
    user makes afterward are preserved (and, being global, apply everywhere)."""
    conn = _conn
    if conn is None:
        return
    marker_row = conn.execute(
        "SELECT settings_json FROM presets WHERE name = ?",
        ("__rig_builder_master_pre__",),
    ).fetchone()
    marker = json.loads(marker_row[0] or "{}") if marker_row else {}
    if marker.get("gear_global_consolidated"):
        return
    # Sound-changing bulk rewrite → back the DB up first; bail if we can't.
    if _db_path:
        try:
            shutil.copy2(_db_path, f"{_db_path}.pre-consolidate.bak")
        except OSError:
            log.exception("pre-consolidate backup failed; skipping consolidation")
            return
    report = _consolidate_gear_assignments(conn, apply=True)
    pid = _get_master_preset_id("pre")
    if pid is not None:
        marker["master_role"] = "pre"
        marker["gear_global_consolidated"] = True
        conn.execute("UPDATE presets SET settings_json = ? WHERE id = ?",
                     (json.dumps(marker), pid))
        conn.commit()
    log.info("gear global consolidation: %d pieces across %d songs unified to "
             "a single assignment", report.get("rows_changed", 0),
             report.get("presets_affected", 0))


def _migrate_assign_bundled_primary_once() -> None:
    """One-shot: point pedal/rack pieces at their BUNDLED VST primary.

    The 100 bundled effects are the default in rs_gear_to_vst.json, but DBs
    mapped before they existed (or before a pedal rename) still point at
    external plugins (kHs/Melda) or a now-removed bundle name — so the bundled
    DSP never loads ("still getting kHs Chorus", "FuzzWasHe won't open"). This
    reassigns every AUTO-assigned pedal/rack piece — plus any piece whose
    current VST file no longer exists (e.g. a renamed bundle) — to the bundled
    primary and recomputes its vst_state from the RS knobs. Amps flip too when
    a bundled VST is installed for them (rs_gear_to_vst.json, e.g. BT880B ->
    FreddyKrueger800BR); amps without one and cabs keep their NAM/IR. A
    still-valid MANUAL pick is
    preserved. Sentinel-guarded so it runs exactly once (later manual choices
    stick). Backs the DB up first."""
    conn = _conn
    if conn is None:
        return
    marker_row = conn.execute(
        "SELECT settings_json FROM presets WHERE name = ?",
        ("__rig_builder_master_pre__",),
    ).fetchone()
    marker = json.loads(marker_row[0] or "{}") if marker_row else {}
    already_ran = bool(marker.get("bundled_primary_assigned_v27"))
    # Runs on EVERY startup now (was one-shot). Songs seeded AFTER the first run
    # kept their raw NAM amp/pedal/rack instead of the bundled VST, so a pure-VST
    # rig kept "sprouting NAMs" (277 amp pieces stuck on NAM). The reassignment is
    # idempotent (skips pieces already on the bundled VST) and still preserves a
    # deliberate MANUAL pick, so re-running every launch is safe and self-heals
    # any song whose amp resolves to a bundled VST. Back the DB up only once.
    if not already_ran and _db_path:
        try:
            shutil.copy2(_db_path, f"{_db_path}.pre-bundled-primary-v27.bak")
        except OSError:
            log.exception("pre-bundled-primary backup failed; skipping")
            return
    known = _build_known_vst_lookup()
    rows = conn.execute(
        "SELECT id, rs_gear_type, params_json, vst_path, assigned_mode, kind "
        "FROM preset_pieces"
    ).fetchall()
    changed = 0
    for pid, gear, pj, cur, mode, kind in rows:
        if not gear or _gear_category(gear) == "cab":
            continue
        # Amps flip to a bundled VST only when one is actually installed for
        # the gear (rs_gear_to_vst.json); _pick_installed_primary_vst returns
        # None for amps without a bundled VST, so they keep their NAM below.
        pick = _pick_installed_primary_vst(gear, known)
        if not pick:
            continue
        bundled = pick["vst_path"]
        # Only skip when the piece is ALREADY a fully-resolved VST on this bundle.
        # A piece with the right vst_path but kind='none' (e.g. its NAM file was
        # deleted and the file-missing cleanup blanked the kind) must still be
        # repaired to kind='vst' — otherwise the amp shows up dead/unloadable.
        if cur == bundled and kind == "vst":
            continue
        cur_broken = bool(cur) and not Path(cur).exists()
        # Respect a deliberate manual pick that still resolves — a manual NAM
        # ('manual') OR a manual VST ('manual_vst'); reassign everything auto, and
        # any broken path (e.g. a renamed bundle or a deleted NAM). Since this now
        # runs every startup, preserving manual_vst matters: without it a user's
        # hand-picked non-primary VST would be overwritten with the gear's primary
        # on every launch.
        if mode in ("manual", "manual_vst") and not cur_broken:
            continue
        try:
            knobs = json.loads(pj) if pj else {}
        except (ValueError, TypeError):
            knobs = {}
        state = _compute_vst_state_for_piece(
            gear, bundled, knobs if isinstance(knobs, dict) else {})
        conn.execute(
            "UPDATE preset_pieces SET kind='vst', file=NULL, "
            "vst_path=?, vst_format=?, vst_state=? WHERE id=?",
            (bundled, pick["vst_format"], state, pid),
        )
        changed += 1
    if not already_ran:
        mpid = _get_master_preset_id("pre")
        if mpid is not None:
            marker["bundled_primary_assigned_v27"] = True
            conn.execute("UPDATE presets SET settings_json = ? WHERE id = ?",
                         (json.dumps(marker), mpid))
    conn.commit()
    if changed:
        log.info("bundled-primary migration: reassigned %d auto pedal/rack/amp piece(s) to their bundled VST", changed)


def _load_settings() -> dict:
    global _settings
    if _settings is not None:
        return _settings
    if _config_dir is None:
        return dict(_DEFAULT_SETTINGS)
    path = _config_dir / _SETTINGS_FILENAME
    # One-shot migration from the legacy filename. Only run when the new
    # file is missing — once we write our own there's nothing to copy.
    if not path.exists():
        legacy = _config_dir / _LEGACY_SETTINGS_FILENAME
        if legacy.exists():
            try:
                path.write_bytes(legacy.read_bytes())
                log.info("migrated settings from %s to %s", legacy.name, path.name)
            except OSError:
                log.warning("legacy settings copy failed; using defaults", exc_info=True)
    if path.exists():
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
            _settings = {**_DEFAULT_SETTINGS, **loaded}
        except (json.JSONDecodeError, OSError):
            log.warning("settings file unreadable, resetting to defaults")
            _settings = dict(_DEFAULT_SETTINGS)
    else:
        _settings = dict(_DEFAULT_SETTINGS)
    # Cabs are always on now — force the deprecated 'bypass all cabs' flag off
    # even if a stale settings file (or a legacy /settings POST) still carries a
    # True. The Setup toggle that used to set it was removed.
    _settings["bypass_all_cabs"] = False
    return _settings


def _write_settings_file(data: dict) -> None:
    """Persist settings to disk with owner-only permissions (0600).

    The file holds tone3000 credentials (OAuth tokens and/or a secret key),
    so we restrict it to the current OS user — other accounts on a shared
    machine can't read it. Best-effort: chmod is a no-op on some platforms
    (e.g. Windows), which is fine since the profile dir is already per-user."""
    path = _config_dir / _SETTINGS_FILENAME
    _atomic_write_json(path, data)
    try:
        path.chmod(0o600)
    except OSError:
        pass


def _save_settings(new_settings: dict) -> dict:
    global _settings, _t3k_client
    if _config_dir is None:
        raise RuntimeError("config_dir not available")
    with _settings_lock:
        current = _load_settings()
        merged = {**current, **new_settings}
        _write_settings_file(merged)
        _settings = merged
    # Reset the cached client so a new key takes effect immediately.
    with _t3k_lock:
        _t3k_client = None
    return merged


def _persist_tokens(updates: dict) -> None:
    """Write a partial settings update straight to disk WITHOUT nulling the
    cached client. Used as the tone3000 client's `on_tokens` callback: a
    background refresh rotates the access/refresh token mid-request, and we
    must not tear down the very client that's still using it (which is what
    `_save_settings` would do)."""
    global _settings
    if _config_dir is None:
        return
    with _settings_lock:
        current = _load_settings()
        current.update(updates)
        try:
            _write_settings_file(current)
        except OSError:
            log.warning("failed to persist tone3000 tokens", exc_info=True)
        _settings = current


def _safe_loopback_redirect(origin: str) -> str:
    """Build the OAuth redirect_uri, HARD-RESTRICTED to a loopback address.

    Security: the authorization code comes back to whatever redirect_uri we
    request. By refusing anything but 127.0.0.1 / localhost we guarantee the
    code can never be delivered off this machine, even if the `origin` query
    param is tampered with by another local process. (tone3000 also rejects
    unregistered redirect URIs, but we don't rely on that alone.) Anything
    unexpected falls back to the default local server."""
    from urllib.parse import urlparse
    base = "http://127.0.0.1:18000"
    try:
        u = urlparse((origin or "").strip())
        host = (u.hostname or "").lower()
        if u.scheme in ("http", "https") and host in ("127.0.0.1", "localhost", "::1"):
            base = f"{u.scheme}://{u.netloc}"
    except Exception:
        pass
    return f"{base.rstrip('/')}/api/plugins/rig_builder/oauth/callback"


def _oauth_result_page(message: str, ok: bool) -> str:
    """Tiny self-contained page shown in the user's browser after the OAuth
    redirect. The real state lives in the backend; this just tells the user
    they can return to feedBack."""
    color = "#34d399" if ok else "#f87171"
    return (
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>TONE3000 — Rig Builder</title>"
        "<style>body{background:#0f0f12;color:#e5e7eb;font-family:-apple-system,"
        "Segoe UI,Roboto,sans-serif;display:flex;align-items:center;"
        "justify-content:center;height:100vh;margin:0}"
        ".card{max-width:420px;text-align:center;padding:2rem 2.5rem;"
        "background:#17171c;border:1px solid #2a2a32;border-radius:16px}"
        f"h1{{color:{color};font-size:1.1rem;margin:0 0 .5rem}}"
        "p{color:#9ca3af;font-size:.9rem;line-height:1.5;margin:0}</style></head>"
        f"<body><div class='card'><h1>{'Connected' if ok else 'Could not connect'}</h1>"
        f"<p>{html.escape(message)}</p></div></body></html>"
    )


def _get_t3k_client():
    """Lazy tone3000 client. Cached because we open a sqlite cache db on
    construction. Reset when settings change."""
    global _t3k_client
    with _t3k_lock:
        if _t3k_client is None:
            # Import here so that a missing client module doesn't break
            # the plugin's main routes — search just degrades.
            if str(_plugin_dir) not in sys.path:
                sys.path.insert(0, str(_plugin_dir))
            from rb_core.tone3000_client import Tone3000Client
            settings = _load_settings()
            # Cache db lives next to the settings. Migrate the legacy name
            # the same way as settings so the tone3000 search cache survives
            # the rename.
            cache_path = None
            if _config_dir is not None:
                new_cache = _config_dir / "rig_builder_cache.db"
                legacy_cache = _config_dir / "nam_rig_builder_cache.db"
                if not new_cache.exists() and legacy_cache.exists():
                    try:
                        new_cache.write_bytes(legacy_cache.read_bytes())
                        log.info("migrated cache db from %s to %s",
                                 legacy_cache.name, new_cache.name)
                    except OSError:
                        log.warning("legacy cache copy failed; rebuilding",
                                    exc_info=True)
                cache_path = str(new_cache)
            else:
                cache_path = "/tmp/rig_builder_cache.db"
            _t3k_client = Tone3000Client(
                cache_path,
                api_key=settings.get("tone3000_api_key") or None,
                access_token=settings.get("tone3000_access_token") or None,
                refresh_token=settings.get("tone3000_refresh_token") or None,
                token_expires_at=settings.get("tone3000_token_expires_at") or 0,
                on_tokens=_persist_tokens,
            )
        return _t3k_client


_json_cache: dict[str, object] = {}


def _data_path(filename: str) -> Path:
    """Resolve a generated data file (rs_to_real.json, rs_cab_to_ir.json, …).

    These are generated/overridable, so they must live somewhere WRITABLE: in
    a packaged build the plugin dir is read-only (e.g. an AppImage squashfs
    mount), and writing the bundled `data/` raises OSError(Errno 30). setup()
    points RIG_BUILDER_DATA_DIR at a writable per-user dir and seeds the bundled
    defaults into it, so reads find them there and writes don't hit the
    read-only mount. Falls back to the bundled `data/` (then the legacy flat
    location) when the override isn't set (e.g. running the tools standalone).
    """
    override = os.environ.get("RIG_BUILDER_DATA_DIR")
    if override:
        return Path(override) / filename
    in_data = _plugin_dir / "data" / filename
    if in_data.exists():
        return in_data
    legacy = _plugin_dir / filename
    # Default new files into data/ even when neither exists yet.
    return legacy if legacy.exists() else in_data


def _atomic_write_json(path: Path, obj, indent: int = 2, **dumps_kwargs) -> None:
    """Persist `obj` as JSON atomically: serialize to a sibling `.tmp` file,
    then os.replace() it over `path`. A crash / full disk mid-write can then
    never leave a truncated JSON behind — the previous file survives intact.
    Extra keyword args (e.g. sort_keys) pass through to json.dumps."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(
        json.dumps(obj, indent=indent, ensure_ascii=False, **dumps_kwargs),
        encoding="utf-8",
    )
    os.replace(tmp, path)


_CUSTOM_GEAR_FILENAME = "rig_builder_custom_gear.json"
_CUSTOM_GEAR_CATEGORIES = ("amp", "cab", "pedal", "rack")

# "New" / DLC gear codenames: native gears that are NOT part of the original
# factory roster and so should read UNASSIGNED until the user maps them (like a
# user-added custom gear), even though they carry a bundled VST. Everything else
# native counts as a factory game gear and reads assigned. Extend as needed.
_NEW_DLC_GEARS = {
    "Bass_Pedal_NYRBS103",   # NYR BS103 bass-synth pedal — added post-factory
}


def _custom_gear_path() -> Path | None:
    """Path to the user's custom-gear store (a JSON list of user-authored
    amps/cabs/pedals/racks). Lives next to the settings file in the config
    dir. None when we have no config dir (shouldn't happen in the app)."""
    if _config_dir is None:
        return None
    return _config_dir / _CUSTOM_GEAR_FILENAME


def _load_custom_gear() -> list[dict]:
    """Return the list of user-defined custom gear records. Tolerant of a
    missing / corrupt file (returns [])."""
    path = _custom_gear_path()
    if path is None or not path.exists():
        return []
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        log.warning("%s unreadable — ignoring custom gear", _CUSTOM_GEAR_FILENAME,
                    exc_info=True)
        return []
    if isinstance(raw, dict):            # tolerate {"gear": [...]} shape
        raw = raw.get("gear") or raw.get("items") or []
    return [g for g in raw if isinstance(g, dict) and g.get("rs_gear")]


def _save_custom_gear(items: list[dict]) -> None:
    path = _custom_gear_path()
    if path is None:
        raise RuntimeError("no config dir for custom gear")
    _atomic_write_json(path, items)


def _custom_gear_catalog_item(rec: dict) -> dict:
    """Shape a stored custom-gear record like a /gear_catalog entry so it drops
    straight into the browser list, the node-editor palette, and the per-song
    picker (all read the same shape). Custom gear is always 'assigned' — it
    carries its own VST or NAM/IR — and flags `custom: true` so the UI can show
    an Edit/Delete affordance."""
    kind = (rec.get("kind") or "").lower()
    return {
        "rs_gear": rec.get("rs_gear"),
        "real_name": rec.get("real_name") or rec.get("name") or rec.get("rs_gear"),
        "type_tags": rec.get("type_tags", ""),
        "make": rec.get("make", ""),
        "model": rec.get("model", ""),
        "category": rec.get("category") or "other",
        "assigned": bool(rec.get("vst_path") or rec.get("file")),
        "kind": kind,
        "file": rec.get("file"),
        "vst_path": rec.get("vst_path"),
        "vst_format": rec.get("vst_format"),
        "vst_state": rec.get("vst_state"),
        "tone3000_id": None,
        "tone3000_title": None,
        "image": None,
        "tone3000_url": None,
        "rs_order": None,
        "variants": [],
        "mic_variants": [],
        # custom-gear extras
        "custom": True,
        "instrument": rec.get("instrument") or "all",
        "ui": rec.get("ui") or None,
        # Multi-capture amps (auto-downloaded from tone3000): the per-level NAM
        # files + the gain thresholds that switch between them. Lets the UI show
        # an editable clean/crunch/dist switch-point editor. None for normal gear.
        "gain_variants": rec.get("gain_variants") or None,
        "auto_tone3000": bool(rec.get("auto_tone3000")),
    }


# ── Gear mapping: game (RS) gear → one of the user's CUSTOM gears ────────────
# Instead of assigning a bare VST to a game gear, the user maps it to a whole
# custom gear (which carries its own VST + UI + params) and maps each RS knob to
# one of that custom gear's parameters. Persisted in rig_builder_gear_map.json:
#   { "Amp_JCM800": { "custom": "custom_amp_123",
#                     "params": { "Gain": "Drive", "Bass": "Bass" } } }
_GEAR_MAP_FILENAME = "rig_builder_gear_map.json"

# Fallback RS-knob sets when a gear has no entry in the knob table.
_DEFAULT_RS_KNOBS = {
    "amp":  ["Gain", "Bass", "Mid", "Treble", "Presence", "Volume"],
    "pedal": ["Level", "Tone", "Gain"],
    "rack": ["Mix", "Level"],
    "cab":  [],
}


def _gear_map_path() -> Path | None:
    if _config_dir is None:
        return None
    return _config_dir / _GEAR_MAP_FILENAME


def _load_gear_map() -> dict:
    path = _gear_map_path()
    if path is None or not path.exists():
        return {}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
        return raw if isinstance(raw, dict) else {}
    except (ValueError, OSError):
        log.warning("%s unreadable — ignoring gear map", _GEAR_MAP_FILENAME, exc_info=True)
        return {}


def _save_gear_map(m: dict) -> None:
    path = _gear_map_path()
    if path is None:
        raise RuntimeError("no config dir for gear map")
    _atomic_write_json(path, m)


def _rs_knobs_for_gear(rs_gear: str) -> list[str]:
    """The RS knob names for a game gear — taken from the shipped knob table
    (rs_knob_to_vst_param.json) when present, else a per-category default."""
    table = _load_knob_to_vst_table() or {}
    entry = table.get(rs_gear) or {}
    for _vst_name, mapping in entry.items():
        if isinstance(mapping, dict):
            knobs = [k for k in mapping if not str(k).startswith("_")]
            if knobs:
                return knobs
    info = _load_rs_to_real().get(rs_gear) or {}
    cat = info.get("category") or _gear_category(rs_gear) or "amp"
    return list(_DEFAULT_RS_KNOBS.get(cat, []))


def _custom_gear_params(rec: dict) -> list[str]:
    """The parameter names a custom gear exposes for mapping — the labels of the
    controls in its saved UI that are wired to a real plugin param."""
    ui = rec.get("ui") or {}
    out, seen = [], set()
    for c in (ui.get("controls") or []):
        if not isinstance(c, dict):
            continue
        lbl = (c.get("label") or "").strip()
        if lbl and c.get("param") is not None and lbl not in seen:
            seen.add(lbl)
            out.append(lbl)
    # Fall back to ALL labelled controls if none carry an explicit param id.
    if not out:
        for c in (ui.get("controls") or []):
            if isinstance(c, dict):
                lbl = (c.get("label") or "").strip()
                if lbl and lbl not in seen:
                    seen.add(lbl)
                    out.append(lbl)
    return out


def _is_custom_gear(gear_id: str) -> bool:
    return isinstance(gear_id, str) and gear_id.startswith("custom_")


def _installed_vst_block(gear_id: str) -> dict:
    """The knob-table variant mapping for the SPECIFIC VST installed for this
    native gear — a single variant, not a merge across every candidate plugin.
    Falls back to the first shipped variant when the installed stem doesn't
    resolve. This is what prevents the 'more options than knobs' inflation."""
    table = _load_knob_to_vst_table() or {}
    entry = table.get(gear_id) or {}
    if not entry:
        return {}
    try:
        pick = _pick_installed_primary_vst(gear_id, _build_known_vst_lookup())
    except Exception:
        pick = None
    if pick and pick.get("vst_path"):
        blk = entry.get(_vst_mapping_stem(pick["vst_path"]))
        if isinstance(blk, dict):
            return blk
    for v in entry.values():
        if isinstance(v, dict):
            return v
    return {}


def _gear_params(gear_id: str) -> list[str]:
    """The parameters a gear exposes as knob-mapping TARGETS — the real plugin
    params that actually get set. Custom → its created-VST control labels; native
    → the installed VST's own param names (single variant, deduped)."""
    rec = next((c for c in _load_custom_gear() if c.get("rs_gear") == gear_id), None)
    if rec:
        return _custom_gear_params(rec)
    out, seen = [], set()
    for k, spec in _installed_vst_block(gear_id).items():
        if str(k).startswith("_"):
            continue
        p = spec.get("param") if isinstance(spec, dict) else None
        if p and p not in seen:
            seen.add(p)
            out.append(p)
    return out


def _native_default_knobmap(gear_id: str) -> dict:
    """A native gear's own slot pre-fills each RS knob with the VST param it
    actually drives (its shipped mapping for the installed variant)."""
    block = _installed_vst_block(gear_id)
    out = {}
    for k in _rs_knobs_for_gear(gear_id):
        spec = block.get(k) if isinstance(block, dict) else None
        p = spec.get("param") if isinstance(spec, dict) else None
        if p:
            out[k] = p
    return out


def _load_cached_json(filename: str, *, post=None, empty=None):
    """Load the generated data file `<plugin>/data/<filename>` as JSON once,
    cached by filename (legacy flat location as fallback — see `_data_path`).

    Single replacement for the six near-identical `_load_*` JSON loaders.
    `post(raw)` optionally transforms the parsed JSON (wrap in
    `_CaseInsensitiveDict`, strip `_meta` keys, …); `empty()` builds the
    value returned when the file is absent or corrupt (defaults to a plain
    `{}`). Clear the cache entry with `_invalidate_cached_json(filename)`.
    """
    if filename in _json_cache:
        return _json_cache[filename]
    val = empty() if empty else {}
    path = _data_path(filename)
    if path.exists():
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            val = post(raw) if post else raw
        except (ValueError, OSError):
            log.error("%s is corrupt", filename, exc_info=True)
            val = empty() if empty else {}
    _json_cache[filename] = val
    return val


def _invalidate_cached_json(filename: str) -> None:
    _json_cache.pop(filename, None)


def _strip_meta_keys(raw: dict) -> dict:
    """Drop `_meta` / `_example_*` documentation keys, keeping dict values."""
    return {k: v for k, v in raw.items()
            if not k.startswith("_") and isinstance(v, dict)}


def _load_rs_to_real() -> dict:
    """Load (and cache) the rs_to_real.json map (ships pre-generated with
    the plugin in data/). Returns an empty dict if that bundled map is
    missing — the UI surfaces that case as a "needs setup" banner."""
    return _load_cached_json("rs_to_real.json")


def _invalidate_rs_to_real() -> None:
    _invalidate_cached_json("rs_to_real.json")


def _load_default_captures() -> dict:
    """Load (and cache) default_captures.json: a curated `rs_gear ->
    {tone3000_id, kind, model_id}` map shipped with the plugin. The
    batch / auto-download flows prefer these exact captures over a fresh
    tone3000 search, so a new install reproduces the maintainer's tone
    choices. Empty dict if the file is absent (then search/pick is used)."""
    return _load_cached_json("default_captures.json")


def _invalidate_default_captures() -> None:
    _invalidate_cached_json("default_captures.json")


# ── Amp gain variants (clean / crunch / dist) ──────────────────────────
#
# Real amps respond very differently at different gain settings: a Twin
# at gain=10 is sparkling clean; at gain=80 it's snarling. Tone3000
# captures are FIXED-setting snapshots — one NAM is one (amp + knob
# position). So if our `default_captures.json` ships a single "Twin
# clean" capture for `Amp_Twin`, every Twin tone in every song uses that
# clean character even when the song's amp knob is at 80.
#
# `gain_variants` in rs_to_real.json (or default_captures.json) lets a
# curator ship up to 3 captures per amp, tagged by what RS-gain range
# each one models. At download/assign time we look at the actual Gain
# knob value for that amp instance in that tone and pick the matching
# variant. Schema:
#
#   "Amp_Twin": {
#     "name": "Fender Twin Reverb",
#     "category": "amp",
#     "tone3000_query": "fender twin reverb",
#     "gain_variants": {
#       "clean":  { "tone3000_id": 12345, "rs_gain_range": [0, 35] },
#       "crunch": { "tone3000_id": 12346, "rs_gain_range": [35, 70] },
#       "dist":   { "tone3000_id": 12347, "rs_gain_range": [70, 100] }
#     }
#   }
#
# Amps without `gain_variants` keep the single-NAM behaviour exactly as
# before — this is purely additive.


# ── NAM loudness normalization ───────────────────────────────────────
#
# NAM model files (v0.5+) embed a `loudness` field in their JSON header
# — the integrated LUFS value the capture was authored at. Different
# captures land at wildly different LUFS (we've seen -10 to -26 across
# the same library), which translates into a perceived volume jump
# whenever the user switches songs that use different amps.
#
# We read that field once per file (cached), compute a per-NAM makeup
# `outputLevel = 10^((target - lufs) / 20)` and stamp it into each
# stage's state. Result: every NAM stage outputs at roughly the same
# loudness regardless of who captured it.
#
# Configurable target via settings (`nam_loudness_target_lufs`); default
# -18 LUFS is a comfortable middle ground (loud enough to drive the
# cab IR + master chain without clipping, quiet enough to leave
# headroom for transients). The ±12 dB cap prevents pathological
# captures (a -40 LUFS NAM) from blowing up the chain.

_nam_loudness_cache: dict[str, float | None] = {}
_nam_loudness_lock = threading.Lock()


def _read_nam_loudness(file_path: Path) -> float | None:
    """Pull the `loudness` field out of a NAM v0.5+ file's JSON header.

    NAM files start with a JSON object containing
    `{"version": "0.5.x", "metadata": {"loudness": -18.3, ...}, ...}`
    followed by the weights array. We only need the first ~4 KB to
    catch the loudness; reading more is wasteful. Returns None when
    the file is non-NAM, pre-v0.5 (no loudness), or unreadable —
    callers fall back to unity outputLevel.
    """
    try:
        with open(file_path, "rb") as fp:
            head = fp.read(4096).decode("utf-8", errors="ignore")
    except OSError:
        return None
    import re as _re
    m = _re.search(r'"loudness"\s*:\s*(-?\d+(?:\.\d+)?)', head)
    if not m:
        return None
    try:
        return float(m.group(1))
    except ValueError:
        return None


def _nam_loudness_for_path(path: Path) -> float | None:
    """Cached wrapper around `_read_nam_loudness`. The cache is keyed
    by absolute path; we never invalidate within a session because a
    NAM file's loudness is baked-in metadata that doesn't change after
    download (and the user restarts the plugin to pick up new files
    anyway).
    """
    key = str(path)
    with _nam_loudness_lock:
        if key in _nam_loudness_cache:
            return _nam_loudness_cache[key]
    val = _read_nam_loudness(path)
    with _nam_loudness_lock:
        _nam_loudness_cache[key] = val
    return val


def _nam_normalized_output_level(path: Path,
                                  effective_input_drive: float | None = None) -> float:
    """Compute the `outputLevel` to send to the engine for a NAM stage
    so the captured loudness lands at the configured target.

    `effective_input_drive` lets the caller declare the actual inputLevel
    they're sending to the stage (e.g. 1.0 for bass amps, 2.5 for guitar
    amps via `_amp_input_drive_for`). The makeup output level is divided
    by this so perceived loudness stays at the LUFS target regardless of
    the input boost. When omitted, we read the global setting — matches
    the legacy behaviour exactly.

    Falls back to 1.0 when the file lacks metadata (pre-v0.5 captures
    or hand-authored files). When normalization is disabled via
    settings, returns 1.0 (matches the legacy behaviour exactly).

    Cap is asymmetric: we tolerate a large *boost* (+20 dB) because
    high-gain amp captures sit naturally at -25 to -30 LUFS and need
    the full lift to land at the -6 LUFS target, but we cap the
    *attenuation* tighter (-9 dB) so the calmest clean captures stay
    audible. Symmetric ±12 caused all high-gain amps to clip at +12
    while clean amps got the full attenuation, flattening the chain
    to a uniformly clean-sounding wash. The boost cap is configurable
    via `nam_normalize_max_boost_db` / `nam_normalize_max_cut_db` for
    fine-tuning per machine.
    """
    settings = _load_settings()
    if not settings.get("normalize_nam_loudness", True):
        return 1.0
    loudness = _nam_loudness_for_path(path)
    if loudness is None:
        return 1.0
    target = float(settings.get("nam_loudness_target_lufs", -6.0))
    max_boost = float(settings.get("nam_normalize_max_boost_db", 20.0))
    max_cut = float(settings.get("nam_normalize_max_cut_db", 9.0))
    makeup_db = target - loudness
    makeup_db = max(-max_cut, min(max_boost, makeup_db))
    raw = 10.0 ** (makeup_db / 20.0)
    # The inputLevel boost we apply to feed the NAM at capture-level
    # multiplies the OUTPUT too (the amp's gain is post-input). Divide
    # the makeup by the EFFECTIVE input drive so the perceived loudness
    # lands at the target instead of target + input_drive_dB. Callers
    # that switch off the drive (bass amps via `_amp_input_drive_for`)
    # pass effective_input_drive=1.0 here so the division is consistent
    # with what was applied to the stage's inputLevel. Capped so the
    # output never goes super-quiet for a sky-high input drive setting.
    effective_input = effective_input_drive
    if effective_input is None:
        effective_input = float(settings.get("nam_input_drive", 1.0))
    effective_input = max(0.1, float(effective_input))
    return raw / effective_input


def _amp_input_drive_for(rs_gear: str | None, slot: str | None) -> float:
    """Return the inputLevel multiplier to feed a NAM stage.

    Rules:
      - Non-amp stages (pedals/racks/master pre+post) stay at unity 1.0.
        Driving a clean modulation pedal would over-saturate it.
      - Bass amps (rs_gear starting with 'Bass_Amp_') also stay at
        unity. tone3000 bass captures are typically authored at clean
        gain settings (e.g. Gallien-Krueger RB800 G1.0 / G3.0 / G5.0).
        Applying the guitar-amp 2.5× drive pushes the model well
        beyond its capture-time operating point and the output
        sounds distorted — exactly the symptom the user reported on
        Gallien-Krueger.
      - DI amps (rs_gear starting with 'DI_Amp_') stay at unity too.
        They're direct-input preamps / DI boxes (Avalon tube pre, AMS
        Neve 1073, Tech 21 SansAmp Bass Driver) used for acoustic/clean
        DI tones, captured CLEAN — the 2.5× guitar boost saturates them
        into buzz, wrecking an acoustic tone. The tone is shaped by the
        Acoustic Emulator pedal + cab IR, not by overdriving the DI.
      - Guitar amps use the configured `nam_input_drive` (default 2.5).
        Guitar captures are made at HOT gain so the model expects an
        input that's louder than what arrives from a live pickup; the
        boost restores capture-level signal.

    Both `rs_gear` and `slot` may be None (no gear/slot context, e.g.
    standalone audition of a file with no metadata): we still treat
    that as a guitar amp and apply the default drive. The frontend
    passes a sensible rs_gear whenever it can.
    """
    if (slot or "").lower() != "amp":
        return 1.0
    if rs_gear and (rs_gear.startswith("Bass_") or rs_gear.startswith("DI_Amp_")):
        return 1.0
    settings = _load_settings()
    return max(0.1, float(settings.get("nam_input_drive", 1.0)))


def _rs_gain_from_params_json(params_json: str | None, rs_gear: str | None = None):
    """Read the stored the game amp gain value from a preset_pieces row.

    Returns None when the amp does not expose a gain-like knob. The frontend
    uses this only to choose the chain-level input drive, so absence should
    mean "unknown" rather than forcing a synthetic crunch value.
    """
    try:
        knobs = json.loads(params_json or "{}")
    except Exception:
        return None
    if not isinstance(knobs, dict):
        return None
    gear_def = (_load_rs_to_real() or {}).get(rs_gear or "") if rs_gear else {}
    proxies = (gear_def or {}).get("gain_proxy_knobs") or []
    if proxies:
        vals = []
        for key in proxies:
            try:
                vals.append(float(knobs[key]))
            except (KeyError, TypeError, ValueError):
                continue
        if vals:
            return sum(vals) / len(vals)
    raw = knobs.get("Gain")
    if raw is None:
        return None
    try:
        return float(raw)
    except (TypeError, ValueError):
        return None


def _gear_rs_gain(piece: dict, gear_def: dict | None = None) -> float:
    """Read the RS Gain knob value (0-100) from a parsed piece's knobs.

    Default behaviour: look for a knob literally called "Gain". Returns
    50.0 (centre) when absent — that lands in 'crunch' territory for a
    typical 3-variant schema.

    Some the game amps don't expose a single "Gain" knob — the Plexi
    1959, for example, has Loudness1 + Loudness2 (the two channels of
    the original head) and you push them both for distortion. The
    gear definition can opt into a custom mapping by declaring
    `gain_proxy_knobs` — a list of knob names to read and average.

    Example in rs_to_real.json:
        "Amp_MarshallPlexi": {
            ...
            "gain_proxy_knobs": ["Loudness1", "Loudness2"],
            "gain_variants": { ... }
        }

    With the list above, knobs `{Loudness1: 88, Loudness2: 60, ...}`
    yields gain = 74 → "dist" instead of falling back to crunch.
    Missing knobs in the proxy list contribute 0; an empty result
    still falls back to 50 (so we never error out).
    """
    knobs = piece.get("knobs") or {}
    proxies = (gear_def or {}).get("gain_proxy_knobs") or []
    if proxies:
        vals = []
        for k in proxies:
            v = knobs.get(k)
            try:
                vals.append(float(v))
            except (TypeError, ValueError):
                continue
        if vals:
            return sum(vals) / len(vals)
        return 50.0
    g = knobs.get("Gain")
    if g is None:
        return 50.0
    try:
        return float(g)
    except (TypeError, ValueError):
        return 50.0


def _pick_amp_gain_variant(gear_def: dict, rs_gain: float) -> dict | None:
    """Pick the `gain_variants` entry whose `rs_gain_range` contains
    `rs_gain`, or the closest-by-centre fallback if nothing covers it.

    Returns the variant dict augmented with `_picked_level` (the key it
    came from — "clean"/"crunch"/"dist"/whatever the curator named it)
    for diagnostics + UI. Returns None when `gear_def` has no variants
    defined, signalling the caller to use the legacy single-NAM path.
    """
    variants = (gear_def or {}).get("gain_variants") or {}
    if not variants:
        return None
    try:
        g = float(rs_gain)
    except (TypeError, ValueError):
        g = 50.0
    # Pass 1: explicit range match.
    for level, v in variants.items():
        if not isinstance(v, dict):
            continue
        rng = v.get("rs_gain_range")
        if not (isinstance(rng, (list, tuple)) and len(rng) == 2):
            continue
        try:
            lo, hi = float(rng[0]), float(rng[1])
        except (TypeError, ValueError):
            continue
        if lo <= g <= hi:
            return {**v, "_picked_level": level}
    # Pass 2: closest range centre (lets a curator define only 2 variants
    # with non-adjacent ranges and still get a sensible pick for gain
    # values that fall between them).
    best_level, best_v, best_dist = None, None, float("inf")
    for level, v in variants.items():
        if not isinstance(v, dict):
            continue
        rng = v.get("rs_gain_range")
        if not (isinstance(rng, (list, tuple)) and len(rng) == 2):
            continue
        try:
            mid = (float(rng[0]) + float(rng[1])) / 2.0
        except (TypeError, ValueError):
            continue
        d = abs(g - mid)
        if d < best_dist:
            best_level, best_v, best_dist = level, v, d
    return {**best_v, "_picked_level": best_level} if best_v else None


def _build_default_captures() -> dict:
    """Snapshot the current DB's gear -> capture assignments into the
    shippable map. Picks, per gear, the most recent row that has a
    tone3000_id + file. Returns the dict (caller writes it to disk)."""
    conn = _get_conn()
    rows = conn.execute(
        "SELECT rs_gear_type, kind, file, tone3000_id, id FROM preset_pieces "
        "WHERE tone3000_id IS NOT NULL AND file IS NOT NULL AND file != '' "
        "ORDER BY id DESC"
    ).fetchall()
    out: dict[str, dict] = {}
    for gear, kind, file, t3kid, _id in rows:
        if gear in out:
            continue  # rows are DESC by id → first seen is the latest
        # model id is embedded in the filename: tone3000_{tone}_m{model}_...
        model_id = None
        m = re.search(r"_m(\d+)_", file or "")
        if m:
            model_id = int(m.group(1))
        out[gear] = {"tone3000_id": int(t3kid), "kind": kind, "model_id": model_id}
    return out


# ── VST seed catalog + RS-knob translation table (Fase E + knob mapping) ──
# (both cached by filename in `_json_cache` via `_load_cached_json`)


def _load_knob_to_vst_table() -> dict:
    """Load (and cache) the rs_knob_to_vst_param.json translation table.
    Strips out the `_meta` / `_example_*` documentation keys so callers
    don't have to filter. Missing file = empty (no auto-mapping available)."""
    return _load_cached_json("rs_knob_to_vst_param.json", post=_strip_meta_keys)


def _load_vst_seed_catalog() -> dict:
    """Load (and cache) the rs_gear_to_vst.json seed catalog: maps each
    `rs_gear_type` to a list of recommended VST/AU plugins (free first).
    Used by /vst/suggest. Missing file = empty dict (degrades to "no
    suggestions, user picks manually from the full known list")."""
    return _load_cached_json("rs_gear_to_vst.json")


def _load_vst_display_names() -> dict:
    """Load (and cache) vst_display_names.json: maps a bundled VST's stem
    (lowercased, alphanumeric-only — e.g. 'basschorus', 'bz1') to a copyright-
    free display name shown in the Gear catalog instead of the real make/model.
    Chief-family pedals map to just their model code (e.g. 'CB-3'); the rest to
    a clean product name. Missing file = empty dict (falls back to real_name)."""
    return _load_cached_json("vst_display_names.json")


def _load_pedal_type_tags() -> dict:
    """Load (and cache) pedal_type_tags.json: maps an rs_gear key to a space-
    separated string of searchable TYPE keywords (English + Spanish, accent-
    free) so typing a pedal type in the catalog / add-piece picker surfaces
    every gear of that type even when its display name is a bare model code
    (e.g. 'MT-2'). Authoritative — overrides the codename heuristic. Missing
    file or key = '' (search falls back to the codename synonym guess)."""
    return _load_cached_json("pedal_type_tags.json")


def _vst_display_stem(vst_path: str) -> str:
    """Match the UI's gStem: basename minus .vst3/.component, lowercased,
    non-alphanumerics stripped. Keep in sync with screen.js / pedal_canvas."""
    name = Path(vst_path or "").name
    name = re.sub(r"\.(vst3|component)$", "", name, flags=re.IGNORECASE)
    return re.sub(r"[^a-z0-9]", "", name.lower())


_GEAR_DISPLAY_NAME_CACHE: dict | None = None


def _cab_clone_name(rs_gear: str) -> str | None:
    """Clone (parody) display name for a cab gear, from real_cab_catalog.json —
    what we show in-app instead of the raw 'Cab_EN212C' code (cabs aren't in
    rs_to_real, so they have no other name). Handles a mic-position suffix
    (e.g. 'Cab_EN212C_5c') by falling back to the base gear."""
    cat = _load_cached_json("real_cab_catalog.json") or {}
    cabs = cat.get("cabs", cat)
    if not isinstance(cabs, dict):
        return None
    e = cabs.get(rs_gear)
    if not isinstance(e, dict) and "_" in rs_gear:
        e = cabs.get(rs_gear.rsplit("_", 1)[0])
    return e.get("name") if isinstance(e, dict) else None


def _gear_display_name(rs_gear: str, fallback: str = "") -> str:
    """Parody display name for a gear — the copyright-free product name shown
    EVERYWHERE the plugin's name appears (chain piece, add-piece picker, …),
    not just the Gear catalog. Resolves rs_gear → its bundled-VST stem (via
    rs_gear_to_vst.json) → vst_display_names.json; for cabs → the clone name in
    real_cab_catalog.json. Falls back to `fallback` (the real name) when the
    gear has no bundled VST / clone / curated name.
    Cached for the process (no hot reload, so the map is stable)."""
    global _GEAR_DISPLAY_NAME_CACHE
    if _GEAR_DISPLAY_NAME_CACHE is None:
        seed = _load_vst_seed_catalog() or {}
        disp = _load_vst_display_names() or {}
        m: dict[str, str] = {}
        for g, arr in seed.items():
            if not isinstance(arr, list):
                continue
            bundled = next((e.get("bundled") for e in arr
                            if isinstance(e, dict) and e.get("bundled")), None)
            if not bundled:
                continue
            dn = disp.get(_vst_display_stem(bundled))
            if dn:
                m[g] = dn
        _GEAR_DISPLAY_NAME_CACHE = m
    return _GEAR_DISPLAY_NAME_CACHE.get(rs_gear) or _cab_clone_name(rs_gear) or fallback


def _gear_bundled_vst(rs_gear: str) -> str | None:
    """The bundled VST path a gear resolves to (first entry in
    rs_gear_to_vst.json), or None. Used to collapse gears that REUSE the same
    bundled amp (e.g. Amp_AT20 reusing Bass_Amp_BT975B's Sampleg SBT-CL) so the
    catalog/picker lists each bundled model once."""
    arr = (_load_vst_seed_catalog() or {}).get(rs_gear)
    if not isinstance(arr, list):
        return None
    return next((e.get("bundled") for e in arr
                 if isinstance(e, dict) and e.get("bundled")), None)


def _bundled_vst_plugins() -> list[dict]:
    """Return VST/AU plugins shipped inside this plugin's own ``vst/`` dir.

    These are not installed in the OS VST folders and may never appear in the
    native engine's scan cache. Listing them here lets batch mapping and the UI
    resolve our built-in DSP by absolute path immediately after a restart.
    """
    root = _plugin_dir / "vst"
    if not root.exists():
        return []
    # The bundles are filed under category subdirs (vst/amps, vst/pedals,
    # vst/racks); search the root and those one-level subdirs but NOT the C++
    # `src/` tree and NOT inside the .vst3 bundles themselves (which embed
    # per-platform binaries like Contents/x86_64-win/<name>.vst3).
    search_dirs = [root]
    for d in sorted(root.iterdir()):
        if d.is_dir() and d.name != "src" and not d.name.endswith((".vst3", ".component")):
            search_dirs.append(d)
    out = []
    for suffix, fmt in ((".vst3", "VST3"), (".component", "AudioUnit")):
        entries = []
        for base in search_dirs:
            entries.extend(base.glob(f"*{suffix}"))
        for entry in sorted(entries):
            if not entry.exists():
                continue
            name = entry.name[:-len(suffix)]
            out.append({
                "name": name,
                "manufacturer": "Rig Builder",
                "category": "Fx",
                "format": fmt,
                "path": str(entry.resolve()),
                "uid": f"bundled-{fmt}-{name}",
                "isInstrument": False,
                "bundled": True,
            })
    return out


def _build_known_vst_lookup() -> dict:
    """Read rig_builder_known_vsts.json (populated by the frontend after the
    user clicks Settings → Scan for plugins) and return a lookup keyed by
    lowercased plugin name. Each value is a list of installed entries
    (one per format) sorted VST3-first so VST3 wins over AU on ties.

    Used by the batch worker to promote each gear to its primary VST when
    installed. Returns empty dict if the cache file is missing or
    unreadable — caller falls back to the NAM/IR resolution path.
    """
    out: dict[str, list[dict]] = {}
    for p in _bundled_vst_plugins():
        out.setdefault(p["name"].lower(), []).append(p)
    if _config_dir is None:
        return out
    path = _config_dir / "rig_builder_known_vsts.json"
    if not path.exists():
        return out
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        log.warning("known_vsts file unreadable", exc_info=True)
        return out
    plugins = (
        data.get("plugins") if isinstance(data, dict)
        else (data if isinstance(data, list) else [])
    )
    if not isinstance(plugins, list):
        return out
    for p in plugins:
        if not isinstance(p, dict) or not p.get("name") or not p.get("path"):
            continue
        out.setdefault(p["name"].lower(), []).append(p)
    for entries in out.values():
        entries.sort(key=lambda e: 0 if (e.get("format") or "").upper() == "VST3" else 1)
    return out


def _pick_installed_primary_vst(rs_gear: str, known_lookup: dict) -> dict | None:
    """Return {vst_path, vst_format} for the first installed primary VST
    for this gear, or None if none of the gear's recommended VSTs are
    installed.

    Reads `rs_gear_to_vst.json` order — first entry per gear IS the
    primary (user-curated). Walks the candidates; the first one whose
    `name` matches an installed plugin wins. When the candidate declares
    a preferred format (VST3/AU), match it exactly; otherwise pick the
    first installed entry (VST3-first ordering from
    _build_known_vst_lookup).
    """
    if not rs_gear:
        return None
    seed = _load_vst_seed_catalog() or {}
    candidates = seed.get(rs_gear)
    if not isinstance(candidates, list):
        return None
    # Prefer bundled Rig Builder plugins over external scan results, even when
    # the curated list also contains a third-party fallback. This keeps rescan /
    # batch output on the VSTs shipped with rig_builder instead of silently
    # drifting back to NAM/external choices.
    for cand in candidates:
        if not isinstance(cand, dict):
            continue
        bundled = cand.get("bundled")
        if bundled:
            bpath = _plugin_dir / bundled
            if bpath.exists():
                return {"vst_path": str(bpath),
                        "vst_format": cand.get("format") or "VST3"}
    for cand in candidates:
        if not isinstance(cand, dict):
            continue
        if cand.get("bundled"):
            continue
        if not cand.get("name") or not known_lookup:
            continue
        installed = known_lookup.get(cand["name"].lower())
        if not installed:
            continue
        wanted_fmt = (cand.get("format") or "").upper()
        if wanted_fmt:
            for entry in installed:
                if (entry.get("format") or "").upper() == wanted_fmt:
                    return {"vst_path": entry["path"],
                            "vst_format": entry["format"]}
        return {"vst_path": installed[0]["path"],
                "vst_format": installed[0].get("format") or "VST3"}
    return None


_AMP_EQ_RS_KEYS = {
    "bass", "mid", "middle", "treble", "pres", "presence", "res", "resonance",
    "lo", "low", "lomid", "hi", "high", "himid",
}


def _vst_mapping_stem(vst_path: str) -> str:
    name = Path(vst_path or "").name
    for ext in (".vst3", ".component"):
        if name.lower().endswith(ext):
            return name[:-len(ext)].lower()
    return name.lower()


def _amp_default_role_for_param(param_name: str) -> str | None:
    n = re.sub(r"[^a-z0-9]+", " ", str(param_name or "").lower()).strip()
    if not n:
        return None
    if re.search(r"\bgain boost\b|\bboost\b", n):
        return None
    if re.search(r"\b(bass|mid|middle|treble|presence|resonance)\b", n):
        return "eq"
    if re.search(r"^eq\s*\d+\b|\bgeq\b|\bgain\s+\d+\s+eq\b", n):
        return "eq"
    if re.search(r"\b(gain|drive)\b", n):
        return "gain"
    if re.search(r"\b(master|volume|vol|output|level|loudness)\b", n):
        return "volume"
    return None


def _apply_amp_open_defaults(
    rs_gear: str,
    vst_path: str,
    params_by_name: dict | None,
    knob_table: dict,
) -> dict:
    """Gear-level amp defaults: gain 30%, EQ centered, volume/master 60%.

    This intentionally works on the VST state envelope, not on stored
    the game `params_json`, so existing song knob maps remain untouched.
    """
    params = dict(params_by_name or {})
    if not rs_gear or not vst_path or _gear_category(rs_gear) != "amp":
        return params

    gear_block = (knob_table or {}).get(rs_gear) or {}
    vst_block = gear_block.get(_vst_mapping_stem(vst_path)) or {}
    protected: set[str] = set()

    for rs_knob, spec in vst_block.items():
        if not isinstance(spec, dict):
            continue
        pname = spec.get("param")
        if not isinstance(pname, str) or not pname:
            continue
        key = str(rs_knob).lower()
        if key == "gain":
            params[pname] = 0.30
            protected.add(pname.lower())
        elif key in _AMP_EQ_RS_KEYS:
            params[pname] = 0.50
            protected.add(pname.lower())

    for pname in list(params.keys()):
        if str(pname).lower() in protected:
            continue
        role = _amp_default_role_for_param(pname)
        if role == "gain":
            params[pname] = 0.30
        elif role == "eq":
            params[pname] = 0.50
        elif role == "volume":
            params[pname] = 0.60

    return params


def _compute_vst_state_for_piece(rs_gear: str, vst_path: str,
                                  params_dict: dict | None) -> str | None:
    """Compute the JSON `{"params": {...}}` envelope to stamp into
    preset_piece.vst_state for a freshly-auto-assigned VST primary.

    Mirrors apply_vst_state.py's _build_params_for_piece logic — same
    `_VST_PARAM_RANGES`, same `_static` block + per-knob translation —
    so the batch worker produces the same state the standalone bulk
    script would. Without this, fresh-installed pedals would load
    their VST primaries at plugin defaults regardless of RS knob
    settings, defeating the point of the auto-mapping.

    Returns the JSON string, or `None` when no mapping exists for this
    (gear, vst) pair (caller leaves vst_state NULL and the plugin
    opens at defaults — same as a 📸 Capture that hasn't been saved).
    """
    if not rs_gear or not vst_path:
        return None
    try:
        # apply_vst_state.py moved under tools/ in the v1.3.2 restructure; it's
        # still a RUNTIME dependency here (the RS-knob → VST-param mapper). Put
        # tools/ on sys.path so `import apply_vst_state` resolves AND its own
        # `from common import …` works (common lives at tools/common).
        _tools_dir = str(_plugin_dir / "tools")
        if _tools_dir not in sys.path:
            sys.path.insert(0, _tools_dir)
        import apply_vst_state as _avs
    except ImportError:
        return None
    knob_table = _load_knob_to_vst_table()
    if not knob_table:
        return None
    try:
        result = _avs._build_params_for_piece(
            rs_gear, vst_path,
            json.dumps(params_dict or {}), knob_table,
        )
    except Exception:
        log.warning("vst_state compute failed for %s / %s",
                    rs_gear, vst_path, exc_info=True)
        return None
    if result is None:
        params_by_name = {}
    else:
        params_by_name, _skipped = result
    return json.dumps({"params": params_by_name})


def _compute_gear_open_vst_state(rs_gear: str, vst_path: str) -> str | None:
    """Default state for opening a gear-level VST editor/audition.

    This is intentionally separate from _compute_vst_state_for_piece(), which
    preserves song-specific the game knob mapping. Gear-level browsing has no
    song tone, so amps open at neutral musical defaults instead.
    """
    state = _compute_vst_state_for_piece(rs_gear, vst_path, {}) or "{}"
    try:
        env = json.loads(state)
        params = env.get("params") if isinstance(env, dict) else {}
    except (ValueError, TypeError):
        params = {}
    if not isinstance(params, dict):
        params = {}
    params = _apply_amp_open_defaults(
        rs_gear, vst_path, params, _load_knob_to_vst_table() or {})
    return json.dumps({"params": params})


def _effective_vst_state_for_piece(
    rs_gear: str,
    vst_path: str,
    vst_state: str | None,
    params_json: str | dict | None,
) -> str | None:
    """Return stored VST state, or derive it from the game knobs.

    UI assignment endpoints may leave `vst_state` empty while preserving the
    original `params_json`. For bundled mapped VSTs that should still load the
    the game knob values, not plugin defaults.
    """
    if vst_state:
        try:
            env = json.loads(vst_state)
            # Honor a stored state only when it actually carries settings:
            # an opaque blob, or a NON-EMPTY params dict. A stale
            # `{"params": {}}` (saved when the gear's VST stem didn't yet
            # resolve in rs_knob_to_vst_param.json) falls through so we
            # recompute from the RS knobs below — otherwise the pedal/rack
            # would keep playing at plugin defaults forever.
            if isinstance(env, dict) and (
                env.get("opaque")
                or (isinstance(env.get("params"), dict) and env.get("params"))
            ):
                return vst_state
        except (ValueError, TypeError):
            return vst_state

    if isinstance(params_json, dict):
        knobs = params_json
    else:
        try:
            knobs = json.loads(params_json or "{}") or {}
        except (ValueError, TypeError):
            knobs = {}
    if not isinstance(knobs, dict) or not knobs:
        return vst_state
    return _compute_vst_state_for_piece(rs_gear, vst_path, knobs) or vst_state


class _CaseInsensitiveDict(dict):
    """A dict whose `.get(key)` falls back to a case-insensitive lookup
    when the exact key isn't present. Used for the rs_cab_to_ir and
    rs_cab_mic_map loaders because the DB stores some cab codenames
    fully uppercased (`Cab_MARSHALL1960A`) while the source JSON uses
    proper case (`Cab_Marshall1960A`). Pure dict otherwise — iteration
    and len behave normally.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._lower_index = {k.lower(): k for k in self.keys()}

    def get(self, key, default=None):
        if key in self:
            return super().get(key)
        real_key = self._lower_index.get(str(key).lower())
        if real_key is None:
            return default
        return super().get(real_key, default)


def _load_rs_cab_to_ir() -> dict:
    """Load (and cache) the rs_cab_to_ir.json map (ships pre-generated
    with the plugin in data/). Each key is a game gear entity (e.g.
    `Cab_4x12_AT_CL`); the value is `{sound_bank, irs: [relpaths under
    nam_irs/], category}`.

    Returns an empty dict if that bundled map is missing. The deep-link
    path still works without it; this is purely a "do we have a local
    IR for this cab?" lookup.
    """
    return _load_cached_json("rs_cab_to_ir.json",
                             post=_CaseInsensitiveDict, empty=_CaseInsensitiveDict)


def _invalidate_rs_cab_to_ir() -> None:
    global _effect_to_mic_cache
    _invalidate_cached_json("rs_cab_to_ir.json")
    _invalidate_cached_json("rs_cab_mic_map.json")
    _effect_to_mic_cache = None


def _load_rs_cab_mic_map() -> dict:
    """Load (and cache) `rs_cab_mic_map.json` — the table that maps each
    cab's mic-position suffix (`5c`, `cc`, `te`, …) to an IR file plus a
    friendly label ("Dynamic Cone", "Condenser Edge", …). Ships
    pre-generated with the plugin in data/.

    Schema:
      {
        "Bass_Cab_AT1150BC": {
          "5c": {ir_index, ir_file, mic_type, position, label, effect_name},
          ...
        },
        ...
      }

    Returns {} if the bundled map is missing — in that case callers fall
    back to the numbered IR list from rs_cab_to_ir.json (legacy behaviour).
    """
    return _load_cached_json("rs_cab_mic_map.json",
                             post=_CaseInsensitiveDict, empty=_CaseInsensitiveDict)


def _load_cab_overrides() -> dict:
    """Load (and cache) `rb_cab_overrides.json` — OUR own synthesized/captured
    cab IRs that OVERRIDE the Rocksmith-extracted mic-position table for a cab.

    Schema (one entry per cab gear key; ir_dir carries the clone-named
    per-cab subfolder, prefix is empty):
      { "Cab_MARSHALL1960A": { "ir_dir": "cabs/Marsten_1960A_4x12", "prefix": "" } }

    The per-suffix file is NOT listed here — it is derived from the *authoritative*
    Wwise `effect_name` of each RS mic-map entry (see `_override_variant`), which
    also auto-fixes the condenser label rotation present in some extracted maps.

    These ship WITH the plugin under nam_irs/<ir_dir>/, so the song editor marks
    them always-available (no fragile on-disk existence gate — that flaked when
    the generated mic-map was edited in place), they get the same +18 dB
    convolution makeup as RS cabs via `_is_synth_cab_ir`, and they are exempt
    from the global 'bypass all Rocksmith cabs' (their path is not under
    nam_irs/rocksmith/, so `_is_rocksmith_ir_file` is False)."""
    return _load_cached_json("rb_cab_overrides.json",
                             post=_CaseInsensitiveDict, empty=_CaseInsensitiveDict)


# effect_name token -> our filename stem / friendly labels
_OVR_MIC = {"57": "dyn", "condenser": "cond", "ribbon": "ribbon", "tube": "tube"}
_OVR_MIC_LABEL = {"dyn": "Dynamic", "cond": "Condenser", "ribbon": "Ribbon", "tube": "Tube"}
_OVR_POS_LABEL = {"cone": "Cone", "edge": "Edge", "offaxis": "OffAxis"}
_OVR_POS_DESC = {"cone": "Cone (close)", "edge": "Edge", "offaxis": "Off-axis"}


def _override_variant(ovr: dict, entry: dict) -> dict | None:
    """Resolve OUR cab IR for one RS mic-map entry, deriving mic + position from
    the entry's `effect_name` (Cab_<model>_<mic>_<pos>) — NOT its friendly
    `label`, which is rotated for the condenser positions in some maps.

    Returns {ir_file, label, position} or None when effect_name is unparseable
    (caller then falls back to the RS file for that suffix)."""
    parts = (entry.get("effect_name") or "").split("_")
    if len(parts) < 2:
        return None
    mic = _OVR_MIC.get(parts[-2].lower())
    pos = parts[-1].lower()
    if not mic or pos not in _OVR_POS_DESC:
        return None
    ir_dir = str(ovr.get("ir_dir") or "cabs").strip("/")
    prefix = str(ovr.get("prefix") or "")
    # New layout: ir_dir carries the clone-named subfolder and prefix is empty,
    # so files are `cabs/<Clone>/<mic>_<pos>.wav`. Legacy overrides that still
    # carry a prefix fall back to `<ir_dir>/<prefix>_<mic>_<pos>.wav`.
    stem = f"{prefix}_{mic}_{pos}" if prefix else f"{mic}_{pos}"
    return {
        "ir_file": f"{ir_dir}/{stem}.wav",
        "label": f"{_OVR_MIC_LABEL[mic]} {_OVR_POS_LABEL[pos]}",
        "position": _OVR_POS_DESC[pos],
    }


def _cab_clone_slug(name: str) -> str:
    """Filesystem-safe per-cab folder name from a catalog clone name — MUST
    match tools/generate_real_cab_irs.py `clone_slug` (the brother's subfolder
    IR layout)."""
    s = (name or "").strip().replace(".", "")
    s = re.sub(r"[^0-9A-Za-z_-]+", "_", s)
    return re.sub(r"_+", "_", s).strip("_")


_FLAT_CAB_RE = re.compile(
    r"(^|/)cabs/RC_(?P<key>.+)_(?P<mic>dyn|cond|ribbon|tube)_"
    r"(?P<pos>cone|edge|offaxis)\.wav$", re.IGNORECASE)


def _migrate_flat_cab_ir_path(ir_path):
    """Self-heal a STALE flat cab IR path → the current per-cab subfolder layout.

    An earlier backfill wrote `cabs/RC_<KEY>_<mic>_<pos>.wav` into preset_pieces;
    the IRs later moved to `cabs/<Clone>/<mic>_<pos>.wav` and the flat files were
    removed, so those stored paths now point at nothing and the cab plays silent.
    Rewrite them here (read/chain-build time) so old DB rows — or a re-seed that
    resurrects the flat name — keep working with no DB migration. Returns the
    path unchanged when it isn't an old flat cab path."""
    if not ir_path:
        return ir_path
    m = _FLAT_CAB_RE.search(str(ir_path).replace("\\", "/"))
    if not m:
        return ir_path
    key, mic, pos = m.group("key"), m.group("mic").lower(), m.group("pos").lower()
    cat = (_load_cached_json("real_cab_catalog.json") or {}).get("cabs", {})
    # RC_<KEY> was "RC_" + gear.replace("Cab_", "") → recover the gear key.
    gear = next((g for g in cat if g.replace("Cab_", "") == key), None)
    if not gear:
        return ir_path
    slug = _cab_clone_slug(cat[gear].get("name") or gear)
    rel = f"cabs/{slug}/{mic}_{pos}.wav"
    pp = Path(str(ir_path))
    if pp.is_absolute():
        # tail was <irs_root>/cabs/RC_....wav → swap the "cabs/…" tail
        return str(pp.parent.parent.parent / rel)
    return rel


def _apply_cab_override(ir_path):
    """Auto-substitute a Rocksmith cab IR with OUR own equivalent.

    If `ir_path` is a Rocksmith mic-position IR for a cab we ship our own IRs
    for (rb_cab_overrides.json), return OUR IR for the SAME mic position —
    matched by the RS mic-map's `ir_file` basename, then resolved through the
    authoritative `effect_name`. Otherwise return `ir_path` unchanged.

    This makes every song that references an overridden RS cab play our own
    distributable IR automatically, with NO per-song edit and WITHOUT touching
    the stored preset_pieces row (we rewrite only at read / chain-build time).

    Path form is preserved: an absolute `<irs_root>/rocksmith/<f>.wav` maps to
    an absolute `<irs_root>/<ir_dir>/<our>.wav` (so the native engine resolves
    it like any cab stage); a relative `rocksmith/<f>.wav` maps to the relative
    `<ir_dir>/<our>.wav` (UI display / `assigned.file`). Idempotent — a path
    already under our cab dir isn't a Rocksmith file, so it returns unchanged."""
    if not ir_path:
        return ir_path
    s = str(ir_path)
    # Self-heal a stale flat `cabs/RC_..._<mic>_<pos>.wav` → subfolder layout
    # first (covers our own stored 'ir' pieces, which aren't Rocksmith paths).
    migrated = _migrate_flat_cab_ir_path(s)
    if migrated != s:
        return migrated
    if not _is_rocksmith_ir_file(s):
        return ir_path
    overrides = _load_cab_overrides()
    if not overrides:
        return ir_path
    mic_map = _load_rs_cab_mic_map()
    base = Path(s).name.lower()
    for cab_key, ovr in overrides.items():
        if not isinstance(ovr, dict):
            continue                                    # skip the "_meta" doc string
        for entry in (mic_map.get(cab_key) or {}).values():
            ef = entry.get("ir_file") or ""
            if ef and Path(ef).name.lower() == base:
                o = _override_variant(ovr, entry)
                if not o:
                    return ir_path
                rel = o["ir_file"]
                pp = Path(s)
                # RS IRs live at <irs_root>/rocksmith/<file>; swap the tail.
                return str(pp.parent.parent / rel) if pp.is_absolute() else rel

    # Fallback: the cab HAS an override + a clone on disk, but the RS mic-map has
    # no entry matching this file — the PA/hi-fi novelty voicings (Cab_PA600C /
    # Cab_PA999C) are absent from the Wwise map, so the by-basename match above
    # can't fire and the tone would keep playing the raw Rocksmith IR. Parse the
    # cab key straight from the filename (cab_pa600c_00.wav -> cab_pa600c) and
    # map to the clone's neutral Dynamic Cone so it STILL plays parody, never
    # Rocksmith. (These voicings are character curves — position-agnostic.)
    stem = re.sub(r"_\d+$", "", Path(s).stem).lower()
    for cab_key, ovr in overrides.items():
        if not isinstance(ovr, dict) or cab_key.lower() != stem:
            continue
        ir_dir = str(ovr.get("ir_dir") or "cabs").strip("/")
        prefix = str(ovr.get("prefix") or "")
        neutral = f"{prefix}_dyn_cone" if prefix else "dyn_cone"
        rel = f"{ir_dir}/{neutral}.wav"
        pp = Path(s)
        return str(pp.parent.parent / rel) if pp.is_absolute() else rel
    return ir_path


# Neutral default cab for generic/unmapped cab gears (the RS "Cabinets" placeholder,
# or any cab not in rb_cab_overrides). Slot-sensitive: a bass tone gets a bass cab,
# a guitar tone the 2x12. Matches the frontend RB_DEFAULT_CAB_GEAR / _BASS so the
# audio IR and the shown cab art agree.
_DEFAULT_CAB_GEAR = "Cab_EN212C"              # guitar 2x12
_DEFAULT_BASS_CAB_GEAR = "Bass_Cab_AT810BC"   # SVT 8x10 — neutral bass default


def _default_cab_gear_for_rows(rows) -> str:
    """Bass vs guitar default cab for a tone whose cab is the generic 'Cabinets'
    placeholder — decided by whether the tone carries a bass amp/gear (Bass_* or a
    DI preamp). `rows` are the preset_pieces tuples (rs_gear_type at index 3)."""
    for r in rows:
        g = str((r[3] if len(r) > 3 else "") or "")
        if g.startswith("Bass_") or g.startswith("DI_Amp"):
            return _DEFAULT_BASS_CAB_GEAR
    return _DEFAULT_CAB_GEAR


def _override_ir_for_cab(rs_gear: str | None, irs_root, default_gear: str | None = None) -> str | None:
    """Resolve OUR shipped override IR (rb_cab_overrides) for a cab GEAR that has
    no assigned IR file — so a song whose cabinet seeded to kind='none' (the RS
    game IR doesn't ship and nothing was downloaded) still gets a real cab at
    playback instead of dropping to a thin, cab-less sound.

    Returns a relpath under nam_irs (e.g. "cabs/<Clone>/dyn_cone.wav") that exists
    on disk, or None. Uses a neutral default mic position (Dynamic Cone); the user
    can still fine-tune the exact mic in the Cab Room, which persists its own IR."""
    if not rs_gear or irs_root is None:
        return None
    overrides = _load_cab_overrides()
    if not overrides:
        return None
    ovr = overrides.get(rs_gear)
    if not isinstance(ovr, dict):
        base = re.sub(r"_[a-z0-9]{2}$", "", str(rs_gear), flags=re.I)   # drop a mic-pos suffix
        if base != rs_gear:
            ovr = overrides.get(base)
    if not isinstance(ovr, dict):
        # Generic/unmapped cab (e.g. the RS "Cabinets" placeholder that never got
        # promoted to a specific modeled cab) — fall back to a neutral DEFAULT cab
        # (bass or guitar, chosen by the caller) so it still gets a real, loudness-
        # matched bundled IR (and cab art in the UI) instead of a thin/cab-less
        # sound or a stale other/*.wav download.
        ovr = overrides.get(default_gear or _DEFAULT_CAB_GEAR)
    if not isinstance(ovr, dict):
        return None
    ir_dir = ovr.get("ir_dir")
    if not ir_dir:
        return None
    # Prefer a close dynamic mic; fall back to a condenser, then any variant.
    for rel in (f"{ir_dir}/dyn_cone.wav", f"{ir_dir}/cond_cone.wav"):
        p = _safe_child(irs_root, rel)
        if p and p.exists():
            return rel
    cab_dir = Path(irs_root) / ir_dir
    if cab_dir.is_dir():
        wavs = sorted(cab_dir.glob("*.wav"))
        if wavs:
            return f"{ir_dir}/{wavs[0].name}"
    return None


def _install_bundled_cab_irs() -> None:
    """Install OUR bundled, distributable cab IRs into <config>/nam_irs/cabs/.

    The override layer (rb_cab_overrides.json) points songs/catalog at
    `cabs/<Clone>/<mic>_<pos>.wav` (per-cab clone-named subfolders), which the
    engine loads from nam_irs/. So a fresh install needs those files on disk. We
    ship them under `<plugin>/assets/cab_irs/` and copy any that are MISSING here
    — never overwriting, so a user's own re-synthesized IR is preserved. Runs
    once at `setup()`; no-op when either dir is absent."""
    if not _config_dir:
        return
    src = Path(__file__).resolve().parent / "assets" / "cab_irs"
    if not src.is_dir():
        return
    dst = _config_dir / "nam_irs" / "cabs"
    try:
        dst.mkdir(parents=True, exist_ok=True)
        n = 0
        # Recursive: cab IRs now live in per-cab clone-named subfolders
        # (cabs/<Clone>/<mic>_<pos>.wav), so mirror the tree. `_legacy/` and any
        # stray files copy too — harmless, nothing references them.
        for wav in sorted(src.rglob("*.wav")):
            target = dst / wav.relative_to(src)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(wav, target)
                n += 1
        if n:
            log.info("installed %d bundled cab IR(s) into nam_irs/cabs", n)
    except Exception:
        log.exception("bundled cab IR install failed")


# ── NAM/IR storage layout — category subdirs ─────────────────────────
#
# Before v1.2 every downloaded NAM landed in `nam_models/` flat and every
# IR in `nam_irs/` flat, with the engine resolving `presets.model_file` /
# `preset_pieces.file` via `_safe_child(models_dir, name)`. That worked
# but made the folders impossible to browse at scale: 150+ NAMs across
# amps, pedals and racks all in one bucket.
#
# v1.2 layout:
#     nam_models/amps/<name>.nam
#     nam_models/pedals/<name>.nam
#     nam_models/racks/<name>.nam
#     nam_models/other/<name>.nam   ← uploads + uncategorised
#     nam_irs/cabs/<name>.wav
#     nam_irs/other/<name>.wav      ← RS-extracted IRs + leftovers
#
# The sister `nam_tone` plugin's `_safe_child` already accepts subdir
# paths (it just validates `(root / name).resolve()` stays under root),
# so playback Just Works with `model_file = "amps/foo.nam"`. The
# migration in `_migrate_nam_storage_to_subdirs` moves existing flat
# files into the new layout AND rewrites DB references to keep playback
# unbroken across the upgrade. Idempotent: a sentinel file
# `.nam_layout_v2` is dropped after a successful pass so subsequent
# restarts skip the work.

# Map rs_to_real `category` → folder name. Pluralised so the layout
# reads naturally in Finder.
_CATEGORY_SUBDIR = {
    "amp":   "amps",
    "pedal": "pedals",
    "rack":  "racks",
    "cab":   "cabs",
}


def _category_subdir_for_gear(rs_gear_type: str | None) -> str:
    """Pick the storage subdir for a NAM/IR linked to this RS gear.

    Looks up the gear's `category` in rs_to_real.json and maps it to the
    pluralised folder. Anything we can't classify (NULL gear, missing
    map entry, unknown category) lands in `other/` — including user
    uploads (which we don't tag with an rs_gear).
    """
    if not rs_gear_type:
        return "other"
    info = (_load_rs_to_real() or {}).get(rs_gear_type)
    if not info:
        return "other"
    return _CATEGORY_SUBDIR.get(info.get("category"), "other")


def _classify_filename_by_db(conn: sqlite3.Connection,
                             filename: str) -> str:
    """Resolve a NAM/IR's category by joining preset_pieces → rs_to_real.

    Used by the migration to decide which subdir a flat file should
    move into. Three lookup tiers, in order:

      1. **preset_pieces.file** match → take that row's rs_gear_type.
         Most NAMs have at least one piece referencing them.
      2. **Filename pattern fallback.** Our downloader names files
         `tone3000_<tid>_m<mid>_<rs_gear>.<ext>`, so we can parse the
         rs_gear out even when no DB row points at the file — happens
         when a song was deleted but the NAM stuck around (orphan), or
         when the file was downloaded for an audition without being
         assigned to any preset. Without this fallback every orphan
         landed in `other/`, which is misleading.
      3. **presets.model_file/ir_file** match → without an rs_gear we
         can't classify, so this case still falls to `other`.

    Returns the subdir name (`amps`/`pedals`/`racks`/`cabs`/`other`).
    """
    row = conn.execute(
        "SELECT rs_gear_type FROM preset_pieces WHERE file = ? LIMIT 1",
        (filename,),
    ).fetchone()
    if row and row[0]:
        return _category_subdir_for_gear(row[0])

    # Tier 2: parse rs_gear out of our own filename convention.
    # `tone3000_<tone_id>_m<model_id>_<rs_gear>.<ext>` → grab the
    # rs_gear segment, then look it up. We anchor on the
    # `tone3000_<digits>_m<digits>_` prefix to avoid false positives
    # on user-named uploads.
    import re as _re
    m = _re.match(r"tone3000_\d+_m\d+_([^.]+)\.(nam|wav)$", filename)
    if m:
        rs_gear = m.group(1)
        info = (_load_rs_to_real() or {}).get(rs_gear)
        if info:
            return _CATEGORY_SUBDIR.get(info.get("category"), "other")

    # Tier 3: presence in `presets` confirms the file is referenced
    # but doesn't tell us the gear. Falls through to "other".
    return "other"


def _migrate_nam_storage_to_subdirs() -> dict:
    """One-time migration: shuffle flat NAM/IR files into category subdirs.

    Idempotent — the sentinel `.nam_layout_v2` is dropped after a
    successful pass and the function short-circuits on subsequent
    restarts. Safe to call from `_get_conn()` even when the user has
    never downloaded a single file; in that case nothing happens.

    Strategy per file in `nam_models/` (and `nam_irs/`):
      1. If the filename has a "/" — it's already in a subdir, skip.
      2. Classify via DB join (`preset_pieces.file` → `rs_gear_type` →
         category → subdir).
      3. `shutil.move()` the file. On success, update every DB column
         that referenced the bare name to the new "subdir/name" path
         (`presets.model_file`, `presets.ir_file`, `preset_pieces.file`).
      4. If anything throws, leave the file where it is — better a
         half-migrated layout than a broken one. The sentinel only gets
         written when the loop completes without exceptions.
    """
    summary = {"nam_moved": 0, "ir_moved": 0, "skipped": 0,
               "db_rows_updated": 0, "errors": []}
    if _config_dir is None:
        return summary
    sentinel = _config_dir / ".nam_layout_v2"
    if sentinel.exists():
        return summary

    conn = _get_conn()
    for kind, root_name in [("nam", "nam_models"), ("ir", "nam_irs")]:
        root = _config_dir / root_name
        if not root.exists():
            continue
        # Snapshot the file list so the iteration isn't disturbed by
        # the moves we make inside the loop.
        flat_files = [p for p in root.iterdir()
                      if p.is_file() and not p.name.startswith(".")]
        for src in flat_files:
            try:
                subdir = _classify_filename_by_db(conn, src.name)
                dest_dir = root / subdir
                dest_dir.mkdir(parents=True, exist_ok=True)
                dest = dest_dir / src.name
                if dest.exists():
                    # Already in subdir from a prior run that crashed
                    # before updating the sentinel. Leave the flat copy
                    # alone (don't clobber); the DB rewrite below will
                    # still align references.
                    summary["skipped"] += 1
                else:
                    shutil.move(str(src), str(dest))
                # Update DB references. Bare name → "subdir/name". COMMIT
                # per-file so a crash mid-loop leaves at most ONE row in
                # an inconsistent state, not the whole batch.
                new_rel = f"{subdir}/{src.name}"
                cur = conn.cursor()
                cur.execute(
                    "UPDATE preset_pieces SET file = ? WHERE file = ?",
                    (new_rel, src.name),
                )
                summary["db_rows_updated"] += cur.rowcount
                cur.execute(
                    "UPDATE presets SET model_file = ? WHERE model_file = ?",
                    (new_rel, src.name),
                )
                summary["db_rows_updated"] += cur.rowcount
                cur.execute(
                    "UPDATE presets SET ir_file = ? WHERE ir_file = ?",
                    (new_rel, src.name),
                )
                summary["db_rows_updated"] += cur.rowcount
                conn.commit()
                summary[f"{kind}_moved"] += 1
            except Exception as e:
                log.exception("migration failed for %s", src)
                summary["errors"].append(f"{src.name}: {e}")

    if not summary["errors"]:
        try:
            sentinel.write_text(
                json.dumps({"migrated_at": time.time(), **summary}, indent=2),
                encoding="utf-8",
            )
            log.info("nam storage migrated to subdir layout: %s", summary)
        except OSError:
            log.warning("could not write %s sentinel", sentinel.name)
    return summary


# ── Tone parsing (mirrors plugins/tones/routes.py logic) ─────────────
#
# We intentionally re-implement here rather than import from the tones
# plugin: plugin-to-plugin imports require path acrobatics that fragile
# across slopsmith versions, and the parsing surface is small enough
# that copying is cheaper than coupling. Gear images are still served
# by the tones plugin (`/api/plugins/tones/gear-image/{type}`).


def _extract_model_from_knobs(knobs: dict) -> str | None:
    if not knobs:
        return None
    first_knob = next(iter(knobs))
    parts = first_knob.split("_")
    if len(parts) >= 3:
        rs_map = _load_rs_to_real()
        for i in range(len(parts) - 1, 0, -1):
            candidate = "_".join(parts[:i])
            if candidate in rs_map:
                return candidate
        return "_".join(parts[:-1])
    if len(parts) == 2:
        return parts[0]
    return None


def _parse_gear(gear: dict, slot_type: str) -> dict:
    gear_type = gear.get("Type", "")
    knobs = gear.get("KnobValues", {}) or {}
    category = gear.get("Category", "")

    # the game stores the gear's canonical identity in `.Key`
    # (e.g. "Amp_MarshallDSL100H", "Pedal_EQ5", "Rack_StudioDelay") for
    # EVERY slot, while `.Type` is always the generic family string
    # ("Amps"/"Pedals"/"Racks"/"Cabinets"). Historically we derived the
    # rs_gear from `_extract_model_from_knobs` (the first knob's prefix),
    # which silently falls back to the generic `.Type` whenever the gear
    # has no KnobValues in that tone or the prefix doesn't resolve — the
    # same failure that dumped every cab into "Cabinets". Prefer `.Key`:
    # it's authoritative, present even for knob-less gear, and agrees with
    # the knob heuristic whenever that heuristic actually worked. Cabinets
    # are handled by the dedicated branch below (their Key carries a mic
    # suffix that must be stripped); for every other slot the Key IS the
    # rs_gear, so use it directly.
    key = gear.get("Key", "") or ""
    if slot_type != "cabinet" and key:
        model_key = key
    else:
        model_key = _extract_model_from_knobs(knobs) or gear_type

    clean_knobs: dict = {}
    for k, v in knobs.items():
        parts = k.split("_")
        clean_name = parts[-1] if len(parts) > 1 else k
        clean_knobs[clean_name] = round(v, 1) if isinstance(v, float) else v

    out = {
        "type": model_key,
        "slot": slot_type,
        "category": category,
        "knobs": clean_knobs,
    }

    # Cabinet mic-position: the game encodes the specific cab + mic
    # in `Cabinet.Key` (e.g. "Bass_Cab_AT810BC_Tube_Cone"), while
    # `Cabinet.Type` is the literal string "Cabinets" for every cab.
    # The .Key value is the Wwise Effect name — the same string our
    # cab-IR mapping uses to resolve mic-position → IR.
    #
    # Surface it as a separate field so:
    #   1. `_enrich_chain_piece` can promote it into rs_gear_type
    #      (Bass_Cab_AT810BC) so the cab tab works against the base
    #      cab in rs_cab_mic_map.
    #   2. The auto-assign / chain build flow can pick the matching
    #      IR file via rs_cab_mic_map.
    if slot_type == "cabinet":
        key = gear.get("Key") or ""
        if key:
            out["cabinet_key"] = key
            # Promote `model_key` from generic "Cabinets" to the cab's
            # base rs_gear (e.g. "Bass_Cab_AT810BC") so downstream lookups
            # against rs_cab_to_ir / rs_cab_mic_map / photos all work.
            # The mic suffix is NOT in `model_key` — it stays purely in
            # `cabinet_key` so the per-piece variant override sees it.
            base = _cab_base_from_effect_name(key)
            if not base:
                # RS1-era tones store the BASE cab key directly (no mic
                # suffix, e.g. "Cab_PA600C") — absent from the mic map, but
                # a direct (case-insensitive) rs_cab_to_ir key. Same
                # resolution as _resolve_cab_for_effect's Tier 2.
                c2i = _load_rs_cab_to_ir() or {}
                base = key if key in c2i else next(
                    (k for k in c2i if k.lower() == key.lower()), None)
            if base:
                out["type"] = base
    return out


# Cache for the inverse `Effect name → mic suffix` lookup so cab parsing
# doesn't re-walk the whole mic map every time we see a Cabinet piece.
_effect_to_mic_cache: dict[str, tuple[str, str]] | None = None


def _build_effect_to_mic_index() -> dict[str, tuple[str, str]]:
    """Walk rs_cab_mic_map once and build `{effect_name: (base_rs_gear,
    suffix)}` keyed by lowercased effect name — case-insensitive so
    the game's mixed-case Cabinet.Key values match cleanly."""
    global _effect_to_mic_cache
    if _effect_to_mic_cache is None:
        idx: dict[str, tuple[str, str]] = {}
        for base, mics in (_load_rs_cab_mic_map() or {}).items():
            for suffix, info in (mics or {}).items():
                ename = (info or {}).get("effect_name") or ""
                if ename:
                    idx[ename.lower()] = (base, suffix)
        _effect_to_mic_cache = idx
    return _effect_to_mic_cache


def _cab_base_from_effect_name(effect_name: str) -> str | None:
    """Resolve a PSARC Cabinet.Key (e.g. "Bass_Cab_AT810BC_Tube_Cone")
    back to its base rs_gear ("Bass_Cab_AT810BC"). Uses the mic-map
    inverse index so the base form matches what rs_cab_to_ir +
    rs_cab_mic_map are keyed by. Returns None when the effect name
    isn't in the index (e.g. cab not in the map, or mic map outdated).
    """
    if not effect_name:
        return None
    base_suffix = _build_effect_to_mic_index().get(effect_name.lower())
    return base_suffix[0] if base_suffix else None


def _cab_suffix_from_effect_name(effect_name: str) -> str | None:
    """Resolve a PSARC Cabinet.Key to the mic-position suffix ('5c',
    'cc', 'tc', …). Returns None when not found."""
    if not effect_name:
        return None
    base_suffix = _build_effect_to_mic_index().get(effect_name.lower())
    return base_suffix[1] if base_suffix else None


def _resolve_cab_for_effect(effect_name: str, irs_root) -> tuple[str, str] | None:
    """Resolve a Cabinet.Key to (base_rs_gear, ir_file_relpath), verifying
    the IR exists under `irs_root`. Two tiers:

      1. **HIRC mic map** — a Cabinet.Key carrying a mic-position suffix
         (e.g. "Cab_Marshall1960a_57_Edge") resolves to (base, suffix) and
         that mic's ir_file.
      2. **Bare-cab fallback** — PA / acoustic / DI cabs (e.g. "Cab_PA600C")
         have no per-mic captures, so they're absent from the HIRC mic map
         entirely. Treat the Cabinet.Key as a bare base cab and use its
         default IR from rs_cab_to_ir. Without this, those cabs never
         promote out of the generic "Cabinets" placeholder.

    Returns None if neither tier resolves or the IR isn't on disk.
    """
    if not effect_name:
        return None
    # Tier 1 — mic map (suffixed effect names)
    base = _cab_base_from_effect_name(effect_name)
    suffix = _cab_suffix_from_effect_name(effect_name)
    if base and suffix:
        spec = (_load_rs_cab_mic_map().get(base) or {}).get(suffix)
        f = (spec or {}).get("ir_file")
        if f and irs_root and (irs_root / f).exists():
            return (base, f)
        # The game's rocksmith/ mic IR isn't extracted on disk, but we ship OUR
        # own modeled cab for this base → use it AT THE SAME mic position the
        # Cabinet.Key names (derived from spec.effect_name, not a default), so
        # the cab plays and the node's bypass works instead of the piece being
        # left empty (kind='none'). `_apply_cab_override` can't help here — it
        # only fires on a rocksmith/ file, which this song has none of.
        if spec:
            ovr = _load_cab_overrides().get(base)
            if isinstance(ovr, dict):
                o = _override_variant(ovr, spec)
                if o and irs_root and (irs_root / o["ir_file"]).exists():
                    return (base, o["ir_file"])
    # Tier 2 — bare base cab via rs_cab_to_ir default IR. RS1-era songs store
    # the BASE key directly in Cabinet.Key (e.g. "Cab_PA600C", no mic suffix),
    # so resolve it case-insensitively against rs_cab_to_ir (the RS gear-id
    # casing drifts between eras — the Title-Case vs UPPER gotcha).
    c2i = _load_rs_cab_to_ir() or {}
    canon = effect_name if effect_name in c2i else next(
        (k for k in c2i if k.lower() == effect_name.lower()), effect_name)
    entry = c2i.get(canon)
    if isinstance(entry, dict):
        irs = entry.get("irs") or []
        if irs and irs[0] and irs_root and (irs_root / irs[0]).exists():
            return (canon, irs[0])
    # Tier 2b — the game IR isn't on disk (post-DMCA nothing extracts it), but
    # we ship a MODELED cab for this base (rb_cab_overrides): use its default
    # mic variant, mirroring Tier 1's override fallback. Without this the RS1
    # bare cabs (Cab_PA600C et al.) never promote out of the generic
    # "Cabinets" placeholder. Guarded on the override existing for THIS gear:
    # _override_ir_for_cab's own generic-default fallback would pair a default
    # cab's IR with the promoted rs_gear.
    if isinstance(_load_cab_overrides().get(canon), dict):
        f = _override_ir_for_cab(canon, irs_root)
        if f:
            return (canon, f)
    return None


def _parse_tone(tone_data: dict) -> dict:
    name = tone_data.get("Name", "Unknown")
    key = tone_data.get("Key", "")
    gear_list = tone_data.get("GearList", {}) or {}

    chain: list[dict] = []
    for slot in ("PrePedal1", "PrePedal2", "PrePedal3", "PrePedal4"):
        g = gear_list.get(slot)
        if g and g.get("Type"):
            chain.append(_parse_gear(g, "pre_pedal"))

    g = gear_list.get("Amp")
    if g and g.get("Type"):
        chain.append(_parse_gear(g, "amp"))

    for slot in ("PostPedal1", "PostPedal2", "PostPedal3", "PostPedal4"):
        g = gear_list.get(slot)
        if g and g.get("Type"):
            chain.append(_parse_gear(g, "post_pedal"))

    for slot in ("Rack1", "Rack2", "Rack3", "Rack4"):
        g = gear_list.get(slot)
        if g and g.get("Type"):
            chain.append(_parse_gear(g, "rack"))

    g = gear_list.get("Cabinet")
    if g and g.get("Type"):
        chain.append(_parse_gear(g, "cabinet"))

    return {"name": name, "key": key, "chain": chain}


# Curated rs_gear -> daw_category overrides for gears the substring heuristic
# misbuckets (codename lacks the type word). Grows category by category.
_DAW_CATEGORY_OVERRIDE = {
    "Pedal_ShredZone": "distortion",   # Metal Zone (MT-2) — 'shredzone' has no 'dist'
    "Bass_Pedal_NYRBS103": "pitch",    # bass synth — pitch-tracked osc + sub-octave
}


def _daw_category_for(rs_gear: str, rs_category: str) -> str:
    """Bucket a game gear into a DAW-style subcategory the user
    recognises ('compression', 'modulation', 'delay', etc.) so the chain
    editor's "Add piece" picker can group it the way any modern DAW
    plugin browser does — instead of forcing the user to think in the
    RS-coarse 'amp/pedal/rack' axis.

    Heuristic over the rs_gear name; falls back to 'other' for stuff
    the substring rules don't recognise. Returns one of:
      amps, cabs, distortion, modulation, delay, reverb, compression,
      eq, wah, pitch, filter, utility, other.
    """
    if rs_category == "amp":
        return "amps"
    if rs_category == "cab":
        return "cabs"
    # Curated overrides for gears whose codename doesn't contain the type word
    # (e.g. 'ShredZone' is a distortion but has no 'dist'/'metalzone' substring).
    ov = _DAW_CATEGORY_OVERRIDE.get(rs_gear)
    if ov:
        return ov
    name = (rs_gear or "").lower()
    # Order matters: more-specific patterns first so e.g. "Pedal_LoFiFilter"
    # doesn't get swallowed by the generic "filter" bucket before the
    # "lofi" pattern hits.
    BUCKETS = [
        # (category, substrings — match if ANY are in the name)
        ("compression", ("compres", "compr", "optcomp", "limit", "limiter",
                         "punch", "swole", "sustain", "studiocomp")),
        ("modulation",  ("chorus", "flanger", "phaser", "tremolo", "amptrem",
                         "vibrato", "ringmod", "rotary", "rotatoe", "univibe",
                         "ensemble")),
        ("delay",       ("delay", "echo", "tapeecho", "slapback")),
        ("reverb",      ("reverb", "spring", "platerev", "hallrev",
                         "digitalverb", "ambient", "spaceverb", "cosmic")),
        ("distortion",  ("dist", "fuzz", "rat", "tubescream", "ts9", "ts808",
                         "overdrive", "screamer", "drive", "boost", "centaur",
                         "muff", "octfuzz", "supacharger", "hotbox",
                         "supercrunch", "redcrunch", "metalzone", "metal_zone")),
        ("eq",          ("eq5", "eq8", "graphiceq", "studioeq",
                         "studioparametriceq", "parametriceq")),
        ("wah",         ("wah", "envelopefilter", "autowah")),
        ("pitch",       ("octave", "octavius", "octaver", "pitchshift",
                         "multipitch", "pitchdly", "harmony", "harmonist",
                         "detune", "subocta")),
        ("filter",      ("filter", "lofifilter", "acousticemulator",
                         "rangebooster", "exciter", "monosynth")),
        ("utility",     ("noisegate", "gate", "tuner", "noise", "znr",
                         "hush", "silencer", "bitcrush", "sfx",
                         "acoustic_emulator", "amplifier_simulator")),
    ]
    for cat, needles in BUCKETS:
        for needle in needles:
            if needle in name:
                return cat
    return "other"


# DAW-style subcategories the chain picker uses. Order matches the order
# of buttons in the UI so the user sees them in a predictable layout.
_DAW_CATEGORIES_ORDER = [
    "amps", "cabs", "distortion", "modulation", "delay", "reverb",
    "compression", "eq", "wah", "pitch", "filter", "utility", "other",
]


# ── Master chain (global pre/post FX wrap around every tone) ──────────
#
# The plugin keeps two sentinel rows in the host's `presets` table that
# never appear in the UI's per-song lookups: `__master_pre__` and
# `__master_post__`. Their preset_pieces rows form a chain that
# native_preset_full prepends + appends to every tone, so e.g. a global
# input compressor + output limiter stay on regardless of which song /
# tone is loaded.
_MASTER_PRESET_NAMES = {
    "pre":  "__rig_builder_master_pre__",
    "post": "__rig_builder_master_post__",
}


def _get_master_preset_id(role: str) -> int | None:
    """Return the sentinel preset id for the master pre/post chain,
    creating it on first call so the rest of the code can assume it
    exists. `role` is 'pre' or 'post'."""
    name = _MASTER_PRESET_NAMES.get(role)
    if not name:
        return None
    conn = _get_conn()
    with _lock:
        row = conn.execute(
            "SELECT id FROM presets WHERE name = ?", (name,)
        ).fetchone()
        if row:
            return int(row[0])
        # Insert with empty model/IR/gain defaults — the sentinel never
        # plays through the bundle's 2-stage path, only through
        # native_preset_full where its pieces are read directly.
        conn.execute(
            "INSERT INTO presets (name, model_file, ir_file, input_gain, output_gain, gate_threshold, settings_json) "
            "VALUES (?, '', '', 1.0, 1.0, -60.0, ?)",
            (name, json.dumps({"master_role": role})),
        )
        conn.commit()
        new_row = conn.execute(
            "SELECT id FROM presets WHERE name = ?", (name,)
        ).fetchone()
        return int(new_row[0]) if new_row else None


def _load_master_chain(role: str) -> list[dict]:
    """Return the master pre or post chain as a list of enriched pieces
    (same shape as _load_saved_chain). Empty list if the sentinel
    preset has no pieces yet."""
    pid = _get_master_preset_id(role)
    if pid is None:
        return []
    conn = _get_conn()
    return _load_saved_chain(conn, pid) or []


# ── Default tone ────────────────────────────────────────────────────
# A single standalone chain the user assembles from gear (amp/cab/pedals).
# It plays when no song is active — at startup, and again whenever the user
# leaves a song or stops a Listen preview — so the menu no longer just keeps
# "whatever tone was loaded last". Stored exactly like the master chain: a
# sentinel preset whose pieces are read by native_preset_full, which wraps it
# with the master pre/post + final leveler like any ordinary song tone (its
# name does NOT start with "__rig_builder_master_", so it isn't excluded).
_DEFAULT_TONE_PRESET_NAME = "__rig_builder_default_tone__"
# User-saved Studio tones are presets named with this prefix + the user's name.
_SAVED_TONE_PREFIX = "__rig_builder_saved_tone__"


def _seed_bundled_default_tone(conn: sqlite3.Connection, pid: int) -> None:
    """Seed the freshly-created default-tone sentinel preset with the pieces
    from the bundled data/default_tone.json. Each piece's `vst_rel` is resolved
    against the plugin dir so the stored vst_path is correct on the current
    OS/install (macOS/Windows/Linux); pieces whose bundled VST is missing are
    skipped. Called once, under `_lock`, from _get_default_tone_preset_id — must
    not re-acquire `_lock`."""
    try:
        doc = json.loads(_data_path("default_tone.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"[rig_builder] default tone seed skipped ({e})")
        return
    ig, og, gt = doc.get("input_gain"), doc.get("output_gain"), doc.get("gate_threshold")
    if ig is not None or og is not None or gt is not None:
        conn.execute(
            "UPDATE presets SET input_gain = COALESCE(?, input_gain), "
            "output_gain = COALESCE(?, output_gain), "
            "gate_threshold = COALESCE(?, gate_threshold) WHERE id = ?",
            (ig, og, gt, pid),
        )
    for piece in doc.get("pieces", []):
        rel = piece.get("vst_rel")
        if not rel:
            continue
        vst_path = _plugin_dir / rel
        if not vst_path.exists():
            print(f"[rig_builder] default tone: bundled VST missing, skipping {rel}")
            continue
        conn.execute(
            "INSERT INTO preset_pieces "
            "(preset_id, slot_order, slot, rs_gear_type, kind, file, "
            " params_json, tone3000_id, assigned_mode, bypassed, "
            " vst_path, vst_format, vst_state) "
            "VALUES (?, ?, ?, ?, 'vst', NULL, ?, NULL, ?, ?, ?, ?, ?)",
            (
                pid,
                piece.get("slot_order", 0),
                piece.get("slot") or "amp",
                piece.get("rs_gear_type") or "",
                piece.get("params_json") or "{}",
                piece.get("assigned_mode") or "manual",
                int(piece.get("bypassed") or 0),
                str(vst_path),
                piece.get("vst_format") or "VST3",
                piece.get("vst_state"),
            ),
        )
    conn.commit()


def _get_default_tone_preset_id() -> int | None:
    """Return the sentinel preset id for the user's default tone, creating
    it on first call so callers can assume it exists."""
    conn = _get_conn()
    with _lock:
        row = conn.execute(
            "SELECT id FROM presets WHERE name = ?", (_DEFAULT_TONE_PRESET_NAME,)
        ).fetchone()
        if row:
            return int(row[0])
        conn.execute(
            "INSERT INTO presets (name, model_file, ir_file, input_gain, output_gain, gate_threshold, settings_json) "
            "VALUES (?, '', '', 1.0, 1.0, -60.0, ?)",
            (_DEFAULT_TONE_PRESET_NAME, json.dumps({"default_tone": True})),
        )
        conn.commit()
        new_row = conn.execute(
            "SELECT id FROM presets WHERE name = ?", (_DEFAULT_TONE_PRESET_NAME,)
        ).fetchone()
        pid = int(new_row[0]) if new_row else None
        # Seed the out-of-the-box default tone from the bundled
        # data/default_tone.json (a curated multi-piece studio rig) so a fresh
        # install has an audible idle rig with zero setup. Only runs once, when
        # the sentinel is first created — existing users keep whatever default
        # tone they already have.
        if pid is not None:
            _seed_bundled_default_tone(conn, pid)
        return pid


def _default_tone_fallback_mapping(conn: sqlite3.Connection):
    """Return a single synthetic mega-chain mapping for the user's default tone,
    or None when there is no usable default tone.

    Used by `/mega_chain/{song}` when a song has no per-song tone mappings —
    which is EVERY feedpak song, since the feedpak format carries no Rocksmith
    gear/tone descriptors to map (see feedpak-spec FEP-0001). Falling back to
    the default tone lets the highway "Rig Tones" button apply a rig to any song
    instead of failing. Gated on `default_tone_enabled` so a user who turned the
    default tone off still gets an honest "no tones" 404.

    The tuple shape matches `_lookup()` rows so the caller can drop it straight
    into the mega-chain builder:
        (tone_key, preset_id, name, input_gain, output_gain, gate_threshold)
    """
    if not _load_settings().get("default_tone_enabled", True):
        return None
    default_pid = _get_default_tone_preset_id()
    if default_pid is None:
        return None
    drow = conn.execute(
        "SELECT name, input_gain, output_gain, gate_threshold "
        "FROM presets WHERE id = ?", (default_pid,),
    ).fetchone()
    has_pieces = conn.execute(
        "SELECT 1 FROM preset_pieces WHERE preset_id = ? LIMIT 1",
        (default_pid,),
    ).fetchone()
    if not (drow and has_pieces):
        return None
    return [(
        "__default__", default_pid, drow[0],
        drow[1] if drow[1] is not None else 1.0,
        drow[2] if drow[2] is not None else 1.0,
        drow[3],
    )]


def _load_default_tone_chain() -> list[dict]:
    """Return the default tone's chain as enriched pieces (same shape as
    _load_master_chain). Empty list if nothing configured yet."""
    pid = _get_default_tone_preset_id()
    if pid is None:
        return []
    conn = _get_conn()
    return _load_saved_chain(conn, pid) or []


def _build_master_stages(role: str, models_dir, irs_dir,
                         output_gain: float, missing: list) -> list[dict]:
    """Return native-engine stages (type:0/1/2) for the master pre or post
    chain. Iterates preset_pieces rows for the sentinel preset and emits
    one stage per piece that has an actual file/vst_path. Pieces lacking
    an assignment (kind='none' or empty file) are silently skipped — the
    user added them but hasn't picked a plugin yet."""
    pid = _get_master_preset_id(role)
    if pid is None:
        return []
    conn = _get_conn()
    rows = conn.execute(
        "SELECT slot, kind, file, rs_gear_type, bypassed, slot_order, "
        "       vst_path, vst_format, vst_state "
        "FROM preset_pieces WHERE preset_id = ? "
        "ORDER BY slot_order",
        (pid,),
    ).fetchall()
    stages: list[dict] = []
    for slot, kind, file, gear, bypassed, _slot_order, vst_path, vst_format, vst_state in rows:
        bypassed = bool(bypassed)
        slot_tag = f"master_{role}:{slot or 'unspecified'}"
        if kind == "nam" and file:
            path = _safe_child(models_dir, file)
            if not path or not path.exists():
                missing.append(file)
                continue
            # inputLevel 1.0: master pre/post NAMs aren't amps to over-drive.
            stages.append(_nam_stage(path, bypassed=bypassed, input_level=1.0,
                                     slot=slot_tag, rs_gear=gear))
        elif kind in ("ir", "rs_ir") and file:
            ir_path = _safe_child(irs_dir, file)
            if not ir_path or not ir_path.exists():
                missing.append(file)
                continue
            stages.append(_ir_stage(ir_path, bypassed=bypassed,
                                    slot=slot_tag, rs_gear=gear,
                                    gain=_ir_stage_gain(kind, ir_path)))
        elif kind == "vst" and vst_path:
            vp = Path(vst_path)
            stages.append(_vst_stage(
                vp, vst_format or "VST3", bypassed=bypassed,
                # Opaque state blob (verbatim) so a master VST — e.g. a comp
                # in master_post — comes up with its captured settings.
                state=_vst_stage_state(str(vp), vst_format, vst_state),
                slot=slot_tag, rs_gear=gear))
        # else: kind == 'none' or no file → skip (user placeholder)
    return stages


def _load_saved_chain(conn: sqlite3.Connection, preset_id: int,
                      img_idx: dict | None = None) -> list[dict] | None:
    """Return the user-edited chain for a preset, rebuilt from preset_pieces
    rows + enriched the same way as a PSARC-derived chain. Returns None if
    the preset has no rows (yet), in which case the caller falls back to
    the PSARC's GearList.

    `img_idx` is the same pre-built tone-image lookup `_enrich_chain_piece`
    takes; pass it from get_song to avoid hitting the search cache once
    per piece. None = each enrich call builds its own (slow path, ok for
    one-off uses).

    Each row's fields surface as both the "piece" shape the UI expects AND
    pre-populated `assigned` block so the UI immediately renders the saved
    file / VST / bypass without round-tripping through the gear-global
    assignment lookup.
    """
    rows = conn.execute(
        "SELECT id, slot_order, slot, rs_gear_type, kind, file, params_json, "
        "       tone3000_id, assigned_mode, bypassed, vst_path, vst_format, vst_state "
        "FROM preset_pieces WHERE preset_id = ? ORDER BY slot_order",
        (preset_id,),
    ).fetchall()
    if not rows:
        return None
    out: list[dict] = []
    for r in rows:
        (piece_id, slot_order, slot, rs_gear, kind, file, params_json,
         t3kid, assigned_mode, bypassed, vst_path, vst_format, vst_state) = r
        try:
            knobs = json.loads(params_json) if params_json else {}
        except json.JSONDecodeError:
            knobs = {}
        # Build the same "raw piece" shape _enrich_chain_piece expects.
        piece = {
            "type": rs_gear,
            "slot": slot,
            "category": _gear_category(rs_gear) or "",
            "knobs": knobs,
        }
        enriched = _enrich_chain_piece(piece, img_idx)
        # Override the gear-global `assigned` block with THIS preset's row
        # so the UI shows the correct file/VST for this specific tone
        # (otherwise a different tone using the same rs_gear could bleed in).
        enriched["assigned"] = {
            "preset_piece_id": piece_id,
            "preset_id": preset_id,
            "kind": kind,
            # Show OUR cab IR when we ship one for this RS cab (rb_cab_overrides)
            # so the song editor highlights the matching mic button and the label
            # reads our file — mirrors the auto-substitution done at playback.
            "file": _apply_cab_override(file),
            "tone3000_id": t3kid,
            "assigned_mode": assigned_mode,
            "vst_path": vst_path,
            "vst_format": vst_format,
            "vst_state": _effective_vst_state_for_piece(rs_gear, vst_path, vst_state, knobs)
            if kind == "vst" and vst_path else vst_state,
        }
        enriched["bypassed"] = bool(bypassed)
        enriched["_preset_piece_id"] = piece_id   # so the UI can reorder/remove by id
        enriched["_slot_order"] = slot_order
        out.append(enriched)
    return out


def _enrich_chain_piece(piece: dict, img_idx: dict | None = None) -> dict:
    """Add make/model + tone3000 hints + existing-assignment info to a
    parsed chain piece. Returns a new dict, doesn't mutate input.

    `img_idx` is an optional pre-built `_tone_image_index()` (tone_id ->
    {title,...}); pass it when enriching a whole chain so we read the
    search cache once instead of per piece."""
    rs_type = piece["type"]
    rs_map = _load_rs_to_real()
    info = rs_map.get(rs_type) or {}

    category = info.get("category") or _guess_category_from_slot(piece["slot"])
    platform = _PLATFORM_FOR_CATEGORY.get(category, "nam")

    query = info.get("tone3000_query") or rs_type
    gears = info.get("tone3000_gears") or ""

    from rb_core.tone3000_client import Tone3000Client  # local import (same dir)
    deep_link = Tone3000Client.build_search_url(query, gears=gears or None, platform=platform)

    # Lookup any preset_piece already saved for this rs_gear. Prefer a
    # row that actually has a file over a pending one (kind='none' /
    # empty file), then the most recent — otherwise a stale pending row
    # for the same gear (e.g. left by a batch on another song) would
    # shadow a real assignment and the chain would wrongly show
    # "(no file)" even though the capture is downloaded.
    assigned = None
    conn = _get_conn()
    # A piece counts as "really assigned" when it has EITHER a NAM/IR file
    # OR a vst_path. We want those rows surfaced over pending kind='none'
    # rows for the same gear (e.g. left by a batch on another song that
    # only mapped the gear without picking a file/VST).
    row = conn.execute(
        "SELECT id, preset_id, kind, file, tone3000_id, assigned_mode, "
        "       vst_path, vst_format, vst_state "
        "FROM preset_pieces WHERE rs_gear_type = ? "
        "ORDER BY (CASE "
        "  WHEN (file IS NOT NULL AND file != '') "
        "    OR (vst_path IS NOT NULL AND vst_path != '') "
        "  THEN 0 ELSE 1 END), id DESC "
        "LIMIT 1",
        (rs_type,),
    ).fetchone()
    if row:
        assigned = {
            "preset_piece_id": row[0],
            "preset_id": row[1],
            "kind": row[2],
            "file": row[3],
            "tone3000_id": row[4],
            "assigned_mode": row[5],
            "vst_path": row[6],
            "vst_format": row[7],
            "vst_state": row[8],
        }
        # Human tone3000 title (from the local search cache) so the UI can
        # show e.g. "Two Rock Studio 22" instead of the technical
        # tone3000_<id>_m<model>_<rs_gear> filename. None when the tone
        # isn't cached (e.g. assigned via default_captures by id).
        tid = row[4]
        if tid is not None:
            idx = img_idx if img_idx is not None else _tone_image_index()
            assigned["tone3000_title"] = (idx.get(int(tid)) or {}).get("title")
        else:
            assigned["tone3000_title"] = None

    # the game-extracted IRs: only meaningful for cab slots. The map
    # is keyed by the same entity name we have in rs_to_real, so it's
    # a direct lookup. We filter by on-disk existence so a fresh
    # install that ships `rs_cab_to_ir.json` without the .wav files
    # (the game cab IRs don't ship) doesn't surface broken references —
    # the UI just falls back to the tone3000 deep-link for that cab.
    rs_irs: list[str] = []
    cab_mic_variants: list[dict] = []
    if category == "cab":
        rs_entry = _load_rs_cab_to_ir().get(rs_type) or {}
        candidates = list(rs_entry.get("irs") or [])
        irs_root = _config_dir / "nam_irs" if _config_dir else None
        if irs_root:
            rs_irs = [f for f in candidates if (irs_root / f).exists()]
        # Per-mic-position list — same shape as the catalog enrichment so
        # the song editor can render labeled audition buttons ("Dynamic
        # Cone", "Condenser Edge", "Tube Off-axis", …) instead of a raw
        # filename dropdown. Sorted by ir_index for stable layout.
        mic_map = _load_rs_cab_mic_map().get(rs_type) or {}
        ovr = _load_cab_overrides().get(rs_type) or {}
        if mic_map and irs_root is not None:
            for suffix, entry in sorted(
                    mic_map.items(),
                    key=lambda kv: kv[1].get("ir_index", 99)):
                f = entry.get("ir_file")
                label = entry.get("label") or suffix
                position = entry.get("position")
                # OUR shipped cab IRs override the RS mic-position table when
                # rb_cab_overrides.json has an entry for this cab. Mark them
                # always-available (we ship them; don't gate on an on-disk check
                # that flaked when the generated mic-map was edited in place).
                our = False
                if ovr:
                    o = _override_variant(ovr, entry)
                    if o:
                        f, label, position = o["ir_file"], o["label"], o["position"]
                        our = True
                available = True if our else (bool(f) and (irs_root / f).exists())
                cab_mic_variants.append({
                    "suffix": suffix,
                    "ir_index": entry.get("ir_index"),
                    "ir_file": f,
                    "label": label,
                    "mic_type": entry.get("mic_type"),
                    "position": position,
                    "available": available,
                    "our_synth": our,
                })

    # Amp gain variant info — only relevant when the curator has
    # actually shipped `gain_variants` for this amp in rs_to_real.json.
    # We expose three fields:
    #   `available`: ordered list of level keys (e.g. ["clean", "crunch", "dist"])
    #   `picked`:    the level the system would auto-pick for this tone's Gain knob
    #   `picked_id`: the tone3000_id that picked level maps to
    # The per-song UI shows a small badge ("variant: clean") on amps
    # that have this defined. None when no variants exist for this gear.
    amp_variant_info = None
    if category == "amp":
        variants = info.get("gain_variants") or {}
        if variants:
            picked = _pick_amp_gain_variant(info, _gear_rs_gain(piece, info))
            # Map the currently-assigned NAM back to a level name
            # ("clean"/"crunch"/"dist") so the UI can highlight the
            # button the user is actually hearing.
            #
            # NOTE: tone3000_id can be IDENTICAL across all variants of
            # an amp (when the curator picked multiple captures from the
            # same tone3000 page — model_id differs, tone3000_id doesn't).
            # If we matched by tone3000_id alone we'd always return the
            # first variant in the dict ("clean"), which is the bug the
            # UI was hitting. So match by the resolved file BASENAME —
            # each variant has unique `notes` → unique filename.
            assigned_file = (assigned or {}).get("file") or ""
            assigned_basename = (
                assigned_file.rsplit("/", 1)[-1] if assigned_file else ""
            )
            current_level = None
            if assigned_basename:
                for lvl, spec in variants.items():
                    if not isinstance(spec, dict):
                        continue
                    notes = (spec.get("notes") or "").strip()
                    if notes:
                        expected = f"{_safe_filename_human(notes)}.nam"
                        if expected == assigned_basename:
                            current_level = lvl
                            break
                    # Legacy filename fallback: tone3000_<tid>_m<mid>_<gear>.nam.
                    tid = spec.get("tone3000_id")
                    mid = spec.get("model_id")
                    if tid and mid:
                        legacy = (
                            f"tone3000_{tid}_m{mid}_"
                            f"{_safe_filename(rs_type)}.nam"
                        )
                        if legacy == assigned_basename:
                            current_level = lvl
                            break
            amp_variant_info = {
                "available": list(variants.keys()),
                "picked": (picked or {}).get("_picked_level"),
                "picked_id": (picked or {}).get("tone3000_id"),
                "current_level": current_level,
                "rs_gain": _gear_rs_gain(piece, info),
            }

    return {
        **piece,
        "real_name": _gear_display_name(rs_type, info.get("name", rs_type)),
        "make": info.get("make", ""),
        "model": info.get("model", ""),
        "rs_category": category,
        "tone3000_query": query,
        "tone3000_gears": gears,
        "tone3000_platform": platform,
        "tone3000_search_url": deep_link,
        "rs_irs": rs_irs,
        # Labeled mic-position picker for cab pieces (Dynamic Cone,
        # Condenser Edge, …). Empty when the cab has no entry in
        # rs_cab_mic_map.json.
        "cab_mic_variants": cab_mic_variants,
        "amp_variant": amp_variant_info,
        "assigned": assigned,
    }


def _guess_category_from_slot(slot: str) -> str:
    if slot == "amp":
        return "amp"
    if slot == "cabinet":
        return "cab"
    if slot == "rack":
        return "rack"
    return "pedal"


def _gear_category(rs_gear: str) -> str:
    """Category for a gear: from rs_to_real if known, else guessed from the
    entity name. Catch-all entities ('Cabinets', 'Pedals', 'DI_Amp_*') are
    NOT in rs_to_real, so without this they default to 'amp' — which made
    the cab's Suggest search look for amp NAMs (platform=nam) and download
    cabs as NAMs instead of cab IRs. Used by /search and download_for_gear,
    which only have the rs_gear (not the slot)."""
    info = _load_rs_to_real().get(rs_gear) or {}
    if info.get("category"):
        return info["category"]
    name = rs_gear.lower()
    if "cab" in name:
        return "cab"
    if "pedal" in name:
        return "pedal"
    if "rack" in name:
        return "rack"
    return "amp"


# ── PSARC / sloppak readers ──────────────────────────────────────────


_SKIP_ARRANGEMENTS = ("Vocals", "ShowLights", "JVocals")


def _collect_tone_defs(arr_json: dict, tones: list[dict], seen: set[str]) -> None:
    """Accumulate an arrangement's tone definitions, de-duped by Key."""
    arr_tones = arr_json.get("tones")
    if not isinstance(arr_tones, dict):
        return
    for t in arr_tones.get("definitions") or []:
        if not isinstance(t, dict):
            continue
        key = t.get("Key", "")
        if isinstance(key, str) and key:
            if key in seen:
                continue
            seen.add(key)
        tones.append(t)


def _read_tones_from_sloppak(filename: str, dlc: Path) -> list[dict]:
    """Tone definitions for a song, WITHOUT unpacking it.

    This used to call sloppak.load_song(), which calls resolve_source_dir() and
    writes the entire pack — every stem — into sloppak_cache/. We only ever want
    a few KB of tone JSON out of the arrangement files, so that was a ~45x write
    amplification per song. Harmless once; catastrophic from _batch_worker(),
    which runs this over the WHOLE library: a tester's cache reached 60 GB — his
    entire 1800-song library, unpacked, none of it ever played (feedBack#TODO).

    Read the manifest and the arrangement members straight out of the zip
    instead. Both are single-member reads; nothing is written to disk.
    """
    try:
        import sloppak as sloppak_mod
    except ImportError:
        return []

    pack = dlc / filename.rstrip("/\\")
    tones: list[dict] = []
    seen: set[str] = set()

    # read_member_bytes lands in core alongside this change. On an older core,
    # fall back to the unpacking path rather than silently reporting no tones —
    # a bloated cache beats a rig builder that thinks every song is tone-less.
    if not hasattr(sloppak_mod, "read_member_bytes"):
        return _read_tones_via_unpack(filename, dlc)

    try:
        manifest = sloppak_mod.load_manifest(pack)       # zip-form: manifest only
    except Exception:
        log.warning("failed to read manifest of %r", filename, exc_info=True)
        return []

    for entry in (manifest or {}).get("arrangements", []) or []:
        if not isinstance(entry, dict):
            continue
        if str(entry.get("name") or "") in _SKIP_ARRANGEMENTS:
            continue
        rel = entry.get("file")
        if not isinstance(rel, str) or not rel.strip():
            continue
        try:
            raw = sloppak_mod.read_member_bytes(pack, rel)   # single member, no unpack
        except Exception:
            # One unreadable member (CRC error, truncated pack) must not abort the
            # song's whole tone read — and _batch_worker runs this across the
            # entire library, so one bad pack must not poison the batch.
            log.warning("failed to read arrangement %r in %r", rel, filename, exc_info=True)
            continue
        if not raw:
            continue
        try:
            arr_json = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            log.warning("unparseable arrangement %r in %r", rel, filename)
            continue
        if not isinstance(arr_json, dict):
            continue
        # load_song() took the manifest entry's name when it had one and the
        # arrangement JSON's otherwise; mirror both so a Vocals track with no
        # manifest name doesn't start contributing tones it never did before.
        if str(arr_json.get("name") or "") in _SKIP_ARRANGEMENTS:
            continue
        _collect_tone_defs(arr_json, tones, seen)
    return tones


def _read_tones_via_unpack(filename: str, dlc: Path) -> list[dict]:
    """Downlevel-core fallback for _read_tones_from_sloppak. Unpacks the pack."""
    import sloppak as sloppak_mod
    cache_dir = _get_sloppak_cache_dir() if _get_sloppak_cache_dir else None
    if not cache_dir and _config_dir:
        cache_dir = _config_dir / "sloppak_cache"
    try:
        loaded = sloppak_mod.load_song(filename.rstrip("/\\"), dlc, cache_dir)
    except Exception:
        log.warning("failed to read sloppak %r", filename, exc_info=True)
        return []
    tones: list[dict] = []
    seen: set[str] = set()
    for arr in loaded.song.arrangements:
        if arr.name in _SKIP_ARRANGEMENTS:
            continue
        _collect_tone_defs({"tones": getattr(arr, "tones", None) or {}}, tones, seen)
    return tones


def _materialized_cache_dir(name: str) -> Path | None:
    """Return the unpacked `sloppak_cache` directory for this song, if the
    cloud loader has already materialized it.

    Cloud-placeholder libraries keep a 0-byte stub in the DLC dir while the
    real arrangement/tone data lives here, so the seed paths must look here
    before treating the song as unavailable. Mirrors how the cache is keyed
    in `_read_tones_from_sloppak` (basename, with the `sloppak__` prefix
    variant the loader also writes)."""
    cache_dir = _get_sloppak_cache_dir() if _get_sloppak_cache_dir else None
    if not cache_dir and _config_dir:
        cache_dir = _config_dir / "sloppak_cache"
    if not cache_dir:
        return None
    base = name.rsplit("/", 1)[-1].rsplit("\\", 1)[-1]
    for cand in (cache_dir / base, cache_dir / f"sloppak__{base}"):
        try:
            if cand.is_dir() and (cand / "arrangements").exists():
                return cand
        except OSError:
            continue
    return None


def _song_data_available(path: Path) -> bool:
    """True when the song's tone data can actually be read — either the DLC
    file itself is materialized (>0 bytes) or the cloud loader has unpacked
    it into `sloppak_cache`.

    Replaces a bare `st_size == 0` guard in the seed paths, which wrongly
    rejected cloud-placeholder libraries whose real data lives in the cache
    (the tone reader already reads from there — see
    `_read_tones_from_sloppak`)."""
    try:
        if path.stat().st_size > 0:
            return True
    except OSError:
        pass
    return _materialized_cache_dir(path.name) is not None


def _resolve_song_file(filename: str) -> Path | None:
    """Return the PSARC/sloppak path inside DLC_DIR, or None if invalid.

    Supports both:
      - Plain basenames ("Reptilia_v1.psarc") — recursively searches the
        DLC dir for a file with that name. Required for libraries that
        organise songs in subfolders (by artist, album, etc.).
      - Relative paths under the DLC dir ("TheStrokes/Reptilia_v1.psarc")
        — used directly when present.

    Either way the result is path-confined to the DLC tree so an
    attacker can't escape via "../" in the filename param.
    """
    dlc = _get_dlc_dir() if _get_dlc_dir else None
    if not dlc:
        return None
    # First try as a direct path under the DLC dir (root file or
    # relative subdir hit). This is the cheap path — no walk required.
    candidate = (dlc / filename).resolve()
    try:
        candidate.relative_to(dlc.resolve())
    except (OSError, ValueError):
        return None
    if candidate.exists():
        return candidate
    # Fallback: recursive search for a basename match. Lets users keep
    # their library organised in subfolders while the plugin keeps
    # surfacing songs by basename only (which is what web_library.db
    # and the meta lookups expect).
    if "/" not in filename and "\\" not in filename:
        try:
            for p in dlc.rglob(filename):
                if p.is_file() and _is_song_pack(p):
                    return p
        except OSError:
            return None
    return None


def _dlc_relative_song_key(path: Path) -> str | None:
    dlc = _get_dlc_dir() if _get_dlc_dir else None
    if not dlc:
        return None
    try:
        return path.resolve().relative_to(dlc.resolve()).as_posix()
    except (OSError, ValueError):
        return None


def _normalise_song_filename(filename: str) -> str:
    return str(filename or "").replace("\\", "/").strip("/")


def _db_song_key(filename: str, path: Path | None = None) -> str:
    if path is not None:
        rel = _dlc_relative_song_key(path)
        if rel:
            return rel
    resolved = _resolve_song_file(filename)
    if resolved is not None:
        rel = _dlc_relative_song_key(resolved)
        if rel:
            return rel
    return _normalise_song_filename(filename)


def _song_key_candidates(filename: str, path: Path | None = None) -> list[str]:
    """Database filename keys to try, newest first plus legacy basenames."""
    vals: list[str] = []

    def add(v: str | None) -> None:
        v = _normalise_song_filename(v or "")
        if v and v not in vals:
            vals.append(v)

    key = _db_song_key(filename, path)
    add(key)
    add(filename)
    add(key.rsplit("/", 1)[-1])
    add(_normalise_song_filename(filename).rsplit("/", 1)[-1])
    return vals


def _tone_mapping_filename_filter(filename: str, path: Path | None = None) -> tuple[str, tuple[str, ...]]:
    candidates = _song_key_candidates(filename, path)
    placeholders = ", ".join("?" for _ in candidates)
    return f"filename IN ({placeholders})", tuple(candidates)


# ── v3 auto-download: tone3000 model → nam_models/ or nam_irs/ ───────


def _safe_filename_human(text: str) -> str:
    """Sanitise a tone3000 capture title for use as a filename.

    Unlike `_safe_filename`, this preserves spaces, dashes, parens and
    most printable punctuation — so a tone titled "JCM800 2203 D.I. -
    G5 B5 M5 T5 P5 V5 - STD" comes out readable in Finder rather than
    collapsed to underscores. We only strip the characters every
    filesystem (HFS+, APFS, ext4, NTFS) actually rejects: the path
    separators and the Windows-reserved set.

    Empty input → "unnamed.nam" elsewhere; we return "unnamed" here
    and let the caller append the extension.
    """
    import re as _re
    illegal = set('\x00\\/:*?"<>|')
    out = ''.join((c if c not in illegal else '_') for c in (text or ""))
    out = _re.sub(r"\s+", " ", out).strip()
    # Trim leading/trailing dots — Windows treats them specially.
    out = out.strip(".")
    # Cap at 180 chars so the whole path stays under typical 255-byte
    # filename limits even after we append ".nam" + subdir prefix.
    if len(out) > 180:
        out = out[:180].rstrip()
    return out or "unnamed"


def _safe_filename(text: str) -> str:
    """Make a string safe to use as a filename leaf.

    Used to derive stable filenames from tone3000 IDs/titles. We keep
    ascii letters/digits and a few separators; everything else
    collapses to underscore so the result is portable across the
    filesystems the plugin runs on (HFS+, APFS, ext4, NTFS).
    """
    out: list[str] = []
    for ch in text:
        if ch.isalnum() or ch in "-_.":
            out.append(ch)
        elif ch in " /\\":
            out.append("_")
    cleaned = "".join(out).strip("._")
    return cleaned or "file"


def _ffmpeg_normalize_ir(src: Path, dest: Path) -> bool:
    """Run a downloaded IR through ffmpeg to 48 kHz mono float32 WAV.

    Mirrors what nam_tone's /irs upload endpoint does to user-supplied
    IRs — anything looser (different sample rate, stereo, int16, opus
    inside a .wav-named container) confuses the browser's
    decodeAudioData and the IR fails silently at playback.

    Returns True on success. On failure (ffmpeg missing, decode error,
    timeout) the caller falls back to a raw byte copy.

    Writes to a sibling temp file and os.replace()s it into `dest` on
    success, so a killed/failed ffmpeg run can never leave a partial
    file at the final path (which would poison the exists() dedupe on
    the next download).
    """
    tmp = dest.with_suffix(".tmp.wav")
    try:
        result = subprocess.run(
            [
                "ffmpeg", "-y", "-i", str(src),
                "-ar", "48000", "-ac", "1",
                "-c:a", "pcm_f32le",
                str(tmp),
            ],
            capture_output=True,
            timeout=60,
        )
        if result.returncode == 0:
            os.replace(tmp, dest)
            return True
        return False
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False
    finally:
        try:
            tmp.unlink(missing_ok=True)
        except OSError:
            pass


# Tracks how many bytes the current batch has pulled. Reset at the
# start of each batch run.
_batch_disk_bytes = 0


def _disk_budget_reached(settings: dict) -> bool:
    cap_mb = int(settings.get("disk_budget_mb", 2000) or 0)
    if cap_mb <= 0:
        return False
    return _batch_disk_bytes >= cap_mb * 1024 * 1024


# Records WHY the most recent `_download_candidate()` call on THIS thread
# returned None. `_download_candidate` swallows failures to None so the
# batch/preload loops keep going, but that erased the reason — a deleted
# capture (permanent 404) looked identical to a transient network blip.
# The preload worker reads this after a falsy result to label each failure
# ("removed from tone3000 (404)" vs a temporary error) so the user can
# tell which re-running will fix and which it won't. Thread-local because
# the preload runs 3 workers in parallel.
_t3k_dl_error = threading.local()

# Captures whose model download 404'd (deleted/renamed on tone3000). Keyed
# by (tone3000_id, model_id). Process-scoped negative cache: once a capture
# is confirmed gone we skip the network on subsequent preload runs and
# report it immediately, so re-running no longer silently re-attempts the
# same dead captures. Cleared on backend restart (cheap re-probe if the
# capture ever comes back).
_t3k_gone: set = set()
_t3k_gone_lock = threading.Lock()


def _classify_dl_error(exc: BaseException) -> str:
    """Human-readable, one-line reason for a failed capture download."""
    import urllib.error
    if isinstance(exc, urllib.error.HTTPError):
        if exc.code == 404:
            return "removed from tone3000 (404)"
        if exc.code in (401, 403):
            return f"not authorized ({exc.code})"
        if exc.code == 429:
            return "rate-limited (429)"
        return f"HTTP {exc.code}"
    if isinstance(exc, urllib.error.URLError):
        return "network error"
    return type(exc).__name__


def _set_dl_error(reason: str | None) -> None:
    _t3k_dl_error.reason = reason


def _dl_error_is_permanent(reason: str | None) -> bool:
    """True for reasons that re-running won't fix (capture is gone)."""
    return bool(reason) and "404" in reason


def _download_candidate(
    *,
    tone3000_id: int,
    is_ir: bool,
    rs_gear: str,
    settings: dict,
    model_id_override: int | None = None,
) -> tuple[str, str] | None:
    """Download a model for a tone3000 tone id and stage it in the
    right nam_tone directory.

    `model_id_override`: pin a SPECIFIC capture inside the tone (a
    tone3000 page can host multiple NAMs — different sizes, different
    mic positions, sometimes alternate gain settings). When provided,
    we look up that exact model in the list_models response and
    download it; when missing or not found, we fall back to
    pick_best_model (current behaviour: best match for preferred_size).

    Returns `(kind, relative_path)` to put into a preset_piece, or
    None if no model was available, the download failed, or the disk
    budget was exhausted. `kind` is `'nam'` for amp/pedal/rack and
    `'ir'` for cabinet, matching the values nam_tone's runtime
    understands.

    The function is idempotent across batch runs: filenames embed the
    tone3000 tone+model id, so once a file is on disk subsequent
    batches skip the network entirely (the client's
    download_model_file is itself idempotent on dest existence).
    """
    global _batch_disk_bytes
    _set_dl_error(None)
    if _disk_budget_reached(settings):
        _set_dl_error("disk budget reached")
        return None

    client = _get_t3k_client()
    if not client.has_api_access:
        _set_dl_error("tone3000 not connected")
        return None

    try:
        models_payload = client.list_models(tone3000_id)
    except Exception as e:
        # One concise line, not a full traceback per failure — a dead/forbidden
        # tone3000 connection would otherwise spam the console for every gear.
        log.warning("list_models failed for tone_id=%s: %s", tone3000_id, e)
        _set_dl_error(_classify_dl_error(e))
        return None
    if not models_payload:
        _set_dl_error("no models for this tone")
        return None

    from rb_core.tone3000_client import pick_best_model
    model = None
    if model_id_override is not None:
        # Curator pinned a specific capture inside this tone. Find it
        # by id in the `data` array; fall back to pick_best_model with
        # a warning if it's missing (e.g. the capture was deleted from
        # tone3000 after curation).
        for m in (models_payload.get("data") or []):
            if m.get("id") == model_id_override:
                model = m
                break
        if model is None:
            log.warning(
                "model_id_override=%s not found in tone_id=%s — falling "
                "back to pick_best_model (capture may have been removed "
                "from tone3000)", model_id_override, tone3000_id)
    if model is None:
        model = pick_best_model(models_payload, preferred_size=settings.get("preferred_size", "standard"))
    if not model:
        _set_dl_error("no matching capture")
        return None

    model_url = model.get("model_url") or ""
    if not model_url:
        _set_dl_error("capture has no download URL")
        return None
    model_id = model.get("id")

    # Derive a human-readable filename from the capture's tone3000
    # title (e.g. "JCM800 2203 D.I. - G5 B5 M5 T5 P5 V5 - STD"). This
    # is the same text the curation script and the Variants dropdown
    # display, so the file in Finder matches what the user sees in
    # the UI. Falls back to the legacy cryptic naming
    # `tone3000_<tid>_m<mid>_<gear>` when the API didn't surface a
    # title — keeps the file uniquely identifiable in either path.
    _title = ""
    for k in ("title", "name", "display_name", "description"):
        v = model.get(k)
        if isinstance(v, str) and v.strip():
            _title = v.strip()
            break
    if _title:
        bare_stem = _safe_filename_human(_title)
    else:
        bare_stem = (f"tone3000_{tone3000_id}_m{model_id}_"
                     f"{_safe_filename(rs_gear)}")

    if is_ir:
        # tone3000 IRs come in various formats; nam_tone wants 48k
        # mono float32 WAV. We stage to a temp path, normalize, and
        # only count bytes against the budget once the final file
        # exists.
        # New layout: drop cab IRs into nam_irs/<category>/. For cabs
        # that's `cabs/`; for the rare IR-platform tones that happen to
        # be assigned to a non-cab gear (audition flow) the gear's own
        # category is used. The DB-stored path is the RELATIVE form
        # ("cabs/foo.wav") so the engine resolves it via the same
        # _safe_child(root, name) hop as the legacy flat layout.
        subdir = _category_subdir_for_gear(rs_gear)
        irs_dir = _config_dir / "nam_irs" / subdir
        irs_dir.mkdir(parents=True, exist_ok=True)
        bare_name = f"{bare_stem}.wav"
        final_name = f"{subdir}/{bare_name}"   # DB-relative
        final_path = _config_dir / "nam_irs" / final_name
        # Collision guard: if two different captures have the same
        # title, suffix with the model_id so they don't clobber. Only
        # triggers if the existing file's content differs (we can't
        # tell, so just disambiguate to be safe when the existing
        # entry isn't ours to reuse). A same-content collision is
        # benign — the engine doesn't care who put the file there.
        if final_path.exists():
            return ("ir", final_name)
        tmp_path = irs_dir / (bare_name + ".raw")
        try:
            client.download_model_file(model_url, str(tmp_path))
        except Exception as e:
            log.warning("IR download failed for %s", rs_gear, exc_info=True)
            _set_dl_error(_classify_dl_error(e))
            try:
                tmp_path.unlink(missing_ok=True)
            except Exception:
                pass
            return None
        ok = _ffmpeg_normalize_ir(tmp_path, final_path)
        if not ok:
            # ffmpeg unavailable/failed — fall back to a raw copy (matches
            # nam_tone's IR-upload behaviour). tone3000 cab IRs are usually
            # already 48k mono float32 WAV, so this plays fine, and it's far
            # better than dropping the assignment (which left the cab
            # unchanged on re-download).
            log.warning("ffmpeg normalization failed for %s — using raw copy", rs_gear)
            copy_tmp = final_path.with_suffix(".tmp.wav")
            try:
                shutil.copyfile(tmp_path, copy_tmp)
                os.replace(copy_tmp, final_path)
            except Exception:
                log.warning("raw IR copy also failed for %s — IR not assigned", rs_gear)
                for p in (tmp_path, copy_tmp):
                    try:
                        p.unlink(missing_ok=True)
                    except Exception:
                        pass
                return None
        try:
            tmp_path.unlink(missing_ok=True)
        except Exception:
            pass
        _batch_disk_bytes += final_path.stat().st_size
        return ("ir", final_name)

    # NAM model — into nam_models/<category>/.
    subdir = _category_subdir_for_gear(rs_gear)
    models_dir = _config_dir / "nam_models" / subdir
    models_dir.mkdir(parents=True, exist_ok=True)
    bare_name = f"{bare_stem}.nam"
    final_name = f"{subdir}/{bare_name}"   # DB-relative
    final_path = _config_dir / "nam_models" / final_name
    if final_path.exists():
        return ("nam", final_name)
    try:
        written = client.download_model_file(model_url, str(final_path))
    except Exception as e:
        log.warning("NAM download failed for %s", rs_gear, exc_info=True)
        _set_dl_error(_classify_dl_error(e))
        try:
            final_path.unlink(missing_ok=True)
        except Exception:
            pass
        return None
    _batch_disk_bytes += written
    return ("nam", final_name)


# ── Persistence: presets + preset_pieces + tone_mappings ─────────────


def _gate_kwargs_from(data: dict) -> dict:
    """Extract a per-tone noise gate from a save payload's `gate` object into
    _persist_preset_chain kwargs. Shape: `{"gate": {enabled, threshold,
    release, depth}}`. Keys that are absent/None are omitted so the persist
    PRESERVES whatever the preset already had (the UPSERT COALESCEs NULLs to
    the existing row) — a plain chain edit never wipes a saved gate."""
    g = data.get("gate")
    if not isinstance(g, dict):
        return {}
    out: dict = {}
    if g.get("threshold") is not None:
        out["gate_threshold"] = float(g["threshold"])
    if g.get("enabled") is not None:
        out["gate_enabled"] = 1 if g.get("enabled") else 0
    if g.get("release") is not None:
        out["gate_release"] = float(g["release"])
    if g.get("depth") is not None:
        out["gate_depth"] = float(g["depth"])
    return out


def _preset_gate(preset_id) -> dict:
    """Read a preset's per-tone noise gate as {enabled, threshold, release,
    depth}. Falls back to the gate's defaults (off, −60/100/−60) when the
    preset or its row is missing."""
    default = {"enabled": False, "threshold": -60.0, "release": 100.0, "depth": -60.0}
    if preset_id is None:
        return default
    try:
        row = _get_conn().execute(
            "SELECT gate_threshold, gate_enabled, gate_release, gate_depth "
            "FROM presets WHERE id = ?", (preset_id,),
        ).fetchone()
    except Exception:
        return default
    if not row:
        return default
    gt, ge, gr, gd = row
    return {"enabled": bool(ge), "threshold": gt, "release": gr, "depth": gd}


def _resolve_tone_preset_id(source: str, name: str):
    """Resolve the preset id for the tone the Studio is showing, by identity:
    `default` → the default-tone preset; `saved` → the __rig_builder_saved_...
    row for `name`; anything else (a song tone) → the client sends the full
    "<filename>::<tone_key>" preset name verbatim (the same one save_preset
    stores under). Returns None when no preset exists yet (unsaved tone)."""
    if source == "default":
        return _get_default_tone_preset_id()
    pname = (_SAVED_TONE_PREFIX + name) if source == "saved" else (name or "")
    if not pname:
        return None
    try:
        row = _get_conn().execute(
            "SELECT id FROM presets WHERE name = ?", (pname,)).fetchone()
    except Exception:
        return None
    return int(row[0]) if row else None


def _persist_preset_chain(
    *,
    filename: str,
    tone_key: str,
    name: str,
    pieces: list[dict],
    input_gain: float = 1.0,
    output_gain: float = 1.0,
    gate_threshold: float | None = None,
    gate_enabled: int | None = None,
    gate_release: float | None = None,
    gate_depth: float | None = None,
    assigned_mode: str = "manual",
) -> int:
    """Insert/replace a preset + chain pieces + tone mapping. Returns
    the preset id.

    `output_gain` default is unity (1.0). nam_tone historically used
    0.5 (−6 dB), but that value gets applied TWICE in our chain — once
    by the engine's `chainOutputGain` (via `applyPresetGainLevels` →
    `setGain('chain', …)`) and once by the IR stage's internal `gain`
    in `_state_b64({"gain": output_gain})`. The double attenuation
    caused a perceived −12 dB volume drop the moment a song's preset
    replaced the user's idle chain. Defaulting to 1.0 here + emitting
    the IR stage at unity (see `native_preset_full` /
    `_build_master_stages`) leaves chainOutputGain as the single point
    of control — the OUT slider in the mixer does what it says.

    The "primary" amp piece (first piece with kind=nam) becomes the
    preset's `model_file`; the first IR piece becomes `ir_file`. That's
    what the existing nam_tone runtime reads. Everything else goes
    into preset_pieces for the UI and future multi-stage support.
    """
    filename = _db_song_key(filename)
    conn = _get_conn()
    # The nam_tone runtime only consumes one model + one IR per preset
    # (see nam_tone/routes.py:get_native_preset — single NAM + single IR;
    # multi-stage chains aren't supported by the audio engine). Pick the
    # primary NAM by slot priority. Only amp/rack are eligible: a pedal
    # NAM must NEVER be promoted to the sole "amp", or a song whose amp
    # is still unmapped but whose pedal got a capture plays the pedal as
    # if it were the amp (wrong/weak tone). With no amp/rack NAM the
    # primary stays empty and the amp surfaces as pending instead.
    # A bypassed piece is excluded from the primary model/IR too, so the
    # bundle's single-NAM runtime stays consistent with the saved bypass
    # (e.g. bypassing the amp leaves no primary, rather than playing it).
    # A full-chain NAM is the sole primary model and forces NO cab IR — the
    # capture bakes in the whole rig including the cab. Detected first so it
    # short-circuits the amp/rack + cab selection below.
    full_chain_piece = next(
        (p for p in pieces
         if p.get("kind") == _FULL_CHAIN_KIND and p.get("file")
         and not p.get("bypassed")),
        None,
    )
    if full_chain_piece:
        primary_model = full_chain_piece["file"]
        primary_ir = ""
    else:
        _MODEL_PRIORITY = _MODEL_SLOT_PRIORITY
        primary_model = ""
        for slot in _MODEL_PRIORITY:
            for p in pieces:
                if (
                    p.get("slot") == slot
                    and p.get("kind") == "nam"
                    and p.get("file")
                    and not p.get("bypassed")
                ):
                    primary_model = p["file"]
                    break
            if primary_model:
                break
        primary_ir = ""
        for p in pieces:
            if (p.get("slot") == "cabinet" and p.get("kind") in ("ir", "rs_ir")
                    and p.get("file") and not p.get("bypassed")):
                primary_ir = p["file"]
                break
        if not primary_ir:
            # Fall back to any IR if no cabinet-slot IR was provided (e.g.
            # the user supplied an IR for a rack slot instead).
            for p in pieces:
                if p.get("kind") in ("ir", "rs_ir") and p.get("file") and not p.get("bypassed"):
                    primary_ir = p["file"]
                    break

    with _lock:
        cur = conn.execute(
            "INSERT INTO presets (name, model_file, ir_file, input_gain, output_gain, "
            "  gate_threshold, gate_enabled, gate_release, gate_depth, settings_json) "
            "VALUES (?, ?, ?, ?, ?, COALESCE(?,-60.0), COALESCE(?,0), COALESCE(?,100.0), COALESCE(?,-60.0), ?) "
            "ON CONFLICT(name) DO UPDATE SET "
            "  model_file=excluded.model_file, ir_file=excluded.ir_file, "
            "  input_gain=excluded.input_gain, output_gain=excluded.output_gain, "
            # Gate columns preserve the existing row when the caller passes NULL
            # (most chain saves don't touch the gate), and only overwrite when a
            # value is supplied — so editing the chain never wipes a saved gate.
            "  gate_threshold=COALESCE(?, presets.gate_threshold), "
            "  gate_enabled=COALESCE(?, presets.gate_enabled), "
            "  gate_release=COALESCE(?, presets.gate_release), "
            "  gate_depth=COALESCE(?, presets.gate_depth)",
            (name, primary_model, primary_ir, input_gain, output_gain,
             gate_threshold, gate_enabled, gate_release, gate_depth, "{}",
             gate_threshold, gate_enabled, gate_release, gate_depth),
        )
        preset_id_row = conn.execute(
            "SELECT id FROM presets WHERE name = ?", (name,)
        ).fetchone()
        preset_id = int(preset_id_row[0])

        # Replace chain (delete + insert) — simpler than diffing.
        conn.execute("DELETE FROM preset_pieces WHERE preset_id = ?", (preset_id,))
        for i, p in enumerate(pieces):
            conn.execute(
                "INSERT INTO preset_pieces "
                "(preset_id, slot_order, slot, rs_gear_type, kind, file, params_json, "
                " tone3000_id, assigned_mode, bypassed, vst_path, vst_format, vst_state) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    preset_id,
                    i,
                    p.get("slot", ""),
                    p.get("rs_gear_type", ""),
                    p.get("kind", "none"),
                    p.get("file"),
                    json.dumps(p.get("params", {})),
                    p.get("tone3000_id"),
                    p.get("assigned_mode", assigned_mode),
                    1 if p.get("bypassed") else 0,
                    p.get("vst_path"),
                    p.get("vst_format"),
                    p.get("vst_state"),
                ),
            )

        conn.execute(
            "INSERT OR REPLACE INTO tone_mappings (filename, tone_key, preset_id) VALUES (?, ?, ?)",
            (filename, tone_key, preset_id),
        )
        conn.commit()

    return preset_id


# Slot priority for choosing a preset's primary NAM. Shared by
# _persist_preset_chain and _recompute_preset_primaries. Pedals
# (pre_pedal/post_pedal) are intentionally excluded: the engine plays a
# single NAM, and a pedal capture standing in for a missing amp sounds
# wrong. Only an amp (or a rack pre/power amp) may be the primary model.
_MODEL_SLOT_PRIORITY = ("amp", "rack")

# A "full-chain" NAM captures amp + pedals + rack + cab in a SINGLE neural
# model. It's stored as ONE preset_pieces row (slot=_FULL_CHAIN_SLOT,
# kind=_FULL_CHAIN_KIND) whose `file` is an ABSOLUTE path to the user-picked
# .nam (referenced in place — not copied into nam_models). The chain builders
# emit just that one type-1 stage at UNITY drive and append NO cab IR (the
# capture already includes the cab), skip the per-amp loudness trim, and skip
# the gear-map / cab-override layers (there is no discrete gear to redirect).
_FULL_CHAIN_KIND = "nam_fullchain"
_FULL_CHAIN_SLOT = "full_chain"


def _resolve_model_path(payload: str, models_dir):
    """Resolve a preset_pieces.file for a NAM. Relative values live under
    nam_models (sandboxed via _safe_child); a full-chain NAM stores an
    ABSOLUTE path (user-picked, referenced in place) which we use verbatim."""
    if not payload:
        return None
    p = Path(payload)
    if p.is_absolute():
        return p
    return _safe_child(models_dir, payload)


# Order in which chain pieces are sent to the audio engine as type-1 NAM
# stages: signal-flow order pre_pedal → amp → post_pedal → rack. (Used
# only by the experimental full-chain preview endpoint below — the
# persisted presets still expose a single primary NAM for the bundle's
# 2-stage runtime.)
_CHAIN_NAM_ORDER = ("pre_pedal", "amp", "post_pedal", "rack")


def _state_b64(data: dict) -> str:
    """Base64 of compact JSON — byte-identical to nam_tone's _state_b64,
    which the native engine expects for each chain stage's `state`."""
    payload = json.dumps(data, separators=(",", ":")).encode("utf-8")
    return base64.b64encode(payload).decode("ascii")


def _vst_stage_state(vst_path: str, vst_format: str | None, vst_state) -> str:
    """Return the base64 `state` for a VST (type-0) chain stage.

    The native engine restores a VST's settings during real song playback
    ONLY from its opaque state blob — the exact value `savePreset()` produces
    and `loadPreset()` round-trips (the UI also feeds it to `setSlotState`).
    The client captures that blob and stores it under "opaque" in the
    vst_state envelope `{"params": {...}, "opaque": "<base64>"}`; we emit it
    verbatim so the plugin comes up with its captured parameters (e.g. a
    compressor's makeup gain), not its defaults.

    Legacy pieces saved before opaque-capture (vst_state is a bare
    `{"params": {...}}` or empty) fall back to the old pluginPath/pluginState
    wrapper. Current native hosts also parse that wrapper and apply named
    params on load, so these presets no longer depend on the editor/config
    path to leave plugin defaults.
    """
    opaque = None
    if vst_state:
        try:
            env = json.loads(vst_state)
            if isinstance(env, dict):
                blob = env.get("opaque")
                if isinstance(blob, str) and blob:
                    opaque = blob
        except (ValueError, TypeError):
            opaque = None
    if opaque:
        return opaque
    state_obj = {"pluginPath": str(vst_path), "format": vst_format or "VST3"}
    if vst_state:
        state_obj["pluginState"] = vst_state
    return _state_b64(state_obj)


# ── Shared chain-stage builders ──────────────────────────────────────
# One place each for the type-1 (NAM), type-2 (IR) and type-0 (VST) stage
# dicts that the four chain builders (_build_master_stages,
# native_preset_full, mega_chain's _build_tone_stages, native_preset_one)
# used to assemble by hand. Optional slot/rs_gear/tone_key are omitted when
# None so every caller's emitted JSON stays byte-identical to before.

def _nam_stage(path, *, bypassed, input_level=1.0, output_drive=None,
               output_mult=1.0, slot=None, rs_gear=None, tone_key=None,
               rs_gain=None) -> dict:
    """Build a type-1 (NAM) chain stage.

    `input_level` is the engine inputLevel (drive into the model); the
    outputLevel is the per-NAM loudness makeup (`_nam_normalized_output_level`,
    divided by `output_drive` when given) times `output_mult` (the single-NAM
    audition multiplies by its caller-supplied gain; everyone else leaves it
    at 1.0).
    """
    out_level = output_mult * _nam_normalized_output_level(
        path, effective_input_drive=output_drive)
    stage = {"type": 1, "name": Path(path).stem, "path": str(path),
             "bypassed": bypassed}
    if slot is not None:
        stage["slot"] = slot
    if rs_gear is not None:
        stage["rs_gear"] = rs_gear
    if tone_key is not None:
        stage["tone_key"] = tone_key
    if rs_gain is not None:
        try:
            stage["rs_gain"] = float(rs_gain)
        except (TypeError, ValueError):
            pass
    stage["state"] = _state_b64({"modelPath": str(path),
                                 "inputLevel": input_level,
                                 "outputLevel": out_level})
    return stage


# RS cab IR makeup, applied via the per-stage IR `gain` (in `_state_b64`).
# CORRECTION (2026-06-04): the native engine DOES read+apply the per-stage IR
# `gain` — confirmed by (a) the slopsmith_audio.node binary exposing `irPath`/
# `gain`/`state` state keys, (b) the v1.3.2 double-attenuation bug (IR gain 0.5
# × chainOutputGain 0.5 = −12 dB), and (c) IRLoader.cpp reading outputGain off
# the slot state. The old "engine IGNORES per-stage IR gain" note was wrong.
#
# Why +12 dB: the engine loads cab IRs with juce::dsp::Convolution `Normalise::
# yes`, which force-renormalizes EVERY IR to a broadband gain of 0.125 (−18.1
# dB) — discarding the extractor's L2=2.4 (+7.6 dB) calibration. That flat −18
# dB is what made cabs (esp. bass, energy in 40–250 Hz) sound quiet/thin. This
# per-cab gain recovers it at the IR stage (robust on every chain path, unlike
# the fragile `setGain('chain')` makeup). 8.0 (+18.1 dB) = 1/0.125 EXACTLY
# cancels JUCE's force-normalize so the cab sits at its natural broadband level.
# The previous 4.0 (+12 dB) was a half-correction (net ~-6 dB) -> cabs still too
# quiet to hear ("varios cabs no suenan", esp. Marshall/guitar cabs); tune by ear
# (dial to 6.3 ~+16 dB if it runs hot).
_RS_IR_MAKEUP = 8.0


def _is_rocksmith_ir_file(value: str | Path | None) -> bool:
    if not value:
        return False
    return "rocksmith" in Path(value).as_posix().lower()


def _is_synth_cab_ir(value: str | Path | None) -> bool:
    """Our own synthesized/captured cab IRs live under nam_irs/cabs/ (bundled
    RC_ sets) AND nam_irs/realcab/ (posiciones custom del Cab Room). The engine's
    JUCE convolution force-normalizes EVERY cab IR to ~-18 dB, so these need the
    SAME +18 dB makeup as the RS cabs to land at a usable level — but they are NOT
    rocksmith assets, so the global 'bypass all Rocksmith cabs' must NOT skip them.
    (Bug arreglado: realcab/ no matcheaba → los tonos con posición custom del
    Cab Room quedaban ~18 dB abajo = cab inaudible en la canción.)"""
    if not value:
        return False
    p = Path(value).as_posix().lower()
    return "cabs/" in p or "realcab/" in p


def _ir_stage_gain(kind: str | None, ir_path: str | Path | None, base_gain: float = 1.0) -> float:
    """Final IR gain sent to the native engine.

    the game-extracted cab IRs need the same +12 dB makeup in every path:
    full song chains, mega-chain playback, master-chain stages and Gear-catalog
    audition. Gear audition sometimes sends kind="ir" even for files under
    nam_irs/rocksmith/, so detect both by kind and by path.
    """
    try:
        g = float(base_gain)
    except (TypeError, ValueError):
        g = 1.0
    if (kind or "").lower() == "rs_ir" or _is_rocksmith_ir_file(ir_path) or _is_synth_cab_ir(ir_path):
        return g * _RS_IR_MAKEUP
    return g


# ── Per-amp loudness normalization (computed in rig builder, NOT in the VST) ──
#
# Every bundled amp VST3 bakes its own output coeff (kLvl), so amps drift in
# loudness (measured range ≈ −25…−2 LUFS) and cranking Gain raises level. The
# fix lives here, not in the DSP: data/amp_loudness_model.json holds a measured
# BS.1770-4 LUFS sweep per amp (tools/measure_amp_loudness.py). From the amp's
# resolved params we compute a CLEAN trim (dB) so every amp lands at the target
# (−14 LUFS), with Gain allowed to push output up to −12 and Volume/Master up
# to −11. The trim rides a tiny unit-impulse IR stage right after the amp (a
# clean gain the engine already applies to IR stages) — the amp's own Gain param
# is never touched, so it still saturates exactly as before. See AMP_LOUDNESS.md.
_AMP_LOUDNESS_FILE = "amp_loudness_model.json"
# CONSISTENCY across songs: every amp lands at the SAME loudness. The soft caps
# used to let a high-gain amp sit ~2-3 dB LOUDER than a quiet DI preamp (Gain→−12,
# Vol→−11 vs a quiet amp stuck at −14), and the final leveler is a fast limiter
# that doesn't re-normalise that average — so songs came out at different volumes
# (e.g. Runaways ≈ −12 vs Runaway Train ≈ −14). Caps are now 0 (Gain/Vol change the
# CHARACTER, not the level — the leveler owns volume), and the target is the level
# the good-sounding songs already sat at (−12), so nothing that was fine gets
# quieter, only the too-quiet ones come up to match.
_AMP_TARGET_LUFS = -12.0
_AMP_CAP_GAIN_DB = 0.0     # no per-gain loudness bump — all amps hit the same target
_AMP_CAP_VOL_DB = 0.0      # no per-volume loudness bump
_AMP_TRIM_MIN_DB = -24.0   # deepest cut (very loud amps, e.g. jc90 ≈ −1.8 LUFS)
_AMP_TRIM_MAX_DB = 12.0    # largest boost — enough for a quiet DI preamp to reach −12
_AMP_TRIM_RS_GEAR = "__rb_amp_trim"   # sentinel rs_gear on the trim stage


def _load_amp_loudness_model() -> dict:
    return _load_cached_json(_AMP_LOUDNESS_FILE, post=_strip_meta_keys)


def _interp_curve(curve, x: float) -> float:
    """Linear-interpolate a [[v, lufs], …] sweep (v ascending in [0, 1])."""
    if not curve:
        return _AMP_TARGET_LUFS
    if x <= curve[0][0]:
        return curve[0][1]
    if x >= curve[-1][0]:
        return curve[-1][1]
    for i in range(1, len(curve)):
        x0, y0 = curve[i - 1]
        x1, y1 = curve[i]
        if x <= x1:
            t = (x - x0) / (x1 - x0) if x1 > x0 else 0.0
            return y0 + t * (y1 - y0)
    return curve[-1][1]


def _amp_loudness_trim_db(entry: dict, params: dict) -> float:
    """Clean trim (dB) that levels an amp to the target with the soft caps.

    `entry` is one amp's amp_loudness_model.json record; `params` is the amp's
    resolved {param_name: 0..1} (missing names fall back to the measured
    defaults). Gain stays saturating — we never touch it — but its loudness
    contribution is soft-capped at +2 dB and Volume/Master's at +3 dB, so the
    output sits in the −14…−11 LUFS band whatever the knobs do.
    """
    default = entry.get("default") or {}

    def _val(name):
        try:
            return float(params.get(name, default.get(name, 0.5)))
        except (TypeError, ValueError):
            return float(default.get(name, 0.5))

    gain_params = entry.get("gain_params") or []
    gain_curve = entry.get("gain_curve") or []
    if gain_params and gain_curve:
        g_now = sum(_val(p) for p in gain_params) / len(gain_params)
        g_def = sum(float(default.get(p, 0.5)) for p in gain_params) / len(gain_params)
        l_gain = _interp_curve(gain_curve, g_now)
        l_def = _interp_curve(gain_curve, g_def)
    else:
        l_gain = l_def = float(entry.get("lufs_default", _AMP_TARGET_LUFS))

    d_vol = 0.0
    for name, curve in (entry.get("vol_curves") or {}).items():
        if not curve:
            continue
        d_vol += _interp_curve(curve, _val(name)) - _interp_curve(curve, float(default.get(name, 0.5)))

    d_gain = l_gain - l_def
    l_meas = l_gain + d_vol
    extra_g = _AMP_CAP_GAIN_DB * math.tanh(max(0.0, d_gain) / _AMP_CAP_GAIN_DB) if _AMP_CAP_GAIN_DB > 0 else 0.0
    extra_v = _AMP_CAP_VOL_DB * math.tanh(max(0.0, d_vol) / _AMP_CAP_VOL_DB) if _AMP_CAP_VOL_DB > 0 else 0.0
    desired = _AMP_TARGET_LUFS + extra_g + extra_v
    return max(_AMP_TRIM_MIN_DB, min(_AMP_TRIM_MAX_DB, desired - l_meas))


def _amp_params_from_state(effective_vst_state) -> dict | None:
    """Named params from an amp's effective VST state.

    Returns {} when there's no state (→ use the model's measured defaults), a
    {name: value} dict for the normal `{"params": {...}}` envelope, or None when
    the state is opaque/unreadable (→ skip trimming, can't read the params).
    """
    if not effective_vst_state:
        return {}
    try:
        env = json.loads(effective_vst_state) if isinstance(effective_vst_state, str) else effective_vst_state
    except (ValueError, TypeError):
        return None
    if not isinstance(env, dict) or env.get("opaque"):
        return None
    p = env.get("params")
    return p if isinstance(p, dict) else None


def _amp_trim_mult_for_state(vst_path, effective_vst_state) -> float:
    """Linear post-amp gain that levels this amp VST. 1.0 (no-op) for any VST
    that isn't a modeled amp, or whose state is opaque."""
    stem = Path(vst_path).stem
    model = _load_amp_loudness_model()
    if not model:
        return 1.0
    entry = model.get(stem.lower())
    if not isinstance(entry, dict):
        return 1.0
    params = _amp_params_from_state(effective_vst_state)
    if params is None:
        return 1.0
    return 10.0 ** (_amp_loudness_trim_db(entry, params) / 20.0)


# The engine loads EVERY IR stage with JUCE Normalise::yes, which rescales the
# impulse to a broadband L2 of 0.125 (-18.1 dB) — including our 1-sample unit
# impulse. Cab stages cancel that with _RS_IR_MAKEUP (x8); the unit-impulse
# stages (amp trim, bare-cab lift) did NOT, so every chain carrying one played
# a flat -18.1 dB. That ate almost the whole +20 dB max-boost of the final
# leveler: real chains arrived at -33..-45 LUFS (measured with the instrumented
# leveler, 2026-07-08), the AGC pegged at +20 and songs came out 2..5 dB apart
# ("unas canciones necesitan +12 en AMP"). x8 makes the impulse stage carry
# exactly its intended clean gain again.
_UNIT_IMPULSE_MAKEUP = 8.0


def _unit_impulse_ir_path() -> Path | None:
    """A cached 1-sample (identity) IR under nam_irs/other/. The per-amp loudness
    trim rides this as a clean gain stage right after the amp."""
    if not _config_dir:
        return None
    p = _config_dir / "nam_irs" / "other" / "_rb_unit_impulse.wav"
    if not p.exists():
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            _write_ir_f32(p, [1.0], 48000)
        except OSError:
            log.warning("could not write unit-impulse IR for amp trim", exc_info=True)
            return None
    return p


def _amp_trim_stage(trim_mult: float, *, tone_key=None) -> dict | None:
    """A unit-impulse IR stage carrying `trim_mult` as a clean gain — the per-amp
    loudness trim, inserted right after the amp VST. None when the trim is a
    no-op or the impulse IR can't be written. The top-level `amp_trim` marker
    keeps mega_chain's IR dedupe from merging amps that need different trims."""
    if abs(trim_mult - 1.0) < 1e-3:
        return None
    ir = _unit_impulse_ir_path()
    if not ir:
        return None
    # SPLIT the gain across the two engine mechanisms so it sums correctly on
    # every engine version (measured with the instrumented leveler, 2026-07-08):
    #   - state.gain = x8 ONLY (_UNIT_IMPULSE_MAKEUP): cancels the engine's
    #     Normalise::yes on the impulse. Needs the NodeAddon standard-base64
    #     state fix — older engines silently drop plugin-emitted state, which
    #     is exactly why this stage played -18.1 dB flat.
    #   - postGain = the plain trim (below): applied by the legacy loadPreset
    #     path on ALL engine versions.
    # Fixed engine: 0.125 x 8 x trim = trim (intended). Old engine: trim at
    # -18.1 dB (unchanged behaviour, the leveler compensates what it can).
    st = _ir_stage(ir, bypassed=False, gain=_UNIT_IMPULSE_MAKEUP,
                   slot="amp", rs_gear=_AMP_TRIM_RS_GEAR, tone_key=tone_key)
    st["amp_trim"] = round(trim_mult, 4)
    # Engines with per-slot postGain support (loadPreset reads this optional
    # field) apply the trim ON THIS SLOT — which is what makes it work inside a
    # parallel amp branch. Older engines ignore both this and `gain` (the trim
    # was historically folded/absorbed by the final leveler in serial chains).
    st["postGain"] = round(trim_mult, 4)
    return st


# Per-cab RMS matching. A cab IR's broadband convolution gain — i.e. how much
# output RMS it imparts (output_RMS = input_RMS × ‖IR‖₂ for broadband input) —
# is exactly its L2 norm. After the ±1.0 clip-safe peak cap, IRs no longer all
# share an L2 (the peakiest get pulled down ~8 dB), so different cabs/mics play
# at different loudness. We compute each RS cab IR's L2 and a makeup factor
# (target_L2 / L2); it is now BAKED INTO THE ENGINE IR gain in `_ir_stage` (the
# IRLoader applies stage `gain` via buffer.applyGain, confirmed in the engine —
# an earlier note here that "the engine ignores per-IR gain" was stale). This
# makes every cab impart the same output RMS PER STAGE — correct per-tone and
# per-song — instead of the old global last-wins chain-bus fold in screen.js,
# matching amps/pedals' loudness normalization (`_nam_normalized_output_level`).
_IR_REF_L2 = 2.4          # common broadband gain (matches tone3000 cab IRs)
_IR_MAKEUP_MAX = 2.818    # +9 dB cap — covers the ~8 dB L2 spread with headroom
_ir_l2_cache: dict[tuple, float | None] = {}
_ir_l2_lock = threading.Lock()


def _read_ir_l2(path: Path) -> float | None:
    """L2 norm (sqrt of total energy) of a mono float32 cab IR. Returns None
    when the file is unreadable or not the mono-float32 shape we ship."""
    try:
        blob = path.read_bytes()
    except OSError:
        return None
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        return None
    # A truncated/corrupt WAV makes an unpack see a short buffer and
    # raise struct.error — treat it like any other unreadable IR.
    try:
        pos = 12
        fmt_tag = ch = bps = None
        data_off = data_size = None
        while pos < len(blob) - 8:
            cid = blob[pos:pos + 4]
            csize = struct.unpack("<I", blob[pos + 4:pos + 8])[0]
            if cid == b"fmt ":
                fmt_tag, ch, _sr, _, _, bps = struct.unpack(
                    "<HHIIHH", blob[pos + 8:pos + 8 + 16])
            elif cid == b"data":
                data_off, data_size = pos + 8, csize
            pos += 8 + csize + (csize & 1)
        if (fmt_tag, bps) != (3, 32) or data_off is None or ch != 1:
            return None
        if data_off + data_size > len(blob):
            return None   # declared data chunk runs past the file
        n = data_size // 4
        if n == 0:
            return None
        total = sum(v * v for v in struct.unpack(
            "<%df" % n, blob[data_off:data_off + data_size]))
        return total ** 0.5
    except struct.error:
        return None


def _ir_l2_for_path(path: Path) -> float | None:
    """Cached `_read_ir_l2`. Keyed by (path, mtime, size) so the value is
    recomputed if the IR is rewritten (e.g. the normalize endpoint)."""
    try:
        st = path.stat()
        key = (str(path), st.st_mtime_ns, st.st_size)
    except OSError:
        return None
    with _ir_l2_lock:
        if key in _ir_l2_cache:
            return _ir_l2_cache[key]
    val = _read_ir_l2(path)
    with _ir_l2_lock:
        _ir_l2_cache[key] = val
    return val


def _ir_rms_makeup(path: Path) -> float:
    """Linear makeup that brings this cab IR's broadband gain to `_IR_REF_L2`
    so every cab imparts the same output RMS. Clamped to [1.0, _IR_MAKEUP_MAX]
    — we only ever lift quiet IRs toward the common target, never cut (the
    target equals the loudest IRs' L2). Falls back to 1.0 when L2 is unknown."""
    l2 = _ir_l2_for_path(path)
    if not l2 or l2 <= 0.0:
        return 1.0
    return max(1.0, min(_IR_MAKEUP_MAX, _IR_REF_L2 / l2))


# Per-cab PERCEPTUAL loudness makeup — the accurate version of the L2 match above.
# data/cab_loudness_makeup.json holds a measured BS.1770-4 (K-weighted) loudness
# per cab (pink-noise reference convolved through each cab's IRs, target = median),
# because L2 (broadband) badly under-reads the spread: the real spread is ~21 dB
# vs ~7 dB by L2 (full-range hi-fi/PA cabs sit far quieter perceptually than a
# mid-forward guitar 4x12). Keyed by the IR folder name = clone_slug, which at
# runtime is Path(ir_path).parent.name. Regenerate with tools/measure_cab_loudness.py.
_cab_loud_makeup_tbl: dict | None = None


def _load_cab_loudness_makeup() -> dict:
    global _cab_loud_makeup_tbl
    if _cab_loud_makeup_tbl is None:
        try:
            p = _data_path("cab_loudness_makeup.json")
            _cab_loud_makeup_tbl = (json.loads(p.read_text(encoding="utf-8")).get("makeup", {})
                                    if p.exists() else {})
        except Exception:
            _cab_loud_makeup_tbl = {}
    return _cab_loud_makeup_tbl


def _cab_loudness_makeup(path: Path) -> float:
    """Per-cab measured-LUFS makeup; falls back to the broadband L2 match when the
    cab isn't in the table (e.g. a freshly synthesized custom mic-position IR)."""
    f = _load_cab_loudness_makeup().get(path.parent.name)
    if isinstance(f, (int, float)) and f > 0:
        return float(f)
    return _ir_rms_makeup(path)


# ── DI + Cab blend for bass cabs (the "70% DI / 30% cab" feature) ─────────────
# Real bass is amplified/recorded mostly via DI with a little mic'd cab. The
# native engine is series-only (no parallel dry/wet on an IR), so the blend is
# baked into ONE impulse response:
#     blend = 0.7*delta(@cab peak) + 0.3*(cab / ||cab||2)
# Convolving with it = 0.7*DI(dry) + 0.3*cab, with DI and cab at the SAME
# broadband level (then weighted 70/30); the delta aligns to the cab's peak to
# minimise comb filtering. The blend is brighter than the cab alone, so matching
# it by broadband RMS would make bass tones ~4 dB quieter — instead each cab's
# stage `cab_rms_makeup` is the precomputed factor that keeps the BASS-band
# loudness equal to the cab-alone path (data/di_cab_makeup.json, cross-validated
# to <0.5 dB). Generation is pure-python (no numpy) so it runs in the chain
# builder; the makeup table is precomputed offline by tools/make_di_cab_irs.py.
_DI_CAB_DI = 0.8
_DI_CAB_CAB = 0.2
_di_cab_makeup_tbl: dict | None = None


def _di_cab_enabled() -> bool:
    return bool(_load_settings().get("bass_di_cab", True))


def _is_bass_cab_ir(ir_path) -> bool:
    return "bass_cab" in Path(ir_path).name.lower()


def _load_di_cab_makeup() -> dict:
    global _di_cab_makeup_tbl
    if _di_cab_makeup_tbl is None:
        try:
            p = _data_path("di_cab_makeup.json")
            _di_cab_makeup_tbl = (json.loads(p.read_text(encoding="utf-8")).get("makeup", {})
                                  if p.exists() else {})
        except (OSError, ValueError):
            _di_cab_makeup_tbl = {}
    return _di_cab_makeup_tbl


def _read_ir_samples(path: Path):
    """(samples:list[float], sr:int) for a mono float32 cab IR, or None."""
    try:
        blob = path.read_bytes()
    except OSError:
        return None
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        return None
    # Same truncated-file guard as _read_ir_l2.
    try:
        pos = 12
        fmt_tag = ch = sr = bps = None
        data_off = data_size = None
        while pos < len(blob) - 8:
            cid = blob[pos:pos + 4]
            csize = struct.unpack("<I", blob[pos + 4:pos + 8])[0]
            if cid == b"fmt ":
                fmt_tag, ch, sr, _, _, bps = struct.unpack(
                    "<HHIIHH", blob[pos + 8:pos + 8 + 16])
            elif cid == b"data":
                data_off, data_size = pos + 8, csize
            pos += 8 + csize + (csize & 1)
        if (fmt_tag, bps) != (3, 32) or data_off is None or ch != 1 or not data_size:
            return None
        if data_off + data_size > len(blob):
            return None   # declared data chunk runs past the file
        n = data_size // 4
        if n == 0:
            return None
        return list(struct.unpack("<%df" % n, blob[data_off:data_off + data_size])), sr
    except struct.error:
        return None


def _write_ir_f32(path: Path, samples, sr: int) -> None:
    raw = struct.pack("<%df" % len(samples), *samples)
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


def _di_cab_blend_samples(cab):
    """DI*delta(@peak) + CAB*(cab/||cab||2), then SCALED so the whole blend has
    the same L2 as a normal RS cab (_IR_REF_L2). That makes the engine treat the
    blend exactly like any other cab IR loudness-wise (so the big DI delta inside
    it doesn't get processed quietly), while preserving the DI:cab ratio."""
    l2 = (sum(x * x for x in cab) ** 0.5) or 1.0
    scale = _DI_CAB_CAB / l2
    blend = [x * scale for x in cab]
    peak = max(range(len(cab)), key=lambda i: abs(cab[i]))
    blend[peak] += _DI_CAB_DI
    bl2 = (sum(x * x for x in blend) ** 0.5) or 1.0
    norm = _IR_REF_L2 / bl2
    return [x * norm for x in blend]


def _di_cab_blend_file(cab_path: Path) -> Path | None:
    """Generate (and cache) the DI+cab blend IR for a bass cab. Pure-python;
    cached under nam_irs/rocksmith_dicab/ (kept under a 'rocksmith' path so
    screen.js still treats it as an RS cab: +6 dB + cab_rms_makeup)."""
    if not _config_dir:
        return None
    try:
        # Versioned by the DI/cab ratio so changing it regenerates (stale blends
        # of a previous ratio are never reused). Kept under a 'rocksmith' path.
        sub = "rocksmith_dicab2_%d_%d" % (round(_DI_CAB_DI * 100), round(_DI_CAB_CAB * 100))
        out = _config_dir / "nam_irs" / sub / cab_path.name
        try:
            if out.exists() and out.stat().st_mtime_ns >= cab_path.stat().st_mtime_ns:
                return out
        except OSError:
            pass
        r = _read_ir_samples(cab_path)
        if not r:
            return None
        samples, sr = r
        blend = _di_cab_blend_samples(samples)
        out.parent.mkdir(parents=True, exist_ok=True)
        _write_ir_f32(out, blend, sr)
        return out
    except Exception:
        log.warning("di_cab blend failed for %s", cab_path, exc_info=True)
        return None


def _di_cab_makeup_for(cab_path: Path, blend_path: Path) -> float:
    """cab_rms_makeup for the blend stage: the precomputed bass-loudness-
    preserving factor, else a broadband normalize of the blend (fallback)."""
    mk = _load_di_cab_makeup().get(cab_path.stem)
    if mk:
        try:
            return float(mk)
        except (TypeError, ValueError):
            pass
    return _ir_rms_makeup(blend_path)


def _ir_stage(ir_path, *, bypassed, gain=1.0,
              slot=None, rs_gear=None, tone_key=None, di_cab=False) -> dict:
    """Build a type-2 (cab IR) chain stage. `gain` is unity by default — the
    engine's chainOutputGain already applies the preset's output_gain, so the
    IR stays at 1.0 except in the single-IR audition where the caller passes
    its own gain (see the −12 dB double-attenuation fix).

    `di_cab` (set only by the per-tone chain builders) swaps a BASS cab IR for
    its DI+cab blend when the feature is on — see the DI+Cab helpers."""
    ir_path = str(ir_path)
    # Auto-use OUR own cab IR in place of the game's mic-position IR whenever
    # we ship one (rb_cab_overrides.json). Done here (not via a DB rewrite) so it
    # covers every chain-build path and stays non-destructive. After this the
    # path is under nam_irs/cabs/ → NOT a game-shipped IR → exempt from the global
    # bypass below, and `_is_synth_cab_ir` still applies the +18 dB makeup.
    _was_rs_ir = _is_rocksmith_ir_file(ir_path)
    ir_path = _apply_cab_override(ir_path)
    # Global "Bypass all the game cabs" (Settings): force-skip the RS cab on the
    # song chain (di_cab path) so the user can run their own cab/IR. This makes the
    # toggle authoritative even for songs whose preset_pieces row predates / wasn't
    # touched by the /settings DB bulk-flip. Catalog auditions (di_cab=False) still
    # play, so cabs remain comparable.
    if di_cab and _is_rocksmith_ir_file(ir_path) and _load_settings().get("bypass_all_cabs"):
        bypassed = True
    # Inverso del caso anterior: si el override REEMPLAZÓ el IR del juego por el
    # NUESTRO, un bypassed=1 guardado en la DB casi siempre viene del bulk-flip
    # viejo de "Bypass all the game cabs" — apuntaba al cab del juego, no al
    # nuestro. Con bypass_all_cabs activo, límpialo para que el cab modelado
    # SUENE (síntoma: "los cabs no suenan al cargar la canción hasta mover el
    # mic" — mover el mic re-persistía la pieza sin el flag).
    if (_was_rs_ir and not _is_rocksmith_ir_file(ir_path) and bypassed
            and _load_settings().get("bypass_all_cabs")):
        bypassed = False
    di_cab_blend = False
    if di_cab and _is_bass_cab_ir(ir_path) and _di_cab_enabled():
        blended = _di_cab_blend_file(Path(ir_path))
        if blended is not None:
            # Keep the cab's own IR gain (_RS_IR_MAKEUP for RS cabs) — the raw cab
            # played at a fine level with it, and the blend is DI-dominant (0.9),
            # so at the same gain it sits at least as loud as the raw cab did.
            # (An earlier 1/DI override dropped it ~4×, which is what made the bass
            # play very quiet vs running no cab.) The engine applies this `gain`
            # unconditionally, so VST-amp chains get it too.
            ir_path = str(blended)
            di_cab_blend = True
    stage = {"type": 2, "name": Path(ir_path).stem, "path": str(ir_path),
             "bypassed": bypassed}
    if slot is not None:
        stage["slot"] = slot
    if rs_gear is not None:
        stage["rs_gear"] = rs_gear
    if tone_key is not None:
        stage["tone_key"] = tone_key
    # Per-cab RMS-match factor for screen.js (engine ignores it, like slot/
    # rs_gear). Only RS cab IRs vary in L2 after the clip-safe peak cap; other
    # IRs (tone3000) are already loudness-normalized, so we leave them alone.
    # For a DI+cab blend the bass-loudness makeup is already baked into the IR
    # `gain` above (engine-applied, works for VST + NAM amps alike), so keep the
    # chain-gain cab makeup neutral here — else NAM-amp chains would double it.
    p = Path(ir_path)
    if _is_rocksmith_ir_file(p) or _is_synth_cab_ir(p):
        # Per-cab loudness makeup — BAKE IT INTO THE ENGINE GAIN so every cab lands
        # at the final leveler MATCHED. Uses the measured BS.1770 table (real spread
        # ~21 dB) with an L2 fallback. It used to be surfaced only as `cab_rms_makeup`
        # and folded into the global JS chain bus — a single last-wins value that
        # couldn't equalize per-tone or per-song, so the flat _RS_IR_MAKEUP left the
        # spread for the AGC to chase (which it can't do per song → songs came out at
        # different volumes). Now every cab stage carries its own makeup, correct even
        # in the deduped mega-chain. DI+cab blends already bake their own makeup.
        cab_mk = 1.0 if di_cab_blend else _cab_loudness_makeup(p)
        gain = gain * cab_mk
        # Field kept for the UI but neutralised so the JS chain-bus fold is a no-op
        # (the makeup now lives in the engine gain above — never applied twice).
        stage["cab_rms_makeup"] = 1.0
    stage["state"] = _state_b64({"irPath": str(ir_path), "gain": gain})
    return stage


def _vst_stage(vst_path, vst_format, *, bypassed, state,
               slot=None, rs_gear=None, tone_key=None) -> dict:
    """Build a type-0 (VST) chain stage. The `state` blob is passed in by the
    caller because the encodings differ (full/master use `_vst_stage_state`,
    which restores captured opaque state; mega/audition use the simpler
    pluginPath wrapper). This helper only unifies the repeated dict shape."""
    stage = {"type": 0, "name": Path(vst_path).stem, "path": str(vst_path),
             "format": vst_format, "bypassed": bypassed}
    if slot is not None:
        stage["slot"] = slot
    if rs_gear is not None:
        stage["rs_gear"] = rs_gear
    if tone_key is not None:
        stage["tone_key"] = tone_key
    stage["state"] = state
    return stage


def _safe_child(root: Path | None, name: str | None) -> Path | None:
    """Resolve `name` under `root`, refusing path-escape. Mirrors
    nam_tone._safe_child so the engine gets the same absolute paths."""
    if not root or not name:
        return None
    root_resolved = root.resolve()
    path = (root / name).resolve()
    try:
        path.relative_to(root_resolved)
    except ValueError:
        return None
    return path


def _tone_image_index() -> dict:
    """Map tone3000 tone_id -> {title, image, gear} from the LOCAL search
    cache, so the gear catalog can show a photo for an assigned capture
    without extra network calls. Best-effort; empty if the cache is absent.
    tone3000's Tone objects carry an `images` array of public URLs."""
    idx: dict[int, dict] = {}
    if _config_dir is None:
        return idx
    # Prefer the new filename; fall back to the legacy one if the t3k
    # client hasn't been instantiated yet (which would have migrated it).
    cache = _config_dir / "rig_builder_cache.db"
    if not cache.exists():
        legacy = _config_dir / "nam_rig_builder_cache.db"
        if legacy.exists():
            cache = legacy
        else:
            return idx
    try:
        c = sqlite3.connect(f"file:{cache}?mode=ro", uri=True)
        try:
            for (rj,) in c.execute("SELECT response_json FROM search_cache"):
                try:
                    data = json.loads(rj)
                except Exception:
                    continue
                if not isinstance(data, dict):
                    continue
                for t in (data.get("data") or []):
                    if (isinstance(t, dict) and isinstance(t.get("id"), int)
                            and t["id"] not in idx):
                        imgs = t.get("images") or []
                        idx[t["id"]] = {
                            "title": t.get("title"),
                            "image": imgs[0] if imgs else None,
                            "gear": t.get("gear"),
                        }
        finally:
            c.close()
    except Exception:
        log.debug("tone image index unavailable", exc_info=True)
    return idx


def _recompute_preset_primaries(conn: sqlite3.Connection, preset_id: int) -> None:
    """Re-derive `presets.model_file` / `ir_file` from the preset's
    current `preset_pieces`, using the same slot priority as
    `_persist_preset_chain`. The nam_tone runtime only plays those two
    columns, so this is what makes a freshly-assigned piece audible.

    Caller must already hold `_lock`.
    """
    rows = conn.execute(
        "SELECT slot, kind, file, bypassed FROM preset_pieces "
        "WHERE preset_id = ? ORDER BY slot_order",
        (preset_id,),
    ).fetchall()
    # Bypassed pieces are excluded from the primary model/IR (consistent
    # with _persist_preset_chain and the saved bypass state).
    pieces = [
        {"slot": r[0], "kind": r[1], "file": r[2]}
        for r in rows if not r[3]
    ]

    # A full-chain NAM is the sole primary model and forces no cab IR — mirror
    # _persist_preset_chain so a recompute never wipes the baked capture.
    full_chain = next(
        (p for p in pieces if p["kind"] == _FULL_CHAIN_KIND and p["file"]), None)
    if full_chain:
        model_file, ir_file = full_chain["file"], ""
    else:
        model_file = ""
        for slot in _MODEL_SLOT_PRIORITY:
            for p in pieces:
                if p["slot"] == slot and p["kind"] == "nam" and p["file"]:
                    model_file = p["file"]
                    break
            if model_file:
                break
        ir_file = ""
        for p in pieces:
            if p["slot"] == "cabinet" and p["kind"] in ("ir", "rs_ir") and p["file"]:
                ir_file = p["file"]
                break
        if not ir_file:
            for p in pieces:
                if p["kind"] in ("ir", "rs_ir") and p["file"]:
                    ir_file = p["file"]
                    break

    conn.execute(
        "UPDATE presets SET model_file = ?, ir_file = ? WHERE id = ?",
        (model_file, ir_file, preset_id),
    )


def _assign_file_to_gear(
    rs_gear: str, kind: str, file: str, tone3000_id: int | None = None,
) -> dict:
    """Stamp a downloaded file onto every preset_pieces row for `rs_gear`
    (replacing any prior assignment) and refresh the affected presets'
    primary model/IR.

    This is the manual "Download and assign" path. It moves a gear out of
    the Pending list AND replaces an existing capture when the user picks a
    new one (the coverage query counts a gear as pending while any piece has
    `kind='none'` / empty `file`, and the nam_tone runtime only plays
    `presets.model_file` / `ir_file`). It updates ALL of the gear's rows so
    a re-pick actually takes effect rather than keeping the old NAM.

    Returns `{pieces_updated, presets_updated}`. If the gear has no
    preset rows yet (never batched/saved), both are 0 and the caller's
    downloaded file is still returned for the song-view flow to use.
    """
    conn = _get_conn()
    with _lock:
        # Update EVERY preset_pieces row for this gear, not just pending
        # ones. This is the explicit user "Download and assign" path (the
        # only caller), so re-picking a capture for a gear that already has
        # one must REPLACE the old file — otherwise the gear keeps the
        # previous NAM (the bug). Auto/batch flows don't call this; they go
        # through _persist_preset_chain / _download_candidate.
        affected = [
            r[0] for r in conn.execute(
                "SELECT DISTINCT preset_id FROM preset_pieces WHERE rs_gear_type = ?",
                (rs_gear,),
            ).fetchall()
        ]
        cur = conn.execute(
            "UPDATE preset_pieces "
            "SET kind = ?, file = ?, tone3000_id = ?, assigned_mode = 'manual' "
            "WHERE rs_gear_type = ?",
            (kind, file, tone3000_id, rs_gear),
        )
        pieces_updated = cur.rowcount
        for pid in affected:
            _recompute_preset_primaries(conn, pid)
        conn.commit()
    return {"pieces_updated": pieces_updated, "presets_updated": len(affected)}



# Module-level gear-assignment resolver (lifted out of setup()'s closure
# in the v1.3.2 restructure so the chain builders, per-tone save, and the
# global consolidation can all share ONE resolution path — the single
# source of truth for 'what does this gear play?'.
def _resolve_gear_assignment(rs_gear: str, level: str | None,
                             rs_gain: float | None) -> dict | None:
    """Resolve what to play for `rs_gear` — could be a NAM file,
    an extracted the game IR, or a VST plugin path.

    Returns a dict shaped like a preset_piece update payload, or
    None when nothing's available:

      {
        "kind": "nam" | "rs_ir" | "vst",
        "file": "<subdir>/<name>" | "rocksmith/<name>" | None,
        "tone3000_id": int | None,
        "vst_path": "<abs path>" | None,
        "vst_format": "VST3" | "AU" | None,
        "vst_state": "<base64 blob>" | None,
      }

    Cab branch picks the IR by `level` (a mic-position suffix like
    `5c` / `cc`) when supplied, else the first available IR from the
    mic map, else the first available IR from rs_cab_to_ir.

    Amp / pedal / rack branch tries: gain_variants first (amps),
    then the most-used NAM/VST already assigned to this gear in
    preset_pieces (the library's choice), then default_captures.
    """
    if _config_dir is None:
        return None
    rs_map = _load_rs_to_real() or {}
    info = rs_map.get(rs_gear) or {}
    category = (info.get("category") or "").lower()

    # ── Cab branch: IRs, not NAMs ──────────────────────────────
    if category == "cab" or rs_gear.lower().startswith(("cab_", "bass_cab_")):
        irs_root = _config_dir / "nam_irs"
        mic_map = _load_rs_cab_mic_map().get(rs_gear) or {}
        chosen_file: str | None = None
        # 1. If the caller asked for a specific mic-position level, use it.
        if level and level != "auto" and level in mic_map:
            cand = (mic_map[level] or {}).get("ir_file")
            if cand and (irs_root / cand).exists():
                chosen_file = cand
        # 2. Default mic: prefer "Dynamic Cone" (5c), then any close-mic
        #    variant, then the first available — keeps the swap experience
        #    consistent ("similar mic, different cab").
        if chosen_file is None:
            preferred_order = ["5c", "cc", "tc", "rc", "5e", "ce", "te"]
            for k in preferred_order + sorted(mic_map):
                cand = (mic_map.get(k) or {}).get("ir_file")
                if cand and (irs_root / cand).exists():
                    chosen_file = cand
                    break
        # 3. Fallback to the legacy rs_cab_to_ir list (no labels).
        if chosen_file is None:
            rs_entry = _load_rs_cab_to_ir().get(rs_gear) or {}
            for cand in rs_entry.get("irs") or []:
                if (irs_root / cand).exists():
                    chosen_file = cand
                    break
        if chosen_file:
            return {"kind": "rs_ir", "file": chosen_file,
                    "tone3000_id": None,
                    "vst_path": None, "vst_format": None, "vst_state": None}
        return None

    # ── Amp bundled-VST preference (amps-vst branch) ───────────
    # An amp that ships an installed bundled VST (rs_gear_to_vst.json) plays
    # that VST instead of a NAM capture — the whole point of shipping amp
    # VSTs (e.g. BT880B = GK 800RB -> FreddyKrueger800BR.vst3). Scoped to
    # amps: pedals/racks get their bundled VST via the batch/migration paths
    # and may legitimately resolve to an external plugin below, and only the
    # amps we actually built VSTs for appear in rs_gear_to_vst.json, so other
    # amps fall straight through to their gain_variants/NAM as before.
    if category == "amp":
        amp_vst = _pick_installed_primary_vst(rs_gear, _build_known_vst_lookup())
        if amp_vst and amp_vst.get("vst_path"):
            return {"kind": "vst",
                    "file": None,
                    "tone3000_id": None,
                    "vst_path": amp_vst["vst_path"],
                    "vst_format": amp_vst.get("vst_format") or "VST3",
                    "vst_state": _compute_vst_state_for_piece(
                        rs_gear, amp_vst["vst_path"], None)}

    # ── Amp / pedal / rack branch: NAMs ────────────────────────
    # Step 1 — curated gain_variants (amps with clean/crunch/dist
    # captures). Pedals/racks don't usually have these.
    variants = info.get("gain_variants") or {}
    spec = None
    if level and level != "auto" and variants:
        spec = variants.get(level)
    if spec is None and variants:
        # Auto-pick by rs_gain (or 50.0 fallback in the helper).
        spec = _pick_amp_gain_variant(info, rs_gain if rs_gain is not None else 50.0)
    if spec and isinstance(spec, dict):
        subdir = _category_subdir_for_gear(rs_gear)
        amp_dir = _config_dir / "nam_models" / subdir
        title = (spec.get("notes") or "").strip()
        tone3000_id = spec.get("tone3000_id")
        model_id = spec.get("model_id")
        if title:
            cand = amp_dir / f"{_safe_filename_human(title)}.nam"
            if cand.exists():
                return {"kind": "nam",
                        "file": f"{subdir}/{cand.name}",
                        "tone3000_id": tone3000_id,
                        "vst_path": None, "vst_format": None,
                        "vst_state": None}
        if model_id and tone3000_id:
            legacy = (f"tone3000_{tone3000_id}_m{model_id}_"
                      f"{_safe_filename(rs_gear)}.nam")
            if (amp_dir / legacy).exists():
                return {"kind": "nam",
                        "file": f"{subdir}/{legacy}",
                        "tone3000_id": tone3000_id,
                        "vst_path": None, "vst_format": None,
                        "vst_state": None}
        # Variant resolved but file is missing — keep tone3000_id
        # around for the response, fall through to general lookup.
        fallback_tid = tone3000_id
    else:
        fallback_tid = None

    # Step 2 — most-used existing assignment for this rs_gear in
    # preset_pieces. Covers two cases the picker would otherwise
    # refuse:
    #   (a) Pedals/racks that ship without gain_variants but DO
    #       have a NAM file the auto-download chose.
    #   (b) Gears the user has assigned a VST plugin to (kind='vst'
    #       + vst_path), which is the common case for pedals
    #       (Kilohearts, Valhalla, etc.). When the most-used row
    #       is a VST we return that VST instead.
    conn = _get_conn()
    row = conn.execute(
        "SELECT kind, file, vst_path, vst_format, vst_state, "
        "       tone3000_id, COUNT(*) "
        "FROM preset_pieces "
        "WHERE rs_gear_type = ? "
        "  AND ((kind = 'nam' AND file IS NOT NULL AND file != '') "
        "       OR (kind = 'vst' AND vst_path IS NOT NULL AND vst_path != '')) "
        "GROUP BY kind, file, vst_path "
        "ORDER BY COUNT(*) DESC LIMIT 1",
        (rs_gear,),
    ).fetchone()
    if row:
        kind, file_v, vst_path, vst_format, vst_state, tid, _n = row
        if kind == "vst" and vst_path:
            # Verify the VST bundle is still on disk.
            if Path(vst_path).exists():
                return {"kind": "vst",
                        "file": None,
                        "tone3000_id": None,
                        "vst_path": vst_path,
                        "vst_format": vst_format or "VST3",
                        "vst_state": vst_state}
        elif kind == "nam" and file_v:
            cand_path = _safe_child(_config_dir / "nam_models", file_v)
            if cand_path and cand_path.exists():
                return {"kind": "nam",
                        "file": file_v,
                        "tone3000_id": tid or fallback_tid,
                        "vst_path": None, "vst_format": None,
                        "vst_state": None}

    # Step 3 — default_captures.json pinned tone3000_id + an
    # existing download with that id. Last-ditch for gears whose
    # NAM hasn't been used in any preset_piece yet (could happen
    # right after the user added a song that introduced this gear).
    dflt = (_load_default_captures().get(rs_gear) or {})
    dflt_tid = dflt.get("tone3000_id")
    if dflt_tid:
        row = conn.execute(
            "SELECT file FROM preset_pieces "
            "WHERE tone3000_id = ? AND file IS NOT NULL AND file != '' "
            "ORDER BY id DESC LIMIT 1",
            (int(dflt_tid),),
        ).fetchone()
        if row and row[0]:
            cand_path = _safe_child(_config_dir / "nam_models", row[0])
            if cand_path and cand_path.exists():
                return {"kind": "nam",
                        "file": row[0],
                        "tone3000_id": dflt_tid,
                        "vst_path": None, "vst_format": None,
                        "vst_state": None}

    return None


def _consolidate_gear_assignments(conn, apply: bool = False) -> dict:
    """Collapse per-song gear divergence into ONE global assignment per gear
    — the v1.3.2 "make gear global" consolidation.

    Policy (user-chosen):
      - Cabs are SKIPPED: their mic position stays a per-song override.
      - If a gear has ANY valid VST assignment, every row becomes the most
        RECENT VST (highest preset_pieces.id) — "use the VSTs from the last
        update".
      - Otherwise the gear is NAM: each row is re-pointed to the CURATED
        capture via `_resolve_gear_assignment`. Amps with gain_variants
        resolve per-row by that row's Gain knob (clean/crunch/dist stay
        gain-correct); pedals/racks resolve once and apply to all rows.

    `apply=False` is a dry run: it computes and returns the report without
    writing. `apply=True` performs the UPDATEs, recomputes affected presets'
    primary model/IR, and commits. Returns the same report shape either way.
    The caller is responsible for backing up the DB before an apply.
    """
    rs_map = _load_rs_to_real() or {}

    rows = conn.execute(
        "SELECT id, preset_id, rs_gear_type, kind, file, vst_path, vst_format, "
        "vst_state, params_json FROM preset_pieces"
    ).fetchall()
    bygear: dict[str, list] = {}
    for r in rows:
        bygear.setdefault(r[2], []).append(r)

    plan_by_row: dict[int, tuple] = {}   # row_id -> (kind,file,vst_path,vst_format,vst_state,tone3000_id)
    changes: list[tuple] = []            # (row_id, gear, old_assign, new_assign)
    affected_presets: set[int] = set()
    conflicts: list[dict] = []
    cabs_skipped = 0
    vst_gears = 0

    for gear, grows in bygear.items():
        # Cabs (incl. the catch-all "Cabinets") keep their per-song mic
        # position — _gear_category handles names absent from rs_to_real.
        if _gear_category(gear) == "cab":
            cabs_skipped += 1
            continue
        gear_def = rs_map.get(gear) or {}
        # A gear that's been assigned a VST stays VST globally even if the
        # plugin isn't installed on THIS machine — a load error the user can
        # fix beats silently demoting a pedal/rack to a wrong-sounding NAM.
        vst_rows = [r for r in grows if r[3] == "vst" and r[5]]
        if vst_rows:
            vst_gears += 1
            w = max(vst_rows, key=lambda r: r[0])   # most recent id
            for r in grows:
                plan_by_row[r[0]] = ("vst", None, w[5], w[6] or "VST3", w[7], None)
        elif gear_def.get("gain_variants"):
            for r in grows:
                try:
                    pj = json.loads(r[8] or "{}")
                    knobs = pj.get("knobs", pj) if isinstance(pj, dict) else {}
                except Exception:
                    knobs = {}
                rs_gain = _gear_rs_gain({"knobs": knobs}, gear_def)
                res = _resolve_gear_assignment(gear, "auto", rs_gain)
                if res:
                    plan_by_row[r[0]] = (res["kind"], res.get("file"),
                                         res.get("vst_path"), res.get("vst_format"),
                                         res.get("vst_state"), res.get("tone3000_id"))
        else:
            res = _resolve_gear_assignment(gear, "auto", 50.0)
            if res:
                for r in grows:
                    plan_by_row[r[0]] = (res["kind"], res.get("file"),
                                         res.get("vst_path"), res.get("vst_format"),
                                         res.get("vst_state"), res.get("tone3000_id"))

        gear_changed = 0
        distinct_now = {(r[4] or r[5] or "") for r in grows}
        for r in grows:
            new = plan_by_row.get(r[0])
            if new is None:
                continue
            if (new[0], new[1], new[2]) != (r[3], r[4], r[5]):
                changes.append((r[0], gear, r[4] or r[5] or "∅", new[1] or new[2]))
                affected_presets.add(r[1])
                gear_changed += 1
        if gear_changed and len(distinct_now) > 1:
            conflicts.append({
                "gear": gear,
                "rows": len(grows),
                "changed": gear_changed,
                "before": sorted({(Path(x).name if x else "∅") for x in distinct_now}),
            })

    changed_ids = {c[0] for c in changes}
    if apply and changes:
        with _lock:
            for row_id in changed_ids:
                kind, file, vpath, vfmt, vstate, tid = plan_by_row[row_id]
                mode = "manual_vst" if kind == "vst" else "manual"
                conn.execute(
                    "UPDATE preset_pieces SET kind=?, file=?, vst_path=?, "
                    "vst_format=?, vst_state=?, tone3000_id=?, assigned_mode=? "
                    "WHERE id=?",
                    (kind, file, vpath, vfmt, vstate, tid, mode, row_id),
                )
            for pid in affected_presets:
                _recompute_preset_primaries(conn, pid)
            conn.commit()

    return {
        "applied": bool(apply and changes),
        "gears_total": len(bygear),
        "cabs_skipped": cabs_skipped,
        "vst_gears": vst_gears,
        "gears_changed": len({c[1] for c in changes}),
        "rows_changed": len(changes),
        "presets_affected": len(affected_presets),
        "conflicts": conflicts[:200],
    }

# ── Batch job ───────────────────────────────────────────────────────


def _batch_log(msg: str) -> None:
    with _batch_lock:
        _batch_state["log"].append(f"{time.strftime('%H:%M:%S')} {msg}")
        # Keep log bounded so memory doesn't grow on huge libraries.
        if len(_batch_state["log"]) > 500:
            _batch_state["log"] = _batch_state["log"][-500:]


def _list_library_songs() -> tuple[list[Path], int]:
    """Return (parseable songs, count of cloud-only placeholders).

    Walks the DLC dir RECURSIVELY so libraries organised in subfolders
    (e.g. one folder per artist) get fully covered. Songs are surfaced
    with their basename downstream, which matches feedBack's own
    convention in web_library.db.

    Cloud-loader users have a DLC dir full of 0-byte PSARC stubs —
    metadata exists in feedBack's library DB, but the actual archive
    isn't on disk until the user plays the song (or runs cloud_loader's
    materialize action). Trying to parse those stubs floods the batch
    log with "Not a PSARC file" errors and slows nothing down, so we
    filter them out here and report the count separately so the UI
    can tell the user how many songs they'd need to materialize for
    a full library scan.
    """
    dlc = _get_dlc_dir() if _get_dlc_dir else None
    if not dlc:
        return [], 0
    songs: list[Path] = []
    cloud_only = 0
    # rglob('*') is recursive. Filter to playable formats and skip the
    # 0-byte cloud-loader stubs that would otherwise blow up the parser.
    candidates = []
    try:
        for p in dlc.rglob("*"):
            if not p.is_file():
                continue
            if not _is_song_pack(p):
                continue
            candidates.append(p)
    except OSError:
        return [], 0
    candidates.sort()  # stable ordering for the batch + progress count
    for p in candidates:
        # Keep a 0-byte cloud stub only when the loader has already unpacked
        # the real data into sloppak_cache — the tone reader reads from there,
        # so the batch can map it. Genuinely-unmaterialized stubs are still
        # reported as cloud_only and skipped (parsing them just errors).
        if not _song_data_available(p):
            cloud_only += 1
            continue
        songs.append(p)
    return songs, cloud_only


def _existing_assignment_for_gear(rs_type: str, tone3000_id: int | None = None) -> dict | None:
    """Find a capture already assigned to this the game gear in ANY song's
    preset, so a manual (or earlier auto) choice propagates library-wide
    instead of the batch re-searching tone3000 from scratch.

    When `tone3000_id` is provided, restricts the search to rows that
    were downloaded for that exact tone3000_id — this is how amp gain
    variants stay separated (the "clean" capture for Twin doesn't get
    reused for a tone that wants the "dist" variant).

    Prefers hand-assigned pieces (so a manual choice spreads to the auto/
    untouched songs), then the most recent. Returns a full piece-shaped dict
    only if the capture still exists on disk; otherwise None (so a stale pick
    falls back to a search)."""
    if _config_dir is None:
        return None
    if tone3000_id is not None:
        rows = _get_conn().execute(
            """
            SELECT kind, file, tone3000_id, vst_path, vst_format, vst_state
            FROM preset_pieces
            WHERE rs_gear_type = ? AND tone3000_id = ?
                  AND kind IN ('nam', 'ir', 'rs_ir', 'vst')
                  AND ((file IS NOT NULL AND file != '')
                       OR (vst_path IS NOT NULL AND vst_path != ''))
            ORDER BY (assigned_mode IN ('manual', 'manual_vst')) DESC, id DESC
            """,
            (rs_type, tone3000_id),
        ).fetchall()
    else:
        rows = _get_conn().execute(
            """
            SELECT kind, file, tone3000_id, vst_path, vst_format, vst_state
            FROM preset_pieces
            WHERE rs_gear_type = ? AND kind IN ('nam', 'ir', 'rs_ir', 'vst')
                  AND ((file IS NOT NULL AND file != '')
                       OR (vst_path IS NOT NULL AND vst_path != ''))
            ORDER BY (assigned_mode IN ('manual', 'manual_vst')) DESC, id DESC
            """,
            (rs_type,),
        ).fetchall()
    models_dir = _config_dir / "nam_models"
    irs_dir = _config_dir / "nam_irs"
    for kind, file, tone3000_id, vst_path, vst_format, vst_state in rows:
        if kind == "vst":
            if vst_path and Path(vst_path).exists():
                return {"kind": "vst", "file": None, "tone3000_id": tone3000_id,
                        "vst_path": vst_path, "vst_format": vst_format,
                        "vst_state": vst_state}
            continue
        if not file:
            continue
        ok = (models_dir / file).exists() if kind == "nam" else (irs_dir / file).exists()
        if ok:
            return {"kind": kind, "file": file, "tone3000_id": tone3000_id}
    return None


def _manual_piece_usable(prev: dict) -> bool:
    """True if a hand-assigned piece still points at something on disk, so the
    batch can preserve it verbatim ("manual is sacred") instead of re-resolving."""
    if _config_dir is None:
        return False
    kind = prev.get("kind")
    if kind == "vst":
        return bool(prev.get("vst_path")) and Path(prev["vst_path"]).exists()
    f = prev.get("file")
    if not f:
        return False
    if kind == "nam":
        return (_config_dir / "nam_models" / f).exists()
    if kind in ("ir", "rs_ir"):
        return (_config_dir / "nam_irs" / f).exists()
    return False


def _resolve_song_swap_assignment(to_gear: str, params_json: str | None,
                                  rs_gain: float | None) -> dict | None:
    """Resolve the target gear for a per-song swap.

    Song swaps should mirror what the All Gear catalog says this target gear
    plays today. That means an existing manual/manual_vst assignment wins over
    curated amp gain variants; otherwise swapping to an amp with EN30/TW22 VST
    assigned globally silently falls back to the old NAM variant.
    """
    res = _existing_assignment_for_gear(to_gear)
    if res is None:
        res = _resolve_gear_assignment(to_gear, None, rs_gain)
    if res and res.get("kind") == "vst" and res.get("vst_path") and not res.get("vst_state"):
        res = dict(res)
        res["vst_state"] = _effective_vst_state_for_piece(
            to_gear, str(res["vst_path"]), None, params_json)
    return res


# Global rate-limit gate for tone3000 API calls. Pure tone3000 server-
# side limit is ~100 req/min. A batch "download version" fires TWO API
# calls per capture (list_models + download_model_file), so an earlier
# 0.55s gap (~110 ticks/min → ~220 calls/min) overshot the budget and
# the LAST captures in a big run exhausted their 429 retries and failed
# (e.g. all three GB100 variants at the tail of a ~170-capture batch).
# Target ~70 ticks/min (0.85s gap): a big library still finishes in a
# few minutes, but the combined call rate stays comfortably under the
# limit, and the (now longer) 429-retry-with-backoff in the client is
# only ever the second line of defence. Shared by every parallel worker
# so the combined rate never exceeds the budget.
_t3k_rate_lock = threading.Lock()
_t3k_last_call_at = 0.0
_T3K_MIN_GAP = 0.85


def _t3k_rate_gate():
    """Block until at least _T3K_MIN_GAP seconds have passed since the
    previous gated call across the whole process. Cheap enough to call
    around every outbound API request without measurable overhead."""
    global _t3k_last_call_at
    with _t3k_rate_lock:
        elapsed = time.time() - _t3k_last_call_at
        wait = _T3K_MIN_GAP - elapsed
        if wait > 0:
            time.sleep(wait)
        _t3k_last_call_at = time.time()


def _rename_legacy_filenames_to_readable() -> dict:
    """Rename `tone3000_<tid>_m<mid>_*.nam` files to use the tone3000
    title curated in rs_to_real.json `notes`.

    Idempotent — already-readable filenames don't match the pattern.
    Files without a curated `notes` match are left alone (so this is
    offline-safe; no API calls). When the rename target collides with
    an existing file of the same size we treat it as a duplicate
    (multiple rs_gears legacy-named the same tone3000 model) and
    deduplicate; size mismatches get a `(m<id>)` suffix.
    """
    import re as _re
    if _config_dir is None:
        return {"renamed_count": 0, "skipped_no_title_count": 0,
                "errors": []}
    rs_map = _load_rs_to_real() or {}
    title_index: dict[tuple[int, int], str] = {}
    for _gear, _info in rs_map.items():
        for _level, _spec in (_info.get("gain_variants") or {}).items():
            tid = _spec.get("tone3000_id")
            mid = _spec.get("model_id")
            notes = (_spec.get("notes") or "").strip()
            if tid and mid and notes:
                title_index[(int(tid), int(mid))] = notes

    pattern = _re.compile(r"^tone3000_(\d+)_m(\d+)_.*\.(nam|wav)$")
    conn = _get_conn()
    renamed = []
    skipped_no_title = []
    errors = []
    for root_name in ("nam_models", "nam_irs"):
        root = _config_dir / root_name
        if not root.exists():
            continue
        for f in list(root.rglob("*")):
            if not f.is_file() or f.name.startswith("."):
                continue
            m = pattern.match(f.name)
            if not m:
                continue
            tid, mid, ext = int(m.group(1)), int(m.group(2)), m.group(3)
            title = title_index.get((tid, mid))
            if not title:
                skipped_no_title.append(f.name)
                continue
            new_name = f"{_safe_filename_human(title)}.{ext}"
            new_path = f.parent / new_name
            # DB `file` values are relative to the nam_models/nam_irs root
            # (POSIX separators) — parent.name alone breaks for files at
            # the root or nested more than one level deep.
            old_rel = f.relative_to(root).as_posix()
            new_rel = f.relative_to(root).with_name(new_name).as_posix()
            if new_path.exists() and new_path != f:
                # Same-size collision = duplicate copy of the same
                # NAM (legacy scheme suffixed each download with the
                # rs_gear); dedupe by dropping ours and remapping
                # the DB to the kept file.
                try:
                    same = (f.stat().st_size == new_path.stat().st_size)
                except OSError:
                    same = False
                if same:
                    try:
                        f.unlink()
                    except OSError as e:
                        errors.append(f"{old_rel}: dedup unlink failed: {e}")
                        continue
                    conn.execute(
                        "UPDATE preset_pieces SET file = ? WHERE file = ?",
                        (new_rel, old_rel),
                    )
                    conn.execute(
                        "UPDATE presets SET model_file = ? WHERE model_file = ?",
                        (new_rel, old_rel),
                    )
                    conn.execute(
                        "UPDATE presets SET ir_file = ? WHERE ir_file = ?",
                        (new_rel, old_rel),
                    )
                    conn.commit()
                    renamed.append({"from": old_rel, "to": new_rel,
                                    "deduplicated": True})
                    continue
                new_name = (f"{_safe_filename_human(title)} "
                            f"(m{mid}).{ext}")
                new_path = f.parent / new_name
                new_rel = f.relative_to(root).with_name(new_name).as_posix()
            try:
                f.rename(new_path)
            except OSError as e:
                errors.append(f"{old_rel}: {e}")
                continue
            conn.execute(
                "UPDATE preset_pieces SET file = ? WHERE file = ?",
                (new_rel, old_rel),
            )
            conn.execute(
                "UPDATE presets SET model_file = ? WHERE model_file = ?",
                (new_rel, old_rel),
            )
            conn.execute(
                "UPDATE presets SET ir_file = ? WHERE ir_file = ?",
                (new_rel, old_rel),
            )
            conn.commit()
            renamed.append({"from": old_rel, "to": new_rel})
    return {
        "renamed_count": len(renamed),
        "renamed": renamed[:20],
        "skipped_no_title_count": len(skipped_no_title),
        "errors": errors,
    }


def _wire_cabs_to_presets(replace_auto: bool = False) -> dict:
    """Link the game-extracted IRs to cab preset_pieces rows.

    Companion to `_wire_curated_variants_to_presets` but for cabs.
    The variants flow downloads NAMs from tone3000 and wires them to
    amp pieces; this one wires cabs to their IRs via the bundled
    `rs_cab_to_ir.json` map (IRs resolved under `nam_irs/`).

    For each distinct cab `rs_gear_type` in preset_pieces:
      1. Look up `rs_cab_to_ir.json` — both an exact match on the
         gear name AND a prefix match (the JSON keys often carry a
         mic-position suffix like `_5c`, `_5e`, while the DB stores
         the bare gear name).
      2. Pick the first IR file that actually exists on disk.
      3. Bulk UPDATE every cab row of that gear, setting kind='rs_ir'
         and the IR path. Manual rows always preserved.

    `replace_auto` (default False): when False, only fills cab rows
    that are currently empty (file IS NULL). When True, ALSO replaces
    auto-assigned tone3000 IRs/NAMs — useful when the intent is
    clearly "use the game cab IRs now". Manual overrides are
    untouched in both modes.

    Cabs not present in the extracted-IR map (Eden, some Orange
    bass cabs) are left as Pending — they need a tone3000 search
    via Remap all, since we don't ship those IRs.
    """
    if _config_dir is None:
        return {"wired": 0, "skipped_no_ir": [], "errors": []}
    conn = _get_conn()
    rs_cab_map = _load_rs_cab_to_ir() or {}
    irs_root = _config_dir / "nam_irs"
    summary = {"wired": 0, "skipped_no_ir": [], "errors": [], "replace_auto": replace_auto}

    # Distinct cab gears we could wire. In default mode only those with
    # at least one empty row; in replace_auto mode all cabs (any row that
    # isn't a manual override is fair game).
    if replace_auto:
        cab_gears_q = (
            "SELECT DISTINCT rs_gear_type FROM preset_pieces "
            "WHERE slot = 'cabinet' "
            "  AND (assigned_mode IS NULL OR assigned_mode NOT IN ('manual','manual_vst'))"
        )
    else:
        cab_gears_q = (
            "SELECT DISTINCT rs_gear_type FROM preset_pieces "
            "WHERE slot = 'cabinet' AND (file IS NULL OR file = '')"
        )
    cab_gears = [r[0] for r in conn.execute(cab_gears_q).fetchall()]

    # Build a case-insensitive lookup index over rs_cab_to_ir.json.
    # The JSON keys use mixed case (`Cab_Marshall1960a`, `Cab_OrangePPC212OB`)
    # but the preset_pieces.rs_gear_type column upper-cases the model
    # suffix (`Cab_MARSHALL1960A`, `Cab_ORANGEPPC212OB`). Without this
    # normalisation half the Marshall + Orange cabs miss their IR.
    rs_cab_lower = {k.lower(): k for k in rs_cab_map}

    for gear in cab_gears:
        gear_lc = gear.lower()
        # Find every JSON key that matches our gear case-insensitively
        # — exact match OR `gear_*` (mic-position variants like _5c).
        keys = [rs_cab_lower[lc] for lc in rs_cab_lower
                if lc == gear_lc or lc.startswith(gear_lc + "_")]
        if not keys:
            summary["skipped_no_ir"].append(gear)
            continue
        # Dedupe IR paths across the matched keys (variants point to
        # the same physical files most of the time).
        candidate_irs = []
        seen = set()
        for k in keys:
            for ir in (rs_cab_map[k].get("irs") or []):
                if ir not in seen:
                    seen.add(ir)
                    candidate_irs.append(ir)
        # First IR that actually exists wins. Not every variant is
        # necessarily present on this user's machine.
        chosen = next((ir for ir in candidate_irs if (irs_root / ir).exists()),
                       None)
        if not chosen:
            summary["skipped_no_ir"].append(gear)
            continue
        try:
            if replace_auto:
                # Re-point every auto cab row at the game cab
                # IR — including rows that already had a tone3000 IR
                # or auto NAM assigned.
                cur = conn.execute(
                    """
                    UPDATE preset_pieces
                    SET file = ?, kind = 'rs_ir', assigned_mode = 'auto'
                    WHERE slot = 'cabinet'
                      AND rs_gear_type = ?
                      AND (assigned_mode IS NULL OR assigned_mode NOT IN ('manual', 'manual_vst'))
                    """,
                    (chosen, gear),
                )
            else:
                cur = conn.execute(
                    """
                    UPDATE preset_pieces
                    SET file = ?, kind = 'rs_ir', assigned_mode = 'auto'
                    WHERE slot = 'cabinet'
                      AND rs_gear_type = ?
                      AND (file IS NULL OR file = '')
                      AND (assigned_mode IS NULL OR assigned_mode NOT IN ('manual', 'manual_vst'))
                    """,
                    (chosen, gear),
                )
            summary["wired"] += cur.rowcount
        except Exception as e:
            summary["errors"].append(f"{gear}: {e}")
    # Refresh presets.ir_file for cabs that just got assigned, so the
    # bundle's single-IR runtime sees them. Same pattern as the amp
    # backfill in `_wire_curated_variants_to_presets`.
    try:
        conn.execute(
            """
            UPDATE presets
            SET ir_file = (
                SELECT pp.file
                FROM preset_pieces pp
                WHERE pp.preset_id = presets.id
                  AND pp.slot = 'cabinet'
                  AND pp.kind IN ('ir', 'rs_ir')
                  AND pp.file IS NOT NULL AND pp.file != ''
                  AND pp.bypassed = 0
                ORDER BY pp.slot_order
                LIMIT 1
            )
            WHERE (ir_file IS NULL OR ir_file = '')
              AND EXISTS (
                SELECT 1 FROM preset_pieces pp2
                WHERE pp2.preset_id = presets.id
                  AND pp2.slot = 'cabinet'
                  AND pp2.kind IN ('ir', 'rs_ir')
                  AND pp2.file IS NOT NULL
              )
            """
        )
    except Exception:
        log.exception("backfill presets.ir_file failed")
    conn.commit()
    return summary


def _wire_curated_variants_to_presets() -> dict:
    """Link freshly-downloaded curated NAM files back to the existing
    preset_pieces rows that need them.

    Background: the preload writes files to disk but doesn't touch
    `preset_pieces` — that table is normally rewritten by the batch
    worker as it iterates songs. After a Purge → Preload sequence the
    files exist but every `preset_pieces.file` is still NULL, so the
    inventory shows them all as "orphan" and playback would fall back
    to the no-NAM path.

    This sweep is the fast bridge: for each curated gear+variant we
    find every `preset_pieces` row that
        - shares the rs_gear_type,
        - has no file assigned (or no manual override),
        - has a Gain knob inside this variant's rs_gain_range,
    and points it at the downloaded file. Single SQL UPDATE per
    (gear, variant) — ~180 statements total, all under a second on a
    library with thousands of rows.

    Never overwrites a manual assignment (`assigned_mode` IN
    ('manual', 'manual_vst')) — that's the user's choice, not ours.
    """
    if _config_dir is None:
        return {"wired": 0, "skipped_no_file": 0, "errors": []}
    rs_map = _load_rs_to_real() or {}
    conn = _get_conn()
    wired = 0
    skipped = 0
    errors = []
    nam_root = _config_dir / "nam_models"
    for rs_gear, info in rs_map.items():
        if not isinstance(info, dict) or info.get("category") != "amp":
            continue
        for level, spec in (info.get("gain_variants") or {}).items():
            if not isinstance(spec, dict):
                continue
            tone3000_id = spec.get("tone3000_id")
            model_id = spec.get("model_id")
            rng = spec.get("rs_gain_range") or [0.0, 100.0]
            if not tone3000_id:
                continue
            # Find the file on disk. Prefer the new readable name
            # (from `notes`), fall back to the legacy cryptic naming.
            subdir = _category_subdir_for_gear(rs_gear)
            amp_dir = nam_root / subdir
            title = (spec.get("notes") or "").strip()
            rel_path = None
            if title:
                candidate = amp_dir / f"{_safe_filename_human(title)}.nam"
                if candidate.exists():
                    rel_path = f"{subdir}/{candidate.name}"
            if rel_path is None and model_id:
                legacy_name = (f"tone3000_{tone3000_id}_m{model_id}_"
                               f"{_safe_filename(rs_gear)}.nam")
                if (amp_dir / legacy_name).exists():
                    rel_path = f"{subdir}/{legacy_name}"
            if rel_path is None:
                # Variant wasn't downloaded successfully — leave the
                # preset_pieces row as Pending; the next preload run
                # or batch will pick it up.
                skipped += 1
                continue
            lo, hi = float(rng[0]), float(rng[1])
            # Build the gain expression. Default is `$.Gain`; if the
            # gear declares `gain_proxy_knobs`, average those instead
            # (Plexi 1959: Loudness1+Loudness2). Same data the Python
            # _gear_rs_gain helper reads, expressed in SQL so we can
            # filter rows server-side without a Python round-trip.
            proxies = info.get("gain_proxy_knobs") or []
            if proxies:
                terms = [f"CAST(json_extract(params_json, '$.{k}') AS REAL)"
                         for k in proxies]
                gain_expr = "(" + " + ".join(terms) + f") / {float(len(proxies))}"
            else:
                gain_expr = "CAST(json_extract(params_json, '$.Gain') AS REAL)"
            # Matching strategies (OR-combined):
            #   1. Gain expr present and inside the variant's range.
            #   2. Gain expr NULL (knobs missing entirely) and the
            #      variant's range covers 50 → "crunch" fallback.
            try:
                cur = conn.cursor()
                cur.execute(
                    f"""
                    UPDATE preset_pieces
                    SET file = ?, kind = 'nam', tone3000_id = ?,
                        assigned_mode = 'auto'
                    WHERE rs_gear_type = ?
                      AND (file IS NULL OR file = '')
                      AND (assigned_mode IS NULL OR assigned_mode NOT IN ('manual', 'manual_vst'))
                      AND (
                            (json_valid(params_json)
                             AND {gain_expr} >= ?
                             AND {gain_expr} <= ?)
                            OR (json_valid(params_json)
                                AND {gain_expr} IS NULL
                                AND ? <= 50.0 AND 50.0 <= ?)
                      )
                    """,
                    (rel_path, tone3000_id, rs_gear, lo, hi, lo, hi),
                )
                wired += cur.rowcount
            except Exception as e:
                errors.append(f"{rs_gear}/{level}: {e}")
    # After updating preset_pieces, refresh the legacy single-NAM
    # primary on the parent presets so playback through the bundle's
    # 2-stage path works too. This walks every preset whose
    # `model_file` is empty and tries to backfill it from the most
    # recent NAM piece — cheap because the WHERE narrows fast.
    try:
        conn.execute(
            """
            UPDATE presets
            SET model_file = (
                SELECT pp.file
                FROM preset_pieces pp
                WHERE pp.preset_id = presets.id
                  AND pp.kind = 'nam'
                  AND pp.file IS NOT NULL AND pp.file != ''
                  AND pp.slot IN ('amp', 'rack')
                  AND pp.bypassed = 0
                ORDER BY CASE pp.slot WHEN 'amp' THEN 0 ELSE 1 END,
                         pp.slot_order
                LIMIT 1
            )
            WHERE (model_file IS NULL OR model_file = '')
              AND EXISTS (
                SELECT 1 FROM preset_pieces pp2
                WHERE pp2.preset_id = presets.id
                  AND pp2.kind = 'nam'
                  AND pp2.file IS NOT NULL
              )
            """
        )
    except Exception:
        log.exception("backfill presets.model_file failed")
    conn.commit()
    return {"wired": wired, "skipped_no_file": skipped, "errors": errors}


def _preload_worker(jobs):
    """Background thread: download every job in `jobs` with parallel
    workers gated by `_t3k_rate_gate`.

    Each job is `(rs_gear, level, spec)` where `spec` comes from the
    `gain_variants` block in rs_to_real.json. The worker:

      1. Skips jobs whose target file is already on disk (cheap stat
         check — no API call).
      2. Calls `_download_candidate` for the rest, which itself calls
         `list_models` + `download_model_file` (each gated).
      3. Updates `_preload_state` so the UI's polling loop can show
         live progress.

    Uses `ThreadPoolExecutor(max_workers=3)` so multiple downloads can
    overlap the latency of the previous one, even though the rate gate
    serialises the actual HTTP calls. Net effect: the bytes-transfer
    portion of each download (typically ~100–500 KB at broadband
    speed) happens in parallel with the next gate wait, shaving 30–40%
    off the wall-clock vs strictly serial.
    """
    from concurrent.futures import ThreadPoolExecutor

    settings = _load_settings()

    def _one(job):
        rs_gear, level, spec = job
        tone3000_id = spec.get("tone3000_id")
        model_id = spec.get("model_id")
        title = (spec.get("notes") or "").strip()
        label = title or f"{rs_gear} / {level}"
        # Publish what's about to happen so the UI's "Downloading X" hint
        # is up-to-date even before the network round-trip completes.
        with _preload_lock:
            _preload_state["current"] = label

        # Pre-check both naming schemes (readable + legacy).
        subdir = _category_subdir_for_gear(rs_gear)
        amp_dir = _config_dir / "nam_models" / subdir
        pre_existed = False
        if title:
            pretty = amp_dir / f"{_safe_filename_human(title)}.nam"
            if pretty.exists():
                pre_existed = True
        if not pre_existed and model_id:
            legacy = amp_dir / (
                f"tone3000_{tone3000_id}_m{model_id}_"
                f"{_safe_filename(rs_gear)}.nam")
            if legacy.exists():
                pre_existed = True
        if pre_existed:
            with _preload_lock:
                _preload_state["already_present"] += 1
                _preload_state["done"] += 1
            return

        # Skip captures we've already confirmed are gone from tone3000
        # (permanent 404) earlier in this process. Re-attempting them
        # just burns an API round-trip to fail identically, so report
        # the known reason and move on — this is what stops the user
        # from having to re-click the download hoping the count drops.
        with _t3k_gone_lock:
            already_gone = (tone3000_id, model_id) in _t3k_gone
        if already_gone:
            with _preload_lock:
                _preload_state["failed"].append(
                    f"{rs_gear}/{level} — removed from tone3000 (404) "
                    f"(tone={tone3000_id}, model={model_id})"
                )
                _preload_state["failed_permanent"] += 1
                _preload_state["done"] += 1
            return

        # Gate the network call. The retry-on-429 inside
        # download_model_file (and now list_models) is the second line
        # of defence; if the gate is correctly sized we never trigger it.
        _t3k_rate_gate()
        try:
            res = _download_candidate(
                tone3000_id=int(tone3000_id),
                is_ir=False,
                rs_gear=rs_gear,
                settings=settings,
                model_id_override=model_id,
            )
        except Exception as e:
            with _preload_lock:
                _preload_state["errors"].append(
                    f"{rs_gear}/{level}: {e}"
                )
                _preload_state["done"] += 1
            return
        with _preload_lock:
            if res:
                _preload_state["downloaded"] += 1
            else:
                # _download_candidate swallows failures to None but records
                # why on a thread-local; surface it so the user can tell a
                # dead capture (won't fix on re-run) from a transient error.
                reason = getattr(_t3k_dl_error, "reason", None) or "unavailable"
                permanent = _dl_error_is_permanent(reason)
                _preload_state["failed"].append(
                    f"{rs_gear}/{level} — {reason} "
                    f"(tone={tone3000_id}, model={model_id})"
                )
                if permanent:
                    _preload_state["failed_permanent"] += 1
            _preload_state["done"] += 1
        if not res and _dl_error_is_permanent(
                getattr(_t3k_dl_error, "reason", None)):
            with _t3k_gone_lock:
                _t3k_gone.add((tone3000_id, model_id))

    # Phase 1: rename legacy cryptic files to readable names BEFORE
    # downloading. This way the skip-pre-existing pre-check inside
    # `_one()` finds the readable filename (matching what new downloads
    # would land at) and correctly skips already-cached entries that
    # were named the legacy way. Without this rename-first step the
    # pre-check misses them and we'd re-download files we already
    # have, just with a different name.
    try:
        with _preload_lock:
            _preload_state["current"] = "(renaming legacy filenames…)"
        _rename_legacy_filenames_to_readable()
    except Exception:
        log.exception("legacy rename before preload failed")

    try:
        with ThreadPoolExecutor(max_workers=3) as ex:
            list(ex.map(_one, jobs))
        # Phase 3: link the freshly-cached files into the presets
        # that need them so the Manage tab stops showing everything
        # as "orphan" and playback finds the NAMs. Done in the worker
        # (not the request handler) so the SQL sweep doesn't extend
        # the user-facing API call.
        try:
            with _preload_lock:
                _preload_state["current"] = "(wiring files to presets…)"
            wire_result = _wire_curated_variants_to_presets()
            # Also link the game-extracted cab IRs to their cabinet
            # pieces. Purge + preload sequence leaves cab rows with
            # file=NULL just like amps; this re-attaches them without
            # the user having to run a full Remap all afterwards.
            cab_wire = _wire_cabs_to_presets()
            with _preload_lock:
                _preload_state["wired"] = (wire_result.get("wired", 0)
                                            + cab_wire.get("wired", 0))
                if wire_result.get("errors"):
                    _preload_state["errors"].extend(wire_result["errors"])
                if cab_wire.get("errors"):
                    _preload_state["errors"].extend(cab_wire["errors"])
        except Exception:
            log.exception("wire-up after preload failed")
    except Exception:
        log.exception("preload worker crashed")
    finally:
        with _preload_lock:
            _preload_state["running"] = False
            _preload_state["finished_at"] = time.time()
            _preload_state["current"] = ""


# Fallback switch-points (game Gain 0..100) when rs_to_real.json doesn't ship an
# explicit rs_gain_range for a level. Even thirds: clean / crunch / dist.
_DEFAULT_GAIN_RANGES = {
    "clean":  [0, 33],
    "crunch": [34, 66],
    "dist":   [67, 100],
}


def _download_tone3000_gears_worker(amps):
    """Download each amp's clean/crunch/dist captures and bundle them into ONE
    new custom gear per amp — auto-switched by the song's mapped gain at
    playback. Does NOT wire to songs or replace existing gear; it only builds
    your library (the new gears start unassigned). Reuses `_preload_state` so the
    existing progress UI shows the download live.

    `amps` is a list of `(rs_gear, info, specs)` where `specs` is
    `{level: gain_variant_spec}` filtered to entries carrying a tone3000_id.
    """
    settings = _load_settings()
    try:
        items = _load_custom_gear()
        by_id = {g.get("rs_gear"): g for g in items}
        created = 0
        for rs_gear, info, specs in amps:
            variants_out: dict[str, dict] = {}
            for level, spec in specs.items():
                with _preload_lock:
                    _preload_state["current"] = f"{_gear_display_name(rs_gear, rs_gear)} / {level}"
                _t3k_rate_gate()
                try:
                    res = _download_candidate(
                        tone3000_id=int(spec["tone3000_id"]), is_ir=False,
                        rs_gear=rs_gear, settings=settings,
                        model_id_override=spec.get("model_id"))
                except Exception as e:      # noqa: BLE001
                    res = None
                    with _preload_lock:
                        _preload_state["errors"].append(f"{rs_gear}/{level}: {e}")
                with _preload_lock:
                    if res:
                        _preload_state["downloaded"] += 1
                    else:
                        # Surface the real reason (rate-limited / 404 / no
                        # capture) instead of a flat "unavailable" so a
                        # transient rate-limit is distinguishable from a
                        # capture that's genuinely gone.
                        reason = getattr(_t3k_dl_error, "reason", None) or "unavailable"
                        _preload_state["failed"].append(f"{rs_gear}/{level} — {reason}")
                    _preload_state["done"] += 1
                if res:
                    _kind, _file = res
                    rng = spec.get("rs_gain_range")
                    if not (isinstance(rng, (list, tuple)) and len(rng) == 2):
                        rng = _DEFAULT_GAIN_RANGES.get(level)
                    variants_out[level] = {
                        "file": _file,
                        "rs_gain_range": [float(rng[0]), float(rng[1])] if rng else None,
                        "notes": (spec.get("notes") or "").strip() or None,
                    }
            if not variants_out:
                continue
            gear_id = "custom_auto_" + _safe_filename(rs_gear)
            is_bass = rs_gear.startswith("Bass_") or rs_gear.startswith("DI_Amp")
            default_file = (variants_out.get("clean") or next(iter(variants_out.values()))).get("file")
            rec = {
                "rs_gear": gear_id,
                "category": "amp",
                "real_name": _gear_display_name(rs_gear, info.get("name") or rs_gear),
                "instrument": "bass" if is_bass else "guitar",
                "kind": "nam",
                "vst_path": None, "vst_format": None, "vst_state": None,
                "file": default_file,
                "gain_variants": variants_out,
                "auto_tone3000": True,
                "source_rs_gear": rs_gear,
                # Preserve a face the user may have already customized on a re-run.
                "ui": (by_id.get(gear_id) or {}).get("ui"),
                "custom": True,
            }
            if gear_id in by_id:
                by_id[gear_id].update(rec)
            else:
                items.append(rec)
                by_id[gear_id] = rec
            created += 1
        try:
            _save_custom_gear(items)
        except Exception:
            log.exception("saving auto tone3000 gears failed")
        with _preload_lock:
            _preload_state["gears_created"] = created
    except Exception:
        log.exception("download tone3000 gears worker crashed")
    finally:
        with _preload_lock:
            _preload_state["running"] = False
            _preload_state["finished_at"] = time.time()
            _preload_state["current"] = ""


def _redirect_amps_to_auto_gears() -> int:
    """Point every game amp slot that has a matching auto-downloaded tone3000
    gear (custom_auto_<rs_gear>) at that gear via the gear_map redirect. This is
    how 'Rescan all' moves songs onto the new tone3000 gears — the native VSTs
    stay in place but unused (so they read 'unassigned' while the tone3000 gear
    reads 'assigned'). Idempotent; reversible via the per-gear Reset. Returns the
    number of slots newly redirected."""
    autos = {c.get("source_rs_gear"): c.get("rs_gear")
             for c in _load_custom_gear()
             if c.get("auto_tone3000") and c.get("source_rs_gear") and c.get("rs_gear")}
    if not autos:
        return 0
    gmap = _load_gear_map()
    n = 0
    for rs_gear, auto_id in autos.items():
        cur = gmap.get(rs_gear)
        if isinstance(cur, dict) and cur.get("custom") == auto_id:
            continue
        gmap[rs_gear] = {"custom": auto_id, "params": {}}
        n += 1
    if n:
        _save_gear_map(gmap)
    return n


def _clear_all_gear_redirects() -> int:
    """Factory reset: drop EVERY gear_map redirect so each gear plays its own
    native VST again (the default plugin state). Custom gears stay in the library
    but are no longer mapped onto any song slot. Returns how many were cleared."""
    gmap = _load_gear_map()
    n = len(gmap)
    if n:
        _save_gear_map({})
    return n


def _reassign_bundled_vsts_factory() -> int:
    """Factory helper: fill in every UNMAPPED preset piece whose gear ships a
    BUNDLED primary VST with that plugin, so no 'original VST gear' is left
    dangling after a factory reset (e.g. a prior redirect / NAM fallback / purge).
    Only touches rows the normal resolver left as kind='none'; correctly-mapped
    rows (and their song-aware VST state) are untouched. Returns rows updated."""
    known = _build_known_vst_lookup()
    conn = _get_conn()
    rows = conn.execute(
        "SELECT id, rs_gear_type, params_json FROM preset_pieces "
        "WHERE kind IS NULL OR kind='none' OR kind=''"
    ).fetchall()
    updated = 0
    pick_cache: dict = {}
    for pid, rs_gear, params_json in rows:
        if not rs_gear or _is_custom_gear(rs_gear):
            continue
        if rs_gear not in pick_cache:
            p = _pick_installed_primary_vst(rs_gear, known)
            # Only BUNDLED plugins (shipped with rig_builder) — the "original"
            # VST gears we always want restored.
            pick_cache[rs_gear] = p if (p and p.get("vst_path")
                and str(p["vst_path"]).startswith(str(_plugin_dir))) else None
        pick = pick_cache[rs_gear]
        if not pick:
            continue
        try:
            knobs = json.loads(params_json or "{}") or {}
        except (ValueError, TypeError):
            knobs = {}
        vst_state = _compute_vst_state_for_piece(rs_gear, pick["vst_path"], knobs)
        conn.execute(
            "UPDATE preset_pieces SET kind='vst', vst_path=?, vst_format=?, "
            "vst_state=?, file=NULL, tone3000_id=NULL, assigned_mode='auto' "
            "WHERE id=?",
            (pick["vst_path"], pick.get("vst_format") or "VST3", vst_state, pid),
        )
        updated += 1
    conn.commit()
    return updated


def _batch_worker(mode: str = "all", categories=None):
    """Library-wide auto-assign: unique gear → tone3000 candidate (if
    API access) → recorded as 'pending' otherwise.

    `categories`: optional set/list of {amp, pedal, rack, cab} — when given, only
    pieces of those categories are (re)mapped; the rest are left untouched. None
    or empty means all categories.

    For each gear the capture is resolved in this order: (1) reuse a capture
    already assigned to that gear in ANY song — so a manual choice in one song
    propagates to every other song using it; (2) a bundled default capture;
    (3) a fresh tone3000 search. Per-tone bypass and the chosen cab-IR variant
    are always preserved across a re-run.

    `mode`:
    - "all" — (re)map every tone. Reuses existing per-gear assignments first,
      so manual picks survive and spread; only genuinely-unassigned gear hits
      tone3000.
    - "new" — only map tones that have NO preset yet; already-mapped tones
      are left completely untouched. New songs inherit the captures you've
      already assigned to the same gear elsewhere.
    - "factory" — like "all" but IGNORES assigned_mode: even manual swaps are
      re-resolved to the default mapping (bundled VST / curated capture / IR).
      Backs nothing up by itself — it's the "Reset to factory" button. Per-tone
      bypass and the chosen cab-IR variant are still preserved.
    """
    global _batch_disk_bytes
    cat_filter = {str(c).lower() for c in (categories or [])}
    try:
        _batch_disk_bytes = 0
        with _batch_lock:
            _batch_state["started_at"] = time.time()
            _batch_state["log"] = []
        if cat_filter:
            _batch_log(f"Mapping only: {', '.join(sorted(cat_filter))}")

        songs, cloud_only = _list_library_songs()
        with _batch_lock:
            _batch_state["total"] = len(songs)
            _batch_state["progress"] = 0
            _batch_state["assigned"] = 0
            _batch_state["skipped"] = 0
            _batch_state["cloud_only"] = cloud_only
            _batch_state["pending"] = []

        _batch_log(f"Found {len(songs)} materialized songs (parseable)")
        if cloud_only:
            _batch_log(
                f"Skipping {cloud_only} cloud-only placeholders (0-byte stubs). "
                "Play them through feedBack or use cloud_loader → materialize "
                "to pull them onto disk first."
            )
        rs_map = _load_rs_to_real()
        if not rs_map:
            _batch_log("rs_to_real.json missing — bundled gear map not found")
            return

        client = _get_t3k_client()
        settings = _load_settings()
        rs_irs_map = _load_rs_cab_to_ir()
        # Read the installed-VSTs cache ONCE per batch run. Drives the new
        # "promote to primary VST" step below so a fresh install lights up
        # every pedal/comp/EQ/mod with its bundled free VST primary instead
        # of falling through to a tone3000 NAM search. Empty when the user
        # hasn't run Settings → Scan for plugins yet (degrades cleanly to
        # the prior NAM-only resolution).
        known_vst_lookup = _build_known_vst_lookup()
        if known_vst_lookup:
            _batch_log(f"Known VSTs: {sum(len(v) for v in known_vst_lookup.values())} entries "
                       f"across {len(known_vst_lookup)} plugin names")
        else:
            _batch_log("No installed VSTs cached — gears with a VST primary will fall back to NAM "
                       "(run Settings → Scan for plugins first to enable VST auto-assign)")
        seen_gears: dict[str, dict] = {}  # rs_gear_type → top tone3000 candidate or {}

        for idx, song_path in enumerate(songs):
            with _batch_lock:
                _batch_state["progress"] = idx + 1
            filename = _db_song_key(song_path.name, song_path)

            if not _is_song_pack(song_path):
                # Song-pack only: skip raw .psarc songs (convert them first).
                _batch_log(f"skip {filename}: not a .sloppak/.feedpak")
                with _batch_lock:
                    _batch_state["skipped"] += 1
                continue
            try:
                tones = _read_tones_from_sloppak(filename, _get_dlc_dir() or song_path.parent)
            except Exception as e:
                _batch_log(f"skip {filename}: {type(e).__name__}: {e}")
                with _batch_lock:
                    _batch_state["skipped"] += 1
                continue

            for tone in tones:
                parsed = _parse_tone(tone)
                tone_key = parsed["key"]
                preset_name = f"{filename}::{tone_key}" if tone_key else f"{filename}::{parsed['name']}"
                pieces: list[dict] = []

                # Preserve the user's per-tone tweaks across a re-run: load
                # this tone's existing preset pieces (by gear) so we can keep
                # the saved bypass state and the chosen cab-IR variant rather
                # than resetting them. (Files are still re-resolved.)
                existing_by_gear: dict[str, dict] = {}
                _ekey = tone_key or parsed["name"]
                _filename_filter, _filename_args = _tone_mapping_filename_filter(filename, song_path)
                _erow = _get_conn().execute(
                    f"SELECT preset_id FROM tone_mappings WHERE {_filename_filter} AND tone_key = ? "
                    "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)",
                    (*_filename_args, _ekey),
                ).fetchone()
                if _erow:
                    if mode == "new":
                        # "Map new songs only": leave already-mapped tones
                        # completely untouched.
                        continue
                    for _pr in _get_conn().execute(
                        "SELECT rs_gear_type, bypassed, kind, file, assigned_mode, "
                        "tone3000_id, params_json, vst_path, vst_format, vst_state "
                        "FROM preset_pieces WHERE preset_id = ?",
                        (_erow[0],),
                    ).fetchall():
                        existing_by_gear[_pr[0]] = {
                            "bypassed": bool(_pr[1]), "kind": _pr[2], "file": _pr[3],
                            "assigned_mode": _pr[4], "tone3000_id": _pr[5],
                            "params_json": _pr[6], "vst_path": _pr[7],
                            "vst_format": _pr[8], "vst_state": _pr[9],
                        }

                for piece in parsed["chain"]:
                    rs_type = piece["type"]
                    info = rs_map.get(rs_type) or {}
                    category = info.get("category") or _guess_category_from_slot(piece["slot"])
                    # Category-scoped map: skip pieces the user didn't select.
                    if cat_filter and category not in cat_filter:
                        continue
                    gears = info.get("tone3000_gears") or ""
                    query = info.get("tone3000_query") or rs_type
                    platform = _PLATFORM_FOR_CATEGORY.get(category, "nam")

                    # Amp gain variant pick: see _auto_download_for_song
                    # for the rationale. Same per-gear-and-variant cache
                    # key so the batch downloads each variant only once
                    # across the entire library, not once per tone that
                    # uses it.
                    amp_variant = None
                    if category == "amp":
                        amp_variant = _pick_amp_gain_variant(info, _gear_rs_gain(piece, info))
                    cache_key = (rs_type, amp_variant["tone3000_id"]) if amp_variant else rs_type

                    # 0. Manual is sacred. If the user hand-assigned this gear
                    #    in THIS tone, keep it exactly as-is — the batch never
                    #    overwrites a per-song manual choice. (Only reachable in
                    #    "all" mode; "new" skips already-mapped tones entirely.)
                    #    EXCEPTION: "factory" mode ignores assigned_mode entirely
                    #    and re-resolves every piece to its default mapping (the
                    #    "Reset to factory" button) — manual swaps are discarded.
                    _prev_piece = existing_by_gear.get(rs_type, {})
                    if (mode != "factory"
                            and _prev_piece.get("assigned_mode") in ("manual", "manual_vst")
                            and _manual_piece_usable(_prev_piece)):
                        try:
                            _kept_params = json.loads(_prev_piece.get("params_json") or "{}")
                        except (ValueError, TypeError):
                            _kept_params = piece["knobs"]
                        pieces.append({
                            "slot": piece["slot"],
                            "rs_gear_type": rs_type,
                            "kind": _prev_piece.get("kind") or "none",
                            "file": _prev_piece.get("file"),
                            "params": _kept_params,
                            "tone3000_id": _prev_piece.get("tone3000_id"),
                            "assigned_mode": _prev_piece.get("assigned_mode"),
                            "bypassed": _prev_piece.get("bypassed", False),
                            "vst_path": _prev_piece.get("vst_path"),
                            "vst_format": _prev_piece.get("vst_format"),
                            "vst_state": _prev_piece.get("vst_state"),
                        })
                        continue

                    # 0.5. Promote to primary VST when one is installed.
                    # rs_gear_to_vst.json holds a curator-ranked list of
                    # VST/AU recommendations per gear (first = primary).
                    # If the user has installed the primary's plugin
                    # (Kilohearts Essentials, Melda MFreeFXBundle, etc.)
                    # and ran Settings → Scan for plugins to populate
                    # the known_vsts cache, we assign it here instead of
                    # falling through to a tone3000 NAM search. We also
                    # compute the same `vst_state` envelope that
                    # apply_vst_state.py would generate from the RS knobs
                    # — without that step the plugin would load at
                    # defaults regardless of how the song's tone is
                    # actually configured. Cabs use IRs; amps normally use
                    # NAM captures unless a bundled primary VST exists for the
                    # exact amp (e.g. Amp_EN30 -> EN30).
                    if category != "cab":
                        _vst_pick = _pick_installed_primary_vst(rs_type, known_vst_lookup)
                        if _vst_pick:
                            _vst_state = _compute_vst_state_for_piece(
                                rs_type, _vst_pick["vst_path"], piece["knobs"]
                            )
                            pieces.append({
                                "slot": piece["slot"],
                                "rs_gear_type": rs_type,
                                "kind": "vst",
                                "file": None,
                                "params": piece["knobs"],
                                "tone3000_id": None,
                                "assigned_mode": "auto",
                                "bypassed": existing_by_gear.get(rs_type, {}).get("bypassed", False),
                                "vst_path": _vst_pick["vst_path"],
                                "vst_format": _vst_pick["vst_format"],
                                "vst_state": _vst_state,
                            })
                            continue

                    # Cabs first: if we have a game IR for
                    # this gear AND it's actually on disk, assign it.
                    # Saves a tone3000 round-trip and gives the user
                    # the game's own sound out of the box. Skip if the
                    # .wav file doesn't exist locally — the mapping
                    # ships with the plugin but the actual IR assets
                    # don't, so a fresh install without the game
                    # falls through to the deep-link path below.
                    rs_ir_entry = rs_irs_map.get(rs_type) or {}
                    rs_ir_files = rs_ir_entry.get("irs") or []
                    irs_root = _config_dir / "nam_irs"
                    available = [f for f in rs_ir_files if (irs_root / f).exists()]
                    if category == "cab" and available:
                        _prev = existing_by_gear.get(rs_type, {})
                        # IR pick order:
                        #   1. Keep the user's saved variant if it's still
                        #      on disk (their explicit mic-position choice).
                        #   2. Use the song's Cabinet.Key to pick the
                        #      mic-specific IR via rs_cab_mic_map — this is
                        #      what the game's tone designer originally
                        #      specified. Previously this branch defaulted
                        #      to `available[0]` (always _00.wav) and
                        #      ignored Cabinet.Key, so every cab played
                        #      with the SM57-condenser-close mic regardless
                        #      of the song's intent.
                        #   3. Fall back to the first available IR.
                        _cab_file = None
                        if _prev.get("kind") == "rs_ir" and _prev.get("file") in available:
                            _cab_file = _prev.get("file")
                        if _cab_file is None:
                            cabinet_key = piece.get("cabinet_key") or ""
                            if cabinet_key:
                                suffix = _cab_suffix_from_effect_name(cabinet_key)
                                if suffix:
                                    spec = (_load_rs_cab_mic_map().get(rs_type) or {}).get(suffix)
                                    cand = (spec or {}).get("ir_file")
                                    if cand and (irs_root / cand).exists():
                                        _cab_file = cand
                        if _cab_file is None:
                            _cab_file = available[0]
                        pieces.append({
                            "slot": piece["slot"],
                            "rs_gear_type": rs_type,
                            "kind": "rs_ir",
                            "file": _cab_file,
                            "params": piece["knobs"],
                            "tone3000_id": None,
                            "assigned_mode": "auto",
                            "bypassed": _prev.get("bypassed", False),
                        })
                        continue

                    candidate = seen_gears.get(cache_key)
                    if candidate is None:
                        candidate = {}
                        # 1. Reuse a capture already assigned to this gear in
                        #    ANY song (manual choice → propagates library-wide).
                        #    For amps with variants we restrict the reuse to
                        #    the SAME variant — otherwise tone B's "dist"
                        #    would reuse tone A's "clean" download.
                        reused = _existing_assignment_for_gear(
                            rs_type,
                            tone3000_id=(amp_variant["tone3000_id"] if amp_variant else None),
                        )
                        if reused:
                            candidate = reused
                            _batch_log(f"reused existing {rs_type} → {reused.get('file')}")
                        # 2. Otherwise resolve a fresh tone3000 candidate.
                        elif client.has_api_access and query:
                            try:
                                from rb_core.tone3000_client import pick_top_candidate
                                top = None
                                if amp_variant:
                                    # Curated variant wins: use its
                                    # tone3000_id directly. No search.
                                    top = {"id": amp_variant["tone3000_id"],
                                           "title": f"variant:{amp_variant.get('_picked_level')}"}
                                else:
                                    # Prefer a bundled default capture for this gear
                                    # (default_captures.json) over a fresh search,
                                    # so a new install reproduces the curated tones.
                                    _dflt = _load_default_captures().get(rs_type)
                                    if _dflt and _dflt.get("tone3000_id"):
                                        top = {"id": _dflt["tone3000_id"], "title": "default"}
                                    elif settings.get("curated_only", False):
                                        # Curator mode: no fuzzy search.
                                        # Without a curated default or an
                                        # existing assignment, the gear
                                        # is left for the curator to pick.
                                        top = None
                                    else:
                                        resp = client.search_tones(query, gears=gears or None, platform=platform, page_size=5)
                                        top = pick_top_candidate(
                                            resp,
                                            aggressive=settings.get("aggressive", False),
                                            min_downloads=settings.get("min_downloads", 50),
                                        )
                                if top:
                                    candidate = {"tone3000_id": top.get("id"), "title": top.get("title")}
                                    # v3: auto-download the picked model into
                                    # nam_models/ (or normalized into nam_irs/
                                    # for cab IRs that lacked a game IR).
                                    # Result is cached per rs_gear so the
                                    # second song that uses this gear hits the
                                    # cache, not the network.
                                    # `model_id_override`: variant curators
                                    # can pin a SPECIFIC capture inside the
                                    # tone3000 page (see _pick_amp_gain_variant).
                                    if settings.get("auto_download", True):
                                        downloaded = _download_candidate(
                                            tone3000_id=top.get("id"),
                                            is_ir=(category == "cab"),
                                            rs_gear=rs_type,
                                            settings=settings,
                                            model_id_override=(amp_variant.get("model_id") if amp_variant else None),
                                        )
                                        if downloaded:
                                            kind, fname = downloaded
                                            candidate["kind"] = kind
                                            candidate["file"] = fname
                                            _batch_log(f"downloaded {rs_type} → {fname}")
                                        else:
                                            _batch_log(f"no download for {rs_type} (budget? api? no models?)")
                            except Exception:
                                log.warning("tone3000 search failed for %s", rs_type, exc_info=True)
                        seen_gears[cache_key] = candidate

                    pieces.append({
                        "slot": piece["slot"],
                        "rs_gear_type": rs_type,
                        "kind": candidate.get("kind") or "none",
                        "file": candidate.get("file"),
                        "params": piece["knobs"],
                        "tone3000_id": candidate.get("tone3000_id"),
                        "assigned_mode": "auto",
                        "bypassed": existing_by_gear.get(rs_type, {}).get("bypassed", False),
                        "vst_path": candidate.get("vst_path"),
                        "vst_format": candidate.get("vst_format"),
                        "vst_state": candidate.get("vst_state"),
                    })

                _persist_preset_chain(
                    filename=filename,
                    tone_key=tone_key or parsed["name"],
                    name=preset_name,
                    pieces=pieces,
                    assigned_mode="auto",
                )
                with _batch_lock:
                    _batch_state["assigned"] += 1

        _batch_log(f"Done. Unique gear seen: {len(seen_gears)}")

        # Tail step: download every curated amp variant from rs_to_real.json
        # whose .nam is missing on disk, regardless of whether any song uses
        # the amp. User request: 'puedes hacer que si o si se asignen a esos
        # amps cuando se descargen, independiente si se usan en una cancion
        # o no?' — so songs added later (and the catalog's variant
        # auditioner) find their NAMs ready.
        #
        # Re-uses the existing _preload_worker so the same skip-if-on-disk
        # check + rate gate + parallel download + wire-to-presets phase
        # apply. Only kicks in if tone3000 is reachable; otherwise we log
        # a hint and return without touching state.
        try:
            client = _get_t3k_client()
            if client and client.has_api_access:
                rs_map_for_preload = _load_rs_to_real() or {}
                preload_jobs = []
                for rs_gear, info in rs_map_for_preload.items():
                    if not isinstance(info, dict) or info.get("category") != "amp":
                        continue
                    for level, spec in (info.get("gain_variants") or {}).items():
                        if isinstance(spec, dict) and spec.get("tone3000_id"):
                            preload_jobs.append((rs_gear, level, spec))
                if preload_jobs:
                    _batch_log(
                        f"Phase 2: ensuring all {len(preload_jobs)} curated "
                        f"amp variants are downloaded (skips files already on disk)…"
                    )
                    with _preload_lock:
                        # Only start if no one else already kicked it.
                        if not _preload_state.get("running"):
                            _preload_state.update({
                                "running": True, "total": len(preload_jobs),
                                "done": 0, "current": "",
                                "downloaded": 0, "already_present": 0,
                                "failed": [], "errors": [],
                                "started_at": time.time(),
                                "finished_at": None,
                            })
                            _launch_now = True
                        else:
                            _launch_now = False
                    if _launch_now:
                        _preload_worker(preload_jobs)
                        with _preload_lock:
                            dl = _preload_state.get("downloaded", 0)
                            existed = _preload_state.get("already_present", 0)
                            failed = len(_preload_state.get("failed") or [])
                        _batch_log(
                            f"Curated variants: {dl} downloaded · {existed} "
                            f"already on disk · {failed} failed"
                        )
                    else:
                        _batch_log(
                            "Curated-variants preload already running; "
                            "skipping (see /preload_status)"
                        )
            else:
                _batch_log(
                    "Curated-variants tail skipped — tone3000 not connected"
                )
        except Exception:
            log.exception("curated-variants tail step crashed")
            _batch_log("Curated-variants tail step crashed — see server log")

        # Move songs onto the auto-downloaded tone3000 gears: redirect each amp
        # slot that has a matching custom_auto gear to it. No-op until the user
        # has run "Download automatic tone3000 tones". Only in "all" (Map) mode,
        # and only when amps are in the selected category set.
        if mode == "all" and (not categories or "amp" in categories):
            try:
                _rn = _redirect_amps_to_auto_gears()
                if _rn:
                    _batch_log(f"Redirected {_rn} amp slot(s) onto auto tone3000 gears")
            except Exception:
                log.exception("auto-gear redirect step crashed")
        elif mode == "factory":
            # Factory = default plugin: drop ALL gear redirects so every gear
            # plays its own native VST again, then restore every bundled-VST gear
            # onto its own plugin so no 'original VST gear' is left unmapped.
            try:
                _cleared = _clear_all_gear_redirects()
                if _cleared:
                    _batch_log(f"Cleared {_cleared} gear redirect(s) → back to native VSTs")
            except Exception:
                log.exception("clear redirects (factory) step crashed")
            try:
                _rev = _reassign_bundled_vsts_factory()
                if _rev:
                    _batch_log(f"Restored {_rev} piece(s) onto their bundled VST")
            except Exception:
                log.exception("bundled-VST restore (factory) step crashed")
    except Exception:
        log.exception("batch worker crashed")
        _batch_log("ERROR — see server log")
    finally:
        with _batch_lock:
            _batch_state["running"] = False
            _batch_state["finished_at"] = time.time()


# ── Per-song auto-download (shared by endpoint + watcher) ────────────

# Serializes the per-song flow so the background watcher and a manual
# "open song" don't run concurrently — they share the module-level
# `_batch_disk_bytes` budget counter and both call _download_candidate.
_auto_lock = threading.Lock()


def _auto_download_for_song(filename: str, path: Path) -> dict:
    """Parse one song, pick + download tone3000 captures for each gear
    piece, and persist the preset chain. Shared by the
    /auto_download_song endpoint and the materialization watcher.

    The caller must have already checked that `path` exists, is
    non-zero, and that a tone3000 API key is configured. Raises
    ValueError for an unreadable archive; other parse failures
    propagate. Returns the same counts dict the endpoint reports.

    Pieces already assigned in `preset_pieces` are skipped, so calling
    this twice for the same song (watcher + manual open) is idempotent.
    """
    global _batch_disk_bytes
    with _auto_lock:
        settings = _load_settings()
        rs_map = _load_rs_to_real()
        rs_irs_map = _load_rs_cab_to_ir()
        irs_root = _config_dir / "nam_irs"
        client = _get_t3k_client()
        # Installed-VST lookup so pedals/racks/EQ get their curated VST
        # primary (rs_gear_to_vst.json) instead of a tone3000 NAM — same as
        # the batch worker. This is what was missing on the per-song / cloud
        # materialization path, so cloud-downloaded songs landed on NAMs and
        # needed a manual "remap all".
        known_vst_lookup = _build_known_vst_lookup()
        song_key = _db_song_key(filename, path)

        # Song-pack only: a raw .psarc yields no tones (convert it first).
        raw_tones = (_read_tones_from_sloppak(song_key, _get_dlc_dir())
                     if _is_song_pack(path) else [])

        # Dedupe gear across the song's tones — the same JCM800 appears
        # in clean + lead + bass, but we only hit tone3000 once. Reset
        # the disk counter since we're acting on a single song.
        _batch_disk_bytes = 0
        seen_gears: dict[str, dict] = {}
        counts = {"processed": 0, "downloaded": 0, "rs_ir_used": 0,
                  "skipped_assigned": 0, "skipped_no_candidate": 0, "failed": 0}

        for raw in raw_tones:
            parsed = _parse_tone(raw)
            tone_key = parsed["key"] or parsed["name"]
            preset_name = f"{song_key}::{tone_key}"
            pieces: list[dict] = []
            conn = _get_conn()

            # CRITICAL: never touch a tone the user has already edited.
            # /save_preset writes a (filename, tone_key) row into
            # tone_mappings the first time a chain is saved, so the
            # presence of that row signals "user has customised this
            # tone — leave it alone". Without this guard the auto-
            # download flow would rebuild pieces[] from the PSARC's
            # GearList and call _persist_preset_chain, which does
            # DELETE+INSERT and wipes any added/reordered/removed
            # pieces the user had saved.
            # Match BOTH the canonical key (`key or name`) and the raw `key`
            # form — which is "" for tones with an empty Key. Older saves were
            # stored under the raw form (the frontend sent tone.key verbatim),
            # so a tone with an empty Key had its chain saved under tone_key=""
            # while this watcher looked it up under the name → guard missed →
            # the chain got rebuilt + wiped on every re-materialization (e.g.
            # each cloud_loader re-download). Checking both forms keeps those
            # presets protected.
            _filename_filter, _filename_args = _tone_mapping_filename_filter(song_key, path)
            already_saved = conn.execute(
                f"SELECT 1 FROM tone_mappings WHERE {_filename_filter} AND tone_key IN (?, ?) "
                "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id) LIMIT 1",
                (*_filename_args, tone_key, parsed["key"]),
            ).fetchone()
            if already_saved:
                _batch_log(f"  skip {preset_name} — user-saved chain (preserved)")
                # Count every piece as 'processed + skipped_assigned' so
                # the UI's progress meter still moves the right number.
                for _ in parsed["chain"]:
                    counts["processed"] += 1
                    counts["skipped_assigned"] += 1
                continue

            for piece in parsed["chain"]:
                rs_type = piece["type"]
                info = rs_map.get(rs_type) or {}
                category = info.get("category") or _guess_category_from_slot(piece["slot"])
                gears = info.get("tone3000_gears") or ""
                query = info.get("tone3000_query") or rs_type
                platform = _PLATFORM_FOR_CATEGORY.get(category, "nam")

                # Amp gain variant pick — for amps with curated variants,
                # this returns the tone3000_id that matches THIS tone's
                # Gain knob value (a clean Twin tone gets the Twin-clean
                # variant, a saturated one gets the Twin-dist variant).
                # `cache_key` keys the per-song dedupe by both gear AND
                # variant, so the same amp at two different gain settings
                # downloads as two separate captures.
                amp_variant = None
                if category == "amp":
                    amp_variant = _pick_amp_gain_variant(info, _gear_rs_gain(piece, info))
                cache_key = (rs_type, amp_variant["tone3000_id"]) if amp_variant else rs_type

                # Promote to the installed primary VST (rs_gear_to_vst.json)
                # BEFORE the existing-assignment reuse below — mirrors the
                # batch worker's order (manual guard → VST primary → reuse/NAM).
                # Bug fixed: a pedal/rack/EQ that was ever auto-assigned a
                # tone3000 NAM in a PRIOR song would be matched by the reuse
                # query (file IS NOT NULL) and re-used as that NAM here, never
                # reaching the VST primary — so cloud-downloaded songs landed on
                # NAMs and needed a manual "remap all". Computing the VST first
                # means mapped VST gear gets its VST + RS-knob vst_state on
                # download. Cabs still fall through to IR paths below.
                if category != "cab":
                    _vst_pick = _pick_installed_primary_vst(rs_type, known_vst_lookup)
                    if _vst_pick:
                        _vst_state = _compute_vst_state_for_piece(
                            rs_type, _vst_pick["vst_path"], piece["knobs"]
                        )
                        pieces.append({
                            "slot": piece["slot"],
                            "rs_gear_type": rs_type,
                            "kind": "vst",
                            "file": None,
                            "params": piece["knobs"],
                            "tone3000_id": None,
                            "assigned_mode": "auto",
                            "vst_path": _vst_pick["vst_path"],
                            "vst_format": _vst_pick["vst_format"],
                            "vst_state": _vst_state,
                        })
                        counts["processed"] += 1
                        continue

                # Skip pieces that already have a usable assignment in
                # the DB (re-opening a song shouldn't re-download). For
                # amps with curated variants we look for the SPECIFIC
                # variant we need; without that, picking a "clean" for
                # tone A would mean tone B's "dist" check also matches
                # the clean and skips the dist download.
                # Cabs NEVER reuse a stale DB assignment — after prior sessions that
                # can be a random other/*.wav tone3000 download that survived, which
                # is what made factory reset "reuse existing → other/…wav" instead of
                # the rig-builder default. They resolve to the game IR / OUR bundled
                # cab IR below, so skip the reuse query for cabs entirely.
                existing = None
                if category != "cab":
                    if amp_variant:
                        existing = conn.execute(
                            "SELECT kind, file FROM preset_pieces "
                            "WHERE rs_gear_type = ? AND tone3000_id = ? "
                            "  AND file IS NOT NULL AND file != '' "
                            "ORDER BY id DESC LIMIT 1",
                            (rs_type, amp_variant["tone3000_id"]),
                        ).fetchone()
                    else:
                        existing = conn.execute(
                            "SELECT kind, file FROM preset_pieces "
                            "WHERE rs_gear_type = ? AND file IS NOT NULL AND file != '' "
                            "ORDER BY id DESC LIMIT 1",
                            (rs_type,),
                        ).fetchone()
                if existing:
                    # Trust the reused row only if the file still exists on
                    # disk (mirrors _existing_assignment_for_gear) — a purged
                    # or renamed capture must fall through to a fresh download.
                    _ex_kind, _ex_file = existing
                    _ex_root = _config_dir / ("nam_models" if _ex_kind == "nam"
                                              else "nam_irs")
                    if not (_ex_root / _ex_file).exists():
                        existing = None
                if existing:
                    pieces.append({
                        "slot": piece["slot"],
                        "rs_gear_type": rs_type,
                        "kind": existing[0],
                        "file": existing[1],
                        "params": piece["knobs"],
                        "tone3000_id": None,
                        "assigned_mode": "auto",
                    })
                    counts["skipped_assigned"] += 1
                    counts["processed"] += 1
                    continue

                # Cab fast-path: prefer the game IR when on disk.
                # Use the song's `Cabinet.Key` (the Wwise Effect name —
                # e.g. "Bass_Cab_AT810BC_Tube_Cone") to pick the CORRECT
                # mic position via rs_cab_mic_map. Falls back to the
                # first IR in the list when the cab has no mic-map
                # entry (older cabs, novelty cabs, etc.) so the legacy
                # "always _00" behaviour stays as a safety net.
                rs_ir_entry = rs_irs_map.get(rs_type) or {}
                available = [
                    f for f in (rs_ir_entry.get("irs") or [])
                    if (irs_root / f).exists()
                ]
                if category == "cab" and available:
                    chosen_file = available[0]
                    cabinet_key = piece.get("cabinet_key") or ""
                    if cabinet_key:
                        suffix = _cab_suffix_from_effect_name(cabinet_key)
                        if suffix:
                            spec = (_load_rs_cab_mic_map().get(rs_type) or {}).get(suffix)
                            cand = (spec or {}).get("ir_file")
                            if cand and (irs_root / cand).exists():
                                chosen_file = cand
                    pieces.append({
                        "slot": piece["slot"],
                        "rs_gear_type": rs_type,
                        "kind": "rs_ir",
                        "file": chosen_file,
                        "params": piece["knobs"],
                        "tone3000_id": None,
                        "assigned_mode": "auto",
                    })
                    counts["rs_ir_used"] += 1
                    counts["processed"] += 1
                    continue

                # OUR bundled cab IR (rb_cab_overrides) — every modeled cab ships
                # one, so a cab should NEVER need a tone3000 IR download. Critical
                # after a FACTORY RESET: the game IRs (rs_cab_to_ir) were wiped from
                # nam_irs so the fast-path above finds nothing, but
                # _install_bundled_cab_irs re-seeded ours on startup. Without this a
                # cab fell through to tone3000 and "mapped to a NAM/download" instead
                # of the rig-builder default cab.
                if category == "cab":
                    _ovr_rel = _override_ir_for_cab(rs_type, irs_root)
                    if _ovr_rel:
                        pieces.append({
                            "slot": piece["slot"],
                            "rs_gear_type": rs_type,
                            "kind": "rs_ir",
                            "file": _ovr_rel,
                            "params": piece["knobs"],
                            "tone3000_id": None,
                            "assigned_mode": "auto",
                        })
                        counts["rs_ir_used"] += 1
                        counts["processed"] += 1
                        continue

                # tone3000 search + download. Dedupe per gear (+ variant
                # when applicable) so the same amp appearing in 3 tones
                # only hits the API once PER variant.
                cached = seen_gears.get(cache_key)
                if cached is None:
                    cached = {}
                    try:
                        from rb_core.tone3000_client import pick_top_candidate
                        top = None
                        if amp_variant:
                            # Curated variant wins: no search, just use
                            # the tone3000_id the curator chose for this
                            # gain range. Log the level we picked so the
                            # user can spot in the batch log which tones
                            # got which variant.
                            top = {"id": amp_variant["tone3000_id"],
                                   "title": f"variant:{amp_variant.get('_picked_level')}"}
                        else:
                            # Prefer a bundled default capture (default_captures.json).
                            _dflt = _load_default_captures().get(rs_type)
                            if _dflt and _dflt.get("tone3000_id"):
                                top = {"id": _dflt["tone3000_id"], "title": "default"}
                            elif settings.get("curated_only", False):
                                # Curated-only: skip tone3000 fuzzy search.
                                # The piece will land in pending — the
                                # curator picks it manually via
                                # Gear → 📚 Library / 🎚 Variants.
                                top = None
                            else:
                                resp = client.search_tones(
                                    query, gears=gears or None, platform=platform, page_size=5,
                                )
                                top = pick_top_candidate(
                                    resp,
                                    aggressive=settings.get("aggressive", False),
                                    min_downloads=settings.get("min_downloads", 50),
                                )
                        if top:
                            cached["tone3000_id"] = top.get("id")
                            downloaded = _download_candidate(
                                tone3000_id=top.get("id"),
                                is_ir=(category == "cab"),
                                rs_gear=rs_type,
                                settings=settings,
                                model_id_override=(amp_variant.get("model_id") if amp_variant else None),
                            )
                            if downloaded:
                                cached["kind"], cached["file"] = downloaded
                            else:
                                counts["failed"] += 1
                        else:
                            counts["skipped_no_candidate"] += 1
                    except Exception:
                        log.warning("auto_download_song search failed for %s", rs_type, exc_info=True)
                        counts["failed"] += 1
                    seen_gears[cache_key] = cached

                if cached.get("file"):
                    counts["downloaded"] += 1
                pieces.append({
                    "slot": piece["slot"],
                    "rs_gear_type": rs_type,
                    "kind": cached.get("kind") or "none",
                    "file": cached.get("file"),
                    "params": piece["knobs"],
                    "tone3000_id": cached.get("tone3000_id"),
                    "assigned_mode": "auto",
                })
                counts["processed"] += 1

            _persist_preset_chain(
                filename=song_key,
                tone_key=tone_key,
                name=preset_name,
                pieces=pieces,
                assigned_mode="auto",
            )

        return {"ok": True, **counts}


# Songs whose seed we've already reconciled this session — bounds
# `_resync_stale_song_seed` to AT MOST one re-seed attempt per song per
# run, so a song whose new gear genuinely can't be mapped can never spin
# in a re-seed loop (check → still "stale" → check → …).
_resynced_songs: set[str] = set()


def _resync_stale_song_seed(song_key: str, path: Path) -> list[str]:
    """Re-seed tones whose SOURCE gear changed under a stable filename.

    A cloud library rewrites `sloppak_cache/<song>` in place (switching to a
    different library, re-downloading a corrected chart) while the DLC entry
    stays a 0-byte stub. The materialization watcher keys on DLC *size*, so it
    never sees that change and never re-fires; and even if it did,
    `_auto_download_for_song` skips any tone that already has a mapping. Net
    effect: the seeded `tone_mappings`/`presets` keep describing the OLD gear,
    so the song plays a wrong tone (e.g. an acoustic tone whose amp silently
    became a tweed combo after a library swap).

    Detect per-tone staleness by comparing the AMP gear key — every guitar/bass
    tone has exactly one, it always resolves to a VST/NAM (so this can't false-
    positive on an unmappable pedal and loop), and a user VST swap keeps the
    rs_gear_type while only changing which plugin plays it, so this never fires
    on a customised tone. When the amp key differs, the seed predates the
    current sloppak: drop those tones' presets and let `_auto_download_for_song`
    rebuild them from the live GearList (untouched tones keep their mapping and
    are skipped, so user edits elsewhere in the song are preserved).

    Returns the tone_keys that were re-seeded (empty when nothing was stale).
    """
    if song_key in _resynced_songs:
        return []
    if not _is_song_pack(path):
        _resynced_songs.add(song_key)
        return []
    try:
        raw_tones = _read_tones_from_sloppak(song_key, _get_dlc_dir())
    except Exception:
        return []   # transient cache miss — retry on the next open
    if not raw_tones:
        return []

    def _amp_key(parsed: dict) -> str | None:
        for piece in parsed.get("chain") or []:
            if piece.get("slot") == "amp" and piece.get("type"):
                return piece["type"]
        return None

    current_amp: dict[str, str | None] = {}
    for raw in raw_tones:
        parsed = _parse_tone(raw)
        tk = parsed.get("key") or parsed.get("name")
        if tk:
            current_amp[tk] = _amp_key(parsed)

    conn = _get_conn()
    _filt, _args = _tone_mapping_filename_filter(song_key, path)
    seeded_amp: dict[str, str] = {}
    seeded_mode: dict[str, str] = {}
    preset_by_tone: dict[str, int] = {}
    for tk, pid, gear, amode in conn.execute(
        f"SELECT tm.tone_key, tm.preset_id, pp.rs_gear_type, pp.assigned_mode "
        f"FROM tone_mappings tm JOIN preset_pieces pp ON pp.preset_id = tm.preset_id "
        f"WHERE {_filt} AND pp.slot = 'amp'",
        _args,
    ).fetchall():
        seeded_amp[tk] = gear
        seeded_mode[tk] = str(amode or "")
        preset_by_tone[tk] = pid

    # Mark handled now: even if the read/parse above was fine but nothing is
    # stale, we don't want to re-read the sloppak on every open of this song.
    _resynced_songs.add(song_key)

    # A DIFFERENT amp key means EITHER the sloppak was rewritten under a stable
    # filename (a genuine stale seed → re-seed) OR the user deliberately swapped
    # the amp gear in the editor to fix a bad tone (assigned_mode 'manual'/'manual_vst').
    # NEVER re-seed a user's manual amp swap — doing so silently deleted the edited
    # preset and reverted the song to its original tone on the next play.
    stale = [tk for tk, amp in current_amp.items()
             if tk in seeded_amp and amp and amp != seeded_amp[tk]
             and not seeded_mode.get(tk, "").startswith("manual")]
    if not stale:
        return []

    for tk in stale:
        pid = preset_by_tone[tk]
        conn.execute("DELETE FROM tone_mappings WHERE preset_id = ?", (pid,))
        conn.execute("DELETE FROM presets WHERE id = ?", (pid,))   # cascades pieces
    conn.commit()
    log.info("rig_builder: re-seeding %d stale tone(s) for %s (amp key changed): %s",
             len(stale), song_key, stale)
    try:
        _auto_download_for_song(song_key, path)
    except Exception:
        log.warning("rig_builder: re-seed after stale-amp detection failed for %s",
                    song_key, exc_info=True)
    return stale


# ── Background materialization watcher ───────────────────────────────

# Polls the DLC dir; when a song flips from a 0-byte cloud stub to real
# content (cloud_loader materialize, a manual copy, another plugin's
# extractor, …) it runs the per-song auto-download so the NAM chain is
# ready before the user hits play in feedBack's main view. Decoupled
# from cloud_loader on purpose — it reacts to the file appearing on
# disk, not to any specific code path that produced it.
_WATCH_INTERVAL_SEC = 5
_watcher_thread: threading.Thread | None = None
_watcher_stop = threading.Event()
_watcher_sizes: dict[str, int] = {}     # filename -> size at last tick
_watcher_pending: dict[str, int] = {}   # filename -> size awaiting a stable confirmation
_watcher_state: dict = {
    "running": False,
    "primed": False,
    "fired_count": 0,
    "last_fired": None,
    "last_error": None,
    "interval_sec": _WATCH_INTERVAL_SEC,
}


def _watch_scan_dlc() -> dict[str, int] | None:
    """Return {relative_dlc_path: size} for every PSARC/sloppak in the DLC dir,
    or None if the dir isn't available. RECURSIVE — picks up files
    nested in subfolders (artist-based, converter output, or any other
    organisation)."""
    dlc = _get_dlc_dir() if _get_dlc_dir else None
    if not dlc:
        return None
    current: dict[str, int] = {}
    try:
        for p in dlc.rglob("*"):
            if not p.is_file():
                continue
            if not _is_song_pack(p):
                continue
            try:
                key = _dlc_relative_song_key(p)
                if not key:
                    continue
                current[key] = p.stat().st_size
            except OSError:
                continue
    except OSError:
        return None
    return current


def _watch_tick() -> list[str]:
    """Diff the DLC dir against the previous tick and return the list of
    songs that just finished materializing (confirmed stable). Updates
    the baseline as a side effect."""
    current = _watch_scan_dlc()
    if current is None:
        return []

    # First tick: record a baseline and fire nothing. We only react to
    # transitions during the session — firing for every already-
    # materialized song at startup would replicate the library-wide
    # batch and could blow the disk budget.
    if not _watcher_state["primed"]:
        _watcher_sizes.clear()
        _watcher_sizes.update(current)
        _watcher_state["primed"] = True
        return []

    to_fire: list[str] = []
    for name, cur in current.items():
        prev = _watcher_sizes.get(name)
        if name in _watcher_pending:
            # Saw it go non-zero last tick; confirm the size is stable
            # before parsing (defends against non-atomic writers that
            # stream into place rather than rename a temp file).
            if cur > 0 and cur == _watcher_pending[name]:
                to_fire.append(name)
                del _watcher_pending[name]
            elif cur > 0:
                _watcher_pending[name] = cur          # still growing
            else:
                del _watcher_pending[name]            # re-stubbed mid-write
        elif cur > 0 and (prev == 0 or prev is None):
            # 0-byte stub became real, or a brand-new file appeared.
            _watcher_pending[name] = cur

    # Forget pending entries for files that disappeared this tick.
    for gone in [n for n in _watcher_pending if n not in current]:
        del _watcher_pending[gone]

    _watcher_sizes.clear()
    _watcher_sizes.update(current)
    return to_fire


def _watch_loop():
    log.info("rig_builder watcher started (interval=%ss)", _WATCH_INTERVAL_SEC)
    _watcher_state["running"] = True
    try:
        while not _watcher_stop.is_set():
            try:
                to_fire = _watch_tick()
            except Exception:
                log.exception("rig_builder watcher tick failed")
                _watcher_state["last_error"] = "tick failed (see server log)"
                to_fire = []

            if to_fire:
                settings = _load_settings()
                # Fire on every newly-materialized song. tone3000 is optional:
                # _watch_fire → _auto_download_for_song assigns VST primaries with
                # no download and only uses the client for gear lacking a VST,
                # so a VST-only rig (no token) still gets its chain seeded in the
                # background. (Previously gated on has_api_access, so users
                # without a tone3000 key never got background seeding at all.)
                if settings.get("auto_watch", True):
                    for name in to_fire:
                        if _watcher_stop.is_set():
                            break
                        _watch_fire(name)

            _watcher_stop.wait(_WATCH_INTERVAL_SEC)
    finally:
        _watcher_state["running"] = False


def _watch_fire(name: str) -> None:
    path = _resolve_song_file(name)
    if path is None:
        return
    song_key = _db_song_key(name, path)
    # Cloud-placeholder libraries keep a 0-byte stub in the DLC dir; the real
    # data lives in sloppak_cache. Don't bail on the stub when the cache copy
    # is materialized — the tone reader reads from there.
    if not _song_data_available(path):
        return
    try:
        result = _auto_download_for_song(song_key, path)
        _watcher_state["fired_count"] += 1
        _watcher_state["last_fired"] = {
            "filename": song_key,
            "ts": time.time(),
            "processed": result.get("processed"),
            "downloaded": result.get("downloaded"),
        }
        log.info("rig_builder watcher auto-downloaded %s: %s", song_key, result)
        # Also fix cab assignments right after the download. Materialised
        # songs whose DLC PSARC ships `Cabinet.Type='Cabinets'` get the
        # real cab + correct mic IR baked into preset_pieces here — the
        # user doesn't need to open the song in the editor for the fix
        # to land. Idempotent: already-correct rows are no-ops.
        try:
            # Song-pack only: a raw .psarc yields no tones (convert it first).
            raw_tones = (_read_tones_from_sloppak(song_key, _get_dlc_dir())
                         if _is_song_pack(path) else [])
            _auto_fix_cab_mics_for_song_module(song_key, raw_tones)
            _promote_generic_gear_for_song_module(song_key, raw_tones)
        except Exception:
            log.exception("rig_builder watcher cab-mic fix failed for %s", song_key)
    except Exception as e:
        log.exception("rig_builder watcher auto-download failed for %s", song_key)
        _watcher_state["last_error"] = f"{song_key}: {type(e).__name__}: {e}"


def _self_heal_song_gear(filename: str) -> None:
    """Idempotent cab + generic-gear self-heal against the song's live
    GearList, callable from any chain-build path (not just the editor's
    get_song). Covers songs SEEDED before a parser fix — e.g. RS1-era tones
    whose Cabinet.Key is a bare base cab ("Cab_PA600C"): their rows stayed on
    the generic "Cabinets" placeholder until the user happened to open the
    song in the editor. Already-correct rows are no-ops, so running it on
    every song load doesn't churn."""
    try:
        path = _resolve_song_file(filename)
        if path is None or not _is_song_pack(path) or not _song_data_available(path):
            return
        song_key = _db_song_key(filename, path)
        raw_tones = _read_tones_from_sloppak(song_key, _get_dlc_dir())
        if not raw_tones:
            return
        _auto_fix_cab_mics_for_song_module(song_key, raw_tones)
        _promote_generic_gear_for_song_module(song_key, raw_tones)
    except Exception:
        log.exception("song gear self-heal failed for %s", filename)


def _auto_fix_cab_mics_for_song_module(filename: str, raw_tones: list) -> int:
    """Module-level version of the per-song cab-mic fixer (the closure
    `_auto_fix_cab_mics_for_song` inside setup() captures `conn`/`_lock`
    via the outer scope; this twin is callable from the watcher + the
    playback path without going through the FastAPI handler).

    Walks each tone's Cabinet.Key, resolves to (base_rs_gear, suffix)
    via rs_cab_mic_map, and updates every preset_piece for that tone
    so rs_gear_type + file land on the correct cab + mic IR. Skips
    real manual picks (assigned_mode='manual' AND a real rs_gear_type
    that isn't 'Cabinets'); always force-fixes the generic 'Cabinets'
    placeholder. Returns the number of rows updated.
    """
    if _config_dir is None or not raw_tones:
        return 0
    irs_root = _config_dir / "nam_irs"
    conn = _get_conn()
    _filename_filter, _filename_args = _tone_mapping_filename_filter(filename)
    tm_rows = conn.execute(
        "SELECT tone_key, preset_id FROM tone_mappings "
        f"WHERE {_filename_filter} "
        "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)",
        _filename_args,
    ).fetchall()
    if not tm_rows:
        return 0
    tone_to_preset = {tk: pid for tk, pid in tm_rows}
    updated = 0
    affected: set[int] = set()
    with _lock:
        for raw in raw_tones:
            tone_key = raw.get("Key") or raw.get("Name") or ""
            preset_id = tone_to_preset.get(tone_key)
            if preset_id is None:
                continue
            cab = (raw.get("GearList") or {}).get("Cabinet") or {}
            effect_name = (cab.get("Key") or "").strip()
            if not effect_name:
                continue
            resolved = _resolve_cab_for_effect(effect_name, irs_root)
            if not resolved:
                continue
            base_rs_gear, new_file = resolved
            base_lc = base_rs_gear.lower()
            rows = conn.execute(
                "SELECT id, rs_gear_type, file, assigned_mode "
                "FROM preset_pieces "
                "WHERE preset_id = ? "
                "  AND (rs_gear_type = 'Cabinets' "
                "       OR LOWER(rs_gear_type) = ? "
                "       OR LOWER(rs_gear_type) LIKE ?)",
                (preset_id, base_lc, base_lc + "_%"),
            ).fetchall()
            for row_id, rs_gear, cur_file, mode in rows:
                is_generic = (rs_gear == "Cabinets")
                if mode == "manual" and not is_generic:
                    continue
                needs_promote = (rs_gear != base_rs_gear)
                if not needs_promote and cur_file == new_file:
                    continue
                if needs_promote:
                    conn.execute(
                        "UPDATE preset_pieces "
                        "SET rs_gear_type = ?, file = ?, kind = 'rs_ir' "
                        "WHERE id = ?",
                        (base_rs_gear, new_file, row_id),
                    )
                else:
                    conn.execute(
                        "UPDATE preset_pieces "
                        "SET file = ?, kind = 'rs_ir' WHERE id = ?",
                        (new_file, row_id),
                    )
                updated += 1
                affected.add(preset_id)
        if updated:
            for pid in affected:
                _recompute_preset_primaries(conn, pid)
            conn.commit()
    return updated


# Generic family strings the game puts in `gear.Type` for every gear of
# that family. A preset_piece carrying one of these as its rs_gear_type is
# a parser-miss (the .Key didn't resolve via the old knob heuristic) — the
# UI never lets the user pick a generic family deliberately, so promoting it
# to the real .Key is always safe. Cabinets are handled by the cab self-heal.
_GENERIC_GEAR_FAMILIES = {"Amps", "Pedals", "Racks"}

# RS GearList slot names per internal slot_type, in the SAME order
# `_parse_tone` walks them — so positional zip against a preset's
# preset_pieces (ordered by slot_order) lines up piece-for-piece.
_SLOT_GEARLIST_NAMES = {
    "pre_pedal":  ("PrePedal1", "PrePedal2", "PrePedal3", "PrePedal4"),
    "amp":        ("Amp",),
    "post_pedal": ("PostPedal1", "PostPedal2", "PostPedal3", "PostPedal4"),
    "rack":       ("Rack1", "Rack2", "Rack3", "Rack4"),
}


def _promote_generic_gear_for_song_module(filename: str, raw_tones: list) -> int:
    """Promote generic 'Amps'/'Pedals'/'Racks' preset_piece rows to the
    real rs_gear (the GearList slot's `.Key`, e.g. "Pedal_EQ5").

    The non-cabinet analogue of `_auto_fix_cab_mics_for_song_module`: the
    same parser-miss that dumped knob-less / unmapped gear into a generic
    family bucket (because the old `_extract_model_from_knobs` heuristic
    fell back to `gear.Type`) is fixed at parse time now, but existing rows
    in the DB still carry the generic string. For each tone, re-read its
    GearList, line the slots up positionally with the preset's stored
    pieces of the same slot, and rewrite ONLY rs_gear_type on rows still
    holding a generic family string. Assignments (file / vst / kind /
    assigned_mode) are left untouched — the gear identity becomes accurate
    without disturbing what's loaded. Returns rows updated.
    """
    if not raw_tones:
        return 0
    conn = _get_conn()
    _filename_filter, _filename_args = _tone_mapping_filename_filter(filename)
    tm_rows = conn.execute(
        "SELECT tone_key, preset_id FROM tone_mappings "
        f"WHERE {_filename_filter} "
        "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)",
        _filename_args,
    ).fetchall()
    if not tm_rows:
        return 0
    tone_to_preset = {tk: pid for tk, pid in tm_rows}
    updated = 0
    with _lock:
        for raw in raw_tones:
            tone_key = raw.get("Key") or raw.get("Name") or ""
            preset_id = tone_to_preset.get(tone_key)
            if preset_id is None:
                continue
            gear_list = raw.get("GearList") or {}
            for slot_type, gl_names in _SLOT_GEARLIST_NAMES.items():
                # Ordered .Key list for this slot (skip empty/typeless slots,
                # mirroring _parse_tone's `g and g.get("Type")` guard).
                gl_keys: list[str] = []
                for nm in gl_names:
                    g = gear_list.get(nm)
                    if g and g.get("Type"):
                        gl_keys.append((g.get("Key") or "").strip())
                if not gl_keys:
                    continue
                pp_rows = conn.execute(
                    "SELECT id, rs_gear_type FROM preset_pieces "
                    "WHERE preset_id = ? AND slot = ? ORDER BY slot_order",
                    (preset_id, slot_type),
                ).fetchall()
                for i, (row_id, rs_gear) in enumerate(pp_rows):
                    if rs_gear not in _GENERIC_GEAR_FAMILIES:
                        continue
                    if i >= len(gl_keys):
                        continue
                    key = gl_keys[i]
                    if key and key != rs_gear:
                        conn.execute(
                            "UPDATE preset_pieces SET rs_gear_type = ? WHERE id = ?",
                            (key, row_id),
                        )
                        updated += 1
        if updated:
            conn.commit()
    return updated


def _start_watcher() -> None:
    global _watcher_thread
    if _watcher_thread is not None and _watcher_thread.is_alive():
        return
    _watcher_stop.clear()
    _watcher_thread = threading.Thread(
        target=_watch_loop, name="rig_builder_watcher", daemon=True,
    )
    _watcher_thread.start()


# ── FastAPI route registration ───────────────────────────────────────


def _canonical_song_key(filename: str) -> str:
    """Identity of a song independent of container format, so the same song in
    `.psarc` and `.sloppak` resolves to one thing. Drops the extension and the
    psarc PC marker (`_p`). E.g. both
    `RHCP_Aquatic-Mouth-Dance_v1.sloppak` and
    `RHCP_Aquatic-Mouth-Dance_v1_p.psarc` → `rhcp_aquatic-mouth-dance_v1`.
    The version suffix (`_v1`) is kept on purpose — different versions/charts
    are genuinely different files."""
    base = filename.rsplit("/", 1)[-1]
    if "." in base:
        base = base.rsplit(".", 1)[0]
    low = base.lower()
    if low.endswith("_p"):
        low = low[:-2]
    return low

def setup(app, context):
    global _config_dir, _get_dlc_dir, _get_sloppak_cache_dir, _db_path

    _config_dir = Path(context["config_dir"])
    _get_dlc_dir = context["get_dlc_dir"]
    _get_sloppak_cache_dir = context.get("get_sloppak_cache_dir")
    _db_path = str(_config_dir / "nam_tone.db")

    # Generated data (rs_to_real.json, default_captures.json, …) must be
    # writable. The bundled plugin dir is read-only in packaged builds (e.g. an
    # AppImage squashfs mount), so writing into the bundled data/ fails with
    # OSError(Errno 30). Use a writable per-user data dir, seed
    # the bundled defaults into it once, and export RIG_BUILDER_DATA_DIR so both
    # _data_path() and any subprocesses (common.DATA_DIR honours it)
    # read/write there. Idempotent: only seeds files without a (user-edited)
    # copy already present.
    # Only these are user-generated (e.g. "Export defaults") and must
    # PERSIST across updates — seed them once if missing, never overwrite.
    # EVERYTHING ELSE is a static ship-with-plugin catalog (gear→VST map, display
    # names, knob→param map, loudness model, cab makeup, type tags, …) and is
    # refreshed from the bundle whenever it differs, so a plugin update actually
    # takes effect. The old "seed once for all" left these stale on update — e.g.
    # the seeded vst_display_names.json predated the amp clones, so amps showed
    # their the game names, and the gear map predated the amp VSTs, so amps fell
    # back to NAM.
    _USER_GENERATED = {"rs_to_real.json", "rs_cab_to_ir.json", "default_captures.json"}
    _writable_data = _config_dir / "nam_rig_builder" / "data"
    try:
        _writable_data.mkdir(parents=True, exist_ok=True)
        _bundled_data = _plugin_dir / "data"
        if _bundled_data.is_dir():
            for _src in _bundled_data.glob("*.json"):
                _dest = _writable_data / _src.name
                if _src.name in _USER_GENERATED:
                    if not _dest.exists():
                        _dest.write_bytes(_src.read_bytes())
                elif not _dest.exists() or _dest.read_bytes() != _src.read_bytes():
                    _dest.write_bytes(_src.read_bytes())
        os.environ["RIG_BUILDER_DATA_DIR"] = str(_writable_data)
    except OSError:
        log.exception("could not set up writable data dir; falling back to bundled data/")

    # Force migration on cold start so the table exists by the time the
    # UI first asks for assignments.
    _get_conn()
    _load_settings()

    # Install OUR bundled cab IRs into nam_irs/cabs so rb_cab_overrides
    # resolves on a fresh install (copies only missing files).
    _install_bundled_cab_irs()

    # Start the background materialization watcher so songs played from
    # feedBack's main view (which only triggers cloud_loader to put the
    # PSARC on disk) get their NAM chain auto-downloaded too — not just
    # songs opened from the NAM Rig Builder tab.
    _start_watcher()

    # ── Status / setup ────────────────────────────────────────────────

    @app.post("/api/plugins/rig_builder/debug_log")
    async def debug_log(request: Request):
        # Frontend diagnostics relay: the renderer's console isn't visible when
        # the app runs from a terminal, but this server's stdout is (the main
        # process echoes it as [python:stdout]). Print-and-forget.
        try:
            body = await request.json()
            print(f"[rb-debug] {str(body.get('msg', ''))[:2000]}", flush=True)
        except Exception:
            pass
        return {"ok": True}

    # ── Plugin self-update (Setup → Rig Builder version) ────────────────────
    # Self-contained: reads its own version from plugin.json and the latest from
    # GitHub raw, and updates via `git` when this copy is a clone. Does NOT depend
    # on the update_manager plugin (older bundled copies lack its /check endpoint,
    # which is what left the version showing "unknown").
    _PLUGIN_RAW_MANIFEST = (
        "https://raw.githubusercontent.com/got-feedBack/"
        "feedBack-plugin-rig-builder/main/plugin.json"
    )

    def _rb_own_version():
        try:
            return json.loads((_plugin_dir / "plugin.json").read_text(encoding="utf-8")).get("version")
        except Exception:
            return None

    def _rb_ver_tuple(v):
        try:
            return tuple(int(x) for x in str(v).split(".")[:3])
        except Exception:
            return None

    @app.get("/api/plugins/rig_builder/update_status")
    def rb_update_status():
        local = _rb_own_version()
        remote, err = None, None
        try:
            req = urllib.request.Request(
                _PLUGIN_RAW_MANIFEST, headers={"User-Agent": "rig_builder-updater"})
            with urllib.request.urlopen(req, timeout=12) as r:
                remote = (json.loads(r.read().decode("utf-8")) or {}).get("version")
        except Exception as e:
            err = str(e)
        lt, rt = _rb_ver_tuple(local), _rb_ver_tuple(remote)
        return {
            "local_version": local,
            "remote_version": remote,
            "update_available": bool(lt and rt and rt > lt),
            "is_git": (_plugin_dir / ".git").exists(),
            "error": err,
        }

    @app.post("/api/plugins/rig_builder/self_update")
    def rb_self_update():
        if not (_plugin_dir / ".git").exists():
            return {"error": "This copy isn't a git checkout — reinstall from GitHub "
                             "(clone into your plugins folder) to enable in-app updates."}
        try:
            for args in (["fetch", "--depth", "1", "origin", "main"],
                         ["reset", "--hard", "FETCH_HEAD"]):
                subprocess.run(["git", "-C", str(_plugin_dir), *args],
                               check=True, capture_output=True, timeout=600)
            return {"ok": True, "version": _rb_own_version()}
        except subprocess.CalledProcessError as e:
            msg = (e.stderr or b"").decode("utf-8", "ignore").strip()
            return {"error": msg[:300] or "git update failed"}
        except Exception as e:
            return {"error": str(e)}

    @app.get("/api/plugins/rig_builder/status")
    def status():
        rs_map = _load_rs_to_real()
        settings = _load_settings()
        client = _get_t3k_client()
        # Run a tiny harmless request only when we have a key, to verify
        # it's valid and update has_api_access. Cached after first hit.
        if client.api_key and not client.has_api_access:
            try:
                client.search_tones("test", gears="amp", page_size=1)
            except Exception:
                pass
        coverage = {"total": len(rs_map), "by_category": {}}
        for v in rs_map.values():
            coverage["by_category"][v.get("category", "other")] = (
                coverage["by_category"].get(v.get("category", "other"), 0) + 1
            )
        rs_irs_map = _load_rs_cab_to_ir()
        # Count how many of the mapped IRs are actually on disk. The
        # JSON ships with the plugin; the game cab .wav files don't.
        # A fresh install has the map but no game-cab audio — the UI
        # uses this distinction to show whether game-cab IRs are present.
        irs_root = _config_dir / "nam_irs"
        rs_irs_on_disk = 0
        for entry in rs_irs_map.values():
            for f in entry.get("irs") or []:
                if (irs_root / f).exists():
                    rs_irs_on_disk += 1
                    break  # one disk-resident IR per RS entity is enough
        return {
            "rs_to_real_loaded": bool(rs_map),
            "rs_to_real_count": len(rs_map),
            "rs_to_real_by_category": coverage["by_category"],
            "rs_cab_to_ir_loaded": bool(rs_irs_map),
            "rs_cab_to_ir_count": len(rs_irs_map),
            "rs_irs_on_disk": rs_irs_on_disk,
            "has_tone3000_key": bool(settings.get("tone3000_api_key") or settings.get("tone3000_access_token")),
            "tone3000_api_works": client.has_api_access,
            "tone3000_connected": bool(settings.get("tone3000_access_token")),
            "tone3000_username": settings.get("tone3000_username") or "",
            "settings": {
                "min_downloads": settings.get("min_downloads"),
                "aggressive": settings.get("aggressive"),
                "auto_watch": settings.get("auto_watch", True),
            },
            "watcher": {
                "running": _watcher_state["running"],
                "primed": _watcher_state["primed"],
                "fired_count": _watcher_state["fired_count"],
                "last_fired": _watcher_state["last_fired"],
                "last_error": _watcher_state["last_error"],
                "interval_sec": _watcher_state["interval_sec"],
            },
        }

    @app.get("/api/plugins/rig_builder/final_leveler_stage")
    def final_leveler_stage():
        # Only used by the standalone-VST gear audition → clamp to the safe level.
        missing = []
        stage = _final_leveler_stage(missing, audition=True)
        return {
            "enabled": stage is not None,
            "stage": stage,
            "missing": missing,
        }
    
    @app.get("/api/plugins/rig_builder/settings")
    def get_settings():
        s = _load_settings()
        # Return the key truncated so we can show "configured" in the
        # UI without leaking the secret to the rendered DOM.
        key = s.get("tone3000_api_key") or ""
        return {
            "min_downloads": s.get("min_downloads", 50),
            "aggressive": s.get("aggressive", False),
            "curated_only": s.get("curated_only", False),
            "preferred_size": s.get("preferred_size", "standard"),
            "mega_chain_mode": s.get("mega_chain_mode", True),
            "default_tone_enabled": bool(s.get("default_tone_enabled", False)),
            "rig_builder_enabled": bool(s.get("rig_builder_enabled", True)),
            # "Play a specific tone" override — MUST be returned here or the
            # Setup toggle + chosen tone can't be restored on reopen (they persist
            # to disk fine via POST, but this GET is a fixed allowlist).
            "tone_override_enabled": bool(s.get("tone_override_enabled", False)),
            "tone_override_name": s.get("tone_override_name") or "",
            "bypass_all_cabs": s.get("bypass_all_cabs", True),
            # Chain-input drive (engine setGain('input', X)). Read by JS
            # at every chain load — value of 8.0 = +18 dB feeds NAM amps
            # at capture-time levels so they actually saturate.
            "nam_chain_input_drive": float(s.get("nam_chain_input_drive", 1.0)),
            # User "Output chain" / chain-volume trim (setGain('chain')
            # multiplier). Range 0–5, default 1× (the knob value IS the multiplier).
            "chain_makeup": float(s.get("chain_makeup", 1.0)),
            "final_chain_normalize": bool(s.get("final_chain_normalize", True)),
            "final_chain_target_rms_db": float(s.get("final_chain_target_rms_db", -14.0)),
            "final_chain_min_gain_db": float(s.get("final_chain_min_gain_db", -20.0)),
            "final_chain_max_gain_db": float(s.get("final_chain_max_gain_db", 20.0)),
            "final_chain_gate_db": float(s.get("final_chain_gate_db", -45.0)),
            "final_chain_attack_ms": int(min(float(s.get("final_chain_attack_ms", 12)), 80.0)),
            "final_chain_release_ms": int(min(float(s.get("final_chain_release_ms", 120)), 250.0)),
            "has_tone3000_key": bool(key),
            "tone3000_api_key_preview": (key[:6] + "…") if key else "",
            "tone3000_connected": bool(s.get("tone3000_access_token")),
            "tone3000_username": s.get("tone3000_username") or "",
        }

    @app.post("/api/plugins/rig_builder/settings")
    def update_settings(data: dict = Body(...)):
        # Only persist known keys to avoid junk accumulating in the file.
        # `preferred_size` is restricted to the 4 valid sizes — anything else
        # falls back to "standard" so a typo can't break model picking.
        allowed = {k: data[k] for k in ("tone3000_api_key", "min_downloads", "aggressive") if k in data}
        if "curated_only" in data:
            allowed["curated_only"] = bool(data["curated_only"])
        if "preferred_size" in data:
            size = str(data["preferred_size"]).strip().lower()
            if size in ("standard", "lite", "feather", "nano"):
                allowed["preferred_size"] = size
        if "mega_chain_mode" in data:
            allowed["mega_chain_mode"] = bool(data["mega_chain_mode"])
        if "default_tone_enabled" in data:
            allowed["default_tone_enabled"] = bool(data["default_tone_enabled"])
        if "rig_builder_enabled" in data:
            allowed["rig_builder_enabled"] = bool(data["rig_builder_enabled"])
        if "nam_chain_input_drive" in data:
            try:
                v = float(data["nam_chain_input_drive"])
                # Input-chain (amp drive) knob: range 0–5, default 1× (no boost).
                allowed["nam_chain_input_drive"] = max(0.0, min(5.0, v))
            except (TypeError, ValueError):
                pass
        if "tone_override_enabled" in data:
            allowed["tone_override_enabled"] = bool(data["tone_override_enabled"])
        if "tone_override_name" in data:
            allowed["tone_override_name"] = str(data["tone_override_name"])[:200]
        if "nam_input_calibration" in data:
            try:
                # Clean input-level trim (the "Input" fader / Calibration Wizard's
                # −12 dBFS result). Linear ×, applied on top of the amp drive. Range
                # covers the fader (−24..+12 dB ≈ 0.063..3.98×) and the wizard's
                # 0.1..5 clamp; default 1× = no trim.
                allowed["nam_input_calibration"] = max(0.05, min(5.0, float(data["nam_input_calibration"])))
            except (TypeError, ValueError):
                pass
        if "chain_makeup" in data:
            try:
                # Output-chain / chain-volume trim — multiplies the auto chain-gain
                # target (setGain('chain',X), the only level the engine respects).
                # Range 0–5, default 1×.
                allowed["chain_makeup"] = max(0.0, min(5.0, float(data["chain_makeup"])))
            except (TypeError, ValueError):
                pass
        if "final_chain_normalize" in data:
            allowed["final_chain_normalize"] = bool(data["final_chain_normalize"])

        for key in (
            "final_chain_target_rms_db",
            "final_chain_min_gain_db",
            "final_chain_max_gain_db",
            "final_chain_gate_db",
        ):
            if key in data:
                try:
                    allowed[key] = float(data[key])
                except (TypeError, ValueError):
                    pass

        for key in ("final_chain_attack_ms", "final_chain_release_ms"):
            if key in data:
                try:
                    hi = 80 if key == "final_chain_attack_ms" else 250
                    allowed[key] = max(1, min(hi, int(data[key])))
                except (TypeError, ValueError):
                    pass
        if "bypass_all_cabs" in data:
            want = bool(data["bypass_all_cabs"])
            allowed["bypass_all_cabs"] = want
            # Only rewrite the DB when the toggle actually flips, so changing
            # an unrelated setting doesn't clobber a manual per-cab bypass.
            if want != _load_settings().get("bypass_all_cabs", False):
                conn = _get_conn()
                with _lock:
                    conn.execute("UPDATE preset_pieces SET bypassed=? WHERE slot='cabinet'",
                                 (1 if want else 0,))
                    conn.commit()
        _save_settings(allowed)
        return {"ok": True}

    # ── OAuth (Connect with tone3000) ─────────────────────────────────
    # The PKCE loopback flow for native apps: the UI opens the authorize
    # URL in the system browser, the user logs in, tone3000 redirects back
    # to this local server's /oauth/callback, and we exchange the code for
    # tokens. No secret key ever touches the user's machine.

    @app.get("/api/plugins/rig_builder/oauth/start")
    def oauth_start(origin: str = ""):
        client = _get_t3k_client()
        verifier, challenge = client.generate_pkce()
        state = secrets.token_urlsafe(24)
        # redirect_uri returns the browser to THIS local server. The UI
        # passes window.location.origin so we match whatever port uvicorn
        # bound (default 127.0.0.1:18000, but findPort() may bump it).
        # Hard-restricted to loopback — see _safe_loopback_redirect.
        redirect_uri = _safe_loopback_redirect(origin)
        now = time.time()
        with _oauth_lock:
            for k in [k for k, v in _oauth_pending.items()
                      if now - v["created"] > _OAUTH_PENDING_TTL]:
                _oauth_pending.pop(k, None)
            _oauth_pending[state] = {
                "verifier": verifier,
                "redirect_uri": redirect_uri,
                "created": now,
            }
        url = client.build_authorize_url(redirect_uri, challenge, state)
        return {"authorize_url": url, "redirect_uri": redirect_uri}

    @app.get("/api/plugins/rig_builder/oauth/callback")
    def oauth_callback(code: str = "", state: str = "", error: str = ""):
        if error:
            return HTMLResponse(_oauth_result_page(
                f"tone3000 reported: {error}. You can close this tab and try again.",
                ok=False))
        with _oauth_lock:
            pending = _oauth_pending.pop(state, None)
        if not pending or not code:
            return HTMLResponse(_oauth_result_page(
                "This sign-in link expired or was already used. "
                "Return to feedBack and click Connect again.", ok=False))
        client = _get_t3k_client()
        try:
            client.exchange_code(code, pending["verifier"], pending["redirect_uri"])
        except Exception as e:
            log.warning("oauth token exchange failed", exc_info=True)
            return HTMLResponse(_oauth_result_page(
                f"Token exchange failed: {e}", ok=False))
        # exchange_code applied + persisted the tokens via on_tokens. Grab
        # the username for display (best-effort).
        username = ""
        try:
            user = client.get_user() or {}
            username = user.get("username") or ""
        except Exception:
            pass
        _persist_tokens({"tone3000_username": username})
        # Rebuild the cached client so subsequent requests pick up the new
        # tokens (they were applied in-place, but resetting is harmless and
        # keeps the path identical to a settings change).
        global _t3k_client
        with _t3k_lock:
            _t3k_client = None
        return HTMLResponse(_oauth_result_page(
            "You're connected to TONE3000. Close this tab and return to feedBack.",
            ok=True))

    @app.get("/api/plugins/rig_builder/oauth/status")
    def oauth_status():
        s = _load_settings()
        return {
            "connected": bool(s.get("tone3000_access_token")),
            "username": s.get("tone3000_username") or "",
            "has_secret": bool(s.get("tone3000_api_key")),
        }

    @app.post("/api/plugins/rig_builder/oauth/disconnect")
    def oauth_disconnect():
        _persist_tokens({
            "tone3000_access_token": "",
            "tone3000_refresh_token": "",
            "tone3000_token_expires_at": 0,
            "tone3000_username": "",
        })
        global _t3k_client
        with _t3k_lock:
            _t3k_client = None
        return {"ok": True}

    @app.get("/api/plugins/rig_builder/master_chain")
    def get_master_chain():
        """Return both halves of the global master chain (pre + post),
        enriched the same way as a per-tone chain. The UI's Master Chain
        tab uses this to render its two columns."""
        return {
            "pre":  _load_master_chain("pre"),
            "post": _load_master_chain("post"),
            "pre_preset_id":  _get_master_preset_id("pre"),
            "post_preset_id": _get_master_preset_id("post"),
        }

    @app.post("/api/plugins/rig_builder/master_chain/save")
    def save_master_chain(data: dict = Body(...)):
        """Persist one half of the master chain (role='pre' or 'post').
        Body: `{role, pieces: [...]}` — same piece shape /save_preset
        accepts. Replaces the whole half; client sends the full ordered
        list every time (just like rbPersistTone for a regular tone)."""
        role = (data.get("role") or "").strip()
        if role not in ("pre", "post"):
            return JSONResponse({"error": "role must be 'pre' or 'post'"}, 400)
        pieces = data.get("pieces") or []
        if not isinstance(pieces, list):
            return JSONResponse({"error": "pieces must be a list"}, 400)
        pid = _get_master_preset_id(role)
        if pid is None:
            return JSONResponse({"error": "could not get master preset"}, 500)
        name = _MASTER_PRESET_NAMES[role]
        try:
            preset_id = _persist_preset_chain(
                filename="__master__",
                tone_key=role,
                name=name,
                pieces=pieces,
                input_gain=1.0,
                output_gain=1.0,
                assigned_mode="master",
            )
        except Exception as e:
            log.exception("save_master_chain failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"ok": True, "preset_id": preset_id, "role": role,
                "piece_count": len(pieces)}

    @app.get("/api/plugins/rig_builder/default_tone")
    def get_default_tone():
        """Return the user's default tone: its chain (enriched pieces, same
        shape as a master half), the sentinel preset id, and the enabled
        flag. The Settings UI's Default Tone editor renders from this."""
        return {
            "pieces": _load_default_tone_chain(),
            "preset_id": _get_default_tone_preset_id(),
            "enabled": bool(_load_settings().get("default_tone_enabled", False)),
            "gate": _preset_gate(_get_default_tone_preset_id()),
        }

    @app.get("/api/plugins/rig_builder/tone_gate")
    def get_tone_gate(source: str = "", name: str = ""):
        """The per-tone noise gate for the tone the Studio is showing, resolved
        by identity (source + name). Used by the client to refresh the gate on
        every tone switch, independent of whether the audio monitor loads — so
        each tone's gate stays its own (not the last-loaded tone's)."""
        return {"gate": _preset_gate(_resolve_tone_preset_id(source, name))}

    @app.post("/api/plugins/rig_builder/tone_gate")
    def set_tone_gate(data: dict = Body(...)):
        """Persist ONLY the per-tone noise gate for the tone identified by
        {source, name} + a `gate` object — decoupled from chain saves so an
        audition re-save can't clobber it. 404 when the tone has no preset yet
        (the client saves the chain first, then retries)."""
        pid = _resolve_tone_preset_id(data.get("source", ""), data.get("name", ""))
        if pid is None:
            return JSONResponse({"error": "tone not saved yet"}, 404)
        gk = _gate_kwargs_from(data)
        cols = [c for c in ("gate_threshold", "gate_enabled", "gate_release", "gate_depth") if c in gk]
        if cols:
            sets = ", ".join(f"{c}=?" for c in cols)
            args = [gk[c] for c in cols] + [pid]
            with _lock:
                _get_conn().execute(f"UPDATE presets SET {sets} WHERE id=?", args)
                _get_conn().commit()
        return {"ok": True, "gate": _preset_gate(pid)}

    @app.post("/api/plugins/rig_builder/default_tone/save")
    def save_default_tone(data: dict = Body(...)):
        """Persist the default tone chain. Body: `{pieces: [...]}` — same
        piece shape save_master_chain/save_preset accept. Replaces the whole
        chain (client sends the full ordered list every time)."""
        pieces = data.get("pieces") or []
        if not isinstance(pieces, list):
            return JSONResponse({"error": "pieces must be a list"}, 400)
        pid = _get_default_tone_preset_id()
        if pid is None:
            return JSONResponse({"error": "could not get default tone preset"}, 500)
        try:
            preset_id = _persist_preset_chain(
                filename="__default_tone__",
                tone_key="default",
                name=_DEFAULT_TONE_PRESET_NAME,
                pieces=pieces,
                input_gain=1.0,
                output_gain=1.0,
                assigned_mode="default",
                **_gate_kwargs_from(data)
            )
        except Exception as e:
            log.exception("save_default_tone failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"ok": True, "preset_id": preset_id, "piece_count": len(pieces)}

    @app.get("/api/plugins/rig_builder/saved_tones")
    def list_saved_tones():
        """User-saved Studio tones: {tones:[{name, pieces}]}. Each is a preset
        named __rig_builder_saved_tone__<name> with its enriched chain."""
        conn = _get_conn()
        rows = conn.execute(
            "SELECT id, name FROM presets WHERE name LIKE ? ORDER BY id",
            (_SAVED_TONE_PREFIX + "%",),
        ).fetchall()
        out = []
        for pid, name in rows:
            out.append({"id": pid,   # so the Studio can load its native preset for live audition
                        "name": name[len(_SAVED_TONE_PREFIX):],
                        "pieces": _load_saved_chain(conn, pid) or []})
        return {"tones": out}

    @app.post("/api/plugins/rig_builder/saved_tone/save")
    def save_saved_tone(data: dict = Body(...)):
        """Save a Studio tone under a user name. Body: `{name, pieces}` (same
        piece shape as default_tone/save). Re-saving an existing name replaces."""
        name = (data.get("name") or "").strip()
        pieces = data.get("pieces") or []
        if not name:
            return JSONResponse({"error": "name required"}, 400)
        if not isinstance(pieces, list):
            return JSONResponse({"error": "pieces must be a list"}, 400)
        try:
            preset_id = _persist_preset_chain(
                filename="__saved_tone__",
                tone_key=name,
                name=_SAVED_TONE_PREFIX + name,
                pieces=pieces,
                input_gain=1.0, output_gain=1.0,
                assigned_mode="manual",
                **_gate_kwargs_from(data)
            )
        except Exception as e:
            log.exception("save_saved_tone failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"ok": True, "preset_id": preset_id, "name": name}

    @app.post("/api/plugins/rig_builder/saved_tone/delete")
    def delete_saved_tone(data: dict = Body(...)):
        name = (data.get("name") or "").strip()
        if not name:
            return JSONResponse({"error": "name required"}, 400)
        conn = _get_conn()
        try:
            row = conn.execute("SELECT id FROM presets WHERE name = ?",
                               (_SAVED_TONE_PREFIX + name,)).fetchone()
            if row:
                pid = row[0]
                # Drop the tone_mapping first — it references presets(id) and can
                # block the preset delete (FK), causing a 500.
                conn.execute("DELETE FROM tone_mappings WHERE preset_id = ?", (pid,))
                conn.execute("DELETE FROM preset_pieces WHERE preset_id = ?", (pid,))
                conn.execute("DELETE FROM presets WHERE id = ?", (pid,))
                conn.commit()
        except Exception as e:
            log.exception("delete_saved_tone failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"ok": True}

    @app.get("/api/plugins/rig_builder/default_tone/native")
    def default_tone_native():
        """Build the native_preset payload for the default tone using the
        same full-chain builder songs use — so it's wrapped with the master
        pre/post + final leveler. The frontend feeds this straight to the
        engine when no song is active. When nothing is assigned yet the
        chain is just the master wrap; the caller gates on the piece list
        from /default_tone and skips loading in that case."""
        pid = _get_default_tone_preset_id()
        if pid is None:
            return JSONResponse({"error": "could not get default tone preset"}, 500)
        return native_preset_full(pid)

    @app.get("/api/plugins/rig_builder/gears_catalog")
    def gears_catalog():
        """Return the full rs_to_real catalog as a flat list of choices the
        UI's chain editor uses to populate the 'Add piece' picker. Each
        entry has: rs_gear (the key), name (display), category (RS coarse
        bucket: amp/cab/pedal/rack/other), AND daw_category (DAW-style
        subcategory: amps/cabs/distortion/modulation/delay/reverb/comp/
        eq/wah/pitch/filter/utility/other) for finer-grained filtering in
        the picker. Sorted by daw_category → name."""
        rs_map = _load_rs_to_real()
        type_tags = _load_pedal_type_tags()
        out: list[dict] = []
        # Collapse gears that REUSE the same bundled VST (e.g. Amp_AT20 reusing
        # Bass_Amp_BT975B's Sampleg SBT-CL) so the picker lists each bundled
        # model once instead of two identical cards.
        seen_bundled: set[str] = set()
        for rs_gear, info in rs_map.items():
            if rs_gear.startswith("_") or not isinstance(info, dict):
                continue
            b = _gear_bundled_vst(rs_gear)
            if b:
                if b in seen_bundled:
                    continue
                seen_bundled.add(b)
            category = info.get("category") or _gear_category(rs_gear) or "other"
            out.append({
                "rs_gear": rs_gear,
                "name": _gear_display_name(rs_gear, info.get("name") or rs_gear),
                "make": info.get("make", ""),
                "model": info.get("model", ""),
                "category": category,
                "daw_category": _daw_category_for(rs_gear, category),
                "type_tags": type_tags.get(rs_gear, ""),
            })
        # User-defined custom gear — surface it in the per-song "Add piece"
        # picker too (same flat shape). daw_category maps the coarse category
        # to a picker bucket so it lands under the right tab.
        _daw_bucket = {"amp": "amps", "cab": "cabs", "pedal": "other", "rack": "other"}
        for rec in _load_custom_gear():
            cat = rec.get("category") or "other"
            out.append({
                "rs_gear": rec.get("rs_gear"),
                "name": rec.get("real_name") or rec.get("name") or rec.get("rs_gear"),
                "make": rec.get("make", ""),
                "model": rec.get("model", ""),
                "category": cat,
                "daw_category": _daw_bucket.get(cat, "other"),
                "type_tags": rec.get("type_tags", ""),
                "custom": True,
                # Source fields so the per-song "Add piece" picker can attach the
                # saved VST/NAM the moment a custom gear is added (no manual
                # re-assign). Absent for regular game gear.
                "kind": rec.get("kind"),
                "vst_path": rec.get("vst_path"),
                "vst_format": rec.get("vst_format"),
                "vst_state": rec.get("vst_state"),
                "file": rec.get("file"),
                # The custom UI layout so a piece added from the picker shows the
                # user's own face (not the plugin's native window) in the editor.
                "ui": rec.get("ui"),
            })
        out.sort(key=lambda g: (g["daw_category"], (g["name"] or "").lower()))
        return {"gears": out, "count": len(out)}

    # ── Custom (user-authored) gear: create / edit / delete ───────────

    @app.get("/api/plugins/rig_builder/custom_gear")
    def custom_gear_list():
        """List the user's custom gear (raw stored records)."""
        return {"gear": _load_custom_gear()}

    @app.post("/api/plugins/rig_builder/custom_gear")
    def custom_gear_save(data: dict = Body(...)):
        """Create or update a user-defined gear (amp/cab/pedal/rack) backed by a
        bundled/installed VST or a NAM/IR file, plus a serializable custom UI
        layout. Persisted to rig_builder_custom_gear.json; surfaced by
        /gear_catalog (browser + node-editor palette) and /gears_catalog
        (per-song picker). No preset_pieces rows are written — the gear only
        materializes into a chain when the user actually adds it to a tone.

        Body:
          rs_gear   optional — present ⇒ edit that record, absent ⇒ create
          category  required — amp | cab | pedal | rack
          name      required — display name
          instrument optional — guitar | bass | all (default all)
          kind      vst | nam | ir  (inferred from vst_path/file when omitted)
          vst_path, vst_format, vst_state  — when kind == vst
          file      — relative NAM/IR path (under nam_models/ or nam_irs/)
          ui        — the serializable canvas layout (opaque to the backend)
        """
        category = (data.get("category") or "").strip().lower()
        name = (data.get("name") or data.get("real_name") or "").strip()
        if category not in _CUSTOM_GEAR_CATEGORIES:
            return JSONResponse(
                {"error": f"category must be one of {_CUSTOM_GEAR_CATEGORIES}"}, 400)
        if not name:
            return JSONResponse({"error": "name is required"}, 400)

        vst_path = (data.get("vst_path") or "").strip()
        file = (data.get("file") or "").strip()
        if not vst_path and not file:
            return JSONResponse(
                {"error": "a source is required (vst_path or file)"}, 400)
        kind = (data.get("kind") or "").strip().lower()
        if kind not in ("vst", "nam", "ir"):
            kind = "vst" if vst_path else ("ir" if category == "cab" else "nam")

        instrument = (data.get("instrument") or "all").strip().lower()
        if instrument not in ("guitar", "bass", "all"):
            instrument = "all"

        items = _load_custom_gear()
        rs_gear = (data.get("rs_gear") or "").strip()
        existing = None
        if rs_gear:
            existing = next((g for g in items if g.get("rs_gear") == rs_gear), None)
        if not rs_gear:
            # Stable, collision-resistant synthetic id.
            rs_gear = f"custom_{category}_{int(time.time()*1000)}_{secrets.token_hex(2)}"

        # Amp gain variants (clean/crunch/dist → NAM file + gain range): playback
        # auto-picks the level whose range holds the song's mapped Gain (consumed
        # by _pick_amp_gain_variant — same shape the auto-tone3000 flow builds).
        # Only kept for amps and only when ≥2 levels are provided; clean becomes
        # the default file/kind so single-NAM playback still works.
        gv_in = data.get("gain_variants")
        gain_variants = None
        if category == "amp" and isinstance(gv_in, dict):
            gv_out: dict[str, dict] = {}
            for lvl in ("clean", "crunch", "dist"):
                e = gv_in.get(lvl)
                f = (e.get("file") if isinstance(e, dict) else None) or None
                if not f:
                    continue
                rng = e.get("rs_gain_range") if isinstance(e, dict) else None
                if not (isinstance(rng, (list, tuple)) and len(rng) == 2):
                    rng = _DEFAULT_GAIN_RANGES.get(lvl)
                gv_out[lvl] = {
                    "file": str(f),
                    "rs_gain_range": [float(rng[0]), float(rng[1])] if rng else None,
                }
            if len(gv_out) >= 2:
                gain_variants = gv_out
                kind = "nam"
                clean_file = (gv_out.get("clean") or {}).get("file")
                if clean_file:
                    file = clean_file

        rec = {
            "rs_gear": rs_gear,
            "category": category,
            "real_name": name,
            "instrument": instrument,
            "kind": kind,
            "vst_path": vst_path or None,
            "vst_format": (data.get("vst_format") or "VST3") if vst_path else None,
            "vst_state": data.get("vst_state") if vst_path else None,
            "file": file or None,
            "gain_variants": gain_variants,
            "make": (data.get("make") or "").strip(),
            "model": (data.get("model") or "").strip(),
            "type_tags": (data.get("type_tags") or "").strip(),
            "ui": data.get("ui") or None,
            "custom": True,
        }
        if existing is not None:
            existing.update(rec)
        else:
            items.append(rec)
        try:
            _save_custom_gear(items)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)
        return {"ok": True, "gear": _custom_gear_catalog_item(rec)}

    @app.post("/api/plugins/rig_builder/custom_gear_gain_ranges")
    def custom_gear_gain_ranges(data: dict = Body(...)):
        """Update the switch-point thresholds of a multi-capture (auto tone3000)
        gear. Body {rs_gear, ranges: {level: [lo, hi]}} in game-Gain units 0..100.
        Playback picks the level whose range contains the song's mapped Gain."""
        gear_id = (data.get("rs_gear") or "").strip()
        ranges = data.get("ranges") or {}
        items = _load_custom_gear()
        rec = next((g for g in items if g.get("rs_gear") == gear_id), None)
        if not rec or not isinstance(rec.get("gain_variants"), dict):
            return JSONResponse({"error": "not a multi-capture gear"}, 404)
        gv = rec["gain_variants"]
        for level, rng in ranges.items():
            if (level in gv and isinstance(gv[level], dict)
                    and isinstance(rng, (list, tuple)) and len(rng) == 2):
                try:
                    gv[level]["rs_gain_range"] = [float(rng[0]), float(rng[1])]
                except (TypeError, ValueError):
                    pass
        try:
            _save_custom_gear(items)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)
        return {"ok": True, "gain_variants": gv}

    @app.delete("/api/plugins/rig_builder/custom_gear/{rs_gear}")
    def custom_gear_delete(rs_gear: str):
        """Remove a custom gear by its rs_gear id."""
        items = _load_custom_gear()
        kept = [g for g in items if g.get("rs_gear") != rs_gear]
        if len(kept) == len(items):
            return JSONResponse({"error": "not found"}, 404)
        try:
            _save_custom_gear(kept)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)
        return {"ok": True, "removed": rs_gear}

    # ── Gear mapping: game (RS) gear → a user custom gear + knob map ───

    def _assign_vst_to_gear(rs_gear: str, vst_path: str, vst_format: str,
                            vst_state) -> int:
        """Point every preset_pieces row for a game gear at a VST (mirrors
        /vst/assign). Used when mapping a game gear to a custom gear so it plays
        that custom gear's plugin."""
        conn = _get_conn()
        with _lock:
            cur = conn.execute(
                "UPDATE preset_pieces SET kind='vst', file=NULL, "
                "  vst_path=?, vst_format=?, vst_state=?, assigned_mode='manual_vst' "
                "WHERE rs_gear_type = ?",
                (vst_path, vst_format, vst_state, rs_gear),
            )
            n = cur.rowcount
            affected = [r[0] for r in conn.execute(
                "SELECT DISTINCT preset_id FROM preset_pieces WHERE rs_gear_type = ?",
                (rs_gear,)).fetchall()]
            for pid in affected:
                _recompute_preset_primaries(conn, pid)
            conn.commit()
        return n

    @app.get("/api/plugins/rig_builder/gear_map/{rs_gear}")
    def gear_map_get(rs_gear: str):
        """Everything the game gear's detail needs: its current mapping, the
        selectable custom gears (SAME category only), the gear's RS knobs, and
        the mapped custom gear's params."""
        rs_gear = (rs_gear or "").strip()
        gmap = _load_gear_map()
        entry = gmap.get(rs_gear) or {}
        info = _load_rs_to_real().get(rs_gear) or {}
        category = info.get("category") or _gear_category(rs_gear) or "amp"
        custom = _load_custom_gear()
        # Only custom gear of the SAME category is offered.
        options = [
            {"rs_gear": c.get("rs_gear"),
             "name": c.get("real_name") or c.get("rs_gear"),
             "params": _custom_gear_params(c)}
            for c in custom if (c.get("category") or "") == category
        ]
        mapped_to = entry.get("custom")
        mapped_rec = next((c for c in custom if c.get("rs_gear") == mapped_to), None)
        return {
            "rs_gear": rs_gear,
            "category": category,
            "rs_knobs": _rs_knobs_for_gear(rs_gear),
            "options": options,
            "mapped_to": mapped_to if mapped_rec else None,
            "mapped_name": (mapped_rec.get("real_name") if mapped_rec else None),
            "mapped_params": (_custom_gear_params(mapped_rec) if mapped_rec else []),
            "params": entry.get("params") or {},
        }

    @app.post("/api/plugins/rig_builder/gear_map")
    def gear_map_save(data: dict = Body(...)):
        """Set (or CLEAR) what plays a game gear in songs — a pure REDIRECT that
        never changes the gear's own identity. custom='' ⇒ back to itself (native).

        Body: { rs_gear, custom (''=native), params {rs_knob: param} }
        """
        rs_gear = (data.get("rs_gear") or "").strip()
        custom_id = (data.get("custom") or "").strip()
        params = data.get("params") or {}
        if not rs_gear:
            return JSONResponse({"error": "rs_gear required"}, 400)
        gmap = _load_gear_map()
        if custom_id:
            if not any(c.get("rs_gear") == custom_id for c in _load_custom_gear()):
                return JSONResponse({"error": "custom gear not found"}, 404)
            gmap[rs_gear] = {"custom": custom_id,
                             "params": {k: v for k, v in params.items() if v}}
        else:
            gmap.pop(rs_gear, None)   # native
        try:
            _save_gear_map(gmap)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)
        # Keep the gear's own identity intact (restore its native bundled VST);
        # the redirect is applied only at play-chain build.
        try:
            prim = _pick_installed_primary_vst(rs_gear, _build_known_vst_lookup())
            if prim and prim.get("vst_path"):
                _assign_vst_to_gear(rs_gear, prim["vst_path"],
                                    prim.get("vst_format") or "VST3",
                                    _compute_vst_state_for_piece(rs_gear, prim["vst_path"], {}))
        except Exception:   # noqa: BLE001
            log.warning("gear_map: restore native failed for %s", rs_gear, exc_info=True)
        return {"ok": True, "mapped_to": custom_id or None}

    @app.delete("/api/plugins/rig_builder/gear_map/{rs_gear}")
    def gear_map_delete(rs_gear: str):
        gmap = _load_gear_map()
        if rs_gear in gmap:
            gmap.pop(rs_gear, None)
            try:
                _save_gear_map(gmap)
            except Exception as e:  # noqa: BLE001
                return JSONResponse({"error": f"could not save: {e}"}, 500)
        return {"ok": True, "removed": rs_gear}

    # ── Assignment from the REAL gear's side: a custom gear covers game gears ──

    def _rs_gear_options(category: str) -> list[dict]:
        """The game (RS) gears of a category a real gear can be assigned to —
        shown by their COPYRIGHT-FREE display name (never the RS codename), each
        with its RS knob list (for the knob→param mapping)."""
        rs_map = _load_rs_to_real()
        out = []
        for g, info in rs_map.items():
            if str(g).startswith("_") or not isinstance(info, dict):
                continue
            cat = info.get("category") or _gear_category(g)
            if cat != category:
                continue
            out.append({
                "rs_gear": g,
                "name": _gear_display_name(g, info.get("name") or g),
                "knobs": _rs_knobs_for_gear(g),
            })
        out.sort(key=lambda x: (x["name"] or "").lower())
        return out

    def _gear_category_of(gear_id: str) -> str:
        rec = next((c for c in _load_custom_gear() if c.get("rs_gear") == gear_id), None)
        if rec:
            return rec.get("category") or "amp"
        info = _load_rs_to_real().get(gear_id) or {}
        return info.get("category") or _gear_category(gear_id) or "amp"

    @app.get("/api/plugins/rig_builder/custom_assignments/{gear_id}")
    def custom_assignments_get(gear_id: str):
        """Assignment panel data for ANY gear (native OR custom — treated the
        same): its params, the assignable game gears (same category, copyright-
        free names), and which game gears it currently covers + their knob maps.
        A NATIVE gear covers its OWN slot by default."""
        category = _gear_category_of(gear_id)
        gmap = _load_gear_map()
        options = _rs_gear_options(category)
        by_rs = {o["rs_gear"]: o for o in options}
        assigned = {}
        for rs_gear, entry in gmap.items():
            if isinstance(entry, dict) and entry.get("custom") == gear_id:
                o = by_rs.get(rs_gear) or {"name": _gear_display_name(rs_gear, rs_gear),
                                           "knobs": _rs_knobs_for_gear(rs_gear)}
                assigned[rs_gear] = {"name": o["name"], "knobs": o["knobs"],
                                     "params": entry.get("params") or {},
                                     "self": rs_gear == gear_id}
        # Native gear covers its OWN slot unless that slot was redirected away —
        # pre-filled with its shipped default knob mapping so it's not blank.
        if not _is_custom_gear(gear_id) and gear_id not in gmap and gear_id in by_rs:
            o = by_rs[gear_id]
            assigned[gear_id] = {"name": o["name"], "knobs": o["knobs"],
                                 "params": _native_default_knobmap(gear_id), "self": True}
        return {
            "custom": gear_id,
            "category": category,
            "custom_params": _gear_params(gear_id),
            "options": options,
            "assigned": assigned,
        }

    @app.post("/api/plugins/rig_builder/custom_assignments")
    def custom_assignments_save(data: dict = Body(...)):
        """Set which game gears a gear (native OR custom) covers. Pure REDIRECT
        (gear_map): a gear covering slot X ⇒ songs on X play this gear. A gear
        covering its OWN slot is the native default (no entry stored).

        Body: { custom: <gear_id>, assigned: { <rs_gear>: { <rs_knob>: <param> } } }
        """
        gear_id = (data.get("custom") or "").strip()
        assigned = data.get("assigned") or {}
        if not gear_id:
            return JSONResponse({"error": "gear id required"}, 400)

        gmap = _load_gear_map()
        removed = [k for k, v in gmap.items()
                   if isinstance(v, dict) and v.get("custom") == gear_id and k not in assigned]
        for rs_gear in removed:
            gmap.pop(rs_gear, None)
        for rs_gear, params in assigned.items():
            clean = {k: v for k, v in (params or {}).items() if v}
            # Own slot with NO custom knob overrides = native default → no entry
            # (keeps the map clean; Reset just removes any entry).
            if rs_gear == gear_id and not clean:
                gmap.pop(rs_gear, None)
                continue
            gmap[rs_gear] = {"custom": gear_id, "params": clean}
        try:
            _save_gear_map(gmap)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)

        # The map is a pure REDIRECT (applied at play-chain build in
        # native_preset_full) — it does NOT change any gear's identity. Restore
        # every touched game gear's NATIVE bundled VST in preset_pieces (repairs
        # the earlier overwrite bug: a gear now always "points to itself" again;
        # the redirect swaps it only during playback).
        known = _build_known_vst_lookup()
        for rs_gear in set(list(assigned.keys()) + removed):
            try:
                prim = _pick_installed_primary_vst(rs_gear, known)
                if prim and prim.get("vst_path"):
                    _assign_vst_to_gear(
                        rs_gear, prim["vst_path"], prim.get("vst_format") or "VST3",
                        _compute_vst_state_for_piece(rs_gear, prim["vst_path"], {}))
            except Exception:   # noqa: BLE001
                log.warning("restore native VST failed for %s", rs_gear, exc_info=True)
        return {"ok": True, "assigned_count": len(assigned)}

    @app.post("/api/plugins/rig_builder/gear_reset")
    def gear_reset(data: dict = Body(...)):
        """Reset a NATIVE gear to its factory state: drop every redirect where
        it's the target (incl. its own slot), so it covers ONLY itself again with
        its shipped default knob mapping."""
        gear_id = (data.get("gear") or "").strip()
        if not gear_id:
            return JSONResponse({"error": "gear required"}, 400)
        gmap = _load_gear_map()
        touched = [s for s, v in gmap.items()
                   if isinstance(v, dict) and v.get("custom") == gear_id]
        for s in touched:
            gmap.pop(s, None)
        gmap.pop(gear_id, None)   # its own slot back to native
        try:
            _save_gear_map(gmap)
        except Exception as e:      # noqa: BLE001
            return JSONResponse({"error": f"could not save: {e}"}, 500)
        known = _build_known_vst_lookup()
        for rs_gear in set(touched + [gear_id]):
            try:
                prim = _pick_installed_primary_vst(rs_gear, known)
                if prim and prim.get("vst_path"):
                    _assign_vst_to_gear(rs_gear, prim["vst_path"],
                                        prim.get("vst_format") or "VST3",
                                        _compute_vst_state_for_piece(rs_gear, prim["vst_path"], {}))
            except Exception:   # noqa: BLE001
                log.warning("gear_reset restore failed for %s", rs_gear, exc_info=True)
        return {"ok": True}

    # ── Song / chain inspection ───────────────────────────────────────

    def _auto_fix_cab_mics_for_song(filename: str, raw_tones: list,
                                      conn) -> int:
        """Auto-promote generic "Cabinets" rs_gear and re-pick mic IRs
        when a song is opened. Runs synchronously inside get_song so the
        chain rendered to the user always reflects the corrected cab.

        For each parsed tone:
          - Read its Cabinet.Key from the PSARC (e.g. "Bass_Cab_AT810BC_57_Cone")
          - Resolve to (base_rs_gear, mic_suffix) via the HIRC-derived
            rs_cab_mic_map.
          - Find the preset_piece for THIS tone whose rs_gear_type is
            either the resolved base, the generic "Cabinets" placeholder,
            or a stale base-form (case-insensitive).
          - If the row's current rs_gear / file doesn't match what
            Cabinet.Key says it should be → UPDATE.

        Preserves real manual picks: rows with assigned_mode='manual'
        AND a real (non-generic) rs_gear_type stay untouched. The
        generic-Cabinets rows ARE force-fixed regardless of mode
        because the UI never lets the user pick "Cabinets" deliberately
        — it's always a parser-miss placeholder.

        Returns the number of rows updated (mostly for diagnostics —
        not surfaced to the user; the corrected state shows up in the
        re-rendered chain).
        """
        if _config_dir is None or not raw_tones:
            return 0
        irs_root = _config_dir / "nam_irs"
        _filename_filter, _filename_args = _tone_mapping_filename_filter(filename)
        tm_rows = conn.execute(
            "SELECT tone_key, preset_id FROM tone_mappings "
            f"WHERE {_filename_filter} "
            "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)",
            _filename_args,
        ).fetchall()
        if not tm_rows:
            return 0
        tone_to_preset = {tk: pid for tk, pid in tm_rows}

        updated = 0
        with _lock:
            for raw in raw_tones:
                tone_key = raw.get("Key") or raw.get("Name") or ""
                preset_id = tone_to_preset.get(tone_key)
                if preset_id is None:
                    continue
                cab = (raw.get("GearList") or {}).get("Cabinet") or {}
                effect_name = (cab.get("Key") or "").strip()
                if not effect_name:
                    continue
                resolved = _resolve_cab_for_effect(effect_name, irs_root)
                if not resolved:
                    continue
                base_rs_gear, new_file = resolved
                # Find the cab preset_piece for this preset. Match by
                # category-ish criteria: rs_gear is "Cabinets", or
                # case-insensitively equals the resolved base, or a
                # stale variant of it.
                base_lc = base_rs_gear.lower()
                rows = conn.execute(
                    "SELECT id, rs_gear_type, file, assigned_mode "
                    "FROM preset_pieces "
                    "WHERE preset_id = ? "
                    "  AND (rs_gear_type = 'Cabinets' "
                    "       OR LOWER(rs_gear_type) = ? "
                    "       OR LOWER(rs_gear_type) LIKE ?)",
                    (preset_id, base_lc, base_lc + "_%"),
                ).fetchall()
                for row_id, rs_gear, cur_file, mode in rows:
                    is_generic = (rs_gear == "Cabinets")
                    # Skip real manual picks. Generic-Cabinets rows are
                    # always force-fixed (parser-miss, not deliberate).
                    if mode == "manual" and not is_generic:
                        continue
                    needs_promote = (rs_gear != base_rs_gear)
                    if not needs_promote and cur_file == new_file:
                        continue
                    if needs_promote:
                        conn.execute(
                            "UPDATE preset_pieces "
                            "SET rs_gear_type = ?, file = ?, kind = 'rs_ir' "
                            "WHERE id = ?",
                            (base_rs_gear, new_file, row_id),
                        )
                    else:
                        conn.execute(
                            "UPDATE preset_pieces "
                            "SET file = ?, kind = 'rs_ir' WHERE id = ?",
                            (new_file, row_id),
                        )
                    updated += 1
            if updated:
                # Recompute primary IR file on every preset we touched.
                affected = {tone_to_preset[k] for k in tone_to_preset
                             if tone_to_preset[k] is not None}
                for pid in affected:
                    _recompute_preset_primaries(conn, pid)
                conn.commit()
        return updated

    @app.get("/api/plugins/rig_builder/song/{filename:path}")
    def get_song(filename: str):
        path = _resolve_song_file(filename)
        if path is None:
            return JSONResponse({"error": "file not found inside DLC dir"}, 404)
        song_key = _db_song_key(filename, path)

        # 0-byte placeholder = cloud_loader stub with no data yet. Return a
        # structured signal so the UI can call cloud_loader/materialize before
        # retrying. Status 409 (Conflict) is the closest fit — the resource
        # exists in the index but isn't ready to be read. A stub whose data is
        # already unpacked in sloppak_cache is readable, so don't 409 on it.
        if not _song_data_available(path):
            return JSONResponse(
                {"error": "cloud_only", "filename": filename,
                 "hint": "POST /api/cloud_loader/materialize?filename=... then retry"},
                409,
            )

        try:
            if _is_song_pack(path):
                raw_tones = _read_tones_from_sloppak(song_key, _get_dlc_dir())
            elif path.suffix.lower() == ".psarc":
                return JSONResponse(
                    {"error": "psarc_unsupported", "filename": filename,
                     "hint": "Rig Builder reads .sloppak / .feedpak songs only. "
                             "Convert this .psarc first, then reopen it."},
                    400,
                )
            else:
                return JSONResponse({"error": "unsupported file type"}, 400)
        except ValueError as e:
            return JSONResponse({"error": f"unreadable file: {e}"}, 400)
        except Exception as e:
            log.exception("get_song failed for %s", filename)
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)

        # Map tone_key → existing nam_tone mapping (if any), so the UI
        # can show which tones already have a preset and which don't.
        conn = _get_conn()
        # Self-healing cab assignment: every song open re-evaluates
        # each cab piece against its PSARC's Cabinet.Key. Updates
        # rows whose rs_gear_type='Cabinets' (parser-miss placeholder)
        # or whose mic IR doesn't match what the game actually
        # specified for the tone. Real manual picks (manual mode + real
        # rs_gear) stay protected. Idempotent — already-correct rows
        # are no-ops, so re-opening a fixed song doesn't churn.
        try:
            _auto_fix_cab_mics_for_song(song_key, raw_tones, conn)
        except Exception:
            log.exception("auto cab mic fix failed for %s", song_key)
        # Same self-heal for non-cab generics: promote any 'Amps'/'Pedals'/
        # 'Racks' parser-miss rows to the real rs_gear from the GearList.
        try:
            _promote_generic_gear_for_song_module(song_key, raw_tones)
        except Exception:
            log.exception("auto generic-gear promote failed for %s", song_key)
        _filename_filter, _filename_args = _tone_mapping_filename_filter(song_key, path)
        existing_rows = conn.execute(
            f"SELECT tone_key, preset_id FROM tone_mappings WHERE {_filename_filter} "
            "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tone_mappings.preset_id)",
            _filename_args,
        ).fetchall()
        existing_by_key = {r[0]: r[1] for r in existing_rows}

        _img_idx = _tone_image_index()
        tones: list[dict] = []
        for raw in raw_tones:
            parsed = _parse_tone(raw)
            preset_id = existing_by_key.get(parsed["key"])
            # If the user has a saved chain for this tone, that's the
            # source of truth — it can have pieces added by the user,
            # original PSARC pieces removed, or any custom slot_order.
            # Fall back to the PSARC's GearList only for tones the user
            # has never touched (no preset yet).
            saved_chain = _load_saved_chain(conn, preset_id, _img_idx) if preset_id is not None else None
            if saved_chain:
                enriched_chain = saved_chain
            else:
                enriched_chain = [_enrich_chain_piece(p, _img_idx) for p in parsed["chain"]]
                # Persisted per-piece bypass, scoped to THIS tone's preset so a
                # bypass saved in another song doesn't bleed in.
                if preset_id is not None:
                    bypass_map = {
                        r[0]: bool(r[1]) for r in conn.execute(
                            "SELECT rs_gear_type, bypassed FROM preset_pieces WHERE preset_id = ?",
                            (preset_id,),
                        )
                    }
                    for piece in enriched_chain:
                        piece["bypassed"] = bypass_map.get(piece.get("type"), False)
            tones.append({
                "name": parsed["name"],
                "key": parsed["key"],
                "chain": enriched_chain,
                "preset_id": preset_id,
                # When the chain is coming from preset_pieces (user-edited),
                # tell the UI so it can show "edited" badge / skip the
                # PSARC-defaults explainer.
                "chain_source": "edited" if saved_chain else "psarc",
            })
        return {"filename": filename, "tones": tones}

    @app.get("/api/plugins/rig_builder/search")
    def search(rs_gear: str, limit: int = 10, query_override: str = "", gears_override: str = ""):
        """Search tone3000 for a specific the game gear. Always returns
        a deep-link URL; the candidates list is populated only when the
        API key is set up.

        `query_override` and `gears_override` let the UI re-run the
        search with the user's own guess at the right query when the
        rs_to_real.json mapping turns out to be wrong (e.g. CS-series
        bass amps that don't get a brand match). The override is
        request-scoped — the persisted mapping isn't changed unless
        the user explicitly saves it via /override_query.
        """
        rs_map = _load_rs_to_real()
        info = rs_map.get(rs_gear) or {}
        category = _gear_category(rs_gear)
        query = query_override.strip() or info.get("tone3000_query") or rs_gear
        gears = gears_override.strip() or info.get("tone3000_gears") or ""
        platform = _PLATFORM_FOR_CATEGORY.get(category, "nam")

        from rb_core.tone3000_client import Tone3000Client
        deep_link = Tone3000Client.build_search_url(query, gears=gears or None, platform=platform)

        client = _get_t3k_client()
        candidates: list[dict] = []
        if client.has_api_access:
            try:
                resp = client.search_tones(query, gears=gears or None, platform=platform, page_size=min(limit, 25))
                if isinstance(resp, dict):
                    for t in resp.get("data", []):
                        tid = t.get("id")
                        candidates.append({
                            "id": tid,
                            "title": t.get("title"),
                            # Build from the id — the API's own `url`
                            # field returns a 404ing slug (see
                            # Tone3000Client.tone_page_url).
                            "url": Tone3000Client.tone_page_url(tid) if tid is not None else "",
                            "license": t.get("license"),
                            "downloads_count": t.get("downloads_count"),
                            "favorites_count": t.get("favorites_count"),
                            "images": t.get("images"),
                        })
            except Exception:
                log.warning("search failed for %s", rs_gear, exc_info=True)

        return {
            "rs_gear": rs_gear,
            "query": query,
            "gears": gears,
            "platform": platform,
            "deep_link": deep_link,
            "candidates": candidates,
            "has_api_access": client.has_api_access,
        }

    # ── Per-gear query override (persist user discoveries) ──────────

    @app.post("/api/plugins/rig_builder/override_query")
    def override_query(data: dict = Body(...)):
        """Persist a user-supplied tone3000 query/gears override for
        one rs_gear into rs_to_real.json.

        Use case: rs_to_real.json's auto-generated query for a
        pseudonym amp (e.g. `Bass_Amp_CS75B` → "Cs 75B") returns 0
        candidates from tone3000. The user finds the actual real-world
        model by editing the query in the Sugerir modal ("Ampeg SVT")
        and clicks "Guardar como override" — we patch the JSON so
        future batches and lookups use the corrected query.

        Body: `{rs_gear, query, gears?, platform?}`. `query` is
        required; `gears` and `platform` are optional and only
        written if non-empty (so partial overrides don't clobber
        the category-derived defaults).
        """
        rs_gear = (data.get("rs_gear") or "").strip()
        new_query = (data.get("query") or "").strip()
        new_gears = (data.get("gears") or "").strip()
        if not rs_gear or not new_query:
            return JSONResponse({"error": "rs_gear and query required"}, 400)
        # Work on a FRESH read under the shared lock — never mutate the
        # dict `_load_rs_to_real()` caches (other threads read it live),
        # and only invalidate the cache after a successful write.
        with _rs_map_lock:
            path = _data_path("rs_to_real.json")
            try:
                rs_map = json.loads(path.read_text(encoding="utf-8"))
            except (FileNotFoundError, json.JSONDecodeError, OSError,
                    UnicodeDecodeError):
                log.exception("override_query: rs_to_real.json unreadable")
                return JSONResponse(
                    {"error": "rs_to_real.json is missing or unreadable"}, 500)
            if not isinstance(rs_map.get(rs_gear), dict):
                return JSONResponse(
                    {"error": f"rs_gear {rs_gear} not present in rs_to_real.json"},
                    404,
                )
            rs_map[rs_gear]["tone3000_query"] = new_query
            if new_gears:
                rs_map[rs_gear]["tone3000_gears"] = new_gears
            _atomic_write_json(path, rs_map, sort_keys=True)
            _invalidate_rs_to_real()
        # The tone3000 client caches each search URL for 7 days. Our
        # override changes the URL so the new query naturally misses
        # the cache — no explicit invalidation needed.
        return {"ok": True, "rs_gear": rs_gear, "query": new_query, "gears": new_gears}

    # ── v3: per-gear manual auto-download (used from the Sugerir modal) ─

    @app.post("/api/plugins/rig_builder/use_local_for_gear")
    def use_local_for_gear(data: dict = Body(...)):
        """Bulk-assign a file already present in nam_models/ or nam_irs/
        to every preset_pieces row for a given rs_gear_type. Mirrors the
        post-download path of /download_for_gear (calls
        _assign_file_to_gear), but skips the tone3000 round-trip — the
        file is already local.

        Body: `{rs_gear, local_file, local_kind}` where
          - rs_gear:    e.g. "Amp_TW22"
          - local_file: relative path under the kind's root
                        (e.g. "Plexi 51.nam" or "rocksmith/cab_4x12_x.wav")
          - local_kind: "nam" | "ir" | "rs_ir"

        Returns `{ok, pieces_updated, presets_updated, kind, file}`.
        """
        rs_gear = (data.get("rs_gear") or "").strip()
        local_file = (data.get("local_file") or "").strip()
        local_kind = (data.get("local_kind") or "").strip().lower()
        if not rs_gear or not local_file or not local_kind:
            return JSONResponse(
                {"error": "rs_gear, local_file, local_kind required"}, 400)
        if local_kind not in ("nam", "ir", "rs_ir"):
            return JSONResponse(
                {"error": f"local_kind must be nam|ir|rs_ir, got {local_kind}"}, 400)
        # Sanity-check the file actually exists on disk so we never bind
        # phantom paths.
        if _config_dir is None:
            return JSONResponse({"error": "config_dir not initialized"}, 500)
        root = _config_dir / ("nam_models" if local_kind == "nam" else "nam_irs")
        target = _safe_child(root, local_file)
        if target is None or not target.exists():
            return JSONResponse(
                {"error": f"file not found: {local_file}"}, 404)
        # Detect whether the IR is one of the game-extracted ones so
        # the kind column matches what /batch + the catalog filter expect.
        if local_kind == "ir" and local_file.startswith("rocksmith/"):
            local_kind = "rs_ir"
        assigned = _assign_file_to_gear(rs_gear, local_kind, local_file, None)
        return {"ok": True, "kind": local_kind, "file": local_file, **assigned}

    @app.post("/api/plugins/rig_builder/download_for_gear")
    def download_for_gear(data: dict = Body(...)):
        """Manually pull a tone3000 capture for a single the game gear.

        The UI calls this from the per-candidate "Descargar y asignar"
        button inside the Sugerir modal. Reuses the same
        `_download_candidate` helper the batch worker uses so naming,
        deduplication, and ffmpeg normalization stay identical.

        Body: `{rs_gear: "Amp_AT120", tone3000_id: 12345}`.

        Returns `{kind, file}` on success — the UI stores those on the
        in-memory piece so the next "Guardar preset" picks them up,
        the same way it does for a manually-uploaded file.
        """
        rs_gear = data.get("rs_gear", "")
        tone3000_id = data.get("tone3000_id")
        if not rs_gear or not tone3000_id:
            return JSONResponse({"error": "rs_gear and tone3000_id required"}, 400)
        try:
            tone3000_id = int(tone3000_id)
        except (TypeError, ValueError):
            return JSONResponse({"error": "tone3000_id must be an integer"}, 400)
        settings = _load_settings()
        info = _load_rs_to_real().get(rs_gear) or {}
        category = _gear_category(rs_gear)
        # Manual downloads bypass the batch disk budget — the user
        # explicitly asked for this one file.
        global _batch_disk_bytes
        prev = _batch_disk_bytes
        _batch_disk_bytes = 0
        try:
            result = _download_candidate(
                tone3000_id=tone3000_id,
                is_ir=(category == "cab"),
                rs_gear=rs_gear,
                settings=settings,
            )
        finally:
            _batch_disk_bytes = prev
        if not result:
            return JSONResponse(
                {"error": "download failed — check that the tone3000 key is valid and the tone has a downloadable model"},
                502,
            )
        kind, fname = result
        # Persist the assignment so the gear leaves the Pendientes list
        # and the song actually plays through the downloaded capture.
        # Without this, the file sat on disk unreferenced and coverage
        # kept reporting the gear as pending.
        assigned = _assign_file_to_gear(rs_gear, kind, fname, tone3000_id)
        return {"ok": True, "kind": kind, "file": fname, **assigned}

    # ── Auto-download for a single song (triggered on UI open) ──────

    @app.post("/api/plugins/rig_builder/auto_download_song")
    def auto_download_song(data: dict = Body(...)):
        """Run the same per-piece auto-download flow the batch worker
        does, but scoped to one song. Triggered by the UI when the
        user opens a song with the API key configured — the goal is
        that opening a cloud-loaded song fully materializes its NAM
        chain end-to-end without further clicks.

        Body: `{filename}`. The plugin parses tones, dedupes gear
        across the song's tones, picks tone3000 candidates under the
        current policy, downloads what's missing, and persists
        preset rows. Pieces with an existing assignment in
        `preset_pieces` are skipped — re-opening a song is free.

        Returns counts: `{processed, downloaded, skipped_assigned,
        skipped_no_candidate, failed}`.
        """
        filename = data.get("filename")
        if not filename:
            return JSONResponse({"error": "filename required"}, 400)
        path = _resolve_song_file(filename)
        if path is None:
            return JSONResponse({"error": "file not found"}, 404)
        # A cloud-placeholder library keeps a 0-byte stub here while the real
        # arrangement/tone data lives in sloppak_cache. Accept the song when
        # either is present — the tone reader reads from the cache — so per-song
        # seeding works for cloud libraries instead of 409-ing on the stub.
        if not _song_data_available(path):
            return JSONResponse(
                {"error": "file is a cloud-only placeholder; materialize it first"},
                409,
            )

        # tone3000 is OPTIONAL for seeding. VST-primary gear (rs_gear_to_vst.json)
        # maps with zero downloads, so seed-on-play must work for VST-only rigs
        # with no tone3000 token — otherwise opening a song directly never
        # materializes its chain and the user has to seed each one by hand in the
        # editor first. _auto_download_for_song only touches the client for gear
        # that has no installed VST primary, and skips it gracefully when
        # has_api_access is False. (Previously a hard 400 here blocked the whole
        # direct-play auto-seed path whenever no tone3000 key was configured.)
        try:
            return _auto_download_for_song(_db_song_key(filename, path), path)
        except ValueError as e:
            return JSONResponse({"error": f"unreadable file: {e}"}, 400)
        except Exception as e:
            log.exception("auto_download_song failed for %s", filename)
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)

    # ── Persist a preset (manual save from UI) ────────────────────────

    @app.post("/api/plugins/rig_builder/save_preset")
    def save_preset(data: dict = Body(...)):
        filename = data.get("filename")
        tone_key = data.get("tone_key", "")
        pieces = data.get("pieces") or []
        if not filename or not isinstance(pieces, list):
            return JSONResponse({"error": "filename and pieces required"}, 400)
        filename = _db_song_key(filename)
        name = data.get("name") or f"{filename}::{tone_key or 'tone'}"
        in_gain = float(data.get("input_gain", 1.0))
        # 1.0 unity — see `_persist_preset_chain` docstring for the
        # double-attenuation fix that motivated dropping the 0.5 default.
        out_gain = float(data.get("output_gain", 1.0))
        # Per-tone gate: prefer the `gate` object; fall back to the legacy flat
        # `gate_threshold` field for older callers.
        gate_kwargs = _gate_kwargs_from(data)
        if "gate_threshold" not in gate_kwargs and data.get("gate_threshold") is not None:
            gate_kwargs["gate_threshold"] = float(data["gate_threshold"])
        mode = data.get("assigned_mode", "manual")
        try:
            preset_id = _persist_preset_chain(
                filename=filename, tone_key=tone_key, name=name, pieces=pieces,
                input_gain=in_gain, output_gain=out_gain,
                assigned_mode=mode,
                **gate_kwargs
            )
        except Exception as e:
            log.exception("save_preset failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        # Mirror to the SAME song in the other container format (.psarc <->
        # .sloppak): editing one keeps both in sync, so a song doesn't sound
        # different depending on which file got played. Matched by canonical
        # name (ext + the psarc "_p" marker dropped).
        mirrored = []
        mirrored_presets = []
        try:
            ckey = _canonical_song_key(filename)
            _filename_filter, _filename_args = _tone_mapping_filename_filter(filename)
            sibs = [r[0] for r in _get_conn().execute(
                f"SELECT DISTINCT filename FROM tone_mappings WHERE NOT ({_filename_filter})",
                _filename_args,
            ).fetchall() if _canonical_song_key(r[0]) == ckey]
            for sib in sibs:
                mirror_preset_id = _persist_preset_chain(
                    filename=sib, tone_key=tone_key,
                    name=f"{sib}::{tone_key or 'tone'}", pieces=pieces,
                    input_gain=in_gain, output_gain=out_gain,
                    assigned_mode=mode,
                    **gate_kwargs
                )
                mirrored.append(sib)
                mirrored_presets.append({"filename": sib, "preset_id": mirror_preset_id})
        except Exception:
            log.warning("save_preset: mirror to sibling format failed", exc_info=True)
        return {"ok": True, "preset_id": preset_id, "mirrored": mirrored, "mirrored_presets": mirrored_presets}

    # ── Real Cab: el CAB ROOM del catálogo (Gear → Cabs) ────────────────
    # El panel de cada cab dibuja un canvas con el mic ARRASTRABLE; al
    # soltarlo la UI llama a /cab/synthesize y AUDICIONA el IR renderizado
    # (rbAuditionFile). Con assign=true, además ese IR pasa a ser el sonido
    # del cab en TODAS las canciones (bulk sobre preset_pieces del gear).
    @app.get("/api/plugins/rig_builder/cab/catalog")
    def real_cab_catalog():
        """El catálogo Real Cab completo (la UI lo cachea una vez)."""
        try:
            with open(_plugin_dir / "data" / "real_cab_catalog.json",
                      encoding="utf-8") as fh:
                return json.load(fh)
        except Exception as e:
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)

    @app.post("/api/plugins/rig_builder/cab/synthesize")
    def cab_synthesize(data: dict = Body(...)):
        """Renderiza el IR del modelo físico para (gear, mic, x, dist, angle).

        Devuelve {"name": "realcab/….wav"} (cacheado por parámetros). Con
        `assign=true`, además escribe ese IR en TODAS las preset_pieces del
        cab (base + sufijos de posición), marcadas manual para que Remap all
        no las pise — el cab queda sonando así en todas las canciones.
        """
        gear = str(data.get("gear_type") or "")
        mic = str(data.get("mic") or "sm57")
        x = float(data.get("x", 0.15))
        dist_in = float(data.get("dist_in", 1.0))
        angle = float(data.get("angle_deg", 0.0))
        try:
            with open(_plugin_dir / "data" / "real_cab_catalog.json",
                      encoding="utf-8") as fh:
                cat = json.load(fh)
        except Exception as e:
            return JSONResponse({"error": f"catalog: {e}"}, 500)
        base_gear = gear
        entry = cat["cabs"].get(base_gear)
        if entry is None and "_" in gear:
            base_gear = gear.rsplit("_", 1)[0]
            entry = cat["cabs"].get(base_gear)
        if entry is None:
            return JSONResponse({"error": f"cab not in Real Cab catalog: {gear}"}, 404)
        if mic not in ("sm57", "tlm103", "md421", "km84", "r121", "tube"):
            return JSONResponse({"error": f"unknown mic: {mic}"}, 400)
        x = min(max(x, 0.0), 1.0)
        dist_in = min(max(dist_in, 0.0), 12.0)
        angle = min(max(angle, 0.0), 60.0)

        out_dir = _config_dir / "nam_irs" / "realcab"
        out_dir.mkdir(parents=True, exist_ok=True)
        spk = entry.get("speaker", "g12m")
        # override de parlante (el Cab Room deja elegir entre entry.speakers)
        req_spk = str(data.get("speaker") or "").strip()
        if req_spk:
            allowed = entry.get("speakers") or [spk]
            if req_spk not in allowed:
                return JSONResponse({"error": f"speaker {req_spk} not offered for {base_gear}"}, 400)
            spk = req_spk
        fname = (f"realcab_{spk}_{entry.get('drivers', 1)}x{entry.get('size_in', 12)}"
                 f"{'c' if entry.get('back') == 'closed' else 'o'}_{mic}"
                 f"_x{int(round(x * 100)):03d}_d{int(round(dist_in * 10)):03d}"
                 f"_a{int(round(angle)):02d}.wav")
        out_path = out_dir / fname
        rel = f"realcab/{fname}"
        if not out_path.exists():
            try:
                from rb_core import cab_synth
                cab_synth.synthesize_ir_wav(
                    str(out_path), speaker=spk, mic=mic, x=x, dist_in=dist_in,
                    angle_deg=angle, drivers=int(entry.get("drivers", 1)),
                    size_in=float(entry.get("size_in", 12)),
                    back=str(entry.get("back", "closed")),
                    baffle_m=float(entry.get("baffle_m", 0.6)))
            except Exception as e:
                log.exception("cab_synthesize failed")
                return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)

        assigned = 0
        if data.get("assign"):
            conn = _get_conn()
            with _lock:
                cur = conn.execute(
                    "UPDATE preset_pieces SET file = ?, kind = 'ir', "
                    "assigned_mode = 'manual' WHERE slot = 'cabinet' AND "
                    "(rs_gear_type = ? OR rs_gear_type LIKE ?)",
                    (rel, base_gear, base_gear + "_%"))
                assigned = cur.rowcount or 0
                conn.commit()
        return {"ok": True, "name": rel, "assigned": assigned,
                "params": {"mic": mic, "x": x, "dist_in": dist_in,
                           "angle_deg": angle}}

    @app.post("/api/plugins/rig_builder/reset_tone")
    def reset_tone(data: dict = Body(...)):
        """Reload ONE song tone EXACTLY as it ships in the sloppak.

        Deletes the user's edited preset for (song, tone_key) and re-seeds every
        piece (amp, pedals, rack, cab) from the ORIGINAL GearList via
        ``_auto_download_for_song`` — the SAME resolver the first seed used — so
        ALL edits are discarded (params AND gear swaps) and the tone returns to
        its bundled gear + resolved VST + RS-knob params. The re-seed is what
        restores the VSTs: a plain delete alone makes get_song fall back to the
        raw GearList and the amp comes back with NO VST (blank face), so re-seed
        is mandatory. The current preset is snapshotted first and rolled back if
        the re-seed fails to produce a resolved chain, so a reset can never leave
        the tone half-built. Applies across BOTH containers (.psarc / .sloppak)."""
        filename = data.get("filename")
        tone_key = (data.get("tone_key") or "").strip()
        if not filename or not tone_key:
            return JSONResponse({"error": "filename and tone_key required"}, 400)
        filename = _db_song_key(filename)
        conn = _get_conn()

        def _dump(table, where, arg):
            cur = conn.execute(f"SELECT * FROM {table} WHERE {where}", (arg,))
            cols = [c[0] for c in cur.description]
            return cols, [dict(zip(cols, r)) for r in cur.fetchall()]

        def _reinsert(table, cols, rows):
            if not rows:
                return
            collist = ", ".join(cols)
            placeholders = ", ".join("?" for _ in cols)
            for row in rows:
                conn.execute(
                    f"INSERT OR REPLACE INTO {table} ({collist}) VALUES ({placeholders})",
                    [row[c] for c in cols],
                )

        snap = []   # [(fn, pid, presets_cols, presets_rows, pieces_cols, pieces_rows)]
        try:
            ckey = _canonical_song_key(filename)
            with _lock:
                targets = [(fn, pid) for fn, pid in conn.execute(
                    "SELECT filename, preset_id FROM tone_mappings WHERE tone_key = ?",
                    (tone_key,)).fetchall() if _canonical_song_key(fn) == ckey]
                for fn, pid in targets:
                    pc, pr = _dump("presets", "id = ?", pid)
                    ic, ir = _dump("preset_pieces", "preset_id = ?", pid)
                    snap.append((fn, pid, pc, pr, ic, ir))
                for fn, pid in targets:
                    conn.execute("DELETE FROM tone_mappings WHERE preset_id = ?", (pid,))
                    conn.execute("DELETE FROM preset_pieces WHERE preset_id = ?", (pid,))
                    conn.execute("DELETE FROM presets WHERE id = ?", (pid,))
                conn.commit()
        except Exception as e:
            log.exception("reset_tone: snapshot/delete failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)

        # Re-seed from the ORIGINAL GearList (resolves the VSTs).
        try:
            _song_path = _resolve_song_file(filename)
            if _song_path is not None and _song_path.exists():
                _auto_download_for_song(filename, _song_path)
        except Exception:
            log.warning("reset_tone: re-seed failed for %r", filename, exc_info=True)

        # Did the tone come back with a resolved amp? If not, roll the snapshot
        # back so we never leave the tone broken (VST-less amp / blank face).
        # An amp is "resolved" as EITHER a VST with a path OR a NAM with a model
        # file — a NAM-amp song is a valid re-seed, so requiring kind='vst' here
        # wrongly rolled every NAM-amp reset back with "reseed_failed".
        got = conn.execute(
            "SELECT COUNT(*) FROM tone_mappings tm JOIN preset_pieces pp "
            "ON pp.preset_id = tm.preset_id "
            "WHERE tm.tone_key = ? AND pp.slot = 'amp' AND ("
            "  (pp.kind = 'vst' AND COALESCE(pp.vst_path,'') != '') OR "
            "  (pp.kind = 'nam' AND COALESCE(pp.file,'') != ''))",
            (tone_key,)).fetchone()
        reseeded = bool(got and got[0])
        if not reseeded and snap:
            try:
                with _lock:
                    # Drop whatever the failed re-seed produced for this tone.
                    for fn, pid in [(fn, pid) for fn, pid in conn.execute(
                        "SELECT filename, preset_id FROM tone_mappings WHERE tone_key = ?",
                        (tone_key,)).fetchall() if _canonical_song_key(fn) == ckey]:
                        conn.execute("DELETE FROM tone_mappings WHERE preset_id = ?", (pid,))
                        conn.execute("DELETE FROM preset_pieces WHERE preset_id = ?", (pid,))
                        conn.execute("DELETE FROM presets WHERE id = ?", (pid,))
                    for fn, pid, pc, pr, ic, ir in snap:
                        _reinsert("presets", pc, pr)
                        _reinsert("preset_pieces", ic, ir)
                        conn.execute(
                            "INSERT OR REPLACE INTO tone_mappings (filename, tone_key, preset_id) "
                            "VALUES (?, ?, ?)", (fn, tone_key, pid))
                    conn.commit()
                log.warning("reset_tone: re-seed produced no resolved amp; rolled back "
                            "to the prior preset for %r/%r", filename, tone_key)
                return JSONResponse(
                    {"error": "reseed_failed", "detail": "reverted to previous tone"}, 500)
            except Exception:
                log.exception("reset_tone: rollback failed")
        return {"ok": True, "reseeded": reseeded}

    # ── Export current gear→capture assignments as shipped defaults ───
    @app.post("/api/plugins/rig_builder/export_default_captures")
    @app.post("/api/plugins/nam_rig_builder/export_default_captures")
    def export_default_captures():
        """Snapshot the current DB's gear→capture choices into
        default_captures.json (shipped with the plugin). A fresh install
        then auto-downloads these exact captures during batch/auto-download
        instead of searching tone3000 fresh. Returns the entry count."""
        try:
            captures = _build_default_captures()
            path = _data_path("default_captures.json")
            _atomic_write_json(path, captures, sort_keys=True)
            _invalidate_default_captures()
        except Exception as e:
            log.exception("export_default_captures failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"ok": True, "count": len(captures)}

    # ── Full-chain native preset (experimental multi-NAM preview) ─────
    @app.get("/api/plugins/rig_builder/native_preset_full/{preset_id}")
    def native_preset_full(preset_id: int):
        """Build a native_preset that sends the WHOLE chain to the engine:
        every NAM piece as its own type-1 stage (in signal-flow order) plus
        the cab IR as type-2 — unlike nam_tone's get_native_preset, which
        emits only the single primary NAM + IR. Used by the "Escuchar"
        preview to test/realise multi-NAM playback (pedal → amp → cab)
        without touching the bundle. Returns `nam_stage_count` so the UI/
        operator can see how many NAM stages were built, and the engine's
        `slotsLoaded` (logged client-side) reveals how many it accepted.
        """
        conn = _get_conn()
        prow = conn.execute(
            "SELECT id, name, input_gain, output_gain, gate_threshold, "
            "       gate_enabled, gate_release, gate_depth "
            "FROM presets WHERE id = ?",
            (preset_id,),
        ).fetchone()
        if not prow:
            return JSONResponse({"error": "preset not found"}, 404)
        (_, name, input_gain, output_gain, gate_threshold,
         gate_enabled, gate_release, gate_depth) = prow

        rows = conn.execute(
            "SELECT slot, kind, file, rs_gear_type, bypassed, slot_order, "
            "vst_path, vst_format, vst_state, params_json "
            "FROM preset_pieces WHERE preset_id = ? "
            "ORDER BY slot_order",
            (preset_id,),
        ).fetchall()

        models_dir = (_config_dir / "nam_models") if _config_dir else None
        irs_dir = (_config_dir / "nam_irs") if _config_dir else None

        # A full-chain NAM is an OVERRIDE that coexists with the tone's original
        # gear: when ENABLED (a non-bypassed full-chain row) it is the ONLY stage
        # that plays (originals skipped, no cab-IR tail); when disabled/absent the
        # original gear plays and the full-chain row is ignored.
        is_full_chain = any(r[1] == _FULL_CHAIN_KIND and r[2] and not r[4] for r in rows)

        # Pieces that produce an audio stage (NAM or VST), ordered by signal
        # flow. IR (type 2) is handled separately at the tail of the chain.
        def _rank(slot: str) -> int:
            return _CHAIN_NAM_ORDER.index(slot) if slot in _CHAIN_NAM_ORDER else len(_CHAIN_NAM_ORDER)

        # Build (rank, slot_order, kind, slot, payload, gear, bypassed) tuples
        # so we can stable-sort everything (NAM + VST) by signal-flow first
        # and stored slot_order within the same slot second.
        # REDIRECT layer (gear_map): a game gear can be redirected to one of the
        # user's custom gears WITHOUT changing the stored piece — songs then play
        # the custom's plugin/file while the gear keeps its own identity. Applied
        # only here, at chain-build time.
        # A slot can be redirected to a CUSTOM gear (its VST/file) or to another
        # NATIVE gear (its bundled VST). Resolve each to a stage descriptor.
        _redirect: dict[str, dict] = {}
        _gmap = _load_gear_map()
        if _gmap:
            _custom_by_id = {c.get("rs_gear"): c for c in _load_custom_gear()}
            _known = _build_known_vst_lookup()
            for _rs, _e in _gmap.items():
                tgt = _e.get("custom") if isinstance(_e, dict) else None
                if not tgt:
                    continue
                if tgt in _custom_by_id:
                    _redirect[_rs] = _custom_by_id[tgt]
                else:   # native target → its bundled VST
                    prim = _pick_installed_primary_vst(tgt, _known)
                    if prim and prim.get("vst_path"):
                        _redirect[_rs] = {"vst_path": prim["vst_path"],
                                          "vst_format": prim.get("vst_format") or "VST3",
                                          "vst_state": _compute_vst_state_for_piece(tgt, prim["vst_path"], {})}

        audio_pieces = []
        for slot, kind, file, gear, bypassed, slot_order, vst_path, vst_format, vst_state, params_json in rows:
            if is_full_chain:
                # Override ON: only the enabled full-chain stage plays; skip the
                # original gear (kept in the DB so disabling restores it).
                if kind == _FULL_CHAIN_KIND and file and not bypassed:
                    audio_pieces.append((_rank(slot), slot_order, "nam_full", slot,
                                         file, gear, False, None, None, params_json))
                continue
            if kind == _FULL_CHAIN_KIND:
                continue   # disabled/inactive override — ignore, original gear plays
            rec = _redirect.get(gear)
            if rec is not None:
                if rec.get("gain_variants"):
                    # Multi-capture amp: pick clean/crunch/dist by the song's
                    # mapped Gain, using the gear's editable thresholds.
                    _rg = _rs_gain_from_params_json(params_json, gear)
                    _v = _pick_amp_gain_variant(rec, _rg if _rg is not None else 50.0)
                    _vf = (_v or {}).get("file") or rec.get("file")
                    if _vf:
                        kind, file, vst_path = "nam", _vf, None
                elif rec.get("vst_path"):
                    kind, vst_path = "vst", rec["vst_path"]
                    vst_format, vst_state, file = rec.get("vst_format") or "VST3", rec.get("vst_state"), None
                elif rec.get("file"):
                    kind, file, vst_path = ("ir" if rec.get("kind") == "ir" else "nam"), rec["file"], None
            if kind == "nam" and file:
                audio_pieces.append((_rank(slot), slot_order, "nam", slot,
                                     file, gear, bool(bypassed), None, None,
                                     params_json))
            elif kind == "vst" and vst_path:
                audio_pieces.append((_rank(slot), slot_order, "vst", slot,
                                     vst_path, gear, bool(bypassed),
                                     vst_format or "VST3", vst_state,
                                     params_json))
        audio_pieces.sort(key=lambda t: (t[0], t[1]))

        chain: list[dict] = []
        missing: list[str] = []
        for _r, _o, kind, slot, payload, gear, bypassed, vst_format, vst_state, params_json in audio_pieces:
            if kind == "nam_full":
                # Full-chain capture: absolute path, unity drive (no 2.5×
                # guitar-amp push), and NO cab IR is appended below.
                path = _resolve_model_path(payload, models_dir)
                if not path or not path.exists():
                    missing.append(payload)
                    continue
                chain.append(_nam_stage(path, bypassed=bypassed,
                                        input_level=1.0, output_drive=1.0,
                                        slot=slot, rs_gear=gear or None))
            elif kind == "nam":
                path = _safe_child(models_dir, payload)
                if not path or not path.exists():
                    missing.append(payload)
                    continue
                # Guitar-amp NAMs need inputLevel above unity to reach
                # the saturation region of the captured model (live
                # guitar arrives quieter than the capture-time DI).
                # Bass amps stay at unity — tone3000 bass captures
                # (e.g. Gallien-Krueger G1.0/G3.0) are authored at
                # clean settings and the 2.5× drive over-saturates
                # them. Pedal/rack NAMs also stay at unity so we
                # don't over-drive a clean modulation/utility plugin.
                # outputLevel divides the drive back out so perceived
                # volume tracks the LUFS target.
                # Persisted bypass = engine passes signal THROUGH (not silence).
                _drive = _amp_input_drive_for(gear, slot)
                chain.append(_nam_stage(path, bypassed=bypassed,
                                        input_level=_drive, output_drive=_drive,
                                        slot=slot, rs_gear=gear,
                                        rs_gain=_rs_gain_from_params_json(params_json, gear)))
            else:  # vst
                # VST paths are absolute (no sandbox under models_dir). We
                # don't .exists()-check on the backend because the engine
                # also runs the load — easier to surface a real engine error
                # than a stale stat() in case of bundles vs symlinks.
                vst_path_p = Path(payload)
                # Opaque state blob (verbatim) so the plugin restores its
                # captured params during real playback — not defaults.
                effective_vst_state = _effective_vst_state_for_piece(
                    gear, str(vst_path_p), vst_state, params_json)
                chain.append(_vst_stage(
                    vst_path_p, vst_format, bypassed=bypassed,
                    state=_vst_stage_state(str(vst_path_p), vst_format, effective_vst_state),
                    slot=slot, rs_gear=gear))
                # Per-amp loudness trim: a clean gain right after the amp so all
                # amps sit at the target LUFS (Gain still saturates — untouched).
                if slot == "amp" and not bypassed:
                    _ts = _amp_trim_stage(
                        _amp_trim_mult_for_state(str(vst_path_p), effective_vst_state))
                    if _ts:
                        chain.append(_ts)

        # One cab IR at the tail (prefer the cabinet slot). Indexed in the
        # original `rows` tuples — column order: slot, kind, file, rs_gear,
        # bypassed, slot_order, vst_path, vst_format, vst_state, params_json.
        # Exclude the generic "Cabinets" placeholder (often carrying a stale
        # other/*.wav download) so it drops to the _override_ir_for_cab default
        # below — a real, loudness-matched bundled cab instead of a thin/quiet IR.
        # A full-chain capture already bakes in the cab — never append a cab IR
        # (and never fall back to a shipped override cab) for it.
        ir_rows = [] if is_full_chain else [(r[0], r[2], r[3], bool(r[4]), r[1]) for r in rows
                   if r[1] in ("ir", "rs_ir") and r[2] and r[3] != "Cabinets"]
        ir_pick = next((row for row in ir_rows if row[0] == "cabinet"), None)
        if ir_pick is None and ir_rows:
            ir_pick = ir_rows[0]
        if ir_pick:
            ir_slot, ir_file, ir_gear, ir_bypassed, ir_kind = ir_pick
            ir_path = _safe_child(irs_dir, ir_file)
            if ir_path and ir_path.exists():
                # chainOutputGain applies the preset output_gain once (−12 dB
                # fix); RS IRs get a fixed makeup since they're quieter.
                chain.append(_ir_stage(ir_path, bypassed=ir_bypassed,
                                       slot=ir_slot, rs_gear=ir_gear, di_cab=True,
                                       gain=_ir_stage_gain(ir_kind, ir_path)))
            else:
                missing.append(ir_file)
        elif not is_full_chain:
            # No assigned cab IR (cabinet seeded to kind='none') — fall back to
            # OUR shipped override IR so the cab loads without needing a Cab Room
            # visit (mirrors the mega-chain highway fix).
            cab_row = next((r for r in rows if r[0] == "cabinet" and r[3]), None) \
                or next((r for r in rows if _gear_category(r[3]) == "cab" and r[3]), None)
            if cab_row:
                ir_rel = _override_ir_for_cab(cab_row[3], irs_dir, default_gear=_default_cab_gear_for_rows(rows))
                if ir_rel:
                    ir_path = _safe_child(irs_dir, ir_rel)
                    if ir_path and ir_path.exists():
                        chain.append(_ir_stage(ir_path, bypassed=bool(cab_row[4]),
                                               slot="cabinet", rs_gear=cab_row[3], di_cab=True,
                                               gain=_ir_stage_gain("rs_ir", ir_path)))

        # ── Master chain wrap (global pre + post FX) ──
        # Prepend master_pre stages BEFORE the song's chain (sees the
        # raw DI), append master_post AFTER the cab IR (sees the wet
        # output). Bypass on a master piece survives just like any other.
        #
        # GUARD: if the caller asked for the master sentinel itself (e.g.
        # the master-chain tab's "Listen master in isolation"), don't
        # wrap again — that would double-stack pre/post around itself.
        master_pre_stages: list[dict] = []
        master_post_stages: list[dict] = []
        if not (name or "").startswith("__rig_builder_master_"):
            master_pre_stages  = _build_master_stages("pre",  models_dir, irs_dir, output_gain, missing)
            master_post_stages = _build_master_stages("post", models_dir, irs_dir, output_gain, missing)
            chain = master_pre_stages + chain + master_post_stages

        # Siempre último: normalizador final de cadena completa.
        _append_final_leveler(chain, missing)

        return {
            "id": preset_id,
            "name": name,
            "input_gain": input_gain,
            "output_gain": output_gain,
            "gate_threshold": gate_threshold,
            "gate": {
                "enabled": bool(gate_enabled),
                "threshold": gate_threshold,
                "release": gate_release,
                "depth": gate_depth,
            },
            "native_preset": {"version": 1, "chain": chain},
            "nam_stage_count": sum(1 for s in chain if s["type"] == 1),
            "vst_stage_count": sum(1 for s in chain if s["type"] == 0),
            "ir_stage_count": sum(1 for s in chain if s["type"] == 2),
            "master_pre_count":  len(master_pre_stages),
            "master_post_count": len(master_post_stages),
            "missing": missing,
        }

    # ── Experimental: mega-chain for a whole song ────────────────────
    @app.get("/api/plugins/rig_builder/mega_chain/{filename:path}")
    def mega_chain_for_song(filename: str):
        """Build a single chain that contains EVERY tone of this song,
        plus the master pre/post chain wrapping it. Returns slot ranges
        so the front-end knows which slots belong to which tone (to
        toggle bypass during tone changes). Used by RbMegaChain when
        `mega_chain_mode` is enabled.

        Order of stages in the returned chain:
            master_pre stages
            tone[0] stages (initially bypassed except for the first tone)
            tone[1] stages
            ...
            master_post stages

        Switching between tones in the front-end becomes setBypass on
        the previous tone's slot range + clearing bypass on the new
        tone's slot range — no clearChain, no loadPreset, no transient.
        """
        try:
            decoded = urllib.parse.unquote(filename)
        except Exception:
            decoded = filename

        conn = _get_conn()

        def _lookup(name: str):
            _filename_filter, _filename_args = _tone_mapping_filename_filter(name)
            return conn.execute(
                "SELECT tm.tone_key, tm.preset_id, p.name, p.input_gain, p.output_gain, "
                "       p.gate_threshold "
                "FROM tone_mappings tm JOIN presets p ON tm.preset_id = p.id "
                f"WHERE tm.{_filename_filter} "
                "AND EXISTS (SELECT 1 FROM preset_pieces pp WHERE pp.preset_id = tm.preset_id) "
                "ORDER BY tm.id ASC",
                _filename_args,
            ).fetchall()

        # Heal a stale seed before building the chain. A cloud library can
        # rewrite this song's sloppak (and thus its GearList) in place — a
        # library swap, a corrected chart — without changing the DLC stub the
        # watcher keys on, so the seed silently keeps describing the OLD gear
        # and the song plays a wrong tone. Re-seed any tone whose amp key
        # drifted so `_lookup` below reads the freshly-rebuilt mapping. Bounded
        # to one attempt per song per session; never touches tones whose gear
        # didn't change. Best-effort — a failure here just builds from whatever
        # is currently seeded (the prior behaviour).
        try:
            _song_path = _resolve_song_file(decoded)
            if _song_path is not None:
                _resync_stale_song_seed(decoded, _song_path)
        except Exception:
            log.warning("rig_builder: stale-seed resync check failed for %r",
                        decoded, exc_info=True)
        # Promote parser-miss placeholders (cab "Cabinets" / generic Amps/
        # Pedals/Racks) against the live GearList on every song build — songs
        # seeded before a parser fix (e.g. RS1 bare-cab keys like Cab_PA600C)
        # heal on PLAY, not only when the user opens them in the editor.
        _self_heal_song_gear(decoded)

        mappings = _lookup(decoded)
        # No per-song tone mappings. This is the norm for songs that carry no
        # gear metadata at all — notably feedpak-format songs, which store chart
        # + stems but NO Rocksmith amp/cab/pedal descriptors (see feedpak-spec
        # FEP-0001), so per-song gear→VST mapping is impossible for them. Rather
        # than fail the highway "Rig Tones" button on every feedpak, fall back to
        # the user's default tone. The default tone becomes a single synthetic
        # "tone" run through the same mega-chain builder below, so the response
        # shape is unchanged (one tone) apart from the `default_fallback` marker.
        default_fallback = False
        if not mappings:
            fallback = _default_tone_fallback_mapping(conn)
            if fallback:
                mappings = fallback
                default_fallback = True
        if not mappings:
            return JSONResponse(
                {"error": f"no tone mappings for {decoded}",
                 "filename": decoded}, 404)

        models_dir = (_config_dir / "nam_models") if _config_dir else None
        irs_dir = (_config_dir / "nam_irs") if _config_dir else None

        missing: list[str] = []

        # Build stages for ONE tone's preset_id — mirrors native_preset_full
        # but tagged with `tone_key` and `_initial_bypass` so the front-end
        # can group slots by tone and bypass everything except the active one.
        def _build_tone_stages(preset_id: int, tone_key: str, out_gain: float):
            rows = conn.execute(
                "SELECT slot, kind, file, rs_gear_type, bypassed, slot_order, "
                "vst_path, vst_format, vst_state, params_json "
                "FROM preset_pieces WHERE preset_id = ? "
                "ORDER BY slot_order",
                (preset_id,),
            ).fetchall()

            def _rank(slot: str) -> int:
                return _CHAIN_NAM_ORDER.index(slot) if slot in _CHAIN_NAM_ORDER else len(_CHAIN_NAM_ORDER)

            # Full-chain OVERRIDE: when ENABLED (non-bypassed) it's the ONLY stage
            # that plays (no cab IR); disabled/absent → the original gear plays.
            tone_is_full_chain = any(r[1] == _FULL_CHAIN_KIND and r[2] and not r[4] for r in rows)

            audio_pieces = []
            for slot, kind, file, gear, bypassed, slot_order, vst_path, vst_format, vst_state, params_json in rows:
                if tone_is_full_chain:
                    if kind == _FULL_CHAIN_KIND and file and not bypassed:
                        audio_pieces.append((_rank(slot), slot_order, "nam_full", slot,
                                             file, gear, False, None, None, params_json))
                    continue
                if kind == _FULL_CHAIN_KIND:
                    continue   # disabled override — ignore, original gear plays
                if kind == "nam" and file:
                    audio_pieces.append((_rank(slot), slot_order, "nam", slot,
                                         file, gear, bool(bypassed), None, None,
                                         params_json))
                elif kind == "vst" and vst_path:
                    audio_pieces.append((_rank(slot), slot_order, "vst", slot,
                                         vst_path, gear, bool(bypassed),
                                         vst_format or "VST3", vst_state,
                                         params_json))
            audio_pieces.sort(key=lambda t: (t[0], t[1]))

            tone_stages: list[dict] = []
            for _r, _o, kind, slot, payload, gear, persisted_bypassed, vst_format, vst_state, params_json in audio_pieces:
                if kind == "nam_full":
                    # Absolute path, unity drive, no cab IR (see below).
                    path = _resolve_model_path(payload, models_dir)
                    if not path or not path.exists():
                        missing.append(payload)
                        continue
                    tone_stages.append(_nam_stage(
                        path, bypassed=persisted_bypassed,
                        input_level=1.0, output_drive=1.0,
                        slot=slot, rs_gear=gear or None, tone_key=tone_key))
                    continue
                if kind == "nam":
                    path = _safe_child(models_dir, payload)
                    if not path or not path.exists():
                        missing.append(payload)
                        continue
                    # Per-gear input drive: 2.5× for guitar amps, 1.0
                    # for bass amps + every non-amp slot. See
                    # _amp_input_drive_for for the full rationale.
                    _drive = _amp_input_drive_for(gear, slot)
                    tone_stages.append(_nam_stage(
                        path, bypassed=persisted_bypassed,
                        input_level=_drive, output_drive=_drive,
                        slot=slot, rs_gear=gear, tone_key=tone_key,
                        rs_gain=_rs_gain_from_params_json(params_json, gear)))
                else:  # vst
                    vp = Path(payload)
                    # NOTE: mega_chain uses the simpler pluginPath wrapper (no
                    # opaque-state restore) — preserved verbatim by passing the
                    # state in. See _vst_stage / _vst_stage_state.
                    effective_vst_state = _effective_vst_state_for_piece(
                        gear, str(vp), vst_state, params_json)
                    state_obj = {"pluginPath": str(vp), "format": vst_format}
                    if effective_vst_state:
                        state_obj["pluginState"] = effective_vst_state
                    _vs = _vst_stage(
                        vp, vst_format, bypassed=persisted_bypassed,
                        state=_state_b64(state_obj),
                        slot=slot, rs_gear=gear, tone_key=tone_key)
                    # Slot identity for the mega-chain dedupe key: two
                    # instances of the SAME VST inside one tone (e.g. a
                    # doubled pedal) must keep separate chain slots.
                    # Stripped from the flattened chain copy below.
                    _vs["slot_order"] = _o
                    tone_stages.append(_vs)
                    # Per-amp loudness trim (clean gain after the amp). Tagged
                    # tone_key + amp_trim so the dedupe below keeps per-tone
                    # trims distinct. Gain itself is untouched (still saturates).
                    if slot == "amp" and not persisted_bypassed:
                        _ts = _amp_trim_stage(
                            _amp_trim_mult_for_state(str(vp), effective_vst_state),
                            tone_key=tone_key)
                        if _ts:
                            tone_stages.append(_ts)

            # Cab IR at the tail of the tone (prefer cabinet slot). Exclude the
            # generic "Cabinets" placeholder (may carry a stale other/*.wav) so it
            # drops to the _override_ir_for_cab default — a real, matched bundled cab.
            ir_rows = [] if tone_is_full_chain else [(r[0], r[2], r[3], bool(r[4]), r[1]) for r in rows
                       if r[1] in ("ir", "rs_ir") and r[2] and r[3] != "Cabinets"]
            ir_pick = next((row for row in ir_rows if row[0] == "cabinet"), None)
            if ir_pick is None and ir_rows:
                ir_pick = ir_rows[0]
            if ir_pick:
                ir_slot, ir_file, ir_gear, ir_bypassed, ir_kind = ir_pick
                ir_path = _safe_child(irs_dir, ir_file)
                if ir_path and ir_path.exists():
                    tone_stages.append(_ir_stage(
                        ir_path, bypassed=ir_bypassed, di_cab=True,
                        slot=ir_slot, rs_gear=ir_gear, tone_key=tone_key,
                        gain=_ir_stage_gain(ir_kind, ir_path)))
                else:
                    missing.append(ir_file)
            elif not tone_is_full_chain:
                # No assigned cab IR (seed left the cabinet at kind='none' because
                # the RS game IR doesn't ship) — fall back to OUR shipped override
                # IR so the cab still loads on the highway, not just after the user
                # opens the Cab Room and nudges the mic.
                cab_row = next((r for r in rows if r[0] == "cabinet" and r[3]), None) \
                    or next((r for r in rows if _gear_category(r[3]) == "cab" and r[3]), None)
                if cab_row:
                    ir_rel = _override_ir_for_cab(cab_row[3], irs_dir, default_gear=_default_cab_gear_for_rows(rows))
                    if ir_rel:
                        ir_path = _safe_child(irs_dir, ir_rel)
                        if ir_path and ir_path.exists():
                            tone_stages.append(_ir_stage(
                                ir_path, bypassed=bool(cab_row[4]), di_cab=True,
                                slot="cabinet", rs_gear=cab_row[3], tone_key=tone_key,
                                gain=_ir_stage_gain("rs_ir", ir_path)))
            return tone_stages

        # ── Build the deduped mega-chain ──
        # NAMs and IRs are deduplicated by file path: the same .nam or .wav
        # used by multiple tones lives in ONE slot, shared across all tones
        # that need it. This saves real memory and CPU on songs where 4
        # tones share the same amp/cab (very common in the game). VSTs are
        # NOT deduplicated because each instance can carry distinct
        # parameter state — two tones using the same compressor with
        # different makeup-gain settings need separate instances.
        #
        # Slot-type ordering is preserved (_CHAIN_NAM_ORDER: pre_pedal →
        # amp → post_pedal → rack → cabinet) so signal flow stays correct
        # even when several tones share intermediate stages. Each tone's
        # active_slots[] lists the chain indices it needs un-bypassed; the
        # front-end's setBypass-flip toggles all slots NOT in that list
        # off and everything IN the list on.
        master_pre_stages  = _build_master_stages("pre",  models_dir, irs_dir, 1.0, missing)
        master_post_stages = _build_master_stages("post", models_dir, irs_dir, 1.0, missing)

        final_leveler = _final_leveler_stage(missing)
        if final_leveler:
            master_post_stages.append(final_leveler)

        # Pass 1: enumerate each tone's stages and bucket them by slot type
        # so we can flatten in signal-flow order. Track per-tone usage so we
        # can compute active_slots[] in pass 2.
        # Dedupe key:
        #   - NAM (type 1): ("nam", file_path)
        #   - IR  (type 2): ("ir",  file_path)
        #   - VST (type 0): ("vst", tone_key, slot_order, name) — unique
        #     per tone AND per piece slot (a tone can hold two instances
        #     of the same VST with different states)
        per_tone_stages = []      # parallel to mappings: list of stage lists
        slots_by_type: dict[str, dict] = {}   # slot_type → { dedupe_key: stage_dict }
        slot_order_by_type: dict[str, list] = {}   # slot_type → ordered list of dedupe_keys

        for tone_key, preset_id, _name, _in_gain, out_gain, _gate in mappings:
            tone_stages = _build_tone_stages(int(preset_id), tone_key, float(out_gain or 1.0))
            per_tone_stages.append(tone_stages)
            for stage in tone_stages:
                slot_type = stage.get("slot") or "unspecified"
                stype = stage.get("type")
                if stype == 1:        # NAM
                    dkey = ("nam", stage.get("path"))
                elif stype == 2:      # IR. Cabs dedupe across tones by path; the
                    # per-amp trim stage stays PER-TONE so it always sits right
                    # after its own amp (never shared/reordered before another).
                    dkey = (("amp_trim", tone_key) if stage.get("amp_trim") is not None
                            else ("ir", stage.get("path")))
                else:                  # VST — always unique per tone + slot
                    dkey = ("vst", tone_key, stage.get("slot_order"),
                            stage.get("name") or stage.get("path"))
                bucket = slots_by_type.setdefault(slot_type, {})
                order  = slot_order_by_type.setdefault(slot_type, [])
                if dkey not in bucket:
                    # First time we see this resource: copy the stage,
                    # keep its persisted `bypassed` (the front-end uses
                    # it as the "intended" bypass for this slot when the
                    # active tone references it). Strip tone_key — a
                    # deduped slot may belong to many tones.
                    #
                    # Note: with dedupe, a slot's bypass state is shared
                    # across tones. If tone1 has the amp un-bypassed but
                    # tone2 has it bypassed-only-for-this-tone (rare),
                    # the FIRST occurrence wins. This is a minor lossy
                    # behaviour we accept to cut memory ~3-4×.
                    s = dict(stage)
                    s.pop("tone_key", None)
                    s.pop("slot_order", None)
                    bucket[dkey] = s
                    order.append(dkey)

        # Flatten in signal-flow order: master_pre + slots_by_type in
        # _CHAIN_NAM_ORDER + cabinet + master_post.
        chain: list[dict] = list(master_pre_stages)
        index_of_dkey: dict = {}      # (slot_type, dkey) → chain index

        slot_type_order = list(_CHAIN_NAM_ORDER) + ["cabinet"]
        # Append any unrecognised slot types at the end (defensive).
        for st in slots_by_type.keys():
            if st not in slot_type_order:
                slot_type_order.append(st)

        for slot_type in slot_type_order:
            order = slot_order_by_type.get(slot_type, [])
            bucket = slots_by_type.get(slot_type, {})
            for dkey in order:
                stage = bucket[dkey]
                index_of_dkey[(slot_type, dkey)] = len(chain)
                chain.append(stage)

        master_pre_end = len(master_pre_stages)
        master_post_start = len(chain)
        chain.extend(master_post_stages)
        master_post_end = len(chain)

        # Pass 2: per tone, build `slots` — list of {idx, bypassed} for
        # the chain indices this tone uses. The front-end applies each
        # entry's persisted bypass when the tone becomes active, so a
        # piece the user explicitly bypassed in the per-song tab stays
        # bypassed instead of getting forced on. Indices NOT in this
        # tone's list are always force-bypassed on tone switch (signal
        # passes through them).
        # Map each seeded tone_key to its alias set {Key, Name} from the
        # sloppak tone definitions. The highway publishes tone CHANGES by the
        # definition's *Name* (e.g. "paradise_city_general") while we seed by
        # its *Key* (e.g. "intro"); without the Name alias the front-end can't
        # match the two and collapses to a single default tone for the whole
        # song. Best-effort: any read failure just yields no extra aliases.
        tone_alias_map: dict[str, list[str]] = {}
        try:
            _defs = _read_tones_from_sloppak(decoded, _get_dlc_dir()) if _get_dlc_dir else []
            for _t in _defs:
                if not isinstance(_t, dict):
                    continue
                _al = [a for a in ((_t.get("Key") or "").strip(),
                                   (_t.get("Name") or "").strip()) if a]
                for a in _al:
                    tone_alias_map[a] = _al
        except Exception:
            log.warning("tone alias map build failed for %r", decoded, exc_info=True)

        tone_index: list[dict] = []
        for i, (tone_key, preset_id, _name, _in_gain, _out_gain, _gate) in enumerate(mappings):
            tone_stages = per_tone_stages[i]
            seen: dict[int, dict] = {}    # idx → {idx, bypassed}
            for stage in tone_stages:
                slot_type = stage.get("slot") or "unspecified"
                stype = stage.get("type")
                if stype == 1:
                    dkey = ("nam", stage.get("path"))
                elif stype == 2:
                    dkey = (("amp_trim", tone_key) if stage.get("amp_trim") is not None
                            else ("ir", stage.get("path")))
                else:
                    dkey = ("vst", tone_key, stage.get("slot_order"),
                            stage.get("name") or stage.get("path"))
                idx = index_of_dkey.get((slot_type, dkey))
                if idx is not None and idx not in seen:
                    entry = {"idx": idx, "bypassed": bool(stage.get("bypassed", False))}
                    if stage.get("type") is not None:
                        entry["type"] = stage.get("type")
                    if stage.get("slot") is not None:
                        entry["slot"] = stage.get("slot")
                    if stage.get("rs_gear") is not None:
                        entry["rs_gear"] = stage.get("rs_gear")
                    if stage.get("rs_gain") is not None:
                        entry["rs_gain"] = stage.get("rs_gain")
                    seen[idx] = entry
            slots_list = sorted(seen.values(), key=lambda e: e["idx"])
            _aliases: list[str] = []
            for a in [tone_key] + tone_alias_map.get(tone_key, []):
                if a and a not in _aliases:
                    _aliases.append(a)
            tone_index.append({
                "tone_key": tone_key,
                "aliases": _aliases,
                "preset_id": int(preset_id),
                "slots": slots_list,
                "stage_count": len(slots_list),
            })

        # Master slot lists — same shape as `tones[].slots`. The front-end
        # respects each entry's persisted bypass: a master piece toggled
        # off in the Master Chain tab stays bypassed in playback.
        master_pre_slots = [
            {"idx": i, "bypassed": bool(chain[i].get("bypassed", False))}
            for i in range(0, master_pre_end)
        ]
        master_post_slots = [
            {"idx": i, "bypassed": bool(chain[i].get("bypassed", False))}
            for i in range(master_post_start, master_post_end)
        ]

        return {
            "filename": decoded,
            "native_preset": {"version": 1, "chain": chain},
            "tones": tone_index,
            "master_pre_count":  len(master_pre_stages),
            "master_post_count": len(master_post_stages),
            "master_pre_slots":  master_pre_slots,
            "master_post_slots": master_post_slots,
            # Back-compat aliases — index-only lists, in case anything
            # external is still reading the old field names.
            "master_pre_indices":  [s["idx"] for s in master_pre_slots],
            "master_post_indices": [s["idx"] for s in master_post_slots],
            "active_tone_key":   tone_index[0]["tone_key"] if tone_index else None,
            "missing": missing,
            "total_stages": len(chain),
            # True when this song had no per-song mappings and we served the
            # user's default tone instead (feedpak fallback). The front-end uses
            # it to label the player button "default rig" rather than a per-song
            # tone key.
            "default_fallback": default_fallback,
        }

    # ── Single-stage audition (catalog "Escuchar") ────────────────────
    @app.get("/api/plugins/rig_builder/native_preset_one")
    def native_preset_one(file: str = "", kind: str = "nam", gain: float = 1.0,
                          vst_path: str = "", vst_format: str = "VST3",
                          rs_gear: str = ""):
        """A native_preset with ONE stage to audition a single gear in
        isolation. `kind` selects the stage type:
          - "nam"            → NAM model from nam_models/<file>
          - "ir" / "rs_ir"   → IR from nam_irs/<file>
          - "vst"            → VST3/AU at absolute `vst_path` (file ignored)

        `rs_gear` (optional) is the rs_gear_type the frontend has on
        hand for this audition (e.g. 'Bass_Amp_CS75B'). When set, the
        per-gear input-drive helper switches off the guitar-amp boost
        for bass amps — bass tone3000 captures are authored at clean
        gain and the boost over-saturates them. Falls back to a
        filename heuristic when rs_gear is missing.
        """
        models_dir = (_config_dir / "nam_models") if _config_dir else None
        irs_dir = (_config_dir / "nam_irs") if _config_dir else None
        if kind == "vst":
            if not vst_path:
                return JSONResponse({"error": "vst_path required"}, 400)
            vp = Path(vst_path)
            stage = _vst_stage(
                vp, vst_format or "VST3", bypassed=False,
                state=_state_b64({"pluginPath": str(vp),
                                  "format": vst_format or "VST3"}))
            # Loudness-match amp auditions too (state is None → the model's
            # measured defaults, which the plugin also opens at): even level
            # when comparing amps in the Gear catalog.
            _audition_trim = _amp_trim_stage(_amp_trim_mult_for_state(str(vp), None))
        elif kind in ("ir", "rs_ir"):
            p = _safe_child(irs_dir, file)
            if not p or not p.exists():
                return JSONResponse({"error": "ir not found"}, 404)
            stage = _ir_stage(p, bypassed=False, gain=_ir_stage_gain(kind, p, gain))
        else:
            p = _safe_child(models_dir, file)
            if not p or not p.exists():
                return JSONResponse({"error": "model not found"}, 404)
            # Apply per-gear input drive. Frontend usually passes
            # rs_gear (catalog has it on hand); when absent we fall
            # back to the storage-subdir heuristic, treating
            # nam_models/amps/* as a guitar amp by default. The helper
            # automatically switches off the boost for Bass_* gears so
            # tone3000 bass captures (Gallien-Krueger G1.0/G3.0/G5.0,
            # CS75B, etc.) don't over-saturate.
            _is_amp = bool(rs_gear) and rs_gear.startswith(("Amp_", "Bass_Amp_"))
            if not rs_gear:
                _is_amp = (file or "").lower().startswith("amps/")
                # Heuristic-only fallback: treat as guitar amp
                # (rs_gear="") so the default 2.5× drive applies to
                # legacy callers that haven't yet been updated to
                # pass rs_gear.
            _slot_hint = "amp" if _is_amp else None
            _drive = _amp_input_drive_for(rs_gear or None, _slot_hint)
            # Single-NAM audition (▶): same loudness normalisation as the
            # full-chain path; the caller-provided `gain` (default 1.0)
            # multiplies the normalised level so user overrides still work.
            stage = _nam_stage(p, bypassed=False, input_level=_drive,
                               output_drive=_drive, output_mult=float(gain))
        chain = [stage]
        if kind == "vst" and _audition_trim:
            chain.append(_audition_trim)
        missing = []
        _append_final_leveler(chain, missing, audition=True)   # single-gear preview → safe level
        return {"native_preset": {"version": 1, "chain": chain}, "missing": missing}

    # ── VST plugin endpoints (Fase C: known list + assign + state) ────
    # The native engine owns the actual VST scan + load via the JS bridge
    # (`window.feedBackDesktop.audio.getKnownPlugins() / loadVST / ...`).
    # The Python side just persists the user's choice per (song, gear) so
    # we can rebuild the chain on Listen without re-scanning each time.

    _KNOWN_VSTS_FILENAME = "rig_builder_known_vsts.json"

    @app.get("/api/plugins/rig_builder/vst/known")
    def vst_known():
        """Return the last list of installed VST3/AU plugins as seen by the
        engine. Populated by the frontend via POST /vst/sync_known after it
        calls `getKnownPlugins()` on the native API. Empty list if the user
        hasn't synced yet (we can't initiate a scan from Python — the engine
        runs in the renderer's main process)."""
        bundled = _bundled_vst_plugins()
        if _config_dir is None:
            return {"plugins": bundled}
        path = _config_dir / _KNOWN_VSTS_FILENAME
        if not path.exists():
            return {"plugins": bundled}
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            by_path = {p["path"]: p for p in bundled if p.get("path")}
            if isinstance(data, list):
                for p in data:
                    if isinstance(p, dict) and p.get("path"):
                        by_path[p["path"]] = p
                return {"plugins": sorted(by_path.values(), key=lambda x: (x.get("name") or "").lower())}
            if isinstance(data, dict) and isinstance(data.get("plugins"), list):
                for p in data.get("plugins") or []:
                    if isinstance(p, dict) and p.get("path"):
                        by_path[p["path"]] = p
                merged = dict(data)
                merged["plugins"] = sorted(by_path.values(), key=lambda x: (x.get("name") or "").lower())
                return merged
        except (json.JSONDecodeError, OSError):
            log.warning("known vsts file unreadable", exc_info=True)
        return {"plugins": bundled}

    @app.post("/api/plugins/rig_builder/vst/sync_known")
    def vst_sync_known(data: dict = Body(...)):
        """Frontend pushes the result of `getKnownPlugins()` so the Python
        side can render the dropdown without round-tripping through JS.
        Body: `{"plugins": [{name, manufacturer, format, category, path}, ...],
                "mode": "merge" | "replace"}`.

        Default is `merge` (keyed by `path`) — partial scans that crash
        midway (the known JUCE scanPlugins bug) no longer wipe entries
        seeded by `seed_known_vsts.py` or contributed by previous
        successful scans. Pass `mode: "replace"` for an explicit
        clear-and-rescan flow.
        """
        plugins = data.get("plugins")
        mode = (data.get("mode") or "merge").lower()
        if not isinstance(plugins, list):
            return JSONResponse({"error": "plugins must be a list"}, 400)
        if _config_dir is None:
            return JSONResponse({"error": "config_dir not initialized"}, 500)
        path = _config_dir / _KNOWN_VSTS_FILENAME

        merged_by_path: dict = {}
        if mode != "replace" and path.exists():
            try:
                existing = json.loads(path.read_text(encoding="utf-8"))
                for p in existing.get("plugins", []) or []:
                    if isinstance(p, dict) and p.get("path"):
                        merged_by_path[p["path"]] = p
            except (ValueError, OSError):
                merged_by_path = {}
        # Prefer the freshly-scanned entry (richer JUCE metadata) over a
        # synthetic seeded one — by path key.
        for p in plugins:
            if isinstance(p, dict) and p.get("path"):
                merged_by_path[p["path"]] = p
        final = sorted(merged_by_path.values(),
                       key=lambda x: (x.get("name") or "").lower())
        try:
            _atomic_write_json(path, {"plugins": final,
                                      "synced_at": int(time.time())})
        except OSError as e:
            return JSONResponse({"error": f"write failed: {e}"}, 500)
        return {"ok": True, "count": len(final),
                "mode": mode, "added_from_scan": len(plugins)}

    # ── Amp gain variants — CRUD endpoints used by the Gear-catalog UI ──
    #
    # Variants are persisted into rs_to_real.json (the same file the
    # auto-download / batch worker reads). Writing is atomic:
    # rs_to_real.json.bak is updated before the new file is replaced in.

    def _rs_map_write_atomic(rs_map: dict) -> None:
        """Save rs_to_real.json with a one-deep backup. Caller holds the
        module-level _rs_map_lock so a concurrent write can't corrupt
        the file."""
        path = _data_path("rs_to_real.json")
        backup = path.with_suffix(".json.bak")
        if path.exists():
            backup.write_bytes(path.read_bytes())
        _atomic_write_json(path, rs_map)

    @app.get("/api/plugins/rig_builder/amp_variants/{rs_gear}")
    def amp_variants_list(rs_gear: str):
        """Return the gain_variants block currently on this amp + a
        recommended default range table for empty slots. The UI uses
        this to populate the panel."""
        rs_map = _load_rs_to_real()
        info = rs_map.get(rs_gear)
        if not info:
            return JSONResponse({"error": f"rs_gear {rs_gear} not in rs_to_real.json"}, 404)
        variants = info.get("gain_variants") or {}
        # Defaults shown in empty slots — the picker uses these ranges
        # if the user doesn't override.
        defaults = {
            "clean":  {"rs_gain_range": [0, 35]},
            "crunch": {"rs_gain_range": [35, 70]},
            "dist":   {"rs_gain_range": [70, 100]},
        }
        return {
            "rs_gear": rs_gear,
            "name": info.get("name") or rs_gear,
            "category": info.get("category") or "amp",
            "variants": variants,
            "default_levels": defaults,
        }

    @app.post("/api/plugins/rig_builder/amp_variants/{rs_gear}/{level}")
    def amp_variants_upsert(rs_gear: str, level: str, data: dict = Body(...)):
        """Set/replace one variant on the amp. Body:
            {tone3000_id, model_id?, rs_gain_lo, rs_gain_hi, notes?, curator?}
        Numeric fields are coerced; missing range bounds default to the
        clean/crunch/dist defaults if `level` matches one of those.
        """
        try:
            tone3000_id = int(data.get("tone3000_id"))
        except (TypeError, ValueError):
            return JSONResponse({"error": "tone3000_id required (integer)"}, 400)
        try:
            model_id = int(data["model_id"]) if data.get("model_id") not in (None, "") else None
        except (TypeError, ValueError):
            return JSONResponse({"error": "model_id must be an integer or null"}, 400)
        defaults = {"clean": (0, 35), "crunch": (35, 70), "dist": (70, 100)}
        d_lo, d_hi = defaults.get(level, (0, 100))
        try:
            rs_lo = float(data["rs_gain_lo"]) if data.get("rs_gain_lo") not in (None, "") else d_lo
            rs_hi = float(data["rs_gain_hi"]) if data.get("rs_gain_hi") not in (None, "") else d_hi
        except (TypeError, ValueError):
            return JSONResponse({"error": "rs_gain_lo / rs_gain_hi must be numeric"}, 400)
        if rs_lo > rs_hi:
            return JSONResponse({"error": "rs_gain_lo must be ≤ rs_gain_hi"}, 400)

        with _rs_map_lock:
            path = _data_path("rs_to_real.json")
            try:
                rs_map = json.loads(path.read_text(encoding="utf-8"))
            except (FileNotFoundError, json.JSONDecodeError, OSError,
                    UnicodeDecodeError):
                log.exception("amp_variants: rs_to_real.json unreadable")
                return JSONResponse(
                    {"error": "rs_to_real.json is missing or unreadable"}, 500)
            info = rs_map.get(rs_gear)
            if not info:
                return JSONResponse({"error": f"rs_gear {rs_gear} not in rs_to_real.json"}, 404)
            variants = info.setdefault("gain_variants", {})
            entry: dict = {
                "tone3000_id": tone3000_id,
                "rs_gain_range": [rs_lo, rs_hi],
            }
            if model_id is not None:
                entry["model_id"] = model_id
            notes = (data.get("notes") or "").strip()
            curator = (data.get("curator") or "").strip()
            if notes:   entry["notes"] = notes
            if curator: entry["curator"] = curator
            variants[level] = entry
            _rs_map_write_atomic(rs_map)
            _invalidate_rs_to_real()
        return {"ok": True, "rs_gear": rs_gear, "level": level, "entry": entry}

    @app.delete("/api/plugins/rig_builder/amp_variants/{rs_gear}/{level}")
    def amp_variants_delete(rs_gear: str, level: str):
        with _rs_map_lock:
            path = _data_path("rs_to_real.json")
            try:
                rs_map = json.loads(path.read_text(encoding="utf-8"))
            except (FileNotFoundError, json.JSONDecodeError, OSError,
                    UnicodeDecodeError):
                log.exception("amp_variants: rs_to_real.json unreadable")
                return JSONResponse(
                    {"error": "rs_to_real.json is missing or unreadable"}, 500)
            info = rs_map.get(rs_gear)
            if not info:
                return JSONResponse({"error": f"rs_gear {rs_gear} not in rs_to_real.json"}, 404)
            variants = info.get("gain_variants") or {}
            if level not in variants:
                return {"ok": True, "noop": True}
            del variants[level]
            if not variants:
                info.pop("gain_variants", None)   # don't leave an empty dict
            _rs_map_write_atomic(rs_map)
            _invalidate_rs_to_real()
        return {"ok": True, "rs_gear": rs_gear, "level": level, "deleted": True}

    @app.get("/api/plugins/rig_builder/tone3000/captures/{tone_id}")
    def tone3000_captures(tone_id: int):
        """List the captures inside one tone3000 tone page — fuels the
        per-variant capture dropdown.

        Each capture on tone3000 carries a human-readable title that
        encodes the knob settings (e.g. "JCM800 2203 D.I. - G7 B5 M5 T5
        P5 V5 - STD" where G7 = Gain=7). The dropdown HAS to show that
        title — without it the user can't tell which capture matches
        which the game gain level. We try a handful of field names in
        priority order because tone3000's API has shipped the title
        under different keys over time; the URL-derived hash filename is
        the last resort.
        """
        client = _get_t3k_client()
        if not client.has_api_access:
            return JSONResponse({"error": "tone3000 not connected (Settings → Connect with tone3000)"}, 400)
        try:
            payload = client.list_models(int(tone_id))
        except Exception as e:
            return JSONResponse({"error": f"list_models failed: {e}"}, 502)
        models = (payload or {}).get("data") or []
        out = []
        for m in models:
            # Prefer the human-readable title fields. The tone3000 UI
            # surfaces something like "JCM800 2203 D.I. - G7 B5 M5 T5
            # P5 V5 - STD"; depending on the API revision this lands in
            # `title`, `name`, `display_name`, or `description`. Use the
            # first non-empty match and only fall back to URL-derived
            # filename when the API gave us nothing usable.
            label = ""
            for k in ("title", "name", "display_name", "description"):
                v = m.get(k)
                if isinstance(v, str) and v.strip():
                    label = v.strip()
                    break
            if not label:
                url = m.get("model_url") or m.get("url") or ""
                if url:
                    label = url.split("/")[-1].split("?")[0]
                else:
                    label = f"model_{m.get('id')}"
            if len(label) > 80:
                label = label[:77] + "..."
            out.append({
                "model_id": m.get("id"),
                "size": (m.get("size") or "").lower(),
                "license": m.get("license") or "",
                "name": label,
            })
        return {
            "tone_id": int(tone_id),
            "title": (payload or {}).get("title") or "",
            "captures": out,
        }

    @app.post("/api/plugins/rig_builder/vst/assign")
    def vst_assign(data: dict = Body(...)):
        """Assign a VST to every preset_pieces row for a given rs_gear_type
        (or to a single (preset_id, rs_gear_type) pair). Mirrors how
        `/upload_for_gear` and `/download_for_gear` work for NAM/IR, but
        writes the kind=vst columns instead.

        Body:
          - rs_gear_type:  required, e.g. "Pedal_Chorus20"
          - vst_path:      required, absolute path to .vst3 / .component
          - vst_format:    "VST3" | "AudioUnit" (default VST3)
          - vst_state:     optional base64 plugin state (captured later)
          - preset_id:     optional — if present, scope to one preset
        """
        rs_gear = (data.get("rs_gear_type") or "").strip()
        vst_path = (data.get("vst_path") or "").strip()
        vst_format = (data.get("vst_format") or "VST3").strip()
        vst_state = data.get("vst_state")
        preset_id = data.get("preset_id")
        if not rs_gear or not vst_path:
            return JSONResponse(
                {"error": "rs_gear_type and vst_path required"}, 400)
        conn = _get_conn()
        with _lock:
            if preset_id is not None:
                where = "rs_gear_type = ? AND preset_id = ?"
                params = (rs_gear, int(preset_id))
            else:
                where = "rs_gear_type = ?"
                params = (rs_gear,)
            cur = conn.execute(
                f"UPDATE preset_pieces SET "
                f"  kind = 'vst', file = NULL, "
                f"  vst_path = ?, vst_format = ?, vst_state = ?, "
                f"  assigned_mode = 'manual_vst' "
                f"WHERE {where}",
                (vst_path, vst_format, vst_state, *params),
            )
            pieces_updated = cur.rowcount
            # Refresh primary model/IR for affected presets — a VST never
            # becomes the primary `model_file` (that slot only takes NAM),
            # but the recompute is safe and keeps consistency.
            affected = [r[0] for r in conn.execute(
                f"SELECT DISTINCT preset_id FROM preset_pieces WHERE {where}",
                params,
            ).fetchall()]
            for pid in affected:
                _recompute_preset_primaries(conn, pid)
            conn.commit()
        return {"ok": True, "pieces_updated": pieces_updated,
                "presets_updated": len(affected)}

    @app.post("/api/plugins/rig_builder/vst/capture_state")
    def vst_capture_state(data: dict = Body(...)):
        """Persist a captured plugin state blob for an existing VST piece.
        The frontend obtains the blob via `api.savePreset()` (whole-chain
        opaque format) or — better — via the future per-slot getState API.
        Body: {rs_gear_type, vst_state, preset_id?}.
        """
        rs_gear = (data.get("rs_gear_type") or "").strip()
        vst_state = data.get("vst_state")
        preset_id = data.get("preset_id")
        if not rs_gear or vst_state is None:
            return JSONResponse(
                {"error": "rs_gear_type and vst_state required"}, 400)
        conn = _get_conn()
        with _lock:
            if preset_id is not None:
                cur = conn.execute(
                    "UPDATE preset_pieces SET vst_state = ? "
                    "WHERE rs_gear_type = ? AND preset_id = ? AND kind = 'vst'",
                    (vst_state, rs_gear, int(preset_id)),
                )
            else:
                cur = conn.execute(
                    "UPDATE preset_pieces SET vst_state = ? "
                    "WHERE rs_gear_type = ? AND kind = 'vst'",
                    (vst_state, rs_gear),
                )
            conn.commit()
        return {"ok": True, "pieces_updated": cur.rowcount}

    @app.get("/api/plugins/rig_builder/local_files")
    def local_files(kind: str = "nam", category: str = ""):
        """List all locally-available NAM models or IR WAVs that the user
        could pick for a piece, with usage stats so the UI can sort by
        most-used first.

        Query:
          - kind:     "nam" → lists nam_models/**/*.nam (recursive, post-v1.2
                              files live in amps/ pedals/ racks/ other/)
                      "ir"  → lists nam_irs/**/*.wav (recursive, includes any
                              game-cab IRs under nam_irs/rocksmith/)
          - category: optional. "amp"|"pedal"|"rack"|"cab" — restricts the
                      walk to the matching subdir (amps/pedals/racks/cabs)
                      so the per-piece Library picker doesn't dump every
                      NAM in the user's library on an amp slot. Falls back
                      to listing everything if the subdir doesn't exist.

        Returns:
          {"files": [
            {"name": str (relative to dir root),
             "size_bytes": int,
             "mtime_iso": str (ISO 8601),
             "use_count": int (# of preset_pieces referencing it),
             "used_for_gears": [rs_gear_type, ...] (distinct)},
            ...]}

        The list is sorted by use_count DESC then name ASC, so the
        plugins the user has assigned most often surface to the top.
        """
        if _config_dir is None:
            return JSONResponse({"error": "config_dir not initialized"}, 500)
        kind = (kind or "nam").lower()
        if kind == "nam":
            root = _config_dir / "nam_models"
            ext = ".nam"
        elif kind in ("ir", "wav"):
            root = _config_dir / "nam_irs"
            ext = ".wav"
        else:
            return JSONResponse({"error": f"unknown kind: {kind}"}, 400)
        if not root.exists():
            return {"files": []}
        # Optional category narrowing — map external category → subdir.
        # Falls through to the full root walk if the subdir is missing,
        # which keeps the picker working for users still on pre-v1.2
        # flat storage.
        scoped_root = root
        if category:
            cat = (category or "").lower()
            subdir = None
            if kind == "nam":
                subdir = {"amp": "amps", "pedal": "pedals",
                          "rack": "racks", "other": "other"}.get(cat)
            else:
                subdir = {"cab": "cabs", "other": "other"}.get(cat)
            if subdir and (root / subdir).exists():
                scoped_root = root / subdir
        # Walk filesystem (always recursive now to support subdir layout).
        paths: list = []
        for p in scoped_root.rglob(f"*{ext}"):
            if p.is_file():
                paths.append(p)
        # Build a single SQL query to fetch usage stats for ALL files at once
        # (one round-trip beats N).
        conn = _get_conn()
        # Match preset_pieces.file against the relative name we present.
        # For NAM models the file column stores the bare basename; for IRs
        # under nam_irs/rocksmith/foo.wav it stores "rocksmith/foo.wav"
        # (see _assign_file_to_gear and friends — same relative convention).
        rel_names = [p.relative_to(root).as_posix() for p in paths]
        usage: dict[str, dict] = {n: {"count": 0, "gears": []} for n in rel_names}
        for chunk in _chunked(rel_names):
            placeholders = ",".join("?" for _ in chunk)
            rows = conn.execute(
                f"SELECT file, COUNT(*), GROUP_CONCAT(DISTINCT rs_gear_type) "
                f"FROM preset_pieces WHERE file IN ({placeholders}) GROUP BY file",
                tuple(chunk),
            ).fetchall()
            for fname, n, gears in rows:
                usage[fname] = {
                    "count": int(n),
                    "gears": [g for g in (gears or "").split(",") if g],
                }
        # Map each tone3000 download back to its human title so the picker
        # shows "Marshall JTM45" instead of tone3000_<id>_m<model>_<gear>.
        # Best-effort: None when the tone isn't in the local search cache or
        # the file isn't a tone3000 download (manual upload / RS-extracted IR).
        img_idx = _tone_image_index()

        def _title_for(relname: str):
            m = re.match(r"tone3000_(\d+)_m\d+_", Path(relname).name)
            return (img_idx.get(int(m.group(1))) or {}).get("title") if m else None

        out = []
        for p, n in zip(paths, rel_names):
            try:
                st = p.stat()
                size_bytes = st.st_size
                mtime_iso = datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")
            except OSError:
                size_bytes = 0
                mtime_iso = ""
            u = usage[n]
            out.append({
                "name": n,
                "title": _title_for(n),
                "size_bytes": size_bytes,
                "mtime_iso": mtime_iso,
                "use_count": u["count"],
                "used_for_gears": u["gears"],
            })
        # Sort: most-used first, then alphabetical.
        out.sort(key=lambda f: (-f["use_count"], f["name"].lower()))
        return {"files": out, "kind": kind, "total": len(out)}

    @app.get("/api/plugins/rig_builder/vst/knob_mapping")
    def vst_knob_mapping(rs_gear_type: str, vst_name: str):
        """Lookup the per-VST translation table for a game gear:
        which RS knob maps to which VST parameter, plus scaling.

        Query params:
          - rs_gear_type: e.g. "Pedal_Chorus20"
          - vst_name:     filename stem of the .vst3 / .component bundle
                          (case-insensitive). E.g. "uaudio_brigade_chorus".

        Returns: {"mapping": {<rs_knob>: {param, scale, offset?, invert?}}, ...}
        or {"mapping": null} if no curated mapping exists for this pair.
        """
        table = _load_knob_to_vst_table()
        gear_entry = table.get(rs_gear_type) or {}
        # Case-insensitive VST lookup so user typos / case differences don't
        # silently miss a perfectly good mapping.
        key = (vst_name or "").lower()
        if not key:
            return {"mapping": None}
        for name, mapping in gear_entry.items():
            if name.lower() == key:
                return {"mapping": mapping, "matched_vst": name,
                        "rs_gear_type": rs_gear_type}
        return {"mapping": None}

    @app.get("/api/plugins/rig_builder/vst/suggest/{rs_gear_type}")
    def vst_suggest(rs_gear_type: str):
        """Lookup the static rs_gear_to_vst.json seed catalog for VST
        suggestions. Falls back to whatever is in the known list whose
        category matches the gear's family (rough heuristic)."""
        seeds = _load_vst_seed_catalog()
        # Copy before annotating — the seed catalog is a shared cache and
        # mutating its dicts would leak `installed*` keys across requests.
        suggestions = [dict(s) for s in seeds.get(rs_gear_type, [])]
        # Cross-reference suggestions with what's actually installed so the
        # UI can disambiguate "you have it" vs "you'd need to install".
        known = vst_known().get("plugins", [])
        installed_names = {p.get("name", "").lower(): p for p in known}
        for s in suggestions:
            match = installed_names.get(s.get("name", "").lower())
            s["installed"] = match is not None
            if match:
                s["installed_path"] = match.get("path")
                s["installed_format"] = match.get("format")
        return {"rs_gear_type": rs_gear_type, "suggestions": suggestions}

    # ── Gear catalog (grouped by type, with parenting + photo) ────────
    @app.get("/api/plugins/rig_builder/gear_catalog")
    def gear_catalog():
        """The full the game gear catalog, grouped by category, with what
        each is parented to (real make/model + assigned capture/file) and a
        tone3000 photo when resolvable from cache. Gear used by a mapped song
        carries its real assignment; gear NOT in any song is still listed so
        the user can browse/audition everything — bundled-VST gear gets its
        plugin attached (auditionable at defaults), the rest show unassigned."""
        from rb_core.tone3000_client import Tone3000Client
        conn = _get_conn()
        rs_map = _load_rs_to_real()
        # Game gear → user custom gear mapping (for the "mapped to" label).
        _gmap = _load_gear_map()
        _custom_names = {c.get("rs_gear"): (c.get("real_name") or c.get("rs_gear"))
                         for c in _load_custom_gear()}
        # Inverse of the redirect map: which game-gear slots each gear covers
        # (copyright-free names). Used for the uniform "Assigned to" label on both
        # native and custom gear.
        _covers: dict[str, list[str]] = {}
        for _rs, _e in _gmap.items():
            if isinstance(_e, dict) and _e.get("custom"):
                _covers.setdefault(_e["custom"], []).append(_gear_display_name(_rs, _rs))
        img_idx = _tone_image_index()

        # Best preset_piece per gear: prefer a row with an effective
        # assignment (NAM/IR file OR a VST path), else latest. Excludes
        # the master-chain sentinel presets so adding a piece to the
        # global master chain doesn't pollute the Gear catalog with
        # synthetic rs_gear_types like "VST_…".
        master_ids = [pid for pid in (_get_master_preset_id("pre"),
                                      _get_master_preset_id("post"))
                      if pid is not None]
        if master_ids:
            placeholders = ",".join("?" for _ in master_ids)
            rows = conn.execute(
                "SELECT rs_gear_type, kind, file, tone3000_id, id, "
                "       vst_path, vst_format, vst_state "
                f"FROM preset_pieces WHERE preset_id NOT IN ({placeholders}) "
                "ORDER BY id DESC",
                tuple(master_ids),
            ).fetchall()
        else:
            rows = conn.execute(
                "SELECT rs_gear_type, kind, file, tone3000_id, id, "
                "       vst_path, vst_format, vst_state "
                "FROM preset_pieces ORDER BY id DESC"
            ).fetchall()
        best: dict[str, dict] = {}
        for gear, kind, file, t3kid, _id, vst_path, vst_format, vst_state in rows:
            has_assignment = bool(file) or bool(vst_path)
            cur = best.get(gear)
            if cur is None or (has_assignment and not cur["has_assignment"]):
                best[gear] = {
                    "kind": kind, "file": file,
                    "tone3000_id": t3kid, "has_assignment": has_assignment,
                    "vst_path": vst_path, "vst_format": vst_format,
                    "vst_state": vst_state,
                }

        # Pattern-based category fallback for gears that lack an
        # `rs_to_real.json` entry. The gear map doesn't include
        # cab gears (they're handled by `rs_cab_to_ir.json`), so 49
        # cab codenames in this user's library landed in "Other"
        # before. Use the codename prefix to recover the category —
        # works for the canonical RS naming convention.
        def _category_from_codename(gear: str) -> str:
            g = (gear or "").lower()
            if g.startswith("bass_cab_") or g.startswith("cab_") or g == "cabinets":
                return "cab"
            if g.startswith("bass_pedal_") or g.startswith("pedal_") or g == "pedals":
                return "pedal"
            if g.startswith("rack_") or g == "racks":
                return "rack"
            if g.startswith("bass_amp_") or g.startswith("amp_"):
                return "amp"
            return "other"

        # Enumerate curated gain_variants for amps so the UI can render
        # one ▶ button per variant (clean/crunch/dist) in the gear card.
        # The on-disk filename for each variant is derived the same way
        # `_wire_curated_variants_to_presets` finds it — try the
        # readable-from-notes path first, then the legacy cryptic name.
        # Output: list of {level, file, kind, notes, model_id, rs_gain_range}
        # per variant whose NAM is actually on disk. Variants whose file
        # is missing get marked `available: false` so the UI can dim
        # the button instead of trying to audition nothing.
        def _variants_for(rs_gear: str, info: dict) -> list[dict]:
            out = []
            variants = info.get("gain_variants") or {}
            if not variants or _config_dir is None:
                return out
            subdir = _category_subdir_for_gear(rs_gear)
            amp_dir = _config_dir / "nam_models" / subdir
            for level, spec in variants.items():
                if not isinstance(spec, dict):
                    continue
                title = (spec.get("notes") or "").strip()
                model_id = spec.get("model_id")
                tone_id = spec.get("tone3000_id")
                rel = None
                if title:
                    candidate = amp_dir / f"{_safe_filename_human(title)}.nam"
                    if candidate.exists():
                        rel = f"{subdir}/{candidate.name}"
                if rel is None and model_id and tone_id:
                    legacy_name = (f"tone3000_{tone_id}_m{model_id}_"
                                   f"{_safe_filename(rs_gear)}.nam")
                    if (amp_dir / legacy_name).exists():
                        rel = f"{subdir}/{legacy_name}"
                out.append({
                    "level": level,
                    "file": rel,
                    "kind": "nam",
                    "available": rel is not None,
                    "notes": title,
                    "model_id": model_id,
                    "tone3000_id": tone_id,
                    "rs_gain_range": spec.get("rs_gain_range"),
                })
            return out

        # Cab mic-variants enrichment: for every cab in the user's
        # catalog, look up the rs_cab_mic_map entry (built from the
        # Wwise HIRC ↔ DIDX cross-reference). Each entry returns the
        # extracted IR file + a friendly label so the UI can render
        # one ▶ per mic position instead of generic "IR 0/1/2/…".
        # Filtering by `(irs_root / file).exists()` keeps the picker
        # honest: only the variants whose .wav is on disk show as
        # auditionable.
        mic_map = _load_rs_cab_mic_map()
        irs_root = _config_dir / "nam_irs" if _config_dir else None

        def _mic_variants_for(rs_gear: str) -> list[dict]:
            spec = mic_map.get(rs_gear) or {}
            ovr = _load_cab_overrides().get(rs_gear) or {}
            # Cabs we ship but the game has NO mic-map for (the 9 novelty cabs:
            # gramophone/radio/jukebox/boombox/hi-fi/PA — single manifest, no
            # _5c positions). Synthesize the standard mic×position grid from our
            # override so the catalog card still gets ▶ buttons + a default.
            if not spec and ovr:
                ir_dir = str(ovr.get("ir_dir") or "cabs").strip("/")
                prefix = str(ovr.get("prefix") or "")
                grid = []
                for mic_tok in ("dyn", "cond", "ribbon", "tube"):
                    for pos in ("cone", "edge", "offaxis"):
                        stem = f"{prefix}_{mic_tok}_{pos}" if prefix else f"{mic_tok}_{pos}"
                        grid.append({
                            "suffix": f"{mic_tok[0]}{pos[0]}",
                            "ir_index": len(grid),
                            "ir_file": f"{ir_dir}/{stem}.wav",
                            "label": f"{_OVR_MIC_LABEL[mic_tok]} {_OVR_POS_LABEL[pos]}",
                            "mic_type": mic_tok,
                            "position": _OVR_POS_DESC[pos],
                            "available": True,
                            "our_synth": True,
                        })
                return grid
            out = []
            for suffix, entry in sorted(spec.items(),
                                          key=lambda kv: kv[1].get("ir_index", 99)):
                f = entry.get("ir_file")
                label = entry.get("label") or suffix
                position = entry.get("position")
                # OUR shipped cab IRs override the RS mic table (rb_cab_overrides),
                # same as the per-song picker — always-available, label/position
                # derived from the authoritative effect_name.
                our = False
                if ovr:
                    o = _override_variant(ovr, entry)
                    if o:
                        f, label, position = o["ir_file"], o["label"], o["position"]
                        our = True
                available = True if our else (
                    bool(f) and irs_root is not None and (irs_root / f).exists())
                out.append({
                    "suffix": suffix,
                    "ir_index": entry.get("ir_index"),
                    "ir_file": f,
                    "label": label,
                    "mic_type": entry.get("mic_type"),
                    "position": position,
                    "available": available,
                    "our_synth": our,
                })
            return out

        # Surface gear that ISN'T used by any song yet so the user can browse
        # and audition the FULL catalog — e.g. bundled effect pedals (Bass
        # Phase, Sub Octave, …) that none of their tones happen to use. For
        # every rs_to_real pedal/amp/rack not already represented by a
        # preset_piece, synthesize a catalog entry; if the gear has a
        # bundled/installed primary VST, attach it (with has_assignment=True)
        # so the ▶ audition plays it at its defaults. Gear with no resolvable
        # VST shows as unassigned, ready for the Suggest/assign flow.
        # Cabs are intentionally EXCLUDED here: there are hundreds of cab
        # codenames, they can't be auditioned without an extracted IR, and
        # surfacing them all would bury the useful gear. Cabs still appear when
        # a song actually uses them (they're already in `best`).
        known_lookup = _build_known_vst_lookup()
        for gear, b in list(best.items()):
            if not gear or b.get("vst_path"):
                continue
            info = rs_map.get(gear) or {}
            cat = info.get("category") or _category_from_codename(gear)
            if cat == "cab":
                continue
            prim = _pick_installed_primary_vst(gear, known_lookup)
            if not prim:
                continue
            b["kind"] = "vst"
            b["file"] = None
            b["has_assignment"] = True
            b["vst_path"] = prim["vst_path"]
            b["vst_format"] = prim["vst_format"]
            b.setdefault("vst_state", None)

        for gear, info in rs_map.items():
            if gear.startswith("_") or not isinstance(info, dict) or gear in best:
                continue
            cat = info.get("category") or _category_from_codename(gear)
            if cat == "cab":
                continue
            prim = _pick_installed_primary_vst(gear, known_lookup)
            best[gear] = {
                "kind": "vst" if prim else "",
                "file": None,
                "tone3000_id": None,
                "has_assignment": bool(prim),
                "vst_path": prim["vst_path"] if prim else None,
                "vst_format": prim["vst_format"] if prim else None,
                "vst_state": None,
                # Browsable but NOT used by any song — so it must not claim to
                # cover its own slot ("assigned to itself"); it reads unassigned
                # until a song uses it or it's mapped to a game gear.
                "_synth_unused": True,
            }

        # Surface OUR bundled VSTs that aren't mapped to ANY RS gear — the
        # "extra gear" (Silver Jubilee, DS-2, CE-1…) built for manual use. The
        # catalog is keyed off the RS gear map, so without this these never
        # appear even though the .vst3 ships. Skip any whose VST is already
        # represented (the mapped amps/pedals) to avoid duplicates; category
        # comes from the vst/<amps|pedals|racks>/ subfolder.
        _used_vst = {Path(bb["vst_path"]).name.lower()
                     for bb in best.values() if bb.get("vst_path")}
        _bundled_cat = {"amps": "amp", "pedals": "pedal", "racks": "rack"}
        # Internal DSP that ships in vst/ but is NOT user-facing "gear": the master
        # RB Final Leveler + our generic Studio* rack utilities (StudioEQ, StudioComp,
        # StudioGraphicEQ…). They leaked in here as "Extra_…"; never surface them.
        def _is_internal_bundle(nm: str) -> bool:
            n = (nm or "").strip().lower()
            return n == "rb final leveler" or n.startswith("studio")
        for bp in _bundled_vst_plugins():
            if _is_internal_bundle(bp["name"]):
                continue
            fname = Path(bp["path"]).name
            if fname.lower() in _used_vst:
                continue
            cat = _bundled_cat.get(Path(bp["path"]).parent.name.lower())
            if not cat:
                continue
            # Never surface unmapped bundled PEDALS as "Extra_…" entries. vst/pedals/
            # ships old camelCase copies (BassEmulator.vst3) alongside the renamed
            # spaced bundles ("Bass Emulator.vst3"); the leftover copies aren't
            # referenced by any mapped gear, so they slipped through the exact-name
            # dedup above and showed up either as raw "Extra_<name>" rows or — when a
            # vst_display_names.json alias collided with a real pedal — as visual
            # duplicates (Bass Emulator, Bass Wah, Big Buzz…). Real pedals always
            # come from the RS gear map; anything left over here is noise.
            if cat == "pedal":
                continue
            key = "Extra_" + bp["name"]
            if key in best:
                continue
            best[key] = {
                "kind": "vst", "file": None, "tone3000_id": None,
                "has_assignment": True, "category": cat,
                "vst_path": bp["path"], "vst_format": bp["format"],
                "vst_state": None, "_synth_unused": True,
            }

        # Surface EVERY cab WE model (real_cab_catalog.json — the 58 curated
        # cabs) even if no downloaded song uses it, so the Cabs catalog shows
        # our full roster instead of only the ~25 the user's library happens to
        # reference. Each ships an override IR (always auditionable); the mic
        # grid comes from the RS mic-map or is synthesized from the override
        # (novelty cabs). This is scoped to OUR catalog — NOT the hundreds of
        # raw RS cab codenames excluded above.
        cab_catalog = (_load_cached_json("real_cab_catalog.json") or {}).get("cabs", {})
        for gear, entry in cab_catalog.items():
            if gear in best:
                continue
            ovr = _load_cab_overrides().get(gear) or {}
            ir_dir = str(ovr.get("ir_dir") or "cabs").strip("/")
            prefix = str(ovr.get("prefix") or "")
            stem = f"{prefix}_dyn_cone" if prefix else "dyn_cone"
            best[gear] = {
                "kind": "ir",
                "file": f"{ir_dir}/{stem}.wav",
                "tone3000_id": None,
                "has_assignment": True,
                "vst_path": None, "vst_format": None, "vst_state": None,
            }

        display_names = _load_vst_display_names()
        type_tags = _load_pedal_type_tags()
        cats: dict[str, list] = {}
        for gear, b in best.items():
            # Skip the generic parser-miss placeholders ('Cabinets'/'Amps'/
            # 'Pedals'/'Racks') — they're not real models the user can browse;
            # each song's real gear is recovered on open by the auto-fix.
            if gear in ("Cabinets", "Amps", "Pedals", "Racks"):
                continue
            info = rs_map.get(gear) or {}
            # Category: RS map first, then the entry's own (bundled-VST extras
            # carry their vst/<amps|pedals|racks>/ folder category), then the
            # codename-prefix fallback.
            category = info.get("category") or b.get("category") or _category_from_codename(gear)
            # Gear with an assigned VST shows its copyright-free display name
            # when available, even if the historical row is still kind="nam".
            # Route through _gear_display_name so cabs (absent from rs_to_real)
            # get their clone name too — same resolver the chain editor uses,
            # so the Gear catalog and Advanced show identical names.
            real_name = _gear_display_name(gear, info.get("name") or gear)
            if b.get("vst_path"):
                dn = display_names.get(_vst_display_stem(b["vst_path"]))
                if dn:
                    real_name = dn
            t3kid = b["tone3000_id"]
            meta = img_idx.get(t3kid) if t3kid else None
            variants = _variants_for(gear, info) if category == "amp" else []
            mic_variants = _mic_variants_for(gear) if category == "cab" else []
            vst_state = b["vst_state"]
            if category == "amp" and b.get("vst_path"):
                vst_state = _compute_gear_open_vst_state(gear, b["vst_path"])
            _mp = _gmap.get(gear) or {}
            _mapped_custom = _mp.get("custom")
            # IDENTITY: a game gear ALWAYS shows its own NATIVE bundled VST — never
            # a redirect target. This keeps its editor + face pointed at itself
            # even when a song slot is redirected (and repairs the earlier bug
            # where the stored piece got overwritten). The redirect is separate.
            _id_vst_path, _id_vst_format, _id_vst_state = b["vst_path"], b["vst_format"], vst_state
            if b.get("vst_path"):
                _native = _pick_installed_primary_vst(gear, known_lookup)
                if _native and _native.get("vst_path"):
                    _id_vst_path = _native["vst_path"]
                    _id_vst_format = _native.get("vst_format") or "VST3"
                    _id_vst_state = _compute_gear_open_vst_state(gear, _id_vst_path)
            # Cab file: prefer the song's own IR (override-substituted); when it has
            # NONE (kind='none') but we SHIP a bundled IR for THIS specific cab,
            # surface that installed IR so the catalog shows it as assigned/
            # auditionable instead of a bare "unassigned". Membership-gated (only
            # cabs we actually modeled — not the generic default substitute) and
            # only resolves to a file that exists on disk.
            _disp_file = _apply_cab_override(b["file"])
            if not _disp_file and category == "cab":
                _ovr_map = _load_cab_overrides()
                _ovr_base = re.sub(r"_[a-z0-9]{2}$", "", str(gear), flags=re.I)
                if isinstance(_ovr_map.get(gear), dict) or isinstance(_ovr_map.get(_ovr_base), dict):
                    _disp_file = _override_ir_for_cab(gear, irs_root)
            cats.setdefault(category, []).append({
                "rs_gear": gear,
                "real_name": real_name,
                "type_tags": type_tags.get(gear, ""),
                "make": info.get("make", ""),
                "model": info.get("model", ""),
                "category": category,
                "assigned": b["has_assignment"],
                # Factory game gear vs a post-factory "DLC"/new addition. A factory
                # native reads assigned by default; a DLC one reads unassigned
                # until mapped (like a user-added custom gear).
                "is_new": gear in _NEW_DLC_GEARS,
                # Which custom gear this game gear's song slot is redirected to.
                "mapped_custom": _mapped_custom,
                "mapped_name": _custom_names.get(_mapped_custom) if _mapped_custom else None,
                # Slots this game gear COVERS: its own (unless redirected away) +
                # any other slot redirected to it. Same field custom gear uses.
                "assigned_rs": sorted(
                    ([real_name] if (_mapped_custom is None and not b.get("_synth_unused")) else [])
                    + _covers.get(gear, []),
                    key=lambda s: s.lower()),
                "kind": b["kind"],
                # Show OUR cab IR when we ship one (rb_cab_overrides) so the
                # catalog card + its active mic highlight match the per-song
                # editor and the ▶ audition plays ours. No-op for non-cab files.
                "file": _disp_file,
                "vst_path": _id_vst_path,
                "vst_format": _id_vst_format,
                "vst_state": _id_vst_state,
                "tone3000_id": t3kid,
                "tone3000_title": (meta or {}).get("title"),
                "image": (meta or {}).get("image"),
                "tone3000_url": Tone3000Client.tone_page_url(t3kid) if t3kid else None,
                # `rs_order` lets the UI sort gears in the same order
                # the game does (the gear-map order). Missing for gears
                # we couldn't find in the manifest — sorted last.
                "rs_order": info.get("rs_order"),
                # Per-variant audition buttons in the Gear card. Empty
                # list when the amp has no `gain_variants` curation.
                "variants": variants,
                # Per-mic-position audition buttons for cabs (similar
                # shape: {label, ir_file, available}). Empty for non-cab
                # categories and for cabs without an entry in
                # rs_cab_mic_map.json.
                "mic_variants": mic_variants,
            })

        # User-defined custom gear — append each to its category so it shows in
        # the Gear browser, the node-editor palette, and (via /gears_catalog)
        # the per-song picker. `rs_order` is None → these sort after the game's
        # Collapse redundant native aliases: several rs_gear codenames can map to
        # the SAME bundled VST (e.g. Epiphone Electar variants → EpicallCentura),
        # so they render with an identical name + plugin. Merge them into ONE
        # entry (unioning assignment coverage) so the browser doesn't show three
        # "Epicall Centura". Keyed by (name, vst_path/file) — gears with no
        # backing keep their own rs_gear key (never merged). Custom gears are
        # added AFTER this pass and are never collapsed.
        for cat, lst in list(cats.items()):
            seen: dict = {}
            out = []
            for it in lst:
                backing = it.get("vst_path") or it.get("file")
                if not backing:
                    out.append(it)
                    continue
                key = ((it.get("real_name") or "").strip().lower(), backing)
                keep = seen.get(key)
                if keep is None:
                    seen[key] = it
                    out.append(it)
                    continue
                names = set(keep.get("assigned_rs") or []) | set(it.get("assigned_rs") or [])
                keep["assigned_rs"] = sorted(names, key=lambda s: s.lower())
                keep["assigned"] = bool(keep.get("assigned") or it.get("assigned"))
            cats[cat] = out

        # gear within their category.
        for rec in _load_custom_gear():
            cat = rec.get("category") or "other"
            if cat not in cats:
                cat = "other"
            item = _custom_gear_catalog_item(rec)
            item["category"] = cat
            # Copyright-free names of the game gears this custom is assigned to.
            item["assigned_rs"] = sorted(_covers.get(rec.get("rs_gear"), []),
                                         key=lambda s: s.lower())
            cats.setdefault(cat, []).append(item)

        for lst in cats.values():
            # Primary sort: the game's psarc order (same order as the
            # in-game tone designer). Secondary: name fallback for
            # entries missing rs_order. Tertiary: case-insensitive
            # name so similarly-ordered gears land alphabetically.
            lst.sort(key=lambda g: (
                g["rs_order"] if g["rs_order"] is not None else 10**9,
                (g["real_name"] or "").lower(),
            ))

        order = ["amp", "pedal", "cab", "rack", "other"]
        ordered = {c: cats[c] for c in order if c in cats}
        for c in cats:
            ordered.setdefault(c, cats[c])
        return {"categories": ordered,
                "counts": {c: len(v) for c, v in ordered.items()}}

    # ── Manage tab: inventory + delete + purge of downloaded NAMs/IRs ─

    @app.get("/api/plugins/rig_builder/nam_inventory")
    def nam_inventory():
        """Walk nam_models/ + nam_irs/ and return every file grouped by
        category subdir, with size + DB usage info so the Manage tab
        can render the "this NAM is used by N presets" hint.

        Categories follow the v1.2 subdir layout: amps / pedals / racks
        / cabs / other. Files that live at the root of nam_models/
        (i.e. not yet migrated, or user uploads via nam_tone) fall
        into the "other" bucket alongside genuinely orphan files.
        """
        if _config_dir is None:
            return JSONResponse({"error": "config_dir unavailable"}, 500)
        conn = _get_conn()
        rs_map = _load_rs_to_real()

        # Pull every DB row that links a filename → an rs_gear so we
        # can annotate each on-disk file with its assigned gear(s).
        # Use a single sweep + in-Python aggregation; the DB is tiny.
        usage_by_file: dict[str, dict] = {}
        for r in conn.execute(
            "SELECT pp.file, pp.rs_gear_type, pp.tone3000_id, "
            "       p.id AS preset_id, p.name AS preset_name "
            "FROM preset_pieces pp "
            "LEFT JOIN presets p ON p.id = pp.preset_id "
            "WHERE pp.file IS NOT NULL AND pp.file != ''"
        ).fetchall():
            fname, gear, tid, preset_id, preset_name = r
            slot = usage_by_file.setdefault(fname, {
                "rs_gears": set(),
                "tone3000_ids": set(),
                "preset_ids": set(),
                "preset_names": set(),
            })
            if gear:
                slot["rs_gears"].add(gear)
            if tid:
                slot["tone3000_ids"].add(int(tid))
            if preset_id is not None:
                slot["preset_ids"].add(int(preset_id))
            if preset_name:
                slot["preset_names"].add(preset_name)

        def _enrich(fname: str, abs_path: Path, kind: str) -> dict:
            """Build one inventory row."""
            usage = usage_by_file.get(fname, {})
            rs_gears = sorted(usage.get("rs_gears", set()))
            real_names = []
            for g in rs_gears:
                info = rs_map.get(g) or {}
                real_names.append(_gear_display_name(g, info.get("name") or g))
            try:
                size = abs_path.stat().st_size
            except OSError:
                size = 0
            return {
                "name": fname,
                "kind": kind,
                "size_bytes": size,
                "rs_gears": rs_gears,
                "real_names": real_names,
                "tone3000_ids": sorted(usage.get("tone3000_ids", set())),
                "preset_count": len(usage.get("preset_ids", set())),
                "preset_names": sorted(usage.get("preset_names", set()))[:5],
                "orphan": not rs_gears,    # nothing in the DB references it
            }

        # Per-file bucket classifier. The subdir a file lives in is a
        # weak hint — the game cab IRs live under
        # `nam_irs/rocksmith/`, but those are conceptually cabs and
        # should show up under "Cabs" in the UI, not a separate
        # "rocksmith" bucket. So we do the classification per-file
        # using whatever signal we can get:
        #
        #   1. DB join: the file's rs_gear_type → rs_to_real category
        #      (most reliable when the file is in use by a preset).
        #   2. Filename hint: our auto-downloader names files with the
        #      rs_gear embedded (`tone3000_<tid>_m<mid>_<rs_gear>.nam`),
        #      so we can recover the category even when the file is
        #      currently orphan.
        #   3. Last resort: the subdir name itself (works for the
        #      curated layout amps/pedals/racks/cabs/other and for
        #      anything the user manually filed).
        import re as _re_inv
        _name_to_gear = _re_inv.compile(
            r"^tone3000_\d+_m\d+_([^.]+)\.(nam|wav)$")

        def _bucket_for(filename: str, abs_path: Path, kind: str,
                        subdir_hint: str) -> str:
            # Tier 1: DB lookup
            usage = usage_by_file.get(filename, {})
            for g in usage.get("rs_gears", set()):
                info = rs_map.get(g) or {}
                cat = info.get("category")
                if cat:
                    return _CATEGORY_SUBDIR.get(cat, "other")
            # Tier 2: filename pattern (auto-downloader convention)
            m = _name_to_gear.match(Path(filename).name)
            if m:
                info = rs_map.get(m.group(1)) or {}
                cat = info.get("category")
                if cat:
                    return _CATEGORY_SUBDIR.get(cat, "other")
            # Tier 3: subdir name (handles legacy `rocksmith/` extracts
            # via this special case — they're always cab IRs by
            # construction).
            if subdir_hint == "rocksmith":
                return "cabs"
            if subdir_hint in _CATEGORY_SUBDIR.values():
                return subdir_hint
            return "other"

        result: dict[str, dict] = {}
        for root_name, kind in [("nam_models", "nam"), ("nam_irs", "ir")]:
            root = _config_dir / root_name
            if not root.exists():
                continue
            for entry in sorted(root.iterdir()):
                if entry.is_dir():
                    files = sorted(p for p in entry.iterdir() if p.is_file())
                    for p in files:
                        if p.name.startswith("."):
                            continue
                        rel = f"{entry.name}/{p.name}"
                        bucket = _bucket_for(rel, p, kind, entry.name)
                        b = result.setdefault(bucket, {"files": []})
                        b["files"].append(_enrich(rel, p, kind))
                elif entry.is_file() and not entry.name.startswith("."):
                    # Flat file at the root — pre-migration leftover OR
                    # a user upload that bypassed our download path.
                    bucket = _bucket_for(entry.name, entry, kind, "")
                    b = result.setdefault(bucket, {"files": []})
                    b["files"].append(_enrich(entry.name, entry, kind))

        # Final shape: counts + total bytes per bucket.
        for bucket, b in result.items():
            b["count"] = len(b["files"])
            b["total_bytes"] = sum(f["size_bytes"] for f in b["files"])
        # Stable bucket order: known categories first, "other" last.
        order = ["amps", "pedals", "racks", "cabs", "other"]
        ordered = {k: result[k] for k in order if k in result}
        for k in result:
            ordered.setdefault(k, result[k])
        totals = {
            "count": sum(b["count"] for b in result.values()),
            "total_bytes": sum(b["total_bytes"] for b in result.values()),
        }
        return {"buckets": ordered, "totals": totals}

    def _resolve_inventory_path(rel: str) -> tuple[Path | None, str | None]:
        """Resolve `<subdir>/<name>` to an absolute path under
        nam_models/ or nam_irs/, returning the matching kind label.

        Refuses paths that try to escape the storage roots (path
        traversal guard — same shape as nam_tone's _safe_child). The
        UI sends paths verbatim from the inventory listing, so we
        don't trust them.
        """
        if not rel or _config_dir is None:
            return None, None
        for root_name, kind in [("nam_models", "nam"), ("nam_irs", "ir")]:
            root = (_config_dir / root_name).resolve()
            try:
                candidate = (root / rel).resolve()
                candidate.relative_to(root)   # raises if outside
            except (ValueError, OSError):
                continue
            if candidate.exists() and candidate.is_file():
                return candidate, kind
        return None, None

    @app.delete("/api/plugins/rig_builder/nam_file")
    def delete_nam_file(path: str):
        """Delete one NAM/IR file + scrub every DB reference to it.

        Body via query string (DELETE bodies are awkward across clients):
            DELETE /nam_file?path=amps/foo.nam

        Behaviour:
          - Unlinks the file.
          - Sets `preset_pieces.file = NULL`, `kind = 'none'` for every
            row that referenced it (the gear becomes Pending again).
          - Clears `presets.model_file` / `presets.ir_file` matches so
            the engine doesn't try to load a deleted file.
          - Returns a summary of what got nuked so the UI can refresh.
        """
        abs_path, kind = _resolve_inventory_path(path)
        if abs_path is None:
            return JSONResponse({"error": f"file not found: {path}"}, 404)
        try:
            abs_path.unlink()
        except OSError as e:
            return JSONResponse({"error": f"unlink failed: {e}"}, 500)
        conn = _get_conn()
        cur = conn.cursor()
        # NULL the file + downgrade kind so the gear surfaces as
        # Pending again on the next /song fetch. Don't drop the row
        # entirely — the user might want to re-assign without losing
        # their bypass state.
        cur.execute(
            "UPDATE preset_pieces SET file = NULL, kind = 'none', "
            "       tone3000_id = NULL, assigned_mode = NULL "
            "WHERE file = ?",
            (path,),
        )
        pieces_cleared = cur.rowcount
        cur.execute(
            "UPDATE presets SET model_file = '' WHERE model_file = ?",
            (path,),
        )
        presets_model = cur.rowcount
        cur.execute(
            "UPDATE presets SET ir_file = '' WHERE ir_file = ?",
            (path,),
        )
        presets_ir = cur.rowcount
        conn.commit()
        return {
            "deleted": True, "path": path, "kind": kind,
            "pieces_cleared": pieces_cleared,
            "presets_model_cleared": presets_model,
            "presets_ir_cleared": presets_ir,
        }

    # Internal endpoints kept for debugging / manual re-runs. The UI
    # doesn't surface them — the curated-variants preload calls these
    # steps automatically (rename before, wire after).
    @app.post("/api/plugins/rig_builder/rename_to_readable")
    def rename_to_readable():
        return _rename_legacy_filenames_to_readable()

    @app.post("/api/plugins/rig_builder/wire_curated_files")
    def wire_curated_files():
        return _wire_curated_variants_to_presets()

    @app.get("/api/plugins/rig_builder/preload_status")
    def preload_status():
        """Live state of the curated-variants preload worker. The UI
        polls this every ~500ms to drive the progress bar + "now
        downloading X" indicator.

        Returns a snapshot under a lock so the polling client never
        sees a partial update. `running=False` AND `started_at` set
        means the most recent run finished — UI uses that combination
        to know when to stop polling and show the final summary.
        """
        with _preload_lock:
            return dict(_preload_state)

    @app.post("/api/plugins/rig_builder/preload_curated_variants")
    def preload_curated_variants():
        """Spawn the curated-variants preload worker.

        Returns immediately with `{started: true, total: N}`. Actual
        progress is published in `_preload_state` and polled via GET
        `/preload_status`. Re-clicking while a run is in flight
        returns `{started: false, reason: 'already_running'}` instead
        of double-starting.

        The worker uses parallel download workers (3) gated by a
        global rate-limit lock so the combined throughput stays inside
        tone3000's 100 req/min budget. `download_model_file` already
        has its own 429-retry-with-backoff as a belt-and-braces safety
        net in case the budget is tighter than expected.
        """
        global _preload_thread
        with _preload_lock:
            if _preload_state["running"]:
                return {"started": False, "reason": "already_running"}
            # Reset state. We rebuild the job list inside the thread,
            # but seed `total` here so the UI shows a sensible upper
            # bound from the first poll.
            rs_map = _load_rs_to_real() or {}
            client = _get_t3k_client()
            if not client.has_api_access:
                return JSONResponse({"error": "tone3000 not connected"}, 400)
            jobs = []
            for rs_gear, info in rs_map.items():
                if not isinstance(info, dict) or info.get("category") != "amp":
                    continue
                for level, spec in (info.get("gain_variants") or {}).items():
                    if isinstance(spec, dict) and spec.get("tone3000_id"):
                        jobs.append((rs_gear, level, spec))
            _preload_state.update({
                "running": True,
                "total": len(jobs),
                "done": 0,
                "current": "",
                "downloaded": 0,
                "already_present": 0,
                "wired": 0,
                "failed": [],
                "failed_permanent": 0,
                "errors": [],
                "started_at": time.time(),
                "finished_at": None,
            })
        _preload_thread = threading.Thread(
            target=_preload_worker, args=(jobs,), daemon=True
        )
        _preload_thread.start()
        return {"started": True, "total": len(jobs)}

    @app.post("/api/plugins/rig_builder/download_tone3000_gears")
    def download_tone3000_gears():
        """Download each curated amp's clean/crunch/dist captures and create ONE
        new gear per amp bundling them (auto-switched by mapped gain). Builds the
        library only — no song is touched; run Rescan all to map songs onto them.
        Shares `_preload_state`/`/preload_status` for progress."""
        global _preload_thread
        with _preload_lock:
            if _preload_state["running"]:
                return {"started": False, "reason": "already_running"}
            client = _get_t3k_client()
            if not client.has_api_access:
                return JSONResponse({"error": "tone3000 not connected"}, 400)
            rs_map = _load_rs_to_real() or {}
            amps, total = [], 0
            for rs_gear, info in rs_map.items():
                if not isinstance(info, dict) or info.get("category") != "amp":
                    continue
                specs = {lvl: spec for lvl, spec in (info.get("gain_variants") or {}).items()
                         if isinstance(spec, dict) and spec.get("tone3000_id")}
                if specs:
                    amps.append((rs_gear, info, specs))
                    total += len(specs)
            if not amps:
                return {"started": False, "reason": "no_amps_with_variants"}
            _preload_state.update({
                "running": True, "total": total, "done": 0, "current": "",
                "downloaded": 0, "already_present": 0, "wired": 0, "failed": [],
                "failed_permanent": 0, "errors": [], "gears_created": 0,
                "started_at": time.time(), "finished_at": None,
            })
        _preload_thread = threading.Thread(
            target=_download_tone3000_gears_worker, args=(amps,), daemon=True
        )
        _preload_thread.start()
        return {"started": True, "total": total, "amps": len(amps)}

    # ── Gear-centric replace + per-song variant override ────────────────
    # Three endpoints powering the new song/gear editor UX:
    #   GET  /gears_in_category/{cat}    list candidate replacements
    #   POST /piece_variant_override     force a variant for one preset
    #   POST /gear/replace_with          swap one gear to another gear's
    #                                      current All Gear assignment

    # Single source of truth: the module-level resolver. Aliased here
    # so the gear-centric endpoints below keep their original name.
    _resolve_gear_file = _resolve_gear_assignment

    @app.post("/api/plugins/rig_builder/consolidate_gear_assignments")
    def consolidate_gear_assignments(data: dict = Body(default={})):
        """Make gear assignments GLOBAL by collapsing each gear's per-song
        divergence into one assignment (NAM = curated, VST = most recent;
        cabs keep their per-song mic). POST `{}` (or `{"apply": false}`) for a
        dry-run preview; POST `{"apply": true}` to write — which first copies
        the DB to `<db>.pre-consolidate.bak` so the operation is reversible.
        """
        apply = bool(data.get("apply"))
        conn = _get_conn()
        backup = None
        if apply and _db_path:
            backup = f"{_db_path}.pre-consolidate.bak"
            try:
                shutil.copy2(_db_path, backup)
            except OSError as e:
                return JSONResponse(
                    {"error": f"backup failed, not applying: {e}"}, 500)
        report = _consolidate_gear_assignments(conn, apply=apply)
        if backup:
            report["backup"] = backup
        return report

    @app.get("/api/plugins/rig_builder/gears_in_category/{category}")
    def gears_in_category(category: str):
        """List curated gears in `category` (amp/pedal/rack/cab) along
        with whatever's already assigned, so the front-end can render a
        gear-centric replacement picker — instead of dumping every NAM
        file in the bucket, the user sees one card per amp/pedal with
        its photo + variant count.

        Used by the new "Replace amp with…" / "Replace pedal with…"
        modal in the Gear and Song views. Sorted by rs_order so the
        picker mirrors the in-game tone designer's order.
        """
        rs_map = _load_rs_to_real() or {}
        img_idx = _tone_image_index() if category == "amp" else {}
        out = []
        is_cab = category.lower() == "cab"
        # Cabs in rs_to_real.json are stored per mic-position (e.g.
        # `Bass_Cab_AT1150BC_5c`, `_5e`, `_co`, … — 147 entries). The
        # picker should show ONE entry per base cab, so collapse the
        # suffix and dedupe; pick the entry with the most curated info
        # as the representative. Mic-position picking happens elsewhere
        # (rs_cab_mic_map / per-song picker).
        mic_suffix_re = re.compile(r"_(?:[0-9]?[a-z]{1,2})$")
        seen_bases: set[str] = set()
        seen_bundled: set[str] = set()
        for k, info in rs_map.items():
            if not isinstance(info, dict):
                continue
            cat = (info.get("category") or "").lower()
            if cat != category.lower():
                continue
            # Collapse gears that REUSE the same bundled VST (e.g. Amp_AT20 ->
            # Bass_Amp_BT975B's Sampleg SBT-CL) so the picker lists it once.
            b = _gear_bundled_vst(k)
            if b:
                if b in seen_bundled:
                    continue
                seen_bundled.add(b)
            if is_cab:
                base = mic_suffix_re.sub("", k)
                if base in seen_bases:
                    continue
                seen_bases.add(base)
                display_key = base
            else:
                display_key = k
            variants = info.get("gain_variants") or {}
            # Photo: pick the first variant's tone3000_id and look up the
            # cached image. For non-variant gears (most cabs/pedals)
            # there's no photo — UI shows a placeholder.
            t3kid = None
            for spec in variants.values():
                if isinstance(spec, dict) and spec.get("tone3000_id"):
                    t3kid = spec["tone3000_id"]
                    break
            meta = img_idx.get(t3kid) if t3kid else None
            # Cab variant count comes from rs_cab_mic_map (mic positions),
            # not from gain_variants.
            if is_cab:
                mic_count = len(_load_rs_cab_mic_map().get(display_key) or {})
                variant_count = mic_count
                variant_levels = list(
                    (_load_rs_cab_mic_map().get(display_key) or {}).keys())
            else:
                variant_count = len(variants)
                variant_levels = list(variants.keys())
            out.append({
                "rs_gear": display_key,
                "name": _gear_display_name(display_key, info.get("name") or display_key),
                "make": info.get("make", ""),
                "model": info.get("model", ""),
                "variant_count": variant_count,
                "variant_levels": variant_levels,
                "rs_order": info.get("rs_order"),
                "image": (meta or {}).get("image"),
            })
        out.sort(key=lambda g: (g["rs_order"] if g["rs_order"] is not None else 10**9,
                                 (g["name"] or "").lower()))
        return {"category": category, "gears": out}

    @app.get("/api/plugins/rig_builder/gear_photo/{rs_gear}")
    def gear_photo(rs_gear: str):
        """Serve the gear photo for `rs_gear` (PNG), if one is present.

        Photos live under amp_photos/, pedal_photos/, rack_photos/,
        cab_photos/ when available. The redesigned Songs editor uses
        these for the per-piece thumbnail.

        Returns 404 when no photo exists for this gear; the UI falls
        back to a "no photo" placeholder. Strong-caches via ETag because
        these files never change once present — the browser keeps the
        thumbnails warm across full chain re-renders.
        """
        path = _find_gear_photo(rs_gear)
        if path is None:
            # Don't let browsers cache the 404 — earlier lookup misses
            # (e.g. branded cabs before the case-insensitive fix) would
            # stick around forever and keep showing the placeholder
            # even after we patched the backend. `no-store` forces a
            # fresh hit every time the user opens the catalog/editor.
            return JSONResponse(
                {"error": "no photo"}, 404,
                headers={"Cache-Control": "no-store"})
        try:
            data = path.read_bytes()
        except OSError as e:
            return JSONResponse({"error": str(e)}, 500)
        # Cheap ETag — mtime + size. The file is immutable once written,
        # so this lets the browser 304 every refresh.
        try:
            st = path.stat()
            etag = f'W/"{int(st.st_mtime)}-{st.st_size}"'
        except OSError:
            etag = None
        headers = {"Cache-Control": "public, max-age=86400"}
        if etag:
            headers["ETag"] = etag
        return Response(content=data, media_type="image/png", headers=headers)

    @app.get("/api/plugins/rig_builder/asset/font/{name}")
    def asset_font(name: str):
        """Serve a bundled OFL font (TTF) for the in-app canvas pedal UIs.

        The web UI (`pedal_canvas.js`) recreates each bundled pedal's face on a
        <canvas>, matching the C++ VST look — so it needs the same fonts the
        plugins embed (Bebas Neue / Barlow / Anton / Crete Round). They live in
        `assets/fonts/`. Only a fixed allow-list is served (no path traversal).
        """
        allow = {"bebas", "barlow", "anton", "crete", "graffiti", "ink"}
        key = (name or "").split(".")[0].lower()
        if key not in allow:
            return JSONResponse({"error": "unknown font"}, 404)
        path = _plugin_dir / "assets" / "fonts" / f"{key}.ttf"
        if not path.exists():
            return JSONResponse({"error": "missing"}, 404)
        try:
            data = path.read_bytes()
        except OSError as e:
            return JSONResponse({"error": str(e)}, 500)
        return Response(content=data, media_type="font/ttf",
                        headers={"Cache-Control": "public, max-age=604800"})

    @app.get("/api/plugins/rig_builder/asset/pedal_canvas.js")
    def asset_pedal_canvas():
        """Serve pedal_canvas.js — the in-app canvas recreations of the bundled
        pedal UIs. plugin.json only loads screen.js, so screen.js injects this
        as a <script>. Served from the plugin root."""
        path = _plugin_dir / "pedal_canvas.js"
        if not path.exists():
            return JSONResponse({"error": "missing"}, 404)
        try:
            data = path.read_bytes()
        except OSError as e:
            return JSONResponse({"error": str(e)}, 500)
        return Response(content=data, media_type="application/javascript",
                        headers={"Cache-Control": "no-cache"})

    @app.get("/api/plugins/rig_builder/asset/rb.css")
    def asset_rb_css():
        """Serve the plugin's self-contained Tailwind stylesheet. The host
        regenerates tailwind.min.css to cover an installed plugin's classes,
        but several builds skip that rebuild (no node), leaving classes the
        host itself doesn't use undefined (white borders, collapsed `gap-x-4`,
        etc.). We ship our own Tailwind build (`tools/build_tailwind.sh` →
        assets/rb.css, scoped to #rb-root) so the look is correct everywhere,
        independent of the host rebuild. screen.js injects it as a <link>."""
        path = _plugin_dir / "assets" / "rb.css"
        if not path.exists():
            return JSONResponse({"error": "missing"}, 404)
        try:
            data = path.read_bytes()
        except OSError as e:
            return JSONResponse({"error": str(e)}, 500)
        return Response(content=data, media_type="text/css",
                        headers={"Cache-Control": "no-cache"})

    @app.post("/api/plugins/rig_builder/piece_variant_override")
    def piece_variant_override(data: dict = Body(...)):
        """Force a specific variant on ONE preset's gear piece.

        Body:
          - `preset_id`: int
          - `rs_gear`:   the gear's rs_gear_type
          - `variant`:   "auto" to clear the override, or a level name
                         ("clean"/"crunch"/"dist"/etc.)

        Updates the matching preset_pieces row's file + assigned_mode.
        Setting variant="auto" returns to the gain-driven pick (mode
        flips back to "auto"). Any other value pins manually (mode
        "manual" — survives Remap all).
        """
        try:
            preset_id = int(data["preset_id"])
            rs_gear = str(data["rs_gear"])
            level = str(data.get("variant") or "auto").lower().strip()
        except (KeyError, TypeError, ValueError):
            return JSONResponse({"error": "preset_id, rs_gear, variant required"}, 400)
        conn = _get_conn()
        row = conn.execute(
            "SELECT params_json FROM preset_pieces "
            "WHERE preset_id = ? AND rs_gear_type = ? LIMIT 1",
            (preset_id, rs_gear),
        ).fetchone()
        if not row:
            return JSONResponse({"error": "no matching piece"}, 404)
        # Pull the row's Gain knob value so "auto" can use it for picking,
        # and so explicit levels still record a sensible tone3000_id.
        try:
            knobs = json.loads(row[0] or "{}") or {}
        except (ValueError, TypeError):
            knobs = {}
        rs_gain = knobs.get("Gain")
        try:
            rs_gain = float(rs_gain) if rs_gain is not None else None
        except (ValueError, TypeError):
            rs_gain = None
        res = _resolve_gear_file(
            rs_gear, None if level == "auto" else level, rs_gain)
        if res is None or (not res.get("file") and not res.get("vst_path")):
            return JSONResponse({"error": f"variant '{level}' has no file on disk for {rs_gear}"}, 404)
        mode = "auto" if level == "auto" else "manual"
        with _lock:
            if res["kind"] == "vst":
                conn.execute(
                    "UPDATE preset_pieces "
                    "SET kind = 'vst', file = NULL, tone3000_id = NULL, "
                    "    vst_path = ?, vst_format = ?, vst_state = ?, "
                    "    assigned_mode = ? "
                    "WHERE preset_id = ? AND rs_gear_type = ?",
                    (res["vst_path"], res.get("vst_format") or "VST3",
                     res.get("vst_state"), mode, preset_id, rs_gear),
                )
            else:
                conn.execute(
                    "UPDATE preset_pieces "
                    "SET file = ?, kind = ?, tone3000_id = ?, "
                    "    vst_path = NULL, vst_format = NULL, vst_state = NULL, "
                    "    assigned_mode = ? "
                    "WHERE preset_id = ? AND rs_gear_type = ?",
                    (res["file"], res["kind"] or "nam",
                     res.get("tone3000_id"), mode, preset_id, rs_gear),
                )
            _recompute_preset_primaries(conn, preset_id)
            conn.commit()
        return {"ok": True, **res,
                "variant": level, "assigned_mode": mode}

    @app.post("/api/plugins/rig_builder/gear/replace_with")
    def gear_replace_with(data: dict = Body(...)):
        """Swap one gear's assignment for another.

        Body:
          - `from_rs_gear`: e.g. "Amp_MarshallJCM800" — the gear whose
                             rows we're rewriting
          - `to_rs_gear`:   e.g. "Amp_MarshallPlexi" — the gear whose
                             curated variants we'll use
          - `preset_id`:    optional. If given, scope to a single song's
                             preset. Omit to bulk-swap across every song.

        Effect: every matching preset_pieces row with rs_gear_type=from
        is rewritten to the current All Gear assignment for `to_rs_gear`
        (including VST path + VST state). If the target gear is not assigned
        yet, falls back to the curated resolver, with amp variants picked
        per-row by that row's Gain knob. Marked manual/manual_vst so Remap all
        doesn't undo it.
        """
        try:
            from_gear = str(data["from_rs_gear"])
            to_gear = str(data["to_rs_gear"])
        except (KeyError, TypeError):
            return JSONResponse({"error": "from_rs_gear, to_rs_gear required"}, 400)
        preset_id_filter = data.get("preset_id")
        try:
            preset_id_filter = int(preset_id_filter) if preset_id_filter is not None else None
        except (ValueError, TypeError):
            preset_id_filter = None
        if from_gear == to_gear:
            return {"ok": True, "noop": True}
        # We used to pre-validate that the target gear had `gain_variants`
        # (for amps) or extracted IRs (for cabs) before walking rows.
        # That blocked pedals/racks (which never have gain_variants) and
        # cabs whose base form isn't a literal key in rs_to_real.json.
        # Instead, let _resolve_gear_file do the talking: when it can't
        # find ANY file for `to_gear` we count those rows as skipped
        # and report 0 updates back to the UI.
        conn = _get_conn()
        # Walk every row of the source gear, resolve the replacement's
        # variant for each row's Gain, then update.
        if preset_id_filter is not None:
            rows = conn.execute(
                "SELECT id, preset_id, params_json FROM preset_pieces "
                "WHERE rs_gear_type = ? AND preset_id = ?",
                (from_gear, preset_id_filter),
            ).fetchall()
        else:
            rows = conn.execute(
                "SELECT id, preset_id, params_json FROM preset_pieces "
                "WHERE rs_gear_type = ?",
                (from_gear,),
            ).fetchall()
        updated = 0
        skipped = 0
        affected_presets = set()
        with _lock:
            for pid_row, preset_id, params_json in rows:
                try:
                    knobs = json.loads(params_json or "{}") or {}
                except (ValueError, TypeError):
                    knobs = {}
                rs_gain = knobs.get("Gain")
                try:
                    rs_gain = float(rs_gain) if rs_gain is not None else None
                except (ValueError, TypeError):
                    rs_gain = None
                res = _resolve_song_swap_assignment(to_gear, params_json, rs_gain)
                if res is None or (not res.get("file") and not res.get("vst_path")):
                    skipped += 1
                    continue
                # Promote rs_gear_type to the TARGET gear too — the
                # chain card now shows the new gear's name + photo +
                # variants. The link to the PSARC tone is via
                # tone_mappings (filename + tone_key + preset_id),
                # NOT rs_gear_type, so nothing else breaks.
                if res["kind"] == "vst":
                    conn.execute(
                        "UPDATE preset_pieces "
                        "SET rs_gear_type = ?, kind = 'vst', "
                        "    file = NULL, tone3000_id = NULL, "
                        "    vst_path = ?, vst_format = ?, vst_state = ?, "
                        "    assigned_mode = 'manual_vst' "
                        "WHERE id = ?",
                        (to_gear, res["vst_path"],
                         res.get("vst_format") or "VST3",
                         res.get("vst_state"), pid_row),
                    )
                else:
                    conn.execute(
                        "UPDATE preset_pieces "
                        "SET rs_gear_type = ?, file = ?, kind = ?, "
                        "    tone3000_id = ?, "
                        "    vst_path = NULL, vst_format = NULL, vst_state = NULL, "
                        "    assigned_mode = 'manual' "
                        "WHERE id = ?",
                        (to_gear, res["file"], res["kind"] or "nam",
                         res.get("tone3000_id"), pid_row),
                    )
                updated += 1
                affected_presets.add(preset_id)
            for pid in affected_presets:
                _recompute_preset_primaries(conn, pid)
            conn.commit()
        return {"ok": True, "from": from_gear, "to": to_gear,
                "pieces_updated": updated, "skipped": skipped,
                "presets_affected": len(affected_presets)}

    @app.post("/api/plugins/rig_builder/nam_purge")
    def purge_nam(data: dict = Body(...)):
        """Bulk-delete a category (or everything).

        Body:
          - `bucket`: optional. "amps" / "pedals" / "racks" / "cabs" /
            "other" — restrict to this subdir. Omit for "purge ALL".
          - `kind`: optional. "nam" | "ir" — restrict to NAM or IR
            files only. Omit for both.
          - `confirm`: required, must be `true`. Belt-and-braces against
            accidental clicks; the UI ships it explicitly with the
            confirm-dialog.

        The endpoint walks the inventory and calls the single-file
        delete path for each match. Slower than a raw rmtree but it
        keeps the DB clean-up identical for both flows.
        """
        if not data.get("confirm"):
            return JSONResponse({"error": "confirm=true required"}, 400)
        if _config_dir is None:
            return JSONResponse({"error": "config_dir unavailable"}, 500)
        bucket_filter = (data.get("bucket") or "").strip().lower() or None
        kind_filter = (data.get("kind") or "").strip().lower() or None
        if kind_filter and kind_filter not in ("nam", "ir"):
            return JSONResponse({"error": "kind must be 'nam' or 'ir'"}, 400)

        deleted = []
        errors = []
        roots = []
        if kind_filter in (None, "nam"):
            roots.append(("nam_models", "nam"))
        if kind_filter in (None, "ir"):
            roots.append(("nam_irs", "ir"))
        for root_name, _kind in roots:
            root = _config_dir / root_name
            if not root.exists():
                continue
            # Subdir buckets.
            for entry in list(root.iterdir()):
                if entry.is_dir():
                    if bucket_filter and entry.name != bucket_filter:
                        continue
                    for f in list(entry.iterdir()):
                        if not f.is_file() or f.name.startswith("."):
                            continue
                        rel = f"{entry.name}/{f.name}"
                        try:
                            f.unlink()
                            deleted.append(rel)
                        except OSError as e:
                            errors.append(f"{rel}: {e}")
                elif entry.is_file() and not entry.name.startswith("."):
                    # Bare files in the root only count when bucket
                    # filter is "other" or no filter.
                    if bucket_filter and bucket_filter != "other":
                        continue
                    try:
                        entry.unlink()
                        deleted.append(entry.name)
                    except OSError as e:
                        errors.append(f"{entry.name}: {e}")

        # DB cleanup — null any reference to a deleted file. Chunked so a
        # big purge can't blow SQLite's per-statement parameter limit.
        if deleted:
            conn = _get_conn()
            for chunk in _chunked(deleted):
                placeholders = ",".join("?" for _ in chunk)
                conn.execute(
                    "UPDATE preset_pieces SET file = NULL, kind = 'none', "
                    "       tone3000_id = NULL, assigned_mode = NULL "
                    f"WHERE file IN ({placeholders})",
                    tuple(chunk),
                )
                conn.execute(
                    f"UPDATE presets SET model_file = '' WHERE model_file IN ({placeholders})",
                    tuple(chunk),
                )
                conn.execute(
                    f"UPDATE presets SET ir_file = '' WHERE ir_file IN ({placeholders})",
                    tuple(chunk),
                )
            conn.commit()

        # Also drop custom gears whose backing capture(s) were just deleted —
        # otherwise an auto tone3000 gear (or a hand-made NAM/IR gear) lingers in
        # the catalog pointing at files that no longer exist. A gear is removed
        # only when ALL its backing files are gone; VST-backed gears (no file)
        # are never touched here. Its gear_map redirects are cleared too.
        removed_gears = []
        if deleted:
            deleted_set = set(deleted)
            items = _load_custom_gear()
            kept = []
            for g in items:
                files = set()
                if g.get("file"):
                    files.add(g["file"])
                for v in (g.get("gain_variants") or {}).values():
                    if isinstance(v, dict) and v.get("file"):
                        files.add(v["file"])
                if files and files <= deleted_set:
                    removed_gears.append(g.get("rs_gear"))
                else:
                    kept.append(g)
            if removed_gears:
                _save_custom_gear(kept)
                gmap = _load_gear_map()
                if any(isinstance(e, dict) and e.get("custom") in removed_gears
                       for e in gmap.values()):
                    gmap = {k: e for k, e in gmap.items()
                            if not (isinstance(e, dict) and e.get("custom") in removed_gears)}
                    _save_gear_map(gmap)

        return {
            "deleted_count": len(deleted),
            "removed_gears": removed_gears,
            "errors": errors,
            "bucket": bucket_filter,
            "kind": kind_filter,
        }

    # ── Download a candidate to audition it (no assign) ───────────────
    @app.post("/api/plugins/rig_builder/audition_candidate")
    def audition_candidate(data: dict = Body(...)):
        """Download a model for a tone3000 tone_id into nam_models/nam_irs
        WITHOUT assigning, so the UI can audition a search candidate.

        Body {rs_gear, tone3000_id, model_id?, is_ir?}. `model_id` pins a
        SPECIFIC capture inside the tone (a gain/EQ variant) instead of the
        best-match default; `is_ir` forces IR normalization for cab captures
        found under the IR category. Returns {kind, file}."""
        tone3000_id = data.get("tone3000_id")
        if tone3000_id is None:
            return JSONResponse({"error": "tone3000_id required"}, 400)
        rs_gear = data.get("rs_gear") or "audition"
        model_id = data.get("model_id")
        category = (_load_rs_to_real().get(rs_gear) or {}).get("category", "amp")
        is_ir = bool(data.get("is_ir")) or category == "cab"
        try:
            res = _download_candidate(
                tone3000_id=int(tone3000_id), is_ir=is_ir,
                rs_gear=rs_gear, settings=_load_settings(),
                model_id_override=int(model_id) if model_id is not None else None,
            )
        except Exception as e:
            log.exception("audition_candidate failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        if not res:
            return JSONResponse({"error": "no downloadable model (or disk budget reached)"}, 502)
        kind, file = res
        return {"kind": kind, "file": file}

    @app.post("/api/plugins/rig_builder/import_local_model")
    def import_local_model(data: dict = Body(...)):
        """Copy a user-picked local .nam / .wav (absolute path from the host
        file dialog) INTO the library (nam_models/ or nam_irs/) and return its
        DB-relative path, so it resolves like any downloaded capture. Used by the
        'add gear from directory' flow — the normal gear resolver requires a path
        under the library root and rejects absolute paths.

        Body: {path, kind?, category?}. `kind` ('nam'|'ir') is inferred from the
        extension when omitted; `category` (amp|pedal|rack|cab) chooses the
        subdir. Returns {kind, file}."""
        if _config_dir is None:
            return JSONResponse({"error": "config_dir not initialized"}, 500)
        raw = (data.get("path") or "").strip()
        if not raw:
            return JSONResponse({"error": "path required"}, 400)
        src = Path(raw)
        if not src.is_file():
            return JSONResponse({"error": "file not found"}, 404)
        ext = src.suffix.lower()
        kind = (data.get("kind") or "").strip().lower()
        if kind not in ("nam", "ir"):
            kind = "ir" if ext == ".wav" else "nam"
        cat = (data.get("category") or "").strip().lower()
        if kind == "nam":
            subdir = {"amp": "amps", "pedal": "pedals", "rack": "racks"}.get(cat, "other")
            root = _config_dir / "nam_models"
        else:
            subdir = "cabs"
            root = _config_dir / "nam_irs"
        dest_dir = root / subdir
        dest_dir.mkdir(parents=True, exist_ok=True)
        bare = _safe_filename(src.stem) + ext
        dest = dest_dir / bare
        try:
            # Collision guard: same name, different bytes → suffix with size.
            if dest.exists() and dest.stat().st_size != src.stat().st_size:
                bare = f"{_safe_filename(src.stem)}_{src.stat().st_size}{ext}"
                dest = dest_dir / bare
            if not dest.exists():
                shutil.copy2(src, dest)
        except Exception as e:
            log.exception("import_local_model failed")
            return JSONResponse({"error": f"{type(e).__name__}: {e}"}, 500)
        return {"kind": kind, "file": f"{subdir}/{bare}"}

    # ── Batch ─────────────────────────────────────────────────────────

    @app.post("/api/plugins/rig_builder/batch_all")
    def batch_all(data: dict = Body(default={})):
        global _batch_thread
        mode = (data or {}).get("mode", "all")
        if mode not in ("all", "new", "factory"):
            mode = "all"
        # Optional category scope for "Map gear to songs" (amp/pedal/rack/cab).
        # Ignored for factory (which always resets everything).
        _valid_cats = {"amp", "pedal", "rack", "cab"}
        categories = [c for c in (data.get("categories") or []) if c in _valid_cats] \
            if mode != "factory" else []
        with _batch_lock:
            if _batch_state["running"]:
                return JSONResponse({"error": "batch already running"}, 409)
            _batch_state.update({
                "running": True,
                "progress": 0,
                "total": 0,
                "assigned": 0,
                "skipped": 0,
                "pending": [],
                "log": [],
                "started_at": None,
                "finished_at": None,
            })
        _batch_thread = threading.Thread(
            target=_batch_worker, args=(mode, categories), name="rig_builder_batch", daemon=True
        )
        _batch_thread.start()
        return {"ok": True, "mode": mode, "categories": categories}

    @app.get("/api/plugins/rig_builder/batch_status")
    def batch_status():
        with _batch_lock:
            # Return a shallow copy so log list mutations during render
            # can't race with the worker.
            return dict(_batch_state, log=list(_batch_state["log"]))

    # ── Coverage / pendientes ─────────────────────────────────────────

    @app.get("/api/plugins/rig_builder/coverage")
    def coverage():
        """Pending = preset_pieces with no NAM/IR file AND no VST plugin.

        A VST-assigned piece (kind='vst', vst_path set) is NOT pending —
        the audio engine plays the plugin and never reads `file`. The old
        criterion `kind='none' OR file IS NULL OR file=''` flagged every
        VST piece as pending because VST rows legitimately have file=NULL.
        Fixed: also require `vst_path` to be empty for a piece to count
        as pending.

        Excludes the master-chain sentinel presets so pieces added to the
        global pre/post chain don't surface as "pending gears" — those
        rows live outside any song. The Pending tab is the per-song to-do
        list; master pieces are global and shouldn't pollute it.
        """
        conn = _get_conn()
        master_ids = [pid for pid in (_get_master_preset_id("pre"),
                                      _get_master_preset_id("post"))
                      if pid is not None]
        # A piece is pending iff it has neither a usable NAM/IR file nor
        # a VST plugin assigned. Encoded once here to keep the two query
        # branches consistent.
        pending_expr = (
            "(kind='none' OR file IS NULL OR file='') "
            "AND (vst_path IS NULL OR vst_path='')"
        )
        if master_ids:
            placeholders = ",".join("?" for _ in master_ids)
            rows = conn.execute(
                f"SELECT rs_gear_type, COUNT(*) AS n, "
                f"  SUM(CASE WHEN {pending_expr} THEN 1 ELSE 0 END) AS pending "
                f"FROM preset_pieces "
                f"WHERE preset_id NOT IN ({placeholders}) "
                f"GROUP BY rs_gear_type "
                f"ORDER BY pending DESC, n DESC",
                tuple(master_ids),
            ).fetchall()
        else:
            rows = conn.execute(
                f"SELECT rs_gear_type, COUNT(*) AS n, "
                f"  SUM(CASE WHEN {pending_expr} THEN 1 ELSE 0 END) AS pending "
                f"FROM preset_pieces "
                f"GROUP BY rs_gear_type "
                f"ORDER BY pending DESC, n DESC"
            ).fetchall()
        rs_map = _load_rs_to_real()
        out = []
        for r in rows:
            rs = r[0]
            info = rs_map.get(rs) or {}
            out.append({
                "rs_gear": rs,
                "total_chain_slots": r[1],
                "pending_chain_slots": r[2],
                "name": _gear_display_name(rs, info.get("name", rs)),
                "category": info.get("category", "other"),
                "tone3000_query": info.get("tone3000_query", rs),
            })
        return {"items": out}
