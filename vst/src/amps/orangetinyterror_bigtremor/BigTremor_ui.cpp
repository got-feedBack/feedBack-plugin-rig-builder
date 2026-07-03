/* Citrus Big Tremor UI — Orange Tiny Terror style cream head: white panel with
 * the CITRUS bubble logo + BIG TREMOR, an orange control strip with Volume /
 * Tone / Gain and the 7 W Half-power switch. (The in-app face is the canvas spec
 * in pedal_canvas.js; this native UI is only for standalone/host use.) Knobs
 * vertical-drag; Half toggles on click. */
#include "DistrhoUI.hpp"
#include "BigTremorParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kVolume, 0.380f, 0.62f, 0.052f, "VOLUME" },
    { kTone,   0.530f, 0.62f, 0.052f, "TONE" },
    { kGain,   0.680f, 0.62f, 0.052f, "GAIN" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class BigTremorUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/900.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(180,182,186)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(52,52,56),Color(14,14,16));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(10.0f*f); fillColor(Color(28,26,22));
        text(cx,cy+R+6*f,k.name,NULL);
    }

    void onNanoDisplay() override {
        const float w=W(), h=H(), f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(236,236,232)); fill();         // cream panel
        beginPath(); rect(0,h*0.30f,w,h*0.12f); fillColor(Color(232,120,28)); fill(); // orange strip
        fontSize(34*f); fillColor(Color(24,22,20)); textAlign(ALIGN_LEFT|ALIGN_MIDDLE);
        text(w*0.06f,h*0.16f,"CITRUS",NULL);
        fontSize(15*f); text(w*0.06f,h*0.78f,"BIG TREMOR",NULL);
        for (int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        // Half-power switch
        const float sx=w*0.84f, sy=h*0.62f, sw=14*f, sh=24*f;
        beginPath(); rect(sx-sw*0.5f,sy-sh*0.5f,sw,sh); fillColor(Color(30,30,32)); fill();
        beginPath(); rect(sx-sw*0.5f,fValues[kHalf]>0.5f?sy:sy-sh*0.5f,sw,sh*0.5f); fillColor(Color(200,200,206)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(28,26,22));
        text(sx,sy+sh*0.6f,"7W",NULL);
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button!=1) return false;
        if (ev.press){
            const float w=W(), h=H();
            for (int i=0;i<kNumKnobs;++i){ const Spot& k=kKnobs[i];
                const float dx=ev.pos.getX()-w*k.cx, dy=ev.pos.getY()-h*k.cy;
                if (dx*dx+dy*dy <= (w*k.r)*(w*k.r)){ fDrag=k.id; fLastY=ev.pos.getY(); fDragVal=fValues[k.id]; return true; } }
            const float sx=w*0.84f, sy=h*0.62f, sw=14*scale(), sh=24*scale();
            if (std::fabs(ev.pos.getX()-sx)<=sw && std::fabs(ev.pos.getY()-sy)<=sh){
                fValues[kHalf]=fValues[kHalf]>0.5f?0.f:1.f; setParameterValue(kHalf,fValues[kHalf]); repaint(); return true; }
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
    BigTremorUI() : UI(900,300), fDrag(-1), fLastY(0), fDragVal(0) {
        for (int i=0;i<kParamCount;++i) fValues[i]=kBigTremorDef[i];
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BigTremorUI)
};

UI* createUI() { return new BigTremorUI(); }

END_NAMESPACE_DISTRHO
