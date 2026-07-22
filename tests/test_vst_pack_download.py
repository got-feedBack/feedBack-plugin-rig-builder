"""Opt-in VST pack download: the worker streams → sha256-verifies → extracts the
nested tree into the writable root, and the loader then finds the binaries.
Mirrors career's file:// end-to-end pack test (the rig download path had none)."""
from __future__ import annotations

import importlib.util
from pathlib import Path

from tools import content_packs

ROOT = Path(__file__).resolve().parents[1]


def _routes_module():
    spec = importlib.util.spec_from_file_location("rig_builder_routes_for_vst_dl_test", ROOT / "routes.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _fake_vst_tree(root: Path):
    c = root / "amps" / "Foo.vst3" / "Contents"
    (c / "MacOS").mkdir(parents=True)
    (c / "x86_64-win").mkdir(parents=True)
    (c / "x86_64-linux").mkdir(parents=True)
    (c / "MacOS" / "Foo").write_bytes(b"mac-binary")
    (c / "x86_64-win" / "Foo.vst3").write_bytes(b"win-binary")
    (c / "x86_64-linux" / "Foo.so").write_bytes(b"linux-binary")
    (c / "Info.plist").write_bytes(b"<plist/>")


def _isolate(routes, tmp_path):
    # Point config_dir at a temp (download target) and plugin_dir at an empty
    # temp so the loader doesn't scan the real 650 MB bundled vst/ tree.
    routes._config_dir = tmp_path / "config"
    routes._config_dir.mkdir()
    routes._plugin_dir = tmp_path / "plugin"
    routes._plugin_dir.mkdir()
    routes._vst_pack_state.update(status="idle", done=0, total=0, error=None)


def _build_pack(tmp_path, platform):
    src = tmp_path / "vst"
    _fake_vst_tree(src)
    zip_path = tmp_path / f"{platform}.zip"
    info = content_packs.build_vst_pack(src, zip_path, platform)
    return zip_path, info["sha256"]


def test_download_extracts_sliced_tree_and_loader_finds_it(tmp_path):
    routes = _routes_module()
    _isolate(routes, tmp_path)
    plat = routes._current_vst_platform()
    zip_path, sha = _build_pack(tmp_path, plat)

    assert routes._vst_installed() is False           # nothing before download
    state = {"status": "running", "done": 0, "total": 0, "error": None}
    routes._download_vst_pack({"url": zip_path.as_uri(), "sha256": sha}, state)
    assert state["status"] == "done", state["error"]

    # The .vst3 landed in the writable root, nested structure intact.
    installed = routes._downloaded_vst_root() / "amps" / "Foo.vst3" / "Contents"
    assert (installed / "Info.plist").is_file()
    assert routes._vst_installed() is True
    # The loader resolves it by absolute path.
    names = [p["name"] for p in routes._bundled_vst_plugins()]
    assert "Foo" in names


def test_sha_mismatch_is_rejected(tmp_path):
    routes = _routes_module()
    _isolate(routes, tmp_path)
    plat = routes._current_vst_platform()
    zip_path, _ = _build_pack(tmp_path, plat)
    state = {"status": "running", "done": 0, "total": 0, "error": None}
    routes._download_vst_pack({"url": zip_path.as_uri(), "sha256": "0" * 64}, state)
    assert state["status"] == "error"
    assert "sha256" in state["error"]
    assert routes._vst_installed() is False           # nothing installed on failure


def test_download_selects_current_platform_slice(tmp_path):
    # The pack built for this platform must carry this platform's binary.
    routes = _routes_module()
    _isolate(routes, tmp_path)
    plat = routes._current_vst_platform()
    zip_path, sha = _build_pack(tmp_path, plat)
    state = {"status": "running", "done": 0, "total": 0, "error": None}
    routes._download_vst_pack({"url": zip_path.as_uri(), "sha256": sha}, state)
    contents = routes._downloaded_vst_root() / "amps" / "Foo.vst3" / "Contents"
    wanted = {"mac": "MacOS/Foo", "win": "x86_64-win/Foo.vst3", "linux": "x86_64-linux/Foo.so"}
    assert (contents / wanted[plat]).is_file()
