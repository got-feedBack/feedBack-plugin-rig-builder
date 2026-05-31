"""Extract cabinet impulse responses from Rocksmith 2014's gears.psarc.

Rocksmith ships its cab IRs as embedded audio inside Wwise SoundBank
(`.bnk`) files under `audio/windows/cab_*.bnk` and `bass_cab_*.bnk`.
The .bnk container holds DIDX + DATA chunks where DATA is a
concatenation of audio blobs in a Rocksmith-specific layout (verified
against all 58 cab banks in the base game's gears.psarc):

    bytes 0-15: header (u32 magic=128, u32 ?=256, u32 sample_rate, u32 channels)
    bytes 16+ : mono float32 PCM samples

Empirically every cab IR is 48 kHz mono float32, 55-280 ms long, with
peak energy in the first millisecond — textbook IR shape. Amp, pedal,
and rack .bnk files contain only HIRC (DSP graph metadata) with no
embedded audio and so cannot be converted to IRs from this archive.

The script writes one .wav per DIDX entry into
`<config_dir>/nam_irs/rocksmith/<bank_name>_<index>.wav` (e.g.
`rocksmith/cab_tw410c_00.wav`) so the nam_tone runtime can resolve
them via the standard `_safe_child(irs_dir, file)` lookup. It also
emits `rs_cab_to_ir.json` next to the script, mapping every Rocksmith
gear entity whose `sound_bank` matches an extracted .bnk to the list
of IR files we wrote — that mapping is what nam_rig_builder consults when
deciding whether to suggest a Rocksmith IR or fall back to tone3000.

Usage (run with Slopsmith's bundled Python so the psarc reader's
pycryptodome import resolves):

    /Applications/Slopsmith.app/Contents/Resources/python/runtime/bin/python3.12 \\
        extract_irs.py /path/to/gears.psarc /path/to/nam_irs
"""

import json
import os
import re
import struct
import sys
from pathlib import Path

# Make this script's own dir (tools/) importable so `common` resolves even when
# launched as a subprocess whose sys.path[0] isn't tools/ (the "No module named
# 'common'" extractor error reported on Windows).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import PLUGIN_ROOT, DATA_DIR

# psarc.py lives inside the slopsmith bundle; add it to the path so
# this script works both standalone and when invoked from routes.py.
_SLOP_LIB = "/Applications/Slopsmith.app/Contents/Resources/slopsmith/lib"
if _SLOP_LIB not in sys.path:
    sys.path.insert(0, _SLOP_LIB)

from psarc import read_psarc_entries  # noqa: E402


# Glob patterns we extract. Amp/pedal/rack .bnk files do exist in
# gears.psarc but they're DSP graphs with no embedded audio; including
# them would just slow extraction.
_BANK_PATTERNS = ["audio/windows/cab_*.bnk", "audio/windows/bass_cab_*.bnk"]


def _parse_bnk(blob: bytes) -> dict[str, bytes]:
    """Walk a SoundBank's top-level chunks. Returns {chunk_id: bytes}.

    Only depends on the chunk framing (4-byte ASCII id + u32 LE size +
    payload) so it's robust against the version-specific HIRC layouts
    we don't need to interpret.
    """
    chunks: dict[str, bytes] = {}
    pos = 0
    while pos < len(blob) - 8:
        chunk_id = blob[pos:pos + 4]
        if not chunk_id.isalpha():
            # Trailing padding or unexpected bytes — give up rather than
            # mis-parse. Every cab bank we've inspected has BKHD first.
            break
        chunk_size = struct.unpack_from("<I", blob, pos + 4)[0]
        chunks[chunk_id.decode("ascii", "replace")] = blob[pos + 8:pos + 8 + chunk_size]
        pos += 8 + chunk_size
    return chunks


def _parse_didx(didx: bytes) -> list[tuple[int, int, int]]:
    """DIDX is a packed array of 12-byte records: (id u32, offset u32, size u32)."""
    out: list[tuple[int, int, int]] = []
    for i in range(0, len(didx) - 11, 12):
        out.append(struct.unpack_from("<III", didx, i))
    return out


_IR_TARGET_L2 = 2.4    # match tone3000 cab IRs' broadband convolution gain
_IR_PEAK_CAP = 2.0     # clip safety — never let an IR's peak exceed this


