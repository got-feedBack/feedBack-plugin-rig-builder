/* BassFilterEcho UI — copyright-free vintage tape-echo look: dark maroon box,
 * 4 knobs in a row (Time/Feedback/Mix/Filter), gold wordmark. */
#include "BassFilterEchoParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFilterEchoUI : public PedalKitUI {
    static const int GR=212,GG=176,GB=104;       // gold
public:
    BassFilterEchoUI() : PedalKitUI(320, 470, kParamCount, kBassFilterEchoDef) {
        names_ = kBassFilterEchoNames; labelFont_ = fBarlow;
        labelClr = Color(GR,GG,GB); pointerClr = Color(236,226,210); tickClr = Color(150,116,72);
        addKnob(kTime,     0.20f, 0.27f, 0.082f, 36,26,24, 0);
        addKnob(kFeedback, 0.40f, 0.27f, 0.082f, 36,26,24, 0);
        addKnob(kMix,      0.60f, 0.27f, 0.082f, 36,26,24, 0);
        addKnob(kFilter,   0.80f, 0.27f, 0.082f, 36,26,24, 0);
    }
protected:
    void drawFace() override {
        enclosure(96, 58, 42);                   // dark maroon
        const float f = sc();
        accentLine(W()*0.10f,H()*0.085f,W()*0.90f,H()*0.085f,Color(GR,GG,GB),1.4f);
        accentLine(W()*0.10f,H()*0.93f,W()*0.90f,H()*0.93f,Color(GR,GG,GB),1.4f);
        title("ECHO", Color(GR,GG,GB), 0.60f, 46, fBebas);
        textSpaced(0.5f,0.68f,10,Color(168,140,96),"TAPE  ECHO",fBarlow,2.0f);
        ledDot(W()*0.5f,H()*0.77f,5*f,true,210,70,58);
        footswitchRound(W()*0.5f,H()*0.88f,23*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFilterEchoUI)
};
UI* createUI(){ return new BassFilterEchoUI(); }
END_NAMESPACE_DISTRHO
