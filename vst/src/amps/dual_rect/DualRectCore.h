#ifndef DUAL_RECT_CORE_H
#define DUAL_RECT_CORE_H
//
// DualRectCore — Mesa/Boogie 3-Channel Dual Rectifier, circuit-real on the shared
// tube_stage.hpp framework with CONTROLLED gain staging per channel. Rewritten
// from the over-gained 6-stage cascade that saturated every signal to ~100% THD.
//
//   Green: V1A -> CH1 stack/gain -> V2A -> V1B -> CH1 master.
//   Orange/Red: V1A -> channel gain -> V2A -> V2B -> V3A -> V3B -> channel
//   stack/master. Raw/Vintage/Modern switch voicing/NFB, not stage count.
//   Shared 12AX7 LTP PI + 4x 6L6GC. Runs 4x oversampled in the wrapper.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace dualrect {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v){ return v<0?0:(v>1?1:v); }

static inline float channelMakeupDb(int ch, float mode, float gain)
{
    // DI calibration at Output .6/Master .6, direct without cab. Target is
    // -17.5 dBFS RMS unless the clean transient would exceed -1 dBFS; low-Gain
    // Green therefore preserves its large natural crest instead of being
    // flattened by a limiter. All gain is post-circuit and cannot change tone.
    static const float kGreenClean[11] = {
        29.192f,20.956f,19.879f,19.522f,18.686f,17.452f,16.105f,14.533f,13.055f,11.890f,11.167f };
    static const float kGreenPushed[11] = {
        22.640f,18.529f,15.828f,14.117f,12.878f,11.920f,11.149f,10.510f, 9.969f, 9.505f, 9.099f };
    static const float kOrangeRaw[11] = {
        15.015f,13.890f,12.980f,12.274f,11.731f,11.308f,10.975f,10.712f,10.499f,10.327f,10.185f };
    static const float kOrangeVintage[11] = {
        16.593f,14.886f,13.719f,12.950f,12.430f,12.067f,11.831f,11.662f,11.540f,11.450f,11.387f };
    static const float kOrangeModern[11] = {
        12.622f, 9.845f, 8.671f, 8.109f, 7.820f, 7.678f, 7.641f, 7.677f, 7.774f, 7.926f, 8.134f };
    static const float kRedRaw[11] = {
        14.202f,13.120f,12.254f,11.604f,11.112f,10.736f,10.448f,10.222f,10.044f, 9.901f, 9.785f };
    static const float kRedVintage[11] = {
        15.769f,14.158f,13.082f,12.390f,11.932f,11.618f,11.422f,11.284f,11.187f,11.119f,11.072f };
    static const float kRedModern[11] = {
        14.955f,12.489f,11.284f,10.670f,10.342f,10.174f,10.122f,10.150f,10.245f,10.400f,10.614f };

    const float* table;
    if (ch == 0)
        table = mode < 0.5f ? kGreenClean : kGreenPushed;
    else if (ch == 1)
        table = mode < 0.25f ? kOrangeRaw : (mode < 0.75f ? kOrangeVintage : kOrangeModern);
    else
        table = mode < 0.25f ? kRedRaw : (mode < 0.75f ? kRedVintage : kRedModern);
    const float p = 10.0f*clamp01(gain);
    const int i = (int)p;
    return i >= 10 ? table[10] : table[i] + (table[i+1]-table[i])*(p-(float)i);
}

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void peak(float sr,float f,float dB,float Q){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s/(2*Q);
        float a0=1+al/A; b0=(1+al*A)/a0; b1=-2*c/a0; b2=(1-al*A)/a0; a1=-2*c/a0; a2=(1-al/A)/a0; }
    void highShelf(float sr,float f,float dB){ if(f>sr*0.49f)f=sr*0.49f; float A=std::pow(10.f,dB/40.f),w=2*kPi*f/sr,c=std::cos(w),s=std::sin(w),al=s*0.5f*1.4142135f,rA=std::sqrt(A),t=2*rA*al;
        float a0=(A+1)-(A-1)*c+t; b0=A*((A+1)+(A-1)*c+t)/a0; b1=-2*A*((A-1)+(A+1)*c)/a0; b2=A*((A+1)+(A-1)*c-t)/a0; a1=2*((A-1)-(A+1)*c)/a0; a2=((A+1)-(A-1)*c-t)/a0; }
};

