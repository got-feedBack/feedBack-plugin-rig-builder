/* Studio Graphic EQ — API 550L faithful custom UI (DPF NanoVG).
 *
 * ONE row of 4 bands (LF · LMF · HMF · HF). Each band is a CONCENTRIC knob like
 * the real 550L: a WHITE knob = BOOST/CUT (gain), with a smaller BLUE-edged knob
 * on top = FREQUENCY. The selected frequency is printed (in blue) under the knob.
 * LF and HF have a PEAK/SHELF toggle. Both rings detent to the API steps
 * (7 freqs / 11 gain detents). */
#include "SGEqParams.h"
#include "DistrhoUI.hpp"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const char* const kBand[4]   = { "LF", "LMF", "HMF", "HF" };
static const int kGainIdx[4]  = { gBass, gLoMid, gHiMid, gTreble };
static const int kFreqIdx[4]  = { gBassFreq, gLoMidFreq, gHiMidFreq, gTrebleFreq };
static const int kShelfIdx[4] = { gBassShelf, -1, -1, gTrebleShelf };

static float bandFreq(int b, float v) {
    switch (b) { case 0: return sgFLF(v); case 1: return sgFLMF(v); case 2: return sgFHMF(v); default: return sgFHF(v); }
}
static void fmtFreq(float hz, char* out, int n) {
    if (hz >= 1000.0f) { float k=hz/1000.0f; if (k==(int)k) std::snprintf(out,n,"%dk",(int)k); else std::snprintf(out,n,"%.1fk",k); }
    else std::snprintf(out,n,"%d Hz",(int)(hz+0.5f));
}

class SGEqUI : public UI
{
    float  fValues[gNumParams];
    int    fDrag;
    double fLastY;
    float  fDragVal;

    static constexpr float UIW = 720.0f;
    float sc() const { return getWidth() / UIW; }
    static float angleFor(float n) { return (135.0f + n*270.0f) * 3.14159265f/180.0f; }
    static float d2(double px,double py,float cx,float cy){ const double dx=px-cx,dy=py-cy; return (float)(dx*dx+dy*dy); }
    static int steps(int idx) {
        if (idx==gBass||idx==gLoMid||idx==gHiMid||idx==gTreble) return 11;
        if (idx==gBassFreq||idx==gLoMidFreq||idx==gHiMidFreq||idx==gTrebleFreq) return 7;
        return 0;
    }

    float earW()      const { return getWidth()*0.05f; }
    float colW()      const { return (getWidth()-2*earW()-24*sc())/4.0f; }
    float colX(int b) const { return earW()+12*sc() + (b+0.5f)*colW(); }
    float rowCy()     const { return getHeight()*0.44f; }
    float gainR()     const { return getWidth()*0.046f; }   // outer white = gain
    float freqR()     const { return getWidth()*0.026f; }   // inner blue = freq
    void  shelfRect(int b, float& x, float& y, float& w, float& h) const {
        w = colW()*0.55f; h = getHeight()*0.10f; x = colX(b)-w*0.5f; y = getHeight()*0.84f;
    }

