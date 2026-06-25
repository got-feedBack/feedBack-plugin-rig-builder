#ifndef TW22_CORE_H
#define TW22_CORE_H

/*
 * TW22Core - BENDER SUPERNOVA 22 / Fender Super-Sonic 22 (6V6) component model.
 *
 * White-box audio model (no SPICE), plain C++ so it can be unit-tested offline.
 *
 * Local reference:
 *   amps/Fender SuperSonic 22 (TW22)/Fender-Super-Sonic-22-Schematic.pdf
 *
 * The REAL Super-Sonic 22 is a TWO-channel amp (sheet 3 = the front panel):
 *   - VINTAGE: blackface-style American clean — bright, TIGHT, sparkly. Has its
 *     own Volume/Treble/Bass + a Norm/Fat voicing switch (shared input bright cap).
 *   - BURN: cascaded 12AX7 gain stages -> hot-rodded modern lead. Its own
 *     Gain 1, Gain 2, Treble, Bass, Middle, Volume.
 *   A Vintage/Burn switch picks the channel; a shared spring Reverb; 12AT7 PI ->
 *   2x 6V6GT push-pull (~22 W), solid-state rectifier (mild sag), Celestion V30.
 *
 * the game's single Gain knob DRIVES THE CHANNEL: the `channel` param crossfades
 * Vintage(0) -> Burn(1). Because the two channels have very different inherent
 * gain (clean vs cascaded), the morph alone sweeps clean -> cranked. Treble/Bass/
 * Mid map to the Burn tone stack; Bright -> Norm/Fat; Pres -> the power presence.
 */

#include "TW22Params.h"
#include "../../_shared/tube_stage.hpp"   // real 12AX7 + 6V6 circuit models (Koren tables)
#include <cmath>

namespace tw22 {

static constexpr float kPi = 3.14159265359f;

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float clampFreq(float hz, float sr) { const float ny = sr * 0.45f; return std::fmax(10.0f, std::fmin(hz, ny)); }
static inline float smoothstep(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
static inline float smoothstepRange(float e0, float e1, float x) { return smoothstep((x - e0) / (e1 - e0)); }
static inline float softClip(float x) { return std::tanh(x); }
static inline float eqDb(float v, float rangeDb) { return (clamp01(v) - 0.5f) * 2.0f * rangeDb; }

// smooth (C-infinity) asymmetric tube curves — NO piecewise zero-crossing kink
static inline float triode12AX7(float x, float bias) {
    const float g = x + bias;
    const float warped = 1.55f * g + 0.34f * g * std::fabs(g);
    const float idle   = 1.55f * bias + 0.34f * bias * std::fabs(bias);
    return std::tanh(warped) - std::tanh(idle);
}
static inline float sixV6Pair(float x, float bias) {
    const float p = std::tanh(1.30f * (x + bias) + 0.05f * x * x);
    const float n = std::tanh(1.30f * (-x + bias) + 0.05f * x * x);
    const float idle = std::tanh(1.30f * bias);
    return 0.5f * ((p - idle) - (n - idle));
}

class RcHighPass {
    float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    void setHz(float sr, float hz) { hz = clampFreq(hz, sr); const float tau = 1.0f / (2.0f * kPi * hz), dt = 1.0f / std::fmax(sr, 1000.0f); a = tau / (tau + dt); }
    float process(float x) { const float y = a * (y1 + x - x1); x1 = x; y1 = y; return y; }
};
class RcLowPass {
    float a = 1.0f, z = 0.0f;
public:
    void reset() { z = 0.0f; }
    void setHz(float sr, float hz) { hz = clampFreq(hz, sr); const float tau = 1.0f / (2.0f * kPi * hz), dt = 1.0f / std::fmax(sr, 1000.0f); a = dt / (tau + dt); }
    float process(float x) { z += a * (x - z); return z; }
};

class Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        if (std::fabs(na0) < 1.0e-12f) na0 = 1.0f; const float k = 1.0f / na0;
        b0 = nb0 * k; b1 = nb1 * k; b2 = nb2 * k; a1 = na1 * k; a2 = na2 * k;
    }
public:
    void reset() { z1 = z2 = 0.0f; }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void setLowPass(float sr, float hz, float q) { hz = clampFreq(hz, sr); const float w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) * .5f, 1 - c, (1 - c) * .5f, 1 + al, -2 * c, 1 - al); }
    void setHighPass(float sr, float hz, float q) { hz = clampFreq(hz, sr); const float w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) * .5f, -(1 + c), (1 + c) * .5f, 1 + al, -2 * c, 1 - al); }
    void setPeaking(float sr, float hz, float q, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void setLowShelf(float sr, float hz, float slope, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), s = std::sin(w), rA = std::sqrt(A);
        const float al = s * .5f * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2);
        set(A * ((A + 1) - (A - 1) * c + 2 * rA * al), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - 2 * rA * al),
            (A + 1) + (A - 1) * c + 2 * rA * al, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - 2 * rA * al); }
    void setHighShelf(float sr, float hz, float slope, float dB) { hz = clampFreq(hz, sr); const float A = std::pow(10.f, dB / 40), w = 2 * kPi * hz / sr, c = std::cos(w), s = std::sin(w), rA = std::sqrt(A);
        const float al = s * .5f * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2);
        set(A * ((A + 1) + (A - 1) * c + 2 * rA * al), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - 2 * rA * al),
            (A + 1) - (A - 1) * c + 2 * rA * al, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * rA * al); }
};

