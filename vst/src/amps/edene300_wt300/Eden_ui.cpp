/* Aiden GT-300 UI — Eden WT-300 style gold/tan tube-preamp face: input jack,
 * Gain (blue/Comp) + Enhance (white) + Bass (red), a 3-band semi-parametric EQ
 * (Freq row over Level row), Treble (red) + Master (blue), phones jack and a red
 * power rocker. Knobs vertical-drag. */
#include "DistrhoUI.hpp"
#include "EdenParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; int col; const char* name; };   // col 0=red 1=blue 2=white
static const Spot kKnobs[] = {
    { kP1Freq,  0.460f, 0.30f, 0.030f, 0, "FREQ" },
    { kP2Freq,  0.560f, 0.30f, 0.030f, 0, "FREQ" },
    { kP3Freq,  0.660f, 0.30f, 0.030f, 0, "FREQ" },
    { kGain,    0.085f, 0.66f, 0.032f, 1, "GAIN" },
    { kEnhance, 0.175f, 0.66f, 0.032f, 2, "ENHANCE" },
    { kBass,    0.300f, 0.66f, 0.030f, 0, "BASS" },
    { kP1Level, 0.460f, 0.66f, 0.030f, 0, "LEVEL" },
    { kP2Level, 0.560f, 0.66f, 0.030f, 0, "LEVEL" },
    { kP3Level, 0.660f, 0.66f, 0.030f, 0, "LEVEL" },
    { kTreble,  0.790f, 0.66f, 0.030f, 0, "TREBLE" },
    { kMaster,  0.875f, 0.66f, 0.032f, 1, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class EdenUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1000.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }
    static Color cap(int c){ return c==1?Color(70,120,200):(c==2?Color(232,232,228):Color(200,54,46)); }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(40,40,42)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(58,59,62),Color(20,20,22));
        beginPath(); circle(cx,cy,R); fillPaint(g); fill();
        // colored centre cap
        beginPath(); circle(cx,cy,R*0.5f); fillColor(cap(k.col)); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx,cy); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(245,245,245)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(40,30,18));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
public:
    EdenUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kEdenDef[i];
        setGeometryConstraints(1000*3/5,300*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(28,26,22)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(206,176,96),Color(176,146,72));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(180,52,44)); strokeWidth(2.0f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // logo / titles
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(12*f); fillColor(Color(180,52,44));
        text(bx+70*f,by+10*f,"Valve-Tech Series",NULL);
        fontSize(10*f); fillColor(Color(40,30,18));
        text(bx+70*f,by+28*f,"Twin Triode Tube Pre Amplifier   GT-300",NULL);
        textAlign(ALIGN_RIGHT|ALIGN_TOP); fontSize(20*f); fillColor(Color(28,24,18));
        text(bx+bw-70*f,by+8*f,"Aiden",NULL);
        fontSize(8*f); fillColor(Color(40,30,18));
        text(bx+bw-70*f,by+30*f,"Hybrid Bass Guitar Amplifier",NULL);
        // input jack
        beginPath(); circle(w*0.035f,h*0.66f,8*f); fillColor(Color(16,16,18)); fill();
        beginPath(); circle(w*0.035f,h*0.66f,8*f); strokeColor(Color(60,50,36)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(40,30,18)); text(w*0.035f,h*0.66f+12*f,"INPUT",NULL);
        // semi-parametric bracket label
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(8.5f*f); fillColor(Color(40,30,18));
        text(w*0.560f, h*0.92f, "Semi-Parametric Bass Equalizer", NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        // phones jack
        beginPath(); circle(w*0.955f,h*0.66f,8*f); fillColor(Color(16,16,18)); fill();
        beginPath(); circle(w*0.955f,h*0.66f,8*f); strokeColor(Color(60,50,36)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(40,30,18)); text(w*0.955f,h*0.66f+12*f,"PHONES",NULL);
        // power rocker
        const float px=w*0.955f,py=h*0.28f;
        beginPath(); roundedRect(px-9*f,py-13*f,18*f,26*f,3*f); fillColor(Color(20,18,16)); fill();
        beginPath(); roundedRect(px-6*f,py-11*f,12*f,11*f,2*f); fillColor(Color(176,32,30)); fill();
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){ int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale()); if(fDragVal<0)fDragVal=0; if(fDragVal>1)fDragVal=1; int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdenUI)
};

UI* createUI() { return new EdenUI(); }

END_NAMESPACE_DISTRHO
