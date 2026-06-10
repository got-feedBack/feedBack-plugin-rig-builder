/*
 * Studio Comp — dbx 160 model (true-RMS feed-forward VCA compressor), DPF VST3.
 *
 * dbx 160 topology (per the service-manual schematic): a dbx 202 true-RMS
 * level detector feeds a dbx 202 log-domain VCA, feed-forward. Modeled here as:
 *   1. true-RMS detector  — one-pole average of x^2, level in dB = 10*log10(ms)
 *   2. gain computer (dB)  — OverEasy-style soft knee, Threshold + Ratio
 *   3. ballistics          — attack/release smoothing of the gain reduction
 *   4. log-domain VCA      — gain = 10^((makeup - GR)/20)
 * Detector is stereo-LINKED (averaged mean-square) so the image stays put.
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
    double fMsCleanS;  // slow-smoothed input mean-square (closed-loop makeup)
    double fMsWetS;    // slow-smoothed post-reduction mean-square
    // derived coefficients
    float  fThrDb, fRatio, fMakeupDb;
    float  fRmsCoef, fAttCoef, fRelCoef, fSlowCoef;

    static float coefFor(float timeMs, float fs) {
        const float t = timeMs * 0.001f;
        if (t <= 0.0f) return 1.0f;                 // instant
        return 1.0f - std::exp(-1.0f / (t * fs));
    }
    void recalc() {
        fThrDb  = scThresholdDb(fParams[cThreshold]);
        fRatio  = scRatio(fParams[cRatio]);
        // Closed-loop auto make-up matches output loudness to input below, so
        // fMakeupDb is now JUST the user Output-knob trim on top (no open-loop
        // "recover half the reduction" term that made level jump with
        // Threshold/Ratio).
        fMakeupDb = scOutputDb(fParams[cOutput]);
        fRmsCoef = coefFor(SC_RMS_TIME * 1000.0f, fs);
        fAttCoef = coefFor(scAttackMs(fParams[cAttack]), fs);
        fRelCoef = coefFor(scReleaseMs(fParams[cRelease]), fs);
        fSlowCoef = coefFor(250.0f, fs);   // ~250 ms loudness window
    }
    // OverEasy soft-knee gain computer -> gain reduction in dB (>= 0).
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
        fParams[cOutput]    = 0.3333f;  // 0 dB
        fs = (float)getSampleRate(); if (fs <= 0.f) fs = 48000.f;
        fMs = 0.0; fGrDb = 0.0; fMsCleanS = 0.0; fMsWetS = 0.0;
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
        p.hints = kParameterIsAutomatable;
        p.name = kCompNames[i]; p.symbol = kCompNames[i];
        p.ranges.min = 0.0f; p.ranges.max = 1.0f;
        p.ranges.def = fParams[i];
    }
    float getParameterValue(uint32_t i) const override { return (i < (uint32_t)cNumParams) ? fParams[i] : 0.5f; }
    void  setParameterValue(uint32_t i, float v) override { if (i < (uint32_t)cNumParams) { fParams[i] = v; recalc(); } }
    void  sampleRateChanged(double r) override { fs = (float)r; if (fs <= 0.f) fs = 48000.f; recalc(); }

    void run(const float** in, float** out, uint32_t frames) override {
        const float* iL = in[0]; const float* iR = in[1];
        float* oL = out[0]; float* oR = out[1];
        const float outLin = std::pow(10.0f, fMakeupDb / 20.0f);   // Output-knob trim
        const float mkHi = std::pow(10.0f, 18.0f / 20.0f);
        const float mkLo = std::pow(10.0f, -12.0f / 20.0f);
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = iL[i], r = iR[i];
            // true-RMS detection (linked)
            const double sq = 0.5 * ((double)l * l + (double)r * r);
            fMs += (double)fRmsCoef * (sq - fMs);
            const float L = 10.0f * std::log10((float)fMs + 1e-12f);   // dBFS-RMS
            // gain computer -> target gain reduction (dB)
            const float target = grForLevel(L);
            // attack when GR increasing, release when decreasing
            const double coef = (target > fGrDb) ? fAttCoef : fRelCoef;
            fGrDb += coef * (target - fGrDb);
            // log-domain VCA (reduction only — makeup is closed-loop below)
            const float g = std::pow(10.0f, (float)(-fGrDb) / 20.0f);
            const float lw = l * g, rw = r * g;
            // Closed-loop auto make-up: match the post-reduction signal's
            // smoothed RMS to the input's so OUTPUT loudness stays constant as
            // Threshold/Ratio change instead of jumping. Range-limited.
            fMsCleanS += (double)fSlowCoef * (sq - fMsCleanS);
            fMsWetS   += (double)fSlowCoef * (0.5 * ((double)lw * lw + (double)rw * rw) - fMsWetS);
            float mk = 1.0f;
            if (fMsWetS > 1e-12 && fMsCleanS > 1e-12)
                mk = (float)std::sqrt(fMsCleanS / fMsWetS);
            mk = std::fmin(mkHi, std::fmax(mkLo, mk));
            oL[i] = lw * mk * outLin;
            oR[i] = rw * mk * outLin;
        }
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioCompPlugin)
};

Plugin* createPlugin() { return new StudioCompPlugin(); }

END_NAMESPACE_DISTRHO
