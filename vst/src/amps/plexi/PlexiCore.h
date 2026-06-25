#ifndef PLEXI_CORE_H
#define PLEXI_CORE_H

/*
 * Marsten Plexi - Marshall 1959SLP/Super Lead component model.
 *
 * Rebuilt as one stable non-master-volume signal path:
 * inputs -> V1 high-treble/normal 12AX7 stages -> real Loudness pots and
 * 470k mixer -> V2A 12AX7 gain -> V2B cathode follower approximation ->
 * Marshall FMV stack -> V3 12AX7 long-tail pair -> 4x EL34 fixed-bias output.
 *
 * Local schematic references:
 *   amps/Marshall Plexi/1959-01-60-02.pdf
 *   amps/Marshall Plexi/1959sprm.gif
 *   amps/Marshall Plexi/1959spwm.gif
 *
 * Guitarix was used only as a topology/stability reference: Marshall tonestack
 * values, EL34 power-amp placement, and the "keep nonlinear stages feed-forward"
 * lesson. No Guitarix GPL DSP code is copied here.
 *
 * Stability note: this core deliberately avoids the heavy sag/Miller/load
 * feedback pattern (`MultiNodeBPlus` + `Miller12AX7` + `lastXxxLoad`). In-app it
 * can produce a digital "8-bit" roughness on hot material. A Plexi has a stiff
 * silicon supply anyway, so B+ is treated as fixed and the HF/loading losses are
 * represented by stable feed-forward filters.
 */

#include "../../_shared/tube_stage.hpp"
#include "PlexiParams.h"
#include <cmath>

namespace plexi {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

namespace plexipot {
// Front panel and PCB values visible in 59x-60-02.
static constexpr float kAudioExp      = 1.35f;       // A1M loudness pots, calibrated to RS offset range
static constexpr float kLoudnessOhms  = 1000000.0f;
static constexpr float kMixerOhms     = 470000.0f;   // R7/R8 channel mixer
static constexpr float kTrebleOhms    = 220000.0f;   // VR3 B220K
static constexpr float kBassOhms      = 1000000.0f;  // VR5 A1M
static constexpr float kMiddleOhms    = 22000.0f;    // VR4 B22K
static constexpr float kSlopeOhms     = 33000.0f;    // R11
static constexpr float kTrebleCapF    = 220.0e-12f;  // C21
static constexpr float kMidCapF       = 22.0e-9f;    // C20
static constexpr float kBassCapF      = 22.0e-9f;    // C19
static constexpr float kLeadBrightF   = 4700.0e-12f; // C17 dominant bright network

static inline float audioA(float v) { return std::pow(clamp01(v), kAudioExp); }
static inline float linearB(float v) { return clamp01(v); }

static inline float playableA1M(float v)
{
    // Rocksmith clean tones map RS 0 to a low real setting, not to "amp volume off".
    // Keep the A1M sweep but make the floor audible so the DSP never gates cleans.
    static constexpr float kFloor = 0.160f;
    return kFloor + (1.0f - kFloor) * audioA(v);
}

static inline float wiperSourceOhms(float potOhms, float electricalPos)
{
    const float e = 0.001f + 0.998f * clamp01(electricalPos);
    const float upper = potOhms * (1.0f - e);
    const float lower = potOhms * e;
    return (upper * lower) / std::fmax(1.0f, upper + lower);
}
} // namespace plexipot

struct Biquad {
    float b0=1.0f,b1=0.0f,b2=0.0f,a1=0.0f,a2=0.0f,x1=0.0f,x2=0.0f,y1=0.0f,y2=0.0f;
    inline float process(float x) {
        const float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=rbtube::dn(y);
        return y;
    }
    void reset(){ x1=x2=y1=y2=0.0f; }
    void norm(float a0){ b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0; }
    void peaking(float sr,float f,float Q,float dB) {
        f = std::fmin(f, sr * 0.45f);
        const float A=std::pow(10.0f,dB/40.0f), w=2.0f*kPi*f/sr, c=std::cos(w), al=std::sin(w)/(2.0f*Q);
        b0=1.0f+al*A; b1=-2.0f*c; b2=1.0f-al*A;
        const float a0=1.0f+al/A; a1=-2.0f*c; a2=1.0f-al/A; norm(a0);
    }
    void lowShelf(float sr,float f,float dB) {
        f = std::fmin(f, sr * 0.45f);
        const float A=std::pow(10.0f,dB/40.0f), w=2.0f*kPi*f/sr, c=std::cos(w), s=std::sin(w);
        const float al=s*0.5f*std::sqrt((A+1.0f/A)+2.0f), rA=std::sqrt(A);
        b0=A*((A+1.0f)-(A-1.0f)*c+2.0f*rA*al);
        b1=2.0f*A*((A-1.0f)-(A+1.0f)*c);
        b2=A*((A+1.0f)-(A-1.0f)*c-2.0f*rA*al);
        const float a0=(A+1.0f)+(A-1.0f)*c+2.0f*rA*al;
        a1=-2.0f*((A-1.0f)+(A+1.0f)*c);
        a2=(A+1.0f)+(A-1.0f)*c-2.0f*rA*al; norm(a0);
    }
    void highShelf(float sr,float f,float dB) {
        f = std::fmin(f, sr * 0.45f);
        const float A=std::pow(10.0f,dB/40.0f), w=2.0f*kPi*f/sr, c=std::cos(w), s=std::sin(w);
        const float al=s*0.5f*std::sqrt((A+1.0f/A)+2.0f), rA=std::sqrt(A);
        b0=A*((A+1.0f)+(A-1.0f)*c+2.0f*rA*al);
        b1=-2.0f*A*((A-1.0f)+(A+1.0f)*c);
        b2=A*((A+1.0f)+(A-1.0f)*c-2.0f*rA*al);
        const float a0=(A+1.0f)-(A-1.0f)*c+2.0f*rA*al;
        a1=2.0f*((A-1.0f)-(A+1.0f)*c);
        a2=(A+1.0f)-(A-1.0f)*c-2.0f*rA*al; norm(a0);
    }
    void lowpass(float sr,float f,float Q) {
        f = std::fmin(f, sr * 0.45f);
        const float w=2.0f*kPi*f/sr, c=std::cos(w), al=std::sin(w)/(2.0f*Q);
        b0=(1.0f-c)*0.5f; b1=1.0f-c; b2=(1.0f-c)*0.5f;
        const float a0=1.0f+al; a1=-2.0f*c; a2=1.0f-al; norm(a0);
    }
    void highpass(float sr,float f,float Q) {
        f = std::fmin(f, sr * 0.45f);
        const float w=2.0f*kPi*f/sr, c=std::cos(w), al=std::sin(w)/(2.0f*Q);
        b0=(1.0f+c)*0.5f; b1=-(1.0f+c); b2=(1.0f+c)*0.5f;
        const float a0=1.0f+al; a1=-2.0f*c; a2=1.0f-al; norm(a0);
    }
};

struct CathodeFollower12AX7 {
    rbtube::LP1 outLp;
    rbtube::HP1 dc;
    float drive = 1.0f, out = 1.0f, gridCompression = 0.16f;

