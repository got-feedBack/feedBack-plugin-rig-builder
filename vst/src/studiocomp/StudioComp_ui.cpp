/*
 * Studio Comp UI (DPF NanoVG): 5 knobs in a row — Threshold / Ratio / Attack /
 * Release / Output. Rotary, vertical drag. dbx-style dark/blue panel.
 */
#include "DistrhoUI.hpp"
#include "StudioCompParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const char* const kHdr[cNumParams] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE", "OUTPUT" };

class StudioCompUI : public UI
{
    float fValues[cNumParams];
    int   fDrag;
    double fLastY;
    float fDragVal;

    float scale() const { return getWidth() / 560.0f; }
    float knobR() const { return getWidth() * 0.058f; }
    float colX(int c) const { return getWidth() * (0.12f + 0.19f * c); }
    float rowY()    const { return getHeight() * 0.52f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void valueText(int idx, float v, char* b, size_t n) const {
        switch (idx) {
            case cThreshold: std::snprintf(b, n, "%.1f dB", scThresholdDb(v)); break;
            case cRatio:     std::snprintf(b, n, "%.1f:1", scRatio(v)); break;
            case cAttack:    std::snprintf(b, n, "%.0f ms", scAttackMs(v)); break;
            case cRelease:   std::snprintf(b, n, "%.0f ms", scReleaseMs(v)); break;
            case cOutput:    std::snprintf(b, n, "%+.1f dB", scOutputDb(v)); break;
            default:         std::snprintf(b, n, "%.2f", v);
        }
    }
    void drawKnob(int idx) {
        const float cx = colX(idx), cy = rowY(), R = knobR(), f = scale(), n = fValues[idx];
        beginPath(); circle(cx, cy, R);       fillColor(Color(40, 44, 52)); fill();
        beginPath(); circle(cx, cy, R - 3*f); fillColor(Color(58, 64, 76)); fill();
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n*s/36.f, a = angleFor(t); float x = cx + (R-3*f)*std::cos(a), y = cy + (R-3*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(90, 180, 230)); strokeWidth(4*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-9*f)*std::cos(a), cy + (R-9*f)*std::sin(a));
        strokeColor(Color(235, 240, 248)); strokeWidth(3*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(11*f); fillColor(Color(160, 175, 195)); text(cx, cy - R - 16*f, kHdr[idx], NULL);
        char buf[24]; valueText(idx, n, buf, sizeof(buf));
        fontSize(11*f); fillColor(Color(180, 215, 240)); text(cx, cy + R + 6*f, buf, NULL);
    }
    int knobAt(double px, double py) const {
        const float R = knobR(), cy = rowY();
        for (int i = 0; i < cNumParams; ++i) {
            const float cx = colX(i), dx = px - cx, dy = py - cy;
            if (dx*dx + dy*dy <= (R+6)*(R+6)) return i;
        }
        return -1;
    }
public:
    StudioCompUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        fValues[cThreshold]=0.5f; fValues[cRatio]=0.1818f; fValues[cAttack]=0.1333f; fValues[cRelease]=0.2083f; fValues[cOutput]=0.3333f;
        setGeometryConstraints(440, 200, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)cNumParams) { fValues[i] = v; repaint(); } }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale(), m = 10*f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0,0,W,H); fillColor(Color(16,18,22)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); fillColor(Color(28,32,40)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); strokeColor(Color(70,150,210)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(20*f); fillColor(Color(150,200,240)); text(24*f, 16*f, SC_PLUGIN_LABEL, NULL);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fontSize(9*f); fillColor(Color(120,140,160)); text(W-24*f, 22*f, "dbx 160 true-RMS VCA", NULL);
        for (int i = 0; i < cNumParams; ++i) drawKnob(i);
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) { const int k = knobAt(ev.pos.getX(), ev.pos.getY()); if (k >= 0) { fDrag = k; fLastY = ev.pos.getY(); fDragVal = fValues[k]; editParameter(k, true); return true; } }
        else if (fDrag >= 0) { editParameter(fDrag, false); fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) {
            const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
            fDragVal += (float)dy / (170.0f * scale());
            if (fDragVal < 0.f) fDragVal = 0.f; if (fDragVal > 1.f) fDragVal = 1.f;
            fValues[fDrag] = fDragVal; setParameterValue(fDrag, fDragVal); repaint();
            return true;
        }
        return false;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioCompUI)
};

UI* createUI() { return new StudioCompUI(); }

END_NAMESPACE_DISTRHO
