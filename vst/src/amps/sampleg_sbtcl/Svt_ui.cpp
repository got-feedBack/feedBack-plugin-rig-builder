/* Sampleg SBT-CL UI — Ampeg SVT-CL style black tube-head face: input jacks +
 * -15 dB pad, Gain, the Ultra Lo / Ultra Hi push switches, the Bass / Midrange /
 * Frequency / Treble / Master knob row, a chrome SAMPLEG · SBT-CL nameplate and
 * standby/power rockers. Knobs vertical-drag; switches toggle on click. */
#include "DistrhoUI.hpp"
#include "SvtParams.h"
#include <cmath>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; const char* name; };
static const Spot kKnobs[] = {
    { kGain,     0.190f, 0.42f, 0.030f, "GAIN" },
    { kBass,     0.400f, 0.42f, 0.030f, "BASS" },
    { kMidrange, 0.480f, 0.42f, 0.030f, "MIDRANGE" },
    { kFreq,     0.560f, 0.42f, 0.030f, "FREQUENCY" },
    { kTreble,   0.640f, 0.42f, 0.030f, "TREBLE" },
    { kMaster,   0.720f, 0.42f, 0.030f, "MASTER" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));
struct Sw { int id; float cx, cy, h; const char* lbl; };
static const Sw kSwitches[] = {
    { kPad,     0.110f, 0.42f, 0.016f, "-15dB" },
    { kUltraLo, 0.280f, 0.42f, 0.016f, "ULTRA\nLO" },
    { kUltraHi, 0.320f, 0.42f, 0.016f, "ULTRA\nHI" },
};
static const int kNumSw = (int)(sizeof(kSwitches) / sizeof(kSwitches[0]));

