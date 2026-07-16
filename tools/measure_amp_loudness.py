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

# ── Reference cab IRs ─────────────────────────────────────────────────────────
# The model must capture the loudness of what the player HEARS: in the app the
# amp's internal Cab Sim is muted and a real cab IR follows. Measured amp-only,
# high-gain amps (energy largely in harmonics above the cab's ~4-5 kHz rolloff)
# read equal-LUFS but land several dB apart after the cab (10.8 dB spread over
# 9 amps) — and the engine's final leveler then squashes whoever came out hot,
# which players hear as "high-gain amps are quieter". Every measurement is
# therefore taken with "Cab Sim"=0 and the output convolved with a reference IR
# (energy-normalized, truncated to 1024 taps): the classic 4x12 for guitar
# amps, an 8x10 for bass amps.
GUITAR_IR = ROOT / "assets" / "cab_irs" / "Marsten_1960A_4x12" / "dyn_cone.wav"
BASS_IR = ROOT / "assets" / "cab_irs" / "Citrus_OBC810" / "dyn_cone.wav"
IR_TAPS = 1024
_CABSIM_RE = re.compile(r"^cab\s*sim$", re.I)

# Per-amp material-calibration offset (dB), ADDED to every measured LUFS value.
# For amps with heavy program compression the pluck excitation under-predicts
# their in-game loudness: the Eden WT family's opto comp + Output-Limit holds
# ~5.5:1 effective over real bass (input -8 dB -> output only -1.4 dB), so its
# output loudness barely tracks the input and a static trim computed from the
# excitation lands ~3.5 dB hot vs the other bass amps on real material
# (verified against the Garden bass DI through the OBC810 reference IR).
# Keyed by bundle stem; survives regens because it lives here, not in the JSON.
MATERIAL_OFFSET_DB = {
    "aidengt300": 3.5,
    "aidengt550": 3.5,
    "aidengt880": 3.5,
}


def _apply_material_offset(stem: str, entry: dict) -> None:
    off = MATERIAL_OFFSET_DB.get(stem, 0.0)
    if not off:
        return
    def shift_curve(curve):
        return [[v, round(l + off, 4)] for v, l in curve]
    entry["lufs_default"] = round(entry["lufs_default"] + off, 4)
    entry["gain_curve"] = shift_curve(entry.get("gain_curve") or [])
    entry["vol_curves"] = {k: shift_curve(c) for k, c in (entry.get("vol_curves") or {}).items()}
    for prof in entry.get("channel_profiles") or []:
        prof["lufs_default"] = round(prof["lufs_default"] + off, 4)
        prof["gain_curve"] = shift_curve(prof.get("gain_curve") or [])
        prof["vol_curves"] = {k: shift_curve(c) for k, c in (prof.get("vol_curves") or {}).items()}
    entry["material_offset_db"] = off


def _read_wav_mono(path: Path) -> list[float]:
    import struct as _st
    d = path.read_bytes()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", f"not a WAV: {path}"
    i = 12
    fmt = ch = sr = bits = None
    data = b""
    while i + 8 <= len(d):
        cid = d[i:i + 4]
        sz = _st.unpack("<I", d[i + 4:i + 8])[0]
        body = d[i + 8:i + 8 + sz]
        if cid == b"fmt ":
            fmt, ch, sr = _st.unpack("<HHI", body[:8])
            bits = _st.unpack("<H", body[14:16])[0]
        elif cid == b"data":
            data = body
        i += 8 + sz + (sz & 1)
    if fmt == 3 and bits == 32:
        n = len(data) // 4
        vals = list(_st.unpack(f"<{n}f", data))
    elif bits == 16:
        n = len(data) // 2
        vals = [v / 32768.0 for v in _st.unpack(f"<{n}h", data)]
    elif bits == 24:
        vals = []
        for j in range(0, len(data) - 2, 3):
            v = data[j] | (data[j + 1] << 8) | (data[j + 2] << 16)
            if v >= 1 << 23:
                v -= 1 << 24
            vals.append(v / float(1 << 23))
    else:
        n = len(data) // 4
        vals = [v / float(1 << 31) for v in _st.unpack(f"<{n}i", data)]
    if ch == 2:
        vals = vals[0::2]
    return vals


