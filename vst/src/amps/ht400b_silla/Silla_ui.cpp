/* Silla Boogie 400 UI — Mesa/Boogie Bass 400+ style black rack face: the 6-band
 * graphic EQ (40/100/250/625/1560/3900 vertical faders) with EQ In/Out, the
 * Middle/Bass/Treble/Master/Volume 2/Volume 1 knob row with pull Bright/Shift
 * switches, input jacks and power/standby rockers. Knobs+faders vertical-drag;
 * switches toggle on click. */
#include "DistrhoUI.hpp"
#include "SillaParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kMiddle, 0.300f, 0.74f, 0.030f, "MIDDLE" },
    { kBass,   0.375f, 0.74f, 0.030f, "BASS" },
    { kTreble, 0.450f, 0.74f, 0.030f, "TREBLE" },
    { kMaster, 0.545f, 0.74f, 0.030f, "MASTER" },
    { kVol2,   0.650f, 0.74f, 0.030f, "VOL 2" },
    { kVol1,   0.725f, 0.74f, 0.030f, "VOL 1" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
static const char* const kEqLbl[kNumEq] = {"40","100","250","625","1560","3900"};
struct Sw { int id; float cx, cy; const char* lbl; };
static const Sw kSwitches[] = {
    { kEqIn,      0.660f, 0.30f, "EQ IN" },
    { kBassShift, 0.375f, 0.92f, "SHIFT" },
    { kTrebShift, 0.450f, 0.92f, "SHIFT" },
    { kBright2,   0.650f, 0.92f, "BRIGHT" },
    { kBright1,   0.725f, 0.92f, "BRIGHT" },
};
static const int kNumSw = (int)(sizeof(kSwitches)/sizeof(kSwitches[0]));

class SillaUI : public UI {
    float fValues[kParamCount];
    int fDrag, fFader; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/1000.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }
    float eqX(int i) const { return W()*(0.405f + i*0.037f); }
    float eqY0() const { return H()*0.14f; }
    float eqY1() const { return H()*0.50f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(120,122,126)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(54,55,60),Color(16,16,18));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(206,208,212));
        text(cx,cy+R+5*f,k.name,NULL);
    }
    void drawFader(int i){
        const float x=eqX(i), y0=eqY0(), y1=eqY1(), f=scale(), n=fValues[kFirstEq+i];
        beginPath(); roundedRect(x-2.5f*f,y0,5*f,y1-y0,2.5f*f); fillColor(Color(18,18,20)); fill();
        const float cy=y1-n*(y1-y0);
        beginPath(); roundedRect(x-8*f,cy-6*f,16*f,12*f,2*f); fillColor(Color(58,60,64)); fill();
        beginPath(); moveTo(x-6*f,cy); lineTo(x+6*f,cy); strokeColor(Color(228,230,234)); strokeWidth(1.3f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7*f); fillColor(Color(190,192,196));
        text(x,y1+11*f,kEqLbl[i],NULL);
    }
    void drawSwitch(const Sw& s){
        const float x=W()*s.cx, y=H()*s.cy, hs=W()*0.011f, f=scale(); const bool on=fValues[s.id]>0.5f;
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); fillColor(on?Color(54,58,54):Color(26,26,28)); fill();
        beginPath(); roundedRect(x-hs,y-hs,hs*2,hs*2,3*f); strokeColor(Color(96,98,102)); strokeWidth(1.1f*f); stroke();
        const float ny=on?y-hs*0.34f:y+hs*0.30f;
        beginPath(); roundedRect(x-hs*0.58f,ny-hs*0.34f,hs*1.16f,hs*0.68f,2*f); fillColor(Color(150,152,156)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(190,192,196)); text(x,y+hs+3*f,s.lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int faderAt(double px,double py) const { for(int i=0;i<kNumEq;++i){ if(std::fabs(px-eqX(i))<=11 && py>=eqY0()-12 && py<=eqY1()+12) return i; } return -1; }
    int switchAt(double px,double py) const { for(int i=0;i<kNumSw;++i){ float hs=W()*0.011f+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    SillaUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fFader(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kSillaDef[i];
        setGeometryConstraints(1000*3/5,300*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(8,8,9)); fill();
        const float bx=6*f,by=6*f,bw=w-12*f,bh=h-12*f;
        Paint pn=linearGradient(0,by,0,by+bh,Color(26,26,28),Color(12,12,14));
        beginPath(); roundedRect(bx,by,bw,bh,7*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,7*f); strokeColor(Color(150,140,90)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // title
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(18*f); fillColor(Color(220,222,226));
        text(bx+16*f,by+10*f,"SILLA / BOOGIE",NULL);
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(15*f); fillColor(Color(200,180,120));
        text(bx+16*f,by+32*f,"BASS 400+",NULL);
        // EQ frame
        beginPath(); roundedRect(eqX(0)-16*f, eqY0()-10*f, (eqX(kNumEq-1)-eqX(0))+32*f, (eqY1()-eqY0())+34*f, 4*f);
        strokeColor(Color(150,140,90)); strokeWidth(1.2f*f); stroke();
        for(int i=0;i<kNumEq;++i) drawFader(i);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // power + standby rockers (left)
        const char* rl[2]={"POWER","STANDBY"};
        for (int i=0;i<2;++i){ float rx=w*(0.085f+i*0.06f), ry=h*0.74f;
            beginPath(); roundedRect(rx-8*f,ry-13*f,16*f,26*f,3*f); fillColor(Color(16,16,18)); fill();
            beginPath(); roundedRect(rx-8*f,ry-13*f,16*f,26*f,3*f); strokeColor(Color(80,82,86)); strokeWidth(1.1f*f); stroke();
            beginPath(); roundedRect(rx-6*f,ry-11*f,12*f,12*f,2*f); fillColor(Color(60,62,66)); fill();
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(6.5f*f); fillColor(Color(170,172,176)); text(rx,ry+16*f,rl[i],NULL); }
        // input jacks (right)
        for (int j=0;j<2;++j){ float jx=w*0.82f, jy=h*(0.66f+j*0.16f);
            beginPath(); circle(jx,jy,7*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,jy,7*f); strokeColor(Color(120,122,126)); strokeWidth(1.3f*f); stroke(); }
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(7*f); fillColor(Color(190,192,196));
        text(w*0.835f,h*0.66f,"IN 1",NULL); text(w*0.835f,h*0.82f,"IN 2",NULL);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SillaUI)
};

UI* createUI() { return new SillaUI(); }

END_NAMESPACE_DISTRHO
