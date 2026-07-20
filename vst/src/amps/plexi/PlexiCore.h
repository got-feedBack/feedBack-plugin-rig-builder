#ifndef PLEXI_CORE_H
#define PLEXI_CORE_H

// Marshall 1959 Super Lead signal path reconstructed from the local
// 1959-01-60-02 schematic. The Loudness controls are post-V1 attenuators;
// they do not change the guitar level presented to the two V1 triodes.
//
// Bright/Normal V1 -> A1M Loudness pots + 470k mixers -> V2A recovery
// -> V2B cathode follower -> 1959 FMV stack -> 12AX7 Marshall LTP
// -> 22n/220k power-grid coupling -> four EL34 push-pull -> OT.

#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace plexi {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x) {
        const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1; x1=x; y2=y1; y1=rbtube::dn(y); return y;
    }
    void reset() { x1=x2=y1=y2=0.0f; }
    void highShelf(float sr,float f,float dB) {
        f=std::fmin(f,sr*0.49f);
        const float A=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*f/sr,c=std::cos(w),s=std::sin(w);
        const float al=s*0.70710678f,rA=std::sqrt(A),t=2.0f*rA*al;
        const float aa0=(A+1.0f)-(A-1.0f)*c+t;
        b0=A*((A+1.0f)+(A-1.0f)*c+t)/aa0;
        b1=-2.0f*A*((A-1.0f)+(A+1.0f)*c)/aa0;
        b2=A*((A+1.0f)+(A-1.0f)*c-t)/aa0;
        a1=2.0f*((A-1.0f)-(A+1.0f)*c)/aa0;
        a2=((A+1.0f)-(A-1.0f)*c-t)/aa0;
    }
};

// V2B is a low-gain current buffer, not another common-cathode gain stage.
// This normalized model preserves its large headroom and asymmetric positive
// grid-current compression without adding a synthetic gain stage after the EQ.
struct CathodeFollower {
    rbtube::LP1 gridPole;
    float current=0.0f,attack=0.0f,release=0.0f;
    void set(float sr) {
        gridPole.set(sr,19000.0f);
        attack=1.0f-std::exp(-1.0f/(0.0022f*sr));
        release=1.0f-std::exp(-1.0f/(0.060f*sr));
    }
    inline float process(float x) {
        const float g=gridPole.process(x);
        const float positive=std::fmax(0.0f,g-0.55f);
        current+=(positive-current)*(positive>current?attack:release);
        const float y=g/(1.0f+0.38f*current);
        const float shaped=y>=0.0f ? 2.20f*std::tanh(y/2.20f)
                                   : 2.85f*std::tanh(y/2.85f);
        const float mix=std::fmin(0.22f,0.07f+0.05f*current);
        return rbtube::dn(y+mix*(shaped-y));
    }
    void reset() { gridPole.reset(); current=0.0f; }
};

static inline float interpolate(const float* table,float v) {
    const float p=10.0f*clamp01(v); int i=(int)p;
    if(i>=10) return table[10];
    return table[i]+(table[i+1]-table[i])*(p-(float)i);
}

struct PlexiCore {
    float sr=96000.0f;
    float pPres=.5f,pBass=.5f,pMid=.5f,pTreble=.5f,pL1=.5f,pL2=0.0f,pInput=.5f,pCab=0.0f;
    float brightPot=.0f,normalPot=.0f,normalDrivenMix=.0f,piRouteDrive=2.8f,outMakeup=1.0f;
    float lastPowerLoad=0.0f,lastScreenLoad=0.0f,lastPreampLoad=0.0f;

    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1Bright,v1Normal,v2Recovery;
    rbtube::Miller12AX7 millBright,millNormal,normalMixerMiller,millRecovery;
    rbtube::LP1 normalWiringPole;
    rbtube::CouplingCapGridLeak coupleBright,coupleNormal,coupleToPi,coupleToPower;
    Biquad brightVolumeCap,brightMixerBypass,presenceShelf;
    CathodeFollower follower;
    rbtube::ToneStackYeh tone;
    rbtube::PhaseInverterLTP12AX7 pi;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpEL34 power;
    rbtube::LP1 normalDrivenPole;
    rbtube::HP1 cabHP;
    rbtube::LP1 cabLP1,cabLP2;
    Biquad cabPresence;

