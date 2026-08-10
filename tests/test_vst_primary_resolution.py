from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    spec = importlib.util.spec_from_file_location(
        "rig_builder_routes_for_vst_primary_test", ROOT / "routes.py"
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _candidate():
    return {
        "name": "Chorus",
        "format": "VST3",
        "bundled": "vst/pedals/CH-2.vst3",
    }


def test_primary_prefers_plugin_directory_bundle(tmp_path):
    routes = _routes_module()
    routes._plugin_dir = tmp_path / "read-only-plugin"
    bundle = routes._plugin_dir / "vst" / "pedals" / "CH-2.vst3"
    bundle.mkdir(parents=True)
    routes._load_vst_seed_catalog = lambda: {"Pedal_Chorus": [_candidate()]}

    downloaded = tmp_path / "writable-pack" / "pedals" / "Chorus.vst3"
    known = {
        "chorus": [
            {"path": str(downloaded), "format": "VST3", "name": "Chorus"}
        ]
    }

    assert routes._pick_installed_primary_vst("Pedal_Chorus", known) == {
        "vst_path": str(bundle),
        "vst_format": "VST3",
    }


def test_primary_falls_back_to_downloaded_pack_for_bundled_candidate(tmp_path):
    routes = _routes_module()
    routes._plugin_dir = tmp_path / "read-only-plugin"
    routes._load_vst_seed_catalog = lambda: {"Pedal_Chorus": [_candidate()]}

    downloaded = tmp_path / "writable-pack" / "pedals" / "Chorus.vst3"
    known = {
        "chorus": [
            {"path": str(downloaded), "format": "VST3", "name": "Chorus"}
        ]
    }

    assert routes._pick_installed_primary_vst("Pedal_Chorus", known) == {
        "vst_path": str(downloaded),
        "vst_format": "VST3",
    }
