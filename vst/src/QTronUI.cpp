/*
 * AutoSweep — DPF NanoVG UI, stompbox style. Responsive; rotary knobs (drag
 * vertically), labels, values, LED. 8 knobs (3 / 3 / 2).
 */
#include "DistrhoUI.hpp"
#include "QTronParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const char* const kQTronDesc[kParamCount] = {
    "Filter type", "Envelope attack", "Envelope release", "Sweep range",
    "Resonance", "Dry / wet", "Sensitivity", "Output boost",
};

// Visible knobs: only the ones Rocksmith drives (Mode/Peak/Gain/Attack/Release)
// plus Mix. Range + Boost still exist as params (fixed via the preset state) but
// have no knob. Grid: fractional X (0..1 of width) and row (0..1).
static const int kKnobN = 6;
static const struct { int idx; float fx; int row; } kLayout[kKnobN] = {
    { kMode,   0.25f, 0 }, { kPeak,    0.50f, 0 }, { kGain, 0.75f, 0 },
    { kAttack, 0.25f, 1 }, { kRelease, 0.50f, 1 }, { kMix,  0.75f, 1 },
};

class QTronUI : public UI
{
    float  fValues[kParamCount];
    int    fDrag;
    double fLastY;
    float  fDragValue;

    float scale()  const { return getWidth() / 340.0f; }
    float knobR()  const { return getWidth() * 0.092f; }
    float rowY(int r) const { const float ys[2] = { 0.42f, 0.76f }; return getHeight() * ys[r]; }
    void knobCenter(int k, float& cx, float& cy) const { cx = getWidth() * kLayout[k].fx; cy = rowY(kLayout[k].row); }

    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    float norm(int i) const {
        float n = (fValues[i] - kQTronMin[i]) / (kQTronMax[i] - kQTronMin[i]);
        return n < 0.f ? 0.f : n > 1.f ? 1.f : n;
    }

    void valueText(int i, char* buf, size_t n) const {
        const float v = fValues[i];
        if (i == kMode) {
            const char* nm[3] = { "LOW PASS", "BAND PASS", "HIGH PASS" };
            int m = (int)(v + 0.5f); if (m < 0) m = 0; if (m > 2) m = 2;
            std::snprintf(buf, n, "%s", nm[m]);
        } else if (i == kRange) {
            std::snprintf(buf, n, "%s", v < 0.10f ? "LOW" : v < 0.80f ? "MID" : "HIGH");
        } else if (i == kAttack || i == kRelease) {
            const float ms = (i == kAttack) ? std::pow(300.0f, v) : 5.0f * std::pow(200.0f, v);
            if (ms < 10.0f) std::snprintf(buf, n, "%.1f ms", ms);
            else            std::snprintf(buf, n, "%.0f ms", ms);
        } else {
            std::snprintf(buf, n, "%d%%", (int)(v * 100.0f + 0.5f));
        }
    }

    void arcPath(float cx, float cy, float r, float n0, float n1) {
        const int steps = 44;
        for (int s = 0; s <= steps; ++s) {
            const float t = n0 + (n1 - n0) * s / steps, a = angleFor(t);
            if (s == 0) moveTo(cx + r * std::cos(a), cy + r * std::sin(a));
            else        lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
        }
    }

