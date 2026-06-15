/*
 * Marsten DBS 7400 — Marshall DBS 7400 (Dynamic Bass System) bass head model.
 *
 * From the 7400 service schematic (the user's scan, DWG 7400-60-0A/0B):
 *   • FRONTEND board (7400-60-0A): HI/LO input, op-amp GAIN (IC104/105),
 *     BRIGHT / DEEP voicing switches, pre-amp BLEND, Primary EQ.
 *   • COMPGRAF board (7400-60-0B): the COMPRESSION cell + the gyrator GRAPHIC
 *     EQUALIZER + a LEVEL, then out to the SS power amp (~400 W).
 *
 * Unlike the Sampleg SVT/V-4B (valve), the DBS is fully SOLID-STATE: a clean
 * op-amp preamp with lots of headroom — no tube saturation. So we model:
 *   IN ─[Lo pad]─► op-amp GAIN (nodal, rail-clip only when slammed)
 *      ─► Bright / Deep shelves ─► Lo / Hi 2-band Primary EQ
 *      ─► Compressor (fixed threshold, Depth-only) ─► 9-band gyrator graphic EQ
 *      ─► Volume ─► clean SS power
 *
 * The real 7400's Primary EQ is Lo + Hi only (VR3/VR4 — no mid), and the
 * compressor has just a DEPTH knob (VR5) with a FIXED internal threshold
 * (SSM2252 VCA); the panel "Threshold" is only the INDICATOR LED, not a knob.
 *
 * the game ("CLH-350B") drives Gain, Bass->Lo, Treble->Hi and the graphic bands;
 * the rest sit at faithful defaults. The graphic EQ is the REAL 7400 9-band set
 * (50/80/160/320/640/1.25k/2.5k/5k/8k); RS sends 7 bands, remapped to the nearest.
 * Nodal op-amp / RC / MFB primitives are shared with the
 * Sharke (Hartke) build — the right SS sibling — but the voicing is the DBS's.
 */
#include "DistrhoPlugin.hpp"
#include "DbsParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.19 RMS / -14 LUF at real settings);
// transparent below +/-0.90, soft-saturates to a +/-0.99 ceiling. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

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
        const float A=std::pow(10.f,dB/40.f), w0=6.2831853f*fc/fs, cw=std::cos(w0), sw=std::sin(w0);
        const float al=sw*0.5f*1.4142135f, sA=std::sqrt(A), t=2.f*sA*al;
        const float a0=(A+1)+(A-1)*cw+t;
        b0=A*((A+1)-(A-1)*cw+t)/a0; b1=2*A*((A-1)-(A+1)*cw)/a0; b2=A*((A+1)-(A-1)*cw-t)/a0;
        a1=-2*((A-1)+(A+1)*cw)/a0; a2=((A+1)+(A-1)*cw-t)/a0;
    }
    void setHighShelf(float fc, float dB, float fs) {
        const float A=std::pow(10.f,dB/40.f), w0=6.2831853f*fc/fs, cw=std::cos(w0), sw=std::sin(w0);
        const float al=sw*0.5f*1.4142135f, sA=std::sqrt(A), t=2.f*sA*al;
        const float a0=(A+1)-(A-1)*cw+t;
        b0=A*((A+1)+(A-1)*cw+t)/a0; b1=-2*A*((A-1)+(A+1)*cw)/a0; b2=A*((A+1)+(A-1)*cw-t)/a0;
        a1=2*((A-1)-(A+1)*cw)/a0; a2=((A+1)-(A-1)*cw-t)/a0;
    }
    void setPeak(float fc, float dB, float Q, float fs) {
        if (fc > fs * 0.49f) fc = fs * 0.49f;
        const float A=std::pow(10.f,dB/40.f), w0=6.2831853f*fc/fs, cw=std::cos(w0), sw=std::sin(w0);
        const float al=sw/(2.f*Q), a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*cw)/a0; b2=(1-al*A)/a0; a1=(-2*cw)/a0; a2=(1-al/A)/a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
};

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
// Shared with the Sharke (Hartke) build. Node 0 = gnd; nodes 1..nN unknown
// voltages; nX aux currents. Resistors, capacitor companions, ideal op-amps.
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