    void set(float sr, float driveV, float outV, float hot)
    {
        outLp.set(sr, 26000.0f);
        dc.set(sr, 5.0f);
        drive = driveV;
        out = outV;
        gridCompression = 0.10f + 0.10f * hot;
    }

    inline float process(float x)
    {
        const float g = x * drive;
        const float posConduct = std::fmax(0.0f, g - 0.72f);
        float y = g / (1.0f + gridCompression * std::fabs(g));
        y -= 0.10f * std::tanh(posConduct * 0.85f); // V2B grid-current asymmetry
        return dc.process(outLp.process(y) * out);
    }

    void reset(){ outLp.reset(); dc.reset(); }
};

struct PlexiOutputTransformer {
    rbtube::HP1 hp;
    rbtube::LP1 leakLp, fluxLp;
    float coreDrive = 1.0f, coreMix = 0.08f;

    void set(float sr, float hot)
    {
        hp.set(sr, 34.0f);
        leakLp.set(sr, 19000.0f);
        fluxLp.set(sr, 18.0f);
        coreDrive = 1.0f + 0.16f * hot;
        coreMix = 0.07f + 0.05f * hot;
    }

    inline float process(float x)
    {
        float y = hp.process(x);
        const float flux = fluxLp.process(y);
        const float core = std::tanh((y - 0.12f * flux) * coreDrive);
        y = (1.0f - coreMix) * y + coreMix * core;
        return leakLp.process(y);
    }

    void reset(){ hp.reset(); leakLp.reset(); fluxLp.reset(); }
};

struct PlexiFallbackSpeaker {
    rbtube::HP1 hp;
    Biquad cone, lowMid, bite, fizz, coneLp;

    void set(float sr, float treble, float presence, float hot)
    {
        hp.set(sr, 76.0f);
        cone.peaking(sr, 92.0f, 0.90f, 1.4f + 0.4f * hot);
        lowMid.peaking(sr, 430.0f, 0.82f, -1.6f);
        bite.peaking(sr, 2850.0f, 0.76f, 3.4f + 1.8f * treble + 1.4f * presence - 0.5f * hot);
        fizz.highShelf(sr, 6000.0f, -2.8f + 1.8f * treble + 1.2f * presence - 1.2f * hot);
        coneLp.lowpass(sr, 11200.0f + 2600.0f * treble + 1200.0f * presence - 1000.0f * hot, 0.68f);
    }

