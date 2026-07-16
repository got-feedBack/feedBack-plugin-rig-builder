/* Boss VB-2-style editor with the real three pots and three-position mode
 * selector. The parameter ids remain compatible with existing song presets. */
#include "DistrhoUI.hpp"
#include "MultiVibeParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

struct Vb2Knob { int id; float cx; const char* label; };
static const Vb2Knob kKnobs[] = {
    { kSpeed,    0.24f, "RATE" },
    { kMix,      0.50f, "DEPTH" },
    { kWaveform, 0.76f, "RISE TIME" },
};
static const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));

class MultiVibeUI : public UI
{
    float values[kParamCount];
    int dragKnob = -1;
    double lastY = 0.0;
    float dragValue = 0.0f;

    float W() const { return (float)getWidth(); }
    float H() const { return (float)getHeight(); }
    float scale() const { return W() / 360.0f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(const Vb2Knob& k)
    {
        const float f = scale();
        const float cx = W() * k.cx;
        const float cy = H() * 0.205f;
        const float r = W() * 0.085f;
        const float n = values[k.id];

        beginPath(); circle(cx, cy, r); fillColor(Color(24, 25, 29)); fill();
        beginPath(); circle(cx, cy, r - 3.0f*f); fillColor(Color(58, 61, 69)); fill();
        beginPath(); circle(cx, cy, r - 7.0f*f); fillColor(Color(37, 39, 45)); fill();

        beginPath();
        for (int i = 0; i <= 36; ++i) {
            const float a = angleFor(n * i / 36.0f);
            const float x = cx + (r - 2.0f*f) * std::cos(a);
            const float y = cy + (r - 2.0f*f) * std::sin(a);
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        strokeColor(Color(100, 192, 250)); strokeWidth(3.2f*f); stroke();

        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy);
        lineTo(cx + (r - 8.0f*f) * std::cos(a), cy + (r - 8.0f*f) * std::sin(a));
        strokeColor(Color(242, 244, 248)); strokeWidth(2.8f*f); stroke();

        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize((k.id == kWaveform ? 9.5f : 11.0f) * f);
        fillColor(Color(240, 244, 249));
        text(cx, cy - r - 5.0f*f, k.label, NULL);
    }

    void drawMode()
    {
        const float f = scale();
        const float cx = W() * 0.23f;
        const float cy = H() * 0.43f;
        const float slotW = 15.0f*f;
        const float slotH = 76.0f*f;
        const float v = values[kMode] < 0.25f ? 0.0f : (values[kMode] < 0.75f ? 0.5f : 1.0f);

        beginPath(); roundedRect(cx-slotW/2, cy-slotH/2, slotW, slotH, 4.0f*f);
        fillColor(Color(22, 23, 27)); fill();
        beginPath(); roundedRect(cx-slotW/2, cy-slotH/2, slotW, slotH, 4.0f*f);
        strokeColor(Color(7, 8, 10)); strokeWidth(1.2f*f); stroke();

        const float leverY = cy + (0.5f - v) * 48.0f*f;
        Paint cap = linearGradient(cx-7.0f*f, leverY, cx+7.0f*f, leverY,
                                   Color(238, 241, 246), Color(132, 137, 146));
        beginPath(); roundedRect(cx-7.0f*f, leverY-9.0f*f, 14.0f*f, 18.0f*f, 4.0f*f);
        fillPaint(cap); fill();

        const char* labels[3] = { "UNLATCH", "BYPASS", "LATCH" };
        const float ys[3] = { cy-24.0f*f, cy, cy+24.0f*f };
        const bool active[3] = { v > 0.75f, v > 0.25f && v < 0.75f, v < 0.25f };
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(9.0f*f);
        for (int i = 0; i < 3; ++i) {
            fillColor(active[i] ? Color(245, 248, 252) : Color(20, 55, 88));
            text(cx + 18.0f*f, ys[i], labels[i], NULL);
        }
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize(10.0f*f); fillColor(Color(238, 243, 249));
        text(cx, cy-slotH/2-7.0f*f, "MODE", NULL);
    }

    int knobAt(double x, double y) const
    {
        for (int i = 0; i < kNumKnobs; ++i) {
            const float dx = (float)x - W()*kKnobs[i].cx;
            const float dy = (float)y - H()*0.205f;
            const float r = W()*0.085f + 7.0f;
            if (dx*dx + dy*dy <= r*r) return i;
        }
        return -1;
    }

    bool modeAt(double x, double y) const
    {
        return std::fabs(x - W()*0.23f) <= 42.0f*scale()
            && std::fabs(y - H()*0.43f) <= 48.0f*scale();
    }

public:
    MultiVibeUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) values[i] = kMultiVibeDef[i];
        setGeometryConstraints(270, 330, true, false);
    }

