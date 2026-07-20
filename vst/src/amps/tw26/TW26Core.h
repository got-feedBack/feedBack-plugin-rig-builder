#ifndef TW26_CORE_H
#define TW26_CORE_H
//
// TW26Core - BENDER DELUXE / Fender '57 Deluxe (5E3 tweed), REBUILT circuit-real
// (same method as BOX AC30 / see REAL_TUBE_AMP_GUIDE.md). Real cathode-biased tube
// stages with pure Koren transfer tables + the physical cathode auto-bias loop:
//
//   in -> V1A/V1B 12AX7A instrument+mic channels -> interactive volume/tone
//   network -> V2A 12AX7 recovery -> V2B cathodyne PI -> 2x 6V6 push-pull
//   power (cathode-biased, NO global NFB, heavy 5Y3 tube-rectifier sag) ->
//   1x12 tweed speaker.
//
// The 5E3 VOLUME *is* the gain (it dirties as you turn up); the single Tone is the
// only real EQ; jumpering the Mic channel fills body/mids. Bright/Bass/Presence have
// no 5E3 pot -> they are voicing shelves driven by the game transform. Tubes use OUR
// Koren tables (public model), not Guitarix GPL code.
//
#include "../../_shared/tube_stage.hpp"
#include <cmath>

namespace tw26 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static inline float interpolateMakeupDb(const float* table, float value)
{
    const float p = 20.0f * clamp01(value);
    const int i = (int)p;
    if (i >= 20)
        return table[20];
    return table[i] + (table[i + 1] - table[i]) * (p - (float)i);
}

// Passive 5E3 control network from the uploaded Fender 57 schematic:
// C2/C3 feed the two 1M audio volume pots, their wipers share the V2 grid node,
// C4 (500pF) returns the Instrument signal through the upper half of the 1M
// Tone pot and C5 (4.7nF) shunts that node through its lower half.  Solving the
// node preserves the famous interaction between both volume controls; summing
// two independently-scaled channels does not.
struct TweedVolumeToneNetwork {
    float sampleRate = 192000.0f;
    float inst = 0.0f, mic = 0.0f, tone = 0.0f;
    double c3Voltage = 0.0, c3Current = 0.0;
    double c2Voltage = 0.0, c2Current = 0.0;
    double c4Voltage = 0.0, c4Current = 0.0;
    double c5Voltage = 0.0, c5Current = 0.0;

    void set(float sr, float instPos, float micPos, float tonePos) {
        sampleRate = std::fmax(8000.0f, sr);
        inst = clamp01(instPos);
        mic = clamp01(micPos);
        tone = clamp01(tonePos);
    }

    static double resistance(double value) { return std::fmax(1.0, value); }