    void setSampleRate(float s) { sr=s>1000.0f?s:96000.0f; recalc(); reset(); }
    void setPresence(float v) { pPres=clamp01(v); recalc(); }
    void setBass(float v) { pBass=clamp01(v); recalc(); }
    void setMiddle(float v) { pMid=clamp01(v); recalc(); }
    void setTreble(float v) { pTreble=clamp01(v); recalc(); }
    void setLoudness1(float v) { pL1=clamp01(v); recalc(); }
    void setLoudness2(float v) { pL2=clamp01(v); recalc(); }
    void setInput(float v) { pInput=clamp01(v); recalc(); }
    void setCabSim(float v) { pCab=clamp01(v); }
    float outputMakeup() const { return outMakeup; }

    void recalc() {
        // The inputs are DC coupled through 68k stoppers and 1M grid leaks.
        inputCoupling.set(sr,3.0f);

        // V1 High Treble: 100k plate, 2k7/.68u cathode. V1 Normal: 100k
        // plate, 820R/330u cathode. fck = 1/(2*pi*Rk*Ck).
        v1Bright.setWithPlate(sr,0,250.0f,40.0f,86.7f,2700.0f,100000.0f);
        v1Normal.setWithPlate(sr,0,250.0f,40.0f,0.59f,820.0f,100000.0f);
        // V2A recovery: 100k plate, 820R/.68u cathode.
        v2Recovery.setWithPlate(sr,1,300.0f,42.0f,285.0f,820.0f,100000.0f);

        millBright.set(sr,68000.0f,55.0f,8.0f);
        millNormal.set(sr,68000.0f,55.0f,8.0f);
        // The Normal path reaches V2A through its full 470k mixer resistor.
        // High Treble bypasses that source impedance with C6 (470p), while
        // Normal retains the V2A Miller pole and the harness/stray-capacitance
        // pole. This is the main electrical reason the two channels do not
        // have the same upper-band response at equal normalized loudness.
        normalMixerMiller.set(sr,470000.0f,52.0f,8.0f);
        normalWiringPole.set(sr,5500.0f);
        millRecovery.set(sr,470000.0f,52.0f,8.0f);

        // Real A1M Loudness controls. The small floor implements the game's
        // clean-at-low-gain contract; RS zero is also mapped above physical zero.
        brightPot=0.035f+0.965f*rbtube::PotTaper::audio(pL1,1.70f);
        normalPot=0.035f+0.965f*rbtube::PotTaper::audio(pL2,1.70f);

        // Super Lead bright path: small coupling cap plus the 4n7 Volume-I
        // bypass and 470p mixer bypass. Their effect fades as Loudness I opens.
        coupleBright.set(sr,1000000.0f,2.2e-9f,470000.0f,.30f,.045f,.24f);
        coupleNormal.set(sr,1000000.0f,22.0e-9f,470000.0f,.30f,.045f,.24f);
        brightVolumeCap.highShelf(sr,1450.0f,1.5f+7.0f*(1.0f-brightPot));
        brightMixerBypass.highShelf(sr,720.0f,2.2f);

        follower.set(sr);
        // VR3 B250k, VR5 A1M, VR4 B25k, R13 33k, C8 470p, C9/C10 22n.
        tone.setComponents(250e3,1e6,25e3,33e3,470e-12,22e-9,22e-9);
        tone.update(sr,pTreble,pMid,pBass);
        coupleToPi.set(sr,1000000.0f,22e-9f,1000000.0f,.30f,.045f,.24f);

        // V3 is the Marshall ECC83 LTP: unequal 100k/82k plates and 10k tail.
        pi.setComponents(sr,1.0f,0.92f,320.0f,
                         100000.0f,82000.0f,470.0f,12.0f,.065f);
        presenceShelf.highShelf(sr,3200.0f,8.0f*pPres);
        coupleToPower.set(sr,220000.0f,22e-9f,5600.0f,.34f,.035f,.22f);

        // Solid-state bridge, 2x50u reservoir, choke/screen node and two 10k
        // preamp droppers. This is intentionally much stiffer than a JTM45 GZ34.
        supply.set(sr,18.0f,100.0f,180.0f,100.0f,10000.0f,100.0f,
                   .10f,.070f,.045f,.11f);
        float powerHot=clamp01((std::fmax(pL1,pL2)-.55f)/.45f);
        powerHot=powerHot*powerHot*(3.0f-2.0f*powerHot);
        // With the Normal input alone, most of the dense breakup is produced
        // before the stack; driving the EL34 block as hard as High Treble
        // regenerated an upper octave that is absent from the Normal-channel
        // references. Jumpered operation retains the full power-stage drive.
        const bool normalPowerRoute=pInput>.75f;
        const bool bothPowerRoute=pInput>=.25f&&pInput<=.75f;
        const float powerRise=normalPowerRoute?1.30f:(bothPowerRoute?2.00f:1.20f);
        piRouteDrive=2.8f*(1.0f+.60f*bothPowerRoute*powerHot);
        power.set(sr,2.35f+powerRise*powerHot,
                  -38.0f,.075f,32.0f,19000.0f);
        power.out=.0110f;
        power.biasShift=1.0f;
        normalDrivenPole.set(sr,3000.0f);
        float normalDriven=clamp01((normalPot-.55f)/.45f);
        normalDrivenMix=normalPowerRoute*normalDriven*normalDriven*(3.0f-2.0f*normalDriven);

        // Optional audition-only 4x12. The app bypasses this when an external
        // cabinet/IR is connected, so reference calibration uses Cab Sim = 0.
        cabHP.set(sr,82.0f); cabLP1.set(sr,4700.0f); cabLP2.set(sr,5600.0f);
        cabPresence.highShelf(sr,2600.0f,2.5f);

        // 32-second Brit DI calibration after every nonlinear stage. All 11
        // points reach -21.6 dBFS RMS; the lower target leaves 1.1 dB of
        // headroom for the 20.5 dB-crest Bright clean endpoint.
        static const float kBrightDb[11]={
            19.799f,17.234f,12.735f, 8.612f, 5.659f, 3.708f,
             1.218f,-2.523f,-6.169f,-8.617f,-9.720f };
        static const float kNormalDb[11]={
            22.785f,22.625f,22.038f,20.750f,18.508f,14.594f,
             7.657f,-1.144f,-6.945f,-9.093f,-9.748f };
        static const float kBothDb[11]={
            21.663f,14.550f, 8.591f, 3.949f, 0.922f,-0.948f,
            -3.237f,-6.891f,-9.736f,-11.367f,-11.986f };
        const bool brightOnly=pInput<0.25f;
        const bool normalOnly=pInput>0.75f;
        const float active=brightOnly?pL1:(normalOnly?pL2:std::fmax(pL1,pL2));
        const float db=interpolate(brightOnly?kBrightDb:(normalOnly?kNormalDb:kBothDb),active);
        outMakeup=std::pow(10.0f,.05f*db);
    }