// V3B in PREAMP PT2 is a direct-coupled 12AX7 cathode follower (100k cathode
// load) that drives the CH2/CH3 tone stacks. It contributes asymmetric
// grid-current compression but essentially no voltage gain.
struct RectoCathodeFollower {
    rbtube::LP1 gridPole;
    float current = 0.0f, attack = 0.0f, release = 0.0f;

    void set(float sr) {
        gridPole.set(sr, 18000.0f);
        attack = 1.0f - std::exp(-1.0f / (0.0012f * sr));
        // Recover quickly enough that a decaying note does not appear to hit a
        // gate as the follower leaves grid conduction.
        release = 1.0f - std::exp(-1.0f / (0.026f * sr));
    }

    inline float process(float x) {
        const float g = 1.12f * gridPole.process(x);
        const float positive = std::fmax(0.0f, g - 0.22f);
        current += (positive - current) * (positive > current ? attack : release);
        // Positive-grid conduction also lowers the follower's instantaneous
        // transconductance. Keeping this as a smooth conductance change retains
        // pick attack while compressing sustained high-gain frames.
        const float conductance = 1.0f / (1.0f + 1.15f * current);
        const float shifted = (g - 0.22f * current) * conductance;
        return shifted >= 0.0f
            ? 0.82f * std::tanh(shifted / 0.82f)
            : 1.18f * std::tanh(shifted / 1.18f);
    }

    void reset() { gridPole.reset(); current = 0.0f; }
};

// The shared load-line TubeStage cannot represent V2B's 39k cathode bias: its
// 12AX7 table covers normal grid voltages, while the solved cold-clipper bias is
// far outside that range. Model the same 100k/39k operating point explicitly as
// a strongly asymmetric, inverting transfer rather than silently collapsing
// the stage to zero output.
struct RectoColdClipper {
    rbtube::LP1 platePole;
    rbtube::HP1 dcBlock;

    static inline float kneeLimit(float x, float limit, float kneeRatio = 0.14f) {
        const float knee = kneeRatio * limit;
        const float ax = std::fabs(x);
        if (ax <= limit - knee)
            return x;
        if (ax >= limit + knee)
            return std::copysign(limit, x);
        const float t = (ax - (limit - knee)) / (2.0f * knee);
        const float smooth = t*t*(3.0f - 2.0f*t);
        const float y = ax + smooth*(limit - ax);
        return std::copysign(y, x);
    }

    void set(float sr) {
        platePole.set(sr, 16500.0f);
        dcBlock.set(sr, 7.0f);
    }

    inline float process(float x) {
        // 39k / (100k + 39k) establishes the unusually cold bias. Positive
        // grid excursions run out of plate swing much earlier than negative
        // excursions, which is the characteristic Rectifier clipping texture.
        constexpr float bias = 39000.0f / (100000.0f + 39000.0f);
        const float grid = 1.70f * x - 0.08f * bias;
        // Keep a genuinely linear region, then enter a short smooth knee. The
        // previous all-range tanh compressed every sample and hid the sharp
        // 39k cold-clipper edge even when global crest looked saturated.
        const float plate = grid >= 0.0f
            ? -kneeLimit(grid, 0.65f)
            : -kneeLimit(grid, 0.65f);
        return dcBlock.process(platePole.process(plate));
    }

    void reset() { platePole.reset(); dcBlock.reset(); }
};

struct DualRectCore {
    float sr = 96000.0f;
    rbtube::HP1 inCoupling;
    rbtube::TubeStage v1, v2, v3, v4;
    rbtube::CouplingCapGridLeak v2ToV2b, v2bToV3a;
    RectoColdClipper v2bColdClipper;
    RectoCathodeFollower v3bFollower;
    Biquad lowResonance, lowMidShape, modeBody, ampAir, modeShelf, fizzShape,
           ultraAir, modeUltra, presenceShelf;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AX7 pi;
    rbtube::PowerAmp6L6GC power;
    rbtube::LP1 otVoice;

