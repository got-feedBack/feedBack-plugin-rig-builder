/*
 * Studio Comp — dbx 160 model (true-RMS feed-forward VCA compressor), DPF VST3.
 *
 * Modeled from the dbx 160/161/162 Preliminary Technical Service Manual
 * (discrete-VCA "160 COMPLI", full schematic pp.18-19 + the hand-drawn VCA and
 * Control-Signal-Processor pages). Real signal flow:
 *
 *   Audio In ─► VCA (log-domain, gain set by control voltage) ─► Output ─► Out
 *                 ▲
 *   Audio In ─► true-RMS detector ─► DC mixer (Threshold) ─► Slope (ratio) ─► CV
 *
 * Faithful details:
 *   1. FEED-FORWARD true-RMS detector — one-pole average of x^2; level dB =
 *      10*log10(mean-square). True-RMS (not peak) detection is the dbx signature.
 *   2. gain computer (dB) — Threshold + Slope(ratio), NEAR-HARD knee. The
 *      original discrete 160 is hard-knee; its smoothness comes from the RMS
 *      detector, not a wide soft knee. (The 10 dB OverEasy knee is a later 160X
 *      feature — narrowed here for fidelity to this unit.)
 *   3. ballistics — fast program-dependent ATTACK; RELEASE at a CONSTANT RATE in
 *      dB/s (~120 dB/s nominal) — the 160's defining "linear-dB release", NOT an
 *      exponential time-constant. The real 160 has no attack/release knobs; the
 *      RS Attack/Release knobs scale these around the unit's native behaviour.
 *   4. log-domain VCA — gain = 10^(-GR/20). The aligned dbx VCA trims to <0.1%
 *      THD (per the manual), so it is modeled clean.
 * Detector is stereo-LINKED (averaged mean-square) — like a dbx "strapped" pair
 * sharing one control voltage, so the stereo image stays put.
 *
 * OUTPUT is a MANUAL post-VCA gain, exactly like the real 160 (no auto make-up /
 * leveling). Its DEFAULT is tuned (~+2.5 dB) so a calibrated input (−12 dBFS
 * peak) lands the output near −12 dBFS; raise/lower it to taste, and ride it up
 * as you add gain reduction — just like the hardware.
 */
#include "DistrhoPlugin.hpp"
#include "StudioCompParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

class StudioCompPlugin : public Plugin {
    float fParams[cNumParams];
    float fs;
    // detector + envelope state
    double fMs;        // mean-square (linked)
    double fGrDb;      // smoothed gain reduction (dB, >= 0)
    float  fGRout;     // gain reduction reported to the UI meter (output param, dB)
    // derived coefficients
    float  fThrDb, fRatio, fMakeupDb;
    float  fRmsCoef, fAttCoef, fRelDbPerSamp;

