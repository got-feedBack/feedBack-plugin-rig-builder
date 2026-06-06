/*
 * Epicall Ruby — Epiphone Electar Zephyr Amp20 (~1949, Danelectro-built),
 * COMPONENT-LEVEL model.
 *
 * From the 1949 Zephyr schematic (5879 pentode + 6SL7 + 6SF5 preamp/tremolo ->
 * 2x 6L6G push-pull, 5U4G rectifier, ~20 W). Rocksmith abstracts it to three
 * controls, so the model is:
 *   • V1 12AX7 input stage (stands in for the 5879/6SL7 vintage front end)
 *   • TONE  — passive Bass + Treble shelves
 *   • V2 12AX7 driver, pushed by Volume
 *   • 2x 6L6G push-pull (~20 W): big, warm American power section that blooms into
 *     vintage breakup when the Volume is cranked.
 *
 * Real nodal 12AX7s (Koren + Newton/sample) into passive tone shaping and a 6L6
 * push-pull saturator (late, round knee). Shared MNA/Triode/Biquad blocks are
 * byte-identical to the Citrus AD200 source.
 */
#include "DistrhoPlugin.hpp"
#include "RubyParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad — passive Bass/Treble + power band-limit ───────────────────────
class Biquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
public:
    void reset() { z1 = z2 = 0.f; }
    inline float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setLowShelf(float fc, float dB, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw * 0.5f * 1.4142135f;
        const float sA = std::sqrt(A), tsAa = 2.f * sA * alpha;
        const float a0 =       (A + 1) + (A - 1) * cw + tsAa;
        b0 =  A * ((A + 1) - (A - 1) * cw + tsAa) / a0;
        b1 = 2*A * ((A - 1) - (A + 1) * cw)        / a0;
        b2 =  A * ((A + 1) - (A - 1) * cw - tsAa)  / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw)         / a0;
        a2 =      ((A + 1) + (A - 1) * cw - tsAa)  / a0;
    }
    void setHighShelf(float fc, float dB, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw * 0.5f * 1.4142135f;
        const float sA = std::sqrt(A), tsAa = 2.f * sA * alpha;
        const float a0 =       (A + 1) - (A - 1) * cw + tsAa;
        b0 =  A * ((A + 1) + (A - 1) * cw + tsAa) / a0;
        b1 = -2*A * ((A - 1) + (A + 1) * cw)      / a0;
        b2 =  A * ((A + 1) + (A - 1) * cw - tsAa) / a0;
        a1 =  2 * ((A - 1) - (A + 1) * cw)        / a0;
        a2 =      ((A + 1) - (A - 1) * cw - tsAa) / a0;
    }
    void setLowpassQ(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 =  (1 - cw) * 0.5f / a0; b1 = (1 - cw) / a0; b2 = (1 - cw) * 0.5f / a0;
        a1 =  -2 * cw / a0; a2 = (1 - alpha) / a0;
    }
    void setHighpassQ(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = (1 + cw) * 0.5f / a0; b1 = -(1 + cw) / a0; b2 = (1 + cw) * 0.5f / a0;
        a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
    }
};

// ── Tiny fixed-size MNA solver (RT-safe) — for the 12AX7s ─────────────────────
struct Mna {
    static const int MAXN = 8;
    int sz, nn;
    double A[MAXN*MAXN], b[MAXN], x[MAXN];
    void init(int nN, int nX) { nn = nN; sz = nN + nX;
        for (int i = 0; i < sz*sz; ++i) A[i] = 0.0; for (int i = 0; i < sz; ++i) b[i] = 0.0; }
    inline void stampG(int a, int bb, double g) {
        if (a>0)  { A[(a-1)*sz+(a-1)]  += g; if (bb>0) A[(a-1)*sz+(bb-1)] -= g; }
        if (bb>0) { A[(bb-1)*sz+(bb-1)]+= g; if (a>0)  A[(bb-1)*sz+(a-1)] -= g; } }
    inline void R(int a, int bb, double r) { if (r < 1e-9) r = 1e-9; stampG(a, bb, 1.0/r); }
    inline void Isrc(int a, int bb, double I) { if (a>0) b[a-1] -= I; if (bb>0) b[bb-1] += I; }
    inline void Vsrc(int a, double V, int k) { int r = nn+k;
        if (a>0) { A[(a-1)*sz+r] += 1; A[r*sz+(a-1)] += 1; } b[r] = V; }
    inline void gm(int oa, int ob, int ca, int cb, double g) {
        if (oa>0) { if (ca>0) A[(oa-1)*sz+(ca-1)] += g; if (cb>0) A[(oa-1)*sz+(cb-1)] -= g; }
        if (ob>0) { if (ca>0) A[(ob-1)*sz+(ca-1)] -= g; if (cb>0) A[(ob-1)*sz+(cb-1)] += g; } }
    bool solve() { const int n = sz;
        for (int col = 0; col < n; ++col) {
            int piv = col; double mx = std::fabs(A[col*n+col]);
            for (int r = col+1; r < n; ++r) { double v = std::fabs(A[r*n+col]); if (v > mx) { mx = v; piv = r; } }
            if (mx < 1e-18) return false;
            if (piv != col) { for (int c = 0; c < n; ++c) { double t = A[col*n+c]; A[col*n+c] = A[piv*n+c]; A[piv*n+c] = t; }
                double t = b[col]; b[col] = b[piv]; b[piv] = t; }
            const double d = A[col*n+col];
            for (int r = 0; r < n; ++r) { if (r == col) continue; const double f = A[r*n+col]/d; if (f == 0) continue;
                for (int c = col; c < n; ++c) A[r*n+c] -= f*A[col*n+c]; b[r] -= f*b[col]; } }
        for (int i = 0; i < n; ++i) x[i] = b[i] / A[i*n+i];
        return true; } };

