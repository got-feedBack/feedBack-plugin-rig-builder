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
import struct
import sys
from pathlib import Path

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


def extract_all(gears_psarc: str, irs_root: Path) -> dict:
    """Pull every cab IR out of gears_psarc. Returns the extraction
    summary (counts + a sound_bank → [files] map) and writes the .wav
    files into `irs_root/rocksmith/`. Existing files are overwritten.
    """
    out_dir = irs_root / "rocksmith"
    out_dir.mkdir(parents=True, exist_ok=True)

    files = read_psarc_entries(gears_psarc, _BANK_PATTERNS)

    bank_to_irs: dict[str, list[str]] = {}
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
        for idx, (_, offset, size) in enumerate(_parse_didx(chunks["DIDX"])):
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
            _write_float32_wav(out_dir / out_name, samples, sample_rate, channels)
            ir_files.append(f"rocksmith/{out_name}")
            total_irs += 1
        if ir_files:
            bank_to_irs[bank_name] = ir_files

    return {
        "irs_root": str(out_dir),
        "total_irs": total_irs,
        "banks_with_audio": banks_with_audio,
        "banks_without_audio": banks_without_audio,
        "bank_to_irs": bank_to_irs,
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
    rs_cab_to_ir = build_rs_cab_to_ir(summary["bank_to_irs"], Path(__file__).parent / "rs_to_real.json")
    out_json = Path(__file__).parent / "rs_cab_to_ir.json"
    out_json.write_text(json.dumps(rs_cab_to_ir, indent=2, sort_keys=True))

    print(f"Extracted {summary['total_irs']} IRs into {summary['irs_root']}")
    print(f"Banks with audio: {summary['banks_with_audio']}")
    print(f"Banks without audio (skipped): {summary['banks_without_audio']}")
    print(f"Wrote rs_cab_to_ir.json with {len(rs_cab_to_ir)} RS entities mapped to extracted IRs")


if __name__ == "__main__":
    main()
