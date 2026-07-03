/* Bender Fumble 800 UI — Fender Rumble 800 style black Class-D head face: input
 * jack, Gain, the Bright/Contour/Vintage voicing buttons (round LEDs), the
 * Overdrive Drive+Level pair (with a clip LED), the 4-band EQ (Bass/Low Mid/
 * High Mid/Treble), Master, a power LED and a "Fumble 800" script. Cream knobs
 * vertical-drag; buttons toggle on click. */
#include "DistrhoUI.hpp"
#include "RumbleParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,    0.115f, 0.48f, 0.038f, "GAIN" },
    { kDrive,   0.305f, 0.48f, 0.038f, "DRIVE" },
    { kLevel,   0.380f, 0.48f, 0.038f, "LEVEL" },
    { kBass,    0.475f, 0.48f, 0.038f, "BASS" },
    { kLowMid,  0.560f, 0.48f, 0.038f, "LOW MID" },
    { kHighMid, 0.645f, 0.48f, 0.038f, "HIGH MID" },
    { kTreble,  0.730f, 0.48f, 0.038f, "TREBLE" },
    { kMaster,  0.845f, 0.48f, 0.038f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs)/sizeof(kKnobs[0]));
struct Btn { int id; float cx, cy; const char* lbl; };
static const Btn kBtns[] = {
    { kBright,  0.190f, 0.28f, "BRIGHT" },
    { kContour, 0.190f, 0.50f, "CONTOUR" },
    { kVintage, 0.190f, 0.72f, "VINTAGE" },
};
static const int kNumBtn = (int)(sizeof(kBtns)/sizeof(kBtns[0]));

class RumbleUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth()/920.0f; }
    static float angleFor(float n){ return (135.0f+n*270.0f)*3.14159265f/180.0f; }

    void drawKnob(const Spot& k){
        const float cx=W()*k.cx, cy=H()*k.cy, R=W()*k.r, f=scale(), n=fValues[k.id];
        beginPath(); circle(cx,cy,R+2.0f*f); fillColor(Color(18,18,20)); fill();
        Paint g=radialGradient(cx-R*0.3f,cy-R*0.35f,R*0.2f,R*1.2f,Color(232,228,214),Color(198,192,174));
        beginPath(); circle(cx,cy,R); fillPaint(g); fill();
        beginPath(); circle(cx,cy,R); strokeColor(Color(120,116,104)); strokeWidth(1.2f*f); stroke();
        const float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.10f*std::cos(a),cy+R*0.10f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a),cy+(R-3*f)*std::sin(a));
        strokeColor(Color(40,38,34)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9.5f*f); fillColor(Color(232,233,236));
        text(cx,cy-R-13*f,k.name,NULL);
    }
    void drawBtn(const Btn& b){
        const float cx=W()*b.cx, cy=H()*b.cy, f=scale(); const bool on=fValues[b.id]>0.5f;
        if(on){ beginPath(); circle(cx,cy,7*f); fillColor(Color(240,180,40,90)); fill(); }
        beginPath(); circle(cx,cy,4.2f*f); fillColor(on?Color(240,170,30):Color(70,66,58)); fill();
        beginPath(); circle(cx,cy,4.2f*f); strokeColor(Color(30,28,26)); strokeWidth(1.0f*f); stroke();
        textAlign(ALIGN_LEFT|ALIGN_MIDDLE); fontSize(8.5f*f); fillColor(Color(228,229,232)); text(cx+9*f,cy,b.lbl,NULL);
    }
    int knobAt(double px,double py) const { for(int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx,dy=py-H()*kKnobs[i].cy,R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int btnAt(double px,double py) const { for(int i=0;i<kNumBtn;++i){ float dx=px-W()*kBtns[i].cx,dy=py-H()*kBtns[i].cy; if(dx*dx+dy*dy<=144) return i; } return -1; }
public:
    RumbleUI() : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for(int i=0;i<kParamCount;++i) fValues[i]=kRumbleDef[i];
        setGeometryConstraints(920*3/5,200*3/5,true,false);
    }
protected:
    void parameterChanged(uint32_t i,float v) override { if(i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(),h=H(),f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(180,182,186)); fill();   // chrome edge
        const float bx=4*f,by=4*f,bw=w-8*f,bh=h-8*f;
        beginPath(); roundedRect(bx,by,bw,bh,5*f); fillColor(Color(20,20,22)); fill();   // black panel
        beginPath(); roundedRect(bx,by,bw,bh,5*f); strokeColor(Color(60,60,64)); strokeWidth(1.2f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // input jack
        beginPath(); circle(w*0.045f,h*0.48f,9*f); fillColor(Color(40,40,44)); fill();
        beginPath(); circle(w*0.045f,h*0.48f,9*f); strokeColor(Color(150,152,156)); strokeWidth(1.5f*f); stroke();
        beginPath(); circle(w*0.045f,h*0.48f,3.5f*f); fillColor(Color(16,16,18)); fill();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(9.5f*f); fillColor(Color(232,233,236)); text(w*0.045f,h*0.48f-22*f,"INPUT",NULL);
        for(int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for(int i=0;i<kNumBtn;++i) drawBtn(kBtns[i]);
        // clip LED above the overdrive pair
        const bool clip=fValues[kDrive]>0.6f; beginPath(); circle(w*0.3425f,h*0.255f,4*f); fillColor(clip?Color(230,40,30):Color(80,28,26)); fill();
        // OVERDRIVE bracket (under Drive+Level)
        const float oy=h*0.74f; strokeColor(Color(150,152,156)); strokeWidth(1.2f*f);
        beginPath(); moveTo(w*0.275f,oy); lineTo(w*0.275f,oy+4*f); lineTo(w*0.410f,oy+4*f); lineTo(w*0.410f,oy); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(210,212,216)); text(w*0.3425f,oy+6*f,"OVERDRIVE",NULL);
        // EQUALIZATION bracket (under Bass..Treble)
        beginPath(); moveTo(w*0.445f,oy); lineTo(w*0.445f,oy+4*f); lineTo(w*0.760f,oy+4*f); lineTo(w*0.760f,oy); stroke();
        text(w*0.6025f,oy+6*f,"EQUALIZATION",NULL);
        // power LED (right)
        beginPath(); circle(w*0.945f,h*0.30f,5*f); fillColor(Color(230,40,30)); fill();
        beginPath(); circle(w*0.945f,h*0.30f,5*f); strokeColor(Color(90,28,26)); strokeWidth(1.0f*f); stroke();
        // script logo
        textAlign(ALIGN_RIGHT|ALIGN_BOTTOM); fontSize(19*f); fillColor(Color(236,237,240));
        text(bx+bw-16*f,by+bh-8*f,"Fumble 800",NULL);
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
