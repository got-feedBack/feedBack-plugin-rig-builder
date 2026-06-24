/*
 * Peabey T-Max — Peavey T-Max "Two Channel Bass System", COMPONENT-LEVEL model.
 *
 * Built from the Peavey T-MAX BASS AMP PREAMP schematic (Peavey Electronics,
 * Meridian MS, drawing 70985) + the documented front panel:
 *   • INPUT     — Active/Passive jack (Active pads hot active basses)
 *   • TUBE ch   — 12AX7 (V1) preamp: Tube Pre Gain drives the grid, Tube Post
 *                 Gain sets the channel level (the warm, soft-clipping voice)
 *   • SOLID ST  — op-amp/JFET clean-punchy preamp: SS Pre Gain (rail-clips =
 *                 the panel's CLIP LED)
 *   • CHANNEL SELECT / COMBINE — run one channel, or SUM both (the T-Max's
 *                 signature blended dual preamp)
 *   • SHELVING  — Low (~100 Hz) + High (~4 kHz) +/-15 dB shelves
 *   • GRAPHIC EQ — 7 bands 40/100/250/625/1.6k/4k/10k Hz, +/-15 dB (In/Out)
 *   • BIAMP     — Balance tilts the low/high power sections about the X-Over
 *                 crossover (100 Hz..1 kHz); 300 W solid-state power amp
 *   • MASTER    — output level
 *
 * The 12AX7 is the REAL nodal triode (Koren plate law solved by Newton-Raphson
 * each sample = the asymmetric tube grit); the SS path is a nodal op-amp with
 * rail clipping; the graphic-EQ bands are nodal multiple-feedback band-passes;
 * the biamp crossover is a first-order complementary split (lo+hi reconstructs
 * flat) tilted by Balance. Shared building blocks (Mna/Tube/SsPre/RC1/MFB) are
 * byte-identical to the Sharke HB5000 source.
 */
#include "DistrhoPlugin.hpp"
#include "TMaxParams.h"
#include "../../_shared/oversampler.hpp"   // 2× OS around the real-tube + SS clip
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): the soft knee is
// transparent below +/-0.90 and saturates to a +/-0.99 ceiling so EQ boosts
// never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad (transposed direct form II) — the shelving stages ──────────────
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
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
};

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
// Node 0 = gnd; nodes 1..nN unknown voltages; nX aux currents. Resistors,
// capacitor companions, ideal op-amps and the transconductance stamp (gm) for
// nonlinear elements; solved by Gaussian elimination. (Same engine as HB5000.)
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
    inline void OpAmp(int np, int nnode, int no, int k) { int r = nn+k;
        if (no>0) A[(no-1)*sz+r] += 1; if (np>0) A[r*sz+(np-1)] += 1; if (nnode>0) A[r*sz+(nnode-1)] -= 1; }
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

// ── 12AX7 TUBE PREAMP — true nodal triode (Koren model + Newton/sample) ───────
// Common-cathode stage (B+ 300V, Rp 100k, Rk 1.5k self-bias). The plate swing
// clips ASYMMETRICALLY toward B+/cutoff = the warm tube grit. (Same as HB5000.)
struct Tube {
    double vG=0, vP=200, vK=1.4, dcAvg=200.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=200; vK=1.4; dcAvg=200.0; }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {
        const double Bp=300, Rp=100000, Rk=1500, h=1e-4;
        double G=vG, P=vP, K=vK;
        for (int it=0; it<12; ++it) {
            Mna m; m.init(4, 2);                // 1 B+, 2 grid, 3 plate, 4 cathode
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
        return -(P - dcAvg) * (1.0/40.0);                   // DC-blocked, in-phase, ~unity
    }
};

// ── SOLID-STATE PREAMP — nodal non-inverting op-amp (gain ~11), rail clip ─────
// The T-Max SS channel: clean/punchy until it rail-clips (the front-panel CLIP).
struct SsPre {
    inline double process(double vin) {
        const double R2=4700, R3=47000, Vrail=13.5;
        Mna m; m.init(3, 2); m.Vsrc(1,vin,0); m.R(2,0,R2); m.R(3,2,R3); m.OpAmp(1,2,3,1);
        double vo=0; if (m.solve()) vo=m.x[2];
        if (std::fabs(vo)>Vrail) { Mna m2; m2.init(3,2); m2.Vsrc(1,vin,0); m2.R(2,0,R2); m2.R(3,2,R3);
            m2.Vsrc(3, (vo>0?Vrail:-Vrail), 1); if (m2.solve()) vo=m2.x[2]; }
        return vo * (1.0/11.0);
    }
};

