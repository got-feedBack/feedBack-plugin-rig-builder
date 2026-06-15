/*
 * Shared 1U-rack-style UI for the bundled the game rack VSTs (DPF NanoVG).
 *
 * Draws a horizontal rack face like the real the game rack art: dark metal
 * face with rack ears + screws at both ends, a POWER button, a coloured
 * sub-panel holding the knobs, a green LCD nameplate, and a decorative INPUT
 * knob on the right. Knobs are rotary with vertical-drag editing.
 *
 * Each rack's <Name>_ui.cpp defines before including this header:
 *   #include "<Name>Params.h"
 *   #define RACK_COUNT   <kNumParams>
 *   #define RACK_TITLE   "STUDIO COMPRESSOR"
 *   #define RACK_NAMES   kXxxNames        // const char* const [RACK_COUNT]
 *   #define RACK_DEFS    kXxxDef          // float, or define RACK_NO_DEFS
 *   #define RACK_ACR 230                  // sub-panel accent colour
 *   #define RACK_ACG 170
 *   #define RACK_ACB 30
 *   #define RACK_KNOBS { {0.13f,0.50f,0.034f}, ... }   // cx,cy frac W/H; r frac W
 *   // optional: RACK_W / RACK_H default window size
 *   #include "../_shared/rack_ui.hpp"
 */
#include "DistrhoUI.hpp"
#include <cmath>
#include <cstdio>

#ifndef RACK_W
#define RACK_W 760
#endif
#ifndef RACK_H
#define RACK_H 172
#endif

START_NAMESPACE_DISTRHO

struct RackKnob { float cx, cy, r; };
static const RackKnob kRackKnobs[RACK_COUNT] = RACK_KNOBS;

#ifdef RACK_NO_DEFS
struct RackDefFill { float v[RACK_COUNT]; RackDefFill(){ for (int i=0;i<RACK_COUNT;++i) v[i]=0.5f; } };
static const RackDefFill kRackDefFill;
#define RACK_DEFS kRackDefFill.v
#endif

class RackUI : public UI
{
    float  fValues[RACK_COUNT];
    int    fDrag;
    double fLastY;
    float  fDragVal;

    float scale() const { return getWidth() / (float)RACK_W; }
    float kx(int i) const { return getWidth()  * kRackKnobs[i].cx; }
    float ky(int i) const { return getHeight() * kRackKnobs[i].cy; }
    float kr(int i) const { return getWidth()  * kRackKnobs[i].r; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(int i) {
        const float cx = kx(i), cy = ky(i), R = kr(i), f = scale(), n = fValues[i];
        beginPath(); circle(cx, cy, R);       fillColor(Color(30, 31, 36)); fill();
        beginPath(); circle(cx, cy, R - 2*f); fillColor(Color(54, 57, 66)); fill();
        beginPath();
        for (int s = 0; s <= 32; ++s) { float t = n*s/32.f, a = angleFor(t); float x = cx + (R+3*f)*std::cos(a), y = cy + (R+3*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(20, 22, 26)); strokeWidth(2.5f*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-3*f)*std::cos(a), cy + (R-3*f)*std::sin(a));
        strokeColor(Color(238, 240, 245)); strokeWidth(2.5f*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(8.0f*f); fillColor(Color(18, 19, 22)); text(cx, cy + R + 2*f, RACK_NAMES[i], NULL);
    }
    int knobAt(double px, double py) const {
        for (int i = 0; i < RACK_COUNT; ++i) {
            const float dx = px - kx(i), dy = py - ky(i), R = kr(i) + 6;
            if (dx*dx + dy*dy <= R*R) return i;
        }
        return -1;
    }
    void screws(float x, float w, float H, float f) {
        const float r = 5*f;
        fillColor(Color(150,152,158));
        for (float yy : { H*0.22f, H*0.78f }) {
            beginPath(); circle(x + w*0.5f, yy, r); fill();
            beginPath(); moveTo(x+w*0.5f-r*0.7f, yy); lineTo(x+w*0.5f+r*0.7f, yy); strokeColor(Color(60,62,66)); strokeWidth(1.5f*f); stroke();
        }
    }
public:
    RackUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < RACK_COUNT; ++i) fValues[i] = RACK_DEFS[i];
        setGeometryConstraints(RACK_W * 3 / 4, RACK_H * 3 / 4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)RACK_COUNT) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // rack face (brushed dark metal)
        Paint face = linearGradient(0, 0, 0, H, Color(58,60,66), Color(34,35,40));
        beginPath(); rect(0,0,W,H); fillPaint(face); fill();
        beginPath(); rect(0,0,W,H); strokeColor(Color(12,13,15)); strokeWidth(3*f); stroke();
        // rack ears (darker strips + screws) at both ends
        const float earW = W * 0.06f;
        for (float ex : { 0.0f, W - earW }) {
            beginPath(); rect(ex, 0, earW, H); fillColor(Color(26,27,31)); fill();
            screws(ex, earW, H, f);
        }
        // POWER button (top-left, just inside the ear)
        beginPath(); circle(earW + 18*f, H*0.30f, 9*f); fillColor(Color(22,23,27)); fill();
        beginPath(); circle(earW + 18*f, H*0.30f, 9*f); strokeColor(Color(90,92,98)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(150,152,158)); text(earW + 18*f, H*0.30f + 12*f, "POWER", NULL);

        // coloured knob sub-panel
        const float pX = earW + 36*f, pY = H*0.12f, pW = W*0.42f, pH = H*0.76f;
        beginPath(); roundedRect(pX, pY, pW, pH, 6*f);
        fillColor(Color(RACK_ACR, RACK_ACG, RACK_ACB)); fill();
        beginPath(); roundedRect(pX, pY, pW, pH, 6*f); strokeColor(Color(0,0,0,60)); strokeWidth(1.5f*f); stroke();

        for (int i = 0; i < RACK_COUNT; ++i) drawKnob(i);

        // green LCD nameplate
        const float lX = pX + pW + 22*f, lY = H*0.22f, lW = W*0.30f, lH = H*0.56f;
        beginPath(); roundedRect(lX, lY, lW, lH, 4*f); fillColor(Color(8, 20, 10)); fill();
        beginPath(); roundedRect(lX, lY, lW, lH, 4*f); strokeColor(Color(40,90,45)); strokeWidth(1.5f*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(15*f); fillColor(Color(120, 255, 130)); text(lX + 10*f, lY + 9*f, RACK_TITLE, NULL);
        fontSize(8.5f*f); fillColor(Color(70, 180, 80)); text(lX + 10*f, lY + lH - 18*f, "USER PROG  ·  RIG BUILDER", NULL);

        // decorative INPUT knob (far right)
        const float ix = W - earW - 30*f, iy = H*0.5f, iR = H*0.26f;
        beginPath(); circle(ix, iy, iR); fillColor(Color(24,25,29)); fill();
        beginPath(); circle(ix, iy, iR); strokeColor(Color(80,82,88)); strokeWidth(2*f); stroke();
        beginPath(); circle(ix, iy, iR*0.55f); fillColor(Color(40,42,48)); fill();
        textAlign(ALIGN_RIGHT|ALIGN_MIDDLE); fontSize(8*f); fillColor(Color(150,152,158)); text(ix - iR - 4*f, iy, "INPUT", NULL);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackUI)
};

UI* createUI() { return new RackUI(); }

END_NAMESPACE_DISTRHO
