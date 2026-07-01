#ifndef SHARKE_CORE_H
#define SHARKE_CORE_H
//
// SharkeCore — Hartke HA3500 / HA5000 hybrid bass head, circuit-real. SHARED by
// both sister amps (sharke_hb3500 / sharke_hb5000): identical dual-preamp
// topology from the Samson/Hartke main board (HA3500 4005182800 / HA5000
// 4005182801, schematics in ~/Downloads/hartke_ha3500.pdf, Hartke 5000.pdf).
// They differ ONLY in the graphic-EQ band table (passed via setEqFreqs) and the
// power section (HA3500 = one 350 W SS power amp; HA5000 = dual-mono 2×250 W) —
// the plugin wraps one core per channel and sets the headroom accordingly.
//
//   IN → Passive/Active pad → [ 12AX7 tube path (nodal triode) ]
//                            + [ solid-state op-amp path (±15 V rail clip) ]  (blend)
//      → built-in compressor (envelope → JFET VCR divider)
//      → 10-band graphic EQ (peaking biquads, ±12 dB, EQ In switch)
//      → variable High-Pass (20–200 Hz) / Low-Pass (2k–20k) tone filters
//      → Volume (drives the power amp) → SS power amp (flat-top clip + light sag)
//
// The NONLINEAR stages (Newton 12AX7 + op-amp rail clip + SS power clip) are run
// 2× oversampled by the plugin wrapper. The graphic EQ and HP/LP are LINEAR so
// they are exact closed-form biquads / one-poles — the old per-sample nodal MFB
// (10 Gaussian solves/sample) and RC1 solves were replaced 1:1 by their transfer
// functions (huge CPU cut, identical response). See REAL_TUBE_AMP_GUIDE.md.
//
#include <cmath>

namespace sharke {

// ── Tiny fixed-size Modified Nodal Analysis solver (RT-safe, no heap) ─────────
// Node 0 = gnd; nodes 1..nN unknown voltages; nX aux currents. Used only for the
// two NONLINEAR preamp stages (12AX7 triode + op-amp rail clip).
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

// ── 12AX7 TUBE PREAMP — nodal triode (Koren model + Newton/sample) ────────────
// Common-cathode stage, self-bias, unbypassed cathode → flat response + tube
// clip. The plate swing clips ASYMMETRICALLY (B+/cutoff) = the warm tube path.
struct Tube {
    double vG=0, vP=200, vK=1.4, dcAvg=200.0, T=1.0/48000.0;
    void setT(float fs) { T = 1.0 / ((fs>0.f)?fs:48000.0); }
    void reset() { vG=0; vP=200; vK=1.4; dcAvg=200.0; }
    static inline double Ip(double vgk, double vpk) {
        const double MU=100, EX=1.4, KG1=1060, KP=600, KVB=300;
        if (vpk < 0) vpk = 0;
        double e1 = (vpk/KP)*std::log(1.0 + std::exp(KP*(1.0/MU + vgk/std::sqrt(KVB + vpk*vpk))));
        if (e1 < 0) e1 = 0; return std::pow(e1, EX)/KG1*2.0; }
    inline double process(double vin) {        // vin = grid drive; returns AC plate swing
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
        // common-cathode plate INVERTS; negate so the tube path is in phase with
        // the SS path (otherwise they cancel when blended ~equally).
        return -(P - dcAvg) * (1.0/40.0);
    }
};

// ── SOLID-STATE PREAMP — nodal non-inverting op-amp (gain ~11), ±13.5 V rail ──
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

// ── Closed-form biquad (graphic-EQ peaking band; LINEAR → no oversampling) ────
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void reset(){ z1=z2=0.f; }
    inline float process(float x){ const float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    void bypass(){ b0=1;b1=b2=a1=a2=0; }
    void peaking(float fc,float dB,float Q,float fs){ if(fc>fs*0.49f)fc=fs*0.49f;
        const float A=std::pow(10.f,dB/40.f), w=6.2831853f*fc/fs, c=std::cos(w), al=std::sin(w)/(2.f*Q);
        const float a0=1+al/A;
        b0=(1+al*A)/a0; b1=(-2*c)/a0; b2=(1-al*A)/a0; a1=(-2*c)/a0; a2=(1-al/A)/a0; }
};

// ── One-pole TPT HP/LP (variable tone filters; 6 dB/oct like the real RC) ─────
struct OnePole {
    float G=0.f, s=0.f; bool hp=false;
    void set(float fs,float fc,bool isHp){ hp=isHp; const float g=std::tan(3.14159265f*fc/fs); G=g/(1.f+g); }
    void reset(){ s=0.f; }
    inline float proc(float x){ const float v=(x-s)*G; const float lp=v+s; s=lp+v; return hp?(x-lp):lp; }
};

// ── SS power amp — high-headroom flat-top clip + light supply sag ─────────────
// Hartke power is a stiff bipolar/MOSFET stage: clean with lots of headroom, only
// hardens (flat-tops) near the rails when cranked. 4th-order soft knee `u/(1+u⁴)¼`
// is sharper than tanh = the SS flat-top. ceil sets headroom (HA3500 350 W has a
// touch more than the HA5000's 250 W/ch); sag droops the rail under sustained load.
struct SsPowerAmp {
    float ceil=2.0f, sagAmt=0.04f, env=0.f, atk=0.f, rel=0.f;
    void set(float fs,float headroom,float sag){ ceil=headroom; sagAmt=sag;
        atk=std::exp(-1.f/(0.005f*fs)); rel=std::exp(-1.f/(0.12f*fs)); }
    void reset(){ env=0.f; }
    inline float proc(float x){
        const float a=std::fabs(x), c=(a>env)?atk:rel; env=c*env+(1.f-c)*a;
        const float rail=ceil*(1.f - sagAmt*env/(env+ceil));
        const float u=x/rail, u2=u*u;
        return rail * u / std::sqrt(std::sqrt(1.f + u2*u2));
    }
};

// ── The amp core (one channel) ───────────────────────────────────────────────
struct SharkeCore {
    static const int kMaxEq = 10;
    float fs = 48000.f;
    Tube tube; SsPre ss;
    float tubeDrive=1, ssDrive=1, master=1;
    // compressor: envelope detector → JFET VCR gain cell
    bool compOn=false; float env=0, atk=0, rel=0, compThr=1, compAmt=0, compMk=1;
    static inline float msC(float ms, float fs){ return std::exp(-1.f/(0.001f*ms*fs)); }
    Biquad eq[kMaxEq];  float eqFreq[kMaxEq]={30,64,125,250,500,1000,2000,4000,8000,16000};
    int nEq=10;         bool eqIn=true;
    OnePole hpf, lpf;   bool hpfOn=false, lpfOn=false;
    SsPowerAmp power;
    // Remembered so a sample-rate change can't revert the per-model power
    // headroom set once at construction (setParams doesn't re-send it).
    float pwrHeadroom=2.0f, pwrSag=0.04f;

