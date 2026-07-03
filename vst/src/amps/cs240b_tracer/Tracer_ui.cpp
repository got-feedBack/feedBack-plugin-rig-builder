/* Tracer V8 UI — Trace Elliot V-Type V8 style black head: Passive/Active inputs,
 * PREAMP (Gain I+Bright, Gain II+Pull, Level), TONE (Bass+Deep, Middle+Shift,
 * Treble), COMPRESSOR (On/Off + Level), Master, a green V8 badge + V-Type logo
 * and a Standby rocker. Knobs vertical-drag; switches toggle on click. */
#include "DistrhoUI.hpp"
#include "TracerParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain1,  0.150f, 0.55f, 0.030f, "GAIN I" },
    { kGain2,  0.225f, 0.55f, 0.030f, "GAIN II" },
    { kLevel,  0.300f, 0.55f, 0.030f, "LEVEL" },
    { kBass,   0.405f, 0.55f, 0.030f, "BASS" },
    { kMiddle, 0.480f, 0.55f, 0.030f, "MIDDLE" },
    { kTreble, 0.555f, 0.55f, 0.030f, "TREBLE" },
    { kComp,   0.685f, 0.55f, 0.030f, "LEVEL" },
    { kMaster, 0.775f, 0.55f, 0.034f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kActive,    0.055f, 0.55f, "ACTIVE" },
    { kBright,    0.150f, 0.82f, "BRIGHT" },
    { kGain2Pull, 0.225f, 0.82f, "PULL" },
    { kDeep,      0.405f, 0.82f, "DEEP" },
    { kMidShift,  0.480f, 0.82f, "SHIFT" },
    { kCompOn,    0.615f, 0.55f, "ON/OFF" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class TracerUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/960.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(22,22,24)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(58,59,64),Color(18,18,20));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(206,208,212));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.011f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); fillColor(on?Color(40,90,50):Color(28,28,30)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); strokeColor(Color(96,98,102)); strokeWidth(1.1f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(150,152,156)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(190,192,196)); text(x,y+hs+3*f,s.lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.011f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    TracerUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kTracerDef[i];
        setGeometryConstraints(960*3/5,280*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(12,12,13)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(28,28,30),Color(14,14,16));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(60,61,64)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const Color grn=Color(120,200,60), dim=Color(150,152,156);
        // section labels
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(dim);
        text(w*0.055f,h*0.20f,"INPUTS",NULL); text(w*0.225f,h*0.20f,"PREAMP",NULL);
        text(w*0.480f,h*0.20f,"TONE CONTROL",NULL); text(w*0.650f,h*0.20f,"COMPRESSOR",NULL);
        // bracket lines under group headers
        strokeColor(Color(80,82,86)); strokeWidth(1.2f*f);
        beginPath(); moveTo(w*0.135f,h*0.27f); lineTo(w*0.315f,h*0.27f); stroke();
        beginPath(); moveTo(w*0.390f,h*0.27f); lineTo(w*0.570f,h*0.27f); stroke();
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // V8 badge + V-Type logo
        const float vx=w*0.86f, vy=h*0.40f;
        beginPath(); roundedRect(vx-22*f,vy-16*f,44*f,32*f,3*f); fillColor(Color(20,20,22)); fill();
        beginPath(); roundedRect(vx-22*f,vy-16*f,44*f,32*f,3*f); strokeColor(grn); strokeWidth(1.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(22*f); fillColor(grn); text(vx,vy,"V8",NULL);
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(20*f); fillColor(grn); text(w*0.83f,by+bh-22*f,"V-Type",NULL);
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(7*f); fillColor(dim); text(w*0.83f,by+bh-10*f,"400W ALL VALVE",NULL);
        // standby rocker (far right)
        const float sx=w*0.955f,sy=h*0.55f;
        beginPath(); roundedRect(sx-8*f,sy-15*f,16*f,30*f,3*f); fillColor(Color(16,16,18)); fill();
        beginPath(); roundedRect(sx-6*f,sy-13*f,12*f,13*f,2*f); fillColor(Color(60,62,66)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(dim); text(sx,sy+18*f,"STANDBY",NULL);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TracerUI)
};

UI* createUI() { return new TracerUI(); }

END_NAMESPACE_DISTRHO
