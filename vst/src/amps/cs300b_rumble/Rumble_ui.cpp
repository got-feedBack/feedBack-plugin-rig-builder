/* Bender Fumble Bass UI — Fender Rumble Bass (1995) head face, 1:1 look (clone
 * branding): blonde tweed tolex border, oxblood grille cloth, a dark control panel
 * across the top with the two-channel knob row — A VOLUME/TREBLE/BASS/MIDDLE + MID,
 * SELECT, the A/A-B/B input jacks, CHANNEL + MID + power LED, then B VOLUME/TREBLE/
 * BASS/MIDDLE and MIX — a "Bender" script bottom-left and a "Fumble Bass" badge
 * bottom-right. Cream knobs vertical-drag; MID switches toggle on click. */
#include "DistrhoUI.hpp"
#include "RumbleParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const float KY = 0.215f;     // knob-row centre (in the top control panel)
static const float KR = 0.028f;
static const Spot kKnobs[] = {
    { kAVol,    0.070f, KY, KR, "VOLUME" },
    { kATreble, 0.130f, KY, KR, "TREBLE" },
    { kABass,   0.190f, KY, KR, "BASS"   },
    { kAMiddle, 0.250f, KY, KR, "MIDDLE" },
    { kBVol,    0.620f, KY, KR, "VOLUME" },
    { kBTreble, 0.680f, KY, KR, "TREBLE" },
    { kBBass,   0.740f, KY, KR, "BASS"   },
    { kBMiddle, 0.800f, KY, KR, "MIDDLE" },
    { kMix,     0.900f, KY, KR, "MIX"    },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
struct Btn { int id; float cx, cy; const char* lbl; };
static const Btn kBtns[] = {
    { kAMidCut, 0.305f, KY, "MID" },
    { kBMidCut, 0.560f, KY, "MID" },
};
static const int kNumBtn = (int)(sizeof(kBtns)/sizeof(kBtns[0]));

class RumbleUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1000.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        // cream skirted knob with a dark pointer cap
        beginPath(); circle(cx,cy,R+2.2f*f); fillColor(Color(20,16,14)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(238,231,210),Color(200,190,165));
        beginPath(); circle(cx,cy,R); fillPaint(g); fill();
        beginPath(); circle(cx,cy,R); strokeColor(Color(120,112,96)); strokeWidth(1.1f*f); stroke();
        beginPath(); circle(cx,cy,R*0.42f); fillColor(Color(38,32,28)); fill();   // dark centre cap
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.30f*std::cos(a),cy+R*0.30f*std::sin(a)); lineTo(cx+(R-2.5f*f)*std::cos(a),cy+(R-2.5f*f)*std::sin(a));
        strokeColor(Color(236,230,212)); strokeWidth(2.0f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(228,222,206));
        text(cx,cy+R+4*f,k.name,NULL);
    }
    void drawSwitch(const Btn& b){
        const float cx=W()*b.cx, cy=H()*b.cy, f=scale(); const bool on=fValues[b.id]>0.5f;
        // small toggle: bezel + cap, lights amber when CUT engaged
        beginPath(); roundedRect(cx-6*f,cy-9*f,12*f,18*f,2*f); fillColor(Color(24,20,18)); fill();
        beginPath(); roundedRect(cx-4.5f*f,on?cy-7.5f*f:cy+0.5f*f,9*f,7*f,1.5f*f);
        fillColor(on?Color(228,176,70):Color(150,144,132)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(220,214,198));
        text(cx,cy+11*f,b.lbl,NULL);
    }
    void drawJack(float cx,float cy,float f,const char* lbl){
        beginPath(); circle(cx,cy,7*f); fillColor(Color(28,24,22)); fill();
        beginPath(); circle(cx,cy,7*f); strokeColor(Color(150,144,130)); strokeWidth(1.4f*f); stroke();
        beginPath(); circle(cx,cy,2.6f*f); fillColor(Color(14,12,10)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.8f*f); fillColor(Color(214,208,192));
        text(cx,cy+9*f,lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int btnAt(double px,double py) const { for(int i=0;i<kNumBtn;++i){ float dx=px-W()*kBtns[i].cx,dy=py-H()*kBtns[i].cy; if(std::fabs(dx)<=12&&std::fabs(dy)<=14) return i; } return -1; }
public:
    RumbleUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kRumbleDef[i];
        setGeometryConstraints(1000*3/5,440*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        // blonde tweed tolex border
        beginPath(); rect(0,0,w,h); fillColor(Color(196,176,130)); fill();
        for(int i=0;i<(int)(h/ (6*f)); ++i){ beginPath(); rect(0,i*6*f,w,2.0f*f); fillColor(Color(176,156,112,60)); fill(); }
        // oxblood grille cloth (lower body)
        const float gx=10*f, gy=h*0.34f, gw=w-20*f, gh=h-gy-10*f;
        beginPath(); roundedRect(gx,gy,gw,gh,6*f); fillColor(Color(74,30,34)); fill();
        for(int i=0;i<(int)(gw/(5*f)); ++i){ beginPath(); rect(gx+i*5*f,gy,1.4f*f,gh); fillColor(Color(40,16,20,90)); fill(); }
        beginPath(); roundedRect(gx,gy,gw,gh,6*f); strokeColor(Color(150,134,98)); strokeWidth(2.0f*f); stroke();
        // dark control panel across the top
        const float px=10*f, py=8*f, pw=w-20*f, ph=h*0.30f;
        beginPath(); roundedRect(px,py,pw,ph,5*f); fillColor(Color(46,38,32)); fill();
        Paint pg=linearGradient(px,py,px,py+ph,Color(64,54,46),Color(36,30,26));
        beginPath(); roundedRect(px,py,pw,ph,5*f); fillPaint(pg); fill();
        beginPath(); roundedRect(px,py,pw,ph,5*f); strokeColor(Color(96,84,68)); strokeWidth(1.4f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // channel labels
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.0f*f); fillColor(Color(206,200,184));
        text(w*0.160f, py+4*f, "CHANNEL A", NULL);
        text(w*0.710f, py+4*f, "CHANNEL B", NULL);
        // knobs + switches
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumBtn;++i) drawSwitch(kBtns[i]);
        // SELECT + CHANNEL legend switches (decorative routing, drawn for the 1:1 look)
        drawSwitch_static(w*0.350f, h*KY, f, "SELECT");
        drawSwitch_static(w*0.515f, h*KY, f, "CHAN");
        // input jacks
        drawJack(w*0.410f, h*KY, f, "A IN");
        drawJack(w*0.450f, h*KY, f, "A/B");
        drawJack(w*0.490f, h*KY, f, "B IN");
        // power / channel LED
        beginPath(); circle(w*0.585f, h*(KY-0.085f), 4.0f*f); fillColor(Color(232,46,32)); fill();
        beginPath(); circle(w*0.585f, h*(KY-0.085f), 4.0f*f); strokeColor(Color(96,28,24)); strokeWidth(1.0f*f); stroke();
        // "Bender" script (bottom-left, on the grille) — clone of the Fender script
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(30*f); fillColor(Color(238,232,214));
        text(gx+18*f, h-18*f, "Bender", NULL);
        // "Fumble Bass" badge (bottom-right)
        textAlign(ALIGN_RIGHT|ALIGN_BOTTOM); fontSize(15*f); fillColor(Color(228,220,200));
        text(gx+gw-16*f, h-22*f, "Fumble", NULL);
        fontSize(13*f); text(gx+gw-16*f, h-8*f, "Bass", NULL);
    }
    void drawSwitch_static(float cx,float cy,float f,const char* lbl){
        beginPath(); roundedRect(cx-6*f,cy-9*f,12*f,18*f,2*f); fillColor(Color(24,20,18)); fill();
        beginPath(); roundedRect(cx-4.5f*f,cy-3.5f*f,9*f,7*f,1.5f*f); fillColor(Color(150,144,132)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.0f*f); fillColor(Color(214,208,192)); text(cx,cy+11*f,lbl,NULL);
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            int b=btnAt(ev.pos.getX(),ev.pos.getY());
            if(b>=0){ int id=kBtns[b].id; float nv=fValues[id]>0.5f?0.f:1.f; fValues[id]=nv; setParameterValue(id,nv); repaint(); return true; }
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
