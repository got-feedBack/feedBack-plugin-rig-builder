/* Deluxe Servant UI - three Electric Mistress controls plus the physical
 * Filter Matrix switch. Matrix remains a parameter, but is not a fourth knob. */
#include "VintageFlangerParams.h"
#include "../_shared/pedalkit.hpp"

START_NAMESPACE_DISTRHO

class VintageFlangerUI : public PedalKitUI
{
public:
    VintageFlangerUI()
        : PedalKitUI(340, 450, kParamCount, kVintageFlangerDef)
    {
        names_ = kVintageFlangerNames;
        labelFont_ = fBarlow;
        labelClr = Color(224, 229, 238);
        pointerClr = Color(238, 240, 244);
        tickClr = Color(142, 151, 170);
        addKnob(kRate, 0.24f, 0.24f, 0.088f, 42, 45, 52, 0);
        addKnob(kRange, 0.50f, 0.24f, 0.088f, 42, 45, 52, 0);
        addKnob(kColor, 0.76f, 0.24f, 0.088f, 42, 45, 52, 0);
        addToggle(kMatrix, 0.50f, 0.47f, 0.046f, 2);
    }

protected:
    void drawFace() override
    {
        enclosure(68, 80, 105);
        const float f = sc();
        title("DELUXE SERVANT", Color(236, 240, 247), 0.64f, 29, fAnton);
        textSpaced(0.50f, 0.70f, 9.5f, Color(185, 194, 211),
            "ANALOG  FLANGER", fBarlow, 1.5f);
        ledDot(W() * 0.50f, H() * 0.78f, 5.0f * f, true, 220, 70, 58);
        footswitchRound(W() * 0.50f, H() * 0.88f, 23.0f * f);
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageFlangerUI)
};

UI* createUI()
{
    return new VintageFlangerUI();
}

END_NAMESPACE_DISTRHO
