#!/usr/bin/env python3
"""Batch-render amp DSP through a DI and flag likely audio bugs.

This is intentionally metric-based: it compiles one tiny DPF host per amp,
renders a few representative parameter cases, then reports silence, non-finite
samples, clipping, large sample jumps, and dropout windows.
"""
from __future__ import annotations

import argparse
import csv
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import render_amp_wav


ROOT = Path(__file__).resolve().parent.parent
AMPS = ROOT / "vst" / "src" / "amps"

# Bass amps are excluded by default because the user is using a guitar DI here,
# and bass amp work is owned separately. Pass --include-bass to audit them too.
BASS_AMP_DIRS = {
    "bt600b_tmax",
    "cs240b_tracer",
    "cs300b_rumble",
    "cs350b_dbs",
    "cs75b_v4b",
    "edene300_wt300",
    "edenwt550_wt550",
    "edenwt800_wt880",
    "fk800rb",
    "ht100b_lovolt",
    "ht300b_redhead",
    "ht400b_silla",
    "lt25b_dustup",
    "lt85b_electric",
    "orangead200b_citrus",
    "sampleg_sbtcl",
    "sansamp_sinamp",
    "sharke_hb3500",
    "sharke_hb5000",
}


@dataclass
class Case:
    name: str
    overrides: dict[str, float]


@dataclass
class RenderResult:
    amp: str
    case: str
    out_wav: Path
    overrides: dict[str, float]
    ok: bool
    stderr_tail: str
    rms_db: float = -240.0
    peak_db: float = -240.0
    clip_frac: float = 0.0
    near_zero_frac: float = 0.0
    max_jump: float = 0.0
    big_jump_025: int = 0
    big_jump_05: int = 0
    dropout_windows: int = 0
    active_windows: int = 0
    finite: bool = True
    flags: str = ""


def db(v: float) -> float:
    return 20.0 * math.log10(v) if v > 0.0 and math.isfinite(v) else -240.0


def clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))


