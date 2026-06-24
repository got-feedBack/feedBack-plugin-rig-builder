#pragma once
// Guitarix-style tube-amp building blocks, done the REAL way (circuit simulation,
// our own code + the public Koren/Yeh physics -- NOT Guitarix's GPL source):
//
//   TubeStage models one cathode-biased 12AX7 gain stage exactly like Guitarix's
//   `tubestageF`: a PURE plate-transfer table Ftube(Vgrid)->Vplate (volts, cathode
//   NOT baked in) wrapped in the real cathode auto-bias feedback loop. The plate
//   signal, scaled by Rk/(Rp+Ranode) and slowed by the cathode-bypass cap (lpfk),
//   feeds back to the grid -> the bias drifts with level (compression + asymmetry)
//   and the stage saturates against its own load line. Stacking these with a
//   pre-gain between them reproduces a real amp's clean->crunch->plateau ramp.
//
#include "koren12ax7_ftube.h"   // PURE 12AX7 plate transfer + Ranode (our tables)
#include "koren12ay7_ftube.h"   // PURE 12AY7 (low-mu warm preamp triode)
#include "koren12at7_ftube.h"   // PURE 12AT7 / ECC81 (high-gm phase inverter triode)
#include "koren6eu7_ftube.h"    // PURE 6EU7 low-noise high-mu triode
#include "koren6sl7_ftube.h"    // PURE 6SL7-GT high-mu octal triode
#include "koren6sf5_ftube.h"    // PURE 6SF5 high-mu single triode
#include "koren12au7_ftube.h"   // PURE 12AU7 / ECC82 low-mu triode
#include "koren7199t_ftube.h"   // PURE 7199 triode section
#include "koren_el84_ftube.h"   // PURE EL84 pentode plate transfer
#include "koren6bm8_ftube.h"    // PURE 6BM8/ECL82 power pentode
#include "koren6v6_ftube.h"     // PURE 6V6 power pentode
#include "koren6l6_ftube.h"     // PURE legacy 6L6 power pentode
#include "koren6l6g_ftube.h"    // PURE 6L6G lower-voltage glass 6L6 power pentode
#include "koren5881_ftube.h"    // PURE 5881 beam pentode
#include "koren6l6gc_ftube.h"   // PURE 6L6GC high-power beam pentode
#include "koren_kt66_ftube.h"   // PURE KT66 beam tetrode
#include "koren_el34_ftube.h"   // PURE EL34 power pentode
#include "koren_ef86_ftube.h"   // PURE EF86 small-signal pentode (DC30 channel 2)
#include "koren6550_ftube.h"    // PURE 6550 beam pentode (Ampeg SVT-CL, 6x PP)
#include "koren_kt88_ftube.h"   // PURE KT88 beam tetrode (Trace V-Type V8, 8x PP)
#include "koren5879_ftube.h"    // PURE 5879 sharp-cutoff pentode
#include "koren7199p_ftube.h"   // PURE 7199 pentode section
#include <cmath>

