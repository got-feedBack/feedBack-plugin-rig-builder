/*
 * Amp EQ UI (DPF NanoVG): 3 columns (Bass / Mid / Treble). Each column has the
 * main tone pot (top) and its corner-shift knob (bottom: BassFreq / MidShift /
 * TrebleFreq). Rotary knobs, vertical drag. Matches the other bundled EQ pedals.
 */
#include "DistrhoUI.hpp"
#include "AmpEqParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const struct { int idx; int col; int row; } kKnobs[aNumParams] = {
    { aBass,   0, 0 }, { aBassFreq,   0, 1 },
    { aMid,    1, 0 }, { aMidShift,   1, 1 },
    { aTreble, 2, 0 }, { aTrebleFreq, 2, 1 },
};
static const char* const kColHdr[3] = { "BASS", "MID", "TREBLE" };

class AmpEqUI : public UI
{
    float fValues[aNumParams];
    int   fDrag;
    double fLastY;
    float fDragVal;

    float scale() const { return getWidth() / 380.0f; }
    float knobR() const { return getWidth() * 0.085f; }
    float colX(int c) const { return getWidth() * (0.22f + 0.28f * c); }
    float rowY(int r) const { const float ys[2] = { 0.40f, 0.74f }; return getHeight() * ys[r]; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    bool isPot(int idx) const { return idx == aBass || idx == aMid || idx == aTreble; }
    void valueText(int idx, float v, char* b, size_t n) const {
        if (isPot(idx)) std::snprintf(b, n, "%.1f", v * 10.0f);          // amp-dial 0..10
        else std::snprintf(b, n, "%.2fx", 1.0f / aeqCapMul(v));          // corner shift, stock = 1.00x
    }
    const char* subLabel(int k) const {
        const int idx = kKnobs[k].idx;
        switch (idx) { case aBassFreq: return "Freq"; case aMidShift: return "Shift"; case aTrebleFreq: return "Freq"; }
        return "Level";
    }
    void drawKnob(int k) {
        const int idx = kKnobs[k].idx;
        const float cx = colX(kKnobs[k].col), cy = rowY(kKnobs[k].row), R = knobR(), f = scale(), n = fValues[idx];
        beginPath(); circle(cx, cy, R);       fillColor(Color(44, 46, 58)); fill();
        beginPath(); circle(cx, cy, R - 3*f); fillColor(Color(64, 68, 84)); fill();
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n*s/36.f, a = angleFor(t); float x = cx + (R-3*f)*std::cos(a), y = cy + (R-3*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(230, 170, 90)); strokeWidth(4*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-9*f)*std::cos(a), cy + (R-9*f)*std::sin(a));
        strokeColor(Color(244, 240, 230)); strokeWidth(3*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(10*f); fillColor(Color(155, 160, 175)); text(cx, cy + R + 3*f, subLabel(k), NULL);
        char buf[24]; valueText(idx, n, buf, sizeof(buf));
        fontSize(11*f); fillColor(Color(225, 205, 170)); text(cx, cy + R + 16*f, buf, NULL);
    }
    int knobAt(double px, double py) const {
        const float R = knobR();
        for (int k = 0; k < aNumParams; ++k) {
            const float cx = colX(kKnobs[k].col), cy = rowY(kKnobs[k].row), dx = px - cx, dy = py - cy;
            if (dx*dx + dy*dy <= (R+6)*(R+6)) return k;
        }
        return -1;
    }
public:
    AmpEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < aNumParams; ++i) fValues[i] = 0.5f;
        setGeometryConstraints(300, 240, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)aNumParams) { fValues[i] = v; repaint(); } }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale(), m = 10*f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0,0,W,H); fillColor(Color(20,18,16)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); fillColor(Color(40,33,28)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); strokeColor(Color(210,150,80)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(20*f); fillColor(Color(240,200,140)); text(22*f, 16*f, AEQ_PLUGIN_LABEL, NULL);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fontSize(9*f); fillColor(Color(150,135,115)); text(W-22*f, 22*f, "Bassman tone stack", NULL);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (int c = 0; c < 3; ++c) { fontSize(12*f); fillColor(Color(200,170,130)); text(colX(c), H*0.24f, kColHdr[c], NULL); }
        for (int k = 0; k < aNumParams; ++k) drawKnob(k);
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) { const int k = knobAt(ev.pos.getX(), ev.pos.getY()); if (k >= 0) { fDrag = k; fLastY = ev.pos.getY(); fDragVal = fValues[kKnobs[k].idx]; editParameter(kKnobs[k].idx, true); return true; } }
        else if (fDrag >= 0) { editParameter(kKnobs[fDrag].idx, false); fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) {
            const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
            fDragVal += (float)dy / (170.0f * scale());
            if (fDragVal < 0.f) fDragVal = 0.f; if (fDragVal > 1.f) fDragVal = 1.f;
            const int idx = kKnobs[fDrag].idx; fValues[idx] = fDragVal; setParameterValue(idx, fDragVal); repaint();
            return true;
        }
        return false;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpEqUI)
};

UI* createUI() { return new AmpEqUI(); }

END_NAMESPACE_DISTRHO