    // HA3500 350W has slightly more headroom than the HA5000's 250W/ch.
    void setSampleRate(float s){ fs=(s>0)?s:48000.f; atk=msC(4.f,fs); rel=msC(120.f,fs);
        tube.setT(s);
        hpf.set(fs,30.f,true); lpf.set(fs,8000.f,false);
        for (int i=0;i<kMaxEq;++i){ eq[i].bypass(); }
        power.set(fs, pwrHeadroom, pwrSag); }
    // The plugin sets the per-model EQ band table (HA3500 ...4k/8k/16k vs
    // HA5000 ...3k/5k/8k) and the power-amp headroom once at construction.
    void setEqFreqs(const float* f, int n){ nEq = (n<kMaxEq)?n:kMaxEq; for (int i=0;i<nEq;++i) eqFreq[i]=f[i]; }
    void setPowerHeadroom(float h, float sag){ pwrHeadroom=h; pwrSag=sag; power.set(fs, h, sag); }

    void reset(){ tube.reset(); for (int i=0;i<kMaxEq;++i) eq[i].reset();
        hpf.reset(); lpf.reset(); power.reset(); env=0; }

    // p layout (shared by both amps): 0 Tube,1 Solid,2 Comp,3 LowPass,4 HighPass,
    // 5 Volume, 6..15 EQ bands, 16 Active, 17 EQ In.
    void setParams(const float* p) {
        const float padActive = (p[16] > 0.5f) ? 0.20f : 1.0f;   // Active jack pad
        tubeDrive = p[0] * (0.6f + p[0]*p[0]*16.0f) * padActive;  // grid drive into 12AX7
        ssDrive   = p[1] * (0.6f + p[1]*p[1]*16.0f) * padActive;  // drive into SS op-amp

        compOn  = p[2] > 0.001f;
        compThr = 0.35f - p[2]*0.28f;
        compAmt = p[2];
        compMk  = 1.0f + p[2]*0.35f;

        eqIn = p[17] > 0.5f;
        for (int i=0;i<nEq;++i){
            if (eqIn) eq[i].peaking(eqFreq[i], (p[6+i]-0.5f)*24.f, 1.4f, fs);
            else      eq[i].bypass();
        }

        // CENTER-DETENT tone filters: 0.5 = open/off (neutral default). The High
        // Pass engages turning UP from center (0.5→1.0 = 20→200 Hz, cuts lows); the
        // Low Pass engages turning DOWN from center (0.5→0.0 = 20k→2k, cuts highs).
        // At noon (0.5/0.5) both are bypassed so the default tone is unchanged.
        hpfOn = p[4] > 0.52f;  if (hpfOn) hpf.set(fs, 20.f   * std::pow(10.f,(p[4]-0.5f)*2.f), true);
        lpfOn = p[3] < 0.48f;  if (lpfOn) lpf.set(fs, 2000.f * std::pow(10.f, p[3]*2.f),       false);

        master = p[5] / 0.7f;
    }

    inline float process(float x) {
        // dual preamp blend — nodal 12AX7 + nodal op-amp SS
        double s = (tube.process((double)(tubeDrive * x)) + ss.process((double)(ssDrive * x))) * 0.55;

        // compressor — envelope → JFET VCR divider
        if (compOn) {
            const double a = std::fabs(s);
            const double c = (a > env) ? atk : rel;
            env = c*env + (1.0-c)*a;
            const double over = (env > compThr) ? (env - compThr) : 0.0;
            const double ctl  = over * compAmt * 5.0;
            double gain = 1.0;
            if (ctl > 1e-6) { const double Ron=400.0, Rs=4700.0; double rds = Ron*3.0/ctl;
                if (rds < Ron) rds = Ron; gain = rds/(Rs+rds); }
            s = s * gain * compMk;
        }

        // 10-band graphic EQ (peaking biquads)
        float y = (float)s;
        if (eqIn) for (int i=0;i<nEq;++i) y = eq[i].process(y);

        // variable HP/LP tone filters
        if (hpfOn) y = hpf.proc(y);
        if (lpfOn) y = lpf.proc(y);

        // Volume drives the SS power amp (cranking Volume = power-amp flat-top)
        y *= master;
        y = power.proc(y);
        return y;
    }
};

} // namespace sharke
#endif // SHARKE_CORE_H
