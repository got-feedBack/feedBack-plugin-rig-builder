/*
 * Citrus Rumbleverb 50 — Orange Rockerverb 50 MkII, COMPONENT-LEVEL model.
 *
 * From the Rockerverb 50 schematic (Preamp 1 & 2, Loop, Reverb & Phase Splitter
 * + 50 W power board) and the MkII panel:
 *   • INPUT  — guitar jack -> shared 12AX7 input stage
 *   • CLEAN (Natural) channel : Clean Volume + a 2-band passive tone (Bass/Treble),
 *              one extra triode — big clean headroom.
 *   • DIRTY channel : Gain -> cascaded 12AX7 stages -> a 3-band tone stack
 *              (Bass/Middle/Treble) -> channel Volume. The high-gain voice
 *              the game uses (RS Gain/Bass/Mid/Treble map here).
 *   • REVERB — valve-driven spring (Schroeder comb+allpass), blended by Reverb.
 *   • OUTPUT — master into the 2x EL34 push-pull (~50 W; Full/Half on the panel).
 *
 * Real nodal 12AX7 stages (Koren + Newton/sample) per channel into passive tone
 * shaping, a compact spring reverb, and an EL34 push-pull saturator (knee between
 * the EL84 Big Tremor and the KT88 Citrus). Shared MNA/Triode/Biquad blocks are
 * byte-identical to the Citrus AD200 source.
 */
#include "DistrhoPlugin.hpp"
#include "RumbleverbParams.h"
#include "../../_shared/tube_stage.hpp"
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad — tone stacks + power band-limit ──────────────────────────────
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

// ── compact valve-spring reverb (Schroeder: 2 combs + 1 allpass), mono ────────
struct Spring {
    static const int C1=1567, C2=1789, AP=353;
    float c1[C1], c2[C2], ap[AP];
    int i1=0, i2=0, ia=0;
    void reset(){ std::memset(c1,0,sizeof c1); std::memset(c2,0,sizeof c2); std::memset(ap,0,sizeof ap); i1=i2=ia=0; }
    inline float process(float x){
        const float fb=0.78f;
        float o1=c1[i1]; c1[i1]=x+o1*fb; if(++i1>=C1)i1=0;
        float o2=c2[i2]; c2[i2]=x+o2*fb; if(++i2>=C2)i2=0;
        float s=(o1+o2)*0.5f;
        float a=ap[ia]; float out=-s*0.5f+a; ap[ia]=s+a*0.5f; if(++ia>=AP)ia=0;
        return out;
    }
};

class RumbleverbChannel {
    float fs = 48000.f;
    Triode vIn, vDirty, vClean;
    Biquad hp, dBass, dMid, dTreble, cBass, cTreble, pwrLP;
    Biquad cabHp, cabThump, cabLowMid, cabBite, cabFizz, cabLp;
    Spring spring;
    rbtube::CouplingCapGridLeak coupleToDirty;
    rbtube::CouplingCapGridLeak coupleToClean;
    rbtube::CouplingCapGridLeak coupleToPi;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpEL34 power;
    float gain=1, dvol=1, cvol=1, reverb=0, output=1, pwrDrive=1, cabSim=1;
    bool dirty=true;
public:
    void setSampleRate(float s) { fs=(s>0.f)?s:48000.f; vIn.setT(s); vDirty.setT(s); vClean.setT(s); }
    void reset() { vIn.reset(); vDirty.reset(); vClean.reset();
        hp.reset(); dBass.reset(); dMid.reset(); dTreble.reset(); cBass.reset(); cTreble.reset(); pwrLP.reset();
        cabHp.reset(); cabThump.reset(); cabLowMid.reset(); cabBite.reset(); cabFizz.reset(); cabLp.reset();
        coupleToDirty.reset(); coupleToClean.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset(); spring.reset(); }