    inline float process(float instSource, float micSource) {
        constexpr double pot = 1000000.0;
        constexpr double plateSource = 40000.0;
        constexpr double c2c3 = 100.0e-9;
        constexpr double c4 = 500.0e-12;
        constexpr double c5 = 4.7e-9;

        // In this Fender drawing each channel signal enters at the pot wiper.
        // The CW end feeds V2 and the opposite end is grounded. At zero the
        // unused pot therefore leaves 1M from V2 to ground instead of shorting
        // the other channel, which is the source of the 5E3 control interaction.
        const double rio = resistance((1.0 - inst) * pot);
        const double rig = resistance(inst * pot);
        const double rmo = resistance((1.0 - mic) * pot);
        const double rmg = resistance(mic * pot);
        const double rtt = resistance((1.0 - tone) * pot);
        const double rtb = resistance(tone * pot);

        const double gio = 1.0 / rio, gig = 1.0 / rig;
        const double gmo = 1.0 / rmo, gmg = 1.0 / rmg;

        // Trapezoidal companions for C2/C3 with the measured plate source
        // impedance, plus C4-RtoneTop and RtoneBottom-C5.
        const double k23 = 1.0 / (2.0 * sampleRate * c2c3);
        const double k4 = 1.0 / (2.0 * sampleRate * c4);
        const double k5 = 1.0 / (2.0 * sampleRate * c5);
        const double gs = 1.0 / (plateSource + k23);
        const double gt = 1.0 / (rtt + k4);
        const double gb = 1.0 / (rtb + k5);
        const double h3 = c3Voltage + k23 * c3Current;
        const double h2 = c2Voltage + k23 * c2Current;
        const double h4 = c4Voltage + k4 * c4Current;
        const double h5 = c5Voltage + k5 * c5Current;

        // Solve the three-node passive network [instrument wiper, mic wiper,
        // V2 grid]. The matrix is symmetric and positive definite.
        double a[3][4] = {
            { gs + gig + gio + gt, 0.0, -(gio + gt),
              gs * ((double)instSource - h3) + gt * h4 },
            { 0.0, gs + gmg + gmo, -gmo,
              gs * ((double)micSource - h2) },
            { -(gio + gt), -gmo, gio + gmo + gt + gb,
              -gt * h4 + gb * h5 }
        };
        for (int col = 0; col < 3; ++col) {
            int pivot = col;
            for (int row = col + 1; row < 3; ++row)
                if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
            if (pivot != col)
                for (int j = col; j < 4; ++j) {
                    const double t = a[col][j]; a[col][j] = a[pivot][j]; a[pivot][j] = t;
                }
            const double inv = 1.0 / a[col][col];
            for (int j = col; j < 4; ++j) a[col][j] *= inv;
            for (int row = 0; row < 3; ++row) {
                if (row == col) continue;
                const double f = a[row][col];
                for (int j = col; j < 4; ++j) a[row][j] -= f * a[col][j];
            }
        }
        const double instWiper = a[0][3];
        const double micWiper = a[1][3];
        const double output = a[2][3];

        const double i3 = gs * ((double)instSource - instWiper - h3);
        const double i2 = gs * ((double)micSource - micWiper - h2);
        const double i4 = gt * (instWiper - output - h4);
        const double i5 = gb * (output - h5);
        c3Voltage += k23 * (i3 + c3Current);
        c2Voltage += k23 * (i2 + c2Current);
        c4Voltage += k4 * (i4 + c4Current);
        c5Voltage += k5 * (i5 + c5Current);
        c3Current = i3;
        c2Current = i2;
        c4Current = i4;
        c5Current = i5;
        return (float)output;
    }

    void reset() {
        c3Voltage = c3Current = 0.0;
        c2Voltage = c2Current = 0.0;
        c4Voltage = c4Current = 0.0;
        c5Voltage = c5Current = 0.0;
    }
};

static inline float volumeMakeupDb(float inst, float mic)
{
    // Post-circuit calibration from the exact 32-second Brit DI at 48 kHz,
    // Tone/Bass/Presence noon and Cab Sim off. The three tables hold the
    // Instrument, Mic and equally-jumpered sweeps at -21.5 dBFS RMS. They are
    // monitor gains only and therefore cannot change tube drive or sag.
    static const float kInstDb[21] = {
        10.488f, 3.537f, 1.523f, 0.619f, 0.079f,
        -0.304f,-0.611f,-0.876f,-1.120f,-1.353f,
        -1.580f,-1.806f,-2.128f,-3.239f,-4.843f,
        -6.514f,-8.027f,-9.285f,-10.247f,-10.942f,-11.427f
    };
    static const float kMicDb[21] = {
        10.488f, 7.282f, 5.622f, 4.731f, 4.144f,
         3.689f, 3.301f, 2.947f, 2.607f, 2.272f,
         1.930f, 1.580f, 1.090f,-0.453f,-2.626f,
        -4.837f,-6.762f,-8.210f,-9.170f,-9.771f,-10.155f
    };
    static const float kBothDb[21] = {
        10.488f, 2.706f, 0.643f,-0.264f,-0.798f,
        -1.171f,-1.466f,-1.717f,-1.943f,-2.157f,
        -2.361f,-2.561f,-2.851f,-3.902f,-5.426f,
        -7.011f,-8.437f,-9.601f,-10.461f,-11.035f,-11.368f
    };

    inst = clamp01(inst);
    mic = clamp01(mic);
    if (mic < 1.0e-4f)
        return interpolateMakeupDb(kInstDb, inst);
    if (inst < 1.0e-4f)
        return interpolateMakeupDb(kMicDb, mic);

    const bool instDominant = inst >= mic;
    const float dominant = instDominant ? inst : mic;
    const float weaker = instDominant ? mic : inst;
    const float isolatedDb = interpolateMakeupDb(instDominant ? kInstDb : kMicDb, dominant);
    const float bothDb = interpolateMakeupDb(kBothDb, dominant);
    const float balance = weaker / std::fmax(1.0e-4f, dominant);
    return isolatedDb + balance * (bothDb - isolatedDb);
}
// RBJ biquad (peaking / shelves / low-pass), denormal-flushed.
struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x){ float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=rbtube::dn(y); return y; }
    void reset(){ x1=x2=y1=y2=0; }
    void norm(float a0){ b0/=a0;b1/=a0;b2/=a0;a1/=a0;a2/=a0; }
    void peaking(float sr,float f,float Q,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=1+al*A;b1=-2*c;b2=1-al*A; float a0=1+al/A;a1=-2*c;a2=1-al/A; norm(a0); }
    void lowShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)-(A-1)*c+2*rA*al); b1=2*A*((A-1)-(A+1)*c); b2=A*((A+1)-(A-1)*c-2*rA*al);
        float a0=(A+1)+(A-1)*c+2*rA*al; a1=-2*((A-1)+(A+1)*c); a2=(A+1)+(A-1)*c-2*rA*al; norm(a0); }
    void highShelf(float sr,float f,float dB){ float A=powf(10,dB/40),w=2*kPi*f/sr,c=cosf(w),s=sinf(w),al=s/2*sqrtf((A+1/A)*1.0f+2); float rA=sqrtf(A);
        b0=A*((A+1)+(A-1)*c+2*rA*al); b1=-2*A*((A-1)+(A+1)*c); b2=A*((A+1)+(A-1)*c-2*rA*al);
        float a0=(A+1)-(A-1)*c+2*rA*al; a1=2*((A-1)-(A+1)*c); a2=(A+1)-(A-1)*c-2*rA*al; norm(a0); }
    void lowpass(float sr,float f,float Q){ f=fminf(f,sr*0.49f); float w=2*kPi*f/sr,c=cosf(w),al=sinf(w)/(2*Q);
        b0=(1-c)/2;b1=1-c;b2=(1-c)/2; float a0=1+al;a1=-2*c;a2=1-al; norm(a0); }
};