def _peak_normalize_float32(samples: bytes) -> bytes:
    """Scale raw float32 cab-IR PCM so its **L2 norm** (sqrt of total energy)
    hits a tone3000-like target — capping the peak for clip safety.

    A cab IR's playback loudness tracks its L2 (the broadband convolution
    gain), NOT its peak. Rocksmith's raw IRs are unnormalized (peaks ~7-19) AND,
    even after peak-normalizing, carry only ~half the L2 of a tone3000 IR — so
    the guitar/bass comes out far quieter with a Rocksmith cab engaged. Matching
    L2 equalizes cab loudness across the set and against tone3000. Scaling is
    uniform across (interleaved) channels — level only, not frequency response.
    (Name kept for the call site; it's an L2 normalize now.)"""
    import math
    n = len(samples) // 4
    if n == 0:
        return samples
    vals = struct.unpack("<%df" % n, samples)
    peak = max((abs(v) for v in vals), default=0.0)
    l2 = math.sqrt(sum(v * v for v in vals))
    if l2 <= 0.0 or peak <= 0.0:
        return samples
    scale = _IR_TARGET_L2 / l2
    if peak * scale > _IR_PEAK_CAP:
        scale = _IR_PEAK_CAP / peak
    if abs(scale - 1.0) < 1e-3:   # already normalized — leave it (idempotent)
        return samples
    return struct.pack("<%df" % n, *(v * scale for v in vals))


def _write_float32_wav(path: Path, samples: bytes, sample_rate: int, channels: int) -> None:
    """Write a 32-bit IEEE float PCM WAV file.

    Matches the format nam_tone normalizes uploaded IRs to (48 kHz mono
    float32) so we can drop our extracted IRs directly into nam_irs/
    without going through nam_tone's ffmpeg pipeline.
    """
    bits = 32
    block_align = channels * (bits // 8)
    byte_rate = sample_rate * block_align
    fmt_size = 16
    data_size = len(samples)
    riff_size = 4 + (8 + fmt_size) + (8 + data_size)
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", riff_size))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<I", fmt_size))
        f.write(struct.pack("<HHIIHH", 3, channels, sample_rate, byte_rate, block_align, bits))  # format=3 = IEEE float
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        f.write(samples)


def _bank_name_from_path(path: str) -> str:
    """`audio/windows/cab_tw410c.bnk` → `cab_tw410c`."""
    leaf = path.rsplit("/", 1)[-1]
    return leaf[:-4] if leaf.endswith(".bnk") else leaf


def _wwise_fnv1_hash(name: str) -> int:
    """Wwise's name-to-ID hash: FNV-1 32-bit over the **lowercased** name.

    Wwise's authoring tool can ship a SoundBank in "short IDs only" mode
    (no STID table). That's what Rocksmith does — so we never get the
    effect names back from the .bnk itself. But the per-mic-position
    JSON manifests under `manifests/gears/gear_*_<suffix>.json` carry
    the original names; hashing each with this function yields the same
    u32 the HIRC chunk stores as the Effect's object ID. That's how we
    rejoin "5c" (the mic suffix) → "Bass_Cab_AT1150BC_57_Cone" (effect)
    → media_id (DIDX entry) → which extracted .wav file.
    """
    h = 2166136261
    for b in name.lower().encode("utf-8"):
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    return h


# Wwise HIRC object types we care about (most cab banks ship type-18
# Effects — convolution reverb / FX node). The Effect payload starts
# with 16 bytes of plugin metadata, 4 bytes plugin ID, then 38 bytes
# of size/properties, and the referenced media ID lives at offset 58
# from the payload start. The offset is stable across every cab bank
# inspected.
_HIRC_EFFECT_TYPE = 18
_EFFECT_MEDIA_ID_OFFSET = 58


