#!/usr/bin/env python3
"""Offline FULL-CHAIN loudness of a tone (preset): pedal -> amp -> rack -> cab.

Diagnostic for "why do a song's tones sound at different volumes". Each VST piece
is compiled into a separate stdin->stdout pipe process (tools/dpf_pipe_harness.cpp.in)
with its real RS-mapped params; the cab IR + BS.1770 LUFS is the final C stage
(tools/cab_lufs.cpp). Reports each tone's pre-leveler output LUFS so we can see
how far apart they actually are (and whether the final leveler's range can close
the gap). Skips tones whose racks allocate in activate() (reverb/chorus) — those
can't be measured offline.

Usage: measure_tone_loudness.py <preset_id> [<preset_id> ...]   (bundled Python)
"""
import json
import math
import re
import struct
import subprocess
import sys
import sqlite3
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import measure_amp_loudness as mal   # reuse _parse_amp, _sdk_path

import routes
routes._plugin_dir = ROOT
routes._config_dir = (ROOT.parent.parent / "slopsmith-config").resolve()
routes._json_cache = {}
DB = routes._config_dir / "nam_tone.db"

PIPE_TPL = (ROOT / "tools" / "dpf_pipe_harness.cpp.in").read_text()
SR = 48000
_compiled: dict[str, str] = {}


def _parse_plugin(d: Path):
    """Robust DPF parse for any pedal/rack/amp (handles non-*Params.h headers
    like EQ5Bands.h): the .cpp with createPlugin -> class; DISTRHO_PLUGIN_NAME;
    and the param-name array `k*Names[...]` in ANY header."""
    cpps = [p for p in d.glob("*.cpp") if "createPlugin" in p.read_text()]
    if not cpps:
        return None
    src = cpps[0].read_text()
    m = re.search(r"createPlugin\s*\(\s*\)\s*\{\s*return\s+new\s+(\w+)", src) \
        or re.search(r"class\s+(\w+)\s*:\s*public\s+Plugin\b", src)
    info = (d / "DistrhoPluginInfo.h")
    mn = re.search(r'#define\s+DISTRHO_PLUGIN_NAME\s+"([^"]+)"', info.read_text()) if info.exists() else None
    if not m or not mn:
        return None
    names = []
    for h in d.glob("*.h"):
        ma = re.search(r"k\w*Names\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", h.read_text(), re.S)
        if ma:
            names = re.findall(r'"([^"]*)"', ma.group(1))
            break
    return {"plugin_cpp": cpps[0].name, "class": m.group(1),
            "bundle_stem": mn.group(1).lower(), "names": names}


def _gear_to_srcdir():
    """bundle-stem(lower) -> source dir, across pedals/racks/amps."""
    out = {}
    for r in ("vst/src/pedals", "vst/src/racks", "vst/src/amps"):
        d = ROOT / r
        if not d.exists():
            continue
        for sub in d.iterdir():
            info = sub / "DistrhoPluginInfo.h"
            if info.exists():
                m = re.search(r'#define\s+DISTRHO_PLUGIN_NAME\s+"([^"]+)"', info.read_text())
                if m:
                    out.setdefault(m.group(1).lower(), sub)
    return out


def _bundled_stem(gear, g2v):
    for c in g2v.get(gear, []):
        if isinstance(c, dict) and c.get("bundled"):
            return Path(c["bundled"]).stem.lower()
    return None


def _multitone(n):
    freqs = (110, 220, 440, 880, 1760)
    s = [sum(math.sin(2 * math.pi * f * i / SR) for f in freqs) for i in range(n)]
    rms = math.sqrt(sum(v * v for v in s) / n)
    g = 0.10 / rms if rms else 0.0
    return struct.pack("<%df" % n, *[v * g for v in s])


def _compile(srcdir: Path) -> str | None:
    key = str(srcdir)
    if key in _compiled:
        return _compiled[key]
    info = _parse_plugin(srcdir)   # works for any DPF plugin (createPlugin + Params.h)
    if not info:
        print(f"  [parse fail] {srcdir.name}", file=sys.stderr)
        return None
    probe = PIPE_TPL.replace("@PLUGIN_CPP@", info["plugin_cpp"]).replace("@CLASS@", info["class"])
    (srcdir / "_pipe.cpp").write_text(probe)
    binp = tempfile.NamedTemporaryFile(suffix="_pipe", delete=False).name
    cmd = ["/usr/bin/clang++", "-isysroot", mal._sdk_path(), "-std=c++14", "-O2",
           "-I.", "-I..", "-I../_shared", "-I../../DPF/distrho", "-I../../DPF/dgl",
           "_pipe.cpp", "../../DPF/distrho/src/DistrhoPlugin.cpp", "-o", binp]
    r = subprocess.run(cmd, cwd=srcdir, capture_output=True, text=True)
    (srcdir / "_pipe.cpp").unlink(missing_ok=True)
    if r.returncode != 0:
        print(f"  [compile fail] {srcdir.name}:\n{r.stderr[-1200:]}", file=sys.stderr)
        return None
    _compiled[key] = (binp, info)
    return _compiled[key]


