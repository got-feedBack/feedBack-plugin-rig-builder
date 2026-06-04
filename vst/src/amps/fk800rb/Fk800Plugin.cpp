/*
 * Freddy Krueger 800BR — Gallien-Krueger 800RB bass-head model.
 *
 * DSP modeled from the GK 800RB service manual (preamp sheet 406-0045 "Bob
 * Gallien 800 RB Preamp", + the Operators Manual block diagram & specs). The
 * 800RB is an all-op-amp (LF353) preamp feeding a bi-amp power section
 * (300W low / 100W high). We recreate each block in order:
 *
 *   1. Input  : 1/4" in with a -10 dB pad (drops preamp gain 10 dB, +headroom).
 *   2. Volume : preamp drive into a gentle op-amp soft-clip — the GK "growl"
 *               that the input/diode stage (D1,D2) produces when pushed.
 *   3. Voicing filters (the 3 square switches):
 *        Lo Cut      — bass roll-off (high-pass) to kill stage rumble.
 *        Mid Contour — notch at ~500 Hz ("mellow round sound").
 *        Hi Boost    — presence/high-shelf boost ("adds edge and definition").
 *   4. 4-band Active EQ, ±15 dB, flat at 0.5:
 *        Bass   low shelf  60 Hz | Lo-Mid peak 250 Hz
 *        Hi-Mid peak     1.0 kHz | Treble high shelf 4 kHz
 *   5. Boost : footswitchable preset gain, up to +15 dB, with a touch more
 *              growl when driven hard.
 *   6. Electronic Crossover + Master volumes: a Linkwitz-ish LP/HP split at the
 *        Crossover frequency (100 Hz .. 1.04 kHz). In Bi-Amp mode the low band
 *        is scaled by the 300W master and the high band by the 100W master and
 *        summed (the real head's two power amps feeding one signal here); in
 *        Full-range mode both masters act on the full-range signal.
 */
