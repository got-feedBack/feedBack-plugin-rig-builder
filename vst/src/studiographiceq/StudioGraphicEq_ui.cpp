/*
 * Studio Graphic EQ UI (DPF NanoVG): 5 band columns (Bass/Lo Mid/Mid/Hi Mid/
 * Treble), each Gain + Freq knob (no Q — proportional). Rotary, vertical drag.
 */
#include "DistrhoUI.hpp"
#include "SGEqParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const struct { int idx; int col; int row; } kKnobs[gNumParams] = {
    { gBass, 0, 0 }, { gBassFreq, 0, 1 },
    { gLoMid, 1, 0 }, { gLoMidFreq, 1, 1 },
    { gMid, 2, 0 }, { gMidFreq, 2, 1 },
    { gHiMid, 3, 0 }, { gHiMidFreq, 3, 1 },
    { gTreble, 4, 0 }, { gTrebleFreq, 4, 1 },
};
static const char* const kColHdr[5] = { "BASS", "LO MID", "MID", "HI MID", "TREBLE" };

class SGEqUI : public UI
{
    float fValues[gNumParams];
    int fDrag; double fLastY; float fDragVal;
    float scale() const { return getWidth() / 560.0f; }
    float knobR() const { return getWidth() * 0.052f; }
    float colX(int c) const { return getWidth() * (0.13f + 0.185f * c); }
    float rowY(int r) const { const float ys[2] = { 0.42f, 0.74f }; return getHeight() * ys[r]; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    float freqFor(int idx, float v) const {
        switch (idx) { case gBassFreq: return sgFBass(v); case gLoMidFreq: return sgFLoMid(v);
            case gMidFreq: return sgFMid(v); case gHiMidFreq: return sgFHiMid(v); case gTrebleFreq: return sgFTreble(v); }
        return 0.f;
    }
    bool isFreq(int idx) const { return idx==gBassFreq||idx==gLoMidFreq||idx==gMidFreq||idx==gHiMidFreq||idx==gTrebleFreq; }
    void valueText(int idx, float v, char* b, size_t n) const {
        if (isFreq(idx)) { float f = freqFor(idx, v); if (f >= 1000.f) std::snprintf(b,n,"%.1fk",f/1000.f); else std::snprintf(b,n,"%.0f Hz",f); }
        else std::snprintf(b, n, "%+.1f dB", sgDb(v));
    }
    void drawKnob(int k) {
        const int idx = kKnobs[k].idx;
        const float cx = colX(kKnobs[k].col), cy = rowY(kKnobs[k].row), R = knobR(), f = scale(), n = fValues[idx];
        beginPath(); circle(cx, cy, R);       fillColor(Color(44,46,58)); fill();
        beginPath(); circle(cx, cy, R-3*f);   fillColor(Color(64,68,84)); fill();
        beginPath();
        for (int s=0;s<=36;++s){float t=n*s/36.f,a=angleFor(t);float x=cx+(R-3*f)*std::cos(a),y=cy+(R-3*f)*std::sin(a);if(s==0)moveTo(x,y);else lineTo(x,y);}
        strokeColor(Color(110,170,240)); strokeWidth(4*f); stroke();
        const float a=angleFor(n);
        beginPath(); moveTo(cx,cy); lineTo(cx+(R-8*f)*std::cos(a), cy+(R-8*f)*std::sin(a)); strokeColor(Color(230,238,252)); strokeWidth(3*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP);
        fontSize(10*f); fillColor(Color(150,160,180)); text(cx, cy+R+3*f, kKnobs[k].row==0?"Gain":"Freq", NULL);
        char buf[24]; valueText(idx,n,buf,sizeof(buf));
        fontSize(10*f); fillColor(Color(180,200,230)); text(cx, cy+R+15*f, buf, NULL);
    }
    int knobAt(double px,double py) const {
        const float R=knobR();
        for (int k=0;k<gNumParams;++k){const float cx=colX(kKnobs[k].col),cy=rowY(kKnobs[k].row),dx=px-cx,dy=py-cy;if(dx*dx+dy*dy<=(R+6)*(R+6))return k;}
        return -1;
    }
public:
    SGEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i=0;i<gNumParams;++i) fValues[i]=0.5f;
        setGeometryConstraints(440, 240, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)gNumParams){fValues[i]=v;repaint();} }
    void onNanoDisplay() override {
        const float W=getWidth(),H=getHeight(),f=scale(),m=10*f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0,0,W,H); fillColor(Color(18,18,22)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); fillColor(Color(30,33,42)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); strokeColor(Color(90,150,230)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT|ALIGN_TOP); fontSize(20*f); fillColor(Color(170,200,255)); text(22*f,16*f,SG_PLUGIN_LABEL,NULL);
        textAlign(ALIGN_CENTER|ALIGN_TOP);
        for (int c=0;c<5;++c){fontSize(11*f); fillColor(Color(150,160,185)); text(colX(c),H*0.26f,kColHdr[c],NULL);}
        for (int k=0;k<gNumParams;++k) drawKnob(k);
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button!=1) return false;
        if (ev.press){const int k=knobAt(ev.pos.getX(),ev.pos.getY());if(k>=0){fDrag=k;fLastY=ev.pos.getY();fDragVal=fValues[kKnobs[k].idx];editParameter(kKnobs[k].idx,true);return true;}}
        else if (fDrag>=0){editParameter(kKnobs[fDrag].idx,false);fDrag=-1;return true;}
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag>=0){const double dy=fLastY-ev.pos.getY();fLastY=ev.pos.getY();fDragVal+=(float)dy/(170.0f*scale());if(fDragVal<0)fDragVal=0;if(fDragVal>1)fDragVal=1;const int idx=kKnobs[fDrag].idx;fValues[idx]=fDragVal;setParameterValue(idx,fDragVal);repaint();return true;}
        return false;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SGEqUI)
};

UI* createUI() { return new SGEqUI(); }

END_NAMESPACE_DISTRHO