    inline float process(float x)
    {
        float y = hp.process(x);
        y = cone.process(y);
        y = lowMid.process(y);
        y = bite.process(y);
        y = fizz.process(y);
        return coneLp.process(y);
    }

    void reset(){ hp.reset(); cone.reset(); lowMid.reset(); bite.reset(); fizz.reset(); coneLp.reset(); }
};

struct PlexiCore {
    float sr = 48000.0f;

    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1Lead, v1Normal, v2Gain;
    CathodeFollower12AX7 v2Follower;
    rbtube::CouplingCapGridLeak cV1ToV2, cStackToPi, cPiToPower;
    rbtube::ToneStackYeh tonestack;
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::PowerAmpEL34 power;
    Biquad leadBrightHp, normalRolloff, presenceShelf;
    rbtube::LP1 inputLoadLp, mixLoadLp, piLoadLp;
    PlexiOutputTransformer outputTransformer;
    PlexiFallbackSpeaker fallbackSpeaker;

    float pPresence = kPlexiDef[kPresence];
    float pBass     = kPlexiDef[kBass];
    float pMiddle   = kPlexiDef[kMiddle];
    float pTreble   = kPlexiDef[kTreble];
    float pLoud1    = kPlexiDef[kLoudness1];
    float pLoud2    = kPlexiDef[kLoudness2];
    float pInput    = kPlexiDef[kInput];
    float pCabSim   = kPlexiDef[kCabSim];

    float leadOn = 1.0f, normalOn = 1.0f;
    float leadVol = 0.25f, normalVol = 0.10f, hot = 0.5f;
    float inScale = 1.0f, v2Drive = 1.0f, followerDrive = 1.0f;
    float stackToPi = 1.0f, powerDrive = 1.0f, outLevel = 1.0f;

    void setSampleRate(float s)
    {
        sr = s > 1000.0f ? s : 48000.0f;
        recalc();
        reset();
    }

    void reset()
    {
        inputCoupling.reset();
        v1Lead.reset(); v1Normal.reset(); v2Gain.reset(); v2Follower.reset();
        cV1ToV2.reset(); cStackToPi.reset(); cPiToPower.reset();
        tonestack.reset(); phaseInverter.reset(); power.reset();
        leadBrightHp.reset(); normalRolloff.reset(); presenceShelf.reset();
        inputLoadLp.reset(); mixLoadLp.reset(); piLoadLp.reset();
        outputTransformer.reset(); fallbackSpeaker.reset();
    }