class DcBlock {
    float x1 = 0.0f, y1 = 0.0f;
public:
    void reset() { x1 = y1 = 0.0f; }
    float process(float x) { const float y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
};

// compact mono spring reverb (3 allpass diffusers + 2 damped combs, band-limited)
class SpringReverb {
    float ap0[1024], ap1[1024], ap2[1024];
    float cb0[3600], cb1[3600];
    int p0 = 0, p1 = 0, p2 = 0, c0 = 0, c1 = 0;
    int n0 = 225, n1 = 341, n2 = 441, nc0 = 1617, nc1 = 1991;
    float damp0 = 0.0f, damp1 = 0.0f;
    Biquad inHp, inLp;
    static inline float apStep(float* buf, int& p, int n, float in, float g) {
        const float bo = buf[p]; const float v = in + bo * g; buf[p] = v;
        if (++p >= n) p = 0; return bo - v * g;
    }
public:
    void setSampleRate(float sr) {
        const float s = (sr > 1000.0f ? sr : 48000.0f) / 48000.0f;
        n0 = (int)(225 * s); n1 = (int)(341 * s); n2 = (int)(441 * s);
        nc0 = (int)(1617 * s); nc1 = (int)(1991 * s);
        if (nc0 > 3599) nc0 = 3599; if (nc1 > 3599) nc1 = 3599;
        inHp.setHighPass(sr, 240.0f, 0.7f); inLp.setLowPass(sr, 3800.0f, 0.7f);
        clear();
    }
    void clear() {
        for (int i = 0; i < 1024; ++i) ap0[i] = ap1[i] = ap2[i] = 0.0f;
        for (int i = 0; i < 3600; ++i) cb0[i] = cb1[i] = 0.0f;
        p0 = p1 = p2 = c0 = c1 = 0; damp0 = damp1 = 0.0f;
    }
    float process(float x) {
        x = inLp.process(inHp.process(x));
        x = apStep(ap0, p0, n0, x, 0.6f);
        x = apStep(ap1, p1, n1, x, 0.6f);
        x = apStep(ap2, p2, n2, x, 0.6f);
        const float o0 = cb0[c0]; damp0 += 0.42f * (o0 - damp0); cb0[c0] = x + damp0 * 0.71f; if (++c0 >= nc0) c0 = 0;
        const float o1 = cb1[c1]; damp1 += 0.42f * (o1 - damp1); cb1[c1] = x + damp1 * 0.69f; if (++c1 >= nc1) c1 = 0;
        return (o0 + o1) * 0.5f;
    }
};

class TW22Core {
    float sampleRate = 48000.0f;
    float vintVol  = kTW22Def[kVintVol];
    float vintTre  = kTW22Def[kVintTreble];
    float vintBass = kTW22Def[kVintBass];
    float normFat  = kTW22Def[kNormFat];
    float channel  = kTW22Def[kChannel];
    float gain1    = kTW22Def[kGain1];
    float gain2    = kTW22Def[kGain2];
    float burnTre  = kTW22Def[kBurnTreble];
    float burnBass = kTW22Def[kBurnBass];
    float burnMid  = kTW22Def[kBurnMid];
    float burnVol  = kTW22Def[kBurnVol];
    float reverb   = kTW22Def[kReverb];
    float presenceK = kTW22Def[kPresence];
    float cabSim   = kTW22Def[kCabSim];

