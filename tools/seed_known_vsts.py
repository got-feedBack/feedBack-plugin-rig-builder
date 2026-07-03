#!/usr/bin/env python3
"""Workaround for Slopsmith's `api.scanPlugins()` crash bug.

JUCE's plugin validation crashes the host on machines with a malformed
plugin — confirmed in HANDOFF.md and WHATS_NEW.md. The Settings → Scan
button gets stuck partway through and the `rig_builder_known_vsts.json`
cache only contains what was scanned BEFORE the crash. Plugins in
subdirectories (Kilohearts/, MeldaProduction/) are often missed because
the scan dies before reaching them.

This script does what scanPlugins() would have done, but safely from the
filesystem instead of the JUCE host: it walks the standard VST3 + AU
install dirs, lists every `.vst3` bundle and `.component` bundle it
finds, and merges them into the cache (deduping by path). After running,
restart Slopsmith — the per-piece VST picker dropdown will show the new
plugins without ever needing to call scanPlugins.

What this script CANNOT do (vs a real scan):
  - Resolve a plugin's true manufacturer / category / JUCE uid. Those
    come from each plugin's metadata via JUCE's PluginDescription. We
    use sensible defaults (manufacturer = parent dir if it looks like a
    vendor folder; category = "Fx" since we're guitar effects; uid is
    synthesized from the path so the runtime sees a unique key).
  - Detect VSTi (synth) instruments. We mark everything `isInstrument =
    false`. The picker for rig_builder is FX-only anyway.
  - Filter plugins that crash on load. Anything malformed will still
    blow up when the user picks it.

Usage:
    # Dry-run: show what would be added
    python3 seed_known_vsts.py

    # Apply: write to rig_builder_known_vsts.json
    python3 seed_known_vsts.py --apply

    # Wipe + reseed from scratch (drops the old 67 entries that scan
    # collected before crashing — useful if some are stale paths)
    python3 seed_known_vsts.py --apply --replace
"""

import argparse
import json
import os
import platform
import sys
import time
from pathlib import Path

from common import PLUGIN_ROOT


def _vst_roots() -> list[Path]:
    system = platform.system()
    roots = [PLUGIN_ROOT / "vst"]
    if system == "Darwin":
        roots += [
            Path("/Library/Audio/Plug-Ins/VST3"),
            Path.home() / "Library/Audio/Plug-Ins/VST3",
        ]
    elif system == "Windows":
        common = Path(os.environ.get("CommonProgramFiles", r"C:\Program Files\Common Files"))
        roots += [common / "VST3"]
    else:
        roots += [Path.home() / ".vst3"]
    return roots


def _au_roots() -> list[Path]:
    if platform.system() != "Darwin":
        return []
    return [
        Path("/Library/Audio/Plug-Ins/Components"),
        Path.home() / "Library/Audio/Plug-Ins/Components",
    ]


def _cache_path() -> Path | None:
    system = platform.system()
    if system == "Darwin":
        return Path.home() / "Library/Application Support/slopsmith-desktop/slopsmith-config/rig_builder_known_vsts.json"
    if system == "Windows":
        appdata = os.environ.get("APPDATA")
        if appdata:
            return Path(appdata) / "slopsmith-desktop/slopsmith-config/rig_builder_known_vsts.json"
        return None
    xdg = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(xdg) / "slopsmith-desktop/slopsmith-config/rig_builder_known_vsts.json"


def _discover(roots: list[Path], suffix: str, format_label: str) -> list[dict]:
    """Walk roots and return a plugin entry per `.suffix` bundle found.

    Bundle = directory whose name ends with `suffix` (VST3/AU are bundles
    on macOS). Walks 3 levels deep to catch vendor subdirs (Kilohearts/,
    MeldaProduction/Modulation/, …).
    """
    out = []
    for root in roots:
        if not root.exists():
            continue
        for entry in root.rglob(f"*{suffix}"):
            try:
                rel = entry.relative_to(root)
            except ValueError:
                continue
            if len(rel.parts) > 4:
                continue
            name = entry.stem  # filename without suffix
            # Best-effort manufacturer: the first subdir below the root,
            # if it looks like a vendor folder (no spaces in folder name,
            # short, capitalized). Otherwise empty.
            mfg = ""
            if len(rel.parts) >= 2:
                cand = rel.parts[0]
                if cand and cand[0].isupper() and len(cand) <= 30:
                    mfg = cand
            out.append({
                "name": name,
                "manufacturer": mfg,
                "category": "Fx",
                "format": format_label,
                "path": str(entry),
                "uid": f"manual-{format_label}-{name}",
                "isInstrument": False,
            })
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--cache", type=Path, default=None,
                    help="Path to rig_builder_known_vsts.json (default: auto-detect)")
    ap.add_argument("--apply", action="store_true",
                    help="Write the cache (default is dry-run)")
    ap.add_argument("--replace", action="store_true",
                    help="With --apply: replace the cache entirely instead of merging")
    args = ap.parse_args()

    cache_path = args.cache or _cache_path()
    if cache_path is None:
        print("Couldn't locate known_vsts cache.", file=sys.stderr)
        return 1

    # Discover
    discovered = _discover(_vst_roots(), ".vst3", "VST3")
    discovered += _discover(_au_roots(), ".component", "AudioUnit")
    by_path = {p["path"]: p for p in discovered}

    # Load existing (if any)
    existing_by_path: dict = {}
    if cache_path.exists():
        try:
            existing = json.loads(cache_path.read_text())
            for p in existing.get("plugins", []) or []:
                if isinstance(p, dict) and p.get("path"):
                    existing_by_path[p["path"]] = p
        except (ValueError, OSError):
            print(f"Warning: existing cache at {cache_path} unreadable, will rebuild.")

    # Merge
    if args.replace:
        merged = by_path
    else:
        merged = dict(existing_by_path)
        for path, plugin in by_path.items():
            # Prefer existing real-scan entries (richer metadata) over our synthetic ones.
            if path in merged:
                # Replace only if existing is one of our own synthetic uids.
                if str(merged[path].get("uid", "")).startswith("manual-"):
                    merged[path] = plugin
            else:
                merged[path] = plugin

    added = sorted(set(by_path) - set(existing_by_path))
    removed = sorted(set(existing_by_path) - set(by_path)) if args.replace else []

    print(f"Discovered {len(by_path)} plugin file(s) across VST3 + AU dirs.")
    print(f"Existing cache: {len(existing_by_path)} entries.")
    print(f"Final cache:    {len(merged)} entries  (+{len(added)} new" +
          (f", -{len(removed)} dropped" if args.replace else "") + ")")

    if added:
        print(f"\nNew plugins ({len(added)}):")
        for p in added[:50]:
            entry = by_path[p]
            print(f"  {entry['name']:30s}  {entry['format']:10s}  {p}")
        if len(added) > 50:
            print(f"  …and {len(added) - 50} more")
    if removed:
        print(f"\nDropped (no longer on disk; --replace mode):")
        for p in removed[:20]:
            print(f"  {p}")

    if not args.apply:
        print("\n(dry-run; pass --apply to write the cache)")
        return 0

    # Write
    payload = {
        "plugins": sorted(merged.values(), key=lambda x: x.get("name", "").lower()),
        "synced_at": int(time.time()),
    }
    cache_path.write_text(json.dumps(payload, indent=2))
    print(f"\nWrote {len(payload['plugins'])} entries to {cache_path}")
    print("Restart Slopsmith — the VST picker dropdown will show the new plugins.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