def _parse_hirc_effect_to_media(hirc_payload: bytes,
                                  media_ids: set[int]) -> dict[int, int]:
    """Return {effect_object_id: media_id} for every type-18 Effect in
    the HIRC chunk whose payload's media-id field references one of the
    bank's known DIDX entries. Effects that reference unknown media IDs
    (e.g. external streams) are skipped — those aren't cab IRs."""
    out: dict[int, int] = {}
    if len(hirc_payload) < 4:
        return out
    n_obj = struct.unpack_from("<I", hirc_payload, 0)[0]
    p = 4
    for _ in range(n_obj):
        if p + 9 > len(hirc_payload):
            break
        otype = hirc_payload[p]
        osize = struct.unpack_from("<I", hirc_payload, p + 1)[0]
        oid = struct.unpack_from("<I", hirc_payload, p + 5)[0]
        if otype == _HIRC_EFFECT_TYPE and osize >= _EFFECT_MEDIA_ID_OFFSET + 4:
            # payload of the object starts at p+9 (after type, size, id).
            mid = struct.unpack_from(
                "<I", hirc_payload, p + 9 + _EFFECT_MEDIA_ID_OFFSET)[0]
            if mid in media_ids:
                out[oid] = mid
        p += 5 + osize
    return out


def extract_all(gears_psarc: str, irs_root: Path) -> dict:
    """Pull every cab IR out of gears_psarc. Returns the extraction
    summary (counts + a sound_bank → [files] map) and writes the .wav
    files into `irs_root/rocksmith/`. Existing files are overwritten.
    """
    out_dir = irs_root / "rocksmith"
    out_dir.mkdir(parents=True, exist_ok=True)

    files = read_psarc_entries(gears_psarc, _BANK_PATTERNS)

    bank_to_irs: dict[str, list[str]] = {}
    # Per-bank info needed to map mic-position → IR file: which media_id
    # ended up at which 00/01/02/… .wav, and the HIRC effect_id → media_id
    # table. Both populated below; consumed by extract_cab_mic_map() to
    # build rs_cab_mic_map.json without re-parsing the banks.
    bank_to_media_idx: dict[str, dict[int, int]] = {}
    bank_to_effect_media: dict[str, dict[int, int]] = {}
    total_irs = 0
    banks_with_audio = 0
    banks_without_audio = 0

    for bank_path, blob in sorted(files.items()):
        chunks = _parse_bnk(blob)
        if "DIDX" not in chunks or "DATA" not in chunks:
            banks_without_audio += 1
            continue
        banks_with_audio += 1
        bank_name = _bank_name_from_path(bank_path)
        data = chunks["DATA"]
        ir_files: list[str] = []
        media_to_idx: dict[int, int] = {}
        for idx, (media_id, offset, size) in enumerate(_parse_didx(chunks["DIDX"])):
            if size < 16:
                continue
            wem = data[offset:offset + size]
            sample_rate, channels = struct.unpack_from("<II", wem, 8)
            # Only float32 mono 48k confirmed across the full base game.
            # If something else shows up (DLC packs?), skip rather than
            # write garbage and let the user know via the return value.
            if sample_rate not in (44100, 48000) or channels not in (1, 2):
                continue
            samples = wem[16:]
            # Ensure trailing bytes are a multiple of the frame size so
            # writers don't grumble. Trim to the nearest frame.
            frame_bytes = channels * 4
            samples = samples[:len(samples) - (len(samples) % frame_bytes)]
            if not samples:
                continue
            out_name = f"{bank_name}_{idx:02d}.wav"
            samples = _peak_normalize_float32(samples)
            _write_float32_wav(out_dir / out_name, samples, sample_rate, channels)
            ir_files.append(f"rocksmith/{out_name}")
            media_to_idx[media_id] = idx
            total_irs += 1
        if ir_files:
            bank_to_irs[bank_name] = ir_files
            bank_to_media_idx[bank_name] = media_to_idx
            # HIRC: only present in banks that ship FX graphs (cabs do; amps
            # don't). For amp banks this stays an empty dict and the mic-map
            # builder just skips them.
            if "HIRC" in chunks:
                bank_to_effect_media[bank_name] = _parse_hirc_effect_to_media(
                    chunks["HIRC"], set(media_to_idx.keys()))
            else:
                bank_to_effect_media[bank_name] = {}

    return {
        "irs_root": str(out_dir),
        "total_irs": total_irs,
        "banks_with_audio": banks_with_audio,
        "banks_without_audio": banks_without_audio,
        "bank_to_irs": bank_to_irs,
        "bank_to_media_idx": bank_to_media_idx,
        "bank_to_effect_media": bank_to_effect_media,
    }


