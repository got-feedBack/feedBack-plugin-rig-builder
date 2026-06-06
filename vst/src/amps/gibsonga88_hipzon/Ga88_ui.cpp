/* Hipzon GA-88 UI — placeholder native panel (the in-app face is the canvas spec
 * in pedal_canvas.js; this native UI is only for standalone/host use). */
#include "DistrhoUI.hpp"
#include "Ga88Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kVolume, 0.380f, 0.60f, 0.050f, "VOLUME" },
    { kBass,   0.545f, 0.60f, 0.050f, "BASS" },
    { kTreble, 0.705f, 0.60f, 0.050f, "TREBLE" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class Ga88UI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/900.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,150,154)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(10*f); fillColor(Color(228,228,222));
        text(cx,cy+R+6*f,k.name,NULL);
    }

    void onNanoDisplay() override {
        const float w=W(), h=H(), f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(64,64,68)); fill();
        beginPath(); rect(w*0.08f,h*0.12f,w*0.84f,h*0.30f); fillColor(Color(196,194,186)); fill();
        fontSize(26*f); fillColor(Color(34,34,36)); textAlign(ALIGN_CENTER|ALIGN_MIDDLE);
        text(w*0.5f,h*0.27f,"Hipzon  GA-88",NULL);
        for (int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button!=1) return false;
        if (ev.press){ const float w=W(), h=H();
            for (int i=0;i<kNumKnobs;++i){ const Spot& k=kKnobs[i];
                const float dx=ev.pos.getX()-w*k.cx, dy=ev.pos.getY()-h*k.cy;
                if (dx*dx+dy*dy <= (w*k.r)*(w*k.r)){ fDrag=k.id; fLastY=ev.pos.getY(); fDragVal=fValues[k.id]; return true; } }
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
    Ga88UI() : UI(900,300), fDrag(-1), fLastY(0), fDragVal(0) {
        for (int i=0;i<kParamCount;++i) fValues[i]=kGa88Def[i];
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ga88UI)
};

UI* createUI() { return new Ga88UI(); }

END_NAMESPACE_DISTRHO
