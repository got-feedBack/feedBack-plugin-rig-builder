"""Shared helpers for the rig_builder CLI tools under ``tools/``.

The CLIs were originally flat siblings of ``routes.py``; when they moved
under ``tools/`` their ``Path(__file__).parent`` stopped pointing at the
plugin root (which holds ``tone3000_client.py`` and the generated
``*.json`` maps). Import this module to get a stable ``PLUGIN_ROOT`` and
the de-duplicated helpers that several tools used to each define.

Importing this module also inserts ``PLUGIN_ROOT`` onto ``sys.path`` so a
moved tool can still ``import rb_core.tone3000_client`` regardless of the
working directory it was launched from.
"""
from __future__ import annotations

import json
import os
import platform
import re
import sys
import tempfile
from pathlib import Path

# Windows consoles AND subprocess pipes default to the cp1252 ("charmap")
# locale encoding, which can't encode the box-drawing (──), em-dash (—),
# ellipsis (…) and × characters the extractor CLIs print — that raised
# "UnicodeEncodeError: 'charmap' codec can't encode characters" and aborted
# extraction on Windows. Every tool imports `common`, so forcing UTF-8 here
# (errors='replace' as a belt-and-braces) fixes them all at once.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# tools/common/__init__.py → parents[2] is the plugin root (…/nam_rig_builder).
PLUGIN_ROOT = Path(__file__).resolve().parents[2]

# Generated data maps (rs_to_real.json, rs_cab_to_ir.json, …) live here.
# In a packaged build PLUGIN_ROOT is read-only (e.g. an AppImage squashfs
# mount), so writing the bundled data/ raises OSError(Errno 30). The host
# plugin (routes.py) seeds the bundled defaults into a writable per-user dir
# and exports RIG_BUILDER_DATA_DIR; honour it so extractor writes land there.
#
# DATA_DIR is resolved once at import. That is correct for this module's only
# consumers — the tools/*.py extractor scripts, which routes.py runs as
# SUBPROCESSES *after* exporting RIG_BUILDER_DATA_DIR, so each fresh interpreter
# imports `common` with the override already in the environment. In-process code
# (routes.py / rb_core) does NOT read common.DATA_DIR; it uses routes._data_path,
# which re-reads the env per call. So both halves resolve to the same writable
# dir. If you ever import common.DATA_DIR in-process, set the env var first.
_DATA_DIR_OVERRIDE = os.environ.get("RIG_BUILDER_DATA_DIR")
DATA_DIR = Path(_DATA_DIR_OVERRIDE) if _DATA_DIR_OVERRIDE else (PLUGIN_ROOT / "data")

# Make the plugin root importable so tools can `import rb_core.tone3000_client`
# and read the sibling JSON maps no matter where they're launched from.
if str(PLUGIN_ROOT) not in sys.path:
    sys.path.insert(0, str(PLUGIN_ROOT))


def config_dir() -> Path | None:
    """Locate Slopsmith's per-user config dir across the supported OSes.

    Mirrors the resolution `default_db_path()` uses; the settings file
    (`rig_builder_settings.json`) and `nam_tone.db` live side by side here.
    """
    system = platform.system()
    if system == "Darwin":
        return (Path.home() / "Library" / "Application Support"
                / "slopsmith-desktop" / "slopsmith-config")
    if system == "Windows":
        appdata = os.environ.get("APPDATA")
        if appdata:
            return Path(appdata) / "slopsmith-desktop" / "slopsmith-config"
        return None
    xdg = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(xdg) / "slopsmith-desktop" / "slopsmith-config"


def default_db_path() -> Path | None:
    """Locate the user's `nam_tone.db` across the supported OSes."""
    cfg = config_dir()
    return (cfg / "nam_tone.db") if cfg else None


def extract_tone_id(s: str) -> int | None:
    """Pull the trailing numeric id out of a tone3000 URL or a bare id.

    tone3000 URLs end in `-<id>`, e.g.
    `https://www.tone3000.com/tones/jcm800-2203-amp-37987`. Grab the
    last run of digits; works for the URL form and for a bare id.
    """
    s = str(s).strip()
    m = re.search(r"(\d+)\s*$", s)
    if not m:
        m = re.search(r"(\d+)", s)
    return int(m.group(1)) if m else None


def capture_title(m: dict) -> str:
    """Best-effort human title for a tone3000 model object.

    tone3000 has shipped the title under several field names across API
    revisions (`title`, `name`, `display_name`, `description`). Take the
    first non-empty one, then fall back to the URL-derived filename, then
    to a `model_<id>` placeholder.
    """
    for k in ("title", "name", "display_name", "description"):
        v = m.get(k)
        if isinstance(v, str) and v.strip():
            return v.strip()
    url = m.get("model_url") or m.get("url") or ""
    if url:
        return url.split("/")[-1].split("?")[0]
    return f"model_{m.get('id')}"


def safe_filename(s: str) -> str:
    """Convert a free-text gear name to a Finder-friendly filename.

    Strips path separators and collapses runs of weird chars to a single
    space. Accented letters survive (macOS/APFS handles them fine and
    they're more readable than ASCII transliteration).
    """
    s = re.sub(r"[\\/:*?\"<>|]+", " ", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s or "unnamed"


def load_tone3000_client(plugin_dir: Path | str | None = None,
                         cache_name: str = "rig_builder_cli"):
    """Build a `Tone3000Client` using the plugin's stored credentials.

    Replaces the bootstrap that `curate_amp`, `inspect_tone3000` and
    `make_gain_variants` each open-coded: add the plugin dir to `sys.path`,
    import the client, read OAuth/API creds from `rig_builder_settings.json`,
    and point the SQLite cache at a throwaway file under the temp dir (so the
    CLI never contends with a running Slopsmith for the real cache).

    Exits the process with a helpful message on any unrecoverable problem —
    these are interactive dev tools, so failing loudly is the right call.
    """
    root = Path(plugin_dir).resolve() if plugin_dir else PLUGIN_ROOT
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    try:
        from rb_core.tone3000_client import Tone3000Client
    except ImportError as e:
        print(f"error: rb_core.tone3000_client not importable from {root}: {e}",
              file=sys.stderr)
        sys.exit(1)

    cfg = config_dir()
    settings_path = (cfg / "rig_builder_settings.json") if cfg else None
    if not settings_path or not settings_path.exists():
        print(f"error: settings file missing at {settings_path}", file=sys.stderr)
        print("Sign in to tone3000 from the plugin's Settings page first.",
              file=sys.stderr)
        sys.exit(1)
    settings = json.loads(settings_path.read_text())
    access_token = settings.get("tone3000_access_token") or None
    refresh_token = settings.get("tone3000_refresh_token") or None
    api_key = settings.get("tone3000_api_key") or ""
    if not access_token and not api_key:
        print("error: no tone3000 credentials in settings. Sign in via "
              "Settings → Connect with tone3000, or paste an API key.",
              file=sys.stderr)
        sys.exit(1)

    # Throwaway cache so this CLI doesn't share SQLite state with a running
    # plugin (avoids "database is locked").
    cache_db = Path(tempfile.gettempdir()) / f"{cache_name}_cache.sqlite"
    return Tone3000Client(
        cache_db_path=str(cache_db),
        api_key=api_key,
        access_token=access_token,
        refresh_token=refresh_token,
    )
