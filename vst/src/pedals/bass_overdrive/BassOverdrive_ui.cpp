/* BassOverdrive UI — copyright-free recreation of the modern CMOS bass overdrive
 * it models (faithful to the real v2 look): all-black enclosure, white silkscreen,
 * 4 knobs in a 2x2 grid with the label above each (the RS knob count), clean
 * modern sans, "CMOS BASS OVERDRIVE" wordmark at the bottom. No brand/model name. */
#include "BassOverdriveParams.h"
#include "../_shared/pedalkit.hpp"

START_NAMESPACE_DISTRHO

class BassOverdriveUI : public PedalKitUI {
    static const int WR = 235, WG = 236, WB = 239;   // white silkscreen
public:
    BassOverdriveUI() : PedalKitUI(300, 490, kParamCount, kBassOverdriveDef) {
        names_ = kBassOverdriveNames;
        knobLabels_ = false;                 // labels drawn above each knob
        labelClr   = Color(WR, WG, WB);
        tickClr    = Color(150, 151, 154);
        pointerClr = Color(238, 239, 242);
        // 2x2 black knobs (Blend TL, Tone TR, Gain BL, Filter BR) — RS's 4 knobs
        addKnob(kBlend,  0.30f, 0.31f, 0.10f, 32, 32, 34, 3);
        addKnob(kTone,   0.70f, 0.31f, 0.10f, 32, 32, 34, 3);
        addKnob(kGain,   0.30f, 0.62f, 0.10f, 32, 32, 34, 3);
        addKnob(kFilter, 0.70f, 0.62f, 0.10f, 32, 32, 34, 3);
    }
protected:
    void drawFace() override {
        enclosure(20, 20, 22);                       // all black
        const float f = sc();
        const Color white(WR, WG, WB), dim(150, 151, 154);
        // knob labels above each knob (the RS names)
        textSpaced(0.30f, 0.185f, 10.5f, white, "BLEND",  fBarlow, 1.2f);
        textSpaced(0.70f, 0.185f, 10.5f, white, "TONE",   fBarlow, 1.2f);
        textSpaced(0.30f, 0.495f, 10.5f, white, "GAIN",   fBarlow, 1.2f);
        textSpaced(0.70f, 0.495f, 10.5f, white, "FILTER", fBarlow, 1.2f);
        // bottom wordmark (generic) — clean modern sans
        textSpaced(0.5f, 0.80f, 17, white, "OVERDRIVE", fBarlow, 2.2f);
        textSpaced(0.5f, 0.845f, 8.5f, dim, "CMOS  BASS  OVERDRIVE", fBarlow, 1.6f);
        // LED + footswitch
        ledDot(W()*0.5f, H()*0.885f, 4.5f*f, true, 196, 72, 60);
        footswitchRound(W()*0.5f, H()*0.95f, 17*f);
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassOverdriveUI)
};

UI* createUI() { return new BassOverdriveUI(); }

END_NAMESPACE_DISTRHO
