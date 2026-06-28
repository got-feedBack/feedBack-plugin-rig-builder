#!/usr/bin/env python3
"""Offline amp-core calibration harness.

For now this is intentionally focused on cores that can be included directly
without spinning up DPF. It reports:
  - stability at 48/96/192 kHz
  - gain sweep RMS/peak/crest
  - approximate 1 kHz THD (2nd..5th harmonics)
  - a few spectral spot magnitudes for quick voicing regressions
"""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
AMPS = ROOT / "vst" / "src" / "amps"

AMPSPECS = {
    "en30": {
        "include": AMPS / "en30" / "BoxDC30Core.h",
        "type": "boxdc30::BoxDC30Core",
        "defaults": """
    c.setInput(0.50f);
    c.setNormalVol(0.50f);
    c.setTreble(0.50f);
    c.setBass(0.50f);
    c.setBright(0.50f);
    c.setCut(0.50f);
    c.setMaster(0.50f);
    c.setRevTone(0.50f);
    c.setRevLevel(0.00f);
    c.setSpeed(0.50f);
    c.setDepth(0.00f);
""",
        "gain_setter": "c.setTBVol(gain);",
        "run_scale": "0.891f",
    },
    "plexi": {
        "include": AMPS / "plexi" / "PlexiCore.h",
        "type": "plexi::PlexiCore",
        "defaults": """
    c.setParam(kPresence, 0.50f);
    c.setParam(kBass, 0.50f);
    c.setParam(kMiddle, 0.55f);
    c.setParam(kTreble, 0.62f);
    c.setParam(kLoudness2, 0.00f);
    c.setParam(kInput, 0.50f);
    c.setParam(kCabSim, 1.00f);
""",
        "gain_setter": "c.setParam(kLoudness1, gain);",
        "run_scale": "0.280f",
    },
    "sampleg_sbtcl": {
        "include": AMPS / "sampleg_sbtcl" / "SvtCore.h",
        "type": "svtcl::SvtCore",
        "input_scale": 1.0,     # SvtCore's inScale already maps audio->grid volts
        "makeup": 0.45,         # matches kSvtMakeup in SvtPlugin.cpp
        "defaults": """
    c.setBass(0.50f);
    c.setMidrange(0.50f);
    c.setFreq(0.50f);
    c.setTreble(0.50f);
    c.setMaster(0.50f);
    c.setPad(false);
    c.setUltraLo(false);
    c.setUltraHi(false);
""",
        "gain_setter": "c.setGain(gain);",
    },
    "fk800rb": {
        "include": AMPS / "fk800rb" / "Fk800Core.h",
        "type": "fk800gk::Fk800Core",
        "input_scale": 1.0,
        "makeup": 0.9183,      # matches kFkMakeup in Fk800Plugin.cpp
        "defaults": "",
        # GK 800RB is mono via setParams(); sweep "gain" = the Volume knob,
        # tone flat, masters ~unity, all voicing/boost switches off.
        "gain_setter": "c.setParams(gain, 0.5f,0.5f,0.5f,0.5f, 0.5f, 0.5f, 0.7f, 0.7f, false,false,false,false, false,false);",
    },
}


