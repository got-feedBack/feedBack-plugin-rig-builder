/*
 * Sampleg SBT-CL — Ampeg SVT-CL all-tube bass head, COMPONENT-LEVEL model.
 *
 * Built from the factory service schematics (the user's scans):
 *   • PRE AMP  — Ampeg 07S519-03 (SVT-2PRO / IIPRO / SVT-CL shared preamp board)
 *   • TUBE Bd. — Ampeg 07S419-41 (six 6550A output tubes, push-pull)
 *   • POWER AMP — Ampeg 07S419-04/-12 (12AX7 driver / 12AU7 PI, NFB)
 *
 * The SVT-CL is the depopulated "Classic" variant of that board: it keeps the
 * 12AX7 gain stage, the Ultra Lo / Ultra Hi switched contours and the passive
 * Bass / Midrange(+Frequency) / Treble tone stack, and drops the SVT-2PRO's
 * footswitchable Drive channel and 9-band graphic EQ. So we model exactly the
 * SVT-CL front-panel path:
 *
 *   IN ─[R1 47k / −15dB pad R2 27k]─► V1 12AX7 gain (nodal triode, P1 250kA Gain)
 *      ─► Ultra Lo contour (S2A)  ─► Bass/Mid/Treble tone stack
 *      ─► Ultra Hi presence (S4A) ─► Master (P6 250kA) ─► 6550 push-pull power
 *
 * Unlike the previous behavioural build (a generic tanh + RBJ shelves), the
 * gain stage here is the REAL 12AX7: the Koren plate-current law solved by
 * Newton-Raphson every sample (asymmetric grid/cutoff clipping = the SVT growl).
 * The tone-shaping corner frequencies, Qs and depths are WHITE-BOXED from the
 * actual R/C/L on the schematic (designators cited inline). The power stage is
 * the 6550 push-pull: symmetric (even harmonics cancel) soft compression that
 * only bites near full Master, plus the output-transformer band-limit.
 */
#include "DistrhoPlugin.hpp"
#include "SvtParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.30 RMS at real settings); the soft
// knee is transparent below +/-0.80 and saturates to a +/-0.98 ceiling so EQ
// boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.80f,c=0.98f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization (shared amp convention): a per-amp output `makeup`
// tuned so the multitone (110 Hz–1.8 kHz) RMS matches the Box DC30 reference
// (~0.40 RMS), then softClip()*0.98f as the final ceiling (same as EN30Core.h).
static inline float softClip(float x) { return std::tanh(x); }

// ── RBJ biquad (transposed direct form II) ───────────────────────────────────
// White-box tone filters: each section's fc/Q/dB is computed from the real
// schematic components, not tuned by ear.
class Biquad {
    float b0=1, b1=0, b2=0, a1=0, a2=0;
    float z1=0, z2=0;
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

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
// (Same engine the FK 800BR / Sharke HB3500 use.) Node 0 = gnd; nodes 1..nN are
// unknown voltages, nX aux currents. Resistor + transconductance (gm) stamps;
// solved by Gaussian elimination. Validated on the 12AX7 triode.
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

// ── V1 — 12AX7 input/gain triode, true nodal (Koren + Newton/sample) ──────────
// SVT-CL preamp first stage: common-cathode 12AX7 (V1B on 07S519-03). Plate
// supply ~200V (the schematic's "200V" node), plate load Rp ≈ 100k (R7 100k
// 1/2W), self-bias Rk ≈ 1.5k with a 22µF cathode bypass (C2) → ~1.5V cathode,
// which the schematic confirms. The Koren plate-current law Ip(Vgk,Vpk) is
// solved by Newton-Raphson each sample; the plate swing clips asymmetrically
// (toward B+ / cutoff) — the even-harmonic SVT growl when Gain is pushed.
struct Triode {
    double vG=0, vP=200, vK=1.5, dcAvg=200.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=200; vK=1.5; dcAvg=200.0; }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {        // vin = grid drive (V); returns AC plate swing
        const double Bp=200, Rp=100000, Rk=1500, h=1e-4;
        double G=vG, P=vP, K=vK;
        for (int it=0; it<12; ++it) {
            Mna m; m.init(4, 2);                // 1 B+, 2 grid, 3 plate, 4 cathode
            m.Vsrc(1, Bp, 0); m.Vsrc(2, vin, 1);
            m.R(3, 1, Rp); m.R(4, 0, Rk);       // bypassed cathode → flat low end, tube clip
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
        // common-cathode plate is INVERTING; negate so the stage is in phase.
        return -(P - dcAvg) * (1.0/40.0);                   // DC-blocked, ~unity small-signal
    }
};

class SvtChannel {
    float fs = 48000.f;
    Triode v1;                       // nodal 12AX7 input/gain stage (the growl)
    Biquad ulLow, ulMid, ulHigh;     // Ultra Lo contour (S2A network)
    Biquad uhShelf;                  // Ultra Hi presence (S4A network)
    Biquad bqBass, bqMid, bqTreble;  // passive tone stack (P3/Mid/P5)
    Biquad pwrLP;                    // 6550 + output transformer band-limit
    float drive = 1.f, master = 1.f, pwrDrive = 1.f, outMakeup = 1.f;

    // 6550 push-pull: symmetric soft clip (even harmonics cancel in push-pull),
    // generous headroom — only compresses near full Master, with NFB-style give.
    static inline float pushPull(float x) {
        // odd-symmetric saturator; ~linear until |x|→1, then soft-knee compress.
        return std::tanh(x * 0.8f) * 1.2533f;   // 1/tanh-ish makeup so small x ≈ unity
    }
public:
    void setSampleRate(float s) { fs = (s > 0.f) ? s : 48000.f; v1.setT(s); }
    void reset() {
        v1.reset();
        ulLow.reset(); ulMid.reset(); ulHigh.reset(); uhShelf.reset();
        bqBass.reset(); bqMid.reset(); bqTreble.reset(); pwrLP.reset();
    }

