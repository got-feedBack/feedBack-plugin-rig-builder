#!/usr/bin/env python3
"""Offline per-pedal loudness sweep → data/pedal_loudness_model.json.

Companion to measure_amp_loudness.py for the CHAIN-AWARE amp trim: a dirt or
boost pedal in front of an amp raises the signal the amp receives; a saturated
amp absorbs the boost but a clean amp passes it through, so chains with dirt
pedals played several dB louder than the bare amp (measured +8..+13 dB through
the clean Chieftain). To level EVERY chain, routes.py estimates the level at
the amp's input by composing per-pedal measurements (this file) and then
corrects the amp trim with the amp's measured input-response curve.

Per pedal (vst/src/pedals/<dir>/) this measures, with the SAME plucked
multitone at peak -12 dBFS the amp model uses (K-weighted LUFS, no cab):
  - out_db        : output level MINUS the dry reference level, at default
                    params (how hot the pedal runs vs its input, in dB)
  - input_slope   : d(out)/d(in) for a +12 dB hotter input (1.0 = transparent/
                    linear, ~0 = hard clipper whose output level is input-set)
  - level_curves  : out_db vs each Level/Volume-style param (0..1 sweep)
  - drive_curves  : out_db vs each Gain/Drive/Fuzz-style param (0..1 sweep)

Modulation/time pedals with loudness makeup measure ~0 dB and simply
contribute nothing. Pass pedal dir names to (re)measure a subset; omit for all.
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measure_amp_loudness import GUITAR_IR, _prepare_ir   # same reference cab

ROOT = Path(__file__).resolve().parent.parent
PEDALS = ROOT / "vst" / "src" / "pedals"
DPF_SRC = "../DPF/distrho/src/DistrhoPlugin.cpp"
TEMPLATE = (ROOT / "tools" / "amp_lufs_harness.cpp.in").read_text()
MODEL = ROOT / "data" / "pedal_loudness_model.json"
STEPS = 11

_LEVEL_RE = re.compile(r"\b(level|volume|vol|output|out|master|balance)\b", re.I)
_DRIVE_RE = re.compile(r"\b(gain|drive|dist|fuzz|sustain|depth|overdrive|od|boost|comp)\b", re.I)
_EXCLUDE_RE = re.compile(r"sel|select|mode|switch|type|bright|sag|bias|tone|"
                         r"treble|bass|mid|freq|rate|speed|mix|blend|cab|range|"
                         r"attack|release|sens|voice|turbo|channel", re.I)


def _sdk_path() -> str:
    return subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()


def _parse_pedal(d: Path) -> dict | None:
    cpps = [p for p in d.glob("*.cpp")
            if not p.name.startswith("_") and "createPlugin" in p.read_text()]
    if not cpps:
        return None
    plugin_cpp = cpps[0]
    src = plugin_cpp.read_text()
    m = re.search(r"createPlugin\s*\(\s*\)\s*\{\s*return\s+new\s+(\w+)", src)
    if not m:
        return None
    info = d / "DistrhoPluginInfo.h"
    if not info.exists():
        return None
    # The model key must be the BUNDLE stem (what routes.py derives from the
    # piece's vst_path), which comes from the Makefile NAME — NOT the display
    # DISTRHO_PLUGIN_NAME (e.g. bundle FuzzRite.vst3 vs name "Fuzz Rite").
    mn = None
    mk = d / "Makefile"
    if mk.exists():
        mn = re.search(r"^NAME\s*[:+]?=\s*(\S+)", mk.read_text(), re.M)
    if not mn:
        mn = re.search(r'#define\s+DISTRHO_PLUGIN_NAME\s+"([^"]+)"', info.read_text())
    if not mn:
        return None
    hdrs = list(d.glob("*Params.h"))
    if not hdrs:
        return None
    ma = re.search(r"k\w*Names\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", hdrs[0].read_text(), re.S)
    if not ma:
        return None
    names = re.findall(r'"([^"]*)"', ma.group(1))
    return {"dir": d, "plugin_cpp": plugin_cpp.name, "class": m.group(1),
            "bundle_stem": mn.group(1).lower(), "names": names}


def _compile_and_run(info: dict, gain_idx, vol_idx, ir_path: Path | None = None) -> dict | None:
    d: Path = info["dir"]
    probe = TEMPLATE.replace("@PLUGIN_CPP@", info["plugin_cpp"]).replace("@CLASS@", info["class"])
    probe_path = d / "_lufs_probe.cpp"
    probe_path.write_text(probe)
    binpath = None
    try:
        with tempfile.NamedTemporaryFile(suffix="_probe", delete=False) as tf:
            binpath = tf.name
        cmd = ["/usr/bin/clang++", "-isysroot", _sdk_path(), "-std=c++14", "-O2",
               "-I.", "-I..", "-I../DPF/distrho", "-I../DPF/dgl",
               "_lufs_probe.cpp", DPF_SRC, "-o", binpath]
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  COMPILE FAIL:\n{r.stderr[-800:]}", file=sys.stderr)
            return None
        g = ",".join(str(i) for i in gain_idx) or "-"
        v = ",".join(str(i) for i in vol_idx) or "-"
        args = [binpath, g, v, str(STEPS), "-"]
        if ir_path is not None:
            args.append(str(ir_path))
        run = subprocess.run(args, capture_output=True, text=True)
        if run.returncode != 0 or not run.stdout.strip():
            print(f"  RUN FAIL rc={run.returncode}: {run.stderr[-300:]}", file=sys.stderr)
            return None
        return json.loads(run.stdout)
    finally:
        probe_path.unlink(missing_ok=True)
        if binpath:
            Path(binpath).unlink(missing_ok=True)


def _dry_reference_lufs() -> float:
    """LUFS of the excitation itself (measured once through a trivial probe is
    overkill — the tone is deterministic, so compute analytically via a tiny
    C++ run against the bypass? The excitation is deterministic, so its LUFS
    is a constant: computed 2026-07-16 by replicating makeMultitone + the
    reference-cab convolution + the BS.1770 K-filter/gating in Python.

    Pedals are measured THROUGH the reference 4x12 (like the amp model): a
    dirt pedal's mid-heavy spectrum survives the cab better than its raw LUFS
    suggests, so the cab-weighted delta predicts the loudness the chain
    actually gains (the raw-LUFS version under-predicted by ~2 dB)."""
    return -19.52   # plucked multitone -> Marsten_1960A_4x12, K-weighted LUFS


def measure_pedal(d: Path, ir_path: Path | None = None) -> tuple[str, dict] | None:
    info = _parse_pedal(d)
    if not info:
        print(f"[skip] {d.name}: could not parse", file=sys.stderr)
        return None
    names = info["names"]
    drive_idx = [i for i, n in enumerate(names)
                 if _DRIVE_RE.search(n) and not _EXCLUDE_RE.search(n)]
    level_idx = [i for i, n in enumerate(names)
                 if _LEVEL_RE.search(n) and not _EXCLUDE_RE.search(n)
                 and i not in drive_idx]
    print(f"[{d.name}] stem={info['bundle_stem']} "
          f"drive={[names[i] for i in drive_idx]} level={[names[i] for i in level_idx]}")
    res = _compile_and_run(info, drive_idx, level_idx, ir_path)
    if not res:
        return None
    ref = _dry_reference_lufs()
    ic = res.get("input_curve") or []
    # slope from the +12 dB point of the input sweep (vs +0)
    l0 = next((l for o, l in ic if abs(o) < 0.1), res["default"])
    l12 = next((l for o, l in ic if abs(o - 12.0) < 0.1), None)
    slope = round((l12 - l0) / 12.0, 3) if l12 is not None else 1.0
    entry = {
        "out_db": round(res["default"] - ref, 2),
        "input_slope": slope,
        "default": {names[i]: round(res["def"][i], 4)
                    for i in (drive_idx + level_idx)},
        "drive_params": [names[i] for i in drive_idx],
        "level_params": [names[i] for i in level_idx],
        "drive_curves": {"gain": [[v, round(l - ref, 2)] for v, l in res["sweeps"].get("gain", [])]}
                        if res["sweeps"].get("gain") else {},
        "level_curves": {names[int(k)]: [[v, round(l - ref, 2)] for v, l in c]
                         for k, c in res["sweeps"].items() if k != "gain"},
    }
    print(f"    out={entry['out_db']:+.1f} dB  slope={slope}")
    return info["bundle_stem"], entry


def main(argv: list[str]) -> int:
    dirs = sorted(p for p in PEDALS.iterdir() if p.is_dir() and p.name != "DPF")
    if argv:
        want = set(argv)
        dirs = [d for d in dirs if d.name in want]
    model = {}
    if MODEL.exists() and argv:
        model = json.loads(MODEL.read_text())
    ok = 0
    ir_path = _prepare_ir(GUITAR_IR)
    for d in dirs:
        out = measure_pedal(d, ir_path)
        if out:
            model[out[0]] = out[1]
            ok += 1
    model["_meta"] = {
        "reference_lufs": _dry_reference_lufs(),
        "metric": "K-weighted LUFS delta vs the dry excitation, both through "
                  "the Marsten_1960A_4x12 reference cab",
        "excitation": "plucked 110-1760 Hz multitone, peak -12 dBFS "
                      "(same as amp_loudness_model)",
        "note": "out_db = how hot the pedal runs vs its input; input_slope = "
                "d(out)/d(in) at +12 dB (1=linear, ~0=hard clipper). Used by "
                "routes.py to estimate the level a pre-amp pedal chain feeds "
                "the amp and correct the amp trim via its input_curve.",
    }
    MODEL.write_text(json.dumps(model, indent=1))
    print(f"\nwrote {MODEL} — {ok}/{len(dirs)} pedals")
    return 0 if ok == len(dirs) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
