#!/usr/bin/env python3
"""Slim the bundled VST3 plugins down to a single platform.

Every ``.vst3`` bundle we ship carries a binary for each supported OS:

    SomePedal.vst3/Contents/
        MacOS/            <- macOS build
        x86_64-linux/     <- Linux build
        x86_64-win/       <- Windows build
        Info.plist, PkgInfo, Resources/, _CodeSignature/   <- kept always

On any one machine only ONE of those binary folders is ever loaded, so the
other two are dead weight (~2/3 of the bundled-VST size). This tool deletes the
binary folders that don't match the chosen platform, keeping everything else.

Typical use (end user, on their own install):

    python3 tools/prune_vst_platforms.py            # prune to THIS machine
    python3 tools/prune_vst_platforms.py --dry-run  # show what would go first

Cross-check before committing to it, then run for real. Idempotent: re-running
after a prune is a no-op.

SAFETY: the distribution repo intentionally keeps all three platforms. If this
is run inside that git checkout it would stage hundreds of MB of deletions, so
the tool refuses to touch git-tracked bundles unless you pass --force. On an
extracted end-user install (no git) it just runs.
"""
from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

# Binary-folder name -> platform family. Anything NOT listed here (Info.plist,
# PkgInfo, Resources, _CodeSignature, unknown future folders) is left untouched.
def classify(folder_name: str) -> str | None:
    n = folder_name.lower()
    if n == "macos":
        return "macos"
    if n.endswith("-win") or n.endswith("_win") or n == "windows":
        return "windows"
    if n.endswith("-linux"):
        return "linux"
    return None  # not a per-platform binary folder -> keep


def current_family() -> str:
    s = platform.system().lower()
    if s == "darwin":
        return "macos"
    if s == "windows":
        return "windows"
    return "linux"


def dir_size(path: Path) -> int:
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            fp = Path(root) / f
            try:
                total += fp.stat(follow_symlinks=False).st_size
            except OSError:
                pass
    return total


def human(n: int) -> str:
    x = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if x < 1024 or unit == "GB":
            return f"{x:.1f} {unit}"
        x /= 1024
    return f"{x:.1f} GB"


def is_git_tracked(path: Path, repo_root: Path) -> bool:
    try:
        r = subprocess.run(
            ["git", "-C", str(repo_root), "ls-files", "--error-unmatch", str(path)],
            capture_output=True,
        )
        return r.returncode == 0
    except FileNotFoundError:
        return False


def find_git_root(start: Path) -> Path | None:
    try:
        r = subprocess.run(
            ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
            capture_output=True, text=True,
        )
        if r.returncode == 0:
            return Path(r.stdout.strip())
    except FileNotFoundError:
        pass
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", choices=["macos", "windows", "linux", "current"],
                    default="current", help="platform to KEEP (default: this machine)")
    ap.add_argument("--vst-dir", default=None,
                    help="root holding the .vst3 bundles (default: <plugin>/vst)")
    ap.add_argument("--dry-run", action="store_true", help="show what would be removed, delete nothing")
    ap.add_argument("--force", action="store_true",
                    help="allow pruning even when the bundles are git-tracked (distribution repo)")
    args = ap.parse_args()

    keep = current_family() if args.platform == "current" else args.platform
    plugin_root = Path(__file__).resolve().parent.parent
    vst_root = Path(args.vst_dir).resolve() if args.vst_dir else (plugin_root / "vst")
    if not vst_root.is_dir():
        print(f"error: vst dir not found: {vst_root}", file=sys.stderr)
        return 2

    bundles = sorted(vst_root.rglob("*.vst3"))
    bundles = [b for b in bundles if (b / "Contents").is_dir()]
    if not bundles:
        print(f"No .vst3 bundles found under {vst_root}")
        return 0

    git_root = find_git_root(vst_root)
    print(f"Keeping platform: {keep}")
    print(f"VST root:         {vst_root}")
    print(f"Bundles found:    {len(bundles)}")
    if args.dry_run:
        print("Mode:             DRY RUN (nothing will be deleted)\n")

    to_remove: list[Path] = []
    kept_families: set[str] = set()
    for b in bundles:
        contents = b / "Contents"
        for child in sorted(contents.iterdir()):
            if not child.is_dir():
                continue
            fam = classify(child.name)
            if fam is None:
                continue  # Resources / _CodeSignature / etc.
            if fam == keep:
                kept_families.add(fam)
            else:
                to_remove.append(child)

    if not to_remove:
        print("Nothing to remove — bundles already contain only the target platform.")
        return 0

    # Refuse to delete git-tracked bundles unless forced (protects the repo that
    # ships all platforms).
    if git_root and not args.force and not args.dry_run:
        sample_tracked = any(is_git_tracked(p, git_root) for p in to_remove[:5])
        if sample_tracked:
            print(f"\nrefusing: these bundles are tracked in git ({git_root}).")
            print("This looks like the distribution checkout, which keeps all platforms.")
            print("Re-run with --dry-run to preview, or --force if you really mean it.")
            return 3

    total = 0
    for p in to_remove:
        sz = dir_size(p)
        total += sz
        rel = p.relative_to(vst_root)
        print(f"  {'would remove' if args.dry_run else 'removing'}  {human(sz):>9}  {rel}")
        if not args.dry_run:
            shutil.rmtree(p, ignore_errors=True)

    verb = "Would reclaim" if args.dry_run else "Reclaimed"
    print(f"\n{verb} {human(total)} across {len(to_remove)} platform folder(s).")
    if args.dry_run:
        print("Run again without --dry-run to apply.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