    RcHighPass inputHp;
    Biquad inputBright;          // shared Fender bright cap; Norm(bright)/Fat(dark)
    // Vintage channel tone
    rbtube::ToneStackYeh vintTone;   // Vintage: real blackface tone stack (Twin values)
    // Burn channel
    RcHighPass burnTighten;
    RcLowPass interstageLp;
    rbtube::ToneStackYeh burnTone;   // Burn: real hot-rod TMB tone stack (DeVille values)
    // power amp / speaker
    Biquad presence;            // power-amp NFB presence (RS Pres)
    Biquad speakerHp, speakerBody, speakerBite, speakerFizz, speakerLp;
    DcBlock dcBlock;
    SpringReverb spring;
    float sag = 0.0f;

    // ── REAL tube stages (Koren circuit models) replacing the tanh approximations ──
    rbtube::TubeStage vintTube;            // VINTAGE: 1x 12AX7
    rbtube::TubeStage burn1, burn2, burn3; // BURN: 3x 12AX7 cascade
    rbtube::Miller12AX7 vintMiller, burn1Miller, burn2Miller, burn3Miller;
    rbtube::CouplingCapGridLeak burnCouple12, burnCouple23, coupleToPi;
    rbtube::PhaseInverterLTP12AT7 phaseInverter; // real 12AT7 Fender LTP
    rbtube::MultiNodeBPlus supply;               // stiff silicon B+ nodes
    rbtube::PowerAmp6V6 power;             // 2x 6V6 push-pull (~22W)
    float vDrv=1, b1g=1, b2g=1, b3g=1; // computed drives (audio->grid volts)
    float vintVolPot=0, gain1Pot=0, gain2Pot=0, burnVolPot=0;
    float lastPowerLoad=0, lastScreenLoad=0, lastPreampLoad=0;