namespace rbtube {

static constexpr float kPi = 3.14159265358979f;
static inline float dn(float v) { return std::fabs(v) < 1e-15f ? 0.0f : v; }  // flush denormals (glitch guard)

namespace PotTaper {
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float linear(float v) { return clamp01(v); }
static inline float audio(float v, float curve = 1.7f) { return std::pow(clamp01(v), std::fmax(1.0f, curve)); }
static inline float reverseAudio(float v, float curve = 1.7f) { return 1.0f - std::pow(1.0f - clamp01(v), std::fmax(1.0f, curve)); }
static inline float sCurve(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float switchBlend(float v, float center = 0.5f, float width = 0.08f) {
    const float lo = center - width * 0.5f, hi = center + width * 0.5f;
    return sCurve((clamp01(v) - lo) / std::fmax(1.0e-6f, hi - lo));
}
} // namespace PotTaper

// Tube "traits": pick which pure-transfer table a stage uses, so one TubeStage/
// PowerAmpPP template serves every amp. Add an amp's tubes here (generate the table
// in gx_tube.py first). ftube/ranode are the generated lookups (namespace rbtube).
struct Tube12AX7 {
    static constexpr float cGridCathodePf = 1.6f;
    static constexpr float cGridPlatePf = 1.7f;
    static inline float ftube(int t,float v){return AX7_ftube(t,v);}
    static inline float ranode(int t,float v){return AX7_ranode(t,v);}
};
struct Tube12AY7 {
    static constexpr float cGridCathodePf = 1.3f;
    static constexpr float cGridPlatePf = 1.3f;
    static inline float ftube(int t,float v){return AY7_ftube(t,v);}
    static inline float ranode(int t,float v){return AY7_ranode(t,v);}
};
struct Tube12AT7 {
    static constexpr float cGridCathodePf = 2.2f;  // input G to (H+K), Tung-Sol 12AT7.pdf
    static constexpr float cGridPlatePf = 1.5f;    // G to P, Tung-Sol 12AT7.pdf
    static inline float ftube(int t,float v){return AT7_ftube(t,v);}
    static inline float ranode(int t,float v){return AT7_ranode(t,v);}
};
struct Tube6EU7 {
    static constexpr float cGridCathodePf = 1.6f;  // input G to (H+K), RCA 6EU7.pdf
    static constexpr float cGridPlatePf = 1.5f;    // G to P, RCA 6EU7.pdf
    static inline float ftube(int t,float v){return EU7_ftube(t,v);}
    static inline float ranode(int t,float v){return EU7_ranode(t,v);}
};
struct Tube6SL7 {
    static constexpr float cGridCathodePf = 3.2f;  // average of both sections, RCA 6SL7GT.pdf
    static constexpr float cGridPlatePf = 2.8f;    // G to P, RCA 6SL7GT.pdf
    static inline float ftube(int t,float v){return SL7_ftube(t,v);}
    static inline float ranode(int t,float v){return SL7_ranode(t,v);}
};
struct Tube6SF5 {
    static constexpr float cGridCathodePf = 2.0f;  // conservative high-mu triode input C
    static constexpr float cGridPlatePf = 1.6f;    // conservative 12AX7-family Cgp
    static inline float ftube(int t,float v){return SF5_ftube(t,v);}
    static inline float ranode(int t,float v){return SF5_ranode(t,v);}
};
struct Tube12AU7 {
    static constexpr float cGridCathodePf = 1.6f;  // G to K, Brimar 12AU7.pdf
    static constexpr float cGridPlatePf = 1.5f;    // G to A, Brimar 12AU7.pdf
    static inline float ftube(int t,float v){return AU7_ftube(t,v);}
    static inline float ranode(int t,float v){return AU7_ranode(t,v);}
};
struct Tube7199T {
    static constexpr float cGridCathodePf = 2.3f;  // triode input G to H+K, Sylvania 7199.pdf
    static constexpr float cGridPlatePf = 2.0f;    // triode G to P, Sylvania 7199.pdf
    static inline float ftube(int t,float v){return N99T_ftube(t,v);}
    static inline float ranode(int t,float v){return N99T_ranode(t,v);}
};
struct TubeEF86 {
    static constexpr float cGridCathodePf = 3.8f;   // Cg1(all except anode), EF86.pdf
    static constexpr float cGridPlatePf = 0.05f;    // Cag1 max, EF86.pdf
    static inline float ftube(int t,float v){return EF86_ftube(t,v);}
    static inline float ranode(int t,float v){return EF86_ranode(t,v);}
};
struct Tube5879 {
    static constexpr float cGridCathodePf = 2.7f;   // input capacitance, RCA 5879.pdf
    static constexpr float cGridPlatePf = 0.11f;    // G1 to plate max, RCA 5879.pdf
    static inline float ftube(int t,float v){return N5879_ftube(t,v);}
    static inline float ranode(int t,float v){return N5879_ranode(t,v);}
};
struct Tube7199P {
    static constexpr float cGridCathodePf = 5.0f;   // pentode input capacitance, Sylvania 7199.pdf
    static constexpr float cGridPlatePf = 0.06f;    // G1 to plate max, Sylvania 7199.pdf
    static inline float ftube(int t,float v){return N99P_ftube(t,v);}
    static inline float ranode(int t,float v){return N99P_ranode(t,v);}
};
struct TubeEL84  { static inline float ftube(int t,float v){return EL84_ftube(t,v);} };
struct Tube6BM8  { static inline float ftube(int t,float v){return BM8_ftube(t,v);} };
struct Tube6V6   { static inline float ftube(int t,float v){return V6_ftube(t,v);} };
struct Tube6L6   { static inline float ftube(int t,float v){return L6_ftube(t,v);} };
struct Tube6L6G  { static inline float ftube(int t,float v){return L6G_ftube(t,v);} };
struct Tube5881  { static inline float ftube(int t,float v){return T5881_ftube(t,v);} };
struct Tube6L6GC { static inline float ftube(int t,float v){return L6GC_ftube(t,v);} };
struct TubeKT66  { static inline float ftube(int t,float v){return KT66_ftube(t,v);} };
struct TubeEL34  { static inline float ftube(int t,float v){return EL34P_ftube(t,v);} };
struct Tube6550  { static inline float ftube(int t,float v){return T6550_ftube(t,v);} };
struct TubeKT88  { static inline float ftube(int t,float v){return TKT88_ftube(t,v);} };

// one-pole low-pass (anti-alias / Miller / cathode-bypass / coupling rolloff)
struct LP1 {
    float a = 0.0f, y1 = 0.0f;
    void set(float sr, float hz) { float w = 2.0f * kPi * std::fmin(hz, sr * 0.49f) / sr; a = w / (1.0f + w); }
    inline float process(float x) { y1 += a * (x - y1); y1 = dn(y1); return y1; }
    void reset() { y1 = 0.0f; }
};

// one-pole high-pass = DC blocker (coupling cap)
struct HP1 {
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
    void set(float sr, float hz) { float w = 2.0f * kPi * hz / sr; a = 1.0f / (1.0f + w); }
    inline float process(float x) { float y = a * (y1 + x - x1); x1 = x; y1 = dn(y); return y; }
    void reset() { x1 = y1 = 0.0f; }
};

// Coupling capacitor + grid-leak path with grid-current charging. When the next
// grid is driven positive, the cap charges and shifts that grid negative until
// the grid leak bleeds it back down: blocking distortion / recovery.
struct CouplingCapGridLeak {
    HP1 coupling;
    float charge = 0.0f;
    float chargeA = 0.0f, leakA = 0.0f;
    float threshold = 0.12f, strength = 0.60f, maxShift = 2.0f;

