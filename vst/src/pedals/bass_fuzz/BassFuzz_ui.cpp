/* BassFuzz UI — copyright-free recreation of the classic GREEN bass fuzz it
 * models: silver frame + green face, 3 black knobs, a 3-way mode toggle, and a
 * white-with-black-outline wordmark. Faithful layout; no brand/model name. */
#include "BassFuzzParams.h"
#include "../_shared/pedalkit.hpp"
START_NAMESPACE_DISTRHO
class BassFuzzUI : public PedalKitUI {
    static const int FR=92, FG=174, FB=50;   // green face
public:
    BassFuzzUI() : PedalKitUI(320, 400, kParamCount, kBassFuzzDef) {
        names_ = kBassFuzzNames; knobLabels_ = true; labelFont_ = fBarlow;
        labelClr = Color(22,32,16); pointerClr = Color(236,238,238); tickClr = Color(70,120,40);
        // 3 black knobs in a row (Volume/Tone/Sustain positions)
        addKnob(kGain,   0.26f, 0.205f, 0.085f, 24,24,26, 2);
        addKnob(kTone,   0.50f, 0.205f, 0.085f, 24,24,26, 2);
        addKnob(kFilter, 0.74f, 0.205f, 0.085f, 24,24,26, 2);
    }
    void modeToggle(float cx, float cy) {           // static 3-way mode switch (decorative)
        const float f = sc();
        beginPath(); roundedRect(cx-9*f, cy-6*f, 18*f, 12*f, 3*f); fillColor(Color(24,24,26)); fill();
        Paint lev = linearGradient(cx-5*f, cy-5*f, cx+5*f, cy+5*f, Color(232,234,238), Color(140,143,150));
        beginPath(); roundedRect(cx-4*f, cy-7*f, 8*f, 9*f, 2*f); fillPaint(lev); fill();   // lever up (NORM)
        face(fBarlow); fontSize(7.5f*f); fillColor(Color(22,32,16));
        textAlign(ALIGN_RIGHT|ALIGN_MIDDLE); text(cx-12*f, cy, "NORM", NULL);
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE);
        text(cx+12*f, cy-5*f, "BASS BOOST", NULL); text(cx+12*f, cy+5*f, "DRY", NULL);
    }
protected:
    void drawFace() override {
        enclosure(190, 192, 196);                    // silver frame
        const float f = sc(), w = W(), h = H();
        // green face panel inset within the silver frame
        beginPath(); roundedRect(w*0.105f, h*0.085f, w*0.79f, h*0.83f, 10*f);
        Paint face2 = linearGradient(0, h*0.085f, 0, h*0.915f, Color(FR+14,FG+14,FB+10), Color(FR-18,FG-22,FB-16));
        fillPaint(face2); fill();
        beginPath(); roundedRect(w*0.105f, h*0.085f, w*0.79f, h*0.83f, 10*f); strokeColor(Color(0,0,0,70)); strokeWidth(1.5f*f); stroke();
        // 3-way mode toggle
        modeToggle(w*0.40f, h*0.42f);
        // wordmark: big wide centred 'FUZZ' (white + black outline) with lowercase
        // black 'bass' sitting on top of the 'FU'
        outlineText(0.5f, 0.64f, 62, Color(242,242,244), Color(12,14,16), "FUZZ", fAnton, 7.0f);
        textC(0.34f, 0.55f, 29, Color(16,20,14), "bass", fSerif);
        // red LED by the wordmark (not under a knob)
        ledDot(w*0.52f, h*0.55f, 4.5f*f, true, 224,60,52);
        footswitchRound(w*0.5f, h*0.81f, 21*f);
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFuzzUI)
};
UI* createUI(){ return new BassFuzzUI(); }
END_NAMESPACE_DISTRHO
