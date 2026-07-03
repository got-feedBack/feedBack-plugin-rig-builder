/*
 * Shared stompbox-style UI for the bundled the game pedal VSTs (DPF NanoVG).
 *
 * Draws a portrait pedal body (rounded rect + metallic gradient + accent
 * border), the pedal name, an LED + footswitch graphic, and the pedal's knobs
 * at per-pedal positions taken from the real the game pedal art. Knobs are
 * rotary with vertical-drag editing, a value arc, a pointer, a top label and a
 * 0–10 readout.
 *
 * Each pedal's <Name>_ui.cpp defines these before including this header:
 *   #include "<Name>Params.h"          // gives kParamCount + the arrays
 *   #define PEDAL_TITLE   "NAME"        // shown on the body (\n allowed)
 *   #define PEDAL_NAMES   kXxxNames     // const char* const [kParamCount]
 *   #define PEDAL_DEFS    kXxxDef       // float [kParamCount]
 *   #define PEDAL_ACR 90                // accent colour (R,G,B 0-255)
 *   #define PEDAL_ACG 150
 *   #define PEDAL_ACB 230
 *   #define PEDAL_KNOBS { {0.30f,0.20f,0.12f}, ... }  // cx,cy,r (frac of W/H/W)
 *   // optional: PEDAL_W / PEDAL_H default window size
 *   #include "../_shared/pedal_ui.hpp"
 */
#include "DistrhoUI.hpp"
#include <cmath>
#include <cstdio>

#ifndef PEDAL_W
#define PEDAL_W 300
#endif
#ifndef PEDAL_H
#define PEDAL_H 460
#endif
#ifndef PEDAL_ACR
#define PEDAL_ACR 110
#endif
#ifndef PEDAL_ACG
#define PEDAL_ACG 170
#endif
#ifndef PEDAL_ACB
#define PEDAL_ACB 240
#endif
// Optional separate knob value-arc colour (defaults to the body accent). Set
// it when the body is very dark/black and the accent-coloured arc would vanish
// — e.g. a black pedal wants a light/white arc.
#ifndef PEDAL_ARCR
#define PEDAL_ARCR PEDAL_ACR
#define PEDAL_ARCG PEDAL_ACG
#define PEDAL_ARCB PEDAL_ACB
#endif

START_NAMESPACE_DISTRHO

struct PedalKnob { float cx, cy, r; };   // cx,cy fraction of W/H; r fraction of W
static const PedalKnob kPedalKnobs[kParamCount] = PEDAL_KNOBS;

class PedalUI : public UI
{
    float  fValues[kParamCount];
    int    fDrag;
    double fLastY;
    float  fDragVal;

    float scale() const { return getWidth() / (float)PEDAL_W; }
    float kx(int i) const { return getWidth()  * kPedalKnobs[i].cx; }
    float ky(int i) const { return getHeight() * kPedalKnobs[i].cy; }
    float kr(int i) const { return getWidth()  * kPedalKnobs[i].r; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(int i) {
        const float cx = kx(i), cy = ky(i), R = kr(i), f = scale(), n = fValues[i];
        // knob body
        beginPath(); circle(cx, cy, R);        fillColor(Color(36, 38, 44)); fill();
        beginPath(); circle(cx, cy, R - 3*f);  fillColor(Color(58, 62, 72)); fill();
        beginPath(); circle(cx, cy, R - 7*f);  fillColor(Color(44, 47, 56)); fill();
        // value arc
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n*s/36.f, a = angleFor(t); float x = cx + (R-2*f)*std::cos(a), y = cy + (R-2*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(PEDAL_ARCR, PEDAL_ARCG, PEDAL_ARCB)); strokeWidth(3.5f*f); stroke();
        // pointer
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-8*f)*std::cos(a), cy + (R-8*f)*std::sin(a));
        strokeColor(Color(235, 238, 244)); strokeWidth(3*f); stroke();
        // label above, value below
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize(11*f); fillColor(Color(225, 228, 236)); text(cx, cy - R - 4*f, PEDAL_NAMES[i], NULL);
        char buf[16]; std::snprintf(buf, sizeof(buf), "%.1f", n * 10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(10*f); fillColor(Color(170, 178, 190)); text(cx, cy + R + 3*f, buf, NULL);
    }
    int knobAt(double px, double py) const {
        for (int i = 0; i < kParamCount; ++i) {
            const float dx = px - kx(i), dy = py - ky(i), R = kr(i) + 6;
            if (dx*dx + dy*dy <= R*R) return i;
        }
        return -1;
    }
public:
    PedalUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = PEDAL_DEFS[i];
        // keepAspectRatio = true, automaticallyScale = FALSE — DPF must NOT
        // bitmap-scale the canvas (that's what made it blurry); we redraw
        // vector-crisp at any size off getWidth()/getHeight(), like the EQ UIs.
        setGeometryConstraints(PEDAL_W * 3 / 4, PEDAL_H * 3 / 4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale();
        // backdrop
        beginPath(); rect(0, 0, W, H); fillColor(Color(14, 15, 18)); fill();
        // pedal body (portrait, rounded) with vertical metallic gradient
        const float bx = 14*f, by = 12*f, bw = W - 28*f, bh = H - 24*f, rad = 22*f;
        Paint body = linearGradient(0, by, 0, by + bh,
                                    Color(PEDAL_ACR, PEDAL_ACG, PEDAL_ACB, 235),
                                    Color(PEDAL_ACR/3 + 12, PEDAL_ACG/3 + 12, PEDAL_ACB/3 + 12, 255));
        beginPath(); roundedRect(bx, by, bw, bh, rad); fillPaint(body); fill();
        beginPath(); roundedRect(bx, by, bw, bh, rad); strokeColor(Color(245, 247, 250, 60)); strokeWidth(2*f); stroke();
        // dark control plate at the top where the knobs live
        const float plY = by + 10*f, plH = H * 0.46f;
        beginPath(); roundedRect(bx + 12*f, plY, bw - 24*f, plH, 14*f); fillColor(Color(26, 27, 32, 235)); fill();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // pedal name (centre body, below the plate)
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(21*f); fillColor(Color(248, 250, 252, 230));
        text(W * 0.5f, H * 0.63f, PEDAL_TITLE, NULL);

        // knobs
        for (int i = 0; i < kParamCount; ++i) drawKnob(i);

        // LED
        beginPath(); circle(W*0.5f, H*0.75f, 5*f); fillColor(Color(255, 70, 60)); fill();
        beginPath(); circle(W*0.5f, H*0.75f, 9*f); strokeColor(Color(255,120,110,90)); strokeWidth(3*f); stroke();
        // footswitch
        beginPath(); circle(W*0.5f, H*0.87f, 26*f); fillColor(Color(200,205,210)); fill();
        beginPath(); circle(W*0.5f, H*0.87f, 26*f); strokeColor(Color(120,124,130)); strokeWidth(3*f); stroke();
        beginPath(); circle(W*0.5f, H*0.87f, 17*f); fillColor(Color(150,155,162)); fill();
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalUI)
};

UI* createUI() { return new PedalUI(); }

END_NAMESPACE_DISTRHO
