"""Extract gear mappings from Rocksmith 2014's gears.psarc.

Parses the xblock files inside the game's gears archive and produces
rs_to_real.json — a mapping from Rocksmith's internal gear identifier
(e.g. "Amp_Marshall1962Bluesbreaker", "Amp_AT120") to the real-world
make/model exposed via the ThreeDArtAsset URN.

The resulting JSON is consumed by nam_rig_builder to build search queries
against tone3000.com.

Usage:
    python3.12 extract_gear_map.py /path/to/gears.psarc

Run with Slopsmith's bundled Python (which has pycryptodome):
    /Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12 \\
        extract_gear_map.py /path/to/gears.psarc
"""

import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Make this script's own dir (tools/) importable so `common` resolves even when
# launched as a subprocess whose sys.path[0] isn't tools/ — the packaged app's
# embedded Python does that on some platforms (the "No module named 'common'"
# extractor error reported on Windows).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import PLUGIN_ROOT, DATA_DIR

# When run standalone, ensure slopsmith's lib is importable so we can
# reach the psarc reader. The bundled Python already has pycryptodome.
_SLOP_LIB = "/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib"
if _SLOP_LIB not in sys.path:
    sys.path.insert(0, _SLOP_LIB)

from psarc import read_psarc_entries  # noqa: E402


# URN scheme prefixes used by Rocksmith xblock properties. Stripping
# these leaves the asset identifier we care about.
_URN_3D = "urn:application:gamebryo-scenegraph:"
_URN_BANK = "urn:audio:wwise-sound-bank:"
_URN_MANIFEST = "urn:database:json-db:"

# Tone3000's gears enum (amp|full-rig|pedal|outboard|ir). We map each
# Rocksmith category to the tone3000 type we'd search for to find a
# capture/IR substitute.
_TONE3000_GEARS = {
    "amp": "amp",
    "cab": "ir",
    "pedal": "pedal",
    "rack": "outboard",
}

# Known real-brand prefixes used directly inside Rocksmith entity names.
# Order matters: longer/more specific brands first so e.g. "MesaBoogie"
# is matched before any shorter "Mesa" entry would be added.
_BRAND_PREFIXES = [
    "MesaBoogie", "MarkBass", "BlackstarHT",
    "Marshall", "Fender", "Vox", "Mesa", "Orange", "Peavey", "Engl",
    "Blackstar", "Hiwatt", "Soldano", "Ampeg", "Eden", "Aguilar",
    "Gallien", "Hartke", "Trace", "Roland", "Crate", "Hughes",
    "Friedman", "Bogner", "Diezel", "Dr", "Krank", "Splawn",
    "Suhr", "Two", "Vht", "Wizard", "Bad", "Boss", "MXR", "Ibanez",
    "ProCo", "ElectroHarmonix", "EHX", "TC", "Strymon", "Dunlop",
    "DOD", "Digitech", "Way", "Tube",
]

# Community-documented best-guesses for Rocksmith's licensed-pseudonym
# amps and cabs. Rocksmith uses generic codenames when it couldn't (or
# chose not to) license a brand — but the in-game gear is visually and
# sonically modelled on a real amp the community has identified. Used as
# the search query against tone3000 when the asset path itself doesn't
# carry the make/model. Keys are the Rocksmith entity name (without the
# Bass_ prefix); values are the (make, model) we'd present to the user.
#
# Edit rs_to_real.json directly to override any of these per-install.
_PSEUDONYM_OVERRIDES: dict[str, tuple[str, str]] = {
    # AT-series: British 4xEL34 voiced heads — Marshall JCM family.
    "Amp_AT20":  ("Marshall", "JCM 1"),
    "Amp_AT45":  ("Marshall", "JTM 45"),
    "Amp_AT120": ("Marshall", "JCM 800"),
    # BT-series: British class-A combos — Vox AC line.
    "Amp_BT15":  ("Vox", "AC15"),
    "Amp_BT30":  ("Vox", "AC30"),
    "Amp_BT45":  ("Vox", "AC50"),
    # CA-series: California / boutique modern high-gain — Mesa Boogie.
    "Amp_CA38":  ("Mesa Boogie", "Mark III"),
    "Amp_CA85":  ("Mesa Boogie", "Mark IV"),
    "Amp_CA100": ("Mesa Boogie", "Dual Rectifier"),
    # EN-series: ENGL.
    "Amp_EN50":  ("ENGL", "Powerball"),
    "Amp_EN100": ("ENGL", "Invader"),
    # HG-series: modern high-gain — Peavey 5150 / Mesa Triple Rec.
    "Amp_HG500": ("Peavey", "5150"),
    # TW-series: Tweed Fender.
    "Amp_TW20":  ("Fender", "Champ Tweed"),
    "Amp_TW40":  ("Fender", "Bassman Tweed"),
}


