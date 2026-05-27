#!/usr/bin/env python3
"""List every capture inside a tone3000 tone — nothing more.

Usage:

    python3 curate_amp.py https://www.tone3000.com/tones/jcm800-2203-amp-37987

Prints:
  - the tone3000_id (extracted from the URL),
  - one line per capture in the tone with its model_id, size, license,
    and human-readable title.

No auto-assignment, no JSON emission, no opinions. You eyeball the
table, pick which model_id goes to which Rocksmith gain level, then
hand-edit `rs_to_real.json` (or the curation CSV).

Auth: reuses the API key (or OAuth tokens) already in the plugin's
`rig_builder_settings.json` — same store as the running plugin, so if
you can download captures from inside Slopsmith this script also works.
"""

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path


def extract_tone_id(s: str) -> int | None:
    """Pull the trailing numeric id out of a URL or a bare id.

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

    tone3000 has shipped the title under several field names across
    API revisions (`title`, `name`, `display_name`, `description`).
    Take the first non-empty one. Fall back to the URL-derived hash
    filename (which is opaque) only when nothing better is available.
    """
    for k in ("title", "name", "display_name", "description"):
        v = m.get(k)
        if isinstance(v, str) and v.strip():
            return v.strip()
    url = m.get("model_url") or m.get("url") or ""
    if url:
        return url.split("/")[-1].split("?")[0]
    return f"model_{m.get('id')}"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url_or_id",
                    help="tone3000 URL or numeric tone_id.")
    ap.add_argument("--plugin-dir", default=".",
                    help="rig_builder plugin directory (default: cwd).")
    args = ap.parse_args()

    tone_id = extract_tone_id(args.url_or_id)
    if tone_id is None:
        print(f"error: no numeric id found in {args.url_or_id!r}",
              file=sys.stderr)
        sys.exit(1)

    plugin_dir = Path(args.plugin_dir).resolve()
    sys.path.insert(0, str(plugin_dir))
    try:
        from tone3000_client import Tone3000Client
    except ImportError as e:
        print(f"error: tone3000_client not importable from {plugin_dir}: {e}",
              file=sys.stderr)
        sys.exit(1)

    # Reuse the plugin's credentials so the user doesn't have to paste
    # an API key here. Either an OAuth token or a t3k_ key gets us
    # past /models?tone_id=…
    config_dir = (Path.home() / "Library" / "Application Support"
                  / "slopsmith-desktop" / "slopsmith-config")
    settings_path = config_dir / "rig_builder_settings.json"
    if not settings_path.exists():
        print(f"error: settings file missing at {settings_path}",
              file=sys.stderr)
        sys.exit(1)
    settings = json.loads(settings_path.read_text())
    access_token = settings.get("tone3000_access_token") or None
    refresh_token = settings.get("tone3000_refresh_token") or None
    api_key = settings.get("tone3000_api_key") or ""
    if not access_token and not api_key:
        print("error: no tone3000 credentials in settings.", file=sys.stderr)
        sys.exit(1)

    # Throwaway cache under /tmp so this CLI doesn't share SQLite state
    # with the plugin (avoids "database is locked" if Slopsmith is open).
    cache_db = (Path(tempfile.gettempdir())
                / "rig_builder_curate_amp_cache.sqlite")
    client = Tone3000Client(
        cache_db_path=str(cache_db),
        api_key=api_key,
        access_token=access_token,
        refresh_token=refresh_token,
    )

    try:
        payload = client.list_models(tone_id)
    except Exception as e:
        print(f"error: list_models({tone_id}) failed: {e}", file=sys.stderr)
        sys.exit(1)

    models = (payload or {}).get("data") or []
    if not models:
        print(f"tone3000_id {tone_id} returned no captures.", file=sys.stderr)
        sys.exit(1)

    tone_title = (payload or {}).get("title") or ""
    print(f"\ntone3000_id: {tone_id}" + (f" — {tone_title}" if tone_title else ""))
    print("─" * 90)
    print(f"{'model_id':<10}  {'size':<10}  {'license':<14}  title")
    print("─" * 90)
    for m in models:
        mid = m.get("id") or "?"
        size = (m.get("size") or "").lower() or "?"
        lic = (m.get("license") or "")[:14]
        title = capture_title(m)
        # Don't truncate — the title is the WHOLE point of the table.
        # Long titles wrap on the terminal but you can still read them
        # and copy/paste cleanly.
        print(f"{mid:<10}  {size:<10}  {lic:<14}  {title}")
    print("─" * 90)
    print(f"\n{len(models)} captures.\n")


if __name__ == "__main__":
    main()
