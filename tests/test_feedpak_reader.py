from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    spec = importlib.util.spec_from_file_location(
        "rig_builder_routes_for_feedpak_test", ROOT / "routes.py"
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_is_song_pack_accepts_sloppak_and_feedpak_not_psarc():
    routes = _routes_module()
    assert routes._is_song_pack(Path("a/Song_v1.sloppak")) is True
    assert routes._is_song_pack(Path("a/Song_v1.feedpak")) is True
    # Case-insensitive on the extension.
    assert routes._is_song_pack(Path("a/Song_v1.FEEDPAK")) is True
    # Raw game archives are still not readable song packs.
    assert routes._is_song_pack(Path("a/Song_v1_p.psarc")) is False
    assert routes._is_song_pack(Path("a/notes.txt")) is False


def test_watch_scan_dlc_includes_feedpak_alongside_sloppak(tmp_path):
    routes = _routes_module()
    dlc = tmp_path / "dlc"
    (dlc / "artist").mkdir(parents=True)
    (dlc / "artist" / "old_song.sloppak").write_bytes(b"sloppak")
    (dlc / "new_song.feedpak").write_bytes(b"feedpak")
    (dlc / "raw_song.psarc").write_bytes(b"psarc")

    routes._get_dlc_dir = lambda: dlc

    current = routes._watch_scan_dlc()

    assert current is not None
    # Both open-pack formats are picked up so the materialization watcher can
    # auto-seed feedpak songs, not just sloppak (issue #48).
    assert "artist/old_song.sloppak" in current
    assert "new_song.feedpak" in current
    # Raw .psarc still isn't a mappable song pack.
    assert "raw_song.psarc" not in current


def test_collect_dlc_songs_includes_feedpak(tmp_path):
    routes = _routes_module()
    dlc = tmp_path / "dlc"
    dlc.mkdir(parents=True)
    (dlc / "one.sloppak").write_bytes(b"x")
    (dlc / "two.feedpak").write_bytes(b"y")
    (dlc / "three.psarc").write_bytes(b"z")

    routes._get_dlc_dir = lambda: dlc

    songs, _cloud_only = routes._list_library_songs()
    names = sorted(p.name for p in songs)
    assert names == ["one.sloppak", "two.feedpak"]