class SvtUI : public UI {
    float fValues[kParamCount];
    int fDrag; double fLastY; float fDragVal;
    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth() / 840.0f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(const Spot& k) {
        const float cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r, f = scale(), n = fValues[k.id];
        beginPath(); circle(cx, cy, R + 2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx, cy, R); fillColor(Color(24,24,26)); fill();
        Paint g = radialGradient(cx-R*0.3f, cy-R*0.35f, R*0.2f, R*1.2f, Color(64,65,70), Color(20,20,22));
        beginPath(); circle(cx, cy, R - 1.5f*f); fillPaint(g); fill();
        strokeColor(Color(150,152,158)); strokeWidth(1.4f*f);
        for (int t = 0; t <= 10; ++t) { float a = angleFor(t/10.f);
            beginPath(); moveTo(cx+(R+4*f)*std::cos(a), cy+(R+4*f)*std::sin(a)); lineTo(cx+(R+8*f)*std::cos(a), cy+(R+8*f)*std::sin(a)); stroke(); }
        const float a = angleFor(n);
        beginPath(); moveTo(cx+R*0.15f*std::cos(a), cy+R*0.15f*std::sin(a)); lineTo(cx+(R-3*f)*std::cos(a), cy+(R-3*f)*std::sin(a));
        strokeColor(Color(244,245,248)); strokeWidth(2.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(10*f); fillColor(Color(220,222,226));
        text(cx, cy + R + 7*f, k.name, NULL);
    }
    void drawSwitch(const Sw& s) {
        const float cx = W()*s.cx, cy = H()*s.cy, hs = W()*s.h, f = scale();
        const bool on = fValues[s.id] > 0.5f;
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 3*f); fillColor(on?Color(54,58,54):Color(26,26,28)); fill();
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 3*f); strokeColor(Color(96,98,102)); strokeWidth(1.2f*f); stroke();
        const float ny = on ? cy-hs*0.34f : cy+hs*0.30f;
        beginPath(); roundedRect(cx-hs*0.58f, ny-hs*0.34f, hs*1.16f, hs*0.68f, 2*f); fillColor(Color(150,152,156)); fill();
        if (on) { beginPath(); circle(cx, cy-hs-4*f, 2.2f*f); fillColor(Color(70,235,90)); fill(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(206,208,212));
        const char* l = s.lbl; char line[16]; int li=0; float ty = cy+hs+4*f;
        for (const char* p=l;;++p){ if(*p=='\n'||*p=='\0'){line[li]='\0';text(cx,ty,line,NULL);ty+=9*f;li=0;if(*p=='\0')break;} else if(li<15) line[li++]=*p; }
    }
    int knobAt(double px, double py) const {
        for (int i=0;i<kNumKnobs;++i){ float dx=px-W()*kKnobs[i].cx, dy=py-H()*kKnobs[i].cy, R=W()*kKnobs[i].r+6; if(dx*dx+dy*dy<=R*R) return i; } return -1; }
    int switchAt(double px, double py) const {
        for (int i=0;i<kNumSw;++i){ float hs=W()*kSwitches[i].h+5; if(std::fabs(px-W()*kSwitches[i].cx)<=hs && std::fabs(py-H()*kSwitches[i].cy)<=hs) return i; } return -1; }
public:
    SvtUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i=0;i<kParamCount;++i) fValues[i]=kSvtDef[i];
        setGeometryConstraints(840*3/5, 256*3/5, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)kParamCount){ fValues[i]=v; repaint(); } }
    void onNanoDisplay() override {
        const float w=W(), h=H(), f=scale();
        beginPath(); rect(0,0,w,h); fillColor(Color(12,12,13)); fill();
        const float bx=6*f, by=6*f, bw=w-12*f, bh=h-12*f;
        Paint pn = linearGradient(0,by,0,by+bh, Color(40,41,44), Color(18,18,20));
        beginPath(); roundedRect(bx,by,bw,bh,8*f); fillPaint(pn); fill();
        beginPath(); roundedRect(bx,by,bw,bh,8*f); strokeColor(Color(70,71,75)); strokeWidth(1.5f*f); stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // input jacks
        for (int i=0;i<2;++i){ float jx=w*(0.045f+i*0.035f);
            beginPath(); circle(jx,h*0.42f,8*f); fillColor(Color(16,16,18)); fill();
            beginPath(); circle(jx,h*0.42f,8*f); strokeColor(Color(120,122,126)); strokeWidth(1.5f*f); stroke(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8*f); fillColor(Color(206,208,212));
        text(w*0.062f, h*0.42f+12*f, "INPUTS", NULL);
        for (int i=0;i<kNumKnobs;++i) drawKnob(kKnobs[i]);
        for (int i=0;i<kNumSw;++i) drawSwitch(kSwitches[i]);
        // chrome nameplate
        const float nx=w*0.79f, ny=h*0.30f, nw=w*0.135f, nh=h*0.30f;
        Paint cr = linearGradient(0,ny,0,ny+nh, Color(214,216,220), Color(150,152,158));
        beginPath(); roundedRect(nx,ny,nw,nh,5*f); fillPaint(cr); fill();
        beginPath(); roundedRect(nx,ny,nw,nh,5*f); strokeColor(Color(90,92,96)); strokeWidth(1.4f*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(15*f); fillColor(Color(28,28,30));
        text(nx+nw*0.5f, ny+nh*0.38f, "SAMPLEG", NULL);
        fontSize(11*f); text(nx+nw*0.5f, ny+nh*0.72f, "SBT-CL", NULL);
        // standby + power rockers
        const char* rl[2]={"STANDBY","POWER"};
        for (int i=0;i<2;++i){ float rx=w*0.945f - (1-i)*w*0.030f; float ry=h*0.42f;
            beginPath(); roundedRect(rx-9*f,ry-18*f,18*f,36*f,3*f); fillColor(Color(16,16,18)); fill();
            beginPath(); roundedRect(rx-9*f,ry-18*f,18*f,36*f,3*f); strokeColor(Color(80,82,86)); strokeWidth(1.2f*f); stroke();
            beginPath(); roundedRect(rx-6*f,ry-16*f,12*f,16*f,2*f); fillColor(i?Color(176,32,30):Color(60,62,66)); fill();
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(150,152,156)); text(rx, ry+20*f, rl[i], NULL); }
        // brand wordmark
        textAlign(ALIGN_LEFT|ALIGN_BOTTOM); fontSize(22*f); fillColor(Color(232,234,238));
        text(bx+16*f, by+bh-10*f, "Sampleg", NULL);
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button!=1) return false;
        if (ev.press){ int sw=switchAt(ev.pos.getX(),ev.pos.getY());
            if(sw>=0){ int id=kSwitches[sw].id; float nv=fValues[id]>0.5f?0.f:1.f; fValues[id]=nv; setParameterValue(id,nv); repaint(); return true; }
            int k=knobAt(ev.pos.getX(),ev.pos.getY());
            if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[kKnobs[k].id]; editParameter(kKnobs[k].id,true); return true; }
        } else if(fDrag>=0){ editParameter(kKnobs[fDrag].id,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if(fDrag>=0){ double dy=fLastY-ev.pos.getY(); fLastY=ev.pos.getY(); fDragVal+=(float)dy/(170.0f*scale());
            if(fDragVal<0.f)fDragVal=0.f; if(fDragVal>1.f)fDragVal=1.f;
            int id=kKnobs[fDrag].id; fValues[id]=fDragVal; setParameterValue(id,fDragVal); repaint(); return true; }
        return false;
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvtUI)
};

UI* createUI() { return new SvtUI(); }

END_NAMESPACE_DISTRHO
