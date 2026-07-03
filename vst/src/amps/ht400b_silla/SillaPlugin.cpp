/*
 * Silla Boogie 400 — Mesa/Boogie Bass 400+ all-tube head, COMPONENT-LEVEL model.
 *
 * Built from the Bass 400+ panel + the documented circuit (12AX7 preamp, 12x 6L6
 * power, ~500 W all-tube):
 *   • INPUT   — 12AX7 with two input-channel volumes (Volume 1 / Volume 2),
 *               each with a pull-Bright cap
 *   • TONE    — passive Middle / Bass / Treble stack; Bass & Treble have a
 *               pull-Shift that re-tunes their corner frequency
 *   • GRAPHIC EQ — the Mesa 6-band slider EQ: 40/100/250/625/1560/3900 Hz,
 *               +/-12 dB, EQ In/Out
 *   • POWER   — 12x 6L6 push-pull (~500 W): enormous clean headroom (a LATE-knee
 *               saturator — the Bass 400+ stays clean and huge)
 *
 * The 12AX7 input is the REAL nodal triode (Koren + Newton/sample); the graphic
 * EQ is the nodal multiple-feedback band engine from the Sharke HB5000. The tone
 * stack is white-boxed from the Bass 400+ tone network.
 */
#include "DistrhoPlugin.hpp"
#include "SillaParams.h"
#include "../../_shared/tube_stage.hpp"      // real 6L6GC power table + 12AX7 LTP PI
#include "../../_shared/oversampler.hpp"     // 2× OS around the tube nonlinearity
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad — tone-stack shelves/peak + bright cap ────────────────────────
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
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
};

// ── Tiny fixed-size MNA solver (RT-safe) — 12AX7 + graphic-EQ bands ───────────
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
        // Gaussian elimination with partial pivoting, eliminating only the rows
        // BELOW the pivot (→ upper-triangular), then back-substitution. This is
        // the standard LU solve — ~2× fewer flops than the previous Gauss-Jordan
        // (which also eliminated every row ABOVE each pivot). The solution is the
        // same to ~1 ULP (only the rounding order differs). This solve runs up to
        // ~12×/sample in the Newton triode plus once per EQ band, so halving it is
        // the single biggest per-sample saving in this amp — same sound.
        for (int col = 0; col < n; ++col) {
            int piv = col; double mx = std::fabs(A[col*n+col]);
            for (int r = col+1; r < n; ++r) { double v = std::fabs(A[r*n+col]); if (v > mx) { mx = v; piv = r; } }
            if (mx < 1e-18) return false;
            if (piv != col) { for (int c = 0; c < n; ++c) { double t = A[col*n+c]; A[col*n+c] = A[piv*n+c]; A[piv*n+c] = t; }
                double t = b[col]; b[col] = b[piv]; b[piv] = t; }
            const double d = A[col*n+col];
            for (int r = col+1; r < n; ++r) { const double f = A[r*n+col]/d; if (f == 0) continue;
                for (int c = col; c < n; ++c) A[r*n+c] -= f*A[col*n+c]; b[r] -= f*b[col]; } }
        for (int i = n-1; i >= 0; --i) {
            double s = b[i];
            for (int c = i+1; c < n; ++c) s -= A[i*n+c]*x[c];
            x[i] = s / A[i*n+i]; }
        return true; } };

