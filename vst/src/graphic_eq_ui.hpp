/*
 * Shared graphic-EQ UI (DPF NanoVG), portrait, same footprint as the chief
 * pedals. Two looks via EQ_STYLE:
 *   0 — Boss GE-7/GEB-7: coloured body, dark fader plate, body-colour treadle
 *       footswitch with the engraved name.
 *   1 — Mesa-style 5-band: black body, a light recessed fader channel, a white
 *       nameplate at the bottom, no treadle.
 * Each band is a vertical fader. Frequency printed ABOVE each fader; a +EQ_DB/0/
 * -EQ_DB scale + faint horizontal grid lines span the fader area. Copyright-free.
 *
 * Per-pedal Bands.h: kEqBands/kEqFreqs/kEqNames/EQ_PLUGIN_LABEL/EQ_DB + body
 * colour EQ_ACR/EQ_ACG/EQ_ACB; optional EQ_NAME1/EQ_NAME2 (two-word treadle name).
 */
#include "DistrhoUI.hpp"
#include "_shared/fonts_data.hpp"
#include <cmath>
#include <cstdio>

#ifndef EQ_ACR
#define EQ_ACR 190
#define EQ_ACG 192
#define EQ_ACB 188
#endif
#ifndef EQ_STYLE
#define EQ_STYLE 0
#endif

START_NAMESPACE_DISTRHO

class GraphicEqUI : public UI
{
    float fValues[kEqBands];
    int   fDrag;
    bool  fEditing = false;
    int   fName = -1, fLbl = -1;

    float scale()  const { return getWidth() / (float)DISTRHO_UI_DEFAULT_WIDTH; }
    // centred fader plate (Boss = portrait/tall; Mesa = landscape/wide)
    float plateX() const { return getWidth()  * (EQ_STYLE==1 ? 0.100f : 0.085f); }
    float plateW() const { return getWidth()  * (EQ_STYLE==1 ? 0.800f : 0.830f); }
    float plateY() const { return getHeight() * (EQ_STYLE==1 ? 0.170f : 0.135f); }
    float plateH() const { return getHeight() * (EQ_STYLE==1 ? 0.440f : 0.305f); }
    // fader columns sit to the right of the dB scale, inside the plate
    float faderL() const { return plateX() + getWidth() * 0.085f; }
    float faderW() const { return plateW() - getWidth() * 0.105f; }
    float colW()   const { return faderW() / (float)kEqBands; }
    float colX(int i) const { return faderL() + (i + 0.5f) * colW(); }
    float trackTop()    const { return plateY() + getHeight() * (EQ_STYLE==1 ? 0.095f : 0.072f); }
    float trackBottom() const { return plateY() + plateH() - getHeight() * (EQ_STYLE==1 ? 0.055f : 0.038f); }

    float valToY(float v) const { return trackTop() + (1.0f - v) * (trackBottom() - trackTop()); }
    float yToVal(double y) const {
        float v = 1.0f - (float)((y - trackTop()) / (trackBottom() - trackTop()));
        return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    }
    int colAt(double px, double py) const {
        if (py < plateY()-8 || py > plateY() + plateH()+8) return -1;
        int i = (int)((px - faderL() + colW()*0.5f) / colW() - 0.5f + 0.5f);
        i = (int)((px - faderL()) / colW());
        return (i >= 0 && i < kEqBands) ? i : -1;
    }
    Color bodyText() const {
        const float lum = 0.299f*EQ_ACR + 0.587f*EQ_ACG + 0.114f*EQ_ACB;
        return lum > 140.f ? Color(34, 34, 38) : Color(232, 234, 240);
    }
    static int cl(int v){ return v<0?0:(v>255?255:v); }
public:
    GraphicEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1) {
        loadSharedResources();
        fName = createFontFromMemory("pk_serif",  pk_serif_ttf,  pk_serif_ttf_len,  false);
        fLbl  = createFontFromMemory("pk_barlow", pk_barlow_ttf, pk_barlow_ttf_len, false);
        for (int i = 0; i < kEqBands; ++i) fValues[i] = 0.5f;
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH*3/4, DISTRHO_UI_DEFAULT_HEIGHT*3/4, true, false);
    }
