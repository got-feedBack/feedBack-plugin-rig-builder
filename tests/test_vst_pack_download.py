"""Opt-in VST pack download: the worker streams → sha256-verifies → extracts the
nested tree into the writable root, and the loader then finds the binaries.
Mirrors career's file:// end-to-end pack test (the rig download path had none).

Self-contained: builds the sliced pack inline (the same nested layout the core
tool `tools/content_packs.build_vst_pack` emits) instead of importing that
module. That module lives in the *core* feedBack repo, not here — `tools/` is
not even a package in this repo — so importing it made this whole file
un-collectable and left the VST download path with zero runnable coverage."""
from __future__ import annotations

import hashlib
import importlib.util
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# A fat .vst3 lays each platform's binary under Contents/<platform-dir>/. A
# sliced pack keeps exactly one platform's dir plus the shared bundle files.
_PLATFORM_BINARY = {
    "mac": "MacOS/Foo",
    "win": "x86_64-win/Foo.vst3",
    "linux": "x86_64-linux/Foo.so",
}


def _routes_module():
    spec = importlib.util.spec_from_file_location(
        "rig_builder_routes_for_vst_dl_test", ROOT / "routes.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _build_sliced_pack(zip_path: Path, platform: str) -> str:
    """Write the nested zip a per-platform slice produces; return its sha256.

    Arcnames are relative to the vst root (`amps/Foo.vst3/Contents/...`) exactly
    as `content_packs.build_vst_pack` emits them, and only the target platform's
    binary is present (foreign platform dirs dropped) — so this exercises the
    real extract-and-load contract without importing the core tool.
    """
    base = "amps/Foo.vst3/Contents"
    members = {
        f"{base}/Info.plist": b"<plist/>",
        f"{base}/{_PLATFORM_BINARY[platform]}": f"{platform}-binary".encode(),
    }
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_STORED) as zf:
        for name in sorted(members):
            zf.writestr(name, members[name])
    return hashlib.sha256(zip_path.read_bytes()).hexdigest()


def _isolate(routes, tmp_path):
    # Point config_dir at a temp (download target) and plugin_dir at an empty
    # temp so the loader doesn't scan the real 650 MB bundled vst/ tree.
    routes._config_dir = tmp_path / "config"
    routes._config_dir.mkdir()
    routes._plugin_dir = tmp_path / "plugin"
    routes._plugin_dir.mkdir()
    routes._vst_pack_state.update(status="idle", done=0, total=0, error=None)


def test_download_extracts_sliced_tree_and_loader_finds_it(tmp_path):
    routes = _routes_module()
    _isolate(routes, tmp_path)
    plat = routes._current_vst_platform()
    zip_path = tmp_path / f"{plat}.zip"
    sha = _build_sliced_pack(zip_path, plat)

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
    zip_path = tmp_path / f"{plat}.zip"
    _build_sliced_pack(zip_path, plat)
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
    zip_path = tmp_path / f"{plat}.zip"
    sha = _build_sliced_pack(zip_path, plat)
    state = {"status": "running", "done": 0, "total": 0, "error": None}
    routes._download_vst_pack({"url": zip_path.as_uri(), "sha256": sha}, state)
    contents = routes._downloaded_vst_root() / "amps" / "Foo.vst3" / "Contents"
    assert (contents / _PLATFORM_BINARY[plat]).is_file()
