#!/usr/bin/env python3
"""List the captures inside a tone3000 tone page.

Each tone3000 URL (e.g. `https://www.tone3000.com/tones/jcm800-2203-amp-37987`)
can host multiple captures (NAM models) — different sizes, different mic
positions, sometimes even different gain settings of the same amp. When
curating an entry for the rs_to_real.json `gain_variants` you may want
to pin a SPECIFIC capture rather than letting `pick_best_model` choose
by size preference.

Run this with the tone3000_id (the number at the end of the URL) and
it prints every capture in that page: model_id, size, filename, and
download URL.

Usage:

    python3 inspect_tone3000.py 37987

Or pass the full URL — the script extracts the trailing number:

    python3 inspect_tone3000.py https://www.tone3000.com/tones/jcm800-2203-amp-37987

The output looks like:

    tone3000_id: 37987 — jcm800-2203-amp
    ─────────────────────────────────────────────────────────
    model_id: 124567  size: standard  name: jcm800_neckpickup.nam
    model_id: 124568  size: lite      name: jcm800_neckpickup_lite.nam
    model_id: 124569  size: standard  name: jcm800_bridgepickup.nam
    model_id: 124570  size: feather   name: jcm800_neckpickup_f.nam

Then in your curation CSV, set `model_id` to the row whose capture you
want (e.g. 124569 if you want the bridge-pickup version specifically).
If you leave `model_id` empty, the plugin falls back to picking by
preferred_size automatically (current behaviour).

Auth: uses the OAuth token already saved in your rig_builder_settings.json
(same one the plugin uses). If you haven't logged in via Settings → Connect
with tone3000 yet, do that first.
"""

import argparse
import json
import re
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tone_id_or_url",
                    help="The tone3000_id (e.g. 37987) or the full URL.")
    ap.add_argument("--plugin-dir", default=".",
                    help="rig_builder plugin directory (default: current dir).")
    args = ap.parse_args()

    # Extract a numeric ID from whatever the user passed.
    s = str(args.tone_id_or_url).strip()
    m = re.search(r"(\d+)\s*$", s)
    if not m:
        # Maybe a URL with the ID elsewhere — fall back to last number anywhere.
        m = re.search(r"(\d+)", s)
    if not m:
        print(f"error: couldn't find a numeric tone3000_id in {s!r}", file=sys.stderr)
        sys.exit(1)
    tone_id = int(m.group(1))

    plugin_dir = Path(args.plugin_dir).resolve()
    # The OAuth token + settings are read by Tone3000Client from this dir.
    # Import after sys.path tweak so the tone3000_client and settings logic
    # resolves the same way the running plugin does.
    sys.path.insert(0, str(plugin_dir))

    try:
        from tone3000_client import Tone3000Client
    except ImportError as e:
        print(f"error: couldn't import tone3000_client (--plugin-dir {plugin_dir}): {e}", file=sys.stderr)
        sys.exit(1)

    # Resolve the settings file the plugin uses.
    config_dir = Path.home() / "Library" / "Application Support" / "slopsmith-desktop" / "slopsmith-config"
    settings_path = config_dir / "rig_builder_settings.json"
    if not settings_path.exists():
        print(f"error: settings file not found at {settings_path}", file=sys.stderr)
        print("Sign in to tone3000 from the plugin's Settings page first.", file=sys.stderr)
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

    # Tone3000Client requires a cache_db_path (it caches API JSON in
    # SQLite). Use a throwaway under /tmp so this CLI doesn't pollute
    # the real plugin cache.
    import tempfile
    cache_db = Path(tempfile.gettempdir()) / "rig_builder_inspect_tone3000_cache.sqlite"
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
        print(f"tone3000_id {tone_id} has no models (or you don't have access).", file=sys.stderr)
        sys.exit(1)

    # Try to surface the tone's title/slug for context (the list_models
    # response may include it depending on the API version).
    tone_title = (payload or {}).get("title") or ""
    if tone_title:
        print(f"\ntone3000_id: {tone_id} — {tone_title}")
    else:
        print(f"\ntone3000_id: {tone_id}")
    print("─" * 70)
    print(f"{'model_id':<10}  {'size':<10}  {'license':<14}  title")
    print("─" * 70)
    for m in models:
        mid = m.get("id") or "?"
        size = (m.get("size") or "?").lower()
        license_name = (m.get("license") or "")[:14]
        # Prefer the human-readable title — that's what tone3000's web
        # UI shows and it encodes the knob settings ("G7 B5 M5 T5 P5
        # V5 - STD" where G7 = Gain=7), which is the whole point of
        # picking a capture by variant. Fall back to URL-derived
        # filename only when the API gave us nothing.
        title = ""
        for k in ("title", "name", "display_name", "description"):
            v = m.get(k)
            if isinstance(v, str) and v.strip():
                title = v.strip()
                break
        if not title:
            url = m.get("model_url") or m.get("url") or ""
            title = (url.split("/")[-1].split("?")[0]
                     if url else f"model_{mid}")
        if len(title) > 70:
            title = title[:67] + "..."
        print(f"{mid:<10}  {size:<10}  {license_name:<14}  {title}")
    print("─" * 70)
    print(f"\nTo pin a specific capture, add the `model_id` column to your")
    print(f"curation CSV row. Leave it blank to let the plugin auto-pick by")
    print(f"preferred_size (current default behaviour).\n")


if __name__ == "__main__":
    main()
