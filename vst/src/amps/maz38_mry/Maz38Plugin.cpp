/*
 * MR. Y MAZ 38 - circuit-real DSP of the Dr. Z Maz 18 Jr (Legacy "MAZ 38 SENIOR
 * NR" front panel). Parody brand "Mr. Y"; the in-app face must NEVER read
 * "Dr. Z" or carry the real maker's mark.
 *
 * REBUILT schematic-first (component-by-component) from the user schematic
 * "Dr.Z Maz 18 Jr. Reverb  SN: J.8159", cross-checked against Guitarix
 * (src/faust/tonestack.dsp, tools/ampsim/DK input12ax7.sch — VALUES ONLY, no
 * GPL code). NOT the old black-box tanh build. Same circuit-real engine and
 * gain-staging as BOX AC30 (en30/, AC30 Top Boost) — both are cathode-biased
 * EL84 push-pull with no NFB — adapted to the Maz 18: a 2-stage 12AX7 front
 * end, a simple TMB tone stack + Cut, and a smaller 2x EL84 power amp fed from
 * a GZ34 tube rectifier (sag present).
 *
 * SIGNAL CHAIN (from the schematic):
 *   in -> Hi jack 68k grid-stopper + 1M grid-leak (input coupling HP)
 *      -> V1a 12AX7  : 68k plate, 680R cathode + 680n bypass            (TubeStage)
 *      -> 1n couple -> VOLUME 1MA (the amp's only preamp/drive pot)
 *      -> V1b 12AX7  : 100k plate, 820R cathode + 25u bypass, 220k grid (TubeStage)
 *      -> 1n couple -> TONE STACK (TMB): R1 250k Treble / R2 250k Bass /
 *                      R3 10k Mid / R4 56k slope; C1 250pF / C2 100nF / C3 47nF
 *                                                                       (ToneStackYeh, double)
 *      -> 12AX7 phase inverter (long-tail pair) with CUT = 250kL + 4.7n
 *         shunting the differential outputs (higher = darker)           (cut LP, post-power)
 *      -> 2x EL84 push-pull, cathode-biased 150R/~12.7V, NO NFB         (PowerAmpPP, EL84)
 *      -> OT (CinTran 3351 / DRZ 7001) -> speaker voicing (pre-cab)
 *      -> MASTER volume
 *   GZ34 tube rectifier (A=365 / B=284 / C=250 / D=222 / E=204 V rail) -> sag.
 *   (The Maz 18 Jr also has a spring reverb: 12AT7 driver + tank + 12AX7/12AT7
 *    recovery. The SENIOR NR panel exposes NO reverb control and Params.h has
 *    no reverb param, so it is not part of the user-facing signal path here.)
 *
 * the game (rs_knob_to_vst_param.json): RS Gain -> VOLUME (the only drive pot);
 * Bass/Mid/Treble -> the TMB tone stack; Cut + Master set on the face by hand.
 *
 * STEREO I/O, single mono core (the amp IS mono): runs ONE core and writes the
 * same signal to both outputs (centered/balanced, half the CPU). Engine:
 * rbtube::TubeStage/PowerAmpPP/ToneStackYeh (our Koren/Yeh physics, NOT GPL),
 * 2x oversampling around the nonlinear chain (rbshared::Oversampler4x, OS=2).
 */
#include "DistrhoPlugin.hpp"
#include "Maz38Params.h"
#include "../../_shared/tube_stage.hpp"
#include "../../_shared/oversampler.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

// RB loudness/headroom output stage (shared across all amps): transparent below
// +/-0.90, saturates to a +/-0.99 ceiling so EQ boosts never hard-clip.
static inline float rbAmpLvl(float x){ const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);
    if(a<=t) return x; return (x<0.f?-1.f:1.f)*(t+(c-t)*std::tanh((a-t)/(c-t))); }

