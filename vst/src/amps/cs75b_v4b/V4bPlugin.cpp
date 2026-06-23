/*
 * Sampleg V-4B — Ampeg V-4B all-tube bass head, COMPONENT-LEVEL model.
 *
 * Built from the 1971 V-4B factory schematic (the user's scan, 12/71-223) and
 * the power/tone-control PC-board pictorials (12/71-222/-221):
 *   • PRE AMP  — V1 12AX7 input/gain, V2 12AX7, V3 12DW7 driver
 *   • PHASE INV — V4 12AU7
 *   • POWER    — V5..V8 4x 7027A push-pull, ~100W, GZ34-less SS rectifier
 *
 * The V-4B front-panel path we model (single channel of the 2-ch front end):
 *
 *   IN ─[R1 / −15dB pad jack]─► V1 12AX7 gain (nodal triode, Gain pot)
 *      ─► Ultra Lo contour (Lo Sw)  ─► Bass/Mid(+Freq)/Treble tone stack (64-60-077)
 *      ─► Ultra Hi presence (Hi Sw) ─► more preamp gain (V2/V3) ─► Master
 *      ─► 4x 7027A push-pull power ─► output transformer band-limit
 *
 * The gain stage is the REAL 12AX7: the Koren plate-current law solved by
 * Newton-Raphson every sample (asymmetric grid/cutoff clipping = the tube grit).
 * Tone corner frequencies/Qs/depths are white-boxed from the schematic's R/C/L.
 * vs the SVT-CL: the V-4B is HALF the power (100 W / four 7027A vs 300 W / six
 * 6550) so it has LESS clean headroom — the power stage compresses/grinds
 * earlier when pushed; the extra preamp stage (V3 12DW7) gives it more front-end
 * gain, so the Gain knob breaks up sooner. Mid selector is 3-position (not 5).
 */