    void set(float sr, float gridLeakOhms, float couplingCapF, float gridStopOhms,
             float thresholdV = 0.12f, float strengthV = 0.60f, float maxShiftV = 2.0f) {
        const float rLeak = std::fmax(1.0f, gridLeakOhms);
        const float cap = std::fmax(1.0e-12f, couplingCapF);
        const float fc = 1.0f / (2.0f * kPi * rLeak * cap);
        coupling.set(sr, fc);

        const float chargeSec = std::fmax(40.0e-6f, std::fmax(1.0f, gridStopOhms) * cap);
        const float leakSec = std::fmax(0.010f, rLeak * cap);
        chargeA = 1.0f - std::exp(-1.0f / (chargeSec * sr));
        leakA = 1.0f - std::exp(-1.0f / (leakSec * sr));
        threshold = thresholdV;
        strength = strengthV;
        maxShift = maxShiftV;
    }

    inline float process(float x, float gainToGrid = 1.0f) {
        const float y = coupling.process(x) * gainToGrid;
        const float gridV = y - charge;
        const float over = std::fmax(0.0f, gridV - threshold);
        if (over > 0.0f) {
            const float target = std::fmin(maxShift, over * strength);
            charge += (target - charge) * chargeA;
        } else {
            charge -= charge * leakA;
        }
        if (charge < 0.0f) charge = 0.0f;
        if (charge > maxShift) charge = maxShift;
        return dn(y - charge);
    }

    void reset() { coupling.reset(); charge = 0.0f; }
};

// Tube rectifier / reservoir-cap supply sag. This is intentionally a one-node
// B+ model: rectifier resistance + first filter cap set the attack, downstream
// load bleed sets the release, and the returned scale modulates the driven amp.
struct RectifierSag {
    float sag = 0.0f, atk = 0.0f, rel = 0.0f, depth = 0.22f, minScale = 0.72f;

    void set(float sr, float rectifierOhms, float reservoirUf, float releaseSec,
             float depthV, float minScaleV = 0.72f) {
        const float cap = std::fmax(1.0e-6f, reservoirUf * 1.0e-6f);
        const float attackSec = std::fmax(0.0015f, std::fmax(1.0f, rectifierOhms) * cap);
        atk = 1.0f - std::exp(-1.0f / (attackSec * sr));
        rel = 1.0f - std::exp(-1.0f / (std::fmax(0.020f, releaseSec) * sr));
        depth = depthV;
        minScale = minScaleV;
    }

    inline float process(float load) {
        const float e = std::fmax(0.0f, load);
        const float target = e / (1.0f + e);
        sag += (target - sag) * (target > sag ? atk : rel);
        float scale = 1.0f - depth * sag;
        if (scale < minScale) scale = minScale;
        if (scale > 1.0f) scale = 1.0f;
        return scale;
    }

    void reset() { sag = 0.0f; }
};

struct SupplyScales {
    float power = 1.0f;
    float screen = 1.0f;
    float preamp = 1.0f;
};

struct BPlusNode {
    float sag = 0.0f, atk = 0.0f, rel = 0.0f, depth = 0.12f, minScale = 0.80f, track = 0.25f;

    void set(float sr, float seriesOhms, float capUf, float releaseSec,
             float depthV, float minScaleV, float upstreamTrackV) {
        const float cap = std::fmax(1.0e-6f, capUf * 1.0e-6f);
        const float attackSec = std::fmax(0.0010f, std::fmax(1.0f, seriesOhms) * cap);
        atk = 1.0f - std::exp(-1.0f / (attackSec * sr));
        rel = 1.0f - std::exp(-1.0f / (std::fmax(0.020f, releaseSec) * sr));
        depth = depthV;
        minScale = minScaleV;
        track = upstreamTrackV;
    }

    inline float process(float load, float upstreamSag = 0.0f) {
        const float local = std::fmax(0.0f, load);
        const float target = local / (1.0f + local) + track * std::fmax(0.0f, upstreamSag);
        sag += (target - sag) * (target > sag ? atk : rel);
        float scale = 1.0f - depth * sag;
        if (scale < minScale) scale = minScale;
        if (scale > 1.0f) scale = 1.0f;
        return scale;
    }

    void reset() { sag = 0.0f; }
};

// Three-node B+ chain: rectifier/reservoir -> screen/PI node -> preamp node.
// This keeps rectifier and filter-cap behavior tied to component values while
// letting each amp decide how much the power, PI and preamp loads pull on B+.
struct MultiNodeBPlus {
    BPlusNode powerNode, screenNode, preampNode;