protected:
    void parameterChanged(uint32_t i, float v) override
    {
        if (i < (uint32_t)kParamCount) { values[i] = v; repaint(); }
    }

    void onNanoDisplay() override
    {
        const float w = W(), h = H(), f = scale();
        beginPath(); rect(0, 0, w, h); fillColor(Color(12, 13, 16)); fill();

        Paint body = linearGradient(0, 10.0f*f, 0, h-10.0f*f,
                                    Color(65, 164, 226), Color(27, 103, 171));
        beginPath(); roundedRect(12.0f*f, 10.0f*f, w-24.0f*f, h-20.0f*f, 20.0f*f);
        fillPaint(body); fill();
        beginPath(); roundedRect(12.0f*f, 10.0f*f, w-24.0f*f, h-20.0f*f, 20.0f*f);
        strokeColor(Color(190, 224, 246, 100)); strokeWidth(2.0f*f); stroke();

        beginPath(); roundedRect(28.0f*f, 25.0f*f, w-56.0f*f, h*0.50f, 11.0f*f);
        fillColor(Color(20, 25, 31, 232)); fill();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        for (int i = 0; i < kNumKnobs; ++i) drawKnob(kKnobs[i]);
        drawMode();

        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(30.0f*f); fillColor(Color(15, 45, 75));
        text(w*0.50f, h*0.62f, "VIBRATO", NULL);
        fontSize(18.0f*f); fillColor(Color(228, 242, 252));
        text(w*0.78f, h*0.68f, "VB-2", NULL);

        beginPath(); circle(w*0.5f, h*0.76f, 5.0f*f); fillColor(Color(242, 62, 52)); fill();
        beginPath(); circle(w*0.5f, h*0.88f, 25.0f*f); fillColor(Color(198, 203, 210)); fill();
        beginPath(); circle(w*0.5f, h*0.88f, 25.0f*f); strokeColor(Color(105, 111, 120)); strokeWidth(2.5f*f); stroke();
        beginPath(); circle(w*0.5f, h*0.88f, 16.0f*f); fillColor(Color(146, 152, 161)); fill();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (ev.press) {
            if (modeAt(ev.pos.getX(), ev.pos.getY())) {
                const float y = (float)ev.pos.getY();
                const float cy = H()*0.43f;
                const float nv = y < cy-8.0f*scale() ? 1.0f : (y > cy+8.0f*scale() ? 0.0f : 0.5f);
                values[kMode] = nv; setParameterValue(kMode, nv); repaint();
                return true;
            }
            const int k = knobAt(ev.pos.getX(), ev.pos.getY());
            if (k >= 0) {
                dragKnob = k; lastY = ev.pos.getY(); dragValue = values[kKnobs[k].id];
                editParameter(kKnobs[k].id, true); return true;
            }
        } else if (dragKnob >= 0) {
            editParameter(kKnobs[dragKnob].id, false); dragKnob = -1; return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragKnob < 0) return false;
        dragValue += (float)(lastY - ev.pos.getY()) / (170.0f * scale());
        lastY = ev.pos.getY();
        dragValue = dragValue < 0.0f ? 0.0f : (dragValue > 1.0f ? 1.0f : dragValue);
        const int id = kKnobs[dragKnob].id;
        values[id] = dragValue; setParameterValue(id, dragValue); repaint();
        return true;
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiVibeUI)
};

UI* createUI() { return new MultiVibeUI(); }

END_NAMESPACE_DISTRHO
