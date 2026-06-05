/* Bender Fumble 800 UI — Fender Rumble Bass style black tube-head face: input
 * jack, the Gain / Bass / Middle / Treble / Master knob row, a Bright switch, a
 * silver "Bender" script wordmark and a power rocker. Knobs vertical-drag;
 * switch toggles on click. */
#include "DistrhoUI.hpp"
#include "RumbleParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,   0.150f, 0.46f, 0.040f, "GAIN" },
    { kBass,   0.300f, 0.46f, 0.040f, "BASS" },
    { kMiddle, 0.420f, 0.46f, 0.040f, "MIDDLE" },
    { kTreble, 0.540f, 0.46f, 0.040f, "TREBLE" },
    { kMaster, 0.800f, 0.46f, 0.040f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class RumbleUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/760.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(22,22,24)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(58,59,64),Color(18,18,20));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        strokeColor(Color(150,152,158)); strokeWidth(1.3f*f);
        for (int t=0;t<=10;++t){ float a=angleFor(t/10.f);
            beginPath(); moveTo(cx+(R+4*f)*std::cos(a),cy+(R+4*f)*std::sin(a)); lineTo(cx+(R+8*f)*std::cos(a),cy+(R+8*f)*std::sin(a)); stroke(); }
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(10*f); fillColor(Color(220,222,226));
        text(cx,cy+R+8*f,k.name,NULL);
    }
    void drawSwitch(int id,float cx,float cy,const char* lbl){
        const float x=W()*cx, y=H()*cy, hs=W()*0.018f, f=scale(); const bool on=fValues[id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); fillColor(on?Color(54,58,54):Color(26,26,28)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); strokeColor(Color(96,98,102)); strokeWidth(1.2f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(150,152,156)); fill();
        if(on){ beginPath(); circle(x,y-hs-4*f,2.2f*f); fillColor(Color(70,235,90)); fill(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(206,208,212)); text(x,y+hs+5*f,lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
public:
    RumbleUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kRumbleDef[i];
        setGeometryConstraints(760*3/5,220*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(10,10,11)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(34,35,38),Color(14,14,16));
        beginPath(); roundedRect(bx,by,bw,bh,8*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,8*f); strokeColor(Color(64,65,69)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // input jack
        beginPath(); circle(w*0.055f,h*0.46f,9*f); fillColor(Color(16,16,18)); fill();
        beginPath(); circle(w*0.055f,h*0.46f,9*f); strokeColor(Color(120,122,126)); strokeWidth(1.5f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(200,202,206)); text(w*0.055f,h*0.46f+13*f,"INPUT",NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        drawSwitch(kBright,0.660f,0.46f,"BRIGHT");
        // panel title (silver script)
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(13*f); fillColor(Color(206,208,214));
        text(bx+16*f,by+10*f,"FUMBLE BASS",NULL);
        // power rocker
        const float px=w*0.93f,py=h*0.46f;
        beginPath(); roundedRect(px-9*f,py-18*f,18*f,36*f,3*f); fillColor(Color(16,16,18)); fill();
        beginPath(); roundedRect(px-9*f,py-18*f,18*f,36*f,3*f); strokeColor(Color(80,82,86)); strokeWidth(1.2f*f); stroke();
        beginPath(); roundedRect(px-6*f,py-16*f,12*f,16*f,2*f); fillColor(Color(176,32,30)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(150,152,156)); text(px,py+20*f,"POWER",NULL);
        // wordmark
        textAlign(ALIGN_RIGHT|ALIGN_BOTTOM); fontSize(22*f); fillColor(Color(232,234,238));
        text(bx+bw-16*f,by+bh-8*f,"Bender",NULL);
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            float hs=W()*0.018f+5; if(std::fabs(ev.pos.getX()-W()*0.660f)<=hs && std::fabs(ev.pos.getY()-H()*0.46f)<=hs){ float nv=fValues[kBright]>0.5f?0.f:1.f; fValues[kBright]=nv; setParameterValue(kBright,nv); repaint(); return true; }
            int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale()); if(fDragVal<0)fDragVal=0; if(fDragVal>1)fDragVal=1; int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleUI)
};

UI* createUI() { return new RumbleUI(); }

END_NAMESPACE_DISTRHO
