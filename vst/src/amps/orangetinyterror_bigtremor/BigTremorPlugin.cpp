/*
 * Citrus Big Tremor — Orange Tiny Terror single-channel head, COMPONENT-LEVEL.
 *
 * From the Tiny Terror schematic (Orange Music): a short, hot signal path —
 *   • INPUT  — guitar jack
 *   • V1a 12AX7 input gain stage (100k plate, 1.5k/22uF cathode)
 *   • GAIN   — 500K pot between the two triodes (sets how hard V1b is driven)
 *   • V1b 12AX7 second gain stage (68k/100k plate) — this cascade is what lets
 *            the Tiny Terror go from clean to a thick crunch
 *   • TONE   — the single passive tone control (.047uF tilt: down = dark, up = bright)
 *   • VOLUME — master into the phase inverter
 *   • PI 12AX7 long-tail pair -> 2x EL84 CATHODE-BIASED push-pull (~15 W Class A;
 *            EL84s break up early and sing). OUTPUT switch drops to ~7 W (one pair
 *            half, lower headroom = earlier breakup).
 *
 * Two real nodal 12AX7s (Koren + Newton/sample) into a passive tilt and an EL84
 * push-pull saturator (earlier knee than the KT88/6L6 amps). Shared MNA/Triode/
 * Biquad blocks are byte-identical to the Citrus AD200 source.
 */
#include "DistrhoPlugin.hpp"
#include "BigTremorParams.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad — passive tone tilt + power band-limit ─────────────────────────
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
    void setPeak(float fc, float dB, float Q, float fs) {
        const float A = std::pow(10.f, dB / 40.f);
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0; b1 = (-2 * cw) / a0; b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha / A) / a0;
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

// ── 12AX7 triode — nodal Koren + Newton/sample (same model as Citrus) ─────────
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