namespace mrymaz {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// RBJ biquad (peaking / shelves / low-pass) — same helper as BoxDC30Core; used
// ONLY for the mild pre-cab speaker voicing and the post-power Cut, never for
// the tubes or the tone stack (those are the real circuit models).
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

// Circuit-real Dr. Z Maz 18 Jr core. Interface == old Maz38Core (setParam/idx).
struct MrYMaz18Core {
    float sr = 96000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage v1, v2;           // V1a + V1b, real cathode-biased 12AX7 stages
    rbtube::Miller12AX7 inputMiller;    // 68k input stopper + V1a Miller capacitance
    rbtube::Miller12AX7 millerV2;       // Volume/coupling source -> V1b Miller capacitance
    rbtube::CouplingCapGridLeak coupleToV2; // Volume/coupling cap -> V1b grid blocking
    rbtube::CouplingCapGridLeak coupleToPi; // master/cut -> PI grid blocking
    rbtube::PhaseInverterLTP12AX7 phaseInverter;
    rbtube::MultiNodeBPlus supply;           // stiff Maz 38 B+ nodes
    rbtube::PowerAmpPP power;           // real class-A/AB push-pull EL84 (no NFB)
    rbtube::ToneStackYeh tonestack;     // real Maz TMB R/C network (Yeh model, double)
    Biquad bright, cutLP, spkBody, spkPres, spkRoll;
    // params (0..1) — Volume/Treble/Middle/Bass/Cut/Master
    float pVol=0.6f, pTreble=0.55f, pMid=0.5f, pBass=0.5f, pCut=0.4f, pMaster=0.7f, pCabSim=1.0f;
    float inGain=1, inScale=2, preGain=1, gainOut=1, outLevel=1;
    float lastPowerLoad=0.0f, lastScreenLoad=0.0f, lastPreampLoad=0.0f;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void reset(){ inputCoupling.reset(); inputMiller.reset(); v1.reset(); v2.reset(); millerV2.reset();
        coupleToV2.reset();
        coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        tonestack.reset(); bright.reset(); cutLP.reset(); spkBody.reset(); spkPres.reset(); spkRoll.reset();
        lastPowerLoad=lastScreenLoad=lastPreampLoad=0.0f; }

    void setParam(int idx, float v){
        v = clamp01(v);
        switch(idx){
            case kVolume: pVol=v; break;
            case kTreble: pTreble=v; break;
            case kMiddle: pMid=v; break;
            case kBass:   pBass=v; break;
            case kCut:    pCut=v; break;
            case kMaster: pMaster=v; break;
            case kCabSim: pCabSim=v; break;
            default: break;
        }
        recalc();
    }
    void initDefaults(){ for(int i=0;i<kParamCount;++i) setParam(i, kMaz38Def[i]); }

