/* BassFilterDelay UI — copyright-free recreation of the classic analog BBD delay
 * compact it models: Chief-style enclosure, 4 knobs (RS count). No name. */
#include "BassFilterDelayParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFilterDelayUI : public PedalKitUI {
public:
    BassFilterDelayUI() : PedalKitUI(300, 480, kParamCount, kBassFilterDelayDef) {
        names_ = kBassFilterDelayNames; knobLabels_ = false;
        labelClr = Color(240,236,236); pointerClr = Color(240,236,236); tickClr = Color(228,222,222);
        addKnob(kTime,     0.205f, 0.235f, 0.072f, 24,22,23, 1);
        addKnob(kFeedback, 0.400f, 0.235f, 0.072f, 24,22,23, 1);
        addKnob(kMix,      0.595f, 0.235f, 0.072f, 24,22,23, 1);
        addKnob(kFilter,   0.790f, 0.235f, 0.072f, 24,22,23, 1);
    }
protected:
    void drawFace() override {
        chiefPedal(156, 64, 72);                  // dusty rose / dark salmon body
        const Color w(240,236,236);
        textSpaced(0.205f,0.135f,8.0f,w,"TIME",fBarlow, 0.2f);
        textSpaced(0.400f,0.135f,7.5f,w,"FEEDBACK",fBarlow, 0.2f);
        textSpaced(0.595f,0.135f,8.0f,w,"MIX",fBarlow, 0.2f);
        textSpaced(0.790f,0.135f,8.0f,w,"FILTER",fBarlow, 0.2f);
        embossText(0.31f, 0.565f, 50, "Bass", fSerif);
        embossText(0.63f, 0.645f, 50, "Delay", fSerif);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFilterDelayUI)
};
UI* createUI() { return new BassFilterDelayUI(); }
END_NAMESPACE_DISTRHO
