/* Electric B600F UI — Acoustic B600H style black face with cyan accent: Passive/
 * Active inputs + Mute, PREAMP (Gain/Volume), NOTCH (Freq + On), a 6-band EQ
 * (40/120/350/800/2k/5k), power, and the 'electric' / B600F wordmarks. Knobs
 * vertical-drag; switches toggle on click. */
#include "DistrhoUI.hpp"
#include "ElectricParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,      0.175f, 0.50f, 0.026f, "GAIN" },
    { kVolume,    0.235f, 0.50f, 0.026f, "VOLUME" },
    { kNotchFreq, 0.305f, 0.50f, 0.026f, "FREQUENCY" },
    { kEq40,      0.420f, 0.50f, 0.026f, "40" },
    { kEq120,     0.480f, 0.50f, 0.026f, "120" },
    { kEq350,     0.540f, 0.50f, 0.026f, "350" },
    { kEq800,     0.600f, 0.50f, 0.026f, "800" },
    { kEq2k,      0.660f, 0.50f, 0.026f, "2K" },
    { kEq5k,      0.720f, 0.50f, 0.026f, "5K" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kActive,  0.110f, 0.46f, "ACTIVE" },
    { kMute,    0.130f, 0.72f, "MUTE" },
    { kNotchOn, 0.348f, 0.50f, "NOTCH" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class ElectricUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1100.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(40,42,46)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(48,50,54),Color(14,14,16));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(80,200,220)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(210,212,216));
        text(cx,cy-R-13*f,k.name,NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.009f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); fillColor(on?Color(30,90,100):Color(28,28,30)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,2*f); strokeColor(Color(60,140,150)); strokeWidth(1.0f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(190,196,202)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(190,192,196)); text(x,y+hs+3*f,s.lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.009f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    ElectricUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kElectricDef[i];
        setGeometryConstraints(1100*3/5,200*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(24,24,26)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        beginPath(); roundedRect(bx,by,bw,bh,6*f); fillColor(Color(20,20,22)); fill();
        beginPath(); roundedRect(bx,by,bw,bh,6*f); strokeColor(Color(50,52,56)); strokeWidth(1.4f*f); stroke();
        // cyan accent stripes
        const Color cyan=Color(40,180,205);
        beginPath(); rect(bx,by+10*f,bw,2*f); fillColor(cyan); fill();
        beginPath(); rect(bx,by+bh-12*f,bw,2*f); fillColor(cyan); fill();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // input jacks
        for(int i=0;i<2;++i){ float jx=w*(0.045f+i*0.035f);
            beginPath(); circle(jx,h*0.50f,7*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,h*0.50f,7*f); strokeColor(Color(80,90,100)); strokeWidth(1.4f*f); stroke(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(190,192,196));
        text(w*0.045f,h*0.30f,"PASSIVE",NULL); text(w*0.080f,h*0.30f,"ACTIVE",NULL);
        // section labels
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(150,200,210));
        text(w*0.205f,h*0.78f,"PREAMP",NULL); text(w*0.327f,h*0.78f,"NOTCH",NULL); text(w*0.570f,h*0.78f,"EQUALIZER",NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // wordmarks
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(18*f); fillColor(Color(228,230,234)); text(bx+16*f,by+bh-4*f,"electric",NULL);
        textAlign(ALIGN_RIGHT|ALIGN_MIDDLE); fontSize(20*f); fillColor(Color(228,230,234)); text(bx+bw-60*f,h*0.5f,"B600F",NULL);
        // power
        beginPath(); circle(w*0.945f,h*0.50f,4.5f*f); fillColor(Color(40,90,220)); fill();
        beginPath(); roundedRect(w*0.975f-8*f,h*0.5f-13*f,16*f,26*f,2*f); fillColor(Color(16,16,18)); fill();
        beginPath(); roundedRect(w*0.975f-6*f,h*0.5f-11*f,12*f,12*f,2*f); fillColor(Color(60,62,66)); fill();
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectricUI)
};

UI* createUI() { return new ElectricUI(); }

END_NAMESPACE_DISTRHO
