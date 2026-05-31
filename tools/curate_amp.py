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
import sys
from pathlib import Path

from common import (
    PLUGIN_ROOT, extract_tone_id, capture_title, load_tone3000_client,
)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url_or_id",
                    help="tone3000 URL or numeric tone_id.")
    ap.add_argument("--plugin-dir", default=str(PLUGIN_ROOT),
                    help="rig_builder plugin directory (default: cwd).")
    args = ap.parse_args()

    tone_id = extract_tone_id(args.url_or_id)
    if tone_id is None:
        print(f"error: no numeric id found in {args.url_or_id!r}",
              file=sys.stderr)
        sys.exit(1)

    client = load_tone3000_client(args.plugin_dir, cache_name="rig_builder_curate_amp")

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