    void set(float sr,
             float rectifierOhms, float reservoirUf,
             float screenDropOhms, float screenUf,
             float preampDropOhms, float preampUf,
             float powerDepth, float screenDepth, float preampDepth,
             float releaseSec = 0.18f) {
        powerNode.set(sr, rectifierOhms, reservoirUf, releaseSec, powerDepth, 0.72f, 0.0f);
        screenNode.set(sr, screenDropOhms, screenUf, releaseSec * 1.20f, screenDepth, 0.76f, 0.45f);
        preampNode.set(sr, preampDropOhms, preampUf, releaseSec * 1.65f, preampDepth, 0.82f, 0.65f);
    }

    void setGZ34Ac30(float sr, float hot = 0.5f) {
        set(sr,
            115.0f, 32.0f,       // GZ34 + reservoir
            1200.0f, 16.0f,      // screen/PI filter node
            10000.0f, 16.0f,     // preamp dropping resistor/filter node
            0.20f + 0.08f * hot,
            0.14f + 0.05f * hot,
            0.070f + 0.025f * hot,
            0.19f);
    }

    inline SupplyScales process(float powerLoad, float screenLoad, float preampLoad) {
        SupplyScales s;
        s.power = powerNode.process(powerLoad);
        s.screen = screenNode.process(screenLoad, 1.0f - s.power);
        s.preamp = preampNode.process(preampLoad, 1.0f - s.screen);
        return s;
    }

    void reset() { powerNode.reset(); screenNode.reset(); preampNode.reset(); }
};

// Input capacitance of a voltage-gain triode stage:
//   Cin ~= Cgk + Cgp * (1 + |Av|)
// This is the Miller effect. Guitarix approximates this in many Faust amps with
// fixed inter-stage low-pass filters around 6.5 kHz; this helper keeps the same
// audible role but derives the cutoff from tube capacitance + source resistance.
template<class TUBE>
struct MillerLowPassT {
    LP1 lp;
    float hz = 20000.0f;
    void set(float sr, float sourceOhms, float voltageGainAbs, float strayPf = 0.0f) {
        const float gain = std::fmax(0.0f, voltageGainAbs);
        const float capPf = strayPf + TUBE::cGridCathodePf + TUBE::cGridPlatePf * (1.0f + gain);
        const float capF = std::fmax(1.0e-12f, capPf * 1.0e-12f);
        const float r = std::fmax(1.0f, sourceOhms);
        hz = 1.0f / (2.0f * kPi * r * capF);
        hz = std::fmax(1200.0f, std::fmin(hz, sr * 0.45f));
        lp.set(sr, hz);
    }
    inline float process(float x) { return lp.process(x); }
    void reset() { lp.reset(); }
};
using Miller12AX7 = MillerLowPassT<Tube12AX7>;
using Miller12AY7 = MillerLowPassT<Tube12AY7>;
using Miller12AT7 = MillerLowPassT<Tube12AT7>;
using Miller6EU7  = MillerLowPassT<Tube6EU7>;
using Miller6SL7  = MillerLowPassT<Tube6SL7>;
using Miller6SF5  = MillerLowPassT<Tube6SF5>;
using Miller12AU7 = MillerLowPassT<Tube12AU7>;
using Miller7199T = MillerLowPassT<Tube7199T>;
using MillerEF86  = MillerLowPassT<TubeEF86>;
using Miller5879  = MillerLowPassT<Tube5879>;
using Miller7199P = MillerLowPassT<Tube7199P>;

// Yeh tone-stack (Yeh & Smith, DAFx-06 — academic/public model, NOT GPL source): the
// 3rd-order analog transfer function of a real 3-band R/C tone network, computed from
// the component values + the three pot positions (t treble, m mid, l bass), then
// bilinear-transformed to a digital IIR. Reusable: `setComponents()` with that amp's
// R1..R4/C1..C3. Verified offline (digital response == analog to <0.01 dB).
struct ToneStackYeh {
    double R1=220e3,R2=220e3,R3=220e3,R4=100e3, C1=470e-12,C2=100e-9,C3=47e-9;
    double b0=0,b1=0,b2=0,b3=0, a1=0,a2=0,a3=0;            // digital coeffs (a0 == 1)
    double x1=0,x2=0,x3=0, y1=0,y2=0,y3=0;
    void setComponents(double r1,double r2,double r3,double r4,double c1,double c2,double c3){
        R1=r1;R2=r2;R3=r3;R4=r4;C1=c1;C2=c2;C3=c3;
    }
    void update(float sr, float t, float m, float l){
        double B1 = t*C1*R1 + m*C3*R3 + l*(C1*R2+C2*R2) + (C1*R3+C2*R3);
        double B2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4) - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                  + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                  + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                  + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                  + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        double B3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                  - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                  + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                  + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                  + t*l*C1*C2*C3*R1*R2*R4;
        double A0 = 1.0;
        double A1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4) + m*C3*R3 + l*(C1*R2 + C2*R2);
        double A2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                  + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                  - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                  + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                  + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        double A3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                  - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                  + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4)
                  + l*C1*C2*C3*R1*R2*R4 + C1*C2*C3*R1*R3*R4;
        // bilinear s = c(1-z^-1)/(1+z^-1), c=2fs, ×(1+z^-1)^3 (fs>>fc, no prewarp needed)
        double c = 2.0*(double)sr, cc = c*c, ccc = cc*c;
        double n0 =        B1*c + B2*cc + B3*ccc;
        double n1 =        B1*c - B2*cc - 3.0*B3*ccc;
        double n2 =       -B1*c - B2*cc + 3.0*B3*ccc;
        double n3 =       -B1*c + B2*cc -     B3*ccc;
        double d0 = A0   + A1*c + A2*cc + A3*ccc;
        double d1 = 3*A0 + A1*c - A2*cc - 3.0*A3*ccc;
        double d2 = 3*A0 - A1*c - A2*cc + 3.0*A3*ccc;
        double d3 = A0   - A1*c + A2*cc -     A3*ccc;
        b0=n0/d0; b1=n1/d0; b2=n2/d0; b3=n3/d0; a1=d1/d0; a2=d2/d0; a3=d3/d0;
    }
    inline float process(float x){
        double y = b0*x + b1*x1 + b2*x2 + b3*x3 - a1*y1 - a2*y2 - a3*y3;
        x3=x2; x2=x1; x1=x; y3=y2; y2=y1; y1=y;
        return dn((float)y);
    }
    void reset(){ x1=x2=x3=y1=y2=y3=0; }
};

