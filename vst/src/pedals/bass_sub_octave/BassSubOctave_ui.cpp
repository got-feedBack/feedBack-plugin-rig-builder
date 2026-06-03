/* BassSubOctave UI — copyright-free recreation of the classic brown analog
 * octaver compact it models: Chief-style enclosure, 2 knobs (the RS count). No name. */
#include "BassSubOctaveParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassSubOctaveUI : public PedalKitUI {
public:
    BassSubOctaveUI() : PedalKitUI(300, 480, kParamCount, kBassSubOctaveDef) {
        names_ = kBassSubOctaveNames; knobLabels_ = false;
        labelClr = Color(236,232,224); pointerClr = Color(236,232,224); tickClr = Color(222,216,206);
        addKnob(kMix,  0.34f, 0.235f, 0.088f, 24,22,22, 1);
        addKnob(kTone, 0.66f, 0.235f, 0.088f, 24,22,22, 1);
    }
protected:
    void drawFace() override {
        chiefPedal(112, 70, 66);                  // chocolate brown body
        const Color w(236,232,224);
        textSpaced(0.34f,0.12f,9,w,"MIX",fBarlow, 0.2f);
        textSpaced(0.66f,0.12f,9,w,"TONE",fBarlow, 0.2f);
        embossText(0.31f, 0.565f, 50, "Sub", fSerif);
        embossText(0.60f, 0.645f, 46, "Octave", fSerif);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassSubOctaveUI)
};
UI* createUI() { return new BassSubOctaveUI(); }
END_NAMESPACE_DISTRHO