    void drawKnob(int k) {
        float cx, cy; knobCenter(k, cx, cy);
        const int i = kLayout[k].idx;
        const float R = knobR(), n = norm(i), f = scale();

        beginPath(); circle(cx, cy + 2*f, R + f); fillColor(Color(0, 0, 0, 70)); fill();
        beginPath(); circle(cx, cy, R);            fillColor(Color(44, 42, 56)); fill();
        beginPath(); circle(cx, cy, R - 4*f);      fillColor(Color(68, 64, 86)); fill();
        beginPath(); arcPath(cx, cy, R - 3*f, 0.f, 1.f); strokeColor(Color(32, 30, 40)); strokeWidth(5*f); stroke();
        beginPath(); arcPath(cx, cy, R - 3*f, 0.f, n);   strokeColor(Color(172, 114, 244)); strokeWidth(5*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R - 10*f) * std::cos(a), cy + (R - 10*f) * std::sin(a));
        strokeColor(Color(240, 230, 255)); strokeWidth(3.5f*f); stroke();
        beginPath(); circle(cx, cy, 4*f); fillColor(Color(240, 230, 255)); fill();

        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(14*f); fillColor(Color(230, 230, 242)); text(cx, cy + R + 4*f, kQTronNames[i], NULL);
        fontSize(10*f); fillColor(Color(150, 150, 168)); text(cx, cy + R + 20*f, kQTronDesc[i], NULL);
        char buf[40]; valueText(i, buf, sizeof(buf));
        fontSize(11*f); fillColor(Color(184, 156, 238)); text(cx, cy + R + 34*f, buf, NULL);
    }

    int knobAt(double px, double py) const {
        const float R = knobR();
        for (int k = 0; k < kKnobN; ++k) {
            float cx, cy; knobCenter(k, cx, cy);
            const double dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy <= (R + 6.0) * (R + 6.0)) return k;
        }
        return -1;
    }

    void applyDrag(int i, double dyUp) {
        const float range = kQTronMax[i] - kQTronMin[i];
        fDragValue += (float)dyUp * range / (170.0f * scale());
        if (fDragValue < kQTronMin[i]) fDragValue = kQTronMin[i];
        if (fDragValue > kQTronMax[i]) fDragValue = kQTronMax[i];
        const float out = (i == kMode) ? (float)((int)(fDragValue + 0.5f)) : fDragValue;
        fValues[i] = out;
        setParameterValue(i, out);
        repaint();
    }

public:
    QTronUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          fDrag(-1), fLastY(0.0), fDragValue(0.0f)
    {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kQTronDef[i];
        setGeometryConstraints(300, 480, true, false);
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < (uint32_t)kParamCount) { fValues[index] = value; repaint(); }
    }

    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale();
        fontFace(NANOVG_DEJAVU_SANS_TTF);

        beginPath(); rect(0, 0, W, H); fillColor(Color(18, 18, 22)); fill();

        const float m = 10*f;
        beginPath(); roundedRect(m, m, W - 2*m, H - 2*m, 18*f); fillColor(Color(36, 30, 52)); fill();
        beginPath(); roundedRect(m, m, W - 2*m, H - 2*m, 18*f); strokeColor(Color(150, 92, 226)); strokeWidth(2*f); stroke();
        const float sx[4] = { 2.6f*m, W-2.6f*m, 2.6f*m, W-2.6f*m }, sy[4] = { 2.6f*m, 2.6f*m, H-2.6f*m, H-2.6f*m };
        for (int s = 0; s < 4; ++s) { beginPath(); circle(sx[s], sy[s], 4*f); fillColor(Color(72, 68, 88)); fill(); }

        beginPath(); circle(W - 52*f, 46*f, 6*f); fillColor(Color(120, 255, 150)); fill();
        beginPath(); circle(W - 52*f, 46*f, 9*f); strokeColor(Color(72, 68, 88)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(28*f); fillColor(Color(214, 180, 255)); text(28*f, 28*f, "AutoSweep", NULL);
        fontSize(11*f); fillColor(Color(150, 150, 168)); text(30*f, 64*f, "ENVELOPE  FILTER  ·  AUTO-WAH", NULL);
        beginPath(); rect(28*f, 84*f, W - 56*f, 1.5f*f); fillColor(Color(80, 72, 104)); fill();

        for (int k = 0; k < kKnobN; ++k) drawKnob(k);
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int k = knobAt(ev.pos.getX(), ev.pos.getY());
            if (k >= 0) { fDrag = k; fLastY = ev.pos.getY(); fDragValue = fValues[kLayout[k].idx]; editParameter(kLayout[k].idx, true); return true; }
        } else if (fDrag >= 0) {
            editParameter(kLayout[fDrag].idx, false); fDrag = -1; return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) { const double dyUp = fLastY - ev.pos.getY(); fLastY = ev.pos.getY(); applyDrag(kLayout[fDrag].idx, dyUp); return true; }
        return false;
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QTronUI)
};

UI* createUI() { return new QTronUI(); }

END_NAMESPACE_DISTRHO