# Series-prefix fallback. Rocksmith's anonymized amps come in 2-letter
# codename families (AT##, BT##, CS##, …) where every member is modeled
# on the same real brand. The art-asset slug is no help — for a
# pseudonym it's just the codename again (Amp_CS100 → "cs100") — so
# there's no algorithmic way to recover the brand; it has to be
# community-documented. Mapping the *series prefix* once resolves the
# whole family, instead of needing one _PSEUDONYM_OVERRIDES line per
# model. An exact _PSEUDONYM_OVERRIDES entry still wins when present.
#
# Keyed by (instrument, 2-letter prefix) → modeled make, where instrument
# is "guitar" or "bass". They MUST be separate: Rocksmith reuses the same
# prefix letters across both families (BT is a Vox AC on guitar but a
# wholly different head on bass; CS appears in both), so a single
# (category) key would mis-map the bass variants.
#
# IMPORTANT: the trailing codename number (Amp_CS100 → "100") is a
# Rocksmith-internal id, NOT a real product number. Searching tone3000 for
# "Vox 600B" or "Marshall 38" returns ~zero hits and the gear stays
# pending forever. So a series match builds a **brand-only** tone3000
# query ("Vox", "Mesa Boogie") — the number is kept in `model`/`display`
# for the user's reference but deliberately excluded from the query. See
# the query-tier logic in build_mapping().
#
# Add a line here (then regenerate the map) to light up a new family.
_SERIES_PREFIX_OVERRIDES: dict[tuple[str, str], str] = {
    ("guitar", "AT"): "Marshall",
    ("guitar", "BT"): "Vox",
    ("guitar", "CA"): "Mesa Boogie",
    ("guitar", "EN"): "ENGL",
    ("guitar", "HG"): "Peavey",
    ("guitar", "TW"): "Fender Tweed",
    # Unconfirmed families — fill in the modeled brand and regenerate.
    # Until then they fall to a generic brand-less query (see below) so
    # they still surface pickable candidates instead of dead-ending.
    # ("guitar", "CS"): "...",   # CS90/CS100/CS120
    # ("guitar", "GB"): "...",   # GB38/GB50/GB100
    # ("bass",   "BT"): "...",   # BT600B/880B/975B
    # ("bass",   "CH"): "...",   # CH300B/350B/600B
    # ("bass",   "CS"): "...",   # CS75B/240B/300B
    # ("bass",   "HT"): "...",   # HT100B/300B/400B
    # ("bass",   "LT"): "...",   # LT25B/85B
}


def _split_camel(token: str) -> str:
    """Split a CamelCase token into space-separated words.

    Examples: "DSL100H" stays "DSL100H"; "BluesBreaker" → "Blues Breaker".
    Numbers stay attached to the preceding letters so model numbers like
    "JCM800" or "5150" survive intact.
    """
    return re.sub(r"(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", " ", token)


def _split_entity_name(name: str) -> tuple[str, str, str]:
    """Derive (category, make, model) from a Rocksmith entity name.

    Returns category as one of amp/cab/pedal/rack/other. Bass-prefixed
    gear (e.g. "Bass_Amp_AmpegSVT") is treated as its underlying type
    with the make/model identifying the bass-specific model.
    """
    if not name:
        return "other", "", ""

    parts = name.split("_")
    # Strip leading "Bass" — bass gear in Rocksmith is just gear that
    # the bass arrangements use. For tone3000 matching the brand/model
    # is what matters; the instrument family is a filter we apply later
    # if useful.
    if parts and parts[0] == "Bass":
        parts = parts[1:]

    if not parts:
        return "other", "", ""

    category_token = parts[0].lower()
    if category_token in ("amp", "cab", "pedal", "rack"):
        category = category_token
    else:
        category = "other"
    tail = "_".join(parts[1:]) if len(parts) > 1 else ""

    # Try to peel a known brand off the front of the tail.
    for brand in _BRAND_PREFIXES:
        if tail.startswith(brand):
            model = tail[len(brand):]
            return category, brand, _split_camel(model)

    return category, "", _split_camel(tail)