def _names(srcdir):
    info = _parse_plugin(srcdir)
    return info["names"] if info else []


def measure(preset_id, g2v, gd, conn, cab_lufs_bin):
    rows = conn.execute(
        "SELECT slot, kind, file, rs_gear_type, vst_path, vst_state, params_json "
        "FROM preset_pieces WHERE preset_id=? ORDER BY slot_order", (preset_id,)).fetchall()
    order = {"pre_pedal": 0, "amp": 1, "post_pedal": 2, "rack": 3}
    vsts = sorted([r for r in rows if r[1] == "vst" and r[4]],
                  key=lambda r: order.get(r[0], 9))
    irrow = next((r for r in rows if r[1] in ("ir", "rs_ir") and r[2]), None)

    buf = _multitone(int(SR * 3))
    chain_names = []
    for slot, kind, file, gear, vst_path, vst_state, params_json in vsts:
        stem = Path(vst_path).stem.lower()
        srcdir = gd.get(stem)
        if not srcdir:
            print(f"  [no src] {gear} {stem}", file=sys.stderr); return None
        comp = _compile(srcdir)
        if not comp:
            print(f"  [skip piece] {gear} {stem} (unparseable — treated as passthrough)", file=sys.stderr)
            chain_names.append(f"{gear}(SKIP)")
            continue
        binp, info = comp
        names = info["names"]
        idx_of = {n: i for i, n in enumerate(names)}
        eff = routes._effective_vst_state_for_piece(gear, vst_path, vst_state, params_json)
        params = routes._amp_params_from_state(eff) or {}
        lines = [f"{idx_of[n]} {v}" for n, v in params.items() if n in idx_of]
        pf = tempfile.NamedTemporaryFile("w", suffix=".prm", delete=False)
        pf.write("\n".join(lines)); pf.close()
        run = subprocess.run([binp, pf.name], input=buf, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        Path(pf.name).unlink(missing_ok=True)
        buf = run.stdout
        chain_names.append(f"{gear}({stem})")

    # Cab IR + LUFS (RS IRs get +12 dB = ×4.0, like _ir_stage_gain).
    if irrow:
        ir_path = routes._config_dir / "nam_irs" / irrow[2]
        res = routes._read_ir_samples(ir_path)
        if not res:
            print(f"  [ir read fail] {ir_path}", file=sys.stderr); return None
        samples, _sr = res
        irf = tempfile.NamedTemporaryFile(suffix=".f32", delete=False).name
        Path(irf).write_bytes(struct.pack("<%df" % len(samples), *samples))
        gain = 4.0
        run = subprocess.run([cab_lufs_bin, irf, str(gain)], input=buf, stdout=subprocess.PIPE, text=False)
        Path(irf).unlink(missing_ok=True)
        lufs = float(run.stdout.decode().strip())
        cabtag = f"+cab({irrow[2].split('/')[-1]}×4.0)"
    else:
        # no cab: measure the piped signal directly
        run = subprocess.run([cab_lufs_bin, "/nonexistent", "1.0"], input=buf, stdout=subprocess.PIPE)
        lufs = float(run.stdout.decode().strip())
        cabtag = "(no cab)"
    print(f"preset {preset_id}: {' -> '.join(chain_names)} {cabtag}")
    print(f"   PRE-LEVELER OUTPUT = {lufs:.2f} LUFS")
    return lufs


def main(argv):
    if not argv:
        print("usage: measure_tone_loudness.py <preset_id> ..."); return 2
    g2v = json.loads((ROOT / "data" / "rs_gear_to_vst.json").read_text())
    gd = _gear_to_srcdir()
    conn = sqlite3.connect(DB)
    cab_lufs_bin = tempfile.NamedTemporaryFile(suffix="_cablufs", delete=False).name
    c = subprocess.run(["/usr/bin/clang++", "-std=c++14", "-O2",
                        str(ROOT / "tools" / "cab_lufs.cpp"), "-o", cab_lufs_bin],
                       capture_output=True, text=True)
    if c.returncode != 0:
        print("cab_lufs compile fail:\n" + c.stderr, file=sys.stderr); return 1
    res = {}
    for pid in argv:
        v = measure(int(pid), g2v, gd, conn, cab_lufs_bin)
        if v is not None:
            res[pid] = v
    if len(res) > 1:
        vals = list(res.values())
        print(f"\nSPREAD across {len(res)} tones: {max(vals)-min(vals):.1f} dB "
              f"(min {min(vals):.1f}, max {max(vals):.1f} LUFS)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
