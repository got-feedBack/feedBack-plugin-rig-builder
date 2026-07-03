/* Hipzon GA-79 RVT UI — placeholder native panel (the in-app face is the canvas
 * spec in pedal_canvas.js; this native UI is only for standalone/host use). */
#include "DistrhoUI.hpp"
#include "Ga79Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kVolume, 0.150f, 0.60f, 0.042f, "VOLUME" },
    { kBass,   0.290f, 0.60f, 0.042f, "BASS" },
    { kTreble, 0.430f, 0.60f, 0.042f, "TREBLE" },
    { kReverb, 0.570f, 0.60f, 0.042f, "REVERB" },
    { kSpeed,  0.710f, 0.60f, 0.042f, "SPEED" },
    { kDepth,  0.850f, 0.60f, 0.042f, "DEPTH" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class Ga79UI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1000.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,150,154)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(228,228,222));
        text(cx,cy+R+5*f,k.name,NULL);
    }

    void onNanoDisplay() override {
        const float w=W(), h=H(), f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(86,70,58)); fill();             // brown tolex
        beginPath(); rect(w*0.05f,h*0.12f,w*0.90f,h*0.28f); fillColor(Color(196,194,186)); fill();
        fontSize(24*f); fillColor(Color(40,36,30)); textAlign(ALIGN_CENTER|ALIGN_MIDDLE);
        text(w*0.5f,h*0.26f,"Hipzon  GA-79 RVT",NULL);
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
    Ga79UI() : UI(1000,300), fDrag(-1), fLastY(0), fDragVal(0) {
        for (int i=0;i<kParamCount;++i) fValues[i]=kGa79Def[i];
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ga79UI)
};

UI* createUI() { return new Ga79UI(); }

END_NAMESPACE_DISTRHO