def build_rs_cab_to_ir(bank_to_irs: dict[str, list[str]], rs_to_real_path: Path) -> dict:
    """Join `bank_to_irs` (keyed by sound bank name) with rs_to_real.json
    (keyed by RS entity name, carrying sound_bank). Output is keyed by
    RS entity name — the same key nam_rig_builder looks up everywhere else."""
    if not rs_to_real_path.exists():
        return {}
    rs_to_real = json.loads(rs_to_real_path.read_text())
    rs_cab_to_ir: dict[str, dict] = {}
    for rs_name, info in rs_to_real.items():
        sb = info.get("sound_bank", "")
        irs = bank_to_irs.get(sb)
        if irs:
            rs_cab_to_ir[rs_name] = {
                "sound_bank": sb,
                "irs": irs,
                "category": info.get("category"),
            }
    return rs_cab_to_ir


def _decode_mic_suffix(suffix: str, category_field: str = "") -> tuple[str, str, str]:
    """Decode a cab-variant suffix into (mic_type, position, friendly_label).

    Rocksmith encodes cab mic variants as a 2-char suffix on the rs_gear
    name (e.g. `Bass_Cab_AT1150BC_5c`). The first char is the mic type:

        5 → SM57 (dynamic)
        c → Condenser
        r → Ribbon
        t → Tube

    Second char is the position:

        c → close (on-axis cone)
        e → edge (off-cone)
        o → off-axis

    When the JSON manifest already shipped a `Category` field
    (`Dynamic_Cone`, `Condenser_Edge`, …), we trust it over the suffix
    decode — pure belt-and-braces.
    """
    mic_map = {"5": "Dynamic (SM57)", "c": "Condenser",
               "r": "Ribbon", "t": "Tube"}
    pos_map = {"c": "Cone (close)", "e": "Edge", "o": "Off-axis"}
    mic = mic_map.get(suffix[:1].lower(), suffix[:1])
    pos = pos_map.get(suffix[1:2].lower(), suffix[1:])
    # JSON Category like "Dynamic_Cone" — promote it for the friendly label.
    if category_field:
        friendly = category_field.replace("_", " ")
    else:
        friendly = f"{mic} {pos}"
    return mic, pos, friendly


def build_rs_cab_mic_map(gears_psarc: str,
                          bank_to_media_idx: dict[str, dict[int, int]],
                          bank_to_effect_media: dict[str, dict[int, int]],
                          bank_to_irs: dict[str, list[str]]) -> dict:
    """Resolve which IR .wav corresponds to which mic position per cab.

    For every cab gear manifest (`manifests/gears/gear_*_<suffix>.json`)
    we read the Effect name (e.g. `Bass_Cab_AT1150BC_57_Cone`), hash it
    with Wwise's FNV-1 to get the effect's object ID, look up the
    media_id that Effect references in the bank's HIRC, then map that
    media_id to the IR-file index via DIDX order. Output:

        {
          "Bass_Cab_AT1150BC": {
            "5c": {
              "ir_index": 3,
              "ir_file": "rocksmith/bass_cab_at1150bc_03.wav",
              "mic_type": "Dynamic (SM57)",
              "position": "Cone (close)",
              "label": "Dynamic Cone",
              "effect_name": "Bass_Cab_AT1150BC_57_Cone"
            },
            "5e": { … },
            ...
          },
          ...
        }

    Cabs whose .bnk doesn't ship HIRC (none, in the base game) or whose
    manifest references unknown effect names simply get an empty dict —
    the UI keeps the legacy numbered dropdown for those.
    """
    out: dict[str, dict] = {}
    manifests = read_psarc_entries(gears_psarc, ["manifests/gears/gear_*cab*.json"])
    # Group manifests by base manifest name (strip the _<suffix>).
    suffix_re = re.compile(r"_(?:[0-9]?[a-z]{1,2})$")
    for path, blob in manifests.items():
        # path: manifests/gears/gear_bass_cab_at1150bc_5c.json
        leaf = path.rsplit("/", 1)[-1]
        if not leaf.endswith(".json"):
            continue
        stem = leaf[:-5]           # gear_bass_cab_at1150bc_5c
        m = suffix_re.search(stem)
        if not m:
            continue
        suffix = stem[m.start() + 1:]
        base_manifest = stem[:m.start()]   # gear_bass_cab_at1150bc
        # Parse manifest
        try:
            mani = json.loads(blob)
        except (ValueError, UnicodeDecodeError):
            continue
        entries = (mani.get("Entries") or {})
        if not entries:
            continue
        attrs = next(iter(entries.values())).get("Attributes", {})
        sb = (attrs.get("SoundBank") or "").lower()
        if sb.endswith(".bnk"):
            sb = sb[:-4]
        effects = attrs.get("Effects") or []
        if not effects:
            continue
        effect_name = effects[0].get("Name", "")
        if not effect_name:
            continue
        # Look up the IR file.
        media_to_idx = bank_to_media_idx.get(sb, {})
        eff_to_media = bank_to_effect_media.get(sb, {})
        ir_files = bank_to_irs.get(sb, [])
        eid = _wwise_fnv1_hash(effect_name)
        media_id = eff_to_media.get(eid)
        if media_id is None:
            continue
        idx = media_to_idx.get(media_id)
        if idx is None or idx >= len(ir_files):
            continue
        # rs_gear base form: convert `gear_bass_cab_at1150bc` →
        # `Bass_Cab_AT1150BC` to match what the catalog renders.
        # The manifest's Attributes.ManifestUrn is `urn:database:json-db:
        # gear_bass_cab_at1150bc_5c` — but for the parent rs_gear we use
        # the in-app `Name` field's lowercase-with-underscores form. Fall
        # back to title-case of the underscored stem otherwise.
        base_rs_gear = _manifest_to_rs_gear(base_manifest)
        mic_type, position, label = _decode_mic_suffix(
            suffix, attrs.get("Category", ""))
        out.setdefault(base_rs_gear, {})[suffix] = {
            "ir_index": idx,
            "ir_file": ir_files[idx],
            "mic_type": mic_type,
            "position": position,
            "label": label,
            "effect_name": effect_name,
        }
    return out


