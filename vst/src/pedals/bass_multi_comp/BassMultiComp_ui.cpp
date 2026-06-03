/* BassMultiComp UI — copyright-free chrome compressor look (RS 'MB Comp'):
 * brushed-silver box, two big knobs (Compress, Rate) + a small Filter knob,
 * dark lettering, heavy wordmark. 3 knobs = RS count. */
#include "BassMultiCompParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassMultiCompUI : public PedalKitUI {
public:
    BassMultiCompUI() : PedalKitUI(320, 470, kParamCount, kBassMultiCompDef) {
        names_ = kBassMultiCompNames; labelFont_ = fBarlow;
        labelClr = Color(40,42,46); pointerClr = Color(30,32,36); tickClr = Color(96,98,104);
        addKnob(kCompress, 0.30f, 0.26f, 0.105f, 70,72,78, 0);
        addKnob(kRate,     0.70f, 0.26f, 0.105f, 70,72,78, 0);
        addKnob(kFilter,   0.50f, 0.42f, 0.072f, 70,72,78, 0);
    }
protected:
    void drawFace() override {
        enclosure(150, 152, 158);                // brushed silver
        const float f = sc();
        title("COMP", Color(36,38,42), 0.63f, 46, fAnton);
        textSpaced(0.5f,0.71f,10,Color(80,82,88),"MULTI  COMPRESSOR",fBarlow,1.4f);
        ledDot(W()*0.5f,H()*0.79f,5*f,true,210,70,58);
        footswitchRound(W()*0.5f,H()*0.88f,23*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassMultiCompUI)
};
UI* createUI(){ return new BassMultiCompUI(); }
END_NAMESPACE_DISTRHO
