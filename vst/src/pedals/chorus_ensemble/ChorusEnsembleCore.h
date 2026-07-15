#ifndef CHORUS_ENSEMBLE_CORE_H
#define CHORUS_ENSEMBLE_CORE_H
//
// ChorusEnsembleCore — Boss CE-1 Chorus Ensemble, from the factory service
// notes (ET-10D) + the MN3002 datasheet. MN3002 BBD = 512 stages (delay
// 0.32-25.6 ms over its 10-800 kHz clock range; the CE-1 clocks it 60-200 kHz
// -> ~1.3..4.3 ms), THD 0.4%, S/N 70 dB, insertion loss ~10 dB, response
// fi <= 0.3*fcp. TA7136P preamp, LFO-modulated clock, Q10 output LPF (kills
// clock leak + the dark BBD warmth), noise-killer gate, STEREO "Ensemble" out.
//
//   IN -> preamp -> input LP -> BBD (delay set by clock + LFO) -> output LP
//      -> transistor Noise Killer -> jack-switched mono/stereo outputs.
//
// Mono core -> stereo out (process fills outL/outR). Runs at base rate.
//
#include <cmath>
#include "../../_shared/ChorusComponents.h"
#include "../../_shared/opamp.hpp"

namespace chorusensemble {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class OutputBiquad {
    float b0=1.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f,z1=0.f,z2=0.f;

    void set(float nb0,float nb1,float nb2,float na0,float na1,float na2) {
        b0=nb0/na0; b1=nb1/na0; b2=nb2/na0;
        a1=na1/na0; a2=na2/na0;
    }

public:
    void reset(){ z1=z2=0.f; }
    float process(float x){
        const float y=b0*x+z1;
        z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
        return y;
    }
    void lowShelf(float sr,float hz,float db){
        const float A=std::pow(10.f,db/40.f), w=2.f*kPi*hz/sr;
        const float c=std::cos(w), s=std::sin(w), al=0.70710678f*s;
        const float rA=std::sqrt(A), t=2.f*rA*al;
        set(A*((A+1.f)-(A-1.f)*c+t), 2.f*A*((A-1.f)-(A+1.f)*c),
            A*((A+1.f)-(A-1.f)*c-t), (A+1.f)+(A-1.f)*c+t,
            -2.f*((A-1.f)+(A+1.f)*c), (A+1.f)+(A-1.f)*c-t);
    }
    void highShelf(float sr,float hz,float db){
        const float A=std::pow(10.f,db/40.f), w=2.f*kPi*hz/sr;
        const float c=std::cos(w), s=std::sin(w), al=0.70710678f*s;
        const float rA=std::sqrt(A), t=2.f*rA*al;
        set(A*((A+1.f)+(A-1.f)*c+t), -2.f*A*((A-1.f)+(A+1.f)*c),
            A*((A+1.f)+(A-1.f)*c-t), (A+1.f)-(A-1.f)*c+t,
            2.f*((A-1.f)-(A+1.f)*c), (A+1.f)-(A-1.f)*c-t);
    }
    void peak(float sr,float hz,float db,float q){
        const float A=std::pow(10.f,db/40.f), w=2.f*kPi*hz/sr;
        const float c=std::cos(w), al=std::sin(w)/(2.f*q);
        set(1.f+al*A,-2.f*c,1.f-al*A,1.f+al/A,-2.f*c,1.f-al/A);
    }
};

class ChorusEnsembleCore {
    float sampleRate=48000.f;
    float level=0.6f, intensity=0.62f, depth=0.5f, rate=0.45f;
    float smLevel=0.6f, smIntensity=0.62f, smDepth=0.5f, smRate=0.45f;
    float knobSmooth=0.002f;
    bool  vibrato=false, effectOn=true, inputHigh=false;

    rbmod::HighPass  inputHP;
    rbmod::LowPass   inputLP, bbdLP1, bbdLP2;
    rbmod::DelayBuffer bbd;
    rbshared::OpAmpStage preamp;
    OutputBiquad outLowL,outLowR,outMidL,outMidR,outHighL,outHighR;
    float lfoPhase=0.f;
    float nkEnv=0.f, nkGain=0.f, nkAtk=0.f, nkRel=0.f;