def _prepare_ir(path: Path) -> Path:
    """Reference IR → raw float32 for the harness: mono, truncated to IR_TAPS
    with a short fade-out, ENERGY-normalized (sum of squares = 1, i.e. unity
    broadband power gain so the absolute LUFS target stays comparable)."""
    import struct as _st
    ir = _read_wav_mono(path)[:IR_TAPS]
    fade = 128
    for j in range(fade):
        ir[-fade + j] *= 1.0 - (j + 1) / float(fade)
    e = sum(v * v for v in ir)
    scale = (1.0 / e) ** 0.5 if e > 0 else 1.0
    ir = [v * scale for v in ir]
    out = Path(tempfile.gettempdir()) / f"rb_ref_ir_{path.parent.name}.f32"
    out.write_bytes(_st.pack(f"<{len(ir)}f", *ir))
    return out

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


def _compile_and_run(info: dict, gain_idx, vol_idx, fixed: dict[int, float] | None = None,
                     ir_path: Path | None = None, bass_tone: bool = False) -> dict | None:
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
        args = [binpath, g, v, str(STEPS), fx]
        if ir_path is not None:
            args.append(str(ir_path))
            if bass_tone:
                args.append("bass")
        run = subprocess.run(args, capture_output=True, text=True)
        if run.returncode != 0 or not run.stdout.strip():
            print(f"  RUN FAIL rc={run.returncode}: {run.stderr[-500:]}", file=sys.stderr)
            return None
        return json.loads(run.stdout)
    finally:
        probe_path.unlink(missing_ok=True)
        Path(binpath).unlink(missing_ok=True)


def measure_amp(d: Path, gear_idx: dict, knob_map: dict,
                guitar_ir: Path | None = None, bass_ir: Path | None = None) -> tuple[str, dict] | None:
    info = _parse_amp(d)
    if not info:
        print(f"[skip] {d.name}: could not parse", file=sys.stderr)
        return None
    stem = info["bundle_stem"]
    gears = gear_idx.get(stem, [])
    gain_names = _gain_param_names(knob_map, gears, stem)
    gain_idx, vol_idx = _choose_params(info, gain_names)
    names = info["names"]
    # Measure through the reference cab (bass amps → 8x10, everything else →
    # 4x12) with the internal Cab Sim muted — exactly what the app plays when a
    # real cab IR is in the chain.
    is_bass = any(g.startswith("Bass_Amp") for g in gears)
    ir_path = (bass_ir if is_bass else guitar_ir)
    base_fixed: dict[int, float] = {}
    for i, n in enumerate(names):
        if _CABSIM_RE.search(n):
            base_fixed[i] = 0.0
            break
    print(f"[{d.name}] stem={stem} class={info['class']} "
          f"gain={[names[i] for i in gain_idx]} vol={[names[i] for i in vol_idx]} "
          f"cab={'8x10' if is_bass else '4x12'}{' cabsim=0' if base_fixed else ''}")
    res = _compile_and_run(info, gain_idx, vol_idx, base_fixed or None, ir_path, is_bass)
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
            fixed_idx.update(base_fixed)     # Cab Sim stays muted per profile too
            gain_profile_idx = [idx_of[name] for name in spec["gain"]]
            vol_profile_idx = [idx_of[name] for name in spec["vol"]]
            measured = _compile_and_run(info, gain_profile_idx, vol_profile_idx, fixed_idx, ir_path, is_bass)
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
    _apply_material_offset(stem, entry)
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

    guitar_ir = _prepare_ir(GUITAR_IR)
    bass_ir = _prepare_ir(BASS_IR)

    ok = 0
    for d in dirs:
        out = measure_amp(d, gear_idx, knob_map, guitar_ir, bass_ir)
        if out:
            model[out[0]] = out[1]
            ok += 1
    model["_meta"] = {
        "target_lufs": -12.0, "metric": "BS.1770-4 integrated LUFS (mono, left)",
        "excitation": "plucked multitone, peak -12 dBFS (guitar 110-1760 Hz, bass 55-880 Hz; the "
                      "calibrated guitar reference level; 350 ms plucks, "
                      "~180 ms decay)", "steps": STEPS,
        "chain": "Cab Sim=0 + reference cab IR (guitar: Marsten_1960A_4x12, "
                 "bass: Citrus_OBC810; energy-normalized, 1024 taps) — the "
                 "loudness of what the player hears, not the raw amp output",
        "note": "lufs_* are measured with the amp's compiled kLvl; rig builder "
                "computes a clean trim from these — it does NOT modify amp params.",
    }
    MODEL.write_text(json.dumps(model, indent=1))
    print(f"\nwrote {MODEL} — {ok}/{len(dirs)} amps")
    return 0 if ok == len(dirs) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