class BigTremorChannel {
    float fs = 48000.f;
    Triode v1a, v1b;
    Biquad hp, toneLo, toneHi, pwrLP;
    Biquad cabHp, cabThump, cabLowMid, cabBite, cabFizz, cabLp;
    rbtube::CouplingCapGridLeak coupleV1aToV1b;
    rbtube::CouplingCapGridLeak coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpPP power;
    float gain=1, volume=1, pwrDrive=1, cabSim=1;
public:
    void setSampleRate(float s) { fs=(s>0.f)?s:48000.f; v1a.setT(s); v1b.setT(s); }
    void reset() { v1a.reset(); v1b.reset(); hp.reset(); toneLo.reset(); toneHi.reset(); pwrLP.reset();
        cabHp.reset(); cabThump.reset(); cabLowMid.reset(); cabBite.reset(); cabFizz.reset(); cabLp.reset();
        coupleV1aToV1b.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); }

    void setParams(float volumeP, float tone, float gainP, bool half, float cabSimP) {
        hp.setHighpassQ(70.f, 0.7f, fs);                  // input/coupling HPF (tight low end)
        gain = 0.6f + gainP * gainP * 7.0f;               // GAIN pot drives V1b (squared taper)

        // single passive tone control: a tilt about ~800 Hz — turning down cuts
        // treble (dark), turning up cuts a touch of low and lifts highs (bright).
        const float t = (tone - 0.5f) * 2.0f;             // -1 .. +1
        toneHi.setHighShelf(2200.f,  t * 12.f, fs);
        toneLo.setLowShelf (180.f,  -t * 4.0f, fs);

        volume   = volumeP / 0.6f;                        // master (def 0.6 -> unity)
        pwrDrive = 0.5f + volume * 0.9f;
        cabSim   = cabSimP;
        pwrLP.setLowpassQ(14000.f - 3000.f * volume, 0.7f, fs);  // EL84 + OT band-limit (opened, miked-cab top)
        coupleV1aToV1b.set(fs, 500000.0f, 22.0e-9f, 100000.0f,
                           0.11f, 0.44f, 1.10f);
        coupleToPi.set(fs, 1000000.0f, 22.0e-9f, 47000.0f, 0.10f, half ? 0.72f : 0.56f, half ? 2.2f : 1.6f);
        phaseInverter.setVoxAc30(fs, 0.72f + 1.15f * volume + 0.36f * gainP, 0.76f, 0.08f);
        supply.set(fs,
                   half ? 145.0f : 95.0f, half ? 33.0f : 47.0f,
                   1000.0f, 22.0f,
                   10000.0f, 22.0f,
                   half ? 0.28f : 0.18f,
                   half ? 0.18f : 0.12f,
                   half ? 0.080f : 0.052f,
                   half ? 0.20f : 0.16f);
        power.set(fs, 0.92f + 1.42f * volume + 0.55f * gainP + (half ? 0.42f : 0.0f),
                  half ? -6.6f : -7.3f,
                  half ? 0.48f : 0.34f,
                  72.0f,
                  11800.0f + 1100.0f * tone);
        power.out = 0.020f;
        cabHp.setHighpassQ(82.f, 0.72f, fs);
        cabThump.setPeak(132.f, 1.1f + 0.8f * volume, 0.84f, fs);
        cabLowMid.setPeak(520.f, 1.6f, 0.74f, fs);
        cabBite.setPeak(2500.f + 520.f * tone, 1.6f + 1.8f * tone, 0.78f, fs);
        cabFizz.setHighShelf(4850.f, -1.8f + 2.4f * tone - 1.0f * gainP, fs);
        cabLp.setLowpassQ(12400.f + 1400.f * tone - 1500.f * volume, 0.66f, fs);
    }

    inline float process(float x) {
        float s = hp.process(x);
        s = (float)v1a.process((double)(3.0f * s));       // V1a input stage (fixed drive)
        s = coupleV1aToV1b.process(s, gain);
        s = (float)v1b.process((double)s);                // V1b driven by the Gain pot/acople anterior
        s = toneLo.process(s); s = toneHi.process(s);     // passive tone tilt
        const float load = std::fabs(s) * (0.70f + 0.85f * volume);
        const rbtube::SupplyScales bplus = supply.process(load, load * 0.55f, load * 0.24f);
        s *= 0.92f + 0.08f * bplus.preamp;
        s = coupleToPi.process(s * pwrDrive * bplus.screen, 1.0f);
        s = phaseInverter.process(s) * bplus.screen;
        s = power.process(s * bplus.power) * volume;       // 2x EL84 power
        s = pwrLP.process(s);
        const float ampOnly = s;
        float cab = cabHp.process(ampOnly);
        cab = cabThump.process(cab);
        cab = cabLowMid.process(cab);
        cab = cabBite.process(cab);
        cab = cabFizz.process(cab);
        cab = cabLp.process(cab);
        return ampOnly + cabSim * (cab - ampOnly);
    }
};

static constexpr float kBigTremorMakeup = 3.50f;   // tuned offline (-14 dBFS @ kDef)
static constexpr float kBigTremorLvl    = 0.2610f;

class BigTremorPlugin : public Plugin {
    BigTremorChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool half = fParams[kHalf] > 0.5f;
        L.setParams(fParams[kVolume], fParams[kTone], fParams[kGain], half, fParams[kCabSim]);
        R.setParams(fParams[kVolume], fParams[kTone], fParams[kGain], half, fParams[kCabSim]);
    }
public:
    BigTremorPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kBigTremorDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "CitrusBigTremor"; }
    const char* getDescription() const override { return "Orange Tiny Terror single-channel guitar head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'B', 'T'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kHalf) p.hints |= kParameterIsBoolean;
        p.name = kBigTremorNames[i]; p.symbol = kBigTremorSymbols[i];
        p.ranges.min = kBigTremorMin[i]; p.ranges.max = kBigTremorMax[i]; p.ranges.def = kBigTremorDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(kBigTremorLvl * softClip(kBigTremorMakeup * L.process(3.2f * iL[i])) * 0.98f); oR[i] = rbAmpLvl(kBigTremorLvl * softClip(kBigTremorMakeup * R.process(3.2f * iR[i])) * 0.98f); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigTremorPlugin)
};

Plugin* createPlugin() { return new BigTremorPlugin(); }

END_NAMESPACE_DISTRHO