    static float table3x3(const float (&values)[3][3], float y, float x) {
        const float xp=2.f*clamp01(x), yp=2.f*clamp01(y);
        const int x0=xp>=1.f?1:0, y0=yp>=1.f?1:0;
        const float tx=xp-(float)x0, ty=yp-(float)y0;
        const float a=values[y0][x0]+tx*(values[y0][x0+1]-values[y0][x0]);
        const float b=values[y0+1][x0]+tx*(values[y0+1][x0+1]-values[y0+1][x0]);
        return a+ty*(b-a);
    }

    float vibratoStereoWet() const {
        // UAD CE-1 stereo correlation fitted at Depth/Rate min, noon and max.
        // This represents the analogue output switching/matrix around Q11-Q14,
        // not a conventional wet/dry control exposed on the original panel.
        static const float kWet[3][3]={
            {0.2935f,0.4725f,0.3462f},
            {0.8088f,0.7455f,0.4383f},
            {0.9025f,0.8678f,0.7641f},
        };
        return table3x3(kWet,smDepth,smRate);
    }

    float vibratoOutputGain() const {
        // Compensates the increasing side energy so DEPTH changes pitch spread,
        // not overall loudness. Anchors are measured from the same UAD grid.
        static const float kGain[3][3]={
            {0.5537f,0.5222f,0.5458f},
            {0.4805f,0.4971f,0.5664f},
            {0.4438f,0.4576f,0.4899f},
        };
        return table3x3(kGain,smDepth,smRate);
    }

    float lfoRateHz() const {
        // Vibrato re-fit 2026-07-15 against the UAD CE-1 renders (band-energy
        // LFO extraction over the Brit DI): min/half/max measured 3.4/5.4/12.4
        // Hz — a quadratic taper, not linear (the old linear map ran 7.1 Hz at
        // half, audibly too fast mid-knob).
        if (vibrato) return 3.4f + 9.0f*smRate*smRate;   // 3.4..12.4 Hz (UAD-matched)
        return 0.44f+3.31f*smIntensity-0.90f*smIntensity*smIntensity;
    }

public:
    void setSampleRate(float sr){
        sampleRate = sr>1000.f?sr:48000.f;
        knobSmooth=1.f-std::exp(-1.f/(0.012f*sampleRate));
        bbd.resizeForMs(sampleRate, 14.0f);
        inputHP.setHz(30.f, sampleRate);
        inputLP.setHz(6200.f, sampleRate);               // pre-BBD band limit
        bbdLP1.setHz(4800.f, sampleRate);                // Q10 output LPF (dark BBD)
        bbdLP2.setHz(5200.f, sampleRate);
        preamp.setSpec(rbshared::ta7136apSpec());
        preamp.setSampleRate(sampleRate);
        outLowL.lowShelf(sampleRate,312.f,-2.78f); outLowR.lowShelf(sampleRate,312.f,-2.78f);
        outMidL.peak(sampleRate,1902.f,0.91f,0.25f); outMidR.peak(sampleRate,1902.f,0.91f,0.25f);
        outHighL.highShelf(sampleRate,6577.f,-4.10f); outHighR.highShelf(sampleRate,6577.f,-4.10f);
        nkAtk=1.f-std::exp(-1.f/(0.004f*sampleRate));
        nkRel=1.f-std::exp(-1.f/(0.160f*sampleRate));
        reset();
    }
    void setLevel(float v){ level=clamp01(v); }
    void setIntensity(float v){ intensity=clamp01(v); }
    void setDepth(float v){ depth=clamp01(v); }
    void setRate(float v){ rate=clamp01(v); }
    void setMode(float v){ vibrato=(v>=0.5f); }
    void setEffect(float v){ effectOn=(v>=0.5f); }
    void setInputSens(float v){ inputHigh=(v>=0.5f); }

    void reset(){
        inputHP.reset(); inputLP.reset(); bbdLP1.reset(); bbdLP2.reset();
        bbd.reset(); preamp.reset(); lfoPhase=0.f; nkEnv=nkGain=0.f;
        smLevel=level; smIntensity=intensity; smDepth=depth; smRate=rate;
        outLowL.reset(); outLowR.reset(); outMidL.reset(); outMidR.reset();
        outHighL.reset(); outHighR.reset();
    }

