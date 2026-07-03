/* Eden WTDI UI — bespoke landscape face matching the real pedal: brushed-gold
 * panel with black base, "EDEN" red logo top-left, two rows of black knobs, and
 * the two red square switches (Bass Boost / Mid Shift) between the EQ knobs.
 *   Top row:    Bass · [Bass Boost] · Mid · [Mid Shift] · Treble
 *   Bottom row: Gain · Enhance · Compressor · Master
 * Vector-crisp at any size (no bitmap scaling), knobs vertical-drag, switches
 * toggle on click. */
#include "DistrhoUI.hpp"
#include "EdenWtdiParams.h"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

// knob layout (cx,cy fraction of W/H, r fraction of W) — param index -> spot
struct Spot { int id; float cx, cy, r; };
static const Spot kKnobs[] = {
    { kBass,   0.190f, 0.300f, 0.058f },
    { kMid,    0.500f, 0.300f, 0.058f },
    { kTreble, 0.810f, 0.300f, 0.058f },
    { kGain,   0.160f, 0.660f, 0.058f },
    { kEnhance,0.385f, 0.660f, 0.058f },
    { kComp,   0.610f, 0.660f, 0.058f },
    { kMaster, 0.835f, 0.660f, 0.058f },
};
static const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));
// the two red square switches (cx,cy fraction, half-size fraction of W)
struct Sw { int id; float cx, cy, h; const char* lbl; };
static const Sw kSwitches[] = {
    { kBassBoost, 0.345f, 0.330f, 0.024f, "BASS\nBOOST" },
    { kMidShift,  0.655f, 0.330f, 0.024f, "MID\nSHIFT"  },
};
static const int kNumSw = (int)(sizeof(kSwitches) / sizeof(kSwitches[0]));

class EdenWtdiUI : public UI
{
    float fValues[kParamCount];
    int   fDrag;          // knob index being dragged, -1 none
    double fLastY;
    float fDragVal;

    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth() / 560.0f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(const Spot& k) {
        const float cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r, f = scale(), n = fValues[k.id];
        // black knob with subtle bevel
        beginPath(); circle(cx, cy, R);        fillColor(Color(20, 20, 22)); fill();
        beginPath(); circle(cx, cy, R - 2*f);  fillColor(Color(44, 45, 50)); fill();
        beginPath(); circle(cx, cy, R - 6*f);  fillColor(Color(26, 27, 30)); fill();
        // value arc (warm amber, reads on gold)
        beginPath();
        for (int s = 0; s <= 36; ++s) { float t = n*s/36.f, a = angleFor(t); float x = cx + (R-2*f)*std::cos(a), y = cy + (R-2*f)*std::sin(a); if (s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(120, 70, 30)); strokeWidth(3.0f*f); stroke();
        // pointer
        const float a = angleFor(n);
        beginPath(); moveTo(cx, cy); lineTo(cx + (R-7*f)*std::cos(a), cy + (R-7*f)*std::sin(a));
        strokeColor(Color(238, 240, 244)); strokeWidth(2.6f*f); stroke();
        // label below (dark, reads on gold)
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(12*f); fillColor(Color(40, 28, 12));
        text(cx, cy + R + 3*f, kEdenWtdiNames[k.id], NULL);
    }

    void drawSwitch(const Sw& s) {
        const float cx = W()*s.cx, cy = H()*s.cy, hs = W()*s.h, f = scale();
        const bool on = fValues[s.id] > 0.5f;
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 3*f);
        fillColor(on ? Color(208, 40, 36) : Color(78, 22, 20)); fill();
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 3*f);
        strokeColor(Color(20, 12, 10)); strokeWidth(1.5f*f); stroke();
        if (on) { beginPath(); circle(cx, cy, hs*0.34f); fillColor(Color(255, 180, 170)); fill(); }
        // label under the switch
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(8.5f*f); fillColor(Color(40, 28, 12));
        // two lines
        const char* l = s.lbl; char line[16]; int li = 0; float ty = cy + hs + 2*f;
        for (const char* p = l; ; ++p) {
            if (*p == '\n' || *p == '\0') { line[li] = '\0'; text(cx, ty, line, NULL); ty += 9*f; li = 0; if (*p == '\0') break; }
            else if (li < 15) line[li++] = *p;
        }
    }

    int knobAt(double px, double py) const {
        for (int i = 0; i < kNumKnobs; ++i) {
            const float dx = px - W()*kKnobs[i].cx, dy = py - H()*kKnobs[i].cy, R = W()*kKnobs[i].r + 6;
            if (dx*dx + dy*dy <= R*R) return i;
        }
        return -1;
    }
    int switchAt(double px, double py) const {
        for (int i = 0; i < kNumSw; ++i) {
            const float hs = W()*kSwitches[i].h + 4;
            if (std::fabs(px - W()*kSwitches[i].cx) <= hs && std::fabs(py - H()*kSwitches[i].cy) <= hs) return i;
        }
        return -1;
    }