#include "DistrhoPlugin.hpp"
#include "Fk800Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): kLvl matches the
// amp to the common multitone loudness (~0.30 RMS at real settings); the soft
// knee is transparent below +/-0.80 and saturates to a +/-0.98 ceiling so EQ
// boosts never hard-clip. See AMP_LOUDNESS.md.
static inline float rbAmpLvl(float x){ const float t=0.80f,c=0.98f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

// ── RBJ biquad (transposed direct form II) ───────────────────────────────────
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
    void setLowPass(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = ((1 - cw) * 0.5f) / a0;
        b1 = (1 - cw)          / a0;
        b2 = ((1 - cw) * 0.5f) / a0;
        a1 = (-2 * cw)         / a0;
        a2 = (1 - alpha)       / a0;
    }
    void setHighPass(float fc, float Q, float fs) {
        const float w0 = 6.2831853f * fc / fs, cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * Q);
        const float a0 = 1 + alpha;
        b0 = ((1 + cw) * 0.5f) / a0;
        b1 = -(1 + cw)         / a0;
        b2 = ((1 + cw) * 0.5f) / a0;
        a1 = (-2 * cw)         / a0;
        a2 = (1 - alpha)       / a0;
    }
    void setBypass() { b0 = 1; b1 = b2 = a1 = a2 = 0; z1 = z2 = 0; }
};

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
// Solves a small linear circuit (resistors, capacitor companions, ideal op-amps,
// voltage sources) per sample by Gaussian elimination. Node 0 = ground; nodes
// 1..nN are unknown voltages; nX aux currents follow (one per V-source / op-amp).
// Verified against a textbook RC low-pass (-3dB at 1/(2*pi*R*C)) and a
// non-inverting op-amp (gain = 1 + Rf/Rg).
struct Mna {
    static const int MAXN = 8;
    int sz, nn;
    double A[MAXN*MAXN], b[MAXN], x[MAXN];
    void init(int nN, int nX) { nn = nN; sz = nN + nX;
        for (int i = 0; i < sz*sz; ++i) A[i] = 0.0;
        for (int i = 0; i < sz; ++i) b[i] = 0.0; }
    inline void stampG(int a, int bb, double g) {
        if (a>0)  { A[(a-1)*sz+(a-1)]  += g; if (bb>0) A[(a-1)*sz+(bb-1)] -= g; }
        if (bb>0) { A[(bb-1)*sz+(bb-1)]+= g; if (a>0)  A[(bb-1)*sz+(a-1)] -= g; } }
    inline void R(int a, int bb, double r) { if (r < 1e-9) r = 1e-9; stampG(a, bb, 1.0/r); }
    inline void Isrc(int a, int bb, double I) { if (a>0) b[a-1] -= I; if (bb>0) b[bb-1] += I; }
    inline void Vsrc(int a, double V, int k) { int r = nn+k;
        if (a>0) { A[(a-1)*sz+r] += 1; A[r*sz+(a-1)] += 1; } b[r] = V; }
    inline void OpAmp(int np, int nnode, int no, int k) { int r = nn+k;
        if (no>0)    A[(no-1)*sz+r]    += 1;
        if (np>0)    A[r*sz+(np-1)]    += 1;
        if (nnode>0) A[r*sz+(nnode-1)] -= 1; }
    // transconductance: current g*(V(ca)-V(cb)) injected into node oa, out of ob
    // (used to stamp the Jacobian of nonlinear elements like the BJT).
    inline void gm(int oa, int ob, int ca, int cb, double g) {
        if (oa>0) { if (ca>0) A[(oa-1)*sz+(ca-1)] += g; if (cb>0) A[(oa-1)*sz+(cb-1)] -= g; }
        if (ob>0) { if (ca>0) A[(ob-1)*sz+(ca-1)] -= g; if (cb>0) A[(ob-1)*sz+(cb-1)] += g; } }
    bool solve() { const int n = sz;
        for (int col = 0; col < n; ++col) {
            int piv = col; double mx = std::fabs(A[col*n+col]);
            for (int r = col+1; r < n; ++r) { double v = std::fabs(A[r*n+col]); if (v > mx) { mx = v; piv = r; } }
            if (mx < 1e-15) return false;
            if (piv != col) { for (int c = 0; c < n; ++c) { double t = A[col*n+c]; A[col*n+c] = A[piv*n+c]; A[piv*n+c] = t; }
                double t = b[col]; b[col] = b[piv]; b[piv] = t; }
            const double d = A[col*n+col];
            for (int r = 0; r < n; ++r) { if (r == col) continue; const double f = A[r*n+col]/d; if (f == 0) continue;
                for (int c = col; c < n; ++c) A[r*n+c] -= f*A[col*n+c]; b[r] -= f*b[col]; } }
        for (int i = 0; i < n; ++i) x[i] = b[i] / A[i*n+i];
        return true; } };

// ── GK 800RB INPUT/PREAMP stage — true component-level (nodal) model ─────────
// Bob Gallien sheet 60045A, U1: non-inverting amp, Rg=R2 4.7K, Rf=R3 1M
// (DC gain 1+R3/R2 ~ 214, matching the manual's 2 mV sensitivity), C2 across R3
// (stability/HF cap, ~22pF -> gentle ~7 kHz softening) and the op-amp output
// saturating at the +/-supply rails = the GK growl. Each R, C and the op-amp
// are solved as real circuit elements per sample (no tanh).
struct FkPreamp {
    float fs = 48000.f; double T = 1.0/48000.0;
    double c2v = 0.0, c2i = 0.0;            // C2 trapezoidal companion state
    void setFs(float s) { fs = (s > 0.f) ? s : 48000.f; T = 1.0 / fs; }
    void reset() { c2v = 0.0; c2i = 0.0; }
    inline float process(double vin) {
        const double R2 = 4700.0, R3 = 1.0e6, C2 = 22.0e-12, Vrail = 13.5;
        const double Geq = 2.0*C2/T, Ieq = Geq*c2v + c2i;
        Mna m; m.init(3, 2);                                  // nodes: 1 in, 2 inv, 3 out
        m.Vsrc(1, vin, 0); m.R(2, 0, R2); m.R(3, 2, R3);
        m.stampG(3, 2, Geq); m.Isrc(2, 3, Ieq);              // C2 across R3
        m.OpAmp(1, 2, 3, 1);                                  // + = in, - = inv, out = 3
        double vo = 0.0, vinv = 0.0;
        if (m.solve()) { vo = m.x[2]; vinv = m.x[1]; }
        if (std::fabs(vo) > Vrail) {                          // op-amp saturated -> rail
            Mna m2; m2.init(3, 2);
            m2.Vsrc(1, vin, 0); m2.R(2, 0, R2); m2.R(3, 2, R3);
            m2.stampG(3, 2, Geq); m2.Isrc(2, 3, Ieq);
            m2.Vsrc(3, (vo > 0 ? Vrail : -Vrail), 1);        // output held at the rail
            if (m2.solve()) { vo = m2.x[2]; vinv = m2.x[1]; }
        }
        const double v = vo - vinv;                          // advance C2 state
        const double i = Geq*(v - c2v) - c2i; c2i = i; c2v = v;
        return (float)vo;
    }
};

