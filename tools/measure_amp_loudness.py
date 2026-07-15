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
args to (re)measure a subset; omit to do all. See
docs/CHANNEL_LOUDNESS_CALIBRATION.md for the harness recipe and standard.
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

# Explicit channel/mode topology for guitar amps whose selectors cannot be
# inferred from an RS Gain mapping. Each profile is measured with the selector
# values fixed, then only that channel's real gain and level controls are swept.
# Bass amps intentionally stay out of this table.
CHANNEL_PROFILES = {
    "aor50": [
        {"name": "channel_one", "fixed": {"Channel": 0.0}, "gain": ["Ch1 Preamp"], "vol": ["Ch1 Master"]},
        {"name": "aor", "fixed": {"Channel": 1.0}, "gain": ["AOR Preamp"], "vol": ["AOR Master"]},
    ],
    "dc30_unparallel": [
        {"name": "brilliant", "fixed": {"Channel": 0.0}, "gain": ["Ch1 Volume"], "vol": ["Master"]},
        {"name": "ef86", "fixed": {"Channel": 1.0}, "gain": ["Ch2 Volume"], "vol": ["Master"]},
    ],
    "dsl100": [
        {"name": "classic_clean", "fixed": {"Channel": 0.0, "Classic Mode": 0.0}, "gain": ["Classic Gain"], "vol": ["Classic Vol", "Master 1"]},
        {"name": "classic_crunch", "fixed": {"Channel": 0.0, "Classic Mode": 1.0}, "gain": ["Classic Gain"], "vol": ["Classic Vol", "Master 1"]},
        {"name": "ultra_od1", "fixed": {"Channel": 1.0, "Ultra Mode": 0.0}, "gain": ["Ultra Gain"], "vol": ["Ultra Vol", "Master 1"]},
        {"name": "ultra_od2", "fixed": {"Channel": 1.0, "Ultra Mode": 1.0}, "gain": ["Ultra Gain"], "vol": ["Ultra Vol", "Master 1"]},
    ],
    "dsl15_marsten": [
        {"name": "classic", "fixed": {"Channel": 0.0}, "gain": ["Classic Gain"], "vol": ["Classic Volume"]},
        {"name": "ultra", "fixed": {"Channel": 1.0}, "gain": ["Ultra Gain"], "vol": ["Ultra Volume"]},
    ],
    "dual_rect": [
        {"name": "green_clean", "fixed": {"Channel": 0.0, "Green Mode": 0.0}, "gain": ["Green Gain"], "vol": ["Green Master", "Output"]},
        {"name": "green_pushed", "fixed": {"Channel": 0.0, "Green Mode": 1.0}, "gain": ["Green Gain"], "vol": ["Green Master", "Output"]},
        {"name": "orange_raw", "fixed": {"Channel": 0.5, "Orange Mode": 0.0}, "gain": ["Orange Gain"], "vol": ["Orange Master", "Output"]},
        {"name": "orange_vintage", "fixed": {"Channel": 0.5, "Orange Mode": 0.5}, "gain": ["Orange Gain"], "vol": ["Orange Master", "Output"]},
        {"name": "orange_modern", "fixed": {"Channel": 0.5, "Orange Mode": 1.0}, "gain": ["Orange Gain"], "vol": ["Orange Master", "Output"]},
        {"name": "red_raw", "fixed": {"Channel": 1.0, "Red Mode": 0.0}, "gain": ["Red Gain"], "vol": ["Red Master", "Output"]},
        {"name": "red_vintage", "fixed": {"Channel": 1.0, "Red Mode": 0.5}, "gain": ["Red Gain"], "vol": ["Red Master", "Output"]},
        {"name": "red_modern", "fixed": {"Channel": 1.0, "Red Mode": 1.0}, "gain": ["Red Gain"], "vol": ["Red Master", "Output"]},
    ],
    "engel_fireball": [
        {"name": "clean", "fixed": {"Channel": 0.0}, "gain": ["Clean Gain"], "vol": ["Master"]},
        {"name": "lead", "fixed": {"Channel": 1.0}, "gain": ["Lead Gain"], "vol": ["Lead Volume", "Master"]},
    ],
    "jimmybean_citrus": [
        {"name": "channel_1", "fixed": {"Channel": 0.0}, "gain": ["Sustain"], "vol": ["Volume"]},
        {"name": "channel_2", "fixed": {"Channel": 1.0}, "gain": ["Sustain"], "vol": ["Volume"]},
    ],
    "jvm410_marsten": [
        *[
            {"name": f"{channel}_{mode}", "fixed": {"Channel": ch, "Mode": md}, "gain": ["Gain"], "vol": ["Volume", "Master"]}
            for channel, ch in (("clean", 0.0), ("crunch", 0.34), ("od1", 0.67), ("od2", 1.0))
            for mode, md in (("green", 0.0), ("orange", 0.5), ("red", 1.0))
        ],
    ],
    "mark_ii": [
        {"name": "rhythm", "fixed": {"Lead": 0.0}, "gain": ["Volume 1"], "vol": ["Master 1"]},
        {"name": "lead", "fixed": {"Lead": 1.0}, "gain": ["Lead Drive"], "vol": ["Lead Master"]},
    ],
    "mark_iii": [
        {"name": "rhythm", "fixed": {"Lead": 0.0}, "gain": ["Volume"], "vol": ["Master"]},
        {"name": "lead", "fixed": {"Lead": 1.0}, "gain": ["Lead Drive"], "vol": ["Lead Master"]},
    ],
    "orangerockerverb_rumbleverb": [
        {"name": "clean", "fixed": {"Channel": 0.0}, "gain": ["Clean Volume"], "vol": ["Output"]},
        {"name": "dirty", "fixed": {"Channel": 1.0}, "gain": ["Gain"], "vol": ["Volume", "Output"]},
    ],
    "silverjubilee_marsten": [
        {"name": "lead", "fixed": {"Rhythm Clip": 0.0}, "gain": ["Gain"], "vol": ["Lead Master", "Master"]},
        {"name": "rhythm_clip", "fixed": {"Rhythm Clip": 1.0}, "gain": ["Gain"], "vol": ["Lead Master", "Master"]},
    ],
    "superdrive45": [
        {"name": "rhythm", "fixed": {"Channel": 0.0}, "gain": ["Rhythm"], "vol": ["Master"]},
        {"name": "high_gain", "fixed": {"Channel": 1.0, "Modern": 0.0}, "gain": ["Drive"], "vol": ["Master"]},
        {"name": "high_gain_modern", "fixed": {"Channel": 1.0, "Modern": 1.0}, "gain": ["Drive"], "vol": ["Master"]},
    ],
    "tw22": [
        {"name": "vintage", "fixed": {"Channel": 0.0}, "gain": ["Vint Vol"], "vol": []},
        {"name": "burn", "fixed": {"Channel": 1.0}, "gain": ["Gain 1", "Gain 2"], "vol": ["Burn Vol"]},
    ],
    "vh140_sampleg": [
        {"name": "channel_a", "fixed": {"Channel": 0.0}, "gain": ["A Gain"], "vol": ["A Level"]},
        {"name": "channel_b", "fixed": {"Channel": 1.0}, "gain": ["B Gain"], "vol": ["B Level"]},
    ],
    "vs100_marsten": [
        {"name": "clean", "fixed": {"Channel": 0.0}, "gain": ["Cl Volume"], "vol": []},
        {"name": "od1", "fixed": {"Channel": 0.5}, "gain": ["OD1 Gain"], "vol": ["OD1 Volume"]},
        {"name": "od2", "fixed": {"Channel": 1.0}, "gain": ["OD2 Gain"], "vol": ["OD2 Volume"]},
    ],
}

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