    void pointer(float cx,float cy,float r0,float r1,float n,Color c,float f){
        const float a=angleFor(n);
        beginPath(); moveTo(cx+r0*std::cos(a),cy+r0*std::sin(a)); lineTo(cx+r1*std::cos(a),cy+r1*std::sin(a));
        strokeColor(c); strokeWidth(2.4f*f); stroke();
    }
    int hitTest(double px,double py){
        const float f=sc();
        for (int b=0;b<4;++b){
            float x,y,w,h; shelfRect(b,x,y,w,h);
            if (kShelfIdx[b]>=0 && px>=x&&px<=x+w&&py>=y&&py<=y+h) return 1000+b;
            const float di=d2(px,py,colX(b),rowCy());
            if (di<=(freqR()+5*f)*(freqR()+5*f)) return kFreqIdx[b];     // inner = freq
            if (di<=(gainR()+6*f)*(gainR()+6*f)) return kGainIdx[b];     // outer = gain
        }
        return -1;
    }

public:
    SGEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i=0;i<gNumParams;++i) fValues[i]=kSgDef[i];
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH*3/4, DISTRHO_UI_DEFAULT_HEIGHT*3/4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i<(uint32_t)gNumParams){ fValues[i]=v; repaint(); } }

    void onNanoDisplay() override {
        const float W=getWidth(), H=getHeight(), f=sc();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        Paint face = linearGradient(0,0,0,H,Color(40,42,48),Color(18,19,23));
        beginPath(); rect(0,0,W,H); fillPaint(face); fill();
        beginPath(); rect(0,0,W,H); strokeColor(Color(10,11,13)); strokeWidth(3*f); stroke();
        const float ew=earW();
        for (float ex : { 0.0f, W-ew }) { beginPath(); rect(ex,0,ew,H); fillColor(Color(15,16,19)); fill(); }
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(13*f); fillColor(Color(90,170,235));
        text(W*0.5f, H*0.05f, "DISCRETE 4-BAND EQ", NULL);
        textAlign(ALIGN_RIGHT|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(110,112,120)); text(W-ew-8*f,H*0.06f,"RIG BUILDER",NULL);

        char buf[14];
        for (int b=0;b<4;++b) {
            const float cx=colX(b), cy=rowCy(), Rg=gainR(), Rf=freqR();
            const float gv=fValues[kGainIdx[b]], fv=fValues[kFreqIdx[b]];
            // OUTER white knob = BOOST/CUT (gain)
            beginPath(); circle(cx,cy,Rg);     fillColor(Color(30,31,36)); fill();
            beginPath(); circle(cx,cy,Rg-2*f); fillColor(Color(236,238,242)); fill();   // white cap
            beginPath();
            for (int s=0;s<=32;++s){float t=gv*s/32.f,a=angleFor(t);float x=cx+(Rg+3*f)*std::cos(a),y=cy+(Rg+3*f)*std::sin(a);if(s==0)moveTo(x,y);else lineTo(x,y);}
            strokeColor(Color(210,212,218)); strokeWidth(2.5f*f); stroke();            // white value ring
            pointer(cx,cy,Rf+3*f,Rg-3*f,gv,Color(40,44,52),f);                          // dark pointer on white
            // INNER blue knob = FREQUENCY (on top)
            beginPath(); circle(cx,cy,Rf);     fillColor(Color(26,30,40)); fill();
            beginPath(); circle(cx,cy,Rf);     strokeColor(Color(60,140,220)); strokeWidth(2.2f*f); stroke();   // blue edge
            pointer(cx,cy,0,Rf-3*f,fv,Color(120,200,255),f);                            // blue pointer
            // FREQUENCY value (blue) under the knob
            fmtFreq(bandFreq(b,fv), buf, sizeof(buf));
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(11*f); fillColor(Color(120,205,255));
            text(cx, cy+Rg+5*f, buf, NULL);
            // gain dB + band label
            const int gdb=(int)sgGainDb(gv);
            std::snprintf(buf,sizeof(buf), "%+d dB", gdb);
            textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(8.5f*f); fillColor(Color(228,230,235));
            text(cx, cy+Rg+5*f+13*f, buf, NULL);
            textAlign(ALIGN_CENTER|ALIGN_BOTTOM); fontSize(9*f); fillColor(Color(90,170,235));
            text(cx, cy-Rg-5*f, kBand[b], NULL);
            // PEAK/SHELF toggle (LF/HF)
            if (kShelfIdx[b]>=0) {
                float x,y,w,h; shelfRect(b,x,y,w,h);
                const bool shelf=sgIsShelf(fValues[kShelfIdx[b]]);
                beginPath(); roundedRect(x,y,w,h,3*f); fillColor(shelf?Color(30,70,110):Color(34,36,42)); fill();
                beginPath(); roundedRect(x,y,w,h,3*f); strokeColor(Color(70,72,80)); strokeWidth(1.2f*f); stroke();
                textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fontSize(8*f); fillColor(shelf?Color(150,205,245):Color(150,152,160));
                text(x+w*0.5f, y+h*0.5f, shelf?"SHELF":"PEAK", NULL);
            }
        }
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int k = hitTest(ev.pos.getX(), ev.pos.getY());
            if (k >= 1000) { const int idx=kShelfIdx[k-1000]; const float nv=sgIsShelf(fValues[idx])?0.0f:1.0f;
                fValues[idx]=nv; editParameter(idx,true); setParameterValue(idx,nv); editParameter(idx,false); repaint(); return true; }
            if (k >= 0) { fDrag=k; fLastY=ev.pos.getY(); fDragVal=fValues[k]; editParameter(k,true); return true; }
        } else if (fDrag>=0) { editParameter(fDrag,false); fDrag=-1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag<0) return false;
        const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
        fDragVal += (float)dy / (170.0f * sc());
        if (fDragVal<0.f) fDragVal=0.f; if (fDragVal>1.f) fDragVal=1.f;
        float v = fDragVal;
        const int st = steps(fDrag);
        if (st>1) v = (float)((int)(v*(st-1)+0.5f))/(st-1);
        fValues[fDrag]=v; setParameterValue(fDrag,v); repaint();
        return true;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SGEqUI)
};

UI* createUI() { return new SGEqUI(); }

END_NAMESPACE_DISTRHO
