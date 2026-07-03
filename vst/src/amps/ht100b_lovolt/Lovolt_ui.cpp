/* Lovolt 100 UI — Custom Hiwatt 100 (DR103) style black head face: Normal/Bright
 * input jacks, the Normal Vol / Bright Vol / Bass / Treble / Middle / Presence /
 * Master Vol knob row, a silver "LOVOLT" logo box, power LED + standby/mains
 * rockers. Knobs vertical-drag. */
#include "DistrhoUI.hpp"
#include "LovoltParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kNormalVol, 0.180f, 0.52f, 0.030f, "NORMAL" },
    { kBrightVol, 0.265f, 0.52f, 0.030f, "BRIGHT" },
    { kBass,      0.380f, 0.52f, 0.030f, "BASS" },
    { kTreble,    0.465f, 0.52f, 0.030f, "TREBLE" },
    { kMiddle,    0.550f, 0.52f, 0.030f, "MIDDLE" },
    { kPresence,  0.635f, 0.52f, 0.030f, "PRESENCE" },
    { kMaster,    0.720f, 0.52f, 0.030f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class LovoltUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/900.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(22,22,24)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(58,59,64),Color(18,18,20));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        strokeColor(Color(150,152,158)); strokeWidth(1.2f*f);
        for (int t=0;t<=10;++t){ float a=angleFor(t/10.f);
            beginPath(); moveTo(cx+(R+3*f)*std::cos(a),cy+(R+3*f)*std::sin(a)); lineTo(cx+(R+7*f)*std::cos(a),cy+(R+7*f)*std::sin(a)); stroke(); }
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(214,216,220));
        text(cx,cy+R+7*f,k.name,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
public:
    LovoltUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kLovoltDef[i];
        setGeometryConstraints(900*3/5,230*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(8,8,9)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(30,30,32),Color(12,12,14));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(70,71,75)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // LOVOLT logo box (silver border)
        const float lx=w*0.020f, ly=h*0.10f, lw=w*0.135f, lh=h*0.24f;
        beginPath(); roundedRect(lx,ly,lw,lh,3*f); strokeColor(Color(210,212,216)); strokeWidth(1.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(14*f); fillColor(Color(220,222,226));
        text(lx+lw*0.5f, ly+lh*0.5f, "LOVOLT", NULL);
        // title
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(13*f); fillColor(Color(210,212,216));
        text(w*0.45f, by+10*f, "CUSTOM LOVOLT 100", NULL);
        // input jacks (Normal / Bright, two each)
        for (int i=0;i<2;++i) for (int j=0;j<2;++j){ float jx=w*(0.045f+i*0.030f), jy=h*(0.40f+j*0.22f);
            beginPath(); circle(jx,jy,6*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,jy,6*f); strokeColor(Color(120,122,126)); strokeWidth(1.3f*f); stroke(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(190,192,196));
        text(w*0.060f, h*0.74f, "NORMAL   BRIGHT", NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        // power LED + standby/mains rockers
        beginPath(); circle(w*0.80f,h*0.50f,4*f); fillColor(Color(220,40,36)); fill();
        const char* rl[2]={"STANDBY","MAINS"};
        for (int i=0;i<2;++i){ float rx=w*(0.855f+i*0.055f), ry=h*0.50f;
            beginPath(); roundedRect(rx-7*f,ry-15*f,14*f,30*f,3*f); fillColor(Color(16,16,18)); fill();
            beginPath(); roundedRect(rx-7*f,ry-15*f,14*f,30*f,3*f); strokeColor(Color(80,82,86)); strokeWidth(1.1f*f); stroke();
            beginPath(); roundedRect(rx-5*f,ry-13*f,10*f,14*f,2*f); fillColor(Color(60,62,66)); fill();
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(150,152,156)); text(rx,ry+18*f,rl[i],NULL); }
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LovoltUI)
};

UI* createUI() { return new LovoltUI(); }

END_NAMESPACE_DISTRHO