    void recalc(){
        inputCoupling.set(sr, 12.0f);                              // input grid-leak coupling (1M, ~Hz)
        // TWO real cathode-biased 12AX7 stages from the schematic:
        //   V1a: 68k plate (Guitarix uses 100k for the table; the 68k just lowers
        //        the gain a touch -> modeled via inScale), 680R cathode + 680n
        //        bypass -> fck = 1/(2*pi*680*680n) ~= 344 Hz, grid-leak 1M (250k tab).
        //   V1b: 100k plate, 820R cathode + 25u bypass -> fck = 1/(2*pi*820*25u)
        //        ~= 7.8 Hz (fully bypassed in band), 220k grid (250k tab).
        // Each self-biases (Vk0 solved) and saturates on its own load line; the
        // cathode loop makes the bias breathe with level (compression+asymmetry).
        inGain      = 0.40f + 1.6f * pVol;                         // input drive (en30-style)
        v1.set(sr, 1, 250.0f, 40.0f, 344.0f, 680.0f);              // V1a (1M grid-leak, 680R cathode)
        v2.set(sr, 1, 250.0f, 40.0f,   8.0f, 820.0f);              // V1b (220k->250k tab, 820R+25u fully bypassed)
        // VOLUME is the amp's single drive pot: a linear inter-stage pre-gain
        // (Guitarix `*(preamp)`), so turning it up drives V1b into breakup with NO
        // ad-hoc curve. Numbers copied from BoxDC30Core (clean floor + hot slope),
        // scaled for one preamp gain stage instead of two.
        float vol   = std::pow(pVol, 1.1f);                        // audio-taper volume
        inScale     = 1.30f;                                       // audio -> grid volts into V1a (V1a ~clean, 68k plate)
        preGain     = 0.35f + 3.5f * vol;                          // V1a->V1b pre-gain (clean low vol, cooks high)
        gainOut     = 0.60f + 0.55f * vol;                         // post-V1b level into the power amp
        inputMiller.set(sr,  68000.0f, 50.0f, 8.0f);               // Hi jack stopper + V1a Miller, ~28 kHz
        millerV2.set(sr,   180000.0f, 52.0f, 8.0f);               // Volume/coupling source + V1b Miller, ~9 kHz
        coupleToV2.set(sr, 220000.0f, 22.0e-9f, 180000.0f,
                       0.12f, 0.40f, 1.00f);
        // Bright cap across the input network (the small treble lift on the way in).
        bright.highShelf(sr, 2400.0f, 3.0f);
        // Maz 18 TMB tone stack = the REAL R/C network (Yeh, double) — EXACT
        // schematic values: Treble 250kL, Bass 250kL, Mid 10kL, slope 56k;
        // C1 250pF / C2 100nF / C3 47nF. (Guitarix cross-check: the Mesa/Twin
        // family is identical bar the mid pot; the Maz uses a small 10k mid =
        // a built-in scoop, like Guitarix `twin`.)
        tonestack.setComponents(250e3, 250e3, 10e3, 56e3, 250e-12, 100e-9, 47e-9);
        tonestack.update(sr, pTreble, pMid, pBass);                // t=Treble, m=Mid, l=Bass (all three pots real here)
        // CUT 250kL + 4.7n across the PI differential outputs: a treble shunt
        // between the two phases -> higher = darker. Modeled as a post-power LP
        // (real Maz: it tames the output treble post-distortion). 6k(no cut)->1k(full).
        cutLP.lowpass(sr, 1000.0f + 5000.0f * (1.0f - pCut), 0.7f);
        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 100000.0f, 0.12f, 0.42f, 1.10f);
        phaseInverter.setVoxAc30(sr, 0.72f + 1.10f * pMaster + 0.55f * vol, 0.84f);
        // Maz 38 Senior: more headroom and tighter lows than the Maz 18/GZ34 path.
        supply.set(sr,
                   18.0f, 100.0f,
                   1000.0f, 50.0f,
                   10000.0f, 32.0f,
                   0.055f + 0.018f * vol,
                   0.045f + 0.015f * vol,
                   0.030f + 0.012f * pVol,
                   0.14f);
        // EL84 push-pull with reduced sag/headroom compression for the Maz 38 role.
        power.set(sr, 2.6f + 9.0f * vol + 2.0f * pMaster, -7.5f, 0.10f);
        power.out   = 0.0075f;                                     // plate-volt differential -> signal (en30)
        outLevel    = 0.5f * (1.0f - 0.45f * pVol);                // loudness comp vs the drive pot
        // 1x12 EL84 voicing — mild, PRE-CAB (the cab IR adds the real speaker color).
        spkBody.peaking(sr, 110.0f, 0.8f, 2.0f);                   // gentle low body
        spkPres.highShelf(sr, 2500.0f, -4.0f + 9.0f * pTreble);    // Treble shelf headroom (real stack treble is small in-chain)
        spkRoll.highShelf(sr, 4000.0f, 4.0f);                      // EL84 top chime (pre-cab)
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus =
            supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x * inGain);
        x = bright.process(x);
        x = v1.process(inputMiller.process(x) * inScale * bplus.preamp);          // audio -> Miller-loaded V1a grid
        x = coupleToV2.process(x, 0.70f + 2.2f * preGain);
        x = v2.process(millerV2.process(x) * preGain * bplus.preamp);             // pre-gain (VOLUME) -> Miller-loaded V1b
        x = tonestack.process(x);                                  // real TMB stack after V1b, per schematic
        x *= gainOut;
        x = coupleToPi.process(x, 1.0f + 0.10f * preGain);
        lastPreampLoad = 0.10f * std::fabs(x) + 0.04f * pVol;
        x = phaseInverter.process(x * bplus.preamp);
        x = cutLP.process(x);                                      // CUT in the PI/post-preamp treble path
        lastPowerLoad = 0.64f * std::fabs(x) + 0.14f * pMaster + 0.14f * pVol;
        lastScreenLoad = 0.42f * std::fabs(x) + 0.08f * pVol;
        x = power.process(x * bplus.power * bplus.screen);         // EL84 push-pull + B+ dynamics
        float cab = spkRoll.process(spkPres.process(spkBody.process(x)));  // speaker voicing (pre-cab)
        x += pCabSim * (cab - x);
        // MASTER volume: a clean post-power output trim (the schematic Master is a
        // 250kL pot after the cut, before the OT-driven level). Keep loudness sane.
        float m = 0.35f + 0.95f * pMaster;
        return x * outLevel * m;
    }
};

} // namespace mrymaz