    void updateFilters() {
        const float wBurn = smoothstep(channel);    // 0=Vintage .. 1=Burn
        vintVolPot = rbtube::PotTaper::audio(vintVol, 1.24f);
        gain1Pot = rbtube::PotTaper::audio(gain1, 1.18f);
        gain2Pot = rbtube::PotTaper::audio(gain2, 1.16f);
        burnVolPot = rbtube::PotTaper::audio(burnVol, 1.20f);
        const float hot = smoothstepRange(0.45f, 1.0f, channel) * smoothstep(gain1Pot);

        // ── real 12AX7 / 6V6 circuit stages (cathode-biased, self-bias solved) ──
        vintTube.set(sampleRate, 1, 250.0f, 40.0f, 30.0f, 1500.0f);   // Vintage 1x 12AX7
        burn1.set(sampleRate, 1, 250.0f, 40.0f, 25.0f, 1500.0f);      // Burn cascade
        burn2.set(sampleRate, 1, 250.0f, 40.0f, 40.0f, 1500.0f);
        burn3.set(sampleRate, 1, 250.0f, 40.0f, 60.0f, 1500.0f);
        vintMiller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        burn1Miller.set(sampleRate, 68000.0f, 55.0f, 8.0f);
        burn2Miller.set(sampleRate, 220000.0f, 52.0f, 8.0f);
        burn3Miller.set(sampleRate, 180000.0f, 52.0f, 8.0f);
        // Cascaded Burn coupling caps + grid leaks. Exact cap labels vary by
        // schematic node; these follow the SS22 12AX7 cascade scale (22n/1M)
        // so blocking/recovery happens in the right places instead of ideal HPFs.
        burnCouple12.set(sampleRate, 1000000.0f, 22.0e-9f, 150000.0f, 0.12f, 0.50f, 1.55f);
        burnCouple23.set(sampleRate, 1000000.0f, 22.0e-9f, 180000.0f, 0.12f, 0.56f, 1.75f);
        coupleToPi.set(sampleRate, 1000000.0f, 47.0e-9f, 180000.0f, 0.13f, 0.48f, 1.35f);
        phaseInverter.setFenderAB763(sampleRate, 0.90f + 1.20f * wBurn + 2.40f * hot, 0.86f);
        // Super-Sonic 22 uses silicon rectification and larger modern filter caps:
        // much stiffer than tweed, but screens/preamp still sag under Burn load.
        supply.set(sampleRate, 35.0f, 47.0f, 470.0f, 22.0f, 10000.0f, 22.0f,
                   0.095f + 0.035f * hot, 0.070f + 0.030f * hot,
                   0.035f + 0.015f * hot, 0.12f);
        // 2x 6V6 push-pull, solid-state rectifier (MILD sag), NFB approximated by presence
        power.set(sampleRate, 1.7f + 4.6f * wBurn
                  + 6.6f * wBurn * (0.5f * smoothstep(gain1Pot) + 0.5f * smoothstep(gain2Pot))
                  + 3.0f * hot, -13.0f, 0.32f, 80.0f, 10000.0f);   // Burn drive scales with BOTH gains
        power.out = 0.011f;
        // drives (audio -> grid volts): Vintage stays clean-ish; Burn cascade driven by Gain1/Gain2
        vDrv = 1.7f * (0.6f + 1.8f * vintVolPot);
        b1g  = 2.0f * (0.40f + 3.0f * gain1Pot);
        b2g  = 1.3f * (0.45f + 3.2f * gain2Pot);
        b3g  = 0.85f + 0.65f * gain2Pot;

        // input grid HP (tightens a little more on the Burn channel) + shared
        // bright cap (Norm = sparkly, Fat = darker/fuller).
        inputHp.setHz(sampleRate, 45.0f + 32.0f * wBurn);
        inputBright.setHighShelf(sampleRate, 2150.0f, 0.70f, 4.0f - 5.5f * normFat);

        // --- VINTAGE tone (American clean: bright, tight, mild) ---
        // EXACT Super-Sonic Vintage tone (schematic): Treble R9 250k, Bass R10 250k,
        // Mid = R11 6.8k FIXED resistor (no pot), slope R8 100k; C4 250pF, C5 .1uF, C6 .047uF.
        vintTone.setComponents(250e3, 250e3, 6.8e3, 100e3, 250e-12, 100e-9, 47e-9);
        vintTone.update(sampleRate, vintTre, 1.0f, vintBass);     // mid fixed (full R3, no pot)

        // --- BURN cascade tone (scooped American hi-gain) ---
        burnTighten.setHz(sampleRate, 60.0f + 70.0f * gain1 + 55.0f * wBurn);
        interstageLp.setHz(sampleRate, 9200.0f - 2600.0f * gain2 + 1400.0f * burnTre);
        // low shelf with a body baseline so the Burn channel isn't thin (it was
        // ~12 dB light in the lows vs the Box/Deluxe; American-tight, not anaemic).
        // EXACT Super-Sonic Burn TMB (schematic): Treble R47 250k, Bass R48 250k, Mid R49
        // 25k, slope R46 120k; C22 150pF, C23 .15uF, C24 .022uF.
        burnTone.setComponents(250e3, 250e3, 25e3, 120e3, 150e-12, 150e-9, 22e-9);
        burnTone.update(sampleRate, burnTre, burnMid, burnBass);

        // --- power-amp presence (RS Pres) ---
        presence.setHighShelf(sampleRate, 2900.0f, 0.80f, -3.0f + 8.0f * presenceK);

        // --- Celestion V30 12" (bright / tight, modest body) ---
        speakerHp.setHighPass(sampleRate, 84.0f, 0.72f);
        speakerBody.setPeaking(sampleRate, 205.0f, 0.80f, 2.4f + 1.6f * (wBurn * burnBass + (1.0f - wBurn) * vintBass) - 0.4f * hot);
        // Bite tracks gain too (more presence on crank, less at low gain).
        speakerBite.setPeaking(sampleRate, 2600.0f + 500.0f * burnTre, 0.72f, 2.5f + 2.4f * burnTre + 0.8f * wBurn + 1.3f * hot);
        // AIR high-shelf: lifts the V30 top. The AmpliTube SuperSonic gets
        // BRIGHTER with gain (the high-gain ref is the brightest of the set), so
        // the air now RISES strongly with `hot` and CUTS the top at low gain.
        speakerFizz.setHighShelf(sampleRate, 4700.0f, 0.70f, -6.7f + 2.0f * burnTre + 10.0f * hot);
        // Speaker LP: opens with gain to track the reference's brightness trend
        // (dark ~6.5k at low gain -> ~14k miked V30 on crank).
        speakerLp.setLowPass(sampleRate, 1000.0f + 3000.0f * burnTre + 11500.0f * hot, 0.64f);
    }

public:
    void reset() {
        inputHp.reset(); inputBright.reset();
        vintTone.reset();
        burnTighten.reset(); interstageLp.reset();
        burnTone.reset();
        presence.reset();
        speakerHp.reset(); speakerBody.reset(); speakerBite.reset(); speakerFizz.reset(); speakerLp.reset();
        dcBlock.reset(); spring.clear(); sag = 0.0f;
        vintMiller.reset(); burn1Miller.reset(); burn2Miller.reset(); burn3Miller.reset();
        burnCouple12.reset(); burnCouple23.reset(); coupleToPi.reset();
        phaseInverter.reset(); supply.reset();
        vintTube.reset(); burn1.reset(); burn2.reset(); burn3.reset(); power.reset();
        lastPowerLoad = lastScreenLoad = lastPreampLoad = 0.0f;
        updateFilters();
    }
    void setSampleRate(float sr) { sampleRate = sr > 1000.0f ? sr : 48000.0f; spring.setSampleRate(sampleRate); reset(); }

