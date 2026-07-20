#include "DistrhoUI.hpp"
#include "RingModParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

static constexpr int kUiW = 420;
static constexpr int kUiH = 460;
static constexpr float kPi = 3.14159265358979323846f;

struct KnobPosition
{
    uint32_t id;
    float x;
    float y;
    float radius;
};

static const KnobPosition kKnobs[] = {
    { kPitch,      0.22f, 0.22f, 0.095f },
    { kModulation, 0.50f, 0.22f, 0.095f },
    { kVolume,     0.78f, 0.22f, 0.095f },
};

} // namespace

class RingModUI : public UI
{
    float values[kParamCount];
    int dragging = -1;
    double lastY = 0.0;
    float dragValue = 0.0f;

    float scale() const { return getWidth() / static_cast<float>(kUiW); }

    static float angleFor(float value)
    {
        return (135.0f + 270.0f * value) * kPi / 180.0f;
    }

    void drawKnob(int index)
    {
        const KnobPosition& knob = kKnobs[index];
        const float f = scale();
        const float cx = getWidth() * knob.x;
        const float cy = getHeight() * knob.y;
        const float radius = getWidth() * knob.radius;
        const float value = values[knob.id];

        beginPath(); circle(cx, cy, radius); fillColor(Color(25, 27, 31)); fill();
        beginPath(); circle(cx, cy, radius - 5.0f * f); fillColor(Color(57, 59, 63)); fill();
        beginPath(); circle(cx, cy, radius - 10.0f * f); fillColor(Color(37, 39, 43)); fill();

        const float angle = angleFor(value);
        beginPath();
        moveTo(cx, cy);
        lineTo(cx + (radius - 10.0f * f) * std::cos(angle),
               cy + (radius - 10.0f * f) * std::sin(angle));
        strokeColor(Color(236, 226, 198)); strokeWidth(4.0f * f); stroke();

        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize(12.0f * f); fillColor(Color(238, 230, 210));
        text(cx, cy - radius - 7.0f * f, kRingModNames[knob.id], nullptr);
        char valueText[16];
        std::snprintf(valueText, sizeof(valueText), "%.1f", value * 10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(10.0f * f); fillColor(Color(185, 178, 163));
        text(cx, cy + radius + 5.0f * f, valueText, nullptr);
    }

    void drawSwitch(uint32_t id, float cx, const char* left, const char* right)
    {
        const float f = scale();
        const float x = getWidth() * cx;
        const float y = getHeight() * 0.43f;
        const bool on = values[id] >= 0.5f;
        const float width = 74.0f * f;
        const float height = 29.0f * f;

        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fontSize(11.0f * f); fillColor(Color(238, 230, 210));
        text(x, y - 13.0f * f, kRingModNames[id], nullptr);

        beginPath(); roundedRect(x - width * 0.5f, y - height * 0.5f,
                                 width, height, 4.0f * f);
        fillColor(Color(22, 23, 25)); fill();
        beginPath(); roundedRect(x - width * 0.5f + (on ? width * 0.5f : 2.0f * f),
                                 y - height * 0.5f + 2.0f * f,
                                 width * 0.5f - 2.0f * f, height - 4.0f * f,
                                 3.0f * f);
        fillColor(Color(180, 47, 38)); fill();

        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(9.0f * f); fillColor(Color(202, 194, 176));
        text(x - 24.0f * f, y + 19.0f * f, left, nullptr);
        text(x + 24.0f * f, y + 19.0f * f, right, nullptr);
    }

    int knobAt(double px, double py) const
    {
        for (int i = 0; i < 3; ++i)
        {
            const float cx = getWidth() * kKnobs[i].x;
            const float cy = getHeight() * kKnobs[i].y;
            const float radius = getWidth() * kKnobs[i].radius + 8.0f * scale();
            const float dx = static_cast<float>(px) - cx;
            const float dy = static_cast<float>(py) - cy;
            if (dx * dx + dy * dy <= radius * radius)
                return i;
        }
        return -1;
    }

    int switchAt(double px, double py) const
    {
        const float y = getHeight() * 0.43f;
        if (std::fabs(static_cast<float>(py) - y) > 28.0f * scale())
            return -1;
        if (std::fabs(static_cast<float>(px) - getWidth() * 0.35f) < 48.0f * scale())
            return kPitchRange;
        if (std::fabs(static_cast<float>(px) - getWidth() * 0.65f) < 48.0f * scale())
            return kModulate;
        return -1;
    }

public:
    RingModUI()
        : UI(kUiW, kUiH)
    {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i)
            values[i] = kRingModDef[i];
        setGeometryConstraints(kUiW * 3 / 4, kUiH * 3 / 4, true, false);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < static_cast<uint32_t>(kParamCount))
        {
            values[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = getWidth();
        const float height = getHeight();
        const float f = scale();

        beginPath(); rect(0.0f, 0.0f, width, height); fillColor(Color(13, 14, 16)); fill();
        Paint body = linearGradient(0.0f, 10.0f * f, 0.0f, height - 10.0f * f,
                                    Color(79, 73, 65), Color(37, 34, 31));
        beginPath(); roundedRect(14.0f * f, 12.0f * f, width - 28.0f * f,
                                 height - 24.0f * f, 17.0f * f);
        fillPaint(body); fill();
        beginPath(); roundedRect(25.0f * f, 24.0f * f, width - 50.0f * f,
                                 height * 0.48f, 9.0f * f);
        fillColor(Color(15, 16, 18, 235)); fill();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        for (int i = 0; i < 3; ++i)
            drawKnob(i);
        drawSwitch(kPitchRange, 0.35f, "LOW", "HIGH");
        drawSwitch(kModulate, 0.65f, "OFF", "ON");

        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(27.0f * f); fillColor(Color(242, 230, 199));
        text(width * 0.5f, height * 0.62f, "RING MODULATOR", nullptr);
        fontSize(13.0f * f); fillColor(Color(187, 52, 43));
        text(width * 0.5f, height * 0.68f, "RM-1A", nullptr);

        beginPath(); circle(width * 0.5f, height * 0.77f, 6.0f * f);
        fillColor(Color(225, 45, 37)); fill();
        beginPath(); circle(width * 0.5f, height * 0.88f, 26.0f * f);
        fillColor(Color(183, 184, 181)); fill();
        beginPath(); circle(width * 0.5f, height * 0.88f, 18.0f * f);
        fillColor(Color(126, 128, 128)); fill();
    }

    bool onMouse(const MouseEvent& event) override
    {
        if (event.button != 1)
            return false;
        if (event.press)
        {
            const int knob = knobAt(event.pos.getX(), event.pos.getY());
            if (knob >= 0)
            {
                dragging = knob;
                lastY = event.pos.getY();
                dragValue = values[kKnobs[knob].id];
                editParameter(kKnobs[knob].id, true);
                return true;
            }
            const int id = switchAt(event.pos.getX(), event.pos.getY());
            if (id >= 0)
            {
                const float value = values[id] >= 0.5f ? 0.0f : 1.0f;
                editParameter(static_cast<uint32_t>(id), true);
                values[id] = value;
                setParameterValue(static_cast<uint32_t>(id), value);
                editParameter(static_cast<uint32_t>(id), false);
                repaint();
                return true;
            }
        }
        else if (dragging >= 0)
        {
            editParameter(kKnobs[dragging].id, false);
            dragging = -1;
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& event) override
    {
        if (dragging < 0)
            return false;
        const double delta = lastY - event.pos.getY();
        lastY = event.pos.getY();
        dragValue += static_cast<float>(delta) / (170.0f * scale());
        dragValue = dragValue < 0.0f ? 0.0f : (dragValue > 1.0f ? 1.0f : dragValue);
        const uint32_t id = kKnobs[dragging].id;
        values[id] = dragValue;
        setParameterValue(id, dragValue);
        repaint();
        return true;
    }

private:
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingModUI)
};

UI* createUI()
{
    return new RingModUI();
}

END_NAMESPACE_DISTRHO
