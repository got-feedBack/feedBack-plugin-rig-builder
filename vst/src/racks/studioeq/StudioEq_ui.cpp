/* Studio EQ — GML 8200 faithful custom UI (DPF NanoVG).
 *
 * 5 band columns, each: a Cut/Boost knob on top and a CONCENTRIC Freq(outer
 * ring)/Q(inner knob) control below — exactly the 8200's per-band layout (Q is
 * mounted concentrically with Freq). On the Low and High bands the Q knob at its
 * CCW detent (≤0.03) switches the band to SHELF (shown with a shelf glyph). */
#include "StudioEqParams.h"
#include "DistrhoUI.hpp"
#include <cmath>

START_NAMESPACE_DISTRHO

static const char* const kBandLbl[5]    = { "LOW", "LO MID", "MID", "HI MID", "HIGH" };
static const bool        kBandShelf[5]  = { true, false, false, false, true };

class StudioEqUI : public UI
{
    float  fValues[kNumParams];
    int    fDrag;       // param index being dragged, -1 = none
    double fLastY;
    float  fDragVal;

    static constexpr float UIW = 820.0f;
    float sc() const { return getWidth() / UIW; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }
    static float d2(double px, double py, float cx, float cy) { const double dx=px-cx, dy=py-cy; return (float)(dx*dx+dy*dy); }

    // ── layout ────────────────────────────────────────────────────────────
    float earW()      const { return getWidth() * 0.045f; }
    float colW()      const { return (getWidth() - 2*earW() - 20*sc()) / 5.0f; }
    float colX(int b) const { return earW() + 10*sc() + (b + 0.5f) * colW(); }
    float gainCy()    const { return getHeight() * 0.36f; }
    float gainR()     const { return getWidth() * 0.027f; }
    float fqCy()      const { return getHeight() * 0.71f; }
    float fqRo()      const { return getWidth() * 0.040f; }
    float fqRi()      const { return getWidth() * 0.0225f; }