def _manifest_to_rs_gear(manifest: str) -> str:
    """Convert a manifest stem like `gear_bass_cab_at1150bc` to the
    `Bass_Cab_AT1150BC` rs_gear-base form. We strip the `gear_` prefix,
    title-case each underscore-separated token EXCEPT the last segment,
    which is uppercased en bloc (Rocksmith codenames like `AT1150BC` are
    all-caps in rs_to_real.json — see Bass_Cab_AT1150BC vs the manifest
    `gear_bass_cab_at1150bc`)."""
    if manifest.startswith("gear_"):
        manifest = manifest[5:]
    tokens = manifest.split("_")
    if not tokens:
        return ""
    parts = [t.capitalize() for t in tokens[:-1]] + [tokens[-1].upper()]
    return "_".join(parts)


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)

    gears_psarc = sys.argv[1]
    irs_root = Path(sys.argv[2])
    if not os.path.exists(gears_psarc):
        print(f"error: {gears_psarc} not found", file=sys.stderr)
        sys.exit(1)
    irs_root.mkdir(parents=True, exist_ok=True)

    summary = extract_all(gears_psarc, irs_root)
    rs_cab_to_ir = build_rs_cab_to_ir(summary["bank_to_irs"], DATA_DIR / "rs_to_real.json")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    out_json = DATA_DIR / "rs_cab_to_ir.json"
    out_json.write_text(json.dumps(rs_cab_to_ir, indent=2, sort_keys=True))

    # Cab mic-position map — maps each cab's mic-position suffix (`5c`,
    # `cc`, `te`, etc.) to the actual extracted IR file via HIRC ↔ DIDX
    # cross-reference. Optional but high-value: the catalog uses it to
    # show "Dynamic close / Condenser edge / Tube off-axis" instead of
    # generic "IR 00 / IR 01 / IR 02".
    mic_map = build_rs_cab_mic_map(
        gears_psarc,
        summary["bank_to_media_idx"],
        summary["bank_to_effect_media"],
        summary["bank_to_irs"],
    )
    mic_json = DATA_DIR / "rs_cab_mic_map.json"
    mic_json.write_text(json.dumps(mic_map, indent=2, sort_keys=True))

    print(f"Extracted {summary['total_irs']} IRs into {summary['irs_root']}")
    print(f"Banks with audio: {summary['banks_with_audio']}")
    print(f"Banks without audio (skipped): {summary['banks_without_audio']}")
    print(f"Wrote rs_cab_to_ir.json with {len(rs_cab_to_ir)} RS entities mapped to extracted IRs")
    total_mics = sum(len(v) for v in mic_map.values())
    print(f"Wrote rs_cab_mic_map.json: {len(mic_map)} cabs × {total_mics} mic positions resolved")


if __name__ == "__main__":
    main()
