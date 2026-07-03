/* Peabey T-Max UI — Peavey T-Max "Two Channel Bass System" white rack face:
 * Input + Active/Passive, the Tube Pre/Post + Solid-State Pre gains (with TUBE/
 * SOLID-STATE/CLIP LEDs), Channel Select + Combine switches, Shelving High/Low,
 * a 7-band graphic EQ (40/100/250/625/1.6k/4k/10k vertical faders) with Graphic
 * In/Out, Balance, X-Over, Master and a power rocker. Knobs + faders vertical-
 * drag; switches toggle on click. */
#include "DistrhoUI.hpp"
#include "TMaxParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kTubePre,   0.100f, 0.46f, 0.026f, "TUBE PRE" },
    { kTubePost,  0.165f, 0.46f, 0.026f, "TUBE POST" },
    { kSsPre,     0.240f, 0.46f, 0.026f, "SS PRE" },
    { kShelfHigh, 0.335f, 0.31f, 0.024f, "SH HIGH" },
    { kShelfLow,  0.335f, 0.66f, 0.024f, "SH LOW" },
    { kBalance,   0.745f, 0.46f, 0.026f, "BALANCE" },
    { kXover,     0.815f, 0.46f, 0.026f, "X-OVER" },
    { kMaster,    0.885f, 0.46f, 0.026f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
static const char* const kEqLbl[kNumEq] = {"40","100","250","625","1.6k","4k","10k"};
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kActive,    0.038f, 0.66f, "ACTIVE" },
    { kChanSel,   0.300f, 0.30f, "TUBE/SS" },
    { kChanCombine,0.300f, 0.63f, "COMBINE" },
    { kGraphicIn, 0.405f, 0.30f, "GRAPHIC" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class TMaxUI : public UI {
    float fValues[kParamCount];
    int fDrag, fFader; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1100.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }
    float eqX(int i) const { return W()*(0.435f + i*0.032f); }
    float eqY0() const { return H()*0.24f; }
    float eqY1() const { return H()*0.60f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(120,122,128)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(24,24,26)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(64,65,70),Color(20,20,22));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.15f*std::cos(a),cy+R*0.15f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(30,32,38));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawFader(int i){
        const float x=eqX(i), y0=eqY0(), y1=eqY1(), f=scale(), n=fValues[kFirstEq+i];
        beginPath(); roundedRect(x-2.5f*f,y0,5*f,y1-y0,2.5f*f); fillColor(Color(40,42,46)); fill();
        const float cy=y1-n*(y1-y0);
        beginPath(); roundedRect(x-9*f,cy-6*f,18*f,12*f,2*f); fillColor(Color(60,62,66)); fill();
        beginPath(); roundedRect(x-9*f,cy-6*f,18*f,12*f,2*f); strokeColor(Color(20,20,22)); strokeWidth(1*f); stroke();
        beginPath(); moveTo(x-7*f,cy); lineTo(x+7*f,cy); strokeColor(Color(228,230,234)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7.5f*f); fillColor(Color(40,42,48));
        text(x,y0-3*f,kEqLbl[i],NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.013f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); fillColor(on?Color(70,74,82):Color(40,42,46)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); strokeColor(Color(90,92,98)); strokeWidth(1.2f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(170,172,176)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(30,32,38)); text(x,y+hs+4*f,s.lbl,NULL);
    }
    void led(float cx,float cy,Color c){ const float f=scale();
        beginPath(); circle(W()*cx,H()*cy,3.2f*f); fillColor(c); fill();
        beginPath(); circle(W()*cx,H()*cy,3.2f*f); strokeColor(Color(40,40,44)); strokeWidth(0.8f*f); stroke(); }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int faderAt(double px,double py) const { for(int i=0;i<kNumEq;++i){ if(std::fabs(px-eqX(i))<=12 && py>=eqY0()-12 && py<=eqY1()+12) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.013f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    TMaxUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fFader(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kTMaxDef[i];
        setGeometryConstraints(1100*3/5,280*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(20,22,26)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(236,239,243),Color(206,212,220));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(120,124,130)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // title
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(20*f); fillColor(Color(24,26,32));
        text(bx+14*f,by+8*f,"T-MINUS",NULL);
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(8*f); fillColor(Color(70,74,82));
        text(bx+14*f,by+30*f,"TWO CHANNEL BASS SYSTEM",NULL);
        // input jack
        beginPath(); circle(w*0.038f,h*0.45f,8*f); fillColor(Color(16,16,18)); fill();
        beginPath(); circle(w*0.038f,h*0.45f,8*f); strokeColor(Color(90,92,98)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(30,32,38)); text(w*0.038f,h*0.45f+11*f,"INPUT",NULL);
        // channel LEDs: TUBE green, SOLID STATE yellow, CLIP red
        led(0.135f,0.27f,Color(60,210,80)); textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(7.5f*f); fillColor(Color(30,32,38)); text(w*0.150f,h*0.27f,"TUBE",NULL);
        led(0.235f,0.27f,Color(220,200,40)); text(w*0.250f,h*0.27f,"SOLID STATE",NULL);
        led(0.250f,0.66f,Color(210,40,36)); text(w*0.262f,h*0.66f,"CLIP",NULL);
        // shelving bracket label
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(8*f); fillColor(Color(60,64,72));
        text(w*0.300f,h*0.46f,"SHELVING",NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumEq;++i) drawFader(i);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // graphic-eq scale endpoints
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7*f); fillColor(Color(60,64,72));
        text((eqX(0)+eqX(kNumEq-1))*0.5f, eqY0()-13*f, "GRAPHIC EQ  (+/-15 dB)", NULL);
        // power rocker
        const float px=w*0.955f,py=h*0.55f;
        beginPath(); roundedRect(px-10*f,py-16*f,20*f,32*f,3*f); fillColor(Color(18,18,20)); fill();
        beginPath(); roundedRect(px-7*f,py-14*f,14*f,13*f,2*f); fillColor(Color(60,62,66)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(40,42,48)); text(px,py+18*f,"POWER",NULL);
        // wordmark
        textAlign(ALIGN_RIGHT|ALIGN_BOTTOM); fontSize(20*f); fillColor(Color(26,28,34));
        text(bx+bw-16*f,by+bh-8*f,"PeeBee",NULL);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TMaxUI)
};

UI* createUI() { return new TMaxUI(); }

END_NAMESPACE_DISTRHO
