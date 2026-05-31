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
import sys
from pathlib import Path

from common import PLUGIN_ROOT, extract_tone_id, capture_title, load_tone3000_client


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tone_id_or_url",
                    help="The tone3000_id (e.g. 37987) or the full URL.")
    ap.add_argument("--plugin-dir", default=str(PLUGIN_ROOT),
                    help="rig_builder plugin directory (default: current dir).")
    args = ap.parse_args()

    # Extract a numeric ID from whatever the user passed.
    tone_id = extract_tone_id(args.tone_id_or_url)
    if tone_id is None:
        print(f"error: couldn't find a numeric tone3000_id in {args.tone_id_or_url!r}", file=sys.stderr)
        sys.exit(1)

    client = load_tone3000_client(args.plugin_dir, cache_name="rig_builder_inspect_tone3000")

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
        title = capture_title(m)
        if len(title) > 70:
            title = title[:67] + "..."
        print(f"{mid:<10}  {size:<10}  {license_name:<14}  {title}")
    print("─" * 70)
    print(f"\nTo pin a specific capture, add the `model_id` column to your")
    print(f"curation CSV row. Leave it blank to let the plugin auto-pick by")
    print(f"preferred_size (current default behaviour).\n")


if __name__ == "__main__":
    main()
