/* Citrus Rumbleverb 50 UI — Orange Rockerverb style orange-tolex head: cream
 * strip with the CITRUS logo + RUMBLEVERB 50, a black control panel with the
 * Clean (Vol/Bass/Treble) + Dirty (Gain/Bass/Mid/Treble/Vol) channels, Reverb,
 * Output and the channel switch. (The in-app face is the canvas spec in
 * pedal_canvas.js; this native UI is only for standalone/host use.) Knobs
 * vertical-drag; Channel toggles on click. */
#include "DistrhoUI.hpp"
#include "RumbleverbParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kCleanVolume,  0.150f, 0.66f, 0.034f, "VOL" },
    { kCleanBass,    0.230f, 0.66f, 0.034f, "BASS" },
    { kCleanTreble,  0.310f, 0.66f, 0.034f, "TREBLE" },
    { kGain,         0.440f, 0.66f, 0.034f, "GAIN" },
    { kBass,         0.520f, 0.66f, 0.034f, "BASS" },
    { kMiddle,       0.590f, 0.66f, 0.034f, "MID" },
    { kTreble,       0.660f, 0.66f, 0.034f, "TREBLE" },
    { kVolume,       0.730f, 0.66f, 0.034f, "VOL" },
    { kReverb,       0.840f, 0.66f, 0.034f, "REVERB" },
    { kOutput,       0.915f, 0.66f, 0.034f, "OUTPUT" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class RumbleverbUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1000.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(180,182,186)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(18,18,20)); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(28,22,16));
        text(cx,cy+R+5*f,k.name,NULL);
    }

    void onNanoDisplay() override {
        const float w=W(), h=H(), f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(232,120,28)); fill();          // orange tolex
        beginPath(); rect(w*0.03f,h*0.05f,w*0.94f,h*0.30f); fillColor(Color(238,236,230)); fill(); // cream strip
        fontSize(30*f); fillColor(Color(24,22,20)); textAlign(ALIGN_LEFT|ALIGN_MIDDLE);
        text(w*0.06f,h*0.20f,"CITRUS",NULL);
        fontSize(14*f); text(w*0.42f,h*0.20f,"RUMBLEVERB 50",NULL);
        beginPath(); rect(w*0.03f,h*0.42f,w*0.94f,h*0.50f); fillColor(Color(16,16,18)); fill(); // black panel
        for (int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        // channel switch
        const float sx=w*0.065f, sy=h*0.66f, sw=14*f, sh=26*f;
        beginPath(); rect(sx-sw*0.5f,sy-sh*0.5f,sw,sh); fillColor(Color(30,30,32)); fill();
        beginPath(); rect(sx-sw*0.5f,fValues[kChannel]>0.5f?sy:sy-sh*0.5f,sw,sh*0.5f); fillColor(Color(232,120,28)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(232,233,236));
        text(sx,sy+sh*0.6f,"DIRTY",NULL);
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button!=1) return false;
        if (ev.press){
            const float w=W(), h=H();
            for (int i=0;i<kNumKnobs;++i){ const Spot& k=kKnobs[i];
                const float dx=ev.pos.getX()-w*k.cx, dy=ev.pos.getY()-h*k.cy;
                if (dx*dx+dy*dy <= (w*k.r)*(w*k.r)){ fDrag=k.id; fLastY=ev.pos.getY(); fDragVal=fValues[k.id]; return true; } }
            const float sx=w*0.065f, sy=h*0.66f, sw=14*scale(), sh=26*scale();
            if (std::fabs(ev.pos.getX()-sx)<=sw && std::fabs(ev.pos.getY()-sy)<=sh){
                fValues[kChannel]=fValues[kChannel]>0.5f?0.f:1.f; setParameterValue(kChannel,fValues[kChannel]); repaint(); return true; }
        } else fDrag=-1;
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag<0) return false;
        const float dy=(float)(fLastY-ev.pos.getY());
        float v=fDragVal+dy/200.0f; if(v<0)v=0; if(v>1)v=1;
        fValues[fDrag]=v; setParameterValue((uint32_t)fDrag,v); repaint(); return true;
    }

public:
    RumbleverbUI() : UI(1000,300), fDrag(-1), fLastY(0), fDragVal(0) {
        for (int i=0;i<kParamCount;++i) fValues[i]=kRumbleverbDef[i];
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleverbUI)
};

UI* createUI() { return new RumbleverbUI(); }

END_NAMESPACE_DISTRHO
