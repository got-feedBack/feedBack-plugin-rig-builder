/* BassFuzz UI — copyright-free recreation of the classic large-box bass fuzz it
 * models: black enclosure, 3 big skirted (Davies-style) knobs in a top row (the
 * RS count), green + white silkscreen, big wordmark. No brand/model name. */
#include "BassFuzzParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFuzzUI : public PedalKitUI {
    static const int GR = 126, GG = 196, GB = 96;   // green accent
public:
    BassFuzzUI() : PedalKitUI(340, 470, kParamCount, kBassFuzzDef) {
        names_ = kBassFuzzNames; knobLabels_ = true;
        labelClr = Color(236,238,238); pointerClr = Color(238,239,242); tickClr = Color(150,150,152);
        addKnob(kGain,   0.22f, 0.26f, 0.105f, 24,24,26, 2);   // Davies skirted
        addKnob(kTone,   0.50f, 0.26f, 0.105f, 24,24,26, 2);
        addKnob(kFilter, 0.78f, 0.26f, 0.105f, 24,24,26, 2);
    }
protected:
    void drawFace() override {
        enclosure(22, 22, 24);                       // black box
        const float f = sc();
        // green hairlines top & bottom (silkscreen)
        accentLine(W()*0.10f, H()*0.075f, W()*0.90f, H()*0.075f, Color(GR,GG,GB), 1.5f);
        accentLine(W()*0.10f, H()*0.93f,  W()*0.90f, H()*0.93f,  Color(GR,GG,GB), 1.5f);
        // big wordmark (generic) + subtitle
        title("FUZZ", Color(GR, GG, GB), 0.58f, 56, fAnton);
        textSpaced(0.5f, 0.665f, 11, Color(220,222,222), "BASS  FUZZ", fBarlow, 2.4f);
        // LED + footswitch
        ledDot(W()*0.5f, H()*0.755f, 5*f, true, 210, 70, 58);
        footswitchRound(W()*0.5f, H()*0.86f, 24*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzUI)
};
UI* createUI() { return new BassFuzzUI(); }
END_NAMESPACE_DISTRHO
