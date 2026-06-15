from __future__ import annotations

import importlib.util
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    spec = importlib.util.spec_from_file_location("rig_builder_routes_for_watcher_test", ROOT / "routes.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_materialization_watcher_preserves_nested_sloppak_relative_paths(tmp_path):
    routes = _routes_module()
    dlc = tmp_path / "dlc"
    nested = dlc / "sloppak" / "bonjoviwanted.sloppak"
    root = dlc / "root_song.psarc"
    nested.parent.mkdir(parents=True)
    nested.write_bytes(b"sloppak")
    root.write_bytes(b"psarc")

    routes._get_dlc_dir = lambda: dlc

    current = routes._watch_scan_dlc()

    assert current is not None
    assert "sloppak/bonjoviwanted.sloppak" in current
    assert "bonjoviwanted.sloppak" not in current
    assert "root_song.psarc" in current


def test_song_key_candidates_try_relative_then_legacy_basename(tmp_path):
    routes = _routes_module()
    dlc = tmp_path / "dlc"
    nested = dlc / "artist" / "song_p.psarc"
    nested.parent.mkdir(parents=True)
    nested.write_bytes(b"psarc")

    routes._get_dlc_dir = lambda: dlc

    assert routes._dlc_relative_song_key(nested) == "artist/song_p.psarc"
    assert routes._db_song_key("song_p.psarc", nested) == "artist/song_p.psarc"
    assert routes._song_key_candidates("song_p.psarc", nested) == [
        "artist/song_p.psarc",
        "song_p.psarc",
    ]


def test_persist_preset_chain_writes_relative_song_key_for_nested_basename(tmp_path):
    routes = _routes_module()
    dlc = tmp_path / "dlc"
    nested = dlc / "artist" / "song_p.psarc"
    db = tmp_path / "nam_tone.db"
    nested.parent.mkdir(parents=True)
    nested.write_bytes(b"psarc")

    conn = sqlite3.connect(db)
    conn.execute(
        "CREATE TABLE presets ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT UNIQUE, model_file TEXT, ir_file TEXT, "
        "input_gain REAL, output_gain REAL, gate_threshold REAL, settings_json TEXT)"
    )
    conn.execute(
        "CREATE TABLE tone_mappings ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "filename TEXT, tone_key TEXT, preset_id INTEGER, "
        "UNIQUE(filename, tone_key))"
    )
    conn.commit()
    conn.close()

    routes._get_dlc_dir = lambda: dlc
    routes._db_path = str(db)
    routes._conn = None

    routes._persist_preset_chain(
        filename="song_p.psarc",
        tone_key="ToneA",
        name="song_p.psarc::ToneA",
        pieces=[],
    )

    rows = sqlite3.connect(db).execute(
        "SELECT filename, tone_key FROM tone_mappings"
    ).fetchall()
    assert rows == [("artist/song_p.psarc", "ToneA")]
