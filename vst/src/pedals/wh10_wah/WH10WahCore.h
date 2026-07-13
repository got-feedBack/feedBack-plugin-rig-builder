#ifndef WH10_WAH_CORE_H
#define WH10_WAH_CORE_H
//
// WH10WahCore — Ibañez WH10 (WN10) active op-amp wah, from the factory
// schematic (pedals/Ibanez WH10/ibanez_wh10_wah.pdf).
//
// The WH10 is NOT a passive inductor wah. Signal path in the schematic:
//   IN -> Q1 (2SC1815) buffer -> resonant band-pass around IC1A+IC1B (NJM4558)
//       swept by the FREQ treadle pot VR1 -> DEPTH pot VR2 sets resonance/mix
//       -> 2SA1015/2SC1815/2SK30A output buffers -> OUT (+ a Q3 TUNER OUT tap).
//   S2 GUITAR/BASS drops the whole sweep range ~an octave for bass.
//
// Modelled as: input DC-block -> a TPT state-variable band-pass whose centre
// frequency tracks the treadle position, whose Q rises with DEPTH, and whose
// resonant output is pushed into a soft op-amp overdrive (the famous WH10
// "throaty" grind when DEPTH is up and you dig in). A gentle post low-pass
// rounds the clipper fizz, and a mostly-wet mix keeps the vocal, pronounced
// character (the WH10 passes far less dry than a Cry Baby). AUTO mode sweeps
// the treadle from an envelope follower so it plays as a touch-wah without an
// expression pedal.
//
#include <cmath>

namespace wh10wah {

static constexpr float kTwoPi = 6.28318530717958648f;
static inline float clamp01(float v){ return v<0.f?0.f:(v>1.f?1.f:v); }
static inline float dn(float v){ return std::fabs(v)<1.0e-15f?0.f:v; }

class WH10WahCore {
    float fs = 48000.f;

    // params
    bool  autoSweep = true;
    float position = 0.50f;
    float depth    = 0.68f;
    float sens     = 0.60f;
    bool  bassRange = false;

    // envelope follower (auto / touch response)
    float env = 0.f, atk = 0.f, rel = 0.f;

    // TPT state-variable band-pass state
    float ic1 = 0.f, ic2 = 0.f;

    // 1-pole input DC-block + output post low-pass
    float hpX = 0.f, hpY = 0.f, hpA = 0.99f;
    float lpZ = 0.f;

    static inline float msCoef(float ms, float sr){ return std::exp(-1.0f/(0.001f*ms*sr)); }

    void updateDerived(){
        atk = msCoef(4.0f, fs);
        rel = msCoef(140.0f, fs);
        // input DC block ~30 Hz
        const float rc = 1.0f/(kTwoPi*30.0f), dt = 1.0f/fs;
        hpA = rc/(rc+dt);
    }

public:
    void setSampleRate(float sr){ fs = sr>1000.f?sr:48000.f; updateDerived(); reset(); }

    void reset(){ env=0.f; ic1=ic2=0.f; hpX=hpY=0.f; lpZ=0.f; }

    void setParams(float a, float pos, float dep, float sn, float rng){
        autoSweep = a>0.5f;
        position  = clamp01(pos);
        depth     = clamp01(dep);
        sens      = clamp01(sn);
        bassRange = rng>0.5f;
    }

    inline float process(float x){
        // ── input buffer + DC block (Q1, C1 coupling) ──
        const float in = x;
        hpY = hpA*(hpY + in - hpX); hpX = in;
        const float bx = hpY;

        // ── envelope follower for AUTO / touch response ──
        const float lvl = std::fabs(bx);
        const float c = lvl>env ? atk : rel;
        env = c*env + (1.0f-c)*lvl;
        float pick = env * (2.4f + sens*3.6f);
        if (pick>1.0f) pick = 1.0f;

        // ── treadle position (VR1) ──
        float pos;
        if (autoSweep){
            // touch-wah: DEPTH-free floor from the treadle, swept up by picking
            const float floorP = 0.08f + 0.34f*position;
            const float span   = 0.34f + 0.42f*sens;
            pos = floorP + span*pick;
        } else {
            // manual cocked-wah, with a little touch bloom
            pos = position + 0.16f*sens*pick;
        }
        pos = clamp01(pos);

        // ── centre frequency: exponential sweep, dropped ~an octave for bass ──
        // Guitar ~240 Hz..2.7 kHz (wider/lower/throatier than a Cry Baby),
        // Bass ~95 Hz..1.15 kHz.
        const float shaped = pos*pos*(3.0f-2.0f*pos);        // smoothstep
        const float fLo = bassRange ? 95.0f  : 240.0f;
        const float fHi = bassRange ? 1150.0f : 2700.0f;
        float fc = fLo * std::pow(fHi/fLo, shaped);
        const float nyq = fs*0.45f;
        if (fc>nyq) fc = nyq;

        // ── DEPTH (VR2): resonance + how hard the op-amp grinds ──
        const float q = 1.8f + depth*6.6f;                   // ~1.8..8.4, quacky at max

        // ── TPT state-variable band-pass ──
        const float g = std::tan(3.14159265359f * fc / fs);
        const float k = 1.0f/q;
        const float a1 = 1.0f/(1.0f + g*(g+k));
        const float a2 = g*a1;
        const float v3 = bx - ic2;
        const float bp = a1*ic1 + a2*v3;
        const float lp = ic2 + a2*ic1 + g*a2*v3;
        ic1 = 2.0f*bp - ic1;
        ic2 = 2.0f*lp - ic2;

        // resonant peak level rides up with Q; DEPTH sets the band-pass gain
        float wet = bp * k * (2.6f + 1.4f*depth);

        // ── op-amp soft overdrive (the WH10 grind): drive scales with DEPTH^2
        //    and how hard you're playing. Slight asymmetry for even harmonics. ──
        const float drive = 1.0f + depth*depth*5.5f + sens*1.4f;
        float d = wet*drive;
        d = std::tanh(d) + 0.06f*d*d/(1.0f+d*d);             // soft clip + gentle 2nd-harm bias
        wet = d / (0.7f + 0.3f*drive);                        // normalise back toward unity

        // ── gentle post low-pass to round clipper fizz (WH10 isn't harsh up top) ──
        const float lpFc = bassRange ? 2200.0f : 4200.0f;
        const float lg = std::exp(-kTwoPi*lpFc/fs);
        lpZ = lg*lpZ + (1.0f-lg)*wet;
        wet = lpZ;

        // ── mostly-wet mix (vocal, pronounced) + tiny dry for body ──
        return dn(wet + bx*0.05f);
    }
};

} // namespace wh10wah
#endif // WH10_WAH_CORE_H
