/* BassChorus UI — copyright-free recreation of the classic teal bass-chorus
 * compact pedal it models: Chief-style enclosure (coloured body, name strip,
 * big treadle footswitch), 4 knobs (the RS count) in a row. No brand/model name. */
#include "BassChorusParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassChorusUI : public PedalKitUI {
public:
    BassChorusUI() : PedalKitUI(300, 480, kParamCount, kBassChorusDef) {
        names_ = kBassChorusNames; knobLabels_ = false;
        labelClr = Color(238,240,242); pointerClr = Color(238,240,242); tickClr = Color(225,228,230);
        addKnob(kRate,     0.205f, 0.235f, 0.072f, 24,24,26, 1);
        addKnob(kDepth,    0.400f, 0.235f, 0.072f, 24,24,26, 1);
        addKnob(kLoFilter, 0.595f, 0.235f, 0.072f, 24,24,26, 1);
        addKnob(kMix,      0.790f, 0.235f, 0.072f, 24,24,26, 1);
    }
protected:
    void drawFace() override {
        chiefPedal(40, 158, 150);                 // teal body
        const Color w(238,240,242);
        textSpaced(0.205f,0.135f,8.5f,w,"RATE",fBarlow, 0.2f);
        textSpaced(0.400f,0.135f,8.5f,w,"DEPTH",fBarlow, 0.2f);
        textSpaced(0.595f,0.135f,8.0f,w,"LO FILTER",fBarlow, 0.2f);
        textSpaced(0.790f,0.135f,8.5f,w,"MIX",fBarlow, 0.2f);
        embossText(0.31f, 0.565f, 50, "Bass", fSerif);
        embossText(0.63f, 0.645f, 50, "Chorus", fSerif);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassChorusUI)
};
UI* createUI() { return new BassChorusUI(); }
END_NAMESPACE_DISTRHO