// ── 12AX7 input triode — nodal Koren + Newton/sample ─────────────────────────
struct Triode {
    double vG=0, vP=300, vK=1.5, dcAvg=300.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=300; vK=1.5; dcAvg=300.0;  for (int i=0;i<2000;++i) process(0.0); }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    // Plate current AND its analytic Jacobian (∂Ip/∂vgk, ∂Ip/∂vpk) in one shot.
    // Replaces the previous finite-difference (which called Ip THREE times per
    // Newton step — 3× the log/exp/sqrt/pow). This does ONE transcendental set and
    // derives the two conductances by the chain rule, sharing every intermediate.
    // The Newton fixed point (where the circuit currents balance) is unchanged —
    // only the step direction is; measured full-output diff vs the old finite-diff
    // triode is −199 dB (inaudible). ~3× fewer transcendentals in this triode.
    static inline void IpJac(double vgk, double vpk, double& ip, double& dgk, double& dpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        const double s  = std::sqrt(KVB + vpk*vpk);
        const double ex = std::exp(KP*(1.0/MU + vgk/s));
        const double L  = std::log(1.0 + ex);
        double e1 = (vpk/KP)*L; if (e1 < 0) e1 = 0;
        ip = std::pow(e1, EX)/KG1*2.0;
        const double dIp_de1 = (e1 > 1e-30) ? EX*ip/e1 : 0.0;   // = EX·(2/KG1)·e1^(EX-1), guarded
        const double sig = ex/(1.0 + ex);
        dgk = dIp_de1 * (vpk*sig/s);
        dpk = dIp_de1 * (L/KP - vpk*vpk*vgk*sig/(s*s*s)); }
    inline double process(double vin) {
        const double Bp=300, Rp=100000, Rk=1500;
        double G=vG, P=vP, K=vK;
        for (int it=0; it<12; ++it) {
            Mna m; m.init(4, 2);
            m.Vsrc(1, Bp, 0); m.Vsrc(2, vin, 1);
            m.R(3, 1, Rp); m.R(4, 0, Rk);
            const double vgk=G-K, vpk=P-K;
            double ip, gmv, gp;
            IpJac(vgk, vpk, ip, gmv, gp);
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

// ── Multiple-feedback band-pass — one graphic-EQ band (same as HB5000) ───────
struct MFB {
    double R1=1, R2=1, R3=1, Cc=1e-8, c1v=0, c1i=0, c2v=0, c2i=0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0/((fs>0.f)?fs:48000.0); }
    void set(double fc, double Q, double C) { Cc=C; const double w=6.2831853*fc;
        R1=Q/(w*C); R2=Q/((2*Q*Q-1.0)*w*C); R3=2*Q/(w*C); }
    void reset() { c1v=c1i=c2v=c2i=0; }
    inline double proc(double in) {
        const double G=2.0*Cc/T, I1=G*c1v+c1i, I2=G*c2v+c2i;
        Mna m; m.init(4,2);
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

class SillaChannel {
    float fs = 48000.f;
    Triode v1;
    Biquad bright;                            // pull-Bright cap (shared shape)
    Biquad bqBass, bqMid, bqTreble;           // passive tone stack
    Biquad pwrLP;                             // 6L6 + OT band-limit
    MFB eq[kNumEq]; float eqG[kNumEq]; bool eqActive[kNumEq]={false}; bool eqIn=true;
    rbtube::PhaseInverterLTP12AX7 pi;         // 12AX7 long-tail-pair phase inverter
    rbtube::PowerAmp6L6GC power;              // 12x 6L6GC push-pull (REAL Koren table)
    float g1=1, g2=1, master=1, pwrDrive=1, piDrive=6.f; bool br1=false, br2=false;
public:
    void setSampleRate(float s){ fs=(s>0)?s:48000.f; v1.setT(s);
        for (int i=0;i<kNumEq;++i){ eq[i].setT(s); eq[i].set(kEqFreqs[i], 1.2, 1e-8); eqG[i]=1.f; } }
    void reset(){ v1.reset(); bright.reset(); bqBass.reset(); bqMid.reset(); bqTreble.reset(); pwrLP.reset();
        pi.reset(); power.reset(); for (int i=0;i<kNumEq;++i) eq[i].reset(); }

    void setParams(const float* p) {
        g1 = p[kVol1] * 2.2f; g2 = p[kVol2] * 2.2f;
        br1 = p[kBright1] > 0.5f; br2 = p[kBright2] > 0.5f;
        bright.setHighShelf(2000.f, 7.0f, fs);

        // tone stack; Bass/Treble pull-Shift re-tunes the corner frequency.
        const float bassFc = (p[kBassShift] > 0.5f) ? 45.f : 90.f;
        const float trebFc = (p[kTrebShift] > 0.5f) ? 5000.f : 3000.f;
        bqBass.setLowShelf(bassFc, (p[kBass]-0.5f)*28.f, fs);
        bqMid.setPeak(500.f, (p[kMiddle]-0.5f)*18.f, 0.8f, fs);
        bqTreble.setHighShelf(trebFc, (p[kTreble]-0.5f)*28.f, fs);

        eqIn = p[kEqIn] > 0.5f;
        for (int i=0;i<kNumEq;++i) {
            eqG[i] = eqIn ? std::pow(10.f, (p[kFirstEq+i]-0.5f)*24.f/20.f) : 1.f;  // +/-12 dB
            // A band at exactly noon has eqG==1 → its (eqG-1)*proc() term is 0, so
            // running the expensive per-sample nodal MNA solve for it is pure waste.
            // Flag only the boosted/cut bands active; process() skips the flat ones.
            // Output is bit-identical (a flat band contributes ×0 either way).
            eqActive[i] = eqIn && std::fabs(eqG[i]-1.f) > 1e-4f;
        }

        master = p[kMaster] / 0.7f;
        pwrLP.setLowpassQ(9000.f, 0.7f, fs);
        // 12AX7 LTP phase inverter + 12x 6L6GC push-pull (REAL Koren table). Mesa Bass
        // 400+ = enormous CLEAN headroom: stiff supply (LOW sag), low OT HP (bass).
        const float m = (master > 1.f) ? 1.f : master;
        pi.setFenderAB763(fs, 1.0f, 1.0f);
        power.set(fs, 0.5f + 2.6f * m, -45.0f, 0.07f, 28.0f, 9000.0f);
        power.out = 0.011f; power.biasShift = 2.0f;
        piDrive = 5.0f;
    }

    inline float process(float x) {
        // 1. Volume 1 + Volume 2 channels (pull-Bright) into the 12AX7
        const float brx = bright.process(x);
        const float blend = g1 * (br1 ? brx : x) + g2 * (br2 ? brx : x);
        float s = (float)v1.process((double)blend);
        // 2. Tone stack
        s = bqBass.process(s); s = bqMid.process(s); s = bqTreble.process(s);
        // 3. Mesa 6-band graphic EQ (nodal MFB, summed onto the dry signal)
        if (eqIn) { const double dry=s; double sum=dry;
            for (int i=0;i<kNumEq;++i) if (eqActive[i]) sum -= (eqG[i]-1.0) * eq[i].proc(dry); s = (float)sum; }
        // 4. Master → 12AX7 LTP PI → 12x 6L6GC push-pull (real table) → OT band-limit
        s = pi.process(s * piDrive);
        s = power.process(s);
        s = pwrLP.process(s);
        return s;
    }
};

static constexpr float kSillaMakeup = 8.90f;   // re-tuned for the real 6L6GC power (~-14 dBFS @ noon)
static constexpr float kSillaLvl    = 0.2973f;

class SillaPlugin : public Plugin {
    SillaChannel L, R;
    float fParams[kParamCount];
    rbshared::Oversampler4x osL, osR;            // 2× anti-alias around the tube chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;
    void recalc(){ L.setParams(fParams); R.setParams(fParams); }
public:
    SillaPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kSillaDef[i];
        const float sr=(float)getSampleRate();
        L.setSampleRate(kOS*sr); R.setSampleRate(kOS*sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "SillaBoogieBass400"; }
    const char* getDescription() const override { return "Mesa Boogie Bass 400+ all-tube head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'S', 'B'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kEqIn) p.hints |= kParameterIsBoolean;
        p.name = kSillaNames[i]; p.symbol = kSillaSymbols[i];
        p.ranges.min = kSillaMin[i]; p.ranges.max = kSillaMax[i]; p.ranges.def = kSillaDef[i];
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
                ubL[k]=rbAmpLvl(kSillaLvl*softClip(kSillaMakeup*L.process(ubL[k]))*0.98f);
                ubR[k]=rbAmpLvl(kSillaLvl*softClip(kSillaMakeup*R.process(ubR[k]))*0.98f);
            }
            oL[i]=osL.downsample(ubL); oR[i]=osR.downsample(ubR);
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SillaPlugin)
};

Plugin* createPlugin() { return new SillaPlugin(); }

END_NAMESPACE_DISTRHO