def _compile_and_run(info: dict, gain_idx, vol_idx, fixed: dict[int, float] | None = None) -> dict | None:
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
        fx = ",".join(f"{i}={value:.6f}" for i, value in (fixed or {}).items()) or "-"
        run = subprocess.run([binpath, g, v, str(STEPS), fx], capture_output=True, text=True)
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

    profile_specs = CHANNEL_PROFILES.get(d.name, [])
    if profile_specs:
        idx_of = {name: i for i, name in enumerate(names)}
        profiles = []
        defaults = res["def"]
        for spec in profile_specs:
            missing = [name for name in (*spec["fixed"], *spec["gain"], *spec["vol"])
                       if name not in idx_of]
            if missing:
                print(f"    profile {spec['name']} skipped; missing params: {missing}", file=sys.stderr)
                continue
            fixed_idx = {idx_of[name]: value for name, value in spec["fixed"].items()}
            gain_profile_idx = [idx_of[name] for name in spec["gain"]]
            vol_profile_idx = [idx_of[name] for name in spec["vol"]]
            measured = _compile_and_run(info, gain_profile_idx, vol_profile_idx, fixed_idx)
            if not measured:
                print(f"    profile {spec['name']} measurement failed", file=sys.stderr)
                continue
            profile_names = list(dict.fromkeys([*spec["fixed"], *spec["gain"], *spec["vol"]]))
            profile_default = {name: round(float(spec["fixed"].get(name, defaults[idx_of[name]])), 4)
                               for name in profile_names}
            profiles.append({
                "name": spec["name"],
                "fixed": spec["fixed"],
                "gain_params": spec["gain"],
                "vol_params": spec["vol"],
                "default": profile_default,
                "lufs_default": measured["default"],
                "gain_curve": measured["sweeps"].get("gain", []),
                "vol_curves": {names[int(k)]: curve for k, curve in measured["sweeps"].items()
                               if k != "gain"},
            })
            print(f"    profile={spec['name']:<20} lufs={measured['default']:.2f}")
        if len(profiles) != len(profile_specs):
            print(f"    channel calibration incomplete: {len(profiles)}/{len(profile_specs)} profiles",
                  file=sys.stderr)
            return None
        if profiles:
            selector_names = list(dict.fromkeys(
                name for profile in profiles for name in profile["fixed"]
            ))
            def distance(profile):
                return max((abs(defaults[idx_of[name]] - value)
                            for name, value in profile["fixed"].items()), default=0.0)
            entry["default_profile"] = min(profiles, key=distance)["name"]
            entry["selector_defaults"] = {
                name: round(defaults[idx_of[name]], 4) for name in selector_names
            }
            entry["channel_profiles"] = profiles
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
        "target_lufs": -12.0, "metric": "BS.1770-4 integrated LUFS (mono, left)",
        "excitation": "110-1760 Hz multitone @ RMS 0.10", "steps": STEPS,
        "note": "lufs_* are measured with the amp's compiled kLvl; rig builder "
                "computes a clean trim from these — it does NOT modify amp params.",
    }
    MODEL.write_text(json.dumps(model, indent=1))
    print(f"\nwrote {MODEL} — {ok}/{len(dirs)} amps")
    return 0 if ok == len(dirs) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