class Maz38Plugin : public Plugin
{
    mrymaz::MrYMaz18Core core;                  // single mono core (the amp is mono)
    float params[kParamCount];
    rbshared::Oversampler4x os;                 // 2x anti-alias around the nonlinear chain
    static constexpr int kOS = rbshared::Oversampler4x::OS;

    void applyAll() { for (int i = 0; i < kParamCount; ++i) core.setParam(i, params[i]); }

public:
    Maz38Plugin() : Plugin(kParamCount, 0, 0)
    {
        for (int i = 0; i < kParamCount; ++i) params[i] = kMaz38Def[i];
        core.setSampleRate(kOS * (float)getSampleRate());
        applyAll();
    }

protected:
    const char* getLabel() const override { return "MrYMaz38"; }
    const char* getDescription() const override { return "Mr. Y Maz 18 / Dr. Z Maz 18-style 2xEL84 amp"; }
    const char* getMaker() const override { return "RigBuilder"; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override { return d_cconst('Y', 'm', '3', '8'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= (uint32_t)kParamCount) return;
        parameter.hints = kParameterIsAutomatable;
        parameter.name = kMaz38Names[index];
        parameter.symbol = kMaz38Symbols[index];
        parameter.ranges.min = kMaz38Min[index];
        parameter.ranges.max = kMaz38Max[index];
        parameter.ranges.def = kMaz38Def[index];
    }

    float getParameterValue(uint32_t index) const override { return index < (uint32_t)kParamCount ? params[index] : 0.0f; }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= (uint32_t)kParamCount) return;
        params[index] = mrymaz::clamp01(value);
        core.setParam((int)index, params[index]);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        core.setSampleRate(kOS * (float)newSampleRate);
        os.reset();
        applyAll();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* in0 = inputs[0];
        float* outL = outputs[0];
        float* outR = outputs[1];
        for (uint32_t i = 0; i < frames; ++i)
        {
            float ub[kOS];
            os.upsample(3.2f * in0[i], ub);
            for (int k = 0; k < kOS; ++k)                  // core + output soft-clip at 2x
                ub[k] = rbAmpLvl(0.560f * core.process(ub[k]));
            const float y = os.downsample(ub);
            outL[i] = y;
            outR[i] = y;   // dual-mono: one core, same signal both sides = centered/balanced
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Maz38Plugin)
};

Plugin* createPlugin() { return new Maz38Plugin(); }

END_NAMESPACE_DISTRHO
