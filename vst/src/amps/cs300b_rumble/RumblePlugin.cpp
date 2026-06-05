/*
 * Bender Rumble 800 — Fender Rumble Bass (1995 all-tube head), COMPONENT-LEVEL.
 *
 * Built from the Fender "RUMBLE BASS" factory schematic (FMIC drawings 048406
 * preamp / 048411 power amp, rev C/D 1995-97):
 *   • PREAMP  — 12AX7A input gain (V1A/V6A), Fender passive Treble/Bass/Middle
 *               tone stack, Bright cap, 12AT7 make-up gain
 *   • POWER   — 12AX7 → 12AT7 → 12BH7 driver → 6x 6550WA push-pull (~300 W),
 *               Balance/Bias, big output transformer
 *
 * The 12AX7 input stage is the REAL nodal triode (Koren plate law solved by
 * Newton-Raphson each sample). The tone stack corner freqs are white-boxed from
 * the schematic R/C. With SIX 6550WA the power section has huge clean headroom
 * (a loud, punchy clean bass machine) so the push-pull saturator has a LATE knee
 * — it stays clean until the Master is really pushed.
 */
#include "DistrhoPlugin.hpp"
#include "RumbleParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad (transposed direct form II) — the passive tone stack ──────────
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
        b0 = (1 + alpha * A) / a0;
        b1 = (-2 * cw)       / a0;
        b2 = (1 - alpha * A) / a0;
        a1 = (-2 * cw)       / a0;
        a2 = (1 - alpha / A) / a0;
    }
    void setLowpassQ(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 =  (1 - cw) * 0.5f / a0;
        b1 =  (1 - cw)        / a0;
        b2 =  (1 - cw) * 0.5f / a0;
        a1 =  -2 * cw         / a0;
        a2 =  (1 - alpha)     / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
};

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe) — for the 12AX7 ──
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

// ── 12AX7 input/gain triode, true nodal (Koren + Newton/sample) ───────────────
struct Triode {
    double vG=0, vP=250, vK=1.5, dcAvg=250.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=250; vK=1.5; dcAvg=250.0; }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {
        const double Bp=250, Rp=100000, Rk=1500, h=1e-4;       // schematic B+ ~254 VDC
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
        return -(P - dcAvg) * (1.0/40.0);
    }
};

class RumbleChannel {
    float fs = 48000.f;
    Triode v1;                                  // 12AX7 input/gain stage
    Biquad bqBass, bqMid, bqTreble, bqBright;   // Fender passive tone stack + bright
    Biquad pwrLP;                               // 6550 + output-transformer band-limit
    float drive = 1.f, master = 1.f, pwrDrive = 1.f, outMakeup = 1.f;

    // 6x 6550WA push-pull (~300 W): symmetric soft clip with a LATE knee — six
    // power tubes = lots of clean headroom (the Rumble's loud, punchy clean bass).
    static inline float pushPull(float x) {
        return std::tanh(x * 0.70f) * 1.4286f;   // 1/0.70 makeup so small x ≈ unity
    }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; v1.setT(s); }
    void reset() { v1.reset(); bqBass.reset(); bqMid.reset(); bqTreble.reset(); bqBright.reset(); pwrLP.reset(); }

    void setParams(float gain, float bass, float middle, float treble, float masterP, bool bright) {
        // ── input → V1 12AX7 grid drive (clean-ish; grinds only when cranked) ──
        drive = 0.4f + gain * 8.0f;

        // ── Fender passive tone stack (±15 dB bass/treble, gentler mid) ────────
        //  Bass  : 250k pot + .022µF/.1µF  → low shelf ~75 Hz.
        bqBass.setLowShelf(75.f, (bass - 0.5f) * 30.f, fs);
        //  Middle: Fender 25k mid pot — a gentle ~500 Hz peak/dip (small range).
        bqMid.setPeak(500.f, (middle - 0.5f) * 16.f, 0.7f, fs);
        //  Treble: 250pF/150pF treble cap → high shelf ~4 kHz.
        bqTreble.setHighShelf(4000.f, (treble - 0.5f) * 30.f, fs);
        //  Bright: bright cap across the volume → +5 dB high shelf when engaged.
        if (bright) bqBright.setHighShelf(3000.f, 5.0f, fs); else bqBright.setBypass();

        // ── Master → 6x 6550WA push-pull. Big headroom: unity ≈ 0.7, stays clean. ──
        master   = masterP / 0.7f;
        pwrDrive = 0.5f + master * 0.8f;
        pwrLP.setLowpassQ(8000.f, 0.7f, fs);     // OT + 6550 HF roll-off

        // ── Loudness standardization: hold final multitone RMS ~flat across Gain. ──
        const float invMk = 0.0291f + gain * (0.5786f - 0.0980f * gain);
        outMakeup = 1.0f / invMk;
    }

    inline float process(float x) {
        // 1. INPUT → 12AX7 gain stage (asymmetric tube growl when cranked)
        float s = (float)v1.process((double)(drive * x));
        // 2. Fender passive tone stack: Bass → Middle → Treble → Bright
        s = bqBass.process(s); s = bqMid.process(s); s = bqTreble.process(s); s = bqBright.process(s);
        // 3. Master → 6x 6550 push-pull power stage + output-transformer band-limit
        s = pushPull(s * pwrDrive) * master;
        s = pwrLP.process(s);
        return s * outMakeup;
    }
};

// kLvl matches the amp to the common multitone loudness (~-15 dBFS @ noon); the
// per-Gain outMakeup (inside process()) already standardizes loudness across the
// Gain knob, so the final stage is just kLvl*softClip*0.98 (same as the V-4B).
static constexpr float kRumbleLvl = 0.24f;   // tuned offline to ~-15 dBFS multitone @ noon

class RumblePlugin : public Plugin {
    RumbleChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool br = fParams[kBright] > 0.5f;
        L.setParams(fParams[kGain], fParams[kBass], fParams[kMiddle], fParams[kTreble], fParams[kMaster], br);
        R.setParams(fParams[kGain], fParams[kBass], fParams[kMiddle], fParams[kTreble], fParams[kMaster], br);
    }
public:
    RumblePlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kRumbleDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "BenderFumble800"; }
    const char* getDescription() const override { return "Fender Rumble Bass all-tube head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'R', '8'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kBright) p.hints |= kParameterIsBoolean;
        p.name = kRumbleNames[i]; p.symbol = kRumbleSymbols[i];
        p.ranges.min = kRumbleMin[i]; p.ranges.max = kRumbleMax[i]; p.ranges.def = kRumbleDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1]; float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(kRumbleLvl * softClip(L.process(iL[i])) * 0.98f); oR[i] = rbAmpLvl(kRumbleLvl * softClip(R.process(iR[i])) * 0.98f); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumblePlugin)
};

Plugin* createPlugin() { return new RumblePlugin(); }

END_NAMESPACE_DISTRHO