def _series_parts(name: str) -> tuple[str, str] | None:
    """For a codename entity like 'Amp_CS100' or 'Bass_Amp_HT100B',
    return ('CS', '100') / ('HT', '100B') — the 2-letter series prefix
    and the trailing model designation. None if the tail isn't a
    2-letter-prefix + number codename (i.e. it's a real brand name).
    """
    parts = name.split("_")
    if parts and parts[0] == "Bass":
        parts = parts[1:]
    tail = "_".join(parts[1:]) if len(parts) > 1 else ""
    m = re.match(r"^([A-Z]{2})(\d.*)$", tail)
    if not m:
        return None
    return m.group(1), m.group(2)


def _make_from_art_asset(asset: str) -> tuple[str, str]:
    """Derive (make, model) from an art-asset slug like 'marshall-bluesbreaker'.

    Used as a fallback when the entity name doesn't start with a known
    brand. The asset slug uses kebab-case with the brand as the first
    segment.
    """
    if not asset:
        return "", ""
    # Slugs use kebab-case (marshall-bluesbreaker) but a few use
    # underscores (epiphone_electar_modelm); split on both.
    segments = [s for s in re.split(r"[-_]", asset) if s]
    make = segments[0].title() if segments else ""
    model = " ".join(s.title() for s in segments[1:])
    return make, model


def _instrument(name: str) -> str:
    """'bass' for Bass_-prefixed gear, else 'guitar'. Used to key the
    series-prefix table, since the same codename prefix maps to different
    real brands on guitar vs bass."""
    return "bass" if name.startswith("Bass_") else "guitar"


def _parse_xblock(xml_bytes: bytes) -> dict | None:
    """Parse one xblock and pull the fields we need.

    Returns None if the xblock isn't a gear entity (xblocks ship for
    several entity kinds; we only care about RSEnumerable_Gear).
    """
    # Strip a UTF-8 BOM if present — ElementTree complains otherwise.
    if xml_bytes.startswith(b"\xef\xbb\xbf"):
        xml_bytes = xml_bytes[3:]
    try:
        root = ET.fromstring(xml_bytes)
    except ET.ParseError:
        return None

    entity = root.find(".//entity")
    if entity is None:
        return None
    if entity.get("modelName") != "RSEnumerable_Gear":
        return None

    name = entity.get("name") or ""

    props: dict[str, str] = {}
    for prop in entity.findall("./properties/property"):
        prop_name = prop.get("name") or ""
        value_el = prop.find("./set")
        if value_el is None:
            continue
        props[prop_name] = value_el.get("value") or ""

    def _strip(prefix: str, key: str) -> str:
        raw = props.get(key, "")
        return raw[len(prefix):] if raw.startswith(prefix) else raw

    return {
        "name": name,
        "art_asset": _strip(_URN_3D, "ThreeDArtAsset"),
        "sound_bank": _strip(_URN_BANK, "SoundBank"),
        "manifest": _strip(_URN_MANIFEST, "Manifest"),
    }


