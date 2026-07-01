from __future__ import annotations

import importlib.util
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    spec = importlib.util.spec_from_file_location("rig_builder_routes_for_fallback_test", ROOT / "routes.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _make_conn() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.execute(
        "CREATE TABLE presets (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE, "
        "model_file TEXT, ir_file TEXT, input_gain REAL, output_gain REAL, "
        "gate_threshold REAL, settings_json TEXT)"
    )
    conn.execute(
        "CREATE TABLE preset_pieces (id INTEGER PRIMARY KEY AUTOINCREMENT, preset_id INTEGER, "
        "slot_order INTEGER, slot TEXT, rs_gear_type TEXT, kind TEXT, file TEXT, "
        "params_json TEXT DEFAULT '{}', tone3000_id INTEGER, assigned_mode TEXT, "
        "bypassed INTEGER DEFAULT 0, vst_path TEXT, vst_format TEXT, vst_state TEXT)"
    )
    return conn


def _make_default_tone(conn, routes, *, with_piece: bool):
    conn.execute(
        "INSERT INTO presets (name, input_gain, output_gain, gate_threshold) VALUES (?, 0.9, 1.1, -55.0)",
        (routes._DEFAULT_TONE_PRESET_NAME,),
    )
    pid = conn.execute(
        "SELECT id FROM presets WHERE name = ?", (routes._DEFAULT_TONE_PRESET_NAME,)
    ).fetchone()[0]
    if with_piece:
        conn.execute(
            "INSERT INTO preset_pieces (preset_id, slot_order, slot, rs_gear_type, kind, vst_path, vst_format) "
            "VALUES (?, 0, 'amp', 'Bass_Amp_BT975B', 'vst', '/x/SamplegSBTCL.vst3', 'VST3')",
            (pid,),
        )
    return pid


def test_fallback_returns_default_tone_mapping_when_enabled_and_configured():
    routes = _routes_module()
    conn = _make_conn()
    routes._conn = conn
    routes._load_settings = lambda: {"default_tone_enabled": True}
    pid = _make_default_tone(conn, routes, with_piece=True)

    fb = routes._default_tone_fallback_mapping(conn)

    # Tuple shape mirrors _lookup() rows so the mega-chain builder consumes it unchanged.
    assert fb == [("__default__", pid, routes._DEFAULT_TONE_PRESET_NAME, 0.9, 1.1, -55.0)]


def test_fallback_none_when_default_tone_disabled():
    routes = _routes_module()
    conn = _make_conn()
    routes._conn = conn
    routes._load_settings = lambda: {"default_tone_enabled": False}
    _make_default_tone(conn, routes, with_piece=True)

    assert routes._default_tone_fallback_mapping(conn) is None


def test_fallback_none_when_default_tone_has_no_pieces():
    routes = _routes_module()
    conn = _make_conn()
    routes._conn = conn
    routes._load_settings = lambda: {"default_tone_enabled": True}
    _make_default_tone(conn, routes, with_piece=False)

    assert routes._default_tone_fallback_mapping(conn) is None
