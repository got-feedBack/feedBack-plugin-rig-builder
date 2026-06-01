/* BassDistortion UI — detailed copyright-free recreation of the classic black
 * hard-clipping distortion it models (faithful to the real layout): all-black
 * enclosure, white knob labels each boxed in a thin rectangle above its knob,
 * black knobs with a white tick fan + white pointer, and a big boxed wordmark in
 * the centre. Heavy display font for the wordmark. No brand/model name. */
#include "BassDistortionParams.h"
#include "../_shared/pedalkit.hpp"

START_NAMESPACE_DISTRHO

class BassDistortionUI : public PedalKitUI {
    static const int WR = 238, WG = 239, WB = 242;   // white silkscreen
public:
    BassDistortionUI() : PedalKitUI(320, 500, kParamCount, kBassDistortionDef) {
        names_ = kBassDistortionNames;
        knobLabels_ = false;                 // labels are boxed above each knob
        labelClr   = Color(WR, WG, WB);
        tickClr    = Color(232, 233, 236);   // white tick fan
        pointerClr = Color(240, 241, 244);
        // three black knobs in a top row
        addKnob(kGain,   0.215f, 0.305f, 0.105f, 26, 26, 28);
        addKnob(kTone,   0.500f, 0.305f, 0.105f, 26, 26, 28);
        addKnob(kFilter, 0.785f, 0.305f, 0.105f, 26, 26, 28);
    }
protected:
    void drawFace() override {
        enclosure(18, 18, 20);                       // all black
        const float f = sc();
        const Color white(WR, WG, WB);
        // boxed knob labels above each knob
        boxedLabel(0.215f, 0.135f, 0.115f, 0.028f, "GAIN",   12.5f, white, white, fBarlow);
        boxedLabel(0.500f, 0.135f, 0.110f, 0.028f, "TONE",   12.5f, white, white, fBarlow);
        boxedLabel(0.785f, 0.135f, 0.125f, 0.028f, "FILTER", 12.5f, white, white, fBarlow);
        // big boxed wordmark (generic — no brand) in a heavy display font
        boxedLabel(0.5f, 0.55f, 0.34f, 0.075f, "DISTORTION", 44, white, white, fAnton);
        // LED + chrome footswitch
        ledDot(W()*0.5f, H()*0.71f, 5*f, true, 210, 70, 58);
        footswitchRound(W()*0.5f, H()*0.83f, 24*f);
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassDistortionUI)
};

UI* createUI() { return new BassDistortionUI(); }

END_NAMESPACE_DISTRHO
