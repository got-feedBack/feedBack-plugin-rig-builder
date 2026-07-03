/*
 * Epicall Ruby — Epiphone Electar Zephyr Amp20 (~1949, Danelectro-built),
 * COMPONENT-LEVEL model.
 *
 * From the 1949 Zephyr schematic (5879 pentode + 6SL7 + 6SF5 preamp/tremolo ->
 * 2x 6L6G push-pull, 5U4G rectifier, ~20 W). the game abstracts it to three
 * controls, so the model is:
 *   • V1 5879 pentode input stage
 *   • TONE  — passive Bass + Treble shelves
 *   • V2 6SL7 driver + 6SF5 recovery/color stage, pushed by Volume
 *   • 2x 6L6G push-pull (~20 W): big, warm American power section that blooms into
 *     vintage breakup when the Volume is cranked.
 *
 * Tube tables are generated from the local 5879/6SL7/6SF5 datasheets with
 * per-stage Miller loading into passive tone shaping and a 6L6G push-pull
 * power section generated from the local Sylvania 6L6G datasheet.
 */
#include "DistrhoPlugin.hpp"
#include "RubyParams.h"
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/oversampler.hpp"
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
    void reset() { vG=0; vP=320; vK=1.5; dcAvg=320.0;  for (int i=0;i<2000;++i) process(0.0); }
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
    rbtube::TubeStage5879 v1;
    rbtube::TubeStage6SL7 v2;
    rbtube::TubeStage6SF5 v3;
    Biquad hp, bass, treble, pwrLP;
    Biquad cabHp, cabLo, cabHi, cabLp;
    rbtube::Miller5879 inputMiller;
    rbtube::Miller6SL7 driverMiller;
    rbtube::Miller6SF5 recoveryMiller;
    rbtube::CouplingCapGridLeak coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmp6L6G power;
    float drive=1, level=1, pwrDrive=1, cabSim=1;
public:
    void setSampleRate(float s) { fs=(s>0.f)?s:48000.f; }
    void reset() { v1.reset(); v2.reset(); v3.reset(); hp.reset(); bass.reset(); treble.reset(); pwrLP.reset();
        cabHp.reset(); cabLo.reset(); cabHi.reset(); cabLp.reset();
        inputMiller.reset(); driverMiller.reset(); recoveryMiller.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); }

    void setParams(float volume, float bassP, float trebleP, float cabSimP) {
        hp.setHighpassQ(60.f, 0.7f, fs);
        bass.setLowShelf  (120.f, (bassP   - 0.5f) * 16.f, fs);
        treble.setHighShelf(3000.f,(trebleP - 0.5f) * 14.f, fs);
        drive    = 0.42f + volume * volume * 3.8f;         // Volume drives the 6SL7/6SF5 chain
        level    = 0.5f + volume * 0.8f;
        pwrDrive = 0.45f + volume * 0.8f;
        pwrLP.setLowpassQ(12000.f, 0.7f, fs);              // vintage 6L6G + OT, miked-cab bright
        cabSim = cabSimP;
        v1.setWithPlate(fs, 1, 250.0f, 34.0f, 32.0f, 1500.0f, 220000.0f); // 5879 pentode, high plate load
        v2.setWithPlate(fs, 1, 250.0f, 34.0f, 6.0f, 2200.0f, 100000.0f);  // 6SL7 driver
        v3.setWithPlate(fs, 1, 250.0f, 48.0f, 8.0f, 1500.0f, 250000.0f);  // 6SF5 recovery/color
        inputMiller.set(fs, 68000.0f, 30.0f, 6.0f);
        driverMiller.set(fs, 220000.0f, 32.0f, 8.0f);
        recoveryMiller.set(fs, 250000.0f, 42.0f, 8.0f);
        coupleToPi.set(fs, 1000000.0f, 47.0e-9f, 47000.0f, 0.10f, 0.56f, 1.7f);
        phaseInverter.setComponents(fs, 0.70f + 1.05f * volume, 0.78f,
                                    275.0f, 100000.0f, 82000.0f, 1500.0f, 16.0f, 0.055f);
        supply.set(fs,
                   180.0f, 16.0f,
                   1200.0f, 16.0f,
                   10000.0f, 16.0f,
                   0.28f, 0.18f, 0.085f, 0.24f);
        power.set(fs, 0.78f + 1.38f * volume,
                  -22.5f, 0.34f, 62.0f, 10000.0f + 800.0f * trebleP);
        power.out = 0.0140f;
        cabHp.setHighpassQ(74.f, 0.72f, fs);
        cabLo.setLowShelf(120.f, 1.6f + 2.0f * bassP, fs);
        cabHi.setHighShelf(3500.f, -2.8f + 4.5f * trebleP, fs);
        cabLp.setLowpassQ(10800.f + 1700.f * trebleP, 0.66f, fs);
    }

    inline float process(float x) {
        float s = hp.process(x);
        s = v1.process(2.0f * inputMiller.process(s));      // 5879 pentode input
        s = bass.process(s); s = treble.process(s);        // passive tone
        s = v2.process(driverMiller.process(drive * s));    // 6SL7 driver, pushed by Volume
        s = v3.process(recoveryMiller.process((0.48f + 0.40f * level) * s));
        const float load = std::fabs(s) * (0.68f + 0.82f * level);
        const rbtube::SupplyScales bplus = supply.process(load, load * 0.55f, load * 0.24f);
        s *= 0.92f + 0.08f * bplus.preamp;
        s = coupleToPi.process(s * pwrDrive * bplus.screen, 1.0f);
        s = phaseInverter.process(s) * bplus.screen;
        s = power.process(s * bplus.power) * level;        // 2x 6L6G power
        s = pwrLP.process(s);
        const float ampOnly = s;
        float cab = cabHp.process(ampOnly);
        cab = cabLo.process(cab);
        cab = cabHi.process(cab);
        cab = cabLp.process(cab);
        return ampOnly + cabSim * (cab - ampOnly);
    }
};

static constexpr float kRubyMakeup = 3.50f;   // tuned offline (-14 dBFS @ kDef)
static constexpr float kRubyLvl    = 1.138f;

class RubyPlugin : public Plugin {
    RubyChannel L, R;
    rbshared::Oversampler4x osL, osR;
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    float fParams[kParamCount];
    void recalc() {
        L.setParams(fParams[kVolume], fParams[kBass], fParams[kTreble], fParams[kCabSim]);
        R.setParams(fParams[kVolume], fParams[kBass], fParams[kTreble], fParams[kCabSim]);
    }
public:
    RubyPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRubyDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(kOS * sr); R.setSampleRate(kOS * sr); L.reset(); R.reset(); recalc();
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
    void  sampleRateChanged(double r) override { osL.reset(); osR.reset(); L.setSampleRate(kOS * (float)r); R.setSampleRate(kOS * (float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        float ubL[kOS]; float ubR[kOS];
        for (uint32_t i = 0; i < frames; ++i) {
            osL.upsample(3.2f * iL[i], ubL); osR.upsample(3.2f * iR[i], ubR);
            for (int k = 0; k < kOS; ++k) { ubL[k] = softClip(kRubyMakeup * L.process(ubL[k])); ubR[k] = softClip(kRubyMakeup * R.process(ubR[k])); }
            oL[i] = rbAmpLvl(kRubyLvl * osL.downsample(ubL) * 0.98f); oR[i] = rbAmpLvl(kRubyLvl * osR.downsample(ubR) * 0.98f);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RubyPlugin)
};

Plugin* createPlugin() { return new RubyPlugin(); }

END_NAMESPACE_DISTRHO
