/* BassEnbig UI — copyright-free 'Enbiggenator' (fictional RS pedal): dark
 * blue-grey box, cyan accents, 4 knobs (Rate/Depth/Mix/Filter), sci-fi wordmark. */
#include "BassEnbigParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassEnbigUI : public PedalKitUI {
    static const int CR=110,CG=210,CB=224;       // cyan
public:
    BassEnbigUI() : PedalKitUI(320, 470, kParamCount, kBassEnbigDef) {
        names_ = kBassEnbigNames; labelFont_ = fBarlow;
        labelClr = Color(206,224,228); pointerClr = Color(150,225,235); tickClr = Color(70,120,128);
        addKnob(kRate,   0.20f, 0.27f, 0.082f, 40,44,50, 0);
        addKnob(kDepth,  0.40f, 0.27f, 0.082f, 40,44,50, 0);
        addKnob(kMix,    0.60f, 0.27f, 0.082f, 40,44,50, 0);
        addKnob(kFilter, 0.80f, 0.27f, 0.082f, 40,44,50, 0);
    }
protected:
    void drawFace() override {
        enclosure(58, 64, 72);                   // blue-grey
        const float f = sc();
        accentLine(W()*0.10f,H()*0.085f,W()*0.90f,H()*0.085f,Color(CR,CG,CB),1.4f);
        title("ENBIGGEN", Color(CR,CG,CB), 0.585f, 30, fBebas);
        textSpaced(0.5f,0.665f,9.5f,Color(120,160,168),"MOD  FILTER",fBarlow,2.0f);
        ledDot(W()*0.5f,H()*0.77f,5*f,true,90,210,224);
        footswitchRound(W()*0.5f,H()*0.88f,23*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEnbigUI)
};
UI* createUI(){ return new BassEnbigUI(); }
END_NAMESPACE_DISTRHO