// ── One-pole RC filter solved nodally — the biamp crossover low-pass ──────────
struct RC1 {
    double C=1e-9, Rr=10000.0, vp=0.0, ip=0.0, T=1.0/48000.0; bool hp=false;
    void setT(float fs) { T = 1.0/((fs>0.f)?fs:48000.0); }
    void set(double fc, bool isHp) { hp=isHp; Rr=10000.0; C=1.0/(6.2831853*fc*Rr); }
    void reset() { vp=0.0; ip=0.0; }
    inline double proc(double in) {
        const double Geq=2.0*C/T, Ieq=Geq*vp+ip;
        Mna m; m.init(2,1); m.Vsrc(1,in,0);
        if (!hp) { m.R(1,2,Rr); m.stampG(2,0,Geq); m.Isrc(0,2,Ieq); }
        else     { m.stampG(1,2,Geq); m.Isrc(2,1,Ieq); m.R(2,0,Rr); }
        if (!m.solve()) return 0.0;
        const double vo=m.x[1], vc = hp ? (m.x[0]-m.x[1]) : m.x[1];
        const double i=Geq*(vc-vp)-ip; ip=i; vp=vc; return vo;
    }
};

// ── Multiple-feedback band-pass — one graphic-EQ band, solved nodally ─────────
struct MFB {
    double R1=1, R2=1, R3=1, Cc=1e-8, c1v=0, c1i=0, c2v=0, c2i=0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0/((fs>0.f)?fs:48000.0); }
    void set(double fc, double Q, double C) { Cc=C; const double w=6.2831853*fc;
        R1=Q/(w*C); R2=Q/((2*Q*Q-1.0)*w*C); R3=2*Q/(w*C); }
    void reset() { c1v=c1i=c2v=c2i=0; }
    inline double proc(double in) {
        const double G=2.0*Cc/T, I1=G*c1v+c1i, I2=G*c2v+c2i;
        Mna m; m.init(4,2);                            // 1 in, 2 n1, 3 out, 4 VG
        m.Vsrc(1,in,0); m.R(1,2,R1); m.R(2,0,R2); m.R(3,4,R3);
        m.stampG(2,4,G); m.Isrc(4,2,I1);
        m.stampG(2,3,G); m.Isrc(3,2,I2);
        m.OpAmp(0,4,3,1);
        if (!m.solve()) return 0.0;
        const double out=m.x[2];
        { const double v=m.x[1]-m.x[3], i=G*(v-c1v)-c1i; c1i=i; c1v=v; }
        { const double v=m.x[1]-m.x[2], i=G*(v-c2v)-c2i; c2i=i; c2v=v; }
        return out;
    }
};

class TMaxChannel {
    float fs = 48000.f;
    Tube tube; SsPre ss;                     // dual preamp: real 12AX7 + op-amp SS
    Biquad shLow, shHigh;                    // Shelving Low / High (+/-15 dB)
    MFB eq[kNumEq]; float eqG[kNumEq];        // 7-band nodal graphic EQ
    RC1 xover;                                // biamp crossover low-pass
    float tubeDrive=1, tubePost=1, ssDrive=1, master=1;
    float gLow=1, gHigh=1;                     // biamp Balance tilt
    bool combine=true, chanTube=true, graphicIn=true;
public:
    void setSampleRate(float s){ fs=(s>0)?s:48000.f; tube.setT(s); xover.setT(s); xover.set(316.0,false);
        for (int i=0;i<kNumEq;++i){ eq[i].setT(s); eq[i].set(kEqFreqs[i], 1.1, 1e-8); eqG[i]=1.f; } }
    void reset(){ tube.reset(); shLow.reset(); shHigh.reset(); xover.reset();
        for (int i=0;i<kNumEq;++i) eq[i].reset(); }

