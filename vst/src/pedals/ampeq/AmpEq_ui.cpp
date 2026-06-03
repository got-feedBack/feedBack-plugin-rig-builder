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
        beginPath(); rect(0,0,W,H); fillColor(Color(14,13,11)); fill();
        // Fender tweed body (tan → brown), like the real Bassman
        Paint body = linearGradient(0, m, 0, H-m, Color(206,180,120), Color(120,92,52));
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); fillPaint(body); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); strokeColor(Color(92,64,34)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(20*f); fillColor(Color(54,36,18)); text(22*f, 14*f, AEQ_PLUGIN_LABEL, NULL);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fontSize(9*f); fillColor(Color(82,58,34)); text(W-22*f, 20*f, "Bassman tone stack", NULL);
        // column headers on the tweed (dark text), above the knob panel
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (int c = 0; c < 3; ++c) { fontSize(12*f); fillColor(Color(58,40,20)); text(colX(c), H*0.235f, kColHdr[c], NULL); }
        // recessed control panel so the knob labels stay legible
        const float pT = H*0.28f, pB = H*0.94f, pX = W*0.05f, pW = W*0.90f;
        beginPath(); roundedRect(pX, pT, pW, pB-pT, 12*f); fillColor(Color(28,26,24)); fill();
        beginPath(); roundedRect(pX, pT, pW, pB-pT, 12*f); strokeColor(Color(0,0,0,120)); strokeWidth(1.5f*f); stroke();
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