// ── SOLID-STATE PREAMP — nodal non-inverting op-amp, rail clip ────────────────
// The DBS front end is op-amp gain (IC104). Clean, with a 13.5 V rail it only
// clips when the Gain knob is slammed into a hot bass — no tube grind.
struct SsPre {
    inline double process(double vin) {
        const double R2=4700, R3=47000, Vrail=13.5;
        Mna m; m.init(3, 2); m.Vsrc(1,vin,0); m.R(2,0,R2); m.R(3,2,R3); m.OpAmp(1,2,3,1);
        double vo=0; if (m.solve()) vo=m.x[2];
        if (std::fabs(vo)>Vrail) { Mna m2; m2.init(3,2); m2.Vsrc(1,vin,0); m2.R(2,0,R2); m2.R(3,2,R3);
            m2.Vsrc(3, (vo>0?Vrail:-Vrail), 1); if (m2.solve()) vo=m2.x[2]; }
        return vo * (1.0/11.0);                             // ~unity small-signal
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
        m.stampG(2,4,G); m.Isrc(4,2,I1);               // C1 n1->VG
        m.stampG(2,3,G); m.Isrc(3,2,I2);               // C2 n1->out
        m.OpAmp(0,4,3,1);
        if (!m.solve()) return 0.0;
        const double out=m.x[2];
        { const double v=m.x[1]-m.x[3], i=G*(v-c1v)-c1i; c1i=i; c1v=v; }
        { const double v=m.x[1]-m.x[2], i=G*(v-c2v)-c2i; c2i=i; c2v=v; }
        return out;
    }
};

class DbsChannel {
    float fs = 48000.f;
    SsPre ss;                            // clean op-amp gain (no tube)
    float gainDrive=1, master=1, blend=0.6f, graphicLvl=1.f;
    Biquad brite, deep;                  // Bright / Deep voicing shelves
    Biquad bqLo, bqHi;                    // Primary EQ (Lo / Hi — 2-band, no mid)
    // compressor: envelope detector -> JFET voltage-controlled-resistor gain cell.
    // FIXED internal threshold (real 7400 has no threshold pot — only Depth + LED).
    static constexpr float kCompThr = 0.30f;
    bool compOn=false, graphicOn=true; float env=0, atk=0, rel=0, compAmt=0, compMk=1;
    static inline float msC(float ms, float fs){ return std::exp(-1.f/(0.001f*ms*fs)); }
    MFB eq[kNumEq];  float eqG[kNumEq];                  // 7-band gyrator graphic EQ
public:
    void setSampleRate(float s){ fs=(s>0)?s:48000.f; atk=msC(5.f,fs); rel=msC(140.f,fs);
        for (int i=0;i<kNumEq;++i){ eq[i].setT(s); eq[i].set(kEqFreqs[i], 1.1, 1e-8); eqG[i]=1.f; } }
    void reset(){ for (int i=0;i<kNumEq;++i) eq[i].reset();
        brite.reset(); deep.reset(); bqLo.reset(); bqHi.reset(); env=0; }

    void setParams(const float* p) {
        const float pad = (p[kLoInput] > 0.5f) ? 0.35f : 1.0f;        // Lo input pad
        // Clean SS gain. Bass songs run the Gain knob LOW (RS 0-100, median ~5),
        // so the knob is kept in a MODEST, mostly-clean range (it sets feel/level
        // and only edges into grit near the top), instead of a wide drive curve
        // that would make low-Gain songs play quiet. kLvl is calibrated at the
        // real median (Gain 5) so typical songs land at the house loudness.
        gainDrive = (0.8f + p[kGain] * 3.0f) * pad;
        // Pre-amp Blend: VALVE (warm, soft) <-> SOLID-STATE (clean). 0 = valve, 1 = SS.
        blend = p[kBlend];

        // Bright (HF lift ~+6 dB @ 3 kHz) / Deep (LF lift ~+6 dB @ 50 Hz) switches.
        if (p[kBright] > 0.5f) brite.setHighShelf(3000.f, 6.0f, fs); else brite.setBypass();
        if (p[kDeep]   > 0.5f) deep.setLowShelf(50.f, 6.0f, fs);     else deep.setBypass();

        // Primary EQ (2-band, no mid): Lo low shelf ~80 Hz (VR3), Hi high shelf
        // ~3.5 kHz (VR4, C29 10n/R34 4k7), +/-15 dB (0.5 = flat).
        bqLo.setLowShelf(80.f,    (p[kLo] - 0.5f) * 30.f, fs);
        bqHi.setHighShelf(3500.f, (p[kHi] - 0.5f) * 30.f, fs);

        // Compression: only DEPTH (amount). Threshold is FIXED internally (the
        // panel "Threshold" is just the indicator LED, not a knob).
        compOn  = p[kDepth] > 0.001f;
        compAmt = p[kDepth];
        compMk  = 1.0f + p[kDepth]*0.35f;

        // 7-band graphic EQ, +/-15 dB (0.5 = flat) + Graphic Level (+/-6 dB) + in/out.
        graphicOn = p[kGraphicOn] > 0.5f;
        for (int i=0;i<kNumEq;++i)
            eqG[i] = std::pow(10.f, (p[kFirstEq+i]-0.5f)*30.f/20.f);
        graphicLvl = std::pow(10.f, (p[kGraphicLevel]-0.5f)*12.f/20.f);

        master = p[kVolume] / 0.7f;
    }