    void setParams(const float* p) {
        const float pad = (p[kActive] > 0.5f) ? 0.35f : 1.0f;          // Active jack pad (~-9 dB)
        // Drive curves: clean-ish at noon, overdrives near the top. Tube Pre
        // grid-drives the 12AX7; Tube Post is the channel level; SS Pre drives
        // the op-amp toward its rails (CLIP).
        tubeDrive = p[kTubePre] * (0.6f + p[kTubePre] * p[kTubePre] * 16.0f) * pad;
        tubePost  = p[kTubePost] * 1.4f;
        ssDrive   = p[kSsPre]  * (0.6f + p[kSsPre]  * p[kSsPre]  * 16.0f) * pad;

        combine   = p[kChanCombine] > 0.5f;
        chanTube  = p[kChanSel]     > 0.5f;                            // 1 = Tube, 0 = Solid State
        graphicIn = p[kGraphicIn]   > 0.5f;

        shLow.setLowShelf (100.f, (p[kShelfLow]  - 0.5f) * 30.f, fs);  // +/-15 dB
        shHigh.setHighShelf(4000.f, (p[kShelfHigh] - 0.5f) * 30.f, fs);

        for (int i=0;i<kNumEq;++i)
            eqG[i] = graphicIn ? std::pow(10.f, (p[kFirstEq+i]-0.5f)*30.f/20.f) : 1.f;  // +/-15 dB

        // Biamp crossover 100 Hz .. 1 kHz (log); Balance tilts lows vs highs.
        xover.set(100.0 * std::pow(10.0, (double)p[kXover]), false);
        const float bal = p[kBalance];
        gLow  = 1.f + (0.5f - bal) * 1.5f;
        gHigh = 1.f + (bal - 0.5f) * 1.5f;

        master = p[kMaster] / 0.7f;
    }

    inline float process(float x) {
        // 1-2. TUBE channel: 12AX7 grid drive -> Post level
        const double t = tube.process((double)(tubeDrive * x)) * tubePost;
        // 3. SOLID STATE channel
        const double sst = ss.process((double)(ssDrive * x));
        // 4. Channel Select / Combine
        double s = combine ? (t + sst) * 0.6 : (chanTube ? t : sst);
        // 5. Shelving Low / High
        s = shHigh.process(shLow.process((float)s));
        // 6. Graphic EQ — parallel nodal MFB bands summed onto the dry signal
        //    (the MFB band-pass is inverting, so subtract for a proper boost).
        if (graphicIn) { const double dry=s; double sum=dry;
            for (int i=0;i<kNumEq;++i) sum -= (eqG[i]-1.0) * eq[i].proc(dry); s = sum; }
        // 7. Biamp: first-order complementary split tilted by Balance (lo+hi = flat)
        const double lo = xover.proc(s), hi = s - lo;
        s = lo * gLow + hi * gHigh;
        // 8. Master
        return (float)(s * master);
    }
};

// Loudness standard: output makeup tuned so the multitone (110 Hz–1.8 kHz) RMS
// matches the cross-amp reference, then a tanh soft-clip * 0.98 ceiling.
static constexpr float kTMaxMakeup = 4.98f;   // tuned offline to ~-15 dBFS multitone @ noon

class TMaxPlugin : public Plugin {
    TMaxChannel L, R;
    float fParams[kParamCount];
    rbshared::Oversampler4x osL, osR;            // 2× anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    void recalc(){ L.setParams(fParams); R.setParams(fParams); }
public:
    TMaxPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kTMaxDef[i];
        const float sr=(float)getSampleRate();
        L.setSampleRate(kOS*sr); R.setSampleRate(kOS*sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "PeeBeeTMinus"; }
    const char* getDescription() const override { return "Peavey T-Max two-channel bass head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'T', 'M'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kActive) p.hints |= kParameterIsBoolean;
        p.name = kTMaxNames[i]; p.symbol = kTMaxSymbols[i];
        p.ranges.min = kTMaxMin[i]; p.ranges.max = kTMaxMax[i]; p.ranges.def = kTMaxDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate(kOS*(float)r); R.setSampleRate(kOS*(float)r); osL.reset(); osR.reset(); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){
            float ubL[kOS], ubR[kOS];
            osL.upsample(iL[i], ubL); osR.upsample(iR[i], ubR);
            for (int k=0;k<kOS;++k){
                ubL[k]=rbAmpLvl(0.3007f*softClip(kTMaxMakeup*L.process(ubL[k]))*0.98f);
                ubR[k]=rbAmpLvl(0.3007f*softClip(kTMaxMakeup*R.process(ubR[k]))*0.98f);
            }
            oL[i]=osL.downsample(ubL); oR[i]=osR.downsample(ubR);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TMaxPlugin)
};

Plugin* createPlugin() { return new TMaxPlugin(); }

END_NAMESPACE_DISTRHO
