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
#include "koren_el84_ftube.h"   // PURE EL84 pentode plate transfer
#include "koren6v6_ftube.h"     // PURE 6V6 power pentode
#include "koren6l6_ftube.h"     // PURE 6L6/5881 power pentode
#include "koren_el34_ftube.h"   // PURE EL34 power pentode
#include <cmath>

namespace rbtube {

static constexpr float kPi = 3.14159265358979f;
static inline float dn(float v) { return std::fabs(v) < 1e-15f ? 0.0f : v; }  // flush denormals (glitch guard)

// Tube "traits": pick which pure-transfer table a stage uses, so one TubeStage/
// PowerAmpPP template serves every amp. Add an amp's tubes here (generate the table
// in gx_tube.py first). ftube/ranode are the generated lookups (namespace rbtube).
struct Tube12AX7 { static inline float ftube(int t,float v){return AX7_ftube(t,v);} static inline float ranode(int t,float v){return AX7_ranode(t,v);} };
struct Tube12AY7 { static inline float ftube(int t,float v){return AY7_ftube(t,v);} static inline float ranode(int t,float v){return AY7_ranode(t,v);} };
struct TubeEL84  { static inline float ftube(int t,float v){return EL84_ftube(t,v);} };
struct Tube6V6   { static inline float ftube(int t,float v){return V6_ftube(t,v);} };
struct Tube6L6   { static inline float ftube(int t,float v){return L6_ftube(t,v);} };
struct TubeEL34  { static inline float ftube(int t,float v){return EL34P_ftube(t,v);} };

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
    void set(float sr, int Ri_tab, float vplusV, float dividerV, float fckHz, float RkV) {
        antiAlias.set(sr, std::fmin(sr * 0.45f, 20000.0f));
        cathodeLP.set(sr, fckHz);            // cathode bypass cap (Rk||Ck) corner
        dcBlock.set(sr, 7.0f);
        tab = Ri_tab; vplus = vplusV; divider = dividerV; Rk = RkV; Rp = 100000.0f;
        Vk0 = solveVk0();
        VkC = Vk0 * (Rp / Rk);
        float RaRest = TUBE::ranode(tab, -Vk0); // dynamic plate resistance at the operating point
        kFb = Rk / (Rp + RaRest);             // cathode current->voltage feedback factor (real)
        vk = 0.0f;
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
using TubeStage    = TubeStageT<Tube12AX7>;   // 12AX7 (back-compat; BOX DC30)
using TubeStageAY7 = TubeStageT<Tube12AY7>;   // 12AY7 (5E3 V1)

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
using PowerAmp6V6 = PowerAmpPPT<Tube6V6>;     // 6V6 push-pull (5E3)
using PowerAmp6L6 = PowerAmpPPT<Tube6L6>;     // 6L6/5881 push-pull (Bassman/JTM45)
using PowerAmpEL34= PowerAmpPPT<TubeEL34>;    // EL34 push-pull (Plexi/Marshall)

} // namespace rbtube
