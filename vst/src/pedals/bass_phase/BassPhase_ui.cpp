/* BassPhase UI — copyright-free analog phaser look: copper box, 4 knobs in a row
 * (RS count: Rate/Depth/Mix/Filter), condensed wordmark. */
#include "BassPhaseParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassPhaseUI : public PedalKitUI {
public:
    BassPhaseUI() : PedalKitUI(320, 470, kParamCount, kBassPhaseDef) {
        names_ = kBassPhaseNames; labelFont_ = fBarlow;
        labelClr = Color(238,228,210); pointerClr = Color(240,232,216); tickClr = Color(150,128,104);
        addKnob(kRate,   0.20f, 0.27f, 0.082f, 40,34,28, 0);
        addKnob(kDepth,  0.40f, 0.27f, 0.082f, 40,34,28, 0);
        addKnob(kMix,    0.60f, 0.27f, 0.082f, 40,34,28, 0);
        addKnob(kFilter, 0.80f, 0.27f, 0.082f, 40,34,28, 0);
    }
protected:
    void drawFace() override {
        enclosure(124, 92, 68);                  // copper
        const float f = sc();
        title("PHASE", Color(244,236,220), 0.60f, 44, fBebas);
        textSpaced(0.5f,0.68f,10,Color(180,160,134),"BASS  PHASER",fBarlow,1.4f);
        ledDot(W()*0.5f,H()*0.77f,5*f,true,210,70,58);
        footswitchRound(W()*0.5f,H()*0.88f,23*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassPhaseUI)
};
UI* createUI(){ return new BassPhaseUI(); }
END_NAMESPACE_DISTRHO