// ── One-pole RC filter, solved nodally (R and C as real circuit elements) ────
// LP: in-R-out, C from out to gnd. HP: in-C-out, R from out to gnd. R fixed at
// 10k; C = 1/(2*pi*fc*R) sets the corner. Shelves/peaks are built by summing
// these (the EQ's summing op-amps): low shelf = HP + g*LP, high shelf = LP +
// g*HP, peak = dry + (g-1)*bandpass, with g = the pot's +/-dB.
struct RC1 {
    double C = 1e-9, Rr = 10000.0, vp = 0.0, ip = 0.0, T = 1.0/48000.0; bool hp = false;
    void setT(float fs) { T = 1.0 / ((fs > 0.f) ? fs : 48000.0); }
    void set(double fc, bool isHp) { hp = isHp; Rr = 10000.0; C = 1.0 / (6.2831853 * fc * Rr); }
    void reset() { vp = 0.0; ip = 0.0; }
    inline double proc(double in) {
        const double Geq = 2.0*C/T, Ieq = Geq*vp + ip;
        Mna m; m.init(2, 1); m.Vsrc(1, in, 0);
        if (!hp) { m.R(1, 2, Rr); m.stampG(2, 0, Geq); m.Isrc(0, 2, Ieq); }
        else     { m.stampG(1, 2, Geq); m.Isrc(2, 1, Ieq); m.R(2, 0, Rr); }
        if (!m.solve()) return 0.0;
        const double vo = m.x[1];
        const double vc = hp ? (m.x[0] - m.x[1]) : m.x[1];   // capacitor voltage
        const double i = Geq*(vc - vp) - ip; ip = i; vp = vc;
        return vo;
    }
};

