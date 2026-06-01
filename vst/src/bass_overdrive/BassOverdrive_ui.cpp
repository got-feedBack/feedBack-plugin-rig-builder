/* BassOverdrive UI — detailed copyright-free recreation of a modern CMOS bass
 * overdrive (the kind it models): matte-black enclosure, lime-green silkscreen,
 * 2x2 metal knobs, round footswitch. No brand/model name. */
#include "BassOverdriveParams.h"
#include "../_shared/pedalkit.hpp"

START_NAMESPACE_DISTRHO

class BassOverdriveUI : public PedalKitUI {
    static const int LR = 158, LG = 198, LB = 66;   // muted lime accent
public:
    BassOverdriveUI() : PedalKitUI(300, 500, kParamCount, kBassOverdriveDef) {
        names_ = kBassOverdriveNames;
        labelClr = Color(LR, LG, LB);
        tickClr  = Color(108, 134, 58);
        pointerClr = Color(210, 206, 196);
        setWearSeed(0x10A1u);
        // 2x2 dark knobs (Blend, Gain top — Filter, Tone bottom)
        addKnob(kBlend,  0.31f, 0.35f, 0.105f, 52, 54, 58);
        addKnob(kGain,   0.69f, 0.35f, 0.105f, 52, 54, 58);
        addKnob(kFilter, 0.31f, 0.61f, 0.105f, 52, 54, 58);
        addKnob(kTone,   0.69f, 0.61f, 0.105f, 52, 54, 58);
    }
protected:
    void drawFace() override {
        enclosure(34, 35, 39);            // dark charcoal, not pure black
        const float f = sc();
        // muted lime hairlines (silkscreen frame)
        accentLine(W()*0.12f, H()*0.085f, W()*0.88f, H()*0.085f, Color(LR, LG, LB), 1.5f);
        accentLine(W()*0.12f, H()*0.95f,  W()*0.88f, H()*0.95f,  Color(LR, LG, LB), 1.5f);
        // generic title block (no brand) — clean modern sans, spaced caps
        textSpaced(0.5f, 0.13f, 23, Color(LR, LG, LB), "OVERDRIVE", fBarlow, 1.5f);
        textSpaced(0.5f, 0.185f, 9.5f, Color(132, 138, 116), "BASS  CMOS  DRIVE", fBarlow, 2.0f);
        // LED above footswitch
        ledDot(W()*0.5f, H()*0.80f, 5*f, true, 196, 72, 60);
        // round footswitch
        footswitchRound(W()*0.5f, H()*0.89f, 22*f);
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassOverdriveUI)
};

UI* createUI() { return new BassOverdriveUI(); }

END_NAMESPACE_DISTRHO