public:
    EdenWtdiUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kEdenWtdiDef[i];
        setGeometryConstraints(560 * 3 / 4, 360 * 3 / 4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        const float w = W(), h = H(), f = scale();
        // backdrop
        beginPath(); rect(0, 0, w, h); fillColor(Color(12, 12, 14)); fill();
        // black base box
        const float bx = 10*f, by = 8*f, bw = w - 20*f, bh = h - 16*f, rad = 14*f;
        beginPath(); roundedRect(bx, by, bw, bh, rad); fillColor(Color(24, 22, 20)); fill();
        // gold brushed face plate (most of the body, leaving a black strip at the bottom for the switch)
        const float px = bx + 8*f, py = by + 8*f, pw = bw - 16*f, ph = bh*0.80f;
        Paint gold = linearGradient(0, py, 0, py + ph,
                                    Color(214, 178, 96), Color(168, 132, 60));
        beginPath(); roundedRect(px, py, pw, ph, 8*f); fillPaint(gold); fill();
        beginPath(); roundedRect(px, py, pw, ph, 8*f); strokeColor(Color(120, 92, 40)); strokeWidth(1.5f*f); stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        // EDEN logo box top-left
        beginPath(); roundedRect(px + 8*f, py + 8*f, 56*f, 30*f, 4*f); strokeColor(Color(180, 30, 28)); strokeWidth(2*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fontSize(18*f); fillColor(Color(180, 30, 28));
        text(px + 8*f + 28*f, py + 8*f + 15*f, "EDEN", NULL);
        // title top-right
        textAlign(ALIGN_RIGHT | ALIGN_TOP); fontSize(16*f); fillColor(Color(40, 28, 12));
        text(px + pw - 10*f, py + 6*f, "WTDI", NULL);
        fontSize(9*f); fillColor(Color(70, 52, 28));
        text(px + pw - 10*f, py + 24*f, "Bass Guitar Pre Amplifier", NULL);

        for (int i = 0; i < kNumKnobs; ++i) drawKnob(kKnobs[i]);
        for (int i = 0; i < kNumSw; ++i)    drawSwitch(kSwitches[i]);

        // footswitch on the black strip, bottom centre
        const float fy = by + bh - bh*0.10f;
        beginPath(); circle(w*0.5f, fy, 18*f); fillColor(Color(198,202,208)); fill();
        beginPath(); circle(w*0.5f, fy, 18*f); strokeColor(Color(110,114,120)); strokeWidth(2.5f*f); stroke();
        beginPath(); circle(w*0.5f, fy, 11*f); fillColor(Color(150,155,162)); fill();
        // status LED top-centre of plate
        beginPath(); circle(px + pw*0.5f, py + 14*f, 4*f); fillColor(Color(230, 60, 50)); fill();
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int sw = switchAt(ev.pos.getX(), ev.pos.getY());
            if (sw >= 0) { const int id = kSwitches[sw].id; float nv = fValues[id] > 0.5f ? 0.f : 1.f; fValues[id] = nv; setParameterValue(id, nv); repaint(); return true; }
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdenWtdiUI)
};

UI* createUI() { return new EdenWtdiUI(); }

END_NAMESPACE_DISTRHO
