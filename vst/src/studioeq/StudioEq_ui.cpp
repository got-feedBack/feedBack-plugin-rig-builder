/*
 * Studio EQ UI (DPF NanoVG): 4 band columns (Bass / Lo Mid / Hi Mid / Treble),
 * each with Gain + Freq knobs (mids also Q). Rotary knobs, vertical drag.
 */
#include "DistrhoUI.hpp"
#include "StudioEqParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

static const struct { int idx; int col; int row; } kKnobs[kNumParams] = {
    { kBass, 0, 0 }, { kBassFreq, 0, 1 },
    { kLoMid, 1, 0 }, { kLoMidFreq, 1, 1 }, { kLoMidQ, 1, 2 },
    { kHiMid, 2, 0 }, { kHiMidFreq, 2, 1 }, { kHiMidQ, 2, 2 },
    { kTreble, 3, 0 }, { kTrebleFreq, 3, 1 },
};
static const char* const kColHdr[4] = { "BASS", "LO MID", "HI MID", "TREBLE" };

class StudioEqUI : public UI
{
    float fValues[kNumParams];
    int   fDrag;
    double fLastY;
    float fDragVal;

    float scale() const { return getWidth() / 460.0f; }
    float knobR() const { return getWidth() * 0.062f; }
    float colX(int c) const { return getWidth() * (0.16f + 0.227f * c); }
    float rowY(int r) const { const float ys[3] = { 0.34f, 0.60f, 0.84f }; return getHeight() * ys[r]; }

    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    float freqFor(int idx, float v) const {
        switch (idx) { case kBassFreq: return seqFBass(v); case kLoMidFreq: return seqFLoMid(v);
                       case kHiMidFreq: return seqFHiMid(v); case kTrebleFreq: return seqFTreble(v); }
        return 0.f;
    }
    void valueText(int idx, float v, char* b, size_t n) const {
        if (idx == kBass || idx == kLoMid || idx == kHiMid || idx == kTreble)
            std::snprintf(b, n, "%+.1f dB", seqDb(v));
        else if (idx == kLoMidQ || idx == kHiMidQ)
            std::snprintf(b, n, "Q %.2f", seqQ(v));
        else { float f = freqFor(idx, v); if (f >= 1000.f) std::snprintf(b, n, "%.1fk", f / 1000.f); else std::snprintf(b, n, "%.0f Hz", f); }
    }
    const char* rowLabel(int row) const { return row == 0 ? "Gain" : row == 1 ? "Freq" : "Q"; }

    void drawKnob(int k) {
        const int idx = kKnobs[k].idx;
        const float cx = colX(kKnobs[k].col), cy = rowY(kKnobs[k].row), R = knobR(), f = scale(), n = fValues[idx];
        beginPath(); circle(cx, cy, R);        fillColor(Color(44, 46, 58)); fill();
        beginPath(); circle(cx, cy, R - 3*f);  fillColor(Color(64, 68, 84)); fill();
        // value arc
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n * s / 36.f, a = angleFor(t); float x = cx + (R-3*f)*std::cos(a), y = cy + (R-3*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(110, 170, 240)); strokeWidth(4*f); stroke();
        // pointer
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-9*f)*std::cos(a), cy + (R-9*f)*std::sin(a));
        strokeColor(Color(230, 238, 252)); strokeWidth(3*f); stroke();
        // sub-label + value
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(10*f); fillColor(Color(150, 160, 180)); text(cx, cy + R + 3*f, rowLabel(kKnobs[k].row), NULL);
        char buf[24]; valueText(idx, n, buf, sizeof(buf));
        fontSize(11*f); fillColor(Color(180, 200, 230)); text(cx, cy + R + 16*f, buf, NULL);
    }
    int knobAt(double px, double py) const {
        const float R = knobR();
        for (int k = 0; k < kNumParams; ++k) {
            const float cx = colX(kKnobs[k].col), cy = rowY(kKnobs[k].row), dx = px - cx, dy = py - cy;
            if (dx*dx + dy*dy <= (R+6)*(R+6)) return k;
        }
        return -1;
    }
public:
    StudioEqUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < kNumParams; ++i) fValues[i] = 0.5f;
        setGeometryConstraints(360, 300, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kNumParams) { fValues[i] = v; repaint(); } }
    void onNanoDisplay() override {
        const float W = getWidth(), H = getHeight(), f = scale(), m = 10*f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        beginPath(); rect(0,0,W,H); fillColor(Color(18,18,22)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); fillColor(Color(30,33,42)); fill();
        beginPath(); roundedRect(m,m,W-2*m,H-2*m,16*f); strokeColor(Color(90,150,230)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fontSize(22*f); fillColor(Color(170,200,255)); text(24*f, 18*f, SEQ_PLUGIN_LABEL, NULL);
        // column headers
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (int c = 0; c < 4; ++c) { fontSize(12*f); fillColor(Color(150,160,185)); text(colX(c), H*0.20f, kColHdr[c], NULL); }
        for (int k = 0; k < kNumParams; ++k) drawKnob(k);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioEqUI)
};

UI* createUI() { return new StudioEqUI(); }

END_NAMESPACE_DISTRHO