// 5E3 Tweed Deluxe single TONE control — the REAL circuit (1M pot R10, 500pF treble
// bypass C4, .0047uF C5 to gnd, loaded by the 1M next-stage grid leak), solved off the
// '57 Deluxe schematic to a 2nd-order H(s), bilinear -> IIR. One knob `tone`: 10=bright
// (flat), 0=dark with a mid scoop (the woofy tweed tone-down). Analog coeffs are
// polynomials in the pot position p (derived in /tmp/tweed_tone.py).
struct TweedTone {
    double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
    void update(float sr, float tone){
        double p = tone < 0.02f ? 0.02 : (tone > 0.98f ? 0.98 : tone);
        double pp = 47.0*p*(p-1.0);                       // shared s^2 coeff
        double B2=pp, B1=-(84000.0*p+10000.0), B0=-20000000.0;
        double A2=pp, A1=94000.0*p*p-84000.0*p-104000.0, A0=20000000.0*p-40000000.0;
        double c=2.0*(double)sr, cc=c*c;                  // bilinear x(1+z^-1)^2
        double n0=B2*cc+B1*c+B0, n1=2*B0-2*B2*cc, n2=B2*cc-B1*c+B0;
        double d0=A2*cc+A1*c+A0, d1=2*A0-2*A2*cc, d2=A2*cc-A1*c+A0;
        b0=n0/d0; b1=n1/d0; b2=n2/d0; a1=d1/d0; a2=d2/d0;
    }
    inline float process(float x){
        double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return dn((float)y);
    }
    void reset(){ x1=x2=y1=y2=0; }
};

// One real cathode-biased triode gain stage (Guitarix `tubestageF` topology, our tables).
//   TUBE = a traits struct (Tube12AX7 / Tube12AY7 / ...) selecting the transfer table.
//   set(): pick the grid-leak table (Ri 68k/250k), supply Vplus, output divider,
//   cathode-bypass corner fck, and cathode resistor Rk. Vk0 (rest cathode voltage)
//   is solved from the load line so the stage self-biases like the real circuit.
template<class TUBE>
struct TubeStageT {
    LP1 antiAlias, cathodeLP;
    HP1 dcBlock;
    int  tab = 1;
    float vplus = 250.0f, divider = 40.0f, Rk = 1500.0f, Rp = 100000.0f;
    float Vk0 = 1.2f, VkC = 0.0f, kFb = 0.02f, vk = 0.0f;