    void recalc()
    {
        leadOn = (pInput < 0.75f) ? 1.0f : 0.0f;
        normalOn = (pInput >= 0.25f) ? 1.0f : 0.0f;

        leadVol = plexipot::playableA1M(pLoud1);
        normalVol = plexipot::playableA1M(pLoud2);
        hot = clamp01(0.72f * leadOn * std::sqrt(leadVol) +
                      0.28f * normalOn * std::sqrt(normalVol));

        const float treble = plexipot::linearB(pTreble);
        const float bass = plexipot::linearB(pBass);
        const float middle = plexipot::linearB(pMiddle);
        const float presence = plexipot::linearB(pPresence);

        inputCoupling.set(sr, 12.0f);

        // V1: two 12AX7 input stages. Lead channel uses 820R/680n, normal uses
        // 820R/330u. Guitar hits V1 at a fixed level; Loudness happens after V1.
        v1Lead.setWithPlate(sr, 1, 250.0f, 43.0f, 285.0f, 820.0f, 100000.0f);
        v1Normal.setWithPlate(sr, 1, 250.0f, 43.0f, 0.6f, 820.0f, 100000.0f);
        v2Gain.setWithPlate(sr, 1, 285.0f, 43.0f, 285.0f, 820.0f, 100000.0f);

        inScale = 1.85f;

        const float leadSource = 47000.0f + plexipot::wiperSourceOhms(plexipot::kLoudnessOhms, leadVol);
        inputLoadLp.set(sr, 19000.0f - 3000.0f * hot);
        mixLoadLp.set(sr, 15500.0f - 2600.0f * hot);
        piLoadLp.set(sr, 17000.0f - 2600.0f * hot);

        // 22n coupling caps are real; grid-current charging is intentionally mild.
        // This gives blocking when the amp is abused without turning clean notes into
        // the broken cutoff the previous Plexi model produced.
        cV1ToV2.set(sr, 1000000.0f, 22.0e-9f, 470000.0f, 0.62f, 0.095f, 0.42f);
        cStackToPi.set(sr, 1000000.0f, 22.0e-9f, 68000.0f, 0.72f, 0.080f, 0.34f);
        cPiToPower.set(sr, 220000.0f, 22.0e-9f, 5600.0f, 0.95f, 0.045f, 0.24f);

        const float brightHz = 1.0f / (2.0f * kPi * std::fmax(33000.0f, leadSource) * plexipot::kLeadBrightF);
        leadBrightHp.highpass(sr, std::fmax(520.0f, std::fmin(5200.0f, brightHz)), 0.70f);
        normalRolloff.lowpass(sr, 4300.0f + 1500.0f * treble, 0.72f);

        tonestack.setComponents(plexipot::kTrebleOhms, plexipot::kBassOhms, plexipot::kMiddleOhms,
                                plexipot::kSlopeOhms, plexipot::kTrebleCapF,
                                plexipot::kMidCapF, plexipot::kBassCapF);
        tonestack.update(sr, treble, middle, bass);

        // Solid-state bridge rectifier + 50u+50u filters: stiff supply. Do not
        // feed load back into B+ per sample; that was the source of the "8-bit"
        // instability pattern documented by the bass-amp fix.

        // The Loudness pots determine how hard the fixed-gain V2/PI/power chain is
        // driven. These gains are calibration from volts in this model, not extra
        // front-panel controls.
        v2Drive = 5.4f + 6.9f * hot;
        followerDrive = 0.90f + 0.35f * hot;
        stackToPi = 2.45f + 1.85f * hot;
        phaseInverter.setMarshall(sr, 1.10f + 2.20f * hot, 0.90f);
        powerDrive = 9.0f + 9.5f * hot;
        // The generated EL34 Koren table has too little small-signal slope at the
        // literal -36V bias point once fed by this scaled LTP. Use an equivalent
        // calibrated grid-center so the 4xEL34 section idles/conducts like a real
        // AB1 Plexi instead of muting low-Loudness notes.
        power.set(sr, powerDrive, -28.0f, 0.10f + 0.05f * hot, 38.0f, 15500.0f);
        power.out = 0.0080f;

        v2Follower.set(sr, followerDrive, 0.92f, hot);
        presenceShelf.highShelf(sr, 2350.0f + 950.0f * presence,
                                -3.8f + 8.3f * presence + 0.8f * treble);
        outputTransformer.set(sr, hot);
        fallbackSpeaker.set(sr, treble, presence, hot);

        // Non-master amps get quieter at low Loudness. This is host/output makeup,
        // not part of the circuit, and is deliberately broad so it cannot become a
        // gate or a clipping spike.
        const float knob = clamp01(0.72f * pLoud1 + 0.28f * pLoud2);
        const float makeupDb = 7.0f - 4.5f * std::sqrt(knob);
        outLevel = 0.82f * std::pow(10.0f, 0.05f * makeupDb);
    }

    void setParam(int idx, float v)
    {
        v = clamp01(v);
        switch (idx) {
            case kPresence:  pPresence = v; break;
            case kBass:      pBass = v; break;
            case kMiddle:    pMiddle = v; break;
            case kTreble:    pTreble = v; break;
            case kLoudness1: pLoud1 = v; break;
            case kLoudness2: pLoud2 = v; break;
            case kInput:     pInput = v; break;
            case kCabSim:    pCabSim = v; break;
            default: break;
        }
        recalc();
    }

    void initDefaults()
    {
        for (int i = 0; i < kParamCount; ++i)
            setParam(i, kPlexiDef[i]);
    }

    inline float process(float x)
    {
        x = inputCoupling.process(x * inScale);
        x = inputLoadLp.process(x);
        const float lead = v1Lead.process(x);
        const float normal = normalRolloff.process(v1Normal.process(x));

        const float brightBypassAmount = (1.0f - leadVol) * std::sqrt(leadVol) *
                                         (0.22f + 0.23f * pTreble);
        const float leadOut = leadVol * lead + brightBypassAmount * leadBrightHp.process(lead);
        const float normalOut = normalVol * 0.88f * normal;
        float y = leadOn * leadOut + normalOn * normalOut;

        y = cV1ToV2.process(mixLoadLp.process(y), v2Drive);
        y = v2Gain.process(y);
        y = v2Follower.process(y);
        y = tonestack.process(y);

        y = cStackToPi.process(piLoadLp.process(y), stackToPi);
        y = phaseInverter.process(y);

        y = cPiToPower.process(y, 1.0f);
        y = power.process(y);

        y = presenceShelf.process(y);
        y = outputTransformer.process(y);

        const float cab = fallbackSpeaker.process(y);
        y += clamp01(pCabSim) * (cab - y);

        return y * outLevel;
    }
};

} // namespace plexi

#endif // PLEXI_CORE_H
