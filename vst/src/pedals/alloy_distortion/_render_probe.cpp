
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

#include "AlloyDistortionPlugin.cpp"

START_NAMESPACE_DISTRHO
struct Probe : public AlloyDistortionPlugin {
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
