#ifndef FK800_CORE_H
#define FK800_CORE_H
//
// Fk800Core — Gallien-Krueger 800RB ("Freddy Krueger 800BR"), component-level
// SOLID-STATE bass head. Extracted from the plugin into a core header so it runs
// at the oversampled rate (the plugin wraps it 2×) and is offline-calibratable.
//
// The 800RB is all solid-state (no tubes), so this does NOT use the tube framework:
//   • FkPreamp  — real op-amp (LF353) non-inverting stage solved nodally (MNA);
//                 the growl is the actual output clipping at the ±13.5 V rails.
//   • RC1       — one-pole RC sections (real R/C) for the voicing + 4-band EQ.
//   • FkBoost   — real NPN transistor (Ebers-Moll + Newton/sample) = the boost grit.
//   • crossover + bi-amp masters.
//
// Adding 2× oversampling around this (in the plugin) is the key fix vs the old
// non-oversampled build: the op-amp rail clip + transistor clip generate harmonics
// well above base Nyquist that aliased back as harshness.
//
// Values from the GK 800RB service manual (preamp sheet 60045A "Bob Gallien 800 RB").
//
#include <cmath>

namespace fk800gk {

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
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

// ── GK 800RB INPUT/PREAMP — nodal LF353 non-inverting amp (the growl) ─────────
struct FkPreamp {
    float fs = 48000.f; double T = 1.0/48000.0;
    double c2v = 0.0, c2i = 0.0;
    void setFs(float s) { fs = (s > 0.f) ? s : 48000.f; T = 1.0 / fs; }
    void reset() { c2v = 0.0; c2i = 0.0; }
    inline float process(double vin) {
        const double R2 = 4700.0, R3 = 1.0e6, C2 = 22.0e-12, Vrail = 13.5;
        const double Geq = 2.0*C2/T, Ieq = Geq*c2v + c2i;
        Mna m; m.init(3, 2);
        m.Vsrc(1, vin, 0); m.R(2, 0, R2); m.R(3, 2, R3);
        m.stampG(3, 2, Geq); m.Isrc(2, 3, Ieq);
        m.OpAmp(1, 2, 3, 1);
        double vo = 0.0, vinv = 0.0;
        if (m.solve()) { vo = m.x[2]; vinv = m.x[1]; }
        if (std::fabs(vo) > Vrail) {
            Mna m2; m2.init(3, 2);
            m2.Vsrc(1, vin, 0); m2.R(2, 0, R2); m2.R(3, 2, R3);
            m2.stampG(3, 2, Geq); m2.Isrc(2, 3, Ieq);
            m2.Vsrc(3, (vo > 0 ? Vrail : -Vrail), 1);
            if (m2.solve()) { vo = m2.x[2]; vinv = m2.x[1]; }
        }
        const double v = vo - vinv;
        const double i = Geq*(v - c2v) - c2i; c2i = i; c2v = v;
        return (float)vo;
    }
};

// ── One-pole RC filter, solved nodally ────────────────────────────────────────
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
        const double vc = hp ? (m.x[0] - m.x[1]) : m.x[1];
        const double i = Geq*(vc - vp) - ip; ip = i; vp = vc;
        return vo;
    }
};

// ── BOOST stage Q1 — true nodal NPN transistor (Ebers-Moll + Newton) ──────────
struct FkBoost {
    double vB=0.95, vC=13.5, vE=0.33, dcAvg=13.5;
    void reset() { vB=0.95; vC=13.5; vE=0.33; dcAvg=13.5; }
    static inline double lim(double vn, double vo) {
        const double Vt=0.02585, Is=1e-14, vc=Vt*std::log(Vt/(1.41421356*Is));
        if (vn>vc && std::fabs(vn-vo)>2*Vt) { if (vo>0) { double a=1+(vn-vo)/Vt; vn = a>0 ? vo+Vt*std::log(a) : vc; } else vn=Vt*std::log(vn/Vt+1.0); }
        return vn; }
    inline double process(double ain, double injScale) {
        const double Vt=0.02585, Is=1e-14, Bf=220.0, Br=2.0, Vcc=15.0;
        const double Rc=4700, Re=1000, Rb1=100000, Rb2=22000, Rsig=4700;
        double B=vB, C=vC, E=vE;
        for (int it=0; it<8; ++it) {
            Mna m; m.init(5, 2);
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
        dcAvg += 0.0008*(vC-dcAvg);
        return (vC - dcAvg) * 1.78;
    }
};

// ── GK 800RB mono core (preamp → voicing → 4-band EQ → boost → crossover) ─────
struct Fk800Core {
    float fs = 96000.f;
    FkPreamp pre;
    float preDrive = 0.2f, preMakeup = 0.014f;
    RC1 loCutF;  bool loCutOn = false;
    RC1 conHp, conLp;  bool contourOn = false;
    RC1 hbLp, hbHp;    bool hiBoostOn = false;
    RC1 bLp, bHp;   float bassG = 1.f;
    RC1 lmHp, lmLp; float loMidG = 1.f;
    RC1 hmHp, hmLp; float hiMidG = 1.f;
    RC1 tLp, tHp;   float trebG = 1.f;
    FkBoost boostStage;  float boostInj = 0.04f, boostMakeup = 1.f;  bool boostOn = false;
    RC1 xLp, xHp;   float g100 = 1.f, g300 = 1.f;  bool biamp = false;
    // family loudness lift (+7 dB) so the clean GK sits with the SVT/en30 level in
    // the rig; post-clip so it doesn't change the growl onset. Tuned via the harness.
    float outLevel = 2.24f;