def build_mapping(psarc_path: str) -> dict:
    """Return the rs_to_real mapping by scanning every gear xblock."""
    files = read_psarc_entries(psarc_path, ["gamexblocks/ngears/*.xblock"])
    mapping: dict[str, dict] = {}

    for path, data in files.items():
        parsed = _parse_xblock(data)
        if not parsed or not parsed["name"]:
            continue

        name_str = parsed["name"]
        art = parsed["art_asset"]
        instrument = _instrument(name_str)
        category, make, model = _split_entity_name(name_str)
        # Lookup uses the entity name with any leading "Bass_" stripped —
        # same normalization as _split_entity_name — so bass-prefixed
        # variants of pseudonym gear share the guitar override keys.
        lookup_name = name_str[len("Bass_"):] if name_str.startswith("Bass_") else name_str

        # Resolve make/model and the tone3000 query in priority tiers.
        # `model` is for display only; `query` is what we search tone3000
        # with — they diverge for series matches (see below). query_source
        # records which tier won so the UI can flag approximate matches.
        query = ""
        query_source = ""

        override = _PSEUDONYM_OVERRIDES.get(lookup_name)
        sp = _series_parts(lookup_name)
        series_brand = _SERIES_PREFIX_OVERRIDES.get((instrument, sp[0])) if sp else None

        if override:
            # Tier 1 — community-documented exact model. Specific query;
            # these are the popular amps with many tone3000 captures.
            make, model = override
            query = f"{make} {model}".strip()
            query_source = "override"
        elif make:
            # Tier 2 — a real brand is baked into the entity name.
            query = f"{make} {model}".strip()
            query_source = "brand_name"
        elif series_brand:
            # Tier 3 — known codename family. Use the brand ALONE; the
            # codename number is not a real product number and tanks the
            # search. Keep the number in `model`/display only. Anchor bass
            # with "bass" so we don't get guitar captures.
            make, model = series_brand, sp[1]
            query = series_brand + (" bass" if instrument == "bass" else "")
            query_source = "series"
        elif sp is None and art and art != "placeholder":
            # Tier 4 — art-asset slug names a real brand. Gated on `sp is
            # None`: for a codename family (BT600B, CH300B…) the slug is
            # just the codename again ("bt-600b"), so trusting it would
            # reintroduce the junk "Bt 600B" query. Non-codename names
            # (GibsonGA8, EpiphoneZephyr) missing from _BRAND_PREFIXES land
            # here legitimately.
            make, model = _make_from_art_asset(art)
            query = f"{make} {model}".strip()
            query_source = "art_asset"
        elif category == "amp":
            # Tier 5 — unknown amp family. A codename query ("Cs100")
            # returns zero tone3000 hits and dead-ends; fall back to a
            # generic instrument query so the user still gets pickable
            # candidates. Flagged "generic" so the UI can mark it
            # approximate and prompt for the real brand.
            query = "bass amp" if instrument == "bass" else "guitar amp"
            query_source = "generic"
        else:
            # Non-amp (cab/pedal/rack) with nothing resolved: keep the
            # camel-split codename. Cabs prefer the extracted RS IRs so
            # their query rarely matters.
            query = f"{make} {model}".strip() or _split_camel(name_str)
            query_source = "fallback"

        display = f"{make} {model}".strip() or name_str

        mapping[name_str] = {
            "name": display,
            "category": category,
            "make": make,
            "model": model,
            "art_asset": art,
            "sound_bank": parsed["sound_bank"],
            "manifest": parsed["manifest"],
            "tone3000_query": query,
            "tone3000_gears": _TONE3000_GEARS.get(category, ""),
            "query_source": query_source,
        }

    return mapping


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)

    psarc_path = sys.argv[1]
    if not os.path.exists(psarc_path):
        print(f"error: {psarc_path} not found", file=sys.stderr)
        sys.exit(1)

    mapping = build_mapping(psarc_path)
    # Guard: never overwrite the shipped gear map with an empty one. This
    # happens when the file is the wrong archive (e.g. guitars.psarc instead
    # of gears.psarc) — parsing yields 0 entries and would otherwise wipe
    # rs_to_real.json, cascading every downstream lookup (and IR mapping) to 0.
    if not mapping:
        print(
            f"error: parsed 0 gear entries from {psarc_path} — refusing to "
            f"overwrite rs_to_real.json. Point this at your game's "
            f"gears.psarc (not guitars.psarc or another archive).",
            file=sys.stderr,
        )
        sys.exit(3)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    out_path = DATA_DIR / "rs_to_real.json"
    # Preserve curator-only fields when overwriting an existing file.
    # The PSARC walk reproduces everything we derive from the archive
    # (manifest, make/model, name, category, sound_bank, tone3000_query,
    # rs_order, etc.) — but `gain_variants` and any per-gear curator
    # overrides ARE NOT in the PSARC, only in the JSON the curator
    # built by hand. A naive overwrite wipes them. Merge instead:
    # keep new PSARC-derived data, but graft the curator's fields back
    # onto each entry.
    CURATOR_FIELDS = (
        "gain_variants",
        "gain_proxy_knobs",        # Plexi-style Loudness1/Loudness2
        "tone3000_id",             # explicit pinned default capture
        "model_id",                # explicit model_id pin
        "curator",                 # who picked these
        "name_correction_note",    # user-noted disagreement w/ auto-name
        "query_source",            # whether the query was overridden
    )
    if out_path.exists():
        try:
            existing = json.loads(out_path.read_text())
        except (ValueError, OSError):
            existing = {}
        merged_count = 0
        for rs_gear, new_entry in mapping.items():
            old_entry = existing.get(rs_gear) or {}
            if not isinstance(old_entry, dict):
                continue
            for field in CURATOR_FIELDS:
                if field in old_entry and field not in new_entry:
                    new_entry[field] = old_entry[field]
                    merged_count += 1
        if merged_count:
            print(f"Preserved {merged_count} curator-only field(s) "
                  f"from existing rs_to_real.json")
    out_path.write_text(json.dumps(mapping, indent=2, sort_keys=True))

    by_cat: dict[str, int] = {}
    for entry in mapping.values():
        by_cat[entry["category"]] = by_cat.get(entry["category"], 0) + 1
    print(f"Wrote {out_path} with {len(mapping)} entries.")
    for cat, n in sorted(by_cat.items()):
        print(f"  {cat}: {n}")


if __name__ == "__main__":
    main()