// ── 12AX7 triode — nodal Koren + Newton/sample ───────────────────────────────
struct Triode {
    double vG=0, vP=320, vK=1.5, dcAvg=320.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=320; vK=1.5; dcAvg=320.0; }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {
        const double Bp=320, Rp=100000, Rk=1500, h=1e-4;
        double G=vG, P=vP, K=vK;
        for (int it=0; it<12; ++it) {
            Mna m; m.init(4, 2);
            m.Vsrc(1, Bp, 0); m.Vsrc(2, vin, 1);
            m.R(3, 1, Rp); m.R(4, 0, Rk);
            const double vgk=G-K, vpk=P-K, ip=Ip(vgk,vpk);
            const double gmv=(Ip(vgk+h,vpk)-ip)/h, gp=(Ip(vgk,vpk+h)-ip)/h;
            m.gm(3,0,2,0,gmv); m.gm(3,0,3,0,gp); m.gm(3,0,4,0,-(gmv+gp));
            m.gm(4,0,2,0,-gmv); m.gm(4,0,3,0,-gp); m.gm(4,0,4,0,(gmv+gp));
            m.Isrc(3,0, ip-(gmv*G+gp*P-(gmv+gp)*K));
            m.Isrc(4,0, -ip-(-gmv*G-gp*P+(gmv+gp)*K));
            if (!m.solve()) break;
            const double nP=m.x[2], nK=m.x[3]; const double err=std::fabs(nP-P)+std::fabs(nK-K);
            G=m.x[1]; P=P+0.7*(nP-P); K=K+0.7*(nK-K);
            if (err<1e-6) break;
        }
        if (!std::isfinite(P)) { reset(); return 0.0; }
        vG=G; vP=P; vK=K;
        dcAvg += 0.0008*(P-dcAvg);
        return -(P - dcAvg) * (1.0/40.0);
    }
};

class RubyChannel {
    float fs = 48000.f;
    Triode v1, v2;
    Biquad hp, bass, treble, pwrLP;
    float drive=1, level=1, pwrDrive=1;

    // 2x 6L6G push-pull (~20 W): big, round, late American knee.
    static inline float pushPull(float x) { return std::tanh(x * 0.72f) / 0.72f; }
public:
    void setSampleRate(float s) { fs=(s>0.f)?s:48000.f; v1.setT(s); v2.setT(s); }
    void reset() { v1.reset(); v2.reset(); hp.reset(); bass.reset(); treble.reset(); pwrLP.reset(); }

    void setParams(float volume, float bassP, float trebleP) {
        hp.setHighpassQ(60.f, 0.7f, fs);
        bass.setLowShelf  (120.f, (bassP   - 0.5f) * 16.f, fs);
        treble.setHighShelf(3000.f,(trebleP - 0.5f) * 14.f, fs);
        drive    = 0.6f + volume * volume * 5.0f;          // Volume drives V2 into the 6L6s
        level    = 0.5f + volume * 0.8f;
        pwrDrive = 0.45f + volume * 0.8f;
        pwrLP.setLowpassQ(7500.f, 0.7f, fs);               // vintage 6L6 + OT roll-off
    }

    inline float process(float x) {
        float s = hp.process(x);
        s = (float)v1.process((double)(2.2f * s));         // vintage input stage
        s = bass.process(s); s = treble.process(s);        // passive tone
        s = (float)v2.process((double)(drive * s));        // driver, pushed by Volume
        s = pushPull(s * pwrDrive) * level;                // 2x 6L6 power
        s = pwrLP.process(s);
        return s;
    }
};

static constexpr float kRubyMakeup = 3.50f;   // tuned offline (-14 dBFS @ kDef)
static constexpr float kRubyLvl    = 0.3284f;

class RubyPlugin : public Plugin {
    RubyChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kVolume], fParams[kBass], fParams[kTreble]);
        R.setParams(fParams[kVolume], fParams[kBass], fParams[kTreble]);
    }
public:
    RubyPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRubyDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "EpicallRuby"; }
    const char* getDescription() const override { return "Epiphone Electar Zephyr Amp20 vintage guitar amp — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'R', 'B'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        p.name = kRubyNames[i]; p.symbol = kRubySymbols[i];
        p.ranges.min = kRubyMin[i]; p.ranges.max = kRubyMax[i]; p.ranges.def = kRubyDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(kRubyLvl * softClip(kRubyMakeup * L.process(iL[i])) * 0.98f); oR[i] = rbAmpLvl(kRubyLvl * softClip(kRubyMakeup * R.process(iR[i])) * 0.98f); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubyPlugin)
};

Plugin* createPlugin() { return new RubyPlugin(); }

END_NAMESPACE_DISTRHO
