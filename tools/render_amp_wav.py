#!/usr/bin/env python3
"""Render a WAV through one bundled DPF amp or pedal plugin source.

Usage:
  python3 tools/render_amp_wav.py <effect_dir> <input.wav> <output.wav> [Param=Value ...]

The effect_dir defaults to a directory under vst/src/amps. Prefix pedal
directories with ``pedals/``, for example:
  plexi, tw40, pedals/phaser_363, pedals/dynamics_compression

This intentionally compiles a tiny local host around the plugin source instead
of using the desktop app, so DSP failures can be reproduced without preset/UI
routing in the way.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
AMPS = ROOT / "vst" / "src" / "amps"
PEDALS = ROOT / "vst" / "src" / "pedals"
DPF_SRC = "../DPF/distrho/src/DistrhoPlugin.cpp"


HARNESS = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "@PLUGIN_CPP@"

START_NAMESPACE_DISTRHO
struct Probe : public @CLASS@ {
    void setP(uint32_t i, float v) { setParameterValue(i, v); }
    float getP(uint32_t i) const { return getParameterValue(i); }
    void sr(double s) { sampleRateChanged(s); }
    void act() { activate(); }
    void go(const float** in, float** out, uint32_t n) { run(in, out, n); }
};
END_NAMESPACE_DISTRHO

using DISTRHO::Probe;

static uint16_t rd16(const unsigned char* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void wr16(std::ofstream& f, uint16_t v) {
    unsigned char b[2] = { (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255) };
    f.write((const char*)b, 2);
}
static void wr32(std::ofstream& f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v & 255), (unsigned char)((v >> 8) & 255), (unsigned char)((v >> 16) & 255), (unsigned char)((v >> 24) & 255) };
    f.write((const char*)b, 4);
}

struct Audio {
    int sr = 48000;
    int ch = 1;
    std::vector<float> l;
    std::vector<float> r;
};

static Audio readWav(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open input wav");

    unsigned char hdr[12];
    f.read((char*)hdr, 12);
    if (f.gcount() != 12 || std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0)
        throw std::runtime_error("not a RIFF/WAVE file");

    uint16_t fmtTag = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    std::vector<unsigned char> data;
    bool haveFmt = false, haveData = false;

    while (f) {
        unsigned char chdr[8];
        f.read((char*)chdr, 8);
        if (f.gcount() != 8) break;
        uint32_t size = rd32(chdr + 4);
        std::vector<unsigned char> chunk(size);
        if (size) f.read((char*)chunk.data(), size);
        if (size & 1) f.ignore(1);

        if (std::memcmp(chdr, "fmt ", 4) == 0) {
            if (size < 16) throw std::runtime_error("bad fmt chunk");
            fmtTag = rd16(chunk.data());
            channels = rd16(chunk.data() + 2);
            sampleRate = rd32(chunk.data() + 4);
            bits = rd16(chunk.data() + 14);
            if (fmtTag == 0xfffe && size >= 40)
                fmtTag = rd16(chunk.data() + 24); // WAVE_FORMAT_EXTENSIBLE subformat
            haveFmt = true;
        } else if (std::memcmp(chdr, "data", 4) == 0) {
            data.swap(chunk);
            haveData = true;
        }
    }

    if (!haveFmt || !haveData) throw std::runtime_error("wav missing fmt/data");
    if (channels < 1 || channels > 2) throw std::runtime_error("only mono/stereo wav supported");
    if (!(fmtTag == 1 || fmtTag == 3)) throw std::runtime_error("only PCM or float wav supported");
    if (fmtTag == 3 && bits != 32) throw std::runtime_error("only 32-bit float wav supported");
    if (fmtTag == 1 && !(bits == 16 || bits == 24 || bits == 32)) throw std::runtime_error("only 16/24/32-bit PCM wav supported");

    const int bytes = bits / 8;
    const size_t frames = data.size() / (bytes * channels);
    Audio a;
    a.sr = (int)sampleRate;
    a.ch = (int)channels;
    a.l.resize(frames);
    a.r.resize(frames);

    const unsigned char* p = data.data();
    for (size_t i = 0; i < frames; ++i) {
        float vals[2] = {0.0f, 0.0f};
        for (int c = 0; c < channels; ++c) {
            float v = 0.0f;
            if (fmtTag == 3) {
                float fv;
                std::memcpy(&fv, p, 4);
                v = fv;
            } else if (bits == 16) {
                int16_t s = (int16_t)rd16(p);
                v = (float)s / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = (float)s / 8388608.0f;
            } else {
                int32_t s = (int32_t)rd32(p);
                v = (float)((double)s / 2147483648.0);
            }
            vals[c] = v;
            p += bytes;
        }
        a.l[i] = vals[0];
        a.r[i] = channels > 1 ? vals[1] : vals[0];
    }
    return a;
}

static void writeFloatWav(const char* path, int sr, const std::vector<float>& l, const std::vector<float>& r) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open output wav");
    const uint16_t channels = 2;
    const uint16_t bits = 32;
    const uint16_t fmt = 3; // IEEE float
    const uint16_t blockAlign = channels * bits / 8;
    const uint32_t byteRate = (uint32_t)sr * blockAlign;
    const uint32_t dataBytes = (uint32_t)(l.size() * blockAlign);
    f.write("RIFF", 4); wr32(f, 36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); wr32(f, 16); wr16(f, fmt); wr16(f, channels); wr32(f, (uint32_t)sr);
    wr32(f, byteRate); wr16(f, blockAlign); wr16(f, bits);
    f.write("data", 4); wr32(f, dataBytes);
    for (size_t i = 0; i < l.size(); ++i) {
        float lv = l[i], rv = r[i];
        f.write((const char*)&lv, 4);
        f.write((const char*)&rv, 4);
    }
}

struct Stats {
    double rms = 0.0;
    double peak = 0.0;
    double meanAbs = 0.0;
    double clipFrac = 0.0;
    double nearZeroFrac = 0.0;
};

static Stats stats(const std::vector<float>& x) {
    long double ss = 0.0, aa = 0.0;
    double peak = 0.0;
    size_t clip = 0, zero = 0;
    for (float v : x) {
        double a = std::fabs((double)v);
        ss += (long double)v * (long double)v;
        aa += a;
        if (a > peak) peak = a;
        if (a >= 0.99) ++clip;
        if (a < 1.0e-6) ++zero;
    }
    Stats s;
    if (!x.empty()) {
        s.rms = std::sqrt((double)(ss / x.size()));
        s.meanAbs = (double)(aa / x.size());
        s.clipFrac = (double)clip / (double)x.size();
        s.nearZeroFrac = (double)zero / (double)x.size();
    }
    s.peak = peak;
    return s;
}

static double db(double v) { return v > 0.0 ? 20.0 * std::log10(v) : -240.0; }

static void printStats(const char* label, const std::vector<float>& in, const std::vector<float>& out) {
    Stats si = stats(in);
    Stats so = stats(out);
    const size_t win = 2400; // 50 ms @ 48k
    size_t active = 0, dropouts = 0;
    double worstRatio = 999.0;
    size_t worstIndex = 0;
    for (size_t pos = 0; pos + win <= in.size(); pos += win) {
        long double iss = 0.0, oss = 0.0;
        for (size_t j = 0; j < win; ++j) {
            iss += (long double)in[pos + j] * (long double)in[pos + j];
            oss += (long double)out[pos + j] * (long double)out[pos + j];
        }
        double ir = std::sqrt((double)(iss / win));
        double orr = std::sqrt((double)(oss / win));
        if (db(ir) > -55.0) {
            ++active;
            double ratio = db(orr) - db(ir);
            if (ratio < worstRatio) { worstRatio = ratio; worstIndex = pos / win; }
            if (db(orr) < -70.0 || ratio < -45.0) ++dropouts;
        }
    }
    std::cout
        << label
        << " input_rms_dbfs=" << db(si.rms)
        << " input_peak_dbfs=" << db(si.peak)
        << " output_rms_dbfs=" << db(so.rms)
        << " output_peak_dbfs=" << db(so.peak)
        << " clip_frac=" << so.clipFrac
        << " near_zero_frac=" << so.nearZeroFrac
        << " active_windows=" << active
        << " dropout_windows=" << dropouts
        << " worst_window=" << worstIndex
        << " worst_out_in_db=" << worstRatio
        << "\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: probe in.wav out.wav [idx=value ...]\n";
        return 2;
    }
    try {
        Audio in = readWav(argv[1]);
        Probe p;
        p.sr((double)in.sr);
        p.act();
        bool repeatParamsEachBlock = false;
        std::vector<std::pair<int, float>> overrides;
        for (int a = 3; a < argc; ++a) {
            if (std::strcmp(argv[a], "--repeat-params-each-block") == 0) {
                repeatParamsEachBlock = true;
                continue;
            }
            const char* eq = std::strchr(argv[a], '=');
            if (!eq) continue;
            int idx = std::atoi(argv[a]);
            float value = std::strtof(eq + 1, nullptr);
            if (idx >= 0 && idx < (int)kParamCount) {
                p.setP((uint32_t)idx, value);
                overrides.push_back(std::make_pair(idx, value));
            }
        }

        std::vector<float> outL(in.l.size()), outR(in.r.size());
        static constexpr uint32_t CHUNK = 512;
        float inL[CHUNK], inR[CHUNK], oL[CHUNK], oR[CHUNK];
        const float* ins[2] = { inL, inR };
        float* outs[2] = { oL, oR };
        size_t pos = 0;
        while (pos < in.l.size()) {
            uint32_t n = (uint32_t)std::min<size_t>(CHUNK, in.l.size() - pos);
            if (repeatParamsEachBlock) {
                for (const auto& kv : overrides)
                    p.setP((uint32_t)kv.first, kv.second);
            }
            for (uint32_t i = 0; i < n; ++i) {
                inL[i] = in.l[pos + i];
                inR[i] = in.r[pos + i];
            }
            p.go(ins, outs, n);
            for (uint32_t i = 0; i < n; ++i) {
                outL[pos + i] = oL[i];
                outR[pos + i] = oR[i];
            }
            pos += n;
        }
        writeFloatWav(argv[2], in.sr, outL, outR);
        printStats("left", in.l, outL);
        printStats("right", in.r, outR);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
'''


def sdk_path() -> str:
    return subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()


def parse_amp(d: Path) -> dict | None:
    cpps = [p for p in d.glob("*.cpp") if "createPlugin" in p.read_text(errors="ignore")]
    if not cpps:
        return None
    plugin_cpp = cpps[0]
    src = plugin_cpp.read_text(errors="ignore")
    m = re.search(r"createPlugin\s*\(\s*\)\s*\{\s*return\s+new\s+(\w+)", src)
    if not m:
        m = re.search(r"class\s+(\w+)\s*:\s*public\s+Plugin\b", src)
    if not m:
        return None
    hdrs = list(d.glob("*Params.h"))
    if not hdrs:
        return None
    htext = hdrs[0].read_text(errors="ignore")
    ma = re.search(r"k\w*Names\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", htext, re.S)
    if not ma:
        return None
    names = re.findall(r'"([^"]*)"', ma.group(1))
    return {"plugin_cpp": plugin_cpp.name, "class": m.group(1), "names": names}


def param_args(names: list[str], pairs: list[str]) -> list[str]:
    idx = {n.lower(): i for i, n in enumerate(names)}
    args: list[str] = []
    for pair in pairs:
        if pair == "--repeat-params-each-block":
            args.append(pair)
            continue
        if "=" not in pair:
            raise SystemExit(f"bad param override {pair!r}; expected Name=Value")
        name, value = pair.split("=", 1)
        key = name.strip().lower()
        if key not in idx:
            known = ", ".join(names)
            raise SystemExit(f"unknown param {name!r}; known: {known}")
        args.append(f"{idx[key]}={float(value):.8g}")
    return args


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    effect_name = argv[0]
    if effect_name.startswith("pedals/"):
        amp_dir = PEDALS / effect_name.removeprefix("pedals/")
    elif effect_name.startswith("amps/"):
        amp_dir = AMPS / effect_name.removeprefix("amps/")
    else:
        amp_dir = AMPS / effect_name
    in_wav = Path(argv[1]).resolve()
    out_wav = Path(argv[2]).resolve()
    info = parse_amp(amp_dir)
    if not info:
        print(f"could not parse amp in {amp_dir}", file=sys.stderr)
        return 1
    out_wav.parent.mkdir(parents=True, exist_ok=True)

    probe = HARNESS.replace("@PLUGIN_CPP@", info["plugin_cpp"]).replace("@CLASS@", info["class"])
    probe_path = amp_dir / "_render_probe.cpp"
    probe_path.write_text(probe)
    try:
        with tempfile.NamedTemporaryFile(suffix="_render_probe", delete=False) as tf:
            binpath = Path(tf.name)
        cmd = [
            "/usr/bin/clang++", "-isysroot", sdk_path(), "-std=c++14", "-O2",
            "-I.", "-I..", "-I../DPF/distrho", "-I../DPF/dgl",
            "_render_probe.cpp", DPF_SRC, "-o", str(binpath),
        ]
        r = subprocess.run(cmd, cwd=amp_dir, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[-4000:], file=sys.stderr)
            return r.returncode
        run_cmd = [str(binpath), str(in_wav), str(out_wav), *param_args(info["names"], argv[3:])]
        run = subprocess.run(run_cmd, capture_output=True, text=True)
        if run.stdout:
            print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, file=sys.stderr, end="")
        return run.returncode
    finally:
        probe_path.unlink(missing_ok=True)
        try:
            binpath.unlink(missing_ok=True)
        except UnboundLocalError:
            pass


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
