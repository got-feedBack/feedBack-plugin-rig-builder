/* Citrus AD200 UI — Orange AD200B style orange tolex head: cream panel with the
 * CITRUS bubble logo + AD200, a black control strip with Power/Standby, the big
 * GAIN knob, an orange tone section (Bass/Middle/Treble), the big MASTER knob and
 * Passive/Active inputs. Knobs vertical-drag; Active toggles on click. */
#include "DistrhoUI.hpp"
#include "CitrusParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,   0.230f, 0.70f, 0.044f, "GAIN" },
    { kBass,   0.395f, 0.70f, 0.030f, "BASS" },
    { kMiddle, 0.475f, 0.70f, 0.030f, "MIDDLE" },
    { kTreble, 0.555f, 0.70f, 0.030f, "TREBLE" },
    { kMaster, 0.690f, 0.70f, 0.044f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));

class CitrusUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/900.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.5f*f); fillColor(Color(180,182,186)); fill();
        beginPath(); circle(cx,cy,R); fillColor(Color(20,20,22)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(52,52,56),Color(14,14,16));
        beginPath(); circle(cx,cy,R-1.5f*f); fillPaint(g); fill();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.12f*std::cos(a),cy+R*0.12f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.6f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9.5f*f); fillColor(Color(28,26,22));
        text(cx,cy+R+6*f,k.name,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    bool activeAt(double px,double py) const { float dx=px-W()*0.880f,dy=py-H()*0.78f; return dx*dx+dy*dy<=180; }
public:
    CitrusUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kCitrusDef[i];
        setGeometryConstraints(900*3/5,300*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        // orange tolex
        beginPath(); rect(0,0,w,h); fillColor(Color(236,118,24)); fill();
        // cream panel
        const float px=.035f*w,py=.07f*h,pw=.930f*w,ph=.86f*h;
        beginPath(); roundedRect(px,py,pw,ph,6*f); fillColor(Color(238,234,222)); fill();
        beginPath(); roundedRect(px,py,pw,ph,6*f); strokeColor(Color(150,146,134)); strokeWidth(1.4f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // logo + model + crest
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(40*f); fillColor(Color(24,22,20));
        text(.085f*w,.255f*h,"CITRUS",NULL);
        fontSize(22*f); fillColor(Color(40,38,34)); text(.560f*w,.255f*h,"AD200",NULL);
        beginPath(); roundedRect(.770f*w,.13f*h,.085f*w,.24f*h,3*f); fillColor(Color(228,224,212)); fill();
        beginPath(); roundedRect(.770f*w,.13f*h,.085f*w,.24f*h,3*f); strokeColor(Color(150,40,40)); strokeWidth(1.2f*f); stroke();
        // black control strip
        const float sx=.06f*w,sy=.55f*h,sw=.88f*w,sh=.36f*h;
        beginPath(); roundedRect(sx,sy,sw,sh,5*f); fillColor(Color(18,18,20)); fill();
        // orange tone section behind Bass/Middle/Treble
        beginPath(); roundedRect(.355f*w,sy+4*f,.245f*w,sh-8*f,4*f); fillColor(Color(232,112,22)); fill();
        // power / standby (left)
        const char* pl[2]={"POWER","STANDBY"};
        for(int i=0;i<2;++i){ float rx=.105f*w+i*.055f*w, ry=.70f*h;
            beginPath(); roundedRect(rx-8*f,ry-14*f,16*f,28*f,2*f); fillColor(Color(40,40,44)); fill();
            beginPath(); roundedRect(rx-8*f,ry-14*f,16*f,28*f,2*f); strokeColor(Color(90,92,96)); strokeWidth(1*f); stroke();
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(206,150,40)); text(rx,ry+18*f,pl[i],NULL); }
        beginPath(); circle(.085f*w,.70f*h,4*f); fillColor(Color(236,140,30)); fill();
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        // Passive / Active inputs (right)
        const float jx=.880f*w;
        for(int i=0;i<2;++i){ float jy=(i?.78f:.62f)*h;
            beginPath(); circle(jx,jy,8*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,jy,8*f); strokeColor(Color(120,122,126)); strokeWidth(1.4f*f); stroke(); }
        const bool act=fValues[kActive]>0.5f;
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(8*f);
        fillColor(act?Color(150,150,154):Color(236,200,120)); text(.905f*w,.62f*h,"PASSIVE",NULL);
        fillColor(act?Color(236,200,120):Color(150,150,154)); text(.905f*w,.78f*h,"ACTIVE",NULL);
    }
    bool onMouse(const MouseEvent& ev) override {
        if(ev.button!=1) return false;
        if(ev.press){
            if(activeAt(ev.pos.getX(),ev.pos.getY())){ float nv=fValues[kActive]>0.5f?0.f:1.f; fValues[kActive]=nv; setParameterValue(kActive,nv); repaint(); return true; }
            int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale()); if(fDragVal<0)fDragVal=0; if(fDragVal>1)fDragVal=1; int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CitrusUI)
};

UI* createUI() { return new CitrusUI(); }

END_NAMESPACE_DISTRHO