    // active-channel params (selected from the 3 channels by kChannel)
    int ch=2;  // 0 Green, 1 Orange, 2 Red
    float pGain=.7f,pTreble=.6f,pMid=.4f,pBass=.55f,pPres=.5f,pMaster=.55f,pOutput=.6f,pRect=1.f,pMode=1.f;
    float g1=1.f,g2=1.f,g3=1.f,g4=1.f,g5=1.f,piDrive=6.f,postLevel=1.f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }

    // The plugin passes the full param array + resolves the active channel.
    void setChannel(int c){ ch=c; recalc(); }
    void setMode(float m){ pMode=clamp01(m); recalc(); }   // Green Clean(0)/Pushed(1); Orange/Red Raw(0)/Vintage(.5)/Modern(1)
    void setActive(float gain,float treble,float mid,float bass,float pres,float master,float output,float rect){
        pGain=clamp01(gain); pTreble=clamp01(treble); pMid=clamp01(mid); pBass=clamp01(bass);
        pPres=clamp01(pres); pMaster=clamp01(master); pOutput=clamp01(output); pRect=clamp01(rect); recalc(); }

    void reset(){ inCoupling.reset(); v1.reset(); v2.reset(); v3.reset(); v4.reset();
        v2ToV2b.reset(); v2bToV3a.reset(); v3bFollower.reset();
        v2bColdClipper.reset();
        lowResonance.reset(); lowMidShape.reset(); modeBody.reset(); ampAir.reset(); modeShelf.reset();
        fizzShape.reset(); ultraAir.reset(); modeUltra.reset();
        presenceShelf.reset(); tone.reset(); pi.reset(); power.reset(); otVoice.reset(); }

    void recalc(){
        inCoupling.set(sr, 70.0f);   // tighten lows pre-distortion to match the tight amp-only Recto reference (was 30 Hz = too full)
        // PREAMP PT1/PT2 operating points. V2B is the Rectifier cold clipper:
        // 100k plate / 39k unbypassed cathode. V3A is 220k / 1k8 bypassed;
        // V3B is the follower implemented below, not a fifth gain stage.
        v1.setWithPlate(sr, 1, 413.0f, 40.0f, 106.0f, 1500.0f, 220000.0f); // V1A
        v2.setWithPlate(sr, 1, 394.0f, 40.0f, 88.0f, 1800.0f, 100000.0f);  // V2A
        v3.setWithPlate(sr, 1, 394.0f, 40.0f, 106.0f, 1500.0f, 100000.0f); // V1B, CH1 only
        v4.setWithPlate(sr, 1, 435.0f, 40.0f, 88.0f, 1800.0f, 220000.0f);  // V3A
        v2ToV2b.set(sr, 475000.0f, 20.0e-9f, 1000.0f, 0.10f, 0.25f, 0.75f);
        v2bToV3a.set(sr, 220000.0f, 20.0e-9f, 220000.0f, 0.10f, 0.20f, 0.65f);
        v2bColdClipper.set(sr);
        v3bFollower.set(sr);

        // The real Gain pots sit before most of the cascade, so the five-stage
        // channels are already driven at noon and add density rather than
        // exploding into a square wave over the last quarter.
        if (ch == 0) {
            const float g = std::sqrt(pGain);
            g1 = 0.70f + 1.50f*g;
            g2 = 0.70f + 1.00f*g;
            g3 = 0.75f + 0.80f*g;
            g4 = g5 = 1.0f;
            g1 *= 1.0f + 0.55f*pMode;             // Clean -> Pushed relay
            const float hot = clamp01((pGain - 0.5f) * 2.0f);
            const float hotSmooth = hot*hot*(3.0f - 2.0f*hot);
            const float greenScale = 0.80f + 0.12f*pMode + 0.12f*(1.0f-pMode)*hotSmooth;
            g1 *= greenScale; g2 *= greenScale; g3 *= greenScale;
        } else if (ch == 1) {
            // CH2 gain is a 250k audio pot ahead of V2A. At noon the real
            // four-stage path is already saturated; the upper half adds
            // compression and sustain rather than backing the drive down.
            const float g = rbtube::PotTaper::audio(pGain, 1.1f);
            g1 = 2.35f;                         // fixed V1A voltage gain
            g3 = 1.95f;                         // V2B grid drive
            g4 = 0.65f;                         // V3A recovery after the cold clipper
            g5 = 1.12f;                         // V3B / stack driver
            // All three CH2 voices still use the complete V2/V3 cascade. Raw
            // is the least compressed, Vintage is hotter, and Modern reaches
            // the saturated plateau earliest. Fast Thrash comparisons showed
            // that the existing compression amount was already sufficient;
            // the perceived lack of gain came from missing upper harmonics.
            if (pMode < 0.25f)
                g2 = 0.75f + 3.00f*g;
            else if (pMode < 0.75f)
                g2 = 0.75f + 5.00f*g;
            else
                g2 = 0.75f + 8.80f*g + 0.35f*(4.0f*pGain*(1.0f-pGain));
            if (pMode >= 0.75f) {
                g3 = 2.25f;
                g4 = 0.85f;
            }
        } else {
            const float g = rbtube::PotTaper::audio(pGain, 1.1f);
            g1 = 2.45f;
            g3 = 2.02f;
            g4 = 0.68f;
            g5 = 1.15f;
            if (pMode < 0.25f)
                g2 = 0.78f + 3.20f*g;
            else if (pMode < 0.75f)
                g2 = 0.78f + 5.20f*g;
            else
                g2 = 0.78f + 9.00f*g;
            if (pMode >= 0.75f) {
                g3 = 2.35f;
                g4 = 0.88f;
            }
        }

        // Per-channel tone stack (circuit-real, Yeh DOUBLE-precision — float NaNs at 192k).
        // Green (CH1): 250k/250k/25k, slope 150k, 250pF/.1/.047.
        // Orange/Red, PREAMP PT2: Treble 250k, Bass 1M, Mid 25k, slope 47k,
        // 500pF/.02/.02. The previous 25k Bass value was the Mid pot copied into
        // the wrong component slot and made the amp-only output much too dark.
        if (ch == 0)  tone.setComponents(250e3, 250e3, 25e3, 150e3, 250e-12, 100e-9, 47e-9);
        else          tone.setComponents(250e3,   1e6, 25e3,  47e3, 500e-12,  20e-9, 20e-9);
        tone.update(sr, pTreble, pMid, pBass);
        // PREAMP PT2: the three-way relays alter Presence/NFB and the stack's
        // HF path. Match those measured transfers instead of multiplying every
        // tube stage as if Modern were simply "more gain".
        const float voicingHot = clamp01((pGain - 0.5f) * 2.0f);
        float bodyDb = 0.0f, shelfDb = 0.0f, ultraModeDb = 0.0f;
        if (ch >= 1 && pMode < 0.25f) {
            bodyDb = -9.0f;
            shelfDb = 2.0f;
        } else if (ch >= 1 && pMode < 0.75f) {
            bodyDb = -6.0f - voicingHot;
            shelfDb = 3.5f - 1.5f*pGain;
            ultraModeDb = 2.0f * (1.0f - pGain);
        } else if (ch >= 1 && pMode >= 0.75f) {
            bodyDb = -4.75f - 0.50f*voicingHot;
            // Modern must retain useful 2-5 kHz presence after a V30-style
            // cabinet. A gain-dependent shelf that reached 0 dB when cranked
            // left the amp direct close but buried it once the cab removed the
            // much higher 7-8 kHz compensation.
            shelfDb = 1.5f + 3.0f * (1.0f - pGain);
            ultraModeDb = 5.0f * (1.0f - pGain);
        }
        // Raw and Vintage keep more negative feedback than Modern, but the
        // speaker/OT resonance still supplies the low-frequency punch heard on
        // palm mutes. The previous zero-dB placeholder removed that resonance
        // entirely, so those voices retained their correct top end but lost
        // weight in a full mix. Modern is already matched and stays untouched.
        const float lowResonanceDb = ch == 0 ? 0.0f
                                   : (pMode < 0.25f ? 2.0f
                                      : (pMode < 0.75f ? 1.5f : 0.0f));
        lowResonance.peak(sr, 135.0f, lowResonanceDb, 0.72f);
        const float lowMidDb = pMode < 0.25f ? -3.5f : (pMode < 0.75f ? -4.5f : -11.0f);
        lowMidShape.peak(sr, 560.0f, ch == 0 ? 0.0f : lowMidDb, 0.82f);
        modeBody.peak(sr, 950.0f, bodyDb, 0.58f);
        // Preserve direct amp bandwidth without replacing missing saturation
        // with a large post-circuit fizz shelf. The former fixed +17 dB shelf
        // dominated the real V2/V3 harmonics and sounded cleaner than it was.
        const float ampAirDb = ch == 0 ? 6.0f : (pMode < 0.25f ? 4.0f : (pMode < 0.75f ? 5.0f : 7.0f));
        ampAir.highShelf(sr, 3200.0f, ampAirDb);
        modeShelf.highShelf(sr, (ch >= 1 && pMode >= 0.75f) ? 1800.0f : 2200.0f, shelfDb);
        const float fizzDb = pMode < 0.25f ? -5.0f : (pMode < 0.75f ? -7.0f : -6.0f);
        fizzShape.peak(sr, 4800.0f, ch == 0 ? 0.0f : fizzDb, 0.72f);
        const float ultraAirDb = pMode >= 0.75f ? (3.0f + 7.0f*pGain) : (2.0f + 3.0f*pGain);
        ultraAir.highShelf(sr, 7500.0f, ch == 0 ? 0.0f : ultraAirDb);
        // Restore the high-order content measured in the direct Fast Thrash
        // renders without adding more broadband compression. Raw and Vintage
        // need the largest correction around noon; Modern is already close.
        if (ch >= 1 && pMode < 0.25f)
            ultraModeDb += 13.0f - 6.0f*pGain;
        else if (ch >= 1 && pMode < 0.75f)
            ultraModeDb += 20.0f - 16.0f*pGain;
        else if (ch >= 1)
            ultraModeDb += (ch == 1 ? 6.0f : 4.0f) - 6.0f*pGain;
        modeUltra.highShelf(sr, 7000.0f, ultraModeDb);
        presenceShelf.highShelf(sr, 3000.0f, (pPres-0.5f)*10.0f);

        // Vintage retains the normal feedback loop; Modern opens it and raises
        // PI/power sensitivity. That extra density belongs in the power section,
        // not in a post-output gain table.
        const float gainShoulder = 4.0f*pGain*(1.0f-pGain);
        const float powerModeDrive = (ch < 1 || pMode < 0.25f) ? 1.0f
                                   : (pMode < 0.75f ? 0.90f
                                      : (ch == 1 ? 1.15f + 0.05f*gainShoulder
                                                 : 0.95f + 0.04f*gainShoulder));
        piDrive = 5.4f * powerModeDrive;
        // POWER AMP: V5A/V5B are a 12AX7 LTP, 435 V node, 91k/82k plates.
        // The previous 12AT7/Fender preset was the wrong tube and operating
        // point for this schematic.
        pi.setComponents(sr, 1.0f, 1.0f, 435.0f, 91000.0f, 82000.0f,
                         470.0f, 10.0f, 0.055f);
        // Rectifier: Spongy(0)=more sag/lower, Bold(1)=tight/higher.
        const float vol = rbtube::PotTaper::audio(pMaster, 1.15f);
        power.set(sr, (0.5f + 2.2f*vol) * powerModeDrive, -36.0f,
                  0.05f + 0.04f*(1.0f-pRect), 30.0f, 11000.0f);
        power.out = 0.012f;
        otVoice.set(sr, 19000.0f);   // amp-direct OT bandwidth; no speaker/cab roll-off

        postLevel = (0.4f + pOutput) * std::pow(10.0f, 0.05f*channelMakeupDb(ch, pMode, pGain));
    }

    // Loudness calibration belongs after every nonlinear stage in the wrapper.
    // Keeping it here previously drove rbAmpLvl harder at low-gain settings and
    // changed distortion while pretending to be a level-only correction.
    float outputLevel() const { return postLevel; }
    float outputKnee(float y) const {
        return (ch >= 1 && pMode >= 0.75f)
            ? RectoColdClipper::kneeLimit(y, ch == 1 ? 0.116f : 0.098f, 0.05f)
            : y;
    }

    inline float process(float x){
        x = inCoupling.process(x);
        float y = v1.process(x * g1);
        if (ch == 0) {
            y = tone.process(y);                 // CH1 stack is before V2A/V1B
            y = v2.process(y * g2);
            y = v3.process(y * g3);
        } else {
            y = v2.process(y * g2);              // V2A
            y = v2bColdClipper.process(v2ToV2b.process(y, g3)); // V2B, 20n/475k cold clipper
            y = v4.process(v2bToV3a.process(y, g4)); // V3A, 20n/220k
            y = v3bFollower.process(y * g5);      // V3B cathode follower
            y = tone.process(y);                 // CH2/CH3 stack after cascade
        }
        y = lowResonance.process(y);
        y = lowMidShape.process(y);
        y = modeBody.process(y);
        y = ampAir.process(y);
        y = modeShelf.process(y);
        y = fizzShape.process(y);
        y = ultraAir.process(y);
        y = modeUltra.process(y);
        y = presenceShelf.process(y);
        y = pi.process(y * piDrive);
        y = power.process(y);
        y = otVoice.process(y);
        return y;
    }
};

} // namespace dualrect
#endif // DUAL_RECT_CORE_H