    void setVintVol(float v)   { vintVol = clamp01(v);   updateFilters(); }
    void setVintTreble(float v){ vintTre = clamp01(v);   updateFilters(); }
    void setVintBass(float v)  { vintBass = clamp01(v);  updateFilters(); }
    void setNormFat(float v)   { normFat = clamp01(v);   updateFilters(); }
    void setChannel(float v)   { channel = clamp01(v);   updateFilters(); }
    void setGain1(float v)     { gain1 = clamp01(v);     updateFilters(); }
    void setGain2(float v)     { gain2 = clamp01(v);     updateFilters(); }
    void setBurnTreble(float v){ burnTre = clamp01(v);   updateFilters(); }
    void setBurnBass(float v)  { burnBass = clamp01(v);  updateFilters(); }
    void setBurnMid(float v)   { burnMid = clamp01(v);   updateFilters(); }
    void setBurnVol(float v)   { burnVol = clamp01(v);   updateFilters(); }
    void setReverb(float v)    { reverb = clamp01(v); }
    void setPresence(float v)  { presenceK = clamp01(v); updateFilters(); }
    void setCabSim(float v)    { cabSim = clamp01(v); }

    float process(float in) {
        const float wBurn = smoothstep(channel);     // crossfade weight
        const float wVint = 1.0f - wBurn;
        const float hot = smoothstepRange(0.45f, 1.0f, channel) * smoothstep(gain1Pot);
        const rbtube::SupplyScales bplus = supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);
        float x = inputHp.process(in);
        x = inputBright.process(x);
        x = softClip(x * 1.04f) * 0.96f;

