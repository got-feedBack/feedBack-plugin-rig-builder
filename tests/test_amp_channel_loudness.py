from __future__ import annotations

import importlib.util
import sys
import types
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    try:
        import fastapi  # noqa: F401
    except ModuleNotFoundError:
        fastapi = types.ModuleType("fastapi")
        fastapi.Body = lambda *args, **kwargs: None
        fastapi.File = lambda *args, **kwargs: None
        fastapi.Request = type("Request", (), {})
        fastapi.UploadFile = type("UploadFile", (), {})
        responses = types.ModuleType("fastapi.responses")
        responses.HTMLResponse = type("HTMLResponse", (), {})
        responses.JSONResponse = type("JSONResponse", (), {})
        responses.Response = type("Response", (), {})
        sys.modules["fastapi"] = fastapi
        sys.modules["fastapi.responses"] = responses
    spec = importlib.util.spec_from_file_location("rig_builder_routes_for_amp_channel_test", ROOT / "routes.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _entry():
    return {
        "gain_params": ["Channel"],
        "default": {"Channel": 1.0},
        "lufs_default": -11.0,
        "gain_curve": [[0.0, -22.0], [1.0, -11.0]],
        "default_profile": "burn",
        "selector_defaults": {"Channel": 1.0},
        "channel_profiles": [
            {
                "name": "vintage",
                "fixed": {"Channel": 0.0},
                "gain_params": ["Vint Vol"],
                "default": {"Channel": 0.0, "Vint Vol": 0.5},
                "lufs_default": -22.0,
                "gain_curve": [[0.0, -32.0], [0.5, -22.0], [1.0, -15.0]],
                "vol_curves": {},
            },
            {
                "name": "burn",
                "fixed": {"Channel": 1.0},
                "gain_params": ["Gain 1", "Gain 2"],
                "default": {"Channel": 1.0, "Gain 1": 0.5, "Gain 2": 0.5},
                "lufs_default": -11.0,
                "gain_curve": [[0.0, -28.0], [0.5, -11.0], [1.0, -10.0]],
                "vol_curves": {},
            },
        ],
    }


def test_exact_switch_value_selects_channel_profile():
    routes = _routes_module()
    profile = routes._amp_channel_profile(_entry(), {"Channel": 0.0, "Vint Vol": 0.5})
    assert profile["name"] == "vintage"


def test_intermediate_morph_uses_whole_amp_curve():
    routes = _routes_module()
    assert routes._amp_channel_profile(_entry(), {"Channel": 0.4}) is None


def test_missing_state_uses_measured_default_profile():
    routes = _routes_module()
    assert routes._amp_channel_profile(_entry(), {})["name"] == "burn"


def test_partial_state_uses_measured_selector_default():
    routes = _routes_module()
    profile = routes._amp_channel_profile(_entry(), {"Gain 1": 0.7, "Gain 2": 0.4})
    assert profile["name"] == "burn"


def test_trim_uses_active_channel_gain_curve():
    routes = _routes_module()
    trim = routes._amp_loudness_trim_db(_entry(), {"Channel": 0.0, "Vint Vol": 0.0})
    assert trim == 20.0