// ── BOOST stage Q1 — true nodal NPN transistor (Ebers-Moll + Newton) ──────────
// The GK boost is a footswitchable preset around a small-signal NPN (Q1). Here
// it's a real common-emitter stage: the BE/BC junctions and Ebers-Moll currents
// are solved by Newton-Raphson each sample (warm-started from the previous
// sample), so the collector swing clipping against the +/-rails is the actual
// transistor grit. Boost Level sets the drive into the base; a DC blocker takes
// the AC collector swing. Validated standalone (bias point, AC gain, rail clip).
struct FkBoost {
    double vB=0.95, vC=13.5, vE=0.33, dcAvg=13.5;   // warm-start + DC tracker
    void reset() { vB=0.95; vC=13.5; vE=0.33; dcAvg=13.5; }
    static inline double lim(double vn, double vo) {  // junction-voltage step limit
        const double Vt=0.02585, Is=1e-14, vc=Vt*std::log(Vt/(1.41421356*Is));
        if (vn>vc && std::fabs(vn-vo)>2*Vt) { if (vo>0) { double a=1+(vn-vo)/Vt; vn = a>0 ? vo+Vt*std::log(a) : vc; } else vn=Vt*std::log(vn/Vt+1.0); }
        return vn; }
    inline double process(double ain, double injScale) {
        const double Vt=0.02585, Is=1e-14, Bf=220.0, Br=2.0, Vcc=15.0;
        const double Rc=4700, Re=1000, Rb1=100000, Rb2=22000, Rsig=4700;
        double B=vB, C=vC, E=vE;
        for (int it=0; it<8; ++it) {
            Mna m; m.init(5, 2);                       // 1 Vcc, 2 B, 3 C, 4 E, 5 sig
            m.Vsrc(1, Vcc, 0); m.Vsrc(5, ain*injScale, 1);
            m.R(5,2,Rsig); m.R(3,1,Rc); m.R(4,0,Re); m.R(2,1,Rb1); m.R(2,0,Rb2);
            double vbe=lim(B-E, vB-vE), vbc=lim(B-C, vB-vC);
            if (vbe>0.95) vbe=0.95; if (vbc>0.95) vbc=0.95;
            const double ef=std::exp(vbe/Vt), er=std::exp(vbc/Vt);
            const double gf=Is/Vt*ef, gr=Is/Vt*er;
            const double Ib=Is*((1.0/Bf)*(ef-1)+(1.0/Br)*(er-1));
            const double Ic=Is*((ef-1)-(1.0+1.0/Br)*(er-1));
            const double dIb_dB=(gf/Bf)+(gr/Br), dIb_dC=-(gr/Br), dIb_dE=-(gf/Bf);
            const double dIc_dB=gf-(1.0+1.0/Br)*gr, dIc_dC=(1.0+1.0/Br)*gr, dIc_dE=-gf;
            const double dIe_dB=-(dIb_dB+dIc_dB), dIe_dC=-(dIb_dC+dIc_dC), dIe_dE=-(dIb_dE+dIc_dE);
            m.gm(2,0,2,0,dIb_dB); m.gm(2,0,3,0,dIb_dC); m.gm(2,0,4,0,dIb_dE);
            m.gm(3,0,2,0,dIc_dB); m.gm(3,0,3,0,dIc_dC); m.gm(3,0,4,0,dIc_dE);
            m.gm(4,0,2,0,dIe_dB); m.gm(4,0,3,0,dIe_dC); m.gm(4,0,4,0,dIe_dE);
            const double Ie=-(Ib+Ic);
            m.Isrc(2,0, Ib-(dIb_dB*B+dIb_dC*C+dIb_dE*E));
            m.Isrc(3,0, Ic-(dIc_dB*B+dIc_dC*C+dIc_dE*E));
            m.Isrc(4,0, Ie-(dIe_dB*B+dIe_dC*C+dIe_dE*E));
            if (!m.solve()) break;
            const double nB=m.x[1], nC=m.x[2], nE=m.x[3];
            const double err=std::fabs(nB-B)+std::fabs(nC-C)+std::fabs(nE-E);
            B=nB; C=nC; E=nE; if (err<1e-7) break;
        }
        if (!std::isfinite(C)) { reset(); return ain; }
        vB=B; vC=C; vE=E;
        dcAvg += 0.0008*(vC-dcAvg);                   // DC blocker
        return (vC - dcAvg) * 1.78;                   // AC collector swing, ~unity at the fixed drive
    }
};

class Gk800Channel {
    float fs = 48000.f;
    FkPreamp pre;                          // nodal input/preamp stage (the growl)
    float preDrive = 0.2f, preMakeup = 0.014f;
    // voicing (nodal): Lo Cut HPF, Mid Contour notch (dry - bandpass), Hi Boost shelf
    RC1 loCutF;  bool loCutOn = false;
    RC1 conHp, conLp;  bool contourOn = false;
    RC1 hbLp, hbHp;    bool hiBoostOn = false;
    // 4-band active EQ (nodal): each band's filters + the pot's +/-dB gain
    RC1 bLp, bHp;   float bassG = 1.f;     // Bass   low shelf 60 Hz
    RC1 lmHp, lmLp; float loMidG = 1.f;    // Lo-Mid peak 250 Hz
    RC1 hmHp, hmLp; float hiMidG = 1.f;    // Hi-Mid peak 1.15 kHz
    RC1 tLp, tHp;   float trebG = 1.f;     // Treble high shelf 4 kHz
    FkBoost boostStage;  float boostInj = 0.04f, boostMakeup = 1.f;  bool boostOn = false;  // nodal Q1 boost
    RC1 xLp, xHp;   float g100 = 1.f, g300 = 1.f;  bool biamp = false;  // crossover + masters

