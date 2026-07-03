/* SBR Super Redhead UI — SWR Super Redhead style red rack face: Passive/Active
 * inputs, Gain (+Turbo), Aural Enhancer, Bass, Mid Level + Mid Freq, Treble
 * (+Transparency), Master, and the SBR circle logo. Knobs vertical-drag;
 * switches toggle on click. */
#include "DistrhoUI.hpp"
#include "RedheadParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,     0.110f, 0.56f, 0.030f, "GAIN" },
    { kAural,    0.185f, 0.56f, 0.030f, "AURAL" },
    { kBass,     0.260f, 0.56f, 0.030f, "BASS" },
    { kMidLevel, 0.340f, 0.56f, 0.030f, "LEVEL" },
    { kMidFreq,  0.415f, 0.56f, 0.030f, "FREQ" },
    { kTreble,   0.495f, 0.56f, 0.030f, "TREBLE" },
    { kMaster,   0.580f, 0.56f, 0.033f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kActive,       0.040f, 0.50f, "ACTIVE" },
    { kTurbo,        0.110f, 0.84f, "TURBO" },
    { kTransparency, 0.495f, 0.84f, "TRANSP" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class RedheadUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/980.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(40,8,8)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(52,52,56),Color(16,16,18));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(246,246,248)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(245,235,235));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.010f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); fillColor(on?Color(80,18,18):Color(40,12,12)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); strokeColor(Color(150,60,60)); strokeWidth(1.1f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(235,225,225)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(245,230,230)); text(x,y+hs+3*f,s.lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.010f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    RedheadUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kRedheadDef[i];
        setGeometryConstraints(980*3/5,200*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(40,8,8)); fill();
        const float bx=5*f,by=5*f,bw=w-10*f,bh=h-10*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(214,34,34),Color(176,22,22));
        beginPath(); roundedRect(bx,by,bw,bh,6*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,6*f); strokeColor(Color(120,16,16)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const Color ink=Color(245,235,235);
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(250,235,235));
        text(bx+16*f,by+8*f,"ALL TUBE PREAMP / 350 WATT POWER AMP",NULL);
        // input jacks
        for(int i=0;i<2;++i){ float jy=h*(0.40f+i*0.30f);
            beginPath(); circle(w*0.040f,jy,7*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(w*0.040f,jy,7*f); strokeColor(Color(120,30,30)); strokeWidth(1.4f*f); stroke(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(ink); text(w*0.040f,h*0.86f,"INPUTS",NULL);
        // MID-RANGE bracket
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(ink); text(w*0.378f,h*0.27f,"MID-RANGE",NULL);
        strokeColor(Color(120,20,20)); strokeWidth(1.2f*f);
        beginPath(); moveTo(w*0.318f,h*0.345f); lineTo(w*0.438f,h*0.345f); stroke();
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // SBR circle logo (right)
        const float lx=w*0.880f, ly=h*0.52f, lr=24*f;
        beginPath(); circle(lx,ly,lr); fillColor(Color(238,232,232)); fill();
        beginPath(); circle(lx,ly,lr); strokeColor(Color(120,16,16)); strokeWidth(2.0f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(18*f); fillColor(Color(190,24,24)); text(lx,ly,"SBR",NULL);
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(ink); text(lx,h*0.12f,"SUPER REDHEAD",NULL);
        text(lx,h*0.84f,"INTEGRATED BASS SYSTEM",NULL);
        // power rocker
        beginPath(); roundedRect(w*0.965f-7*f,h*0.5f-13*f,14*f,26*f,2*f); fillColor(Color(16,16,18)); fill();
        beginPath(); roundedRect(w*0.965f-5*f,h*0.5f-11*f,10*f,12*f,2*f); fillColor(Color(60,62,66)); fill();
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            int sw=switchAt(ev.pos.getX(),ev.pos.getY());
            if(sw>=0){ int id=kSwitches[sw].id; float nv=fValues[id]>0.5f?0.f:1.f; fValues[id]=nv; setParameterValue(id,nv); repaint(); return true; }
            int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale()); if(fDragVal<0)fDragVal=0; if(fDragVal>1)fDragVal=1; int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RedheadUI)
};

UI* createUI() { return new RedheadUI(); }

END_NAMESPACE_DISTRHO
