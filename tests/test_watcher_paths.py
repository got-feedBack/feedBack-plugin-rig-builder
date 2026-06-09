from __future__ import annotations

import importlib.util
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
