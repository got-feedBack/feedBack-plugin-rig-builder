/* Marsten DBS 7400 UI — silver rack face: Hi/Lo input, Gain + Primary EQ
 * (Bass/Middle/Treble) + Compression + Volume knobs, a 7-band graphic EQ
 * (vertical faders), Bright/Deep/Lo-In switches and a power rocker. Knobs +
 * faders vertical-drag; switches toggle on click. (The in-app canvas face in
 * pedal_canvas.js is what the host normally shows; this is the native editor.) */
#include "DistrhoUI.hpp"
#include "DbsParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,         0.060f, 0.40f, 0.026f, "GAIN" },
    { kBlend,        0.130f, 0.40f, 0.026f, "BLEND" },
    { kLo,           0.250f, 0.40f, 0.026f, "LO" },
    { kHi,           0.315f, 0.40f, 0.026f, "HI" },
    { kDepth,        0.430f, 0.40f, 0.026f, "DEPTH" },
    { kGraphicLevel, 0.840f, 0.40f, 0.026f, "EQ LVL" },
    { kVolume,       0.930f, 0.40f, 0.030f, "VOLUME" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
static const char* const kEqLbl[kNumEq] = {"50","80","160","320","640","1k25","2k5","5k","8k"};

class DbsUI : public UI {
    float fValues[kParamCount];
    int fDrag, fFader; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/960.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }
    float eqX(int i) const { return W()*(0.430f + i*0.030f); }
    float eqY0() const { return H()*0.24f; }
    float eqY1() const { return H()*0.56f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(24,24,26)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(64,65,70),Color(20,20,22));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.15f*std::cos(a),cy+R*0.15f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9*f); fillColor(Color(30,30,34));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawFader(int i){
        const float x=eqX(i), y0=eqY0(), y1=eqY1(), f=scale(), n=fValues[kFirstEq+i];
        beginPath(); roundedRect(x-2.5f*f,y0,5*f,y1-y0,2.5f*f); fillColor(Color(18,18,20)); fill();
        const float cy=y1-n*(y1-y0);
        beginPath(); roundedRect(x-9*f,cy-6*f,18*f,12*f,2*f); fillColor(Color(60,62,66)); fill();
        beginPath(); roundedRect(x-9*f,cy-6*f,18*f,12*f,2*f); strokeColor(Color(20,20,22)); strokeWidth(1*f); stroke();
        beginPath(); moveTo(x-7*f,cy); lineTo(x+7*f,cy); strokeColor(Color(228,230,234)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7.5f*f); fillColor(Color(40,40,44));
        text(x,y0-3*f,kEqLbl[i],NULL);
    }
    void drawSwitch(int id,float cx,float cy,const char* lbl){
        const float x=W()*cx, y=H()*cy, hs=W()*0.014f, f=scale(); const bool on=fValues[id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); fillColor(on?Color(54,58,54):Color(26,26,28)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); strokeColor(Color(96,98,102)); strokeWidth(1.2f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(150,152,156)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(30,30,34)); text(x,y+hs+4*f,lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int faderAt(double px,double py) const { for(int i=0;i<kNumEq;++i){ if(std::fabs(px-eqX(i))<=12 && py>=eqY0()-12 && py<=eqY1()+12) return i; } return -1; }
    struct S{int id;float cx,cy;};
    void switches(S* s) const { s[0]={kBright,0.055f,0.66f}; s[1]={kDeep,0.055f,0.82f}; s[2]={kGraphicOn,0.560f,0.74f}; s[3]={kLoInput,0.110f,0.74f}; }
public:
    DbsUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fFader(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kDbsDef[i];
        setGeometryConstraints(960*3/5,300*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(14,14,15)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(204,206,210),Color(166,168,174));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(110,112,118)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // inputs
        for(int i=0;i<2;++i){ float jx=w*0.055f, jy=h*(0.30f+i*0.16f);
            beginPath(); circle(jx,jy,7*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,jy,7*f); strokeColor(Color(90,92,98)); strokeWidth(1.4f*f); stroke(); }
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(7.5f*f); fillColor(Color(30,30,34));
        text(w*0.072f,h*0.30f,"HI",NULL); text(w*0.072f,h*0.46f,"LO",NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumEq;++i) drawFader(i);
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(8*f); fillColor(Color(40,40,44));
        text(W()*0.50f, eqY0()-13*f, "GRAPHIC EQUALIZER", NULL);
        S sw[4]; switches(sw); const char* lbl[4]={"BRIGHT","DEEP","GRAPHIC","LO IN"};
        for(int i=0;i<4;++i) drawSwitch(sw[i].id, sw[i].cx, sw[i].cy, lbl[i]);
        // power rocker
        const float px=w*0.965f,py=h*0.74f;
        beginPath(); roundedRect(px-10*f,py-14*f,20*f,28*f,3*f); fillColor(Color(18,18,20)); fill();
        beginPath(); roundedRect(px-7*f,py-12*f,14*f,12*f,2*f); fillColor(Color(176,32,30)); fill();
        // wordmark
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(20*f); fillColor(Color(28,28,32));
        text(bx+16*f,by+bh-10*f,"Marsten",NULL);
        textAlign(ALIGN_RIGHT|ALIGN_BOTTOM); fontSize(10*f); fillColor(Color(70,72,78));
        text(bx+bw-16*f,by+bh-10*f,"DBS 7400  400 WATTS",NULL);
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            S sw[4]; switches(sw);
            for(int i=0;i<4;++i){ float hs=W()*0.014f+5; if(std::fabs(ev.pos.getX()-W()*sw[i].cx)<=hs && std::fabs(ev.pos.getY()-H()*sw[i].cy)<=hs){ float nv=fValues[sw[i].id]>0.5f?0.f:1.f; fValues[sw[i].id]=nv; setParameterValue(sw[i].id,nv); repaint(); return true; } }
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DbsUI)
};

UI* createUI() { return new DbsUI(); }

END_NAMESPACE_DISTRHO