CPP_TEMPLATE = r'''
#include "@INCLUDE@"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using CoreT = @CORE_TYPE@;
static constexpr double kPi = 3.14159265358979323846;

static inline float rbAmpLvl(float x) {
    const float t = 0.90f, c = 0.99f, a = x < 0.0f ? -x : x;
    if (a <= t) return x;
    return (x < 0.0f ? -1.0f : 1.0f) * (t + (c - t) * std::tanh((a - t) / (c - t)));
}

static void setup(CoreT& c, float gain, float sr) {
    c.setSampleRate(sr);
@DEFAULTS@
    @GAIN_SETTER@
}

static inline float run(CoreT& c, float x) {
    return rbAmpLvl(@RUN_SCALE@ * c.process(@INPUT_SCALE@ * x));
}

static std::vector<float> multitone(float sr, float seconds) {
    const int n = (int)(sr * seconds);
    const double freqs[] = {110.0, 146.83, 196.0, 261.63, 329.63, 440.0, 587.33, 783.99, 1046.5, 1396.9, 1760.0};
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (double f : freqs) s += std::sin(2.0 * kPi * f * (double)i / sr);
        x[i] = (float)(0.035 * s / std::sqrt((double)(sizeof(freqs) / sizeof(freqs[0]))));
    }
    return x;
}

static std::vector<float> sine(float sr, float hz, float seconds, float amp) {
    const int n = (int)(sr * seconds);
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = amp * (float)std::sin(2.0 * kPi * hz * (double)i / sr);
    return x;
}

struct Stats { double rms = 0.0, peak = 0.0, crest = 0.0; };

static Stats stats(const std::vector<float>& y, int start = 0) {
    double ss = 0.0, pk = 0.0;
    int n = 0;
    for (int i = start; i < (int)y.size(); ++i) {
        const double v = y[i];
        ss += v * v;
        pk = std::max(pk, std::fabs(v));
        ++n;
    }
    Stats s;
    s.rms = std::sqrt(ss / std::max(1, n));
    s.peak = pk;
    s.crest = 20.0 * std::log10((pk + 1.0e-15) / (s.rms + 1.0e-15));
    return s;
}

static double goertzel(const std::vector<float>& y, float sr, float hz, int start) {
    const int n = (int)y.size() - start;
    if (n <= 0) return 0.0;
    const double w = 2.0 * kPi * hz / sr;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = start; i < (int)y.size(); ++i) {
        s0 = y[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, p)) / n;
}

static std::vector<float> render(float sr, float gain, const std::vector<float>& input) {
    CoreT c;
    setup(c, gain, sr);
    std::vector<float> y(input.size());
    for (int i = 0; i < (int)input.size(); ++i) {
        y[i] = run(c, input[i]);
        if (!std::isfinite(y[i])) y[i] = std::numeric_limits<float>::quiet_NaN();
    }
    return y;
}

int main() {
    const float stabilityRates[] = {48000.0f, 96000.0f, 192000.0f};
    std::printf("stability\n");
    for (float sr : stabilityRates) {
        CoreT c;
        setup(c, 1.0f, sr);
        bool bad = false;
        double pk = 0.0;
        const int n = (int)(sr * 0.75f);
        for (int i = 0; i < n; ++i) {
            const float x = 1.5f * (float)std::sin(2.0 * kPi * 150.0 * i / sr) *
                            (0.5f + 0.5f * (float)std::sin(2.0 * kPi * 0.7 * i / sr));
            const float y = run(c, x);
            if (!std::isfinite(y)) { bad = true; break; }
            pk = std::max(pk, (double)std::fabs(y));
        }
        std::printf("  sr=%6.0f nan=%s peak=%.4f\n", sr, bad ? "YES" : "no", pk);
    }

    const float sr = 48000.0f;
    const int discard = (int)(0.25f * sr);
    const float gains[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    const auto mt = multitone(sr, 2.0f);
    const auto oneK = sine(sr, 1000.0f, 1.25f, 0.040f);

    std::printf("\ngain_sweep\n");
    std::printf("  gain   rms_db   peak   crest   thd_db   f120    f800    f3k     f6k\n");
    for (float g : gains) {
        const auto y = render(sr, g, mt);
        const Stats st = stats(y, discard);
        const auto ys = render(sr, g, oneK);
        const double f1 = goertzel(ys, sr, 1000.0f, discard);
        double h2 = 0.0;
        for (int h = 2; h <= 5; ++h) {
            const double a = goertzel(ys, sr, 1000.0f * h, discard);
            h2 += a * a;
        }
        const double thdDb = 20.0 * std::log10((std::sqrt(h2) + 1.0e-15) / (f1 + 1.0e-15));
        const double rmsDb = 20.0 * std::log10(st.rms + 1.0e-15);
        std::printf("  %.2f  %7.2f  %.4f  %6.2f  %7.2f  %6.1f  %6.1f  %6.1f  %6.1f\n",
                    g, rmsDb, st.peak, st.crest, thdDb,
                    20.0 * std::log10(goertzel(y, sr, 120.0f, discard) + 1.0e-15),
                    20.0 * std::log10(goertzel(y, sr, 800.0f, discard) + 1.0e-15),
                    20.0 * std::log10(goertzel(y, sr, 3000.0f, discard) + 1.0e-15),
                    20.0 * std::log10(goertzel(y, sr, 6000.0f, discard) + 1.0e-15));
    }
    return 0;
}
'''


def build_cpp(spec: dict[str, object]) -> str:
    def float_lit(value: object) -> str:
        text = str(value)
        return text if text.endswith("f") else f"{text}f"

    return (
        CPP_TEMPLATE
        .replace("@INCLUDE@", str(spec["include"]))
        .replace("@CORE_TYPE@", str(spec["type"]))
        .replace("@DEFAULTS@", str(spec["defaults"]).rstrip())
        .replace("@GAIN_SETTER@", str(spec["gain_setter"]))
        .replace("@INPUT_SCALE@", float_lit(spec.get("input_scale", 3.2)))
        .replace("@RUN_SCALE@", float_lit(spec.get("run_scale", spec.get("makeup", 0.891))))
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("amp", choices=sorted(AMPSPECS), help="amp core to calibrate")
    args = ap.parse_args()
    spec = AMPSPECS[args.amp]

    with tempfile.TemporaryDirectory(prefix="rb_amp_cal_") as td:
        cpp = Path(td) / f"{args.amp}_cal.cpp"
        binp = Path(td) / f"{args.amp}_cal"
        cpp.write_text(build_cpp(spec))
        cmd = ["c++", "-O2", "-std=c++17", str(cpp), "-o", str(binp)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr)
            return r.returncode
        run = subprocess.run([str(binp)], capture_output=True, text=True)
        if run.stdout:
            print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="")
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
