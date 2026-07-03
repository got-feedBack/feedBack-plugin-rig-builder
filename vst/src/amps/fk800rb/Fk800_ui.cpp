/* Freddy Krueger 800BR UI — a parody of the Gallien-Krueger 800RB front panel:
 * a wide black brushed-metal rack face with a single row of dark knurled knobs,
 * white section legends (INPUT · VOLUME · VOICING FILTERS · ACTIVE EQUALIZATION ·
 * BOOST · CROSSOVER · MASTER VOLUMES), the three square voicing switches, a red
 * power rocker on the right, and the "FK  FREDDY-KRUEGER  800BR" branding along
 * the bottom. Vector-crisp at any size; knobs vertical-drag, switches toggle. */
#include "DistrhoUI.hpp"
#include "Fk800Params.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// knob: param id, centre (frac of W/H), radius (frac of W), name, sub-label
struct Knob { int id; float cx, cy, r; const char* name; const char* sub; };
static const Knob kKnobs[] = {
    { kVolume,    0.135f, 0.50f, 0.027f, "VOLUME",    ""       },
    { kTreble,    0.345f, 0.50f, 0.027f, "TREBLE",    "4kHz"   },
    { kHiMid,     0.420f, 0.50f, 0.027f, "HI-MID",    "1kHz"   },
    { kLoMid,     0.495f, 0.50f, 0.027f, "LO-MID",    "250Hz"  },
    { kBass,      0.570f, 0.50f, 0.027f, "BASS",      "60Hz"   },
    { kBoostLevel,0.650f, 0.50f, 0.027f, "LEVEL",     ""       },
    { kXover,     0.745f, 0.50f, 0.027f, "FREQUENCY", "100-1k" },
    { kMaster100, 0.840f, 0.50f, 0.027f, "100W AMP",  ""       },
    { kMaster300, 0.915f, 0.50f, 0.027f, "300W AMP",  ""       },
};
static const int kNumKnobs = (int)(sizeof(kKnobs) / sizeof(kKnobs[0]));

// square switch: param id, centre (frac), half-size (frac of W), label, vertical?
struct Sw { int id; float cx, cy, h; const char* lbl; };
static const Sw kSwitches[] = {
    { kPad,     0.055f, 0.50f, 0.016f, "-10dB"   },
    { kLoCut,   0.205f, 0.50f, 0.015f, "LO\nCUT" },
    { kContour, 0.245f, 0.50f, 0.015f, "MID\nCONT" },
    { kHiBoost, 0.285f, 0.50f, 0.015f, "HI\nBST" },
    { kBoostOn, 0.700f, 0.50f, 0.016f, "BOOST"   },
    { kBiamp,   0.790f, 0.50f, 0.016f, "BIAMP"   },
};
static const int kNumSw = (int)(sizeof(kSwitches) / sizeof(kSwitches[0]));

// section legends along the top
struct Leg { float cx; const char* t; };
static const Leg kLegends[] = {
    { 0.055f, "INPUT" },
    { 0.135f, "VOLUME" },
    { 0.245f, "VOICING FILTERS" },
    { 0.4575f, "ACTIVE EQUALIZATION" },
    { 0.675f, "BOOST" },
    { 0.745f, "CROSSOVER" },
    { 0.8775f, "MASTER VOLUMES" },
};
static const int kNumLeg = (int)(sizeof(kLegends) / sizeof(kLegends[0]));

class Fk800UI : public UI
{
    float fValues[kParamCount];
    int   fDrag;          // knob index being dragged, -1 none
    double fLastY;
    float fDragVal;

    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float scale() const { return getWidth() / 960.0f; }
    static float angleFor(float n) { return (135.0f + n * 270.0f) * 3.14159265f / 180.0f; }