    void reset() {
        inputCoupling.reset(); v1Bright.reset(); v1Normal.reset(); v2Recovery.reset();
        millBright.reset(); millNormal.reset(); normalMixerMiller.reset(); millRecovery.reset();
        normalWiringPole.reset();
        coupleBright.reset(); coupleNormal.reset(); coupleToPi.reset(); coupleToPower.reset();
        brightVolumeCap.reset(); brightMixerBypass.reset(); presenceShelf.reset();
        follower.reset(); tone.reset(); pi.reset(); supply.reset(); power.reset(); normalDrivenPole.reset();
        cabHP.reset(); cabLP1.reset(); cabLP2.reset(); cabPresence.reset();
        lastPowerLoad=lastScreenLoad=lastPreampLoad=0.0f;
    }

    inline float process(float x) {
        const rbtube::SupplyScales bplus=supply.process(lastPowerLoad,lastScreenLoad,lastPreampLoad);
        x=inputCoupling.process(x);
        const float brightWeight=pInput<=.5f?1.0f:1.0f-(pInput-.5f)*2.0f;
        const float normalWeight=pInput>=.5f?1.0f:pInput*2.0f;

        // V1 sees a fixed guitar level. Loudness attenuation is applied only
        // after the plate coupling caps, where the pots are in the schematic.
        float b=v1Bright.process(millBright.process(x)*1.30f*bplus.preamp);
        float n=v1Normal.process(millNormal.process(x)*1.30f*bplus.preamp);
        const float bp2=brightPot*brightPot, np2=normalPot*normalPot;
        float brightFloor=clamp01((brightPot-.035f)/.25f);
        brightFloor=brightFloor*brightFloor*(3.0f-2.0f*brightFloor);
        brightFloor=.010f+.060f*brightFloor;
        const float brightDrive=.65f*(brightFloor+.45f*brightPot+7.0f*bp2*bp2*brightPot);
        // The Normal pot stays cleaner through its middle travel, then drives
        // V2A hard at the top. This moves its maximum-gain clipping ahead of
        // the cathode follower/stack instead of manufacturing it in the OT.
        const float normalP4=np2*np2;
        const float normalP7=normalP4*np2*normalPot;
        // The Normal A1M pot is substantially below High Treble near zero,
        // catches up through the middle of its travel, then drives V2A hard.
        // The quadratic term reproduces that measured channel balance without
        // adding gain after either nonlinear path.
        const float normalDrive=.65f*(.022f+.25f*np2+24.0f*normalP7);
        b=coupleBright.process(brightVolumeCap.process(b),brightDrive)*brightWeight;
        n=normalWiringPole.process(normalMixerMiller.process(
            coupleNormal.process(n,normalDrive)))*normalWeight;
        b=brightMixerBypass.process(b);

        const bool both=brightWeight>.5f&&normalWeight>.5f;
        const float loudness=std::fmax(pL1,pL2);
        const float bothScale=.35f+3.50f*loudness-3.20f*loudness*loudness;
        float y=(both?bothScale:.62f)*(b+n);
        y=v2Recovery.process(millRecovery.process(y)*bplus.preamp);
        y=follower.process(y);
        y=tone.process(y);
        y=coupleToPi.process(y,8.0f);
        lastPreampLoad=std::fabs(y)*(.18f+.55f*std::fmax(brightPot,normalPot));
        y=pi.process(y*bplus.screen*piRouteDrive);
        y=coupleToPower.process(presenceShelf.process(y),1.65f);
        lastScreenLoad=std::fabs(y)*(.25f+.55f*std::fmax(brightPot,normalPot));
        y=power.process(y*bplus.power*bplus.screen);
        const float normalLimited=normalDrivenPole.process(y);
        y+=normalDrivenMix*(normalLimited-y);
        lastPowerLoad=std::fabs(y)*(.40f+.65f*std::fmax(brightPot,normalPot));

        if(pCab>0.0001f) {
            const float cab=cabPresence.process(cabLP2.process(cabLP1.process(cabHP.process(y))))*1.45f;
            y+=pCab*(cab-y);
        }
        return rbtube::dn(y);
    }
};

} // namespace plexi
#endif // PLEXI_CORE_H