    static inline float softclip(float x) { return std::tanh(x); }
    RC1* allRC[15] = { &loCutF,&conHp,&conLp,&hbLp,&hbHp,&bLp,&bHp,&lmHp,&lmLp,&hmHp,&hmLp,&tLp,&tHp,&xLp,&xHp };
public:
    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f; pre.setFs(s);
        for (RC1* p : allRC) p->setT(fs);
        // fixed band corners (Hz); the crossover (xLp/xHp) is retuned in setParams
        loCutF.set(110.0, true);
        conHp.set(250.0, true);  conLp.set(1000.0, false);    // contour bandpass ~500 Hz
        hbLp.set(2200.0, false); hbHp.set(2200.0, true);      // hi-boost shelf
        bLp.set(60.0, false);    bHp.set(60.0, true);         // bass shelf
        lmHp.set(125.0, true);   lmLp.set(500.0, false);      // lo-mid peak 250 Hz
        hmHp.set(575.0, true);   hmLp.set(2300.0, false);     // hi-mid peak 1.15 kHz
        tLp.set(4000.0, false);  tHp.set(4000.0, true);       // treble shelf
        xLp.set(500.0, false);   xHp.set(500.0, true);
    }
    void reset() { pre.reset(); boostStage.reset(); for (RC1* p : allRC) p->reset(); }

    void setParams(float volume, float treble, float hiMid, float loMid, float bass,
                   float boostLevel, float xover, float master100, float master300,
                   bool pad, bool loCutOn, bool contourOn, bool hiBoostOn,
                   bool boostOnP, bool biampP) {
        // ── input / preamp: Volume drives the nodal preamp stage (clean at low
        //    Volume, op-amp rail-clipping growl when pushed). -10 dB pad cuts the
        //    drive into it. preMakeup normalises the ~13.5 V rail back to ~unity.
        const float padScale = pad ? 0.316f : 1.0f;
        // drive curve: stays clean at low/mid Volume, then a steep (vol^4) kick
        // slams U1 into the rails near the top for heavy growl.
        // (0.5 -> ~0.18 clean, 0.7 -> ~0.36, 1.0 -> ~1.05 heavy.)
        const float v = volume;
        preDrive  = padScale * (0.05f + 0.15f*v + 0.85f*v*v*v*v);   // signal level (V) into U1
        preMakeup = 6.0f / 214.0f;                          // ~unity at default Volume

        // ── voicing filters (nodal RC networks, switched in/out) ──
        this->loCutOn = loCutOn; this->contourOn = contourOn; this->hiBoostOn = hiBoostOn;

        // ── 4-band active EQ — each band is a nodal filter pair; the knob sets
        //    the summing op-amp's +/-dB gain (0.5 = flat). Corners from the
        //    schematic (Bob Gallien 60045A): Bass 60 Hz shelf, Lo-Mid 250 Hz,
        //    Hi-Mid 1.15 kHz, Treble 4 kHz. g = 10^(+/-12 dB). ──
        bassG  = std::pow(10.f, (bass   - 0.5f) * 24.f / 20.f);
        loMidG = std::pow(10.f, (loMid  - 0.5f) * 24.f / 20.f);
        hiMidG = std::pow(10.f, (hiMid  - 0.5f) * 24.f / 20.f);
        trebG  = std::pow(10.f, (treble - 0.5f) * 24.f / 20.f);

        // ── boost: footswitchable preset — Boost Level sets the drive into the
        //    nodal Q1 transistor (more level = more gain + transistor clip). ──
        boostOn  = boostOnP;
        boostInj = 0.04f;                                       // mild fixed drive (transistor character)
        boostMakeup = std::pow(10.f, (boostLevel * 15.f) / 20.f);   // the preset pot: 0 .. +15 dB

        // ── crossover split + masters (retune the nodal LP/HP to the freq) ──
        biamp = biampP;
        const double fc = 100.0 + 940.0 * xover;       // 100 Hz .. 1040 Hz
        xLp.set(fc, false); xHp.set(fc, true);
        g300 = master300 / 0.7f;   // low / 300W amp  (unity ~ 0.7)
        g100 = master100 / 0.7f;   // high / 100W amp
    }

    inline float process(float x) {
        // 1-2. INPUT/PREAMP — solved as the real op-amp circuit (nodal); the
        //      growl is the actual U1 output clipping at the supply rails.
        double d = (double)(pre.process((double)(preDrive * x)) * preMakeup);

        // 3. VOICING (nodal): Lo Cut HPF, Mid Contour notch (dry - bandpass),
        //    Hi Boost high-shelf (+6 dB).
        if (loCutOn)   d = loCutF.proc(d);
        if (contourOn) { const double bp = conLp.proc(conHp.proc(d)); d -= 0.9 * bp; }
        if (hiBoostOn) d = hbLp.proc(d) + 2.0 * hbHp.proc(d);

        // 4. 4-band ACTIVE EQ (nodal). Shelves: HP+g*LP / LP+g*HP. Peaks: dry +
        //    (g-1)*bandpass. Each filter is a real RC network solved per sample.
        d = bHp.proc(d) + bassG * bLp.proc(d);                          // Bass shelf
        { const double bp = lmLp.proc(lmHp.proc(d)); d += (loMidG - 1.0) * bp; }  // Lo-Mid
        { const double bp = hmLp.proc(hmHp.proc(d)); d += (hiMidG - 1.0) * bp; }  // Hi-Mid
        d = tLp.proc(d) + trebG * tHp.proc(d);                          // Treble shelf

        // 5. BOOST — real nodal Q1 transistor stage (Ebers-Moll, Newton/sample);
        //    the preset level (boostMakeup) is the pot after the transistor.
        if (boostOn) d = boostStage.process(d, boostInj) * boostMakeup;

        // 6. CROSSOVER (nodal LP/HP) + masters
        const double low = xLp.proc(d), high = xHp.proc(d);
        if (biamp) return (float)(low * g300 + high * g100);   // bi-amp split & re-sum
        return (float)(d * (0.5 * g300 + 0.5 * g100));         // full range
    }
};