    float solveVk0() {                       // (Ftube(-v)-Vplus)*(Rk/Rp) + v = 0
        float v = 1.2f;
        for (int i = 0; i < 50; ++i) {
            float f  = (TUBE::ftube(tab, -v) - vplus) * (Rk / Rp) + v;
            float h  = 1e-3f;
            float fp = (TUBE::ftube(tab, -(v + h)) - vplus) * (Rk / Rp) + (v + h);
            float d  = (fp - f) / h;
            v -= f / (std::fabs(d) < 1e-9f ? 1e-9f : d);
            if (v < 0.0f) v = 0.05f;
        }
        return v;
    }
    void setWithPlate(float sr, int Ri_tab, float vplusV, float dividerV, float fckHz, float RkV, float RpV) {
        antiAlias.set(sr, std::fmin(sr * 0.45f, 20000.0f));
        cathodeLP.set(sr, fckHz);            // cathode bypass cap (Rk||Ck) corner
        dcBlock.set(sr, 7.0f);
        tab = Ri_tab; vplus = vplusV; divider = dividerV; Rk = RkV; Rp = RpV;
        Vk0 = solveVk0();
        VkC = Vk0 * (Rp / Rk);
        float RaRest = TUBE::ranode(tab, -Vk0); // dynamic plate resistance at the operating point
        kFb = Rk / (Rp + RaRest);             // cathode current->voltage feedback factor (real)
        vk = 0.0f;
    }
    void set(float sr, int Ri_tab, float vplusV, float dividerV, float fckHz, float RkV) {
        setWithPlate(sr, Ri_tab, vplusV, dividerV, fckHz, RkV, 100000.0f);
    }
    inline float process(float x) {           // x = grid signal in volts
        x = antiAlias.process(x);
        float u = x + vk;                     // grid + cathode auto-bias feedback (signed)
        float s = TUBE::ftube(tab, u - Vk0) + (VkC - vplus);   // recentered plate voltage
        vk = cathodeLP.process(s * kFb);      // next-sample cathode feedback (Ck-filtered)
        return dcBlock.process(s / divider);  // scale plate volts back down to signal
    }
    void reset() { antiAlias.reset(); cathodeLP.reset(); dcBlock.reset(); vk = 0.0f; }
};
using TubeStage    = TubeStageT<Tube12AX7>;   // 12AX7 (back-compat; BOX AC30)
using TubeStageAY7 = TubeStageT<Tube12AY7>;   // 12AY7 (5E3 V1)
using TubeStageAT7 = TubeStageT<Tube12AT7>;   // 12AT7 / ECC81 (Fender PI)
using TubeStage6EU7= TubeStageT<Tube6EU7>;    // 6EU7 low-noise high-mu triode
using TubeStage6SL7= TubeStageT<Tube6SL7>;    // 6SL7-GT high-mu octal triode
using TubeStage6SF5= TubeStageT<Tube6SF5>;    // 6SF5 high-mu single triode
using TubeStage12AU7=TubeStageT<Tube12AU7>;   // 12AU7 / ECC82 low-mu triode
using TubeStage7199T=TubeStageT<Tube7199T>;   // 7199 triode section
using TubeStageEF86= TubeStageT<TubeEF86>;    // EF86 small-signal pentode (DC30 channel 2)
using TubeStage5879= TubeStageT<Tube5879>;    // 5879 sharp-cutoff pentode
using TubeStage7199P=TubeStageT<Tube7199P>;   // 7199 pentode section

// Long-tail-pair 12AX7 phase inverter approximation. It keeps the two unequal
// plate loads and shared-tail compression as an actual nonlinear stage before
// the push-pull power tubes instead of feeding PowerAmpPP from an ideal splitter.
struct PhaseInverterLTP12AX7 {
    TubeStage upper, lower;
    HP1 inputCoupling;
    LP1 tailLP;
    float drive = 1.0f, out = 1.0f, imbalance = 0.07f, tailMix = 0.10f;

    void setComponents(float sr, float driveV, float outV,
                       float supplyV, float plateAOhms, float plateBOhms,
                       float cathodeOhms, float tailHz, float imbalanceV) {
        inputCoupling.set(sr, 2.5f);
        tailLP.set(sr, tailHz);
        upper.setWithPlate(sr, 1, supplyV, 52.0f, 5.0f, cathodeOhms, plateAOhms);
        lower.setWithPlate(sr, 1, supplyV, 52.0f, 5.0f, cathodeOhms, plateBOhms);
        drive = driveV;
        out = outV;
        imbalance = imbalanceV;
        tailMix = 0.10f + 0.08f * std::fmin(1.0f, std::fmax(0.0f, driveV * 0.25f));
    }

    void set(float sr, float driveV, float outV = 1.0f, float imbalanceV = 0.07f) {
        setVoxAc30(sr, driveV, outV, imbalanceV);
    }

    void setVoxAc30(float sr, float driveV, float outV = 1.0f, float imbalanceV = 0.075f) {
        setComponents(sr, driveV, outV, 300.0f, 100000.0f, 82000.0f, 1500.0f, 18.0f, imbalanceV);
    }

    void setMarshall(float sr, float driveV, float outV = 1.0f) {
        setComponents(sr, driveV, outV, 320.0f, 100000.0f, 82000.0f, 10000.0f, 12.0f, 0.065f);
    }

    void setFenderAB763(float sr, float driveV, float outV = 1.0f) {
        setComponents(sr, driveV, outV, 300.0f, 82000.0f, 100000.0f, 470.0f, 9.0f, -0.055f);
    }

    inline float process(float x) {
        const float d = inputCoupling.process(x * drive);
        const float common = tailLP.process(std::fabs(d)) * tailMix;
        const float ya = upper.process(d * (1.0f + imbalance) - common);
        const float yb = lower.process(-d * (1.0f - imbalance) - common);
        return dn((ya - yb) * (0.5f * out));
    }

    void reset() { upper.reset(); lower.reset(); inputCoupling.reset(); tailLP.reset(); }
};

// Same LTP topology for Fender amps that use a 12AT7/ECC81 phase inverter.
// The 12AT7 table is fit to Tung-Sol's 250V/10mA/gm 5500umho operating point,
// so this is higher-current and stiffer than the 12AX7 LTP above.
struct PhaseInverterLTP12AT7 {
    TubeStageAT7 upper, lower;
    HP1 inputCoupling;
    LP1 tailLP;
    float drive = 1.0f, out = 1.0f, imbalance = -0.055f, tailMix = 0.10f;

