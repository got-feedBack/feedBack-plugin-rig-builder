/*
 * Shared graphic-EQ UI (DPF NanoVG): N vertical faders, one per band, labelled
 * by frequency with the dB value. Drag a fader up/down to cut/boost. The
 * per-pedal .cpp defines kEqBands/kEqFreqs/kEqNames/EQ_PLUGIN_LABEL/EQ_DB.
 */
#include "DistrhoUI.hpp"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

class GraphicEqUI : public UI
{
    float fValues[kEqBands];
    int   fDrag;

    float scale()  const { return getWidth() / 420.0f; }
    float colW()   const { return (getWidth() - 2 * 18 * scale()) / (float)kEqBands; }
    float colX(int i) const { return 18 * scale() + (i + 0.5f) * colW(); }
    float trackTop()    const { return getHeight() * 0.26f; }
    float trackBottom() const { return getHeight() * 0.82f; }

    float valToY(float v) const { return trackTop() + (1.0f - v) * (trackBottom() - trackTop()); }
    float yToVal(double y) const {
        float v = 1.0f - (float)((y - trackTop()) / (trackBottom() - trackTop()));
        return v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    }
    int colAt(double px) const {
        const float x0 = 18 * scale(), cw = colW();
        int i = (int)((px - x0) / cw);
        return (i >= 0 && i < kEqBands) ? i : -1;
    }

public:
    GraphicEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1) {
        loadSharedResources();
        for (int i = 0; i < kEqBands; ++i) fValues[i] = 0.5f;
        setGeometryConstraints(280, 220, true, false);
    }
protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < (uint32_t)kEqBands) { fValues[index] = value; repaint(); }
    }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0, 0, W, H); fillColor(Color(18, 18, 22)); fill();
        const float m = 10 * f;
        beginPath(); roundedRect(m, m, W - 2*m, H - 2*m, 16*f); fillColor(Color(32, 34, 44)); fill();
        beginPath(); roundedRect(m, m, W - 2*m, H - 2*m, 16*f); strokeColor(Color(90, 150, 230)); strokeWidth(2*f); stroke();

        // title + LED
        beginPath(); circle(W - 48*f, 40*f, 6*f); fillColor(Color(120, 255, 150)); fill();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(24*f); fillColor(Color(170, 200, 255)); text(26*f, 22*f, EQ_PLUGIN_LABEL, NULL);

        const float tT = trackTop(), tB = trackBottom(), midY = (tT + tB) * 0.5f;
        for (int i = 0; i < kEqBands; ++i) {
            const float cx = colX(i);
            // track
            beginPath(); roundedRect(cx - 2.5f*f, tT, 5*f, tB - tT, 2.5f*f); fillColor(Color(54, 56, 68)); fill();
            // 0 dB centre tick
            beginPath(); rect(cx - 8*f, midY, 16*f, 1.0f*f); fillColor(Color(90, 95, 110)); fill();
            // filled portion (centre → handle)
            const float hy = valToY(fValues[i]);
            const float y0 = std::fmin(hy, midY), y1 = std::fmax(hy, midY);
            beginPath(); roundedRect(cx - 2.5f*f, y0, 5*f, y1 - y0, 2.5f*f); fillColor(Color(90, 150, 230)); fill();
            // handle cap
            beginPath(); roundedRect(cx - 13*f, hy - 5*f, 26*f, 10*f, 4*f); fillColor(Color(210, 224, 250)); fill();
            // dB value (top)
            char buf[16]; const float db = (fValues[i] - 0.5f) * (2.0f * EQ_DB);
            std::snprintf(buf, sizeof(buf), "%+.0f", db);
            textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
            fontSize(11*f); fillColor(Color(150, 180, 220)); text(cx, tT - 4*f, buf, NULL);
            // freq label (bottom)
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(11*f); fillColor(Color(220, 220, 232)); text(cx, tB + 6*f, kEqNames[i], NULL);
        }
    }
    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int i = colAt(ev.pos.getX());
            if (i >= 0) { fDrag = i; setFromY(i, ev.pos.getY()); return true; }
        } else if (fDrag >= 0) { editParameter(fDrag, false); fEditing = false; fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) { setFromY(fDrag, ev.pos.getY()); return true; }
        return false;
    }
private:
    bool fEditing = false;
    void setFromY(int i, double y) {
        if (!fEditing) { editParameter(i, true); fEditing = true; }
        const float v = yToVal(y);
        fValues[i] = v; setParameterValue(i, v); repaint();
    }
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphicEqUI)
};

UI* createUI() { return new GraphicEqUI(); }

END_NAMESPACE_DISTRHO