class Fk800Plugin : public Plugin {
    Gk800Channel L, R;
    float fParams[kParamCount];
    void recalc() {
        const bool pad = fParams[kPad] > 0.5f, lc = fParams[kLoCut] > 0.5f;
        const bool ct  = fParams[kContour] > 0.5f, hb = fParams[kHiBoost] > 0.5f;
        const bool bo  = fParams[kBoostOn] > 0.5f, ba = fParams[kBiamp] > 0.5f;
        L.setParams(fParams[kVolume], fParams[kTreble], fParams[kHiMid], fParams[kLoMid], fParams[kBass],
                    fParams[kBoostLevel], fParams[kXover], fParams[kMaster100], fParams[kMaster300],
                    pad, lc, ct, hb, bo, ba);
        R.setParams(fParams[kVolume], fParams[kTreble], fParams[kHiMid], fParams[kLoMid], fParams[kBass],
                    fParams[kBoostLevel], fParams[kXover], fParams[kMaster100], fParams[kMaster300],
                    pad, lc, ct, hb, bo, ba);
    }
public:
    Fk800Plugin() : Plugin(kParamCount, 0, 0) {
        for (int i = 0; i < kParamCount; ++i) fParams[i] = kFk800Def[i];
        const float sr = (float)getSampleRate();
        L.setSampleRate(sr); R.setSampleRate(sr); L.reset(); R.reset(); recalc();
    }
protected:
    const char* getLabel()       const override { return "FreddyKrueger800BR"; }
    const char* getDescription() const override { return "Gallien-Krueger 800RB bass head model"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'B', 'F', 'k'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)kParamCount) return;
        p.hints = kParameterIsAutomatable;
        if (i >= (uint32_t)kPad) p.hints |= kParameterIsBoolean;
        p.name = kFk800Names[i]; p.symbol = kFk800Symbols[i];
        p.ranges.min = kFk800Min[i]; p.ranges.max = kFk800Max[i]; p.ranges.def = kFk800Def[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)kParamCount) ? fParams[i] : 0.f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { L.setSampleRate((float)r); R.setSampleRate((float)r); L.reset(); R.reset(); recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        for (uint32_t i = 0; i < frames; ++i) { oL[i] = rbAmpLvl(1.356f * L.process(iL[i])); oR[i] = rbAmpLvl(1.356f * R.process(iR[i])); }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fk800Plugin)
};

Plugin* createPlugin() { return new Fk800Plugin(); }

END_NAMESPACE_DISTRHO