    RC1* allRC[15] = { &loCutF,&conHp,&conLp,&hbLp,&hbHp,&bLp,&bHp,&lmHp,&lmLp,&hmHp,&hmLp,&tLp,&tHp,&xLp,&xHp };

    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f; pre.setFs(s);
        for (RC1* p : allRC) p->setT(fs);
        loCutF.set(110.0, true);
        conHp.set(250.0, true);  conLp.set(1000.0, false);
        hbLp.set(2200.0, false); hbHp.set(2200.0, true);
        bLp.set(60.0, false);    bHp.set(60.0, true);
        lmHp.set(125.0, true);   lmLp.set(500.0, false);
        hmHp.set(575.0, true);   hmLp.set(2300.0, false);
        tLp.set(4000.0, false);  tHp.set(4000.0, true);
        xLp.set(500.0, false);   xHp.set(500.0, true);
    }
    void reset() { pre.reset(); boostStage.reset(); for (RC1* p : allRC) p->reset(); }

    void setParams(float volume, float treble, float hiMid, float loMid, float bass,
                   float boostLevel, float xover, float master100, float master300,
                   bool pad, bool loCutOnP, bool contourOnP, bool hiBoostOnP,
                   bool boostOnP, bool biampP) {
        const float padScale = pad ? 0.316f : 1.0f;
        const float v = volume;
        preDrive  = padScale * (0.05f + 0.15f*v + 0.85f*v*v*v*v);
        preMakeup = 6.0f / 214.0f;
        loCutOn = loCutOnP; contourOn = contourOnP; hiBoostOn = hiBoostOnP;
        bassG  = std::pow(10.f, (bass   - 0.5f) * 24.f / 20.f);
        loMidG = std::pow(10.f, (loMid  - 0.5f) * 24.f / 20.f);
        hiMidG = std::pow(10.f, (hiMid  - 0.5f) * 24.f / 20.f);
        trebG  = std::pow(10.f, (treble - 0.5f) * 24.f / 20.f);
        boostOn  = boostOnP;
        boostInj = 0.04f;
        boostMakeup = std::pow(10.f, (boostLevel * 15.f) / 20.f);
        biamp = biampP;
        const double fc = 100.0 + 940.0 * xover;
        xLp.set(fc, false); xHp.set(fc, true);
        g300 = master300 / 0.7f;
        g100 = master100 / 0.7f;
    }

    inline float process(float x) {
        double d = (double)(pre.process((double)(preDrive * x)) * preMakeup);
        if (loCutOn)   d = loCutF.proc(d);
        if (contourOn) { const double bp = conLp.proc(conHp.proc(d)); d -= 0.9 * bp; }
        if (hiBoostOn) d = hbLp.proc(d) + 2.0 * hbHp.proc(d);
        d = bHp.proc(d) + bassG * bLp.proc(d);
        { const double bp = lmLp.proc(lmHp.proc(d)); d += (loMidG - 1.0) * bp; }
        { const double bp = hmLp.proc(hmHp.proc(d)); d += (hiMidG - 1.0) * bp; }
        d = tLp.proc(d) + trebG * tHp.proc(d);
        if (boostOn) d = boostStage.process(d, boostInj) * boostMakeup;
        const double low = xLp.proc(d), high = xHp.proc(d);
        if (biamp) return (float)((low * g300 + high * g100) * outLevel);
        return (float)(d * (0.5 * g300 + 0.5 * g100) * outLevel);
    }
};

} // namespace fk800gk
#endif // FK800_CORE_H