    void drawKnob(const Knob& k) {
        const float cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r, f = scale(), n = fValues[k.id];
        // chrome ring + dark knurled body
        beginPath(); circle(cx, cy, R + 2.5f*f); fillColor(Color(150,152,156)); fill();
        beginPath(); circle(cx, cy, R);          fillColor(Color(28,28,30));    fill();
        Paint body = radialGradient(cx - R*0.3f, cy - R*0.3f, R*0.2f, R*1.2f,
                                    Color(70,71,75), Color(22,22,24));
        beginPath(); circle(cx, cy, R - 1.5f*f); fillPaint(body); fill();
        // knurl ticks around the rim
        strokeColor(Color(12,12,14)); strokeWidth(1.0f*f);
        for (int t = 0; t < 24; ++t) {
            const float a = t * (6.2831853f / 24.f);
            beginPath();
            moveTo(cx + (R-1.5f*f)*std::cos(a), cy + (R-1.5f*f)*std::sin(a));
            lineTo(cx + (R-4.0f*f)*std::cos(a), cy + (R-4.0f*f)*std::sin(a));
            stroke();
        }
        // value arc (cool white)
        beginPath();
        for (int s = 0; s <= 32; ++s) { float t=n*s/32.f, a=angleFor(t); float x=cx+(R+4.5f*f)*std::cos(a), y=cy+(R+4.5f*f)*std::sin(a); if(s==0) moveTo(x,y); else lineTo(x,y); }
        strokeColor(Color(210,214,220)); strokeWidth(1.6f*f); stroke();
        // pointer line
        const float a = angleFor(n);
        beginPath(); moveTo(cx + R*0.15f*std::cos(a), cy + R*0.15f*std::sin(a));
        lineTo(cx + (R-3.5f*f)*std::cos(a), cy + (R-3.5f*f)*std::sin(a));
        strokeColor(Color(245,246,250)); strokeWidth(2.4f*f); stroke();
        // name + sub label below
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(10.5f*f); fillColor(Color(214,216,220));
        text(cx, cy + R + 6*f, k.name, NULL);
        if (k.sub[0]) { fontSize(8.0f*f); fillColor(Color(120,122,128)); text(cx, cy + R + 18*f, k.sub, NULL); }
    }

    void drawSwitch(const Sw& s) {
        const float cx = W()*s.cx, cy = H()*s.cy, hs = W()*s.h, f = scale();
        const bool on = fValues[s.id] > 0.5f;
        // recessed black square switch
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 2.5f*f);
        fillColor(on ? Color(40,44,40) : Color(18,18,20)); fill();
        beginPath(); roundedRect(cx-hs, cy-hs, hs*2, hs*2, 2.5f*f);
        strokeColor(Color(90,92,96)); strokeWidth(1.2f*f); stroke();
        // rocker nub (slides up when on)
        const float ny = on ? cy - hs*0.4f : cy + hs*0.4f;
        beginPath(); roundedRect(cx-hs*0.7f, ny-hs*0.42f, hs*1.4f, hs*0.84f, 1.5f*f);
        fillColor(Color(150,152,156)); fill();
        // green LED when engaged
        if (on) { beginPath(); circle(cx, cy - hs - 4*f, 2.2f*f); fillColor(Color(70,235,90)); fill(); }
        // label under switch (supports a \n)
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(7.5f*f); fillColor(Color(190,192,196));
        const char* l = s.lbl; char line[16]; int li = 0; float ty = cy + hs + 5*f;
        for (const char* p = l; ; ++p) {
            if (*p == '\n' || *p == '\0') { line[li]='\0'; text(cx, ty, line, NULL); ty += 8*f; li=0; if(*p=='\0') break; }
            else if (li < 15) line[li++] = *p;
        }
    }

    void drawJack(float cx, float cy, float r, const char* lbl, float f) {
        beginPath(); circle(cx, cy, r);        fillColor(Color(20,20,22)); fill();
        beginPath(); circle(cx, cy, r);        strokeColor(Color(120,122,126)); strokeWidth(1.6f*f); stroke();
        beginPath(); circle(cx, cy, r*0.45f);  fillColor(Color(40,40,44)); fill();
        if (lbl) { textAlign(ALIGN_CENTER | ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(190,192,196)); text(cx, cy + r + 4*f, lbl, NULL); }
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
            const float hs = W()*kSwitches[i].h + 5;
            if (std::fabs(px - W()*kSwitches[i].cx) <= hs && std::fabs(py - H()*kSwitches[i].cy) <= hs) return i;
        }
        return -1;
    }