    // ── primitives ────────────────────────────────────────────────────────
    void valueRing(float cx, float cy, float R, float n, Color c, float f) {
        beginPath();
        for (int s = 0; s <= 32; ++s) { float t=n*s/32.f, a=angleFor(t); float x=cx+R*std::cos(a), y=cy+R*std::sin(a); if(s==0)moveTo(x,y); else lineTo(x,y); }
        strokeColor(c); strokeWidth(2.5f*f); stroke();
    }
    void pointer(float cx, float cy, float r0, float r1, float n, Color c, float f) {
        const float a = angleFor(n);
        beginPath(); moveTo(cx+r0*std::cos(a), cy+r0*std::sin(a)); lineTo(cx+r1*std::cos(a), cy+r1*std::sin(a));
        strokeColor(c); strokeWidth(2.3f*f); stroke();
    }
    void drawGain(int b) {
        const float f=sc(), cx=colX(b), cy=gainCy(), R=gainR(), n=fValues[b*3+0];
        beginPath(); circle(cx,cy,R);     fillColor(Color(30,31,36)); fill();
        beginPath(); circle(cx,cy,R-2*f); fillColor(Color(58,61,70)); fill();
        valueRing(cx,cy,R+3*f,n,Color(20,22,26),f);
        pointer(cx,cy,0,R-3*f,n,Color(238,240,245),f);
        textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(7.5f*f); fillColor(Color(225,225,230));
        text(cx,cy-R-4*f,"CUT / BOOST",NULL);
    }
    void drawFQ(int b) {
        const float f=sc(), cx=colX(b), cy=fqCy(), ro=fqRo(), ri=fqRi();
        const float fr=fValues[b*3+1], q=fValues[b*3+2];
        const bool shelf = kBandShelf[b] && (q <= 0.03f);
        // outer ring = FREQ
        beginPath(); circle(cx,cy,ro);     fillColor(Color(26,27,32)); fill();
        beginPath(); circle(cx,cy,ro);     strokeColor(Color(70,72,80)); strokeWidth(1.5f*f); stroke();
        valueRing(cx,cy,ro+3*f,fr,Color(168,30,120),f);
        pointer(cx,cy,ro-7*f,ro-2*f,fr,Color(232,234,240),f);
        // inner knob = Q (or SHELF on Low/High)
        beginPath(); circle(cx,cy,ri);     fillColor(shelf?Color(38,58,80):Color(54,57,66)); fill();
        beginPath(); circle(cx,cy,ri);     strokeColor(Color(20,22,26)); strokeWidth(1.5f*f); stroke();
        if (shelf) {
            beginPath(); moveTo(cx-ri*0.55f,cy+ri*0.32f); lineTo(cx,cy+ri*0.32f); lineTo(cx,cy-ri*0.32f); lineTo(cx+ri*0.55f,cy-ri*0.32f);
            strokeColor(Color(150,200,240)); strokeWidth(1.8f*f); stroke();
        } else {
            pointer(cx,cy,0,ri-2*f,q,Color(238,240,245),f);
        }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(200,200,206));
        text(cx,cy+ro+4*f,"FREQ · Q",NULL);
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(120,255,130));
        text(cx,getHeight()*0.90f,kBandLbl[b],NULL);
    }
    void screws(float x, float w, float H, float f) {
        const float r=5*f; fillColor(Color(150,152,158));
        for (float yy : { H*0.16f, H*0.84f }) {
            beginPath(); circle(x+w*0.5f, yy, r); fill();
            beginPath(); moveTo(x+w*0.5f-r*0.7f,yy); lineTo(x+w*0.5f+r*0.7f,yy); strokeColor(Color(60,62,66)); strokeWidth(1.5f*f); stroke();
        }
    }
    int hitTest(double px, double py) {
        const float f=sc();
        for (int b=0;b<5;++b) {
            if (d2(px,py,colX(b),gainCy()) <= (gainR()+6*f)*(gainR()+6*f)) return b*3+0;
            const float di = d2(px,py,colX(b),fqCy());
            if (di <= (fqRi()+5*f)*(fqRi()+5*f)) return b*3+2;   // Q (inner)
            if (di <= (fqRo()+6*f)*(fqRo()+6*f)) return b*3+1;   // Freq (outer ring)
        }
        return -1;
    }

public:
    StudioEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i=0;i<kNumParams;++i) fValues[i] = kSeqDef[i];
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH*3/4, DISTRHO_UI_DEFAULT_HEIGHT*3/4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kNumParams) { fValues[i]=v; repaint(); } }

    void onNanoDisplay() override {
        const float W=getWidth(), H=getHeight(), f=sc();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        Paint face = linearGradient(0,0,0,H,Color(58,60,66),Color(34,35,40));
        beginPath(); rect(0,0,W,H); fillPaint(face); fill();
        beginPath(); rect(0,0,W,H); strokeColor(Color(12,13,15)); strokeWidth(3*f); stroke();
        const float ew=earW();
        for (float ex : { 0.0f, W-ew }) {
            beginPath(); rect(ex,0,ew,H); fillColor(Color(26,27,31)); fill();
            screws(ex,ew,H,f);
        }
        // title strip
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(13*f); fillColor(Color(120,255,130));
        text(W*0.5f, H*0.045f, "PARAMETRIC EQUALIZER", NULL);
        textAlign(ALIGN_RIGHT|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(110,112,120));
        text(W-ew-8*f, H*0.05f, "RIG BUILDER", NULL);
        for (int b=0;b<5;++b) { drawGain(b); drawFQ(b); }
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) { const int k=hitTest(ev.pos.getX(),ev.pos.getY()); if(k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[k]; editParameter(k,true); return true; } }
        else if (fDrag>=0) { editParameter(fDrag,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag<0) return false;
        const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
        fDragVal += (float)dy / (170.0f * sc());
        if (fDragVal<0.f) fDragVal=0.f; if (fDragVal>1.f) fDragVal=1.f;
        fValues[fDrag]=fDragVal; setParameterValue(fDrag,fDragVal); repaint();
        return true;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioEqUI)
};

UI* createUI() { return new StudioEqUI(); }

END_NAMESPACE_DISTRHO
