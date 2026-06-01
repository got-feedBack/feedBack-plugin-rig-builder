/* BassFlanger UI — copyright-free recreation of the bass-flanger compact it
 * models: Boss-style enclosure, 4 knobs (RS count). No name. */
#include "BassFlangerParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFlangerUI : public PedalKitUI {
public:
    BassFlangerUI() : PedalKitUI(300, 480, kParamCount, kBassFlangerDef) {
        names_ = kBassFlangerNames; knobLabels_ = false;
        labelClr = Color(236,232,242); pointerClr = Color(236,232,242); tickClr = Color(222,216,232);
        addKnob(kRate,   0.205f, 0.235f, 0.072f, 26,24,30, 1);
        addKnob(kDepth,  0.400f, 0.235f, 0.072f, 26,24,30, 1);
        addKnob(kFilter, 0.595f, 0.235f, 0.072f, 26,24,30, 1);
        addKnob(kMix,    0.790f, 0.235f, 0.072f, 26,24,30, 1);
    }
protected:
    void drawFace() override {
        bossPedal(96, 80, 134);                  // muted purple body
        const Color w(236,232,242);
        textSpaced(0.205f,0.135f,8.5f,w,"RATE",fBarlow, 0.2f);
        textSpaced(0.400f,0.135f,8.5f,w,"DEPTH",fBarlow, 0.2f);
        textSpaced(0.595f,0.135f,8.5f,w,"FILTER",fBarlow, 0.2f);
        textSpaced(0.790f,0.135f,8.5f,w,"MIX",fBarlow, 0.2f);
        embossText(0.30f, 0.495f, 24, "BASS", fBarlow);
        embossText(0.585f, 0.60f, 24, "FLANGER", fBarlow);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFlangerUI)
};
UI* createUI() { return new BassFlangerUI(); }
END_NAMESPACE_DISTRHO