    void setComponents(float sr, float driveV, float outV,
                       float supplyV, float plateAOhms, float plateBOhms,
                       float cathodeOhms, float tailHz, float imbalanceV) {
        inputCoupling.set(sr, 2.5f);
        tailLP.set(sr, tailHz);
        upper.setWithPlate(sr, 1, supplyV, 44.0f, 4.5f, cathodeOhms, plateAOhms);
        lower.setWithPlate(sr, 1, supplyV, 44.0f, 4.5f, cathodeOhms, plateBOhms);
        drive = driveV;
        out = outV;
        imbalance = imbalanceV;
        tailMix = 0.11f + 0.07f * std::fmin(1.0f, std::fmax(0.0f, driveV * 0.22f));
    }

    void setFenderAB763(float sr, float driveV, float outV = 1.0f) {
        setComponents(sr, driveV, outV, 300.0f, 82000.0f, 100000.0f, 470.0f, 10.0f, -0.055f);
    }

    inline float process(float x) {
        const float d = inputCoupling.process(x * drive);
        const float common = tailLP.process(std::fabs(d)) * tailMix;
        const float ya = upper.process(d * (1.0f + imbalance) - common);
        const float yb = lower.process(-d * (1.0f - imbalance) - common);
        return dn((ya - yb) * (0.5f * out));
    }

    void reset() { upper.reset(); lower.reset(); inputCoupling.reset(); tailLP.reset(); }
};

// Lower-mu 12AU7/ECC82 long-tail pair for Gibson-style phase inverters. It clips
// later at the grid, but the plates swing with a rounder, lower-gain imbalance
// than the 12AX7/12AT7 LTPs above.
struct PhaseInverterLTP12AU7 {
    TubeStage12AU7 upper, lower;
    HP1 inputCoupling;
    LP1 tailLP;
    float drive = 1.0f, out = 1.0f, imbalance = 0.035f, tailMix = 0.08f;

    void setComponents(float sr, float driveV, float outV,
                       float supplyV, float plateAOhms, float plateBOhms,
                       float cathodeOhms, float tailHz, float imbalanceV) {
        inputCoupling.set(sr, 2.0f);
        tailLP.set(sr, tailHz);
        upper.setWithPlate(sr, 1, supplyV, 24.0f, 4.0f, cathodeOhms, plateAOhms);
        lower.setWithPlate(sr, 1, supplyV, 24.0f, 4.0f, cathodeOhms, plateBOhms);
        drive = driveV;
        out = outV;
        imbalance = imbalanceV;
        tailMix = 0.08f + 0.05f * std::fmin(1.0f, std::fmax(0.0f, driveV * 0.20f));
    }

    void setGibson(float sr, float driveV, float outV = 1.0f) {
        setComponents(sr, driveV, outV, 260.0f, 47000.0f, 47000.0f, 2200.0f, 18.0f, 0.030f);
    }

    inline float process(float x) {
        const float d = inputCoupling.process(x * drive);
        const float common = tailLP.process(std::fabs(d)) * tailMix;
        const float ya = upper.process(d * (1.0f + imbalance) - common);
        const float yb = lower.process(-d * (1.0f - imbalance) - common);
        return dn((ya - yb) * (0.5f * out));
    }

    void reset() { upper.reset(); lower.reset(); inputCoupling.reset(); tailLP.reset(); }
};

// Long-tail-pair 12AU7 phase inverter (Ampeg V-4B V4). Same topology as the 12AX7
// LTP but LOW-MU 12AU7 triodes → less gain, far more headroom, and the V-4B's
// stiffer/cleaner splitter that only hardens when really pushed. Feeds PowerAmpPP.
// (Distinct from the Gibson PhaseInverterLTP12AU7 above: 330V B+, 100k plates.)
struct PhaseInverterV4B {
    TubeStageT<Tube12AU7> upper, lower;
    HP1 inputCoupling;
    LP1 tailLP;
    float drive = 1.0f, out = 1.0f, imbalance = 0.05f, tailMix = 0.10f;

    void set(float sr, float driveV, float outV = 1.0f, float imbalanceV = 0.05f) {
        inputCoupling.set(sr, 2.5f);
        tailLP.set(sr, 16.0f);
        // V-4B PI ≈ 330 V B+, ~100k plate loads, shared-tail self-bias (Rk small here,
        // the shared-tail compression is the `common` term). divider 14 ≈ 12AU7 PI gain.
        upper.setWithPlate(sr, 0, 330.0f, 14.0f, 5.0f, 1500.0f, 100000.0f);
        lower.setWithPlate(sr, 0, 330.0f, 14.0f, 5.0f, 1500.0f, 100000.0f);
        drive = driveV; out = outV; imbalance = imbalanceV;
        tailMix = 0.10f + 0.08f * std::fmin(1.0f, std::fmax(0.0f, driveV * 0.25f));
    }

    inline float process(float x) {
        const float d = inputCoupling.process(x * drive);
        const float common = tailLP.process(std::fabs(d)) * tailMix;
        const float ya = upper.process(d * (1.0f + imbalance) - common);
        const float yb = lower.process(-d * (1.0f - imbalance) - common);
        return dn((ya - yb) * (0.5f * out));
    }

    void reset() { upper.reset(); lower.reset(); inputCoupling.reset(); tailLP.reset(); }
};

