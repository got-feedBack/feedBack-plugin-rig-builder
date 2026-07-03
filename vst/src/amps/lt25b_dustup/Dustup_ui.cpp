/* Dustup CDN UI — Ashdown ABM EVO style blue-grey face: Input (+VU), Bass/
 * Middle/Treble with a 6-band graphic EQ between them, Valve Drive / Sub
 * Harmonics / Comp, Output (+VU), and Passive/Active + Flat/Shape + EQ/Sub/Comp
 * switches. Knobs + faders vertical-drag; switches toggle on click. */
#include "DistrhoUI.hpp"
#include "DustupParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kInput,  0.065f, 0.40f, 0.030f, "INPUT" },
    { kBass,   0.230f, 0.36f, 0.030f, "BASS" },
    { kMiddle, 0.460f, 0.36f, 0.030f, "MIDDLE" },
    { kTreble, 0.640f, 0.36f, 0.030f, "TREBLE" },
    { kValve,  0.300f, 0.72f, 0.028f, "VALVE DRIVE" },
    { kSub,    0.500f, 0.72f, 0.028f, "SUB" },
    { kComp,   0.640f, 0.72f, 0.028f, "COMP" },
    { kOutput, 0.915f, 0.40f, 0.030f, "OUTPUT" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
static const char* const kEqLbl[kNumEq] = {"100","180","340","1.3k","3.6k","5k"};
static const float kEqX[kNumEq] = { .290f,.330f,.370f, .510f,.550f,.590f };
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kActive, 0.070f, 0.72f, "ACTIVE" },
    { kShape,  0.122f, 0.72f, "SHAPE" },
    { kEqIn,   0.430f, 0.86f, "EQ" },
    { kSubOn,  0.560f, 0.86f, "SUB" },
    { kCompOn, 0.700f, 0.86f, "COMP" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class DustupUI : public UI {
    float fValues[kParamCount];
    int fDrag, fFader; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1100.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }
    float eqX(int i) const { return W()*kEqX[i]; }
    float eqY0() const { return H()*0.20f; }
    float eqY1() const { return H()*0.50f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(40,42,46)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(18,18,20)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(46,47,52),Color(14,14,16));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(30,38,46));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawFader(int i){
        const float x=eqX(i), y0=eqY0(), y1=eqY1(), f=scale(), n=fValues[kFirstEq+i];
        beginPath(); roundedRect(x-2.5f*f,y0,5*f,y1-y0,2.5f*f); fillColor(Color(40,42,46)); fill();
        const float cy=y1-n*(y1-y0);
        beginPath(); roundedRect(x-8*f,cy-5*f,16*f,10*f,2*f); fillColor(Color(60,62,66)); fill();
        beginPath(); moveTo(x-6*f,cy); lineTo(x+6*f,cy); strokeColor(Color(228,230,234)); strokeWidth(1.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(40,48,56)); text(x,y1+3*f,kEqLbl[i],NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.009f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); fillColor(on?Color(60,90,110):Color(30,32,36)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); strokeColor(Color(90,100,110)); strokeWidth(1.0f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(190,196,202)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(30,38,46)); text(x,y+hs+3*f,s.lbl,NULL);
    }
    void vu(float cx,float cy){ const float f=scale(); const float w=58*f,hh=34*f,x=W()*cx-w/2,y=H()*cy-hh/2;
        beginPath(); roundedRect(x,y,w,hh,3*f); fillColor(Color(238,206,90)); fill();
        beginPath(); roundedRect(x,y,w,hh,3*f); strokeColor(Color(20,20,22)); strokeWidth(1.4f*f); stroke();
        strokeColor(Color(40,40,44)); strokeWidth(1.0f*f);
        beginPath(); moveTo(x+w*0.5f,y+hh*0.9f); lineTo(x+w*0.72f,y+hh*0.35f); stroke();
        strokeColor(Color(176,32,30)); beginPath(); moveTo(x+w*0.72f,y+hh*0.35f); lineTo(x+w*0.88f,y+hh*0.30f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7*f); fillColor(Color(40,40,44)); text(x+w*0.5f,y+hh-2*f,"VU",NULL); }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int faderAt(double px,double py) const { for(int i=0;i<kNumEq;++i){ if(std::fabs(px-eqX(i))<=10 && py>=eqY0()-10 && py<=eqY1()+10) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.009f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    DustupUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fFader(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kDustupDef[i];
        setGeometryConstraints(1100*3/5,280*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(28,28,30)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(190,206,214),Color(150,166,178));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(90,100,110)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        vu(0.140f,0.36f); vu(0.840f,0.36f);
        // logo
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(16*f); fillColor(Color(30,38,46)); text(w*0.300f,h*0.16f,"Dustup",NULL);
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(11*f); fillColor(Color(40,48,56)); text(w*0.300f,h*0.30f,"CDN  EVO",NULL);
        // input jack
        beginPath(); circle(w*0.020f,h*0.40f,8*f); fillColor(Color(16,16,18)); fill();
        beginPath(); circle(w*0.020f,h*0.40f,8*f); strokeColor(Color(80,90,100)); strokeWidth(1.4f*f); stroke();
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumEq;++i) drawFader(i);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            int sw=switchAt(ev.pos.getX(),ev.pos.getY());
            if(sw>=0){ int id=kSwitches[sw].id; float nv=fValues[id]>0.5f?0.f:1.f; fValues[id]=nv; setParameterValue(id,nv); repaint(); return true; }
            int fd=faderAt(ev.pos.getX(),ev.pos.getY());
            if(fd>=0){ fFader=fd; editParameter(kFirstEq+fd,true); float n=1.f-(float)((ev.pos.getY()-eqY0())/(eqY1()-eqY0())); if(n<0)n=0; if(n>1)n=1; fValues[kFirstEq+fd]=n; setParameterValue(kFirstEq+fd,n); repaint(); return true; }
            int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else { if(fFader>=0){ editParameter(kFirstEq+fFader,false); fFader=-1; return true; } if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; } }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fFader>=0){ float n=1.f-(float)((ev.pos.getY()-eqY0())/(eqY1()-eqY0())); if(n<0)n=0; if(n>1)n=1; fValues[kFirstEq+fFader]=n; setParameterValue(kFirstEq+fFader,n); repaint(); return true; }
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale()); if(fDragVal<0)fDragVal=0; if(fDragVal>1)fDragVal=1; int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DustupUI)
};

UI* createUI() { return new DustupUI(); }

END_NAMESPACE_DISTRHO