    void setParams(float gain, float bass, float midrange, float freq,
                   float treble, float masterP,
                   bool pad, bool ultraLo, bool ultraHi) {
        // ── input / V1 grid drive. The −15 dB jack (R2 27k pad) cuts ~5.6× ──
        const float padScale = pad ? 0.178f : 1.0f;            // −15 dB
        // P1 250kA Gain: clean-ish at low settings, drives the 12AX7 into grid
        // clipping near the top (the SVT growl). Grid swing ~0.5..9 V.
        drive = (0.5f + gain * gain * 8.5f) * padScale;

        // ── Ultra Lo (S2A): the SVT fixed "loudness" contour. The switched
        //    network (R19/R20/R21/R22 220k + C7 470p + C8 .0047µF) lifts the
        //    deep lows and the top while scooping the low-mids → a smile curve.
        if (ultraLo) {
            ulLow.setLowShelf(50.f, 6.0f, fs);                // deep-low lift
            ulMid.setPeak(500.f, -9.0f, 0.8f, fs);            // .0047µF·220k ≈ low-mid scoop
            ulHigh.setHighShelf(8000.f, 4.0f, fs);            // 470pF top lift
        } else { ulLow.setBypass(); ulMid.setBypass(); ulHigh.setBypass(); }

        // ── Ultra Hi (S4A): presence/bite. R37 270k / R38 18k + C13 .033µF /
        //    C15 220pF inject a high-shelf boost ≈ 4 kHz (the SVT clank). ──
        if (ultraHi) uhShelf.setHighShelf(4000.f, 6.0f, fs); else uhShelf.setBypass();

        // ── Passive tone stack (±15 dB, 0.5 = flat) ──────────────────────────
        //  Bass    : P3 1MA + C20 .001µF / C21 .01µF  → low shelf ~70 Hz.
        bqBass.setLowShelf(70.f, (bass - 0.5f) * 30.f, fs);
        //  Midrange: inductor band (L1 ≈ 0.8 H) resonating with the FREQUENCY-
        //            switch cap (S3): .68/.15/.047/.012/.0033 µF → fc = 1/(2π√LC)
        //            = 220 / 450 / 800 / 1600 / 3000 Hz. Mid pot (50 kL) sets the
        //            peak/notch depth; the LC gives the SVT's broad-ish Q.
        int sel = (int)(freq * 5.0f); if (sel > 4) sel = 4; if (sel < 0) sel = 0;
        bqMid.setPeak(kSvtMidFreqs[sel], (midrange - 0.5f) * 28.f, 0.7f, fs);
        //  Treble  : P5 1MA + C26 15pF / C27 100pF     → high shelf ~5 kHz.
        bqTreble.setHighShelf(5000.f, (treble - 0.5f) * 30.f, fs);

        // ── Master → 6550 push-pull drive. Unity ≈ 0.7; pushing past it leans
        //    the output stage into soft compression (power-amp "give"). ──
        master   = masterP / 0.7f;
        pwrDrive = 0.5f + master * 0.7f;                      // into the saturator
        // Output transformer + 6550 HF roll-off (the amp is dark up top).
        pwrLP.setLowpassQ(9000.f, 0.7f, fs);
        // ── Loudness standardization: gain-dependent output makeup so the
        //    multitone RMS stays ~flat at the Box DC30 reference (~0.40) across
        //    Gain. The SVT level rises steeply with Gain, so makeup falls with it.
        outMakeup = 0.44f + 13.6f * std::exp(-3.05f * gain);
    }

    inline float process(float x) {
        // 1. INPUT → V1 12AX7 gain stage — real nodal triode (asymmetric growl)
        float s = (float)v1.process((double)(drive * x));
        // 2. Ultra Lo contour (switched)
        s = ulLow.process(s); s = ulMid.process(s); s = ulHigh.process(s);
        // 3. Passive tone stack: Bass → Mid(freq) → Treble
        s = bqBass.process(s); s = bqMid.process(s); s = bqTreble.process(s);
        // 4. Ultra Hi presence (switched)
        s = uhShelf.process(s);
        // 5. Master → 6550 push-pull power stage + output-transformer band-limit
        s = pushPull(s * pwrDrive) * master;
        s = pwrLP.process(s);
        return s * outMakeup;
    }
};

class SvtPlugin : public Plugin {
    SvtChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool pad = fParams[kPad] > 0.5f, ul = fParams[kUltraLo] > 0.5f, uh = fParams[kUltraHi] > 0.5f;
        L.setParams(fParams[kGain], fParams[kBass], fParams[kMidrange], fParams[kFreq],
                    fParams[kTreble], fParams[kMaster], pad, ul, uh);
        R.setParams(fParams[kGain], fParams[kBass], fParams[kMidrange], fParams[kFreq],
                    fParams[kTreble], fParams[kMaster], pad, ul, uh);
    }
public:
    SvtPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kSvtDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "SamplegSBTCL"; }
    const char* getDescription() const override { return "Ampeg SVT-CL all-tube bass head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(2, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'S', 'v'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kSvtNames[i]; p.symbol = kSvtSymbols[i];
        p.ranges.min = kSvtMin[i]; p.ranges.max = kSvtMax[i]; p.ranges.def = kSvtDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(0.579f * softClip(L.process(iL[i])) * 0.98f); oR[i] = rbAmpLvl(0.579f * softClip(R.process(iR[i])) * 0.98f); }  // loudness std (~0.40 RMS multitone) + ceiling
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvtPlugin)
};

Plugin* createPlugin() { return new SvtPlugin(); }

END_NAMESPACE_DISTRHO