    void setParams(const float* p) {
        dirty = p[kChannel] > 0.5f;
        hp.setHighpassQ(60.f, 0.7f, fs);

        // DIRTY channel: cascaded gain + 3-band stack (boost/cut about flat)
        gain = 0.6f + p[kGain] * p[kGain] * 8.0f;
        dBass.setLowShelf (110.f, (p[kBass]   - 0.5f) * 18.f, fs);
        dMid.setPeak      (650.f, (p[kMiddle] - 0.5f) * 14.f, 0.8f, fs);
        dTreble.setHighShelf(3200.f, (p[kTreble] - 0.5f) * 18.f, fs);
        dvol = p[kVolume] / 0.6f;

        // CLEAN channel: low drive + 2-band passive tone
        cBass.setLowShelf  (120.f, (p[kCleanBass]   - 0.5f) * 14.f, fs);
        cTreble.setHighShelf(3000.f,(p[kCleanTreble] - 0.5f) * 14.f, fs);
        cvol = p[kCleanVolume] / 0.5f;

        reverb   = p[kReverb];
        output   = p[kOutput] / 0.7f;
        pwrDrive = 0.5f + output * 0.85f;
        cabSim   = p[kCabSim];
        pwrLP.setLowpassQ(14000.f - 3000.f * output, 0.7f, fs);  // EL34 + OT band-limit (opened, miked-cab top)
        coupleToDirty.set(fs, 1000000.0f, 22.0e-9f, 180000.0f,
                          0.12f, 0.52f, 1.45f);
        coupleToClean.set(fs, 1000000.0f, 22.0e-9f, 220000.0f,
                          0.10f, 0.30f, 0.75f);
        coupleToPi.set(fs, 1000000.0f, 47.0e-9f, 47000.0f, 0.10f, 0.60f, 1.9f);
        phaseInverter.setMarshall(fs, 0.74f + 1.05f * output + (dirty ? 0.35f : 0.10f), 0.82f);
        supply.set(fs,
                   85.0f, 100.0f,
                   560.0f, 50.0f,
                   10000.0f, 22.0f,
                   0.14f, 0.10f, 0.050f, 0.17f);
        power.set(fs, 0.90f + 1.55f * output + (dirty ? 0.52f : 0.18f),
                  -35.0f, 0.12f, 58.0f, 11800.0f);
        power.out = 0.0105f;

        cabHp.setHighpassQ(78.f, 0.72f, fs);
        cabThump.setPeak(118.f, 1.1f + 2.1f * p[kBass], 0.86f, fs);
        cabLowMid.setPeak(430.f + 100.f * p[kMiddle], 1.8f, 0.72f, fs);
        cabBite.setPeak(2450.f + 480.f * p[kTreble], 2.0f + 1.5f * p[kTreble], 0.78f, fs);
        cabFizz.setHighShelf(4700.f, -1.0f + 2.2f * p[kTreble], fs);
        cabLp.setLowpassQ(12800.f + 1200.f * p[kTreble] - 1600.f * output, 0.66f, fs);
    }

    inline float process(float x) {
        float s = hp.process(x);
        s = (float)vIn.process((double)(2.5f * s));                  // shared input triode
        float ch;
        if (dirty) {
            float d = coupleToDirty.process(s, gain);
            d = (float)vDirty.process((double)d);                    // cascaded gain stage
            d = dBass.process(d); d = dMid.process(d); d = dTreble.process(d);
            ch = d * dvol;
        } else {
            float cln = coupleToClean.process(s, 1.2f);
            cln = (float)vClean.process((double)cln);                // gentle clean stage
            cln = cBass.process(cln); cln = cTreble.process(cln);
            ch = cln * cvol;
        }
        ch += spring.process(ch) * reverb * 0.6f;                    // valve spring blend
        const float load = std::fabs(ch) * (0.75f + 0.85f * output);
        const rbtube::SupplyScales bplus = supply.process(load, load * 0.52f, load * 0.20f);
        ch *= 0.92f + 0.08f * bplus.preamp;
        ch = coupleToPi.process(ch * pwrDrive * bplus.screen, 1.0f + 0.10f * output);
        ch = phaseInverter.process(ch) * bplus.screen;
        ch = power.process(ch * bplus.power) * output;               // 2x EL34 power
        ch = pwrLP.process(ch);
        const float ampOnly = ch;
        float cab = cabHp.process(ampOnly);
        cab = cabThump.process(cab);
        cab = cabLowMid.process(cab);
        cab = cabBite.process(cab);
        cab = cabFizz.process(cab);
        cab = cabLp.process(cab);
        return ampOnly + cabSim * (cab - ampOnly);
    }
};

static constexpr float kRumbleverbMakeup = 3.50f;   // tuned offline (-14 dBFS @ kDef)
static constexpr float kRumbleverbLvl    = 0.2647f;

class RumbleverbPlugin : public Plugin {
    RumbleverbChannel L, R;
    float fParams[kParamCount];
    void recalc() { L.setParams(fParams); R.setParams(fParams); }
public:
    RumbleverbPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRumbleverbDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "CitrusRumbleverb50"; }
    const char* getDescription() const override { return "Orange Rockerverb 50 MkII 2-channel guitar head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'R', 'V'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i == (uint32_t)kChannel) p.hints |= kParameterIsBoolean;
        p.name = kRumbleverbNames[i]; p.symbol = kRumbleverbSymbols[i];
        p.ranges.min = kRumbleverbMin[i]; p.ranges.max = kRumbleverbMax[i]; p.ranges.def = kRumbleverbDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(kRumbleverbLvl * softClip(kRumbleverbMakeup * L.process(3.2f * iL[i])) * 0.98f); oR[i] = rbAmpLvl(kRumbleverbLvl * softClip(kRumbleverbMakeup * R.process(3.2f * iR[i])) * 0.98f); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleverbPlugin)
};

Plugin* createPlugin() { return new RumbleverbPlugin(); }

END_NAMESPACE_DISTRHO