struct TW26Core {
    float sr = 48000.0f;
    rbtube::HP1 inputCoupling;
    rbtube::TubeStage instV1, micV1;    // V1-A/V1-B 12AX7A in the uploaded schematic
    rbtube::TubeStage    v2;            // V2A 12AX7 recovery/gain
    rbtube::Miller12AX7 instMiller, micMiller;
    rbtube::Miller12AX7 millerV2;       // Volume/Tone source -> 12AX7 Miller load
    rbtube::CouplingCapGridLeak coupleToV2, coupleToPi;
    rbtube::PhaseInverterCathodyne12AX7 phaseInverter; // V2B split-load PI
    rbtube::MultiNodeBPlus supply;      // 5Y3 + 16uF nodes + 4k7/22k droppers
    rbtube::PowerAmp6V6  power;         // 2x 6V6 push-pull, cathode-biased, no NFB
    Biquad bassSh, outputTransformerLow, outputTransformerAir, crankedAirTrim, crankedTopTrim;
    Biquad spkBody, spkRoll, midScoop;
    TweedVolumeToneNetwork controls;
    // params (0..1), interface identical to the old TW26Core
    float pTone=0.6f, pInst=0.45f, pMic=0.0f, pBright=1.0f, pBass=0.5f, pPres=0.5f, pCabSim=1.0f;
    float inScale=1, inputSensitivity=1, preGain=1, gainOut=1;
    float instPot=0, micPot=0, tonePot=0;
    float lastPowerLoad=0, lastScreenLoad=0, lastPreampLoad=0;

    void setSampleRate(float s){ sr=s; recalc(); reset(); }
    void setTone(float v){ pTone=clamp01(v); recalc(); }
    void setInstVol(float v){ pInst=clamp01(v); recalc(); }
    void setMicVol(float v){ pMic=clamp01(v); recalc(); }
    void setBright(float v){ pBright=clamp01(v); recalc(); }
    void setBass(float v){ pBass=clamp01(v); recalc(); }
    void setPresence(float v){ pPres=clamp01(v); recalc(); }
    void setCabSim(float v){ pCabSim=clamp01(v); }
    float outputMakeup() const {
        const float active = std::fmax(pInst, pMic);
        const float finalTrimDb = 0.12f - 0.96f * active * active;
        return std::pow(10.0f, 0.05f * (volumeMakeupDb(pInst, pMic) + finalTrimDb));
    }
    void reset(){ inputCoupling.reset(); instMiller.reset(); micMiller.reset(); instV1.reset(); micV1.reset(); v2.reset();
        millerV2.reset(); coupleToV2.reset(); coupleToPi.reset(); phaseInverter.reset(); supply.reset(); power.reset();
        controls.reset(); bassSh.reset();
        outputTransformerLow.reset(); outputTransformerAir.reset(); crankedAirTrim.reset(); crankedTopTrim.reset();
        spkBody.reset(); spkRoll.reset(); midScoop.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f; }