    static float coefFor(float timeMs, float fs) {
        const float t = timeMs * 0.001f;
        if (t <= 0.0f) return 1.0f;                 // instant
        return 1.0f - std::exp(-1.0f / (t * fs));
    }
    void recalc() {
        fThrDb  = scThresholdDb(fParams[cThreshold]);
        fRatio  = scRatio(fParams[cRatio]);
        // Output is a MANUAL post-VCA gain (like the real 160) — no auto make-up.
        // Its default is tuned so a calibrated input lands the output near −12 dBFS.
        fMakeupDb = scOutputDb(fParams[cOutput]);
        fRmsCoef = coefFor(SC_RMS_TIME * 1000.0f, fs);
        // ATTACK: exponential charge of the RMS detector, fast/program-dependent.
        fAttCoef = coefFor(scAttackMs(fParams[cAttack]), fs);
        // RELEASE: the dbx 160 recovers at a CONSTANT RATE in dB/s (linear-dB),
        // not an exponential tail. Nominal ~120 dB/s; the Release knob scales it
        // (longer Release time -> slower dB/s). 14.4 dB / 0.12 s = 120 dB/s.
        const float relS = scReleaseMs(fParams[cRelease]) * 0.001f;
        const float relDbPerSec = 14.4f / (relS > 1e-4f ? relS : 1e-4f);
        fRelDbPerSamp = relDbPerSec / fs;
    }
    // Near-hard-knee gain computer -> gain reduction in dB (>= 0). The narrow
    // knee (SC_KNEE_DB) just rounds the corner; the dbx smoothness is the RMS
    // detector, not the knee.
    inline float grForLevel(float L) const {
        const float over = L - fThrDb;
        const float W = SC_KNEE_DB;
        const float slope = 1.0f - 1.0f / fRatio;
        if (2.0f * over <= -W)      return 0.0f;
        if (2.0f * over >=  W)      return slope * over;
        const float x = over + 0.5f * W;            // knee region
        return slope * (x * x) / (2.0f * W);
    }
public:
    StudioCompPlugin() : Plugin(cNumParams, 0, 0) {
        fParams[cThreshold] = 0.5f;     // -20 dB
        fParams[cRatio]     = 0.1818f;  // ~3:1
        fParams[cAttack]    = 0.1333f;  // ~20 ms
        fParams[cRelease]   = 0.2083f;  // ~120 ms
        fParams[cOutput]    = 0.4028f;  // +2.5 dB — lands a calibrated input near -12 dBFS
        fParams[cGR] = 0.0f;
        fs = (float)getSampleRate(); if (fs <= 0.f) fs = 48000.f;
        fMs = 0.0; fGrDb = 0.0; fGRout = 0.0f;
        recalc();
    }
protected:
    const char* getLabel()       const override { return "StudioComp"; }
    const char* getDescription() const override { return "dbx 160 true-RMS feed-forward VCA compressor"; }
    const char* getMaker()       const override { return "RigBuilder"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('R', 'S', 'C', 'P'); }

    void initParameter(uint32_t i, Parameter& p) override {
        if (i >= (uint32_t)cNumParams) return;
        p.name = kCompNames[i]; p.symbol = kCompNames[i];
        if (i == cGR) {   // read-only meter: gain reduction in dB, feeds the UI VU
            p.hints = kParameterIsOutput;
            p.ranges.min = 0.0f; p.ranges.max = SC_GR_METER_MAX; p.ranges.def = 0.0f;
            return;
        }
        p.hints = kParameterIsAutomatable;
        p.ranges.min = 0.0f; p.ranges.max = 1.0f;
        p.ranges.def = kCompDef[i];
    }
    float getParameterValue(uint32_t i) const override {
        if (i == cGR) return fGRout;
        return (i < (uint32_t)cNumParams) ? fParams[i] : 0.5f;
    }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)cNumParams && i != (uint32_t)cGR) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { fs = (float)r; if (fs <= 0.f) fs = 48000.f; recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        const float outLin = std::pow(10.0f, fMakeupDb / 20.0f);   // manual Output gain
        float blockGR = 0.0f;                                       // peak gain reduction this block (UI meter)
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = iL[i], r = iR[i];
            // true-RMS detection (linked)
            const double sq = 0.5 * ((double)l * l + (double)r * r);
            fMs += (double)fRmsCoef * (sq - fMs);
            const float L = 10.0f * std::log10((float)fMs + 1e-12f);   // dBFS-RMS
            // gain computer -> target gain reduction (dB)
            const float target = grForLevel(L);
            // Ballistics: exponential ATTACK (detector charge) when GR rises;
            // constant-rate dB/s RELEASE when it falls (the dbx 160 hallmark).
            if (target > fGrDb)
                fGrDb += (double)fAttCoef * (target - fGrDb);
            else
                fGrDb = (target > fGrDb - fRelDbPerSamp) ? target : (fGrDb - fRelDbPerSamp);
            if ((float)fGrDb > blockGR) blockGR = (float)fGrDb;
            // log-domain VCA (reduction), then the manual Output gain.
            const float g = std::pow(10.0f, (float)(-fGrDb) / 20.0f);
            oL[i] = l * g * outLin;
            oR[i] = r * g * outLin;
        }
        fGRout = blockGR;   // report peak GR this block to the UI meter
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioCompPlugin)
};

Plugin* createPlugin() { return new StudioCompPlugin(); }

END_NAMESPACE_DISTRHO
