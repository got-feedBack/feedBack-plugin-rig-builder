/* BassOverdrive UI — copyright-free recreation of the modern CMOS bass overdrive
 * it models (faithful to the real v2 look): all-black enclosure, white silkscreen,
 * real Blend/Drive/Level knobs plus Attack/Grunt switches, clean modern sans,
 * "CMOS BASS OVERDRIVE" wordmark at the bottom. No brand/model name. */
#include "BassOverdriveParams.h"
#include "../_shared/pedalkit.hpp"

START_NAMESPACE_DISTRHO

class BassOverdriveUI : public PedalKitUI {
    static const int WR = 235, WG = 236, WB = 239;   // white silkscreen
public:
    BassOverdriveUI() : PedalKitUI(300, 490, kParamCount, kBassOverdriveDef) {
        names_ = kBassOverdriveNames;
        knobLabels_ = false;                 // labels drawn above each knob
        labelClr   = Color(WR, WG, WB);
        tickClr    = Color(150, 151, 154);
        pointerClr = Color(238, 239, 242);
        addKnob(kBlend, 0.24f, 0.31f, 0.085f, 32, 32, 34, 3);
        addKnob(kDrive, 0.50f, 0.31f, 0.085f, 32, 32, 34, 3);
        addKnob(kLevel, 0.76f, 0.31f, 0.085f, 32, 32, 34, 3);
        addToggle(kAttack, 0.35f, 0.58f, 0.045f, 3);
        addToggle(kGrunt,  0.65f, 0.58f, 0.045f, 3);
    }
protected:
    void drawFace() override {
        enclosure(20, 20, 22);                       // all black
        const float f = sc();
        const Color white(WR, WG, WB), dim(150, 151, 154);
        textSpaced(0.24f, 0.195f, 10.5f, white, "BLEND", fBarlow, 1.2f);
        textSpaced(0.50f, 0.195f, 10.5f, white, "DRIVE", fBarlow, 1.2f);
        textSpaced(0.76f, 0.195f, 10.5f, white, "LEVEL", fBarlow, 1.2f);
        textSpaced(0.35f, 0.455f, 10.0f, white, "ATTACK", fBarlow, 1.2f);
        textSpaced(0.65f, 0.455f, 10.0f, white, "GRUNT", fBarlow, 1.2f);
        // bottom wordmark (generic) — clean modern sans
        textSpaced(0.5f, 0.80f, 17, white, "OVERDRIVE", fBarlow, 2.2f);
        textSpaced(0.5f, 0.845f, 8.5f, dim, "CMOS  BASS  OVERDRIVE", fBarlow, 1.6f);
        // LED + footswitch
        ledDot(W()*0.5f, H()*0.885f, 4.5f*f, true, 196, 72, 60);
        footswitchRound(W()*0.5f, H()*0.95f, 17*f);
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassOverdriveUI)
};

UI* createUI() { return new BassOverdriveUI(); }

END_NAMESPACE_DISTRHO