    void recalc(){
        const float activeUi = std::fmax(pInst, pMic);
        float cranked = clamp01((activeUi - 0.58f) / 0.42f);
        cranked = cranked * cranked * (3.0f - 2.0f * cranked);
        inputCoupling.set(sr, 12.0f);
        // Real cathode-biased stages (self-bias solved). Fender '57 Deluxe schematic:
        // The supplied Fender 57 drawing specifies 12AX7A for both halves of V1,
        // sharing R7 820R/C1 25uF, and another 12AX7A for V2A (R13/C6).
        instV1.set(sr, 0, 250.0f, 40.0f, 7.8f, 820.0f);
        micV1.set(sr,  0, 250.0f, 40.0f, 7.8f, 820.0f);
        v2.set(sr,    1, 250.0f, 40.0f, 4.2f, 1500.0f);
        instMiller.set(sr, pBright >= 0.5f ? 68000.0f : 34000.0f, 24.0f, 8.0f);
        micMiller.set(sr,  pBright >= 0.5f ? 68000.0f : 34000.0f, 24.0f, 8.0f);
        millerV2.set(sr,  180000.0f, 52.0f, 8.0f);

        // 1M audio volume controls and single 1M tone control. The electric taper is
        // real; the drive constants are calibrated so breakup still arrives around
        // the 5E3's real knob range instead of becoming a sterile linear sweep.
        // The reference plug-in leaves the measured residual at the mechanical
        // endpoints of its pots. Preserve that endpoint instead of turning a
        // UI value of zero into an ideal electrical short/open.
        instPot = 0.005f + 0.995f * rbtube::PotTaper::audio(pInst, 1.28f);
        micPot  = 0.005f + 0.995f * rbtube::PotTaper::audio(pMic,  1.28f);
        tonePot = 0.020f + 0.960f * rbtube::PotTaper::audio(pTone, 1.18f);
        inScale = 3.25f;
        // Input 2 is the 68k/68k low-sensitivity divider, not a treble switch.
        // Input 1 keeps the full signal into the 1M grid return.
        inputSensitivity = pBright >= 0.5f ? 1.0f : 0.65f;
        // 5E3 Volume pots are post-V1 attenuators feeding V2. The fixed
        // interstage gain preserves the real operating point: knob position
        // changes signal amplitude, never the PI/power transfer itself.
        preGain = 1.10f;
        gainOut = 1.20f;

        // 0.1uF coupling caps into ~1M grid leaks in the 5E3 preamp path; 0.022uF
        // into the cathodyne grid. Positive-grid drive now charges/recover caps
        // instead of passing through ideal HPFs.
        coupleToV2.set(sr, 1000000.0f, 100.0e-9f, 68000.0f, 0.30f, 0.07f, 0.24f);
        coupleToPi.set(sr, 1000000.0f, 22.0e-9f, 220000.0f, 0.30f, 0.07f, 0.22f);
        phaseInverter.set(sr, 2.10f, 0.92f, 250.0f,
                          56000.0f, 56000.0f, 2.3f, 0.0f);

        // 5Y3 supply: 16uF reservoir/screen/preamp nodes, 4k7 and 22k droppers.
        supply.set(sr, 420.0f, 16.0f, 4700.0f, 16.0f, 22000.0f, 16.0f,
                   0.40f, 0.28f, 0.15f, 0.26f);
        controls.set(sr, instPot, micPot, tonePot);
        bassSh.lowShelf(sr, 180.0f, 12.0f * (pBass - 0.5f));       // hidden game shelf; neutral at noon
        // 2x 6V6 push-pull, cathode-biased, NO NFB, heavy 5Y3 sag (big bloom)
        const float micPowerDrive = pMic > pInst ? 2.5f * cranked : 0.0f;
        power.set(sr, 8.5f + 6.5f * cranked + micPowerDrive,
                  -13.0f, 0.42f, 32.0f, 18000.0f);
        power.out = 0.009f;
        power.biasShift = 3.0f;
        // Electrical low-frequency rise from the 5E3 OT/reactive load remains
        // in amp-only mode; this is not an acoustic speaker/cabinet filter.
        const float activePot = std::fmax(instPot, 0.80f * micPot);
        outputTransformerLow.lowShelf(sr, 95.0f, 12.5f + 5.5f * activePot);
        // Keep only the fixed electrical compensation for the cascaded tube
        // anti-alias poles. Tone-dependent air boosts belonged to the failed
        // reference fit and contradicted C4/C5 at the dark end.
        const float toneBright = std::fmax(0.0f, (tonePot - 0.44f) / 0.56f);
        const float toneDark = std::fmax(0.0f, (0.44f - tonePot) / 0.44f);
        outputTransformerAir.highShelf(sr, 4800.0f,
                                       20.5f + 13.0f * activePot
                                       + 3.0f * toneBright - 4.0f * toneDark);
        if (pMic > pInst) {
            // The microphone channel reference loses substantially more upper
            // harmonic energy once V2/PI/6V6 saturation starts.
            crankedAirTrim.highShelf(sr, 1800.0f, -8.5f * cranked);
        } else {
            // Instrument keeps its top octave, but its cranked upper mids are
            // softer than a broad high-shelf correction would allow.
            crankedAirTrim.peaking(sr, 3200.0f, 0.75f, -2.2f * cranked);
        }
        crankedTopTrim.highShelf(sr, 2600.0f, -1.5f * cranked);
        // 1x12 tweed speaker (mild, pre-cab) + Presence top lift
        spkBody.peaking(sr, 90.0f, 0.6f, 9.0f);                  // fuller lows to match the bassy amp-only Woodrow reference (was thin)
        midScoop.peaking(sr, 1500.0f, 0.9f, -4.0f);             // scoop the upper-mids (ours was ~+3.5 dB hot vs the ref)
        spkRoll.highShelf(sr, 3500.0f, 2.0f + 6.0f * pPres);      // Presence
    }

