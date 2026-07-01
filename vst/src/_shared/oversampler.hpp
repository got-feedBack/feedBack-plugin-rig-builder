#pragma once
#include <cmath>
#include <cstring>

// Shared polyphase-FIR oversampler for the amps' nonlinear chains.
// NOTE: despite the class name, OS = 2 (2x). Always size buffers and set core
// sample rates from Oversampler4x::OS, never from a literal 4.
//
// Why: cascaded tanh/triode stages generate harmonics above Nyquist; at the base
// rate those fold back as aliasing — the "transistor / clinical / fizzy" character
// and the high-gain artifacts. Running the nonlinear core at OS x and band-limiting
// on the way in/out removes that. One instance PER AUDIO CHANNEL (holds filter
// state). Real-time safe after construction; not thread-safe.
//
// Usage:
//   os.upsample(x, buf);          // 1 base-rate sample -> OS oversampled samples
//   for (k=0;k<OS;k++) buf[k]=core.process(buf[k]);   // core was set to OS*sr
//   float y = os.downsample(buf); // OS samples -> 1 base-rate sample
namespace rbshared {

class Oversampler4x {
public:
    static constexpr int OS     = 2;           // 2x. (4x ran the cores at 192k, where
                                               // an amp filter goes unstable -> NaN/noise.)
    static constexpr int NTAPS  = 64;          // 32 taps per polyphase branch
    static constexpr int P      = NTAPS / OS;  // taps per phase

    Oversampler4x() { buildProto(); reset(); }

    void reset() {
        std::memset(upHist,   0, sizeof(upHist));
        std::memset(downHist, 0, sizeof(downHist));
        upPos = downPos = 0;
    }

    // One base-rate input sample -> OS samples at the oversampled rate.
    inline void upsample(float x, float* out) {
        upHist[upPos] = x;
        for (int p = 0; p < OS; ++p) {
            float acc = 0.0f;
            for (int k = 0; k < P; ++k)
                acc += proto[k * OS + p] * upHist[(upPos - k) & HMASK];
            out[p] = OS * acc;                 // restore zero-stuffing gain loss
        }
        upPos = (upPos + 1) & HMASK;
    }

    // OS oversampled samples -> one band-limited, decimated base-rate sample.
    inline float downsample(const float* in) {
        for (int j = 0; j < OS; ++j) {
            downHist[downPos] = in[j];
            downPos = (downPos + 1) & DMASK;
        }
        float acc = 0.0f;
        for (int n = 0; n < NTAPS; ++n)
            acc += proto[n] * downHist[(downPos - 1 - n) & DMASK];
        return acc;
    }

private:
    void buildProto() {
        // Windowed-sinc low-pass, cutoff = base Nyquist = 0.5/OS cycles/sample
        // (of the oversampled stream). Blackman window, normalized to unity DC.
        const double fc = 0.5 / OS;
        const int    M  = NTAPS - 1;
        const double PI = 3.14159265358979323846;
        double sum = 0.0;
        for (int n = 0; n < NTAPS; ++n) {
            double m = n - M / 2.0;
            double s = (std::fabs(m) < 1e-9) ? 2.0 * fc
                                             : std::sin(2.0 * PI * fc * m) / (PI * m);
            double w = 0.42 - 0.5 * std::cos(2.0 * PI * n / M)
                            + 0.08 * std::cos(4.0 * PI * n / M);
            proto[n] = (float)(s * w);
            sum += s * w;
        }
        for (int n = 0; n < NTAPS; ++n) proto[n] /= (float)sum;
    }

    static constexpr int HIST  = 32,  HMASK = HIST  - 1;   // >= P
    static constexpr int DHIST = 128, DMASK = DHIST - 1;   // >= NTAPS
    float proto[NTAPS];
    float upHist[HIST];
    float downHist[DHIST];
    int   upPos, downPos;
};

} // namespace rbshared