private:
    void engrave(float cx, float cy, float size, const char* s, Color c) {
        fontFaceId(fName >= 0 ? fName : fLbl); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(size*scale()); fillColor(c); text(getWidth()*cx, getHeight()*cy, s, NULL);
    }
    // small decorative rotary knob (Mesa Output/Input level — cosmetic)
    void miniKnob(float cx, float cy, const char* lab) {
        const float f = scale(), R = getWidth()*0.050f;
        beginPath(); circle(cx, cy, R*1.12f); fillColor(Color(13,13,15)); fill();
        Paint cap = radialGradient(cx-R*0.3f, cy-R*0.4f, R*0.1f, R*1.1f, Color(56,57,62), Color(22,22,25));
        beginPath(); circle(cx, cy, R); fillPaint(cap); fill();
        beginPath(); circle(cx, cy, R); strokeColor(Color(0,0,0,160)); strokeWidth(1.2f*f); stroke();
        const float a = (135.f + 0.6f*270.f) * 3.14159265f/180.f;
        beginPath(); moveTo(cx, cy); lineTo(cx+R*0.78f*std::cos(a), cy+R*0.78f*std::sin(a));
        strokeColor(Color(220,222,226)); strokeWidth(2*f); stroke();
        fontFaceId(fLbl); textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7*f); fillColor(Color(186,188,194));
        text(cx, cy+R*1.35f, lab, NULL);
    }
protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < (uint32_t)kEqBands) { fValues[index] = value; repaint(); }
    }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale(), m = 7*f;
        const int R = EQ_ACR, G = EQ_ACG, B = EQ_ACB;
        const bool mesa = (EQ_STYLE == 1);
        beginPath(); rect(0, 0, W, H); fillColor(Color(10, 10, 12)); fill();
        Paint body = linearGradient(0, m, 0, H-m, Color(cl(R+16),cl(G+16),cl(B+16)), Color(cl(R-12),cl(G-12),cl(B-12)));
        beginPath(); roundedRect(m, m, W-2*m, H-2*m, 12*f); fillPaint(body); fill();
        beginPath(); roundedRect(m, m, W-2*m, H-2*m, 12*f); strokeColor(Color(0,0,0,120)); strokeWidth(2*f); stroke();
        const Color tc = bodyText();

        // top label + LED
        fontFaceId(fLbl); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fontSize(10.5f*f); fillColor(tc);
        text(W*0.10f, H*0.075f, "GRAPHIC  EQUALIZER", NULL);
        beginPath(); circle(W*0.90f, H*0.075f, 4.5f*f); fillColor(Color(224,70,58)); fill();

        // fader plate
        beginPath(); roundedRect(plateX(), plateY(), plateW(), plateH(), 7*f);
        fillColor(mesa ? Color(15,15,17) : Color(20,20,22)); fill();
        beginPath(); roundedRect(plateX(), plateY(), plateW(), plateH(), 7*f);
        strokeColor(mesa ? Color(78,80,86,150) : Color(0,0,0,140)); strokeWidth(1.4f*f); stroke();

        const float tT = trackTop(), tB = trackBottom(), midY = (tT + tB) * 0.5f;
        // dB scale (left)
        char dbt[8], dbb[8]; std::snprintf(dbt,sizeof(dbt),"+%.0f",(double)EQ_DB); std::snprintf(dbb,sizeof(dbb),"-%.0f",(double)EQ_DB);
        fontFaceId(fLbl); fontSize(8*f); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(mesa ? Color(180,182,188) : Color(160,162,170));
        text(faderL() - W*0.045f, tT,   dbt, NULL);
        text(faderL() - W*0.045f, midY, "0", NULL);
        text(faderL() - W*0.045f, tB,   dbb, NULL);
        // Boss: faint white horizontal grid lines (Mesa uses its white slots instead)
        if (!mesa) for (int s = 0; s <= 4; ++s) {
            const float yy = tT + (tB - tT) * s / 4.f;
            beginPath(); moveTo(faderL() - W*0.04f, yy); lineTo(faderL() + faderW(), yy);
            strokeColor(Color(255,255,255,40)); strokeWidth(1.0f*f); stroke();
        }

        for (int i = 0; i < kEqBands; ++i) {
            const float cx = colX(i), hy = valToY(fValues[i]);
            fontFaceId(fLbl); textAlign(ALIGN_CENTER | ALIGN_TOP); fontSize(8*f);
            fillColor(mesa ? Color(200,202,208) : Color(182,184,192));
            text(cx, plateY() + 4*f, kEqNames[i], NULL);   // freq inside the plate top
            if (mesa) {
                // white vertical slot + chunky black cap
                beginPath(); roundedRect(cx - 3.5f*f, tT, 7*f, tB - tT, 3.5f*f); fillColor(Color(226,227,231)); fill();
                beginPath(); roundedRect(cx - 11*f, hy - 7*f, 22*f, 14*f, 3*f); fillColor(Color(22,22,24)); fill();
                beginPath(); roundedRect(cx - 11*f, hy - 7*f, 22*f, 14*f, 3*f); strokeColor(Color(0,0,0,170)); strokeWidth(1*f); stroke();
                beginPath(); rect(cx - 9*f, hy - 0.8f*f, 18*f, 1.8f*f); fillColor(Color(150,152,158)); fill();
            } else {
                beginPath(); roundedRect(cx - 2.0f*f, tT, 4.0f*f, tB - tT, 2.0f*f); fillColor(Color(46,48,56)); fill();
                Paint cp = linearGradient(0, hy-6*f, 0, hy+6*f, Color(236,238,242), Color(150,153,160));
                beginPath(); roundedRect(cx - 10*f, hy - 6*f, 20*f, 12*f, 2.5f*f); fillPaint(cp); fill();
                beginPath(); roundedRect(cx - 10*f, hy - 6*f, 20*f, 12*f, 2.5f*f); strokeColor(Color(70,72,78)); strokeWidth(1*f); stroke();
                beginPath(); rect(cx - 8*f, hy-0.6f*f, 16*f, 1.4f*f); fillColor(Color(60,62,68)); fill();
            }
        }

        if (mesa) {
            // white nameplate + footswitch at the bottom (no rotary knobs)
            beginPath(); roundedRect(W*0.30f, H*0.66f, W*0.40f, H*0.135f, 5*f); fillColor(Color(236,237,240)); fill();
            engrave(0.5f, 0.727f, 22, EQ_PLUGIN_LABEL, Color(20,20,24));
            beginPath(); circle(W*0.5f, H*0.905f, 15*f); fillColor(Color(150,153,159)); fill();
            beginPath(); circle(W*0.5f, H*0.905f, 15*f); strokeColor(Color(90,92,98)); strokeWidth(2*f); stroke();
        } else {
            const float tx = m+4*f, tw = W-2*m-8*f, tyTop = H*0.49f, tBot = H - m - 6*f;
            Paint tre = linearGradient(0, tyTop, 0, tBot, Color(cl(R-2),cl(G-2),cl(B-2)), Color(cl(R-14),cl(G-14),cl(B-14)));
            beginPath(); roundedRect(tx, tyTop, tw, tBot - tyTop, 12*f); fillPaint(tre); fill();
            beginPath(); roundedRect(tx, tyTop, tw, 10*f, 12*f); fillColor(Color(255,255,255,20)); fill();
            beginPath(); roundedRect(tx, tyTop, tw, tBot - tyTop, 12*f); strokeColor(Color(0,0,0,120)); strokeWidth(1.6f*f); stroke();
            // black step pad = LOWER HALF of the treadle
            const float padT = tyTop + (tBot - tyTop) * 0.50f;
            beginPath(); roundedRect(tx+12*f, padT, tw-24*f, tBot - padT - 9*f, 9*f); fillColor(Color(20,20,22)); fill();
#if defined(EQ_NAME1) && defined(EQ_NAME2)
            engrave(0.30f, 0.575f, 42, EQ_NAME1, Color(16,16,20));
            engrave(0.61f, 0.675f, 34, EQ_NAME2, Color(16,16,20));
#else
            engrave(0.5f, 0.625f, 40, EQ_PLUGIN_LABEL, Color(16,16,20));
#endif
        }
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int i = colAt(ev.pos.getX(), ev.pos.getY());
            if (i >= 0) { fDrag = i; setFromY(i, ev.pos.getY()); return true; }
        } else if (fDrag >= 0) { editParameter(fDrag, false); fEditing = false; fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) { setFromY(fDrag, ev.pos.getY()); return true; }
        return false;
    }
private:
    void setFromY(int i, double y) {
        if (!fEditing) { editParameter(i, true); fEditing = true; }
        const float v = yToVal(y);
        fValues[i] = v; setParameterValue(i, v); repaint();
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphicEqUI)
};

UI* createUI() { return new GraphicEqUI(); }

END_NAMESPACE_DISTRHO