    inline float process(float x){
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        x = inputCoupling.process(x) * inputSensitivity;
        const float instIn = x;
        const float micIn = x * 0.92f;
        float inst = instV1.process(instMiller.process(instIn) * inScale * bplus.preamp);
        float mic = micV1.process(micMiller.process(micIn) * (inScale * 0.92f) * bplus.preamp);
        const float activeUi = std::fmax(pInst, pMic);
        float cranked = clamp01((activeUi - 0.58f) / 0.42f);
        cranked = cranked * cranked * (3.0f - 2.0f * cranked);
        // Noon is already close to the reference. Above roughly 7/12 the 5E3
        // rapidly drives V2, the cathodyne and the cathode-biased 6V6 pair;
        // keeping this factor fixed was why max remained essentially clean.
        const float controlNetworkDrive = 1.25f * (1.0f + 2.0f * cranked);
        const float micDrive = micPot > instPot ? (1.0f + 0.25f * cranked) : 1.0f;
        x = controls.process(inst, mic) * controlNetworkDrive * micDrive;
        x = bassSh.process(x);
        x = v2.process(millerV2.process(coupleToV2.process(x, preGain)) * bplus.preamp);
        x = coupleToPi.process(x, gainOut);
        lastPreampLoad = std::fabs(x) * 0.55f;
        x = phaseInverter.process(x * bplus.screen);
        lastScreenLoad = std::fabs(x) * 0.70f;
        x = power.process(x * bplus.power * bplus.screen);        // 6V6 push-pull
        lastPowerLoad = std::fabs(x) * 0.90f;
        const float ampOnly = crankedTopTrim.process(crankedAirTrim.process(
            outputTransformerAir.process(outputTransformerLow.process(x))));
        const float cab = spkRoll.process(midScoop.process(spkBody.process(ampOnly))); // 1x12 voicing
        x = ampOnly + pCabSim * (cab - ampOnly);
        return x;
    }
};

} // namespace tw26
#endif // TW26_CORE_H