        // --- VINTAGE channel: one REAL 12AX7 (clean until pushed by Volume) ---
        float vt = vintTube.process(vintMiller.process(x) * vDrv * bplus.preamp);
        vt = vintTone.process(vt) * 2.2f;                         // real blackface tone (+ makeup)
        vt *= (0.55f + 0.90f * vintVolPot);

        // --- BURN channel: cascaded gain stages ---
        // Drive is mostly GAIN-knob dependent (steep clean->crunch range) with a
        // small fixed Burn-channel floor, so low gain stays clean and the knob
        // sweeps a wide range like the AmpliTube reference.
        // --- BURN channel: 3 REAL cascaded 12AX7 (Gain1 -> Gain2 -> recovery) ---
        float bn = burnTighten.process(x);
        bn = burn1.process(burn1Miller.process(bn) * b1g * bplus.preamp);  // stage 1 (Gain 1)
        bn = interstageLp.process(bn);
        bn = burnCouple12.process(bn, b2g);
        bn = burn2.process(burn2Miller.process(bn) * bplus.preamp);  // stage 2 (Gain 2)
        bn = burnCouple23.process(bn, b3g);
        bn = burn3.process(burn3Miller.process(bn) * bplus.preamp);  // stage 3 (lead recovery)
        bn = burnTone.process(bn) * 4.0f;                         // real hot-rod TMB (+ makeup for insertion loss + scoop)
        bn *= (0.40f + 0.95f * burnVolPot);

        float y = vt * wVint + bn * wBurn;

        y = coupleToPi.process(y, 1.0f + 0.70f * wBurn + 1.15f * hot);
        lastPreampLoad = std::fabs(y) * (0.18f + 0.52f * wBurn + 0.70f * hot);
        y = phaseInverter.process(y * bplus.screen);              // 12AT7 LTP
        lastScreenLoad = std::fabs(y) * (0.25f + 0.55f * wBurn + 0.45f * hot);

        // --- 2x 6V6 push-pull (REAL circuit: pentode table + B+ + OT) ---
        y = power.process(y * bplus.power * bplus.screen);
        lastPowerLoad = std::fabs(y) * (0.42f + 0.55f * wBurn + 0.60f * hot);
        y = presence.process(y);            // power-amp presence (NFB-approx high shelf)

        y = dcBlock.process(y);

        // --- Celestion V30 fallback cab (bypassable for external cab/IR) ---
        const float ampOnly = y;
        float cab = speakerHp.process(ampOnly);
        cab = speakerBody.process(cab);
        cab = speakerBite.process(cab);
        cab = speakerFizz.process(cab);
        cab = speakerLp.process(cab);
        y = ampOnly + cabSim * (cab - ampOnly);

        // --- shared spring reverb (parallel, mixed) ---
        if (reverb > 0.001f)
            y += spring.process(y) * (0.9f * reverb);

        // --- output makeup: flat loudness at the BOX AC30 reference (~0.40) as
        //     the channel morphs clean -> cranked ---
        const float toneEnergy = 1.0f
            + 0.013f * std::fabs((burnBass - 0.5f) * 20.0f)
            + 0.012f * std::fabs((burnMid - 0.5f) * 22.0f)
            + 0.014f * std::fabs((burnTre - 0.5f) * 22.0f);
        const float makeup = 0.47f + 1.10f / (1.0f + std::exp(11.0f * (channel - 0.30f)));
        const float level = makeup / toneEnergy;
        // loudness flattening vs the Vintage->Burn morph/gain (clean post-output makeup;
        // ~0 dB at morph 0.5).
        float gcDb = 8.317f - 20.537f * channel + 5.580f * channel * channel;
        if (gcDb > 20.0f) gcDb = 20.0f; else if (gcDb < -12.0f) gcDb = -12.0f;
        return softClip(y * level * std::pow(10.0f, 0.05f * gcDb)) * 0.98f;
    }
};

} // namespace tw22

#endif // TW22_CORE_H
