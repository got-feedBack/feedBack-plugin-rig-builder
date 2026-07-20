#ifndef WH10_WAH_CORE_H
#define WH10_WAH_CORE_H
//
// Ibanez WH10 active wah, component-guided by ibanez_wh10_wah.pdf.
// Q1 buffers the input; IC1A/IC1B (NJM4558) form the swept active filter;
// VR1 50k is the treadle, VR2 10k is DEPTH, and S2 selects Guitar/Bass.
// The output transistor/JFET network is represented by its loading and headroom.
//
#include <cmath>
#include "../../_shared/opamp.hpp"

namespace wh10wah {

static constexpr float kPi = 3.14159265358979323846f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class WH10WahCore {
    float fs=48000.f, position=0.5f, depth=0.68f, sens=0.60f, outputGain=1.f;
    bool bassRange=false, autoSweep=true;
    float hpX=0.f, hpY=0.f, hpA=0.f;
    float sv1=0.f, sv2=0.f, outLp=0.f, outA=0.f;
    float env=0.f, envA=0.f, envR=0.f;
    rbshared::OpAmpStage ic1a, ic1b;

    void update(){
        // C1 and Q1's bias/input resistance put the input corner below guitar.
        const float hpHz=7.2f;
        const float rc=1.f/(2.f*kPi*hpHz), dt=1.f/fs;
        hpA=rc/(rc+dt);
        // Output transistor network and cable loading, not a cabinet filter.
        outA=1.f-std::exp(-2.f*kPi*14500.f/fs);
        // Envelope follower for the in-game AUTO touch-sweep (no expression
        // pedal in the game — same affordance as the US/UK/Modern wahs).
        envA=1.f-std::exp(-1.f/(0.004f*fs));
        envR=1.f-std::exp(-1.f/(0.140f*fs));
    }

public:
    void setSampleRate(float sr){
        fs=sr>1000.f?sr:48000.f;
        ic1a.setSpec(rbshared::njm4558Spec());
        ic1b.setSpec(rbshared::njm4558Spec());
        ic1a.setSampleRate(fs); ic1b.setSampleRate(fs);
        update(); reset();
    }
    void reset(){ hpX=hpY=sv1=sv2=outLp=env=0.f; ic1a.reset(); ic1b.reset(); }
    void setParams(float aut,float pos,float dep,float sn,float range){
        autoSweep=aut>=0.5f; position=clamp01(pos); depth=clamp01(dep);
        sens=clamp01(sn); bassRange=range>=0.5f;
        // Q3/Q4 and the passive output network lose more level when VR2 drives
        // the feedback branch hardest. Match that measured loading without
        // hiding it inside the resonator gain.
        const float trimDb=bassRange
            ? -depth*depth*(1.4f-.8f*position)
            : -depth*depth*(2.2f-1.6f*position);
        outputGain=std::pow(10.f,trimDb/20.f);
    }

    inline float process(float x){
        // Q1 2SC1815 emitter follower: essentially unity in-band with finite
        // 9 V headroom. The mild transfer is only reached on hot inputs.
        hpY=hpA*(hpY+x-hpX); hpX=x;
        const float buffered=0.985f*std::tanh(0.62f*hpY)/0.62f;

        // ── treadle position: manual (cocked wah) or AUTO envelope sweep ──
        const float lvl=std::fabs(buffered);
        env+=(lvl>env?envA:envR)*(lvl-env);
        float pick=env*(2.4f+sens*3.6f); if(pick>1.f)pick=1.f;
        float posEff;
        if(autoSweep){
            const float floorP=0.08f+0.34f*position;   // treadle sets the sweep floor
            const float span=0.34f+0.42f*sens;
            posEff=clamp01(floorP+span*pick);
        } else {
            posEff=clamp01(position+0.16f*sens*pick);  // manual + a little touch bloom
        }

        // VR1 is a linear 50k treadle pot, but the RC network converts its
        // travel into an approximately exponential audible frequency sweep.
        const float p=posEff*posEff*(3.f-2.f*posEff);
        const float flo=bassRange?105.f:230.f;
        // VR2 loads the frequency network as DEPTH rises. The references show
        // the guitar toe endpoint moving down modestly with that loading,
        // while zero DEPTH reaches farther into the top octave.
        const float fhi=bassRange?1100.f:(3600.f-900.f*depth);
        float fc=flo*std::pow(fhi/flo,p);
        if(fc>0.42f*fs) fc=0.42f*fs;

        // Two-integrator active filter equivalent to IC1A/IC1B. DEPTH controls
        // the real feedback/mix network; it does not trigger an envelope.
        // The FREQ network loads the feedback loop asymmetrically across the
        // treadle. Near toe-down the measured resonance is wider, especially
        // in Guitar mode; a position-invariant Q made that end thin and left
        // too much gain at heel-down.
        const float qShape=bassRange
            ? (.72f+.03f*p)
            : (.65f+1.40f*p*(1.f-p)-.05f*p);
        // Around mid treadle, the 10 k DEPTH feedback pot has a distinctly
        // convex audible taper. The reference's half-depth peak is about 4 dB
        // below a linear-Q interpolation, while both travel limits retain the
        // expected feedback response.
        const float depthExponent=bassRange?1.f:(1.f+4.f*p*(1.f-p));
        const float q=0.75f+4.75f*std::pow(depth,depthExponent)*qShape;
        const float g=std::tan(kPi*fc/fs);
        const float k=1.f/q;
        const float a1=1.f/(1.f+g*(g+k));
        const float a2=g*a1;
        const float v3=buffered-sv2;
        const float bp=a1*sv1+a2*v3;
        const float lp=sv2+a2*sv1+g*a2*v3;
        sv1=dn(2.f*bp-sv1); sv2=dn(2.f*lp-sv2);

        // NJM4558 bandwidth, slew and rail swing are applied to both active
        // sections. The resonance itself produces the WH10's overload.
        // VR2 changes the Q/feedback path around the filter. It does not add a
        // second depth-dependent voltage gain after IC1A; doing both made the
        // measured peak 7-11 dB hotter than the reference pedal at mid/max
        // DEPTH. Keep the active-stage scale fixed and let Q create the boost.
        const float heelComp=depth*(1.f-p)*(1.f-p);
        const float positionGain=bassRange
            ? (.85f+.55f*p+.25f*heelComp)
            : (.60f+.40f*p+1.00f*p*p+.35f*heelComp);
        const float stageA=ic1a.process(bp*(1.2f*positionGain),1.5f+3.5f*depth);
        const float wet=ic1b.process(stageA,1.0f+1.8f*depth);
        const float wetMix=0.32f+0.66f*depth;
        float y=(1.f-wetMix)*buffered+wetMix*wet;

        outLp+=outA*(y-outLp);
        return dn(0.92f*outputGain*outLp);
    }
};

} // namespace wh10wah
#endif