    void process(float in, float& outL, float& outR){
        smLevel+=knobSmooth*(level-smLevel);
        smIntensity+=knobSmooth*(intensity-smIntensity);
        smDepth+=knobSmooth*(depth-smDepth);
        smRate+=knobSmooth*(rate-smRate);

        // ── LFO: triangle for chorus, sine for vibrato ──
        lfoPhase += lfoRateHz()/sampleRate;
        if (lfoPhase>=1.f) lfoPhase-=std::floor(lfoPhase);
        const float tri = 1.0f - 4.0f*std::fabs(lfoPhase-0.5f);   // -1..1 triangle
        const float sn  = std::sin(rbmod::kTwoPi*lfoPhase);
        const float lfo = vibrato ? sn : tri;

        // TA7136P input stage. LEVEL is the real 50KA input pot, not a final
        // digital output trim. LOW attenuates instrument input before the high
        // closed-loop-gain preamp; HIGH exposes its characteristic overload.
        float x = inputHP.process(in);
        const float pot=std::pow(smLevel,1.7f);
        const float sensitivity=inputHigh?1.f:0.22f;
        const float preGain=20.f;
        x=preamp.process(x*pot*sensitivity*preGain,preGain);
        const float dry=x;                              // direct tap is before BBD filter
        const float bbdIn=inputLP.process(x);

        // ── BBD delay (MN3002 = 512 STAGES per datasheet, NOT 1024): delay =
        //    512/(2·fcp). At the CE-1's 60-200 kHz clock -> ~1.3..4.3 ms.
        //    Centre ~2.9 ms, LFO-swept within that window. ──
        const float clockCentre=100000.f;
        // Sweep spans re-fit 2026-07-15 against the UAD CE-1 renders:
        // - VIBRATO: DEPTH^1.5 taper up to the FULL BBD range (UAD's max sweep
        //   tracked ~3 ms p2p = the whole 1.3-4.3 ms MN3002 window). Default
        //   depth 0.5 lands ~±28 cents; the old linear map hit ±71 at default.
        // - CHORUS: FIXED wide sweep — on the real unit (and UAD) INTENSITY
        //   only moves the LFO RATE; scaling the sweep width by it left low
        //   intensities with almost no modulation (the "muy sutil" report).
        const float clockSpan=vibrato?(0.02f+0.48f*std::pow(smDepth,1.5f)):0.35f;
        float clockHz=clockCentre*(1.f+clockSpan*lfo);
        if(clockHz<60000.f) clockHz=60000.f;
        if(clockHz>200000.f) clockHz=200000.f;
        const float delayMs=1000.f*512.f/(2.f*clockHz);
        bbd.write(bbdIn);
        float wet = bbd.read(delayMs*0.001f*sampleRate);
        wet = bbdLP2.process(bbdLP1.process(wet));

        // MN3002 typical THD is about 0.4%; apply it before the discrete
        // transistor Noise Killer. The gate follows the input and only removes
        // the low-level BBD floor, rather than riding musical dynamics.
        wet += 0.004f*(wet*wet*wet-wet);
        nkEnv += (std::fabs(dry)>nkEnv?nkAtk:nkRel)*(std::fabs(dry)-nkEnv);
        const float gateTarget=clamp01((nkEnv-0.00018f)/0.0012f);
        nkGain += (gateTarget>nkGain?nkAtk:nkRel)*(gateTarget-nkGain);
        wet *= nkGain;
        wet = dn(wet);

        // ── stereo Ensemble output ──
        // UAD's stereo render is a balanced direct/effect matrix: Mid carries
        // the coloured direct path and Side carries the MN3002 path. The old
        // L=0.7*(dry+wet), R=dry routing left far too much common signal
        // (L/R correlation ~0.70 versus ~0.18 in the UAD chorus references).
        float L, R;
        if (!effectOn) {
            // NORMAL: effect off, but the preamp still colours the signal — the
            // CE-1's famous "preamp only" path (buffered, warm, mono).
            L = R = 0.60f*dry;
        } else {
            const float side=vibrato?vibratoStereoWet():0.872f;
            const float gain=vibrato?vibratoOutputGain():0.4554f;
            L=gain*(dry+side*wet);
            R=gain*(dry-side*wet);
        }
        L=outHighL.process(outMidL.process(outLowL.process(L)));
        R=outHighR.process(outMidR.process(outLowR.process(R)));
        outL=dn(L); outR=dn(R);
    }
};

} // namespace chorusensemble
#endif // CHORUS_ENSEMBLE_CORE_H