public:
    Fk800UI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), fDrag(-1), fLastY(0), fDragVal(0.5f) {
        loadSharedResources();
        for (int i = 0; i < kParamCount; ++i) fValues[i] = kFk800Def[i];
        setGeometryConstraints(960 * 3 / 5, 300 * 3 / 5, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)kParamCount) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        const float w = W(), h = H(), f = scale();
        // backdrop
        beginPath(); rect(0, 0, w, h); fillColor(Color(8,8,9)); fill();
        // brushed black panel with a subtle vertical sheen
        const float bx = 6*f, by = 6*f, bw = w - 12*f, bh = h - 12*f, rad = 8*f;
        Paint panel = linearGradient(0, by, 0, by + bh, Color(46,47,50), Color(20,20,22));
        beginPath(); roundedRect(bx, by, bw, bh, rad); fillPaint(panel); fill();
        beginPath(); roundedRect(bx, by, bw, bh, rad); strokeColor(Color(70,71,75)); strokeWidth(1.5f*f); stroke();
        // faint horizontal brush lines
        strokeColor(Color(255,255,255,8)); strokeWidth(1.0f*f);
        for (float y = by + 6*f; y < by + bh*0.78f; y += 3.0f*f) { beginPath(); moveTo(bx+6*f, y); lineTo(bx+bw-6*f, y); stroke(); }

        fontFace(NANOVG_DEJAVU_SANS_TTF);

        // section legends (top), each over a thin underline like the real panel
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        for (int i = 0; i < kNumLeg; ++i) {
            const float lx = w*kLegends[i].cx, ly = h*0.165f;
            fontSize(9.5f*f); fillColor(Color(224,226,230));
            text(lx, ly, kLegends[i].t, NULL);
            beginPath(); moveTo(lx - 40*f, ly + 8*f); lineTo(lx + 40*f, ly + 8*f);
            strokeColor(Color(120,122,126)); strokeWidth(1.0f*f); stroke();
        }

        // input jack (INPUT section)
        drawJack(w*0.030f, h*0.50f, 9*f, NULL, f);

        // footswitch jack (BOOST section)
        drawJack(w*0.650f - 0*f, h*0.50f, 0.0f, NULL, f); // (knob occupies this; jack drawn separately below)
        drawJack(w*0.620f, h*0.74f, 7*f, "FOOTSW", f);

        for (int i = 0; i < kNumKnobs; ++i) drawKnob(kKnobs[i]);
        for (int i = 0; i < kNumSw; ++i)    drawSwitch(kSwitches[i]);

        // red power rocker on the far right
        const float pxc = w*0.965f, pyc = h*0.50f, pw = 12*f, ph = 26*f;
        beginPath(); roundedRect(pxc-pw, pyc-ph, pw*2, ph*2, 3*f); fillColor(Color(16,16,18)); fill();
        beginPath(); roundedRect(pxc-pw, pyc-ph, pw*2, ph*2, 3*f); strokeColor(Color(80,82,86)); strokeWidth(1.2f*f); stroke();
        beginPath(); roundedRect(pxc-pw*0.7f, pyc-ph*0.55f, pw*1.4f, ph*0.55f, 2*f); fillColor(Color(170,30,28)); fill();
        textAlign(ALIGN_CENTER | ALIGN_TOP); fontSize(7.5f*f); fillColor(Color(190,192,196));
        text(pxc, pyc + ph + 3*f, "POWER", NULL);

        // ── bottom branding strip ──
        const float baseY = by + bh - 6*f;
        // "FK" logo box
        const float lgx = bx + 16*f, lgy = baseY - 26*f, lgw = 34*f, lgh = 22*f;
        beginPath(); roundedRect(lgx, lgy, lgw, lgh, 3*f); strokeColor(Color(232,234,238)); strokeWidth(2.2f*f); stroke();
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fontSize(15*f); fillColor(Color(232,234,238));
        text(lgx + lgw*0.5f, lgy + lgh*0.5f, "FK", NULL);
        // wordmark
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fontSize(20*f); fillColor(Color(236,238,242));
        text(lgx + lgw + 12*f, lgy + lgh*0.5f - 1*f, "FREDDY-KRUEGER", NULL);
        // model + tagline on the right
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM); fontSize(15*f); fillColor(Color(236,238,242));
        text(bx + bw - 16*f, baseY, "800BR", NULL);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM); fontSize(6.5f*f); fillColor(Color(120,122,126));
        text(bx + bw - 16*f, baseY - 16*f, "400W BIAMP BASS SYSTEM", NULL);
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
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Fk800UI)
};

UI* createUI() { return new Fk800UI(); }

END_NAMESPACE_DISTRHO
