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
        // Direct trapezoidal one-pole — the CLOSED-FORM of the old 2-node MNA solve
        // (bilinear companion Geq=2C/T), bit-for-bit equivalent but ~10× cheaper:
        // no per-sample Gaussian elimination. With 15 of these per oversampled
        // sample, this is the bulk of FK's CPU; the cut makes it load/run snappy.
        const double Geq = 2.0*C/T, Ieq = Geq*vp + ip, Gr = 1.0/Rr;
        double vo, vc;
        if (!hp) { vo = (in*Gr + Ieq) / (Gr + Geq); vc = vo; }          // lowpass
        else     { vo = (Geq*in - Ieq) / (Geq + Gr); vc = in - vo; }    // highpass
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

// ── GK 800RB POWER AMP — solid-state output stage clipping at the rails ────────
// Two independent amps (60044 sheet): 300W low band (±85 V rails, 4Ω, MJ15022/
// MJ15023 triples) and 100W high band (±60 V rails, 8Ω, MJE15030/MJE15031). A
// real BJT output stage clips nearly HARD at (rail − Vce(sat) − Re·I), flat-
// topping the wave — quite unlike a tube. We model that with a 4th-order soft-
// knee saturator (sharper knee than tanh) plus a gentle supply SAG: the heavy
// 300W rail droops under sustained low-frequency current draw (the manual's
// "push the volume/boost, drop the masters → fatter, can distort"), which softens
// attack the way the real head does when leaned on. The 100W rail barely sags.
struct FkPowerAmp {
    double env = 0.0, aAtk = 0.0, aRel = 0.0;
    double ceil = 1.0, sagDepth = 0.0;
    void setFs(float fs) {
        const double T = 1.0 / ((fs > 0.f) ? fs : 48000.0);
        aAtk = 1.0 - std::exp(-T / 0.010);   // 10 ms current build-up
        aRel = 1.0 - std::exp(-T / 0.220);   // 220 ms rail recovery
    }
    void config(double ceiling, double sag) { ceil = ceiling; sagDepth = sag; }
    void reset() { env = 0.0; }
    inline double process(double x) {
        const double a = std::fabs(x);
        env += (a > env ? aAtk : aRel) * (a - env);
        // rail droops with sustained draw (sag); the high band barely moves
        const double rail = ceil * (1.0 - sagDepth * (env / (env + ceil)));
        const double u  = x / rail;
        const double u2 = u * u;
        // 4th-order soft clip: y = u / (1+u⁴)^¼ → flat-tops like a SS amp, but
        // smooth/stable. sqrt∘sqrt is the cheap RT-safe form of (·)^¼.
        const double y = u / std::sqrt(std::sqrt(1.0 + u2 * u2));
        return rail * y;
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
    FkPowerAmp pa300, pa100;   // 300W low band / 100W high band output stages
    // family loudness lift (+7 dB) so the clean GK sits with the SVT/en30 level in
    // the rig; post-clip so it doesn't change the growl onset. Tuned via the harness.
    float outLevel = 4.0f;

    RC1* allRC[15] = { &loCutF,&conHp,&conLp,&hbLp,&hbHp,&bLp,&bHp,&lmHp,&lmLp,&hmHp,&hmLp,&tLp,&tHp,&xLp,&xHp };

    void setSampleRate(float s) {
        fs = (s > 0.f) ? s : 48000.f; pre.setFs(s);
        for (RC1* p : allRC) p->setT(fs);
        loCutF.set(110.0, true);
        conHp.set(250.0, true);  conLp.set(1000.0, false);
        hbLp.set(2200.0, false); hbHp.set(2200.0, true);
        bLp.set(60.0, false);    bHp.set(60.0, true);
        // 4-band EQ centres from the ACTUAL RC components on the production
        // "NEW 800RB PREAMP" board (sheet 406-0045-D), verified component-by-
        // component. These differ from the Operators Manual's NOMINAL figures
        // (LO-MID "250 Hz" / HI-MID "1 kHz") — the real circuit centres are:
        //   LO-MID = R48 47K + C46 0.01µF  → 1/(2π·47k·10nF)  ≈ 339 Hz
        //   HI-MID = R41 47K + C37 2.2nF   → 1/(2π·47k·2.2nF) ≈ 1539 Hz
        // (Bass 60 Hz / Treble 4 kHz shelves DO match the manual.) Each mid is a
        // peaking band: the 25K pot blends ± a bandpass tap into the main path
        // (d += (G-1)·bandpass); the hp/lp pair brackets the centre at a moderate
        // Q (×2 / ÷2 ⇒ ratio 4 ⇒ Q≈0.67), geometric-mean = centre.
        lmHp.set(170.0, true);   lmLp.set(680.0, false);    // LO-MID  ≈ 339 Hz
        hmHp.set(770.0, true);   hmLp.set(3080.0, false);   // HI-MID  ≈ 1539 Hz
        tLp.set(4000.0, false);  tHp.set(4000.0, true);
        xLp.set(500.0, false);   xHp.set(500.0, true);
        pa300.setFs(fs); pa100.setFs(fs);
        // Clip ceilings (core units, post-master). The 300W amp has more headroom
        // (±85 V vs ±60 V → ~1.4×) and sags more (heavy low-band current draw);
        // the 100W high band clips a touch earlier and barely sags. Set so the
        // default panel (Vol 0.7 / masters 0.8) stays clean and grind appears only
        // when the masters/volume are pushed — A-step harness retunes outLevel.
        pa300.config(1.25, 0.16);   // 300W low band: more headroom, more sag
        pa100.config(0.90, 0.04);   // 100W high band: earlier clip, minimal sag
    }
    void reset() { pre.reset(); boostStage.reset(); pa300.reset(); pa100.reset();
                   for (RC1* p : allRC) p->reset(); }

    void setParams(float volume, float treble, float hiMid, float loMid, float bass,
                   float boostLevel, float xover, float master100, float master300,
                   bool pad, bool loCutOnP, bool contourOnP, bool hiBoostOnP,
                   bool boostOnP, bool biampP) {
        const float padScale = pad ? 0.316f : 1.0f;
        const float v = volume;
        // Drive curve vs the INPUT_CALIBRATION.md contract (hard-played DI peaks
        // at −12 dBFS = 0.25 linear). The op-amp preamp clips when
        // preDrive·x > Vrail/gain = 13.5/213.8 ≈ 0.063, so the clip onset in
        // dBFS is set entirely by this curve. The old 0.05+0.15v+0.85v⁴ put the
        // onset at −14 dBFS for v=0.66 — i.e. the amp GROWLED (≈18% THD) on the
        // contract-level signal at song volumes ("Look Around bass distorsiona,
        // debería sonar limpio"), betraying the design note below that the
        // default panel stays clean. Retuned so the contract level is clean
        // through v≈0.7 (onset −10 dBFS at the 0.70 default, ≈−9 at 0.66) and
        // the GK growl arrives progressively above it (onset −14.5 dBFS at
        // v=0.85, −18.8 at v=1.0) — matching the real 800RB's famous clean
        // headroom with growl only when pushed.
        preDrive  = padScale * (0.03f + 0.10f*v + 0.42f*v*v*v*v);
        preMakeup = 6.0f / 214.0f;
        loCutOn = loCutOnP; contourOn = contourOnP; hiBoostOn = hiBoostOnP;
        // ±15 dB per band (GK 800RB published spec; 500K/47K pot ratio supports it).
        // At centre (0.5) every band is exactly unity → the default tone is unchanged.
        bassG  = std::pow(10.f, (bass   - 0.5f) * 30.f / 20.f);
        loMidG = std::pow(10.f, (loMid  - 0.5f) * 30.f / 20.f);
        hiMidG = std::pow(10.f, (hiMid  - 0.5f) * 30.f / 20.f);
        trebG  = std::pow(10.f, (treble - 0.5f) * 30.f / 20.f);
        boostOn  = boostOnP;
        boostInj = 0.04f;
        boostMakeup = std::pow(10.f, (boostLevel * 15.f) / 20.f);
        biamp = biampP;
        const double fc = 100.0 + 940.0 * xover;
        xLp.set(fc, false); xHp.set(fc, true);
        // Master volumes (300W low / 100W high). The old `master/0.7` only spanned
        // 0..1.43× (≈ +3 dB at full) → "the masters barely raise the volume". Give
        // them real authority: an audio-ish taper up to ~2.4× (≈ +7.6 dB at full),
        // with the default 0.7 sitting a touch hotter so the amp isn't quiet.
        // Real master taper: 0 → fully silent (like the real GK), audio taper up to
        // 2.4× at max. The masters are kept supplied on load by the `_static` block
        // in rs_knob_to_vst_param.json (BT880B) so a fresh load never sits at 0 by
        // accident — only a deliberate knob-to-min mutes the amp, as it should.
        g300 = std::pow(master300, 1.8f) * 2.4f;   // 0→silent · 0.8→1.36× · 1.0→2.4×
        g100 = std::pow(master100, 1.8f) * 2.4f;

        // FLATTENING MAKEUP (mirrors the SVT recipe). The raw GK level ramps ~25 dB
        // across the Volume knob (preDrive scales steeply v→v⁴), so a FIXED outLevel
        // left FK far quieter than the SVT at low/mid Volume and louder at max — not
        // a usable, loudness-matched amp. This Volume-dependent makeup holds the
        // OUTPUT RMS ~flat (≈ −11 dB, the SVT/family level) so the Volume knob reads
        // as growl-AMOUNT (like the SVT Gain), not raw loudness. Tuned against
        // calibrate_amp_core.py so the FK sweep tracks the SVT-CL sweep within ~1 dB.
        // FLATTENING makeup + level calibration. The raw level ramps ~25 dB across
        // Volume (preDrive v→v⁴); the −25.1·v term holds output ~flat so Volume reads
        // as growl-AMOUNT. The 34.4 base was raised +4 dB from the old 30.4 after
        // measuring FK vs the SamplegSBTCL on the real bass DIs (ui_public_inputs):
        // FK sat ~4 dB under the SBT on sustained/quiet content. NOTE: FK and SBT
        // have different DYNAMICS (the SBT's 6550 power amp compresses hard; FK is
        // solid-state and doesn't), so they can't be matched at every input by a
        // fixed gain — FK tracks playing dynamics more. Matching the SBT's CONSTANT
        // loudness would need an output compressor on FK (deferred).
        // BASE REVERTED 49.4 → 30.4 (2026-06-23): the +15 dB (and the +4) were tuned
        // against QUIET reference recordings / the collision-era stranded-mute (FK fell
        // below the −45 normalizer gate). With the DGL collision fixed AND a HOT real
        // bass (the player's live DI, far hotter than the quiet WAVs), ×295 output
        // makeup slammed rbAmpLvl → "muy distorsionado". 30.4 is the clean, family-
        // matched base; the working final-normalizer lifts the (now lower) output, and
        // a real bass clears the gate easily without crushing the output stage.
        // Retuned with the preDrive curve above. Matching the OLD level at
        // mid/high Volume would be wrong: that loudness was manufactured by the
        // preamp saturating (flat-topped output pinned at the rbAmpLvl
        // ceiling) — a first attempt that "compensated" the clean region
        // (+4.5 dB) drove the now-clean signal straight into the output knee
        // (18% ceiling-clipped samples). Base 9.5 keeps the −12 dBFS contract
        // sine's peaks clear of the 0.90 output knee through the clean range
        // (harness: peak ≈0.86 at v=0.66, THD 0.39% there / 0.66% at the 0.70
        // default, growl arriving from the PREAMP only: 11% at v=0.85, 24% at
        // v=1.0). The final-chain leveler owns the family loudness match.
        float mkDb = 9.5f - 13.5f * v;
        if (pad) mkDb += 10.0f;     // the −10 dB input pad drops drive → restore level
        outLevel = std::pow(10.0f, 0.05f * mkDb);
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
        // Crossover → master → POWER AMP. Each amp clips at its own rail (real
        // topology: the master pot feeds the power-amp input, which flat-tops at
        // the supply). Pushing master/volume drives it into clip; outLevel after
        // is the non-physical family loudness-match (retuned by the A-step harness).
        const double low = xLp.proc(d), high = xHp.proc(d);
        if (biamp) {
            const double a300 = pa300.process(low  * g300);   // low band → 300W/4Ω
            const double a100 = pa100.process(high * g100);   // high band → 100W/8Ω
            return (float)((a300 + a100) * outLevel);
        }
        // Full-range: both amps see the whole band, each scaled by its own master
        // and clipping independently; halved to one mixed output (one rig channel).
        const double a300 = pa300.process(d * g300);
        const double a100 = pa100.process(d * g100);
        return (float)(0.5 * (a300 + a100) * outLevel);
    }
};

} // namespace fk800gk
#endif // FK800_CORE_H
