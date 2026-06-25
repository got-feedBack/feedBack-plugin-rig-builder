#!/usr/bin/env python3
"""Offline per-amp loudness sweep → data/amp_loudness_model.json.

For every amp under vst/src/amps/<dir>/ this:
  1. locates the DPF *Plugin.cpp (the one with createPlugin) + its class name,
     the *Params.h param-name array, and DISTRHO_PLUGIN_NAME (→ bundle stem),
  2. decides which params affect loudness — the param RS "Gain" maps to (from
     data/rs_knob_to_vst_param.json), plus any Volume/Master pots by name,
  3. compiles tools/amp_lufs_harness.cpp.in (substituting the include + class),
  4. runs it: it sweeps each loudness param 0→1 and reports BS.1770-4 integrated
     LUFS of the amp output at the multitone operating point,
  5. writes the curves to data/amp_loudness_model.json (keyed by bundle stem,
     the same key routes.py derives from the amp's vst_path).

This is a one-shot calibration tool; rig builder consumes the JSON at playback.
Run with the bundled Python (it just shells out to clang). Pass amp dir names as
args to (re)measure a subset; omit to do all. See AMP_LOUDNESS.md for the harness
recipe and the loudness standard.
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # plugins/rig_builder
AMPS = ROOT / "vst" / "src" / "amps"
DPF_SRC = "../DPF/distrho/src/DistrhoPlugin.cpp"        # relative to an amp dir
TEMPLATE = (ROOT / "tools" / "amp_lufs_harness.cpp.in").read_text()
MODEL = ROOT / "data" / "amp_loudness_model.json"
STEPS = 11

# Params whose NAME marks a clean volume/master pot (swept in addition to Gain).
# Deliberately narrow: "level"/"output"/"loudness" over-match EQ bands ("Mid
# Level"), power switches ("Output 50/100W") and the Plexi gain pots.
_VOL_RE = re.compile(r"\b(vol|volume|master)\b", re.I)
# …but NOT these (switches / mode selectors / voicing, not level controls).
_VOL_EXCLUDE_RE = re.compile(r"sel|select|mode|switch|shift|type|bright|boost|"
                             r"sag|bias|class|tone|deep|fat|voice", re.I)


def _sdk_path() -> str:
    return subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()


def _gear_index() -> dict:
    """bundle-stem(lower) -> [rs_gear_type, ...] from rs_gear_to_vst.json."""
    g2v = json.loads((ROOT / "data" / "rs_gear_to_vst.json").read_text())
    inv: dict[str, list[str]] = {}
    for gear, cands in g2v.items():
        if not isinstance(cands, list):
            continue
        for c in cands:
            b = c.get("bundled") if isinstance(c, dict) else None
            if b and "/amps/" in b:
                stem = Path(b).stem.lower()
                inv.setdefault(stem, []).append(gear)
    return inv


def _gain_param_names(knob_map: dict, gears: list[str], stem: str) -> list[str]:
    """The VST param name(s) RS 'Gain' (and Plexi's Loudness pots) map to."""
    names: list[str] = []
    for gear in gears:
        blk = knob_map.get(gear, {})
        sub = blk.get(stem) if isinstance(blk.get(stem), dict) else blk
        if not isinstance(sub, dict):
            continue
        for knob in ("Gain", "Loudness1", "Loudness2"):
            spec = sub.get(knob)
            if isinstance(spec, dict) and spec.get("param"):
                p = spec["param"]
                if p not in names:
                    names.append(p)
    return names


def _parse_amp(d: Path) -> dict | None:
    cpps = [p for p in d.glob("*.cpp") if "createPlugin" in p.read_text()]
    if not cpps:
        return None
    plugin_cpp = cpps[0]
    src = plugin_cpp.read_text()
    m = re.search(r"createPlugin\s*\(\s*\)\s*\{\s*return\s+new\s+(\w+)", src)
    if not m:
        m = re.search(r"class\s+(\w+)\s*:\s*public\s+Plugin\b", src)
    if not m:
        return None
    cls = m.group(1)

    info = (d / "DistrhoPluginInfo.h").read_text()
    mn = re.search(r'#define\s+DISTRHO_PLUGIN_NAME\s+"([^"]+)"', info)
    if not mn:
        return None
    bundle_stem = mn.group(1).lower()

    hdrs = list(d.glob("*Params.h"))
    if not hdrs:
        return None
    htext = hdrs[0].read_text()
    ma = re.search(r"k\w*Names\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", htext, re.S)
    if not ma:
        return None
    names = re.findall(r'"([^"]*)"', ma.group(1))
    return {"dir": d, "plugin_cpp": plugin_cpp.name, "class": cls,
            "bundle_stem": bundle_stem, "names": names}


def _choose_params(info: dict, gain_names: list[str]) -> tuple[list[int], list[int]]:
    names = info["names"]
    idx_of = {n: i for i, n in enumerate(names)}
    gain_idx = [idx_of[n] for n in gain_names if n in idx_of]
    if not gain_idx:  # fallback: heuristic by name
        for i, n in enumerate(names):
            if re.search(r"gain|drive|loudness|preamp", n, re.I) and \
               not _VOL_EXCLUDE_RE.search(n):
                gain_idx.append(i)
                break
    gain_set = set(gain_idx)
    vol_idx = [i for i, n in enumerate(names)
               if _VOL_RE.search(n) and not _VOL_EXCLUDE_RE.search(n)
               and i not in gain_set]
    return gain_idx, vol_idx


def _compile_and_run(info: dict, gain_idx, vol_idx) -> dict | None:
    d: Path = info["dir"]
    probe = TEMPLATE.replace("@PLUGIN_CPP@", info["plugin_cpp"]).replace("@CLASS@", info["class"])
    probe_path = d / "_lufs_probe.cpp"
    probe_path.write_text(probe)
    try:
        with tempfile.NamedTemporaryFile(suffix="_probe", delete=False) as tf:
            binpath = tf.name
        cmd = ["/usr/bin/clang++", "-isysroot", _sdk_path(), "-std=c++14", "-O2",
               "-I.", "-I..", "-I../DPF/distrho", "-I../DPF/dgl",
               "_lufs_probe.cpp", DPF_SRC, "-o", binpath]
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  COMPILE FAIL:\n{r.stderr[-1500:]}", file=sys.stderr)
            return None
        g = ",".join(str(i) for i in gain_idx) or "-"
        v = ",".join(str(i) for i in vol_idx) or "-"
        run = subprocess.run([binpath, g, v, str(STEPS)], capture_output=True, text=True)
        if run.returncode != 0 or not run.stdout.strip():
            print(f"  RUN FAIL rc={run.returncode}: {run.stderr[-500:]}", file=sys.stderr)
            return None
        return json.loads(run.stdout)
    finally:
        probe_path.unlink(missing_ok=True)
        Path(binpath).unlink(missing_ok=True)


def measure_amp(d: Path, gear_idx: dict, knob_map: dict) -> tuple[str, dict] | None:
    info = _parse_amp(d)
    if not info:
        print(f"[skip] {d.name}: could not parse", file=sys.stderr)
        return None
    stem = info["bundle_stem"]
    gears = gear_idx.get(stem, [])
    gain_names = _gain_param_names(knob_map, gears, stem)
    gain_idx, vol_idx = _choose_params(info, gain_names)
    names = info["names"]
    print(f"[{d.name}] stem={stem} class={info['class']} "
          f"gain={[names[i] for i in gain_idx]} vol={[names[i] for i in vol_idx]}")
    res = _compile_and_run(info, gain_idx, vol_idx)
    if not res:
        return None
    defs = res["def"]
    entry = {
        "gain_params": [names[i] for i in gain_idx],
        "vol_params": [names[i] for i in vol_idx],
        "default": {names[i]: round(defs[i], 4) for i in (gain_idx + vol_idx)},
        "lufs_default": res["default"],
        "gain_curve": res["sweeps"].get("gain", []),
        "vol_curves": {names[int(k)]: v for k, v in res["sweeps"].items() if k != "gain"},
    }
    print(f"    lufs_default={entry['lufs_default']:.2f}  "
          f"gain_range=[{entry['gain_curve'][0][1]:.1f}..{entry['gain_curve'][-1][1]:.1f}]"
          if entry["gain_curve"] else f"    lufs_default={entry['lufs_default']:.2f} (no gain sweep)")
    return stem, entry


def main(argv: list[str]) -> int:
    gear_idx = _gear_index()
    knob_map = json.loads((ROOT / "data" / "rs_knob_to_vst_param.json").read_text())
    dirs = sorted(p for p in AMPS.iterdir() if p.is_dir())
    if argv:
        want = set(argv)
        dirs = [d for d in dirs if d.name in want]

    model = {}
    if MODEL.exists() and argv:          # subset run: merge into existing
        model = json.loads(MODEL.read_text())

    ok = 0
    for d in dirs:
        out = measure_amp(d, gear_idx, knob_map)
        if out:
            model[out[0]] = out[1]
            ok += 1
    model["_meta"] = {
        "target_lufs": -14.0, "metric": "BS.1770-4 integrated LUFS (mono, left)",
        "excitation": "110-1760 Hz multitone @ RMS 0.10", "steps": STEPS,
        "note": "lufs_* are measured with the amp's compiled kLvl; rig builder "
                "computes a clean trim from these — it does NOT modify amp params.",
    }
    MODEL.write_text(json.dumps(model, indent=1))
    print(f"\nwrote {MODEL} — {ok}/{len(dirs)} amps")
    return 0 if ok == len(dirs) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
