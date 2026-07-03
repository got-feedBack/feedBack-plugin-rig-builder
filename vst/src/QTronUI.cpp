/*
 * AutoSweep — DPF NanoVG UI, LANDSCAPE stompbox like the game "Auto
 * Filter" art: taupe body, a single row of knobs (Sens / Attack / Release /
 * FilterType / Res / Mix), big pedal name, footswitch. Rotary knobs drag
 * vertically; meaningful readouts kept (LP/BP/HP, ms, %).
 */
#include "DistrhoUI.hpp"
#include "QTronParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

// Knobs in one row, ordered like the Auto Filter art (+ Res, an RS Auto Tone
// knob the art omits). Range + Boost stay as params (fixed via preset state).
static const int kKnobN = 6;
static const int kRowIdx[kKnobN] = { kGain, kAttack, kRelease, kMode, kPeak, kMix };

class QTronUI : public UI
{
    float  fValues[kParamCount];
    int    fDrag;
    double fLastY;
    float  fDragValue;

    float scale()  const { return getWidth() / 600.0f; }
    float knobR()  const { return getWidth() * 0.052f; }
    float kx(int k) const { return getWidth() * (0.105f + 0.158f * k); }
    float ky()     const { return getHeight() * 0.30f; }
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
        const int steps = 40;
        for (int s = 0; s <= steps; ++s) {
            const float t = n0 + (n1 - n0) * s / steps, a = angleFor(t);
            if (s == 0) moveTo(cx + r * std::cos(a), cy + r * std::sin(a));
            else        lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
        }
    }
    void drawKnob(int k) {
        const int i = kRowIdx[k];
        const float cx = kx(k), cy = ky(), R = knobR(), n = norm(i), f = scale();
        beginPath(); circle(cx, cy + 2*f, R + f); fillColor(Color(0,0,0,70)); fill();
        beginPath(); circle(cx, cy, R);           fillColor(Color(40, 37, 33)); fill();
        beginPath(); circle(cx, cy, R - 4*f);     fillColor(Color(62, 57, 50)); fill();
        beginPath(); arcPath(cx, cy, R - 2*f, 0.f, 1.f); strokeColor(Color(28, 25, 22)); strokeWidth(4*f); stroke();
        beginPath(); arcPath(cx, cy, R - 2*f, 0.f, n);   strokeColor(Color(214, 170, 96)); strokeWidth(4*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R - 7*f) * std::cos(a), cy + (R - 7*f) * std::sin(a));
        strokeColor(Color(244, 238, 228)); strokeWidth(3*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(12*f); fillColor(Color(242, 236, 226)); text(cx, cy + R + 4*f, kQTronNames[i], NULL);
        char buf[40]; valueText(i, buf, sizeof(buf));
        fontSize(10*f); fillColor(Color(226, 188, 130)); text(cx, cy + R + 18*f, buf, NULL);
    }
    int knobAt(double px, double py) const {
        const float R = knobR();
        for (int k = 0; k < kKnobN; ++k) { const double dx = px - kx(k), dy = py - ky(); if (dx*dx + dy*dy <= (R+6)*(R+6)) return k; }
        return -1;
    }
    void applyDrag(int i, double dyUp) {
        const float range = kQTronMax[i] - kQTronMin[i];
        fDragValue += (float)dyUp * range / (170.0f * scale());
        if (fDragValue < kQTronMin[i]) fDragValue = kQTronMin[i];
        if (fDragValue > kQTronMax[i]) fDragValue = kQTronMax[i];
        const float out = (i == kMode) ? (float)((int)(fDragValue + 0.5f)) : fDragValue;
        fValues[i] = out; setParameterValue(i, out); repaint();
    }
public:
    QTronUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0.0), fDragValue(0.0f) {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kQTronDef[i];
        setGeometryConstraints(450, 210, true, false);
    }
protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < (uint32_t)kParamCount) { fValues[index] = value; repaint(); }
    }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale(), m = 10*f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0,0,W,H); fillColor(Color(14,13,12)); fill();
        // taupe body (like the Auto Filter art)
        Paint body = linearGradient(0, m, 0, H-m, Color(150,139,126), Color(60,54,48));
        beginPath(); roundedRect(m, m, W-2*m, H-2*m, 18*f); fillPaint(body); fill();
        beginPath(); roundedRect(m, m, W-2*m, H-2*m, 18*f); strokeColor(Color(40,35,30)); strokeWidth(2*f); stroke();
        // jack-screw dots
        const float sx[2] = { 2.4f*m, W-2.4f*m };
        for (int s=0;s<2;++s){ beginPath(); circle(sx[s], H*0.30f, 4*f); fillColor(Color(40,36,32)); fill(); }
        // LED
        beginPath(); circle(W*0.5f, H*0.10f, 5*f); fillColor(Color(255,80,70)); fill();
        // knobs row
        for (int k = 0; k < kKnobN; ++k) drawKnob(k);
        // big pedal name
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(34*f); fillColor(Color(244,238,228,235)); text(W*0.5f, H*0.66f, "AUTO FILTER", NULL);
        // footswitch
        beginPath(); circle(W*0.5f, H*0.87f, 18*f); fillColor(Color(206,209,214)); fill();
        beginPath(); circle(W*0.5f, H*0.87f, 18*f); strokeColor(Color(110,114,120)); strokeWidth(2.5f*f); stroke();
        beginPath(); circle(W*0.5f, H*0.87f, 11*f); fillColor(Color(156,160,167)); fill();
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) { const int k = knobAt(ev.pos.getX(), ev.pos.getY()); if (k>=0){ fDrag=k; fLastY=ev.pos.getY(); fDragValue=fValues[kRowIdx[k]]; editParameter(kRowIdx[k], true); return true; } }
        else if (fDrag >= 0) { editParameter(kRowIdx[fDrag], false); fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) { const double dyUp = fLastY - ev.pos.getY(); fLastY = ev.pos.getY(); applyDrag(kRowIdx[fDrag], dyUp); return true; }
        return false;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QTronUI)
};

UI* createUI() { return new QTronUI(); }

END_NAMESPACE_DISTRHO
