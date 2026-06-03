/* Bass Wah UI — bespoke Cry Baby treadle: brass body, a black ribbed rocker
 * treadle with chrome trim (its tilt follows the Pedal position), and a dark
 * control panel along the bottom with the Auto toggle + Pedal / Sens / Speed
 * knobs. Vector-crisp at any size. */
#include "DistrhoUI.hpp"
#include "BassWahParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

struct Spot { int id; float cx, cy, r; };
static const Spot kKnobs[] = {
    { kPedal, 0.375f, 0.835f, 0.072f },
    { kSens,  0.610f, 0.835f, 0.072f },
    { kSpeed, 0.845f, 0.835f, 0.072f },
};
static const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));
// Auto toggle (square button)
static const float kAutoCx = 0.135f, kAutoCy = 0.835f, kAutoH = 0.045f;

class BassWahUI : public UI
{
    float fValues[kParamCount];
    int   fDrag;
    double fLastY;
    float fDragVal;

    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth() / 300.0f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(const Spot& k) {
        const float cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r, f = scale(), n = fValues[k.id];
        beginPath(); circle(cx, cy, R);        fillColor(Color(20, 20, 22)); fill();
        beginPath(); circle(cx, cy, R - 2*f);  fillColor(Color(46, 47, 52)); fill();
        beginPath(); circle(cx, cy, R - 6*f);  fillColor(Color(28, 29, 33)); fill();
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n*s/36.f, a = angleFor(t); float x = cx + (R-2*f)*std::cos(a), y = cy + (R-2*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(220, 180, 90)); strokeWidth(3.0f*f); stroke();
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-7*f)*std::cos(a), cy + (R-7*f)*std::sin(a));
        strokeColor(Color(238, 240, 244)); strokeWidth(2.6f*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(11*f); fillColor(Color(225, 210, 175));
        text(cx, cy + R + 3*f, kBassWahNames[k.id], NULL);
    }

    int knobAt(double px, double py) const {
        for (int i = 0; i < kNumKnobs; ++i) {
            const float dx = px - W()*kKnobs[i].cx, dy = py - H()*kKnobs[i].cy, R = W()*kKnobs[i].r + 6;
            if (dx*dx + dy*dy <= R*R) return i;
        }
        return -1;
    }
    bool autoAt(double px, double py) const {
        const float hs = W()*kAutoH + 5;
        return std::fabs(px - W()*kAutoCx) <= hs && std::fabs(py - H()*kAutoCy) <= hs;
    }
public:
    BassWahUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kBassWahDef[i];
        setGeometryConstraints(300 * 3 / 4, 460 * 3 / 4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        const float w = W(), h = H(), f = scale();
        beginPath(); rect(0, 0, w, h); fillColor(Color(12, 11, 9)); fill();
        // brass body
        const float bx = 10*f, by = 8*f, bw = w - 20*f, bh = h - 16*f, rad = 16*f;
        Paint brass = linearGradient(bx, by, bx + bw, by + bh,
                                     Color(150, 120, 70), Color(96, 74, 42));
        beginPath(); roundedRect(bx, by, bw, bh, rad); fillPaint(brass); fill();
        beginPath(); roundedRect(bx, by, bw, bh, rad); strokeColor(Color(60, 46, 24)); strokeWidth(2*f); stroke();

        // ── rocker treadle (chrome trim + black ribbed rocker) ──
        const float tx = bx + 26*f, ty = by + 18*f, tw = bw - 52*f, th = bh * 0.60f;
        // chrome frame
        beginPath(); roundedRect(tx - 6*f, ty - 6*f, tw + 12*f, th + 12*f, 12*f);
        Paint chrome = linearGradient(0, ty - 6*f, 0, ty + th + 6*f, Color(225,228,232), Color(120,124,130));
        fillPaint(chrome); fill();
        // the rocker — tilt the rib pattern by the Pedal position (toe down = more)
        beginPath(); roundedRect(tx, ty, tw, th, 9*f); fillColor(Color(24, 24, 26)); fill();
        const float tilt = (fValues[kPedal] - 0.5f) * 0.16f;   // shear factor
        strokeColor(Color(60, 62, 66)); strokeWidth(2.0f*f);
        const int ribs = 16;
        for (int i = 1; i < ribs; ++i) {
            const float yy = ty + th * i / (float)ribs;
            const float dx = (yy - (ty + th*0.5f)) * tilt;
            beginPath(); moveTo(tx + 6*f + dx, yy); lineTo(tx + tw - 6*f + dx, yy); stroke();
        }
        // "BASS WAH" embossed on the treadle
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(20*f); fillColor(Color(150, 120, 70, 220));
        text(tx + tw*0.5f, ty + th*0.5f, "BASS WAH", NULL);
        // status LED above the panel
        const bool on = fValues[kAuto] > 0.5f;
        beginPath(); circle(w*0.5f, by + bh*0.66f, 5*f); fillColor(on ? Color(255,70,60) : Color(90,30,28)); fill();

        // ── control panel ──
        const float py0 = by + bh*0.70f, pH = bh*0.28f;
        beginPath(); roundedRect(bx + 10*f, py0, bw - 20*f, pH, 10*f); fillColor(Color(26, 24, 22, 240)); fill();

        // Auto toggle button
        const float acx = w*kAutoCx, acy = h*kAutoCy, ahs = w*kAutoH;
        beginPath(); roundedRect(acx-ahs, acy-ahs, ahs*2, ahs*2, 4*f);
        fillColor(on ? Color(208, 40, 36) : Color(70, 30, 28)); fill();
        beginPath(); roundedRect(acx-ahs, acy-ahs, ahs*2, ahs*2, 4*f); strokeColor(Color(20,12,10)); strokeWidth(1.5f*f); stroke();
        if (on) { beginPath(); circle(acx, acy, ahs*0.32f); fillColor(Color(255,180,170)); fill(); }
        textAlign(ALIGN_CENTER | ALIGN_TOP); fontSize(10*f); fillColor(Color(225,210,175));
        text(acx, acy + ahs + 3*f, "Auto", NULL);

        for (int i = 0; i < kNumKnobs; ++i) drawKnob(kKnobs[i]);
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            if (autoAt(ev.pos.getX(), ev.pos.getY())) {
                float nv = fValues[kAuto] > 0.5f ? 0.f : 1.f; fValues[kAuto] = nv; setParameterValue(kAuto, nv); repaint(); return true;
            }
            const int k = knobAt(ev.pos.getX(), ev.pos.getY());
            if (k >= 0) { fDrag = k; fLastY = ev.pos.getY(); fDragVal = fValues[kKnobs[k].id]; editParameter(kKnobs[k].id, true); return true; }
        } else if (fDrag >= 0) { editParameter(kKnobs[fDrag].id, false); fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) {
            const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
            fDragVal += (float)dy / (170.0f * scale());
            if (fDragVal < 0.f) fDragVal = 0.f; if (fDragVal > 1.f) fDragVal = 1.f;
            const int id = kKnobs[fDrag].id;
            fValues[id] = fDragVal; setParameterValue(id, fDragVal); repaint();
            return true;
        }
        return false;
    }
private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassWahUI)
};

UI* createUI() { return new BassWahUI(); }

END_NAMESPACE_DISTRHO