// Cathodyne/split-load phase inverter for 5E3/Princeton-style amps. It uses one
// 12AX7 stage and derives opposite plate/cathode outputs with unequal clipping
// and coupling; the returned value is the differential drive into PowerAmpPP.
struct PhaseInverterCathodyne12AX7 {
    TubeStage triode;
    HP1 inputCoupling;
    LP1 cathodeLP;
    float drive = 1.0f, out = 1.0f, balance = 0.0f, cathodeState = 0.0f;

    void set(float sr, float driveV, float outV = 1.0f, float supplyV = 250.0f,
             float plateOhms = 56000.0f, float cathodeOhms = 56000.0f,
             float gridLeakHz = 2.5f, float balanceV = 0.0f) {
        inputCoupling.set(sr, gridLeakHz);
        cathodeLP.set(sr, 26000.0f);
        triode.setWithPlate(sr, 1, supplyV, 62.0f, 6.0f, 1500.0f, plateOhms);
        drive = driveV;
        out = outV;
        balance = balanceV + 0.06f * ((plateOhms - cathodeOhms) / std::fmax(1.0f, plateOhms + cathodeOhms));
    }

    inline float process(float x) {
        const float d = inputCoupling.process(x * drive);
        const float plate = triode.process(d * (1.0f + balance));
        cathodeState = cathodeLP.process(0.62f * d - 0.10f * plate);
        const float cathode = std::tanh(cathodeState * (1.0f - balance));
        return dn((plate - cathode) * out);
    }

    void reset() { triode.reset(); inputCoupling.reset(); cathodeLP.reset(); cathodeState = 0.0f; }
};

// Class-A push-pull EL84 power amp (AC30 style), real circuit. Two EL84 in anti-phase
// summed by the output transformer = the DIFFERENCE of the pure pentode plate transfer
// EL84_ftube(bias ± drive·x): odd-symmetric (push-pull cancels even harmonics), soft
// class-A compression that hardens into clip as one side cuts off. Cathode-bias rise +
// supply sag are modeled as a slow envelope (physically the cap discharge / cathode cap),
// which also pushes the bias more negative under drive (class-A -> AB shift = the AC30
// compression). NO negative feedback (the Vox trait — that's what makes it raw/chimey).
// The OT is a band-pass (primary inductance low rolloff + leakage high rolloff).
template<class TUBE>
struct PowerAmpPPT {
    HP1 otHP; LP1 otLP;
    float bias = -7.5f, drive = 1.0f, out = 1.0f;
    float sag = 0.0f, atk = 0.0f, rel = 0.0f, sagDepth = 0.45f, biasShift = 2.5f;
    void set(float sr, float driveV, float biasV, float sagDepthV = 0.45f,
             float otHpHz = 70.0f, float otLpHz = 12000.0f) {
        otHP.set(sr, otHpHz);                // OT low rolloff (finite primary inductance)
        otLP.set(sr, otLpHz);                // OT high rolloff (leakage)
        atk = 1.0f - std::exp(-1.0f / (0.004f * sr));
        rel = 1.0f - std::exp(-1.0f / (0.130f * sr));
        drive = driveV; bias = biasV; sagDepth = sagDepthV;
    }
    inline float process(float x) {
        float e = std::fabs(x);
        sag += (e - sag) * (e > sag ? atk : rel);
        float s = 1.0f / (1.0f + sagDepth * sag);    // supply sag -> compression
        float b = bias - biasShift * sag * s;         // cathode bias drifts negative (class-A->AB)
        float xb = x * drive * s;
        // push-pull: two power tubes anti-phase, OT sums the differential plate voltage
        float y = TUBE::ftube(0, b + xb) - TUBE::ftube(0, b - xb);
        y = otLP.process(otHP.process(y));
        return dn(y * out * s);
    }
    void reset() { otHP.reset(); otLP.reset(); sag = 0.0f; }
};
using PowerAmpPP  = PowerAmpPPT<TubeEL84>;    // EL84 push-pull (back-compat; AC30)
using PowerAmp6BM8= PowerAmpPPT<Tube6BM8>;    // 6BM8/ECL82 push-pull (Gibson GA-8)
using PowerAmp6V6 = PowerAmpPPT<Tube6V6>;     // 6V6 push-pull (5E3)
using PowerAmp6L6 = PowerAmpPPT<Tube6L6>;     // legacy 6L6 push-pull
using PowerAmp6L6G= PowerAmpPPT<Tube6L6G>;    // 6L6G push-pull (Epiphone Zephyr/Ruby)
using PowerAmp5881= PowerAmpPPT<Tube5881>;    // 5881 push-pull (Bassman/Bluesbreaker)
using PowerAmp6L6GC=PowerAmpPPT<Tube6L6GC>;   // 6L6GC push-pull (Mesa/ENGL/Boogie)
using PowerAmpKT66= PowerAmpPPT<TubeKT66>;    // KT66 push-pull (BT45 / confirmed KT66 amps)
using PowerAmpEL34= PowerAmpPPT<TubeEL34>;    // EL34 push-pull (Plexi/Marshall)
using PowerAmp6550= PowerAmpPPT<Tube6550>;    // 6550 push-pull (Ampeg SVT-CL, 6x)
using PowerAmpKT88= PowerAmpPPT<TubeKT88>;    // KT88 push-pull (Trace V-Type V8, 8x)

} // namespace rbtube