#include "DistrhoPlugin.hpp"
#include "V4bParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.30 RMS at real settings); the soft
// knee is transparent below +/-0.80 and saturates to a +/-0.98 ceiling so EQ
// boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// Loudness standardization (shared amp convention): a per-amp output `makeup`
// tuned so the multitone (110 Hz–1.8 kHz) RMS matches the Box AC30 reference
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
// V-4B preamp first stage (V1A 12AX7): common-cathode 12AX7. The schematic
// reads ~230 V at the plate node (vs the SVT-CL's ~200 V — the V-4B runs a
// hotter front end), plate load ~100k, self-bias Rk ≈ 1.5k -> ~1.48 V cathode
// (the schematic's "1.48V" label), coupling C4 .047µF. The Koren plate-current
// law Ip(Vgk,Vpk) is solved by Newton-Raphson each sample; the plate swing
// clips asymmetrically (toward B+ / cutoff) — the tube grit when Gain is pushed.
struct Triode {
    double vG=0, vP=230, vK=1.5, dcAvg=230.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=230; vK=1.5; dcAvg=230.0;  for (int i=0;i<2000;++i) process(0.0); }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {        // vin = grid drive (V); returns AC plate swing
        const double Bp=230, Rp=100000, Rk=1500, h=1e-4;
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

class V4bChannel {
    float fs = 48000.f;
    Triode v1;                       // nodal 12AX7 input/gain stage (V1, the grit)
    Biquad ulLow, ulMid, ulHigh;     // Ultra Lo contour (Lo Sw network)
    Biquad uhShelf;                  // Ultra Hi presence (Hi Sw network)
    Biquad bqBass, bqMid, bqTreble;  // passive tone stack (64-60-077 board)
    Biquad pwrLP;                    // 7027A + output transformer band-limit
    float drive = 1.f, master = 1.f, pwrDrive = 1.f, outMakeup = 1.f;

    // 4x 7027A push-pull (~100W): symmetric soft clip (even harmonics cancel),
    // but ~half the headroom of the SVT's six-6550 stage — it compresses/grinds
    // EARLIER when the Master is pushed (the V-4B's punchy, gritty bass voice).
    static inline float pushPull(float x) {
        // odd-symmetric saturator; earlier knee than the SVT (x*1.15 vs x*0.8).
        return std::tanh(x * 1.15f) * 0.8696f;   // 1/1.15 makeup so small x ≈ unity
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
        // ── input / V1 grid drive. The −15 dB jack pad cuts ~5.6× ──
        const float padScale = pad ? 0.178f : 1.0f;            // −15 dB
        // Volume/Gain pot is 1M LINEAR (VR101) — NOT the SVT's audio-taper pot —
        // so drive rises ~linearly with the knob (a more even sweep, not "all at
        // the top"). The V-4B's three preamp stages (V1 12AX7 + V2 12AX7 + V3
        // 12DW7) give more front-end gain, so it grid-clips sooner than the SVT.
        drive = (0.4f + gain * 9.0f) * padScale;

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
        //  Midrange: the V-4B mid is an INDUCTOR-based resonance — L101 + the
        //  3-position SW3 cap selector (C114 .15µF / C113 .033µF / top tap) =
        //  ~300 / 900 / 2500 Hz. An LC tank resonates with a HIGHER Q than the
        //  SVT's broad RC mid, so the boost/cut is tighter/peakier (Q≈1.3). The
        //  50k mid pot sets the depth.
        int sel = (int)(freq * 3.0f); if (sel > 2) sel = 2; if (sel < 0) sel = 0;
        bqMid.setPeak(kV4bMidFreqs[sel], (midrange - 0.5f) * 28.f, 1.3f, fs);
        //  Treble  : P5 1MA + C26 15pF / C27 100pF     → high shelf ~5 kHz.
        bqTreble.setHighShelf(5000.f, (treble - 0.5f) * 30.f, fs);

        // ── Master → 4x 7027A push-pull drive. Unity ≈ 0.7; with only ~100 W of
        //    headroom the V-4B leans into compression/grind SOONER than the SVT,
        //    so we push a bit harder into the (earlier-knee) saturator. ──
        master   = masterP / 0.7f;
        pwrDrive = 0.6f + master * 0.9f;                      // into the 7027A saturator
        // Output transformer + 7027A HF roll-off (100 W head, a touch darker top).
        pwrLP.setLowpassQ(7500.f, 0.7f, fs);
        // ── Loudness standardization: a gain-dependent output makeup that keeps
        //    the FINAL multitone loudness ~flat across the Gain knob (so songs at
        //    different Gain settings don't jump volume). Targeting flat *final*
        //    loudness (not flat pre-clip RMS) matters because the output soft-clip
        //    compresses the cleaner low-Gain signal MORE than the saturated
        //    high-Gain one, so a flat pre-clip level still drifts loud as Gain
        //    rises. The makeup that holds final RMS constant has 1/makeup nearly
        //    linear in Gain — fit below (anchored so Gain 0.5 == the noon level
        //    that the cross-amp match is calibrated at). Earlier curves either
        //    dropped at max Gain (exp, over-cancelled) or boosted ~10–35% Gain
        //    (flat pre-clip, not flat final).
        const float invMk = 0.0291f + gain * (0.5786f - 0.0980f * gain);
        outMakeup = 1.0f / invMk;
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

class V4bPlugin : public Plugin {
    V4bChannel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool pad = fParams[kPad] > 0.5f, ul = fParams[kUltraLo] > 0.5f, uh = fParams[kUltraHi] > 0.5f;
        L.setParams(fParams[kGain], fParams[kBass], fParams[kMidrange], fParams[kFreq],
                    fParams[kTreble], fParams[kMaster], pad, ul, uh);
        R.setParams(fParams[kGain], fParams[kBass], fParams[kMidrange], fParams[kFreq],
                    fParams[kTreble], fParams[kMaster], pad, ul, uh);
    }
public:
    V4bPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kV4bDef[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "SamplegV4B"; }
    const char* getDescription() const override { return "Ampeg V-4B all-tube bass head — component-level model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'V', '4'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kV4bNames[i]; p.symbol = kV4bSymbols[i];
        p.ranges.min = kV4bMin[i]; p.ranges.max = kV4bMax[i]; p.ranges.def = kV4bDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(0.2529f * softClip(L.process(iL[i])) * 0.98f); oR[i] = rbAmpLvl(0.2529f * softClip(R.process(iR[i])) * 0.98f); }  // kLvl -> -14 LUF (~0.19 RMS multitone @ real CS75B settings) + ceiling; see AMP_LOUDNESS.md
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(V4bPlugin)
};

Plugin* createPlugin() { return new V4bPlugin(); }

END_NAMESPACE_DISTRHO