    inline float process(float x) {
        // 1. preamp: blend the SOLID-STATE op-amp path (clean) with a VALVE path
        //    (warm soft-saturation). Pre-amp Blend sweeps between them.
        const double d = (double)(gainDrive * x);
        const double sClean = ss.process(d);
        const double sValve = std::tanh(d * 1.4) * 0.71;       // ~unity small-signal, warm when pushed
        double s = blend * sClean + (1.0 - blend) * sValve;
        // 2. Bright / Deep voicing
        s = brite.process((float)s); s = deep.process((float)s);
        // 3. Primary EQ (Lo / Hi — 2-band, no mid)
        s = bqLo.process((float)s); s = bqHi.process((float)s);
        // 4. COMPRESSOR — envelope → JFET VCR divider (fixed threshold, Depth-only)
        if (compOn) {
            const double a = std::fabs(s);
            const double c = (a > env) ? atk : rel;
            env = c*env + (1.0-c)*a;
            const double over = (env > kCompThr) ? (env - kCompThr) : 0.0;
            const double ctl  = over * compAmt * 5.0;
            double gain = 1.0;
            if (ctl > 1e-6) { const double Ron=400.0, Rs=4700.0; double rds = Ron*3.0/ctl;
                if (rds < Ron) rds = Ron; gain = rds/(Rs+rds); }
            s = s * gain * compMk;
        }
        // 5. GRAPHIC EQ (in/out) — parallel nodal MFB bands summed onto the dry,
        //    then the Graphic Level make-up.
        if (graphicOn) { const double dry=s; double sum=dry;
          for (int i=0;i<kNumEq;++i) sum -= (eqG[i]-1.0) * eq[i].proc(dry); s = sum * graphicLvl; }
        // 6. Volume → clean SS power
        return (float)(s * master);
    }
};

class DbsPlugin : public Plugin {
    DbsChannel L, R;
    float fParams[kParamCount];
    void recalc(){ L.setParams(fParams); R.setParams(fParams); }
public:
    DbsPlugin() : Plugin(kParamCount, 0, 0) {
        for (int i=0;i<kParamCount;++i) fParams[i]=kDbsDef[i];
        const float sr=(float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "MarstenDBS7400"; }
    const char* getDescription() const override { return "Marshall DBS 7400 solid-state bass head model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'D', 'b'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kBright) p.hints |= kParameterIsBoolean;
        p.name = kDbsNames[i]; p.symbol = kDbsSymbols[i];
        p.ranges.min = kDbsMin[i]; p.ranges.max = kDbsMax[i]; p.ranges.def = kDbsDef[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i]=v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL=in[0]; const float* iR=in[1]; float* oL=out[0]; float* oR=out[1];
        for (uint32_t i=0;i<frames;++i){ oL[i]=rbAmpLvl(1.1906f*L.process(iL[i])); oR[i]=rbAmpLvl(1.1906f*R.process(iR[i])); }  // kLvl: -14 LUF at Gain 5 was 1.683; +1.7 dB so this CLEAN amp matches the saturated amps' PERCEIVED loudness (clean has lower crest density). See AMP_LOUDNESS.md
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DbsPlugin)
};

Plugin* createPlugin() { return new DbsPlugin(); }

END_NAMESPACE_DISTRHO