def read_wav_mono(path: Path) -> tuple[int, list[float]]:
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path} is not RIFF/WAVE")

    pos = 12
    fmt = channels = sample_rate = bits = None
    pcm = None
    while pos + 8 <= len(data):
        chunk_id = data[pos : pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        chunk = data[pos : pos + size]
        if chunk_id == b"fmt ":
            fmt, channels, sample_rate, _, _, bits = struct.unpack_from("<HHIIHH", chunk, 0)
        elif chunk_id == b"data":
            pcm = chunk
        pos += size + (size & 1)

    if fmt is None or channels is None or bits is None or pcm is None:
        raise ValueError(f"{path} is missing fmt/data")

    if fmt == 3 and bits == 32:
        vals = struct.unpack("<" + "f" * (len(pcm) // 4), pcm)
    elif fmt == 1 and bits == 16:
        ints = struct.unpack("<" + "h" * (len(pcm) // 2), pcm)
        vals = [x / 32768.0 for x in ints]
    elif fmt == 1 and bits == 24:
        vals = []
        for i in range(0, len(pcm), 3):
            b = pcm[i : i + 3]
            n = int.from_bytes(b + (b"\xff" if b[2] & 0x80 else b"\x00"), "little", signed=True)
            vals.append(n / 8388608.0)
    else:
        raise ValueError(f"{path} unsupported WAV fmt={fmt} bits={bits}")

    mono = []
    for i in range(0, len(vals), channels):
        mono.append(sum(vals[i : i + channels]) / channels)
    return int(sample_rate), mono


def metrics(in_mono: list[float], out_mono: list[float], sample_rate: int) -> dict[str, float | int | bool]:
    finite = all(math.isfinite(x) for x in out_mono)
    if not finite:
        return {
            "rms_db": -240.0,
            "peak_db": -240.0,
            "clip_frac": 1.0,
            "near_zero_frac": 1.0,
            "max_jump": float("inf"),
            "big_jump_025": len(out_mono),
            "big_jump_05": len(out_mono),
            "active_windows": 0,
            "dropout_windows": len(out_mono),
            "finite": False,
        }

    peak = max((abs(x) for x in out_mono), default=0.0)
    rms = math.sqrt(sum(x * x for x in out_mono) / len(out_mono)) if out_mono else 0.0
    diffs = [abs(out_mono[i] - out_mono[i - 1]) for i in range(1, len(out_mono))]
    near_zero = sum(abs(x) < 1.0e-6 for x in out_mono) / len(out_mono) if out_mono else 1.0
    clip_frac = sum(abs(x) >= 0.99 for x in out_mono) / len(out_mono) if out_mono else 0.0

    win = max(1, int(sample_rate * 0.050))
    active = 0
    dropouts = 0
    for pos in range(0, min(len(in_mono), len(out_mono)) - win + 1, win):
        ir = math.sqrt(sum(x * x for x in in_mono[pos : pos + win]) / win)
        orms = math.sqrt(sum(x * x for x in out_mono[pos : pos + win]) / win)
        if db(ir) > -55.0:
            active += 1
            ratio_db = db(orms) - db(ir)
            if db(orms) < -70.0 or ratio_db < -45.0:
                dropouts += 1

    return {
        "rms_db": db(rms),
        "peak_db": db(peak),
        "clip_frac": clip_frac,
        "near_zero_frac": near_zero,
        "max_jump": max(diffs, default=0.0),
        "big_jump_025": sum(d > 0.25 for d in diffs),
        "big_jump_05": sum(d > 0.50 for d in diffs),
        "active_windows": active,
        "dropout_windows": dropouts,
        "finite": finite,
    }


def classify(result: RenderResult) -> str:
    flags: list[str] = []
    if not result.ok:
        flags.append("render_failed")
    if not result.finite:
        flags.append("non_finite")
    if result.rms_db < -55.0:
        flags.append("silent")
    if result.peak_db > -0.15 or result.clip_frac > 0.001:
        flags.append("clipping")
    if result.big_jump_05 > 0:
        flags.append("hard_jumps")
    elif result.big_jump_025 > 500:
        flags.append("many_jumps")
    if result.active_windows and result.dropout_windows / result.active_windows > 0.20:
        flags.append("dropouts")
    return ",".join(flags) if flags else "ok"


def is_output_control(name: str, has_master_or_output: bool = False) -> bool:
    n = name.lower()
    if any(k in n for k in ("master", "output", "level")):
        return True
    if n in {"volume", "vol"}:
        return not has_master_or_output
    return False


def is_drive_control(name: str, has_master_or_output: bool = False) -> bool:
    n = name.lower()
    if any(k in n for k in ("gain", "drive", "preamp", "loudness", "sustain")):
        return True
    if "vol" in n or "volume" in n:
        return not is_output_control(name, has_master_or_output)
    return False


def make_cases(names: list[str]) -> list[Case]:
    lower = [n.lower() for n in names]
    has_master_or_output = any(any(k in n for k in ("master", "output", "level")) for n in lower)
    cases = [Case("default", {})]

    clean: dict[str, float] = {}
    hot: dict[str, float] = {}
    for name in names:
        n = name.lower()
        if "cab sim" in n:
            clean[name] = 1.0
            hot[name] = 1.0
        elif is_output_control(name, has_master_or_output):
            clean[name] = 0.70
            hot[name] = 0.70
        elif is_drive_control(name, has_master_or_output):
            clean[name] = 0.24
            hot[name] = 1.0

    # Put common channel-select amps on their driven channel for the hot pass.
    for name in names:
        n = name.lower()
        if n == "channel":
            hot[name] = 1.0
        elif n in {"mode", "ultra mode", "red mode", "orange mode"}:
            hot[name] = 1.0
        elif "lead" == n or n == "lead":
            hot[name] = 1.0

    if clean:
        cases.append(Case("clean_low", clean))
    if hot:
        cases.append(Case("hot", hot))

    # Cab/off check catches broken fallback cab filters without judging amp-only tone.
    if any("cab sim" == n for n in lower):
        cases.append(Case("default_cab_off", {names[lower.index("cab sim")]: 0.0}))

    return cases


def compile_probe(amp_dir: Path, info: dict, build_dir: Path) -> Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    probe = render_amp_wav.HARNESS.replace("@PLUGIN_CPP@", info["plugin_cpp"]).replace("@CLASS@", info["class"])
    probe_path = amp_dir / "_render_probe.cpp"
    probe_path.write_text(probe)
    bin_path = build_dir / f"{amp_dir.name}_render_probe"
    try:
        cmd = [
            "/usr/bin/clang++",
            "-isysroot",
            render_amp_wav.sdk_path(),
            "-std=c++14",
            "-O2",
            "-I.",
            "-I..",
            "-I../DPF/distrho",
            "-I../DPF/dgl",
            "_render_probe.cpp",
            render_amp_wav.DPF_SRC,
            "-o",
            str(bin_path),
        ]
        r = subprocess.run(cmd, cwd=amp_dir, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(r.stderr[-4000:])
        return bin_path
    finally:
        probe_path.unlink(missing_ok=True)


def run_case(
    amp: str,
    info: dict,
    bin_path: Path,
    case: Case,
    in_wav: Path,
    in_sr: int,
    in_mono: list[float],
    out_dir: Path,
) -> RenderResult:
    out_wav = out_dir / f"{amp}__{case.name}.wav"
    idx = {n.lower(): i for i, n in enumerate(info["names"])}
    args = []
    for name, value in case.overrides.items():
        if name.lower() in idx:
            args.append(f"{idx[name.lower()]}={clamp01(value):.8g}")

    run = subprocess.run(
        [str(bin_path), str(in_wav), str(out_wav), *args],
        capture_output=True,
        text=True,
    )
    result = RenderResult(
        amp=amp,
        case=case.name,
        out_wav=out_wav,
        overrides=case.overrides,
        ok=run.returncode == 0,
        stderr_tail=(run.stderr or "")[-1000:].strip(),
    )
    if result.ok and out_wav.exists():
        sr, out_mono = read_wav_mono(out_wav)
        m = metrics(in_mono if sr == in_sr else in_mono, out_mono, sr)
        for key, value in m.items():
            setattr(result, key, value)
    result.flags = classify(result)
    return result


def amp_dirs(include_bass: bool, requested: list[str]) -> list[Path]:
    if requested:
        dirs = [AMPS / a for a in requested]
    else:
        dirs = sorted(p for p in AMPS.iterdir() if p.is_dir() and (p / "Makefile").exists())
    out = []
    for d in dirs:
        if d.name == "DPF":
            continue
        if not include_bass and d.name in BASS_AMP_DIRS:
            continue
        out.append(d)
    return out


def write_reports(results: list[RenderResult], out_dir: Path, include_bass: bool) -> None:
    csv_path = out_dir / "amp_dsp_audit.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "amp",
                "case",
                "flags",
                "rms_db",
                "peak_db",
                "clip_frac",
                "near_zero_frac",
                "max_jump",
                "big_jump_025",
                "big_jump_05",
                "dropout_windows",
                "active_windows",
                "overrides",
                "out_wav",
            ],
        )
        writer.writeheader()
        for r in results:
            writer.writerow(
                {
                    "amp": r.amp,
                    "case": r.case,
                    "flags": r.flags,
                    "rms_db": f"{r.rms_db:.3f}",
                    "peak_db": f"{r.peak_db:.3f}",
                    "clip_frac": f"{r.clip_frac:.8f}",
                    "near_zero_frac": f"{r.near_zero_frac:.6f}",
                    "max_jump": f"{r.max_jump:.6f}",
                    "big_jump_025": r.big_jump_025,
                    "big_jump_05": r.big_jump_05,
                    "dropout_windows": r.dropout_windows,
                    "active_windows": r.active_windows,
                    "overrides": "; ".join(f"{k}={v:g}" for k, v in sorted(r.overrides.items())),
                    "out_wav": str(r.out_wav),
                }
            )

    problems = [r for r in results if r.flags != "ok"]
    md = out_dir / "report.md"
    lines = [
        "# Amp DSP Audit",
        "",
        f"- Include bass amps: `{include_bass}`",
        f"- Total renders: `{len(results)}`",
        f"- Problem renders: `{len(problems)}`",
        f"- CSV: `{csv_path.name}`",
        "",
        "## Problems",
        "",
    ]
    if not problems:
        lines.append("No metric failures.")
    else:
        lines.append("| Amp | Case | Flags | RMS dBFS | Peak dBFS | Max Jump | >0.25 | >0.5 | Dropouts |")
        lines.append("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |")
        for r in sorted(problems, key=lambda x: (x.amp, x.case)):
            lines.append(
                f"| {r.amp} | {r.case} | {r.flags} | {r.rms_db:.2f} | {r.peak_db:.2f} | "
                f"{r.max_jump:.3f} | {r.big_jump_025} | {r.big_jump_05} | "
                f"{r.dropout_windows}/{r.active_windows} |"
            )
    lines.extend(
        [
            "",
            "## All Renders",
            "",
            "| Amp | Case | Flags | RMS dBFS | Peak dBFS | Max Jump |",
            "| --- | --- | --- | ---: | ---: | ---: |",
        ]
    )
    for r in sorted(results, key=lambda x: (x.amp, x.case)):
        lines.append(f"| {r.amp} | {r.case} | {r.flags} | {r.rms_db:.2f} | {r.peak_db:.2f} | {r.max_jump:.3f} |")
    md.write_text("\n".join(lines) + "\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("di_wav", type=Path)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "test logic" / "amp_dsp_audit")
    parser.add_argument("--include-bass", action="store_true")
    parser.add_argument("--amps", nargs="*", default=[])
    args = parser.parse_args(argv)

    di_wav = args.di_wav.resolve()
    run_dir = args.out_dir / "latest"
    wav_dir = run_dir / "renders"
    build_dir = Path(tempfile.mkdtemp(prefix="amp-dsp-audit-", dir="/private/tmp"))
    wav_dir.mkdir(parents=True, exist_ok=True)

    in_sr, in_mono = read_wav_mono(di_wav)
    results: list[RenderResult] = []
    for amp_dir in amp_dirs(args.include_bass, args.amps):
        info = render_amp_wav.parse_amp(amp_dir)
        if not info:
            print(f"SKIP {amp_dir.name}: could not parse", flush=True)
            continue
        print(f"AUDIT {amp_dir.name} ({len(info['names'])} params)", flush=True)
        try:
            bin_path = compile_probe(amp_dir, info, build_dir)
        except Exception as exc:
            failed = RenderResult(
                amp=amp_dir.name,
                case="compile",
                out_wav=wav_dir / f"{amp_dir.name}__compile.wav",
                overrides={},
                ok=False,
                stderr_tail=str(exc)[-1000:],
                flags="compile_failed",
            )
            results.append(failed)
            print(f"  compile_failed: {exc}", flush=True)
            continue

        for case in make_cases(info["names"]):
            r = run_case(amp_dir.name, info, bin_path, case, di_wav, in_sr, in_mono, wav_dir)
            results.append(r)
            print(
                f"  {case.name:15s} {r.flags:24s} "
                f"rms={r.rms_db:7.2f} peak={r.peak_db:7.2f} "
                f"jump={r.max_jump:.3f} >.5={r.big_jump_05}",
                flush=True,
            )

    write_reports(results, run_dir, args.include_bass)
    print(f"REPORT {run_dir / 'report.md'}", flush=True)
    print(f"CSV {run_dir / 'amp_dsp_audit.csv'}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
