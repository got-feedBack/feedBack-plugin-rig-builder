/* BassFuzz UI — copyright-free recreation of the classic large bass fuzz it
 * models (silver enclosure, red outlined wordmark, black swoosh, 3 black knobs).
 * Faithful to the real layout; no brand/model name. */
#include "BassFuzzParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFuzzUI : public PedalKitUI {
public:
    BassFuzzUI() : PedalKitUI(320, 420, kParamCount, kBassFuzzDef) {
        names_ = kBassFuzzNames; knobLabels_ = true; labelFont_ = fBarlow;
        labelClr = Color(40,42,46); pointerClr = Color(235,236,238); tickClr = Color(110,112,118);
        // 3 black knobs in a row (Davies-ish), like Volume/Tone/Sustain
        addKnob(kGain,   0.22f, 0.20f, 0.092f, 26,26,28, 2);
        addKnob(kTone,   0.50f, 0.20f, 0.092f, 26,26,28, 2);
        addKnob(kFilter, 0.78f, 0.20f, 0.092f, 26,26,28, 2);
    }
protected:
    void drawFace() override {
        enclosure(188, 190, 194);                    // brushed silver
        const float f = sc(), w = W(), h = H();
        // black swoosh band across the lower-middle (the wordmark plate)
        beginPath();
        roundedRect(w*0.10f, h*0.50f, w*0.80f, h*0.26f, 16*f);
        fillColor(Color(18,18,20)); fill();
        // red outlined wordmark: BASS (on silver) + big FUZZ (on the black swoosh)
        outlineText(0.5f, 0.445f, 27, Color(198,44,48), Color(238,238,240), "BASS", fAnton);
        outlineText(0.5f, 0.625f, 50, Color(206,48,52), Color(240,240,242), "FUZZ", fAnton);
        // LED between the first two knobs
        ledDot(w*0.385f, h*0.205f, 4.5f*f, true, 210,70,58);
        footswitchRound(w*0.5f, h*0.87f, 22*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzUI)
};
UI* createUI(){ return new BassFuzzUI(); }
END_NAMESPACE_DISTRHO
