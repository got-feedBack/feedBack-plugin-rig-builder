#ifndef RB_AUTOMAKEUP_HPP
#define RB_AUTOMAKEUP_HPP

#include <cmath>

/*
 * RBAutoMakeup — loudness-matched auto makeup-gain.
 *
 * Tracks the slow RMS of the dry input and of the wet (processed) output and
 * scales the wet so that RMS_out == RMS_in. This decouples a drive pedal's
 * Gain knob from its output level: Gain changes how hard the signal clips, NOT
 * how loud the pedal is, so every distortion / overdrive / fuzz sits at the
 * same level as the bypassed (dry) signal — and therefore at the same level as
 * each other.
 *
 * The RMS window is symmetric (~200 ms) so the loudness match is UNBIASED — at
 * any fixed Gain the output settles to exactly the dry level. That slow window
 * alone would leave a brief loud "blip" the instant the Gain knob is turned up
 * (the envelope needs ~200 ms to notice the louder output). To kill that blip
 * WITHOUT biasing the steady-state match, the plugin calls snap() whenever a
 * parameter changes: snap() opens a short window during which the envelopes
 * track with a fast (~8 ms) coefficient, so the makeup catches the new level
 * almost immediately while the knob is moving, then reverts to the accurate slow
 * window once the knob settles. Normal playing dynamics never trigger snap(), so
 * the makeup never acts like a compressor. Near silence the ratio is frozen so
 * the noise floor is never boosted.
 *
 * Usage (per channel):
 *     run():               outL[i] = makeupL.process(inL[i], core.process(inL[i]));
 *     setParameterValue(): makeupL.snap(); makeupR.snap();
 */
struct RBAutoMakeup
{
    float slowCoef = 0.0f;  // accurate one-pole coefficient (~200 ms)
    float fastCoef = 0.0f;  // fast coefficient used right after a knob change
    float inEnv    = 0.0f;  // mean-square of the dry signal
    float outEnv   = 0.0f;  // mean-square of the wet signal
    float gain     = 1.0f;  // makeup gain currently applied
    int   fast     = 0;     // samples remaining in the fast (snap) window
    int   fastLen  = 0;     // length of the fast window in samples

    void setSampleRate(float sr)
    {
        if (sr < 1000.0f)
            sr = 48000.0f;
        slowCoef = std::exp(-1.0f / (0.200f * sr));   // ~200 ms RMS window
        fastCoef = std::exp(-1.0f / (0.008f * sr));   // ~8 ms during a snap
        fastLen  = (int)(0.040f * sr);                // ~40 ms snap window
        reset();
    }

    void reset()
    {
        inEnv = outEnv = 0.0f;
        gain = 1.0f;
        fast = 0;
    }

    // Call when a parameter (Gain/Tone/…) changes so the makeup re-levels fast.
    void snap()
    {
        fast = fastLen;
    }

    float process(float dry, float wet)
    {
        const float c = (fast > 0) ? fastCoef : slowCoef;
        if (fast > 0)
            --fast;

        inEnv  = c * inEnv  + (1.0f - c) * dry * dry;
        outEnv = c * outEnv + (1.0f - c) * wet * wet;

        // Only chase a new target when there is real output AND input energy;
        // this freezes the ratio during silence so hiss is not amplified.
        if (outEnv > 1.0e-7f && inEnv > 1.0e-9f)
        {
            float target = std::sqrt(inEnv / outEnv);
            if (target > 8.0f)
                target = 8.0f;                   // safety ceiling
            gain = target;
        }

        return wet * gain;
    }
};

#endif // RB_AUTOMAKEUP_HPP
