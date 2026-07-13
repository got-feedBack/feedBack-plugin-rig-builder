"""Reading a song's tones must not unpack the song.

_read_tones_from_sloppak() used to call sloppak.load_song(), which resolves the
pack's source dir — i.e. extracts the WHOLE archive, every stem, into
sloppak_cache/. We only want a few KB of tone JSON out of the arrangement files,
so that was a ~45x write amplification per song.

Harmless once. Catastrophic from _batch_worker(), which runs it over the ENTIRE
library: a tester's sloppak_cache reached 60 GB — his whole 1800-song library,
unpacked, none of it ever played. Stems are already-compressed audio, so an
unpacked pack is ~1.1x its zip: the cache becomes a second copy of the library.

This pins that the tones still come out, and that nothing is written to disk.
"""

from __future__ import annotations

import importlib.util
import json
import os
import sys
import zipfile
from pathlib import Path

import pytest
import yaml


ROOT = Path(__file__).resolve().parents[1]

# Big enough that an accidental unpack is unmissable next to a few hundred bytes
# of JSON — the whole point is that the audio is what makes this expensive.
STEM = b"\x00" * (512 * 1024)

TONES = {
    "definitions": [
        {"Key": "clean_amp", "GearList": {"Amp": {"Type": "Amps"}}},
        {"Key": "dist_amp", "GearList": {"Amp": {"Type": "Amps"}}},
    ]
}


def _routes():
    spec = importlib.util.spec_from_file_location("rb_routes_tone_test", ROOT / "routes.py")
    assert spec and spec.loader
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _pack(path: Path, *, vocals_too: bool = False) -> Path:
    arrangements = [{"file": "arrangements/lead.json", "name": "Lead"}]
    if vocals_too:
        arrangements.append({"file": "arrangements/vocals.json", "name": "Vocals"})
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("manifest.yaml", yaml.safe_dump({
            "title": "T", "artist": "A",
            "arrangements": arrangements,
            "stems": [{"id": "full", "file": "stems/audio.ogg"}],
        }))
        zf.writestr("arrangements/lead.json", json.dumps({"name": "Lead", "tones": TONES}))
        if vocals_too:
            # Vocals must stay excluded — it never contributed tones before.
            zf.writestr("arrangements/vocals.json", json.dumps({
                "name": "Vocals",
                "tones": {"definitions": [{"Key": "SHOULD_NOT_APPEAR"}]},
            }))
        zf.writestr("stems/audio.ogg", STEM)
    return path


def _find_core_lib() -> Path | None:
    """Locate the host core's lib/ (it owns the `sloppak` module this reads through).

    Ordered: an explicit FEEDBACK_CORE_LIB, then a sibling checkout, then the
    conventional dev layout. NOT a single hardcoded developer path — this file is
    the regression guard for a 60 GB disk bug, and one that quietly skips
    everywhere but one laptop is no guard at all.
    """
    env = os.environ.get("FEEDBACK_CORE_LIB")
    candidates = [Path(env)] if env else []
    here = Path(__file__).resolve()
    candidates += [
        here.parents[2] / "feedback" / "lib",          # sibling checkout
        here.parents[2] / "feedBack" / "lib",
        Path.home() / "Repositories" / "feedback" / "lib",
    ]
    for c in candidates:
        if (c / "sloppak.py").is_file():
            return c
    return None


@pytest.fixture
def sloppak_on_path():
    """The plugin imports `sloppak` from the host core at call time."""
    core = _find_core_lib()
    if core is None:
        pytest.skip(
            "host core lib/ not found — set FEEDBACK_CORE_LIB to the core checkout's "
            "lib/ to run the tone-read regression tests"
        )
    sys.path.insert(0, str(core))
    yield
    sys.path.remove(str(core))


def test_reading_tones_does_not_write_to_the_unpack_cache(tmp_path, sloppak_on_path):
    routes = _routes()
    dlc = tmp_path / "dlc"; dlc.mkdir()
    cache = tmp_path / "sloppak_cache"; cache.mkdir()
    routes._get_sloppak_cache_dir = lambda: cache
    _pack(dlc / "song.feedpak")

    tones = routes._read_tones_from_sloppak("song.feedpak", dlc)

    assert [t["Key"] for t in tones] == ["clean_amp", "dist_amp"]
    written = sum(f.stat().st_size for f in cache.rglob("*") if f.is_file())
    assert written == 0, (
        f"reading tones unpacked {written/1e6:.1f} MB into sloppak_cache — this is "
        "the 60 GB bug: _batch_worker() runs this over the whole library"
    )


def test_vocals_arrangement_still_contributes_no_tones(tmp_path, sloppak_on_path):
    routes = _routes()
    dlc = tmp_path / "dlc"; dlc.mkdir()
    routes._get_sloppak_cache_dir = lambda: tmp_path / "cache"
    _pack(dlc / "song.feedpak", vocals_too=True)

    keys = [t["Key"] for t in routes._read_tones_from_sloppak("song.feedpak", dlc)]

    assert "SHOULD_NOT_APPEAR" not in keys, "Vocals must stay excluded, as load_song() had it"
    assert keys == ["clean_amp", "dist_amp"]


def test_tones_match_what_the_unpacking_path_returned(tmp_path, sloppak_on_path):
    """Equivalence with the old load_song() path — same tones, minus the disk cost."""
    routes = _routes()
    dlc = tmp_path / "dlc"; dlc.mkdir()
    cache = tmp_path / "cache"; cache.mkdir()
    routes._get_sloppak_cache_dir = lambda: cache
    _pack(dlc / "song.feedpak", vocals_too=True)

    fast = routes._read_tones_from_sloppak("song.feedpak", dlc)
    slow = routes._read_tones_via_unpack("song.feedpak", dlc)   # downlevel fallback

    assert fast == slow
    assert any(cache.rglob("*.ogg")), "sanity: the fallback really does unpack the stem"


def test_missing_arrangement_member_is_survivable(tmp_path, sloppak_on_path):
    routes = _routes()
    dlc = tmp_path / "dlc"; dlc.mkdir()
    routes._get_sloppak_cache_dir = lambda: tmp_path / "cache"
    with zipfile.ZipFile(dlc / "broken.feedpak", "w") as zf:
        zf.writestr("manifest.yaml", yaml.safe_dump({
            "arrangements": [{"file": "arrangements/gone.json", "name": "Lead"}],
        }))
    assert routes._read_tones_from_sloppak("broken.feedpak", dlc) == []
