/*
 * pedalkit.hpp — rich, skeuomorphic stompbox UI base for the bundled pedal VSTs.
 *
 * Copyright-free recreations: each pedal's UI captures the LOOK of the real
 * pedal it models (enclosure colour, knob style, switches, footswitch, accent
 * graphics) WITHOUT any real brand/model name or logo. A subclass registers its
 * knobs/toggles + colours and draws the body in drawFace(); this base renders
 * the controls on top and handles drag/click.
 *
 * Primitives provided (vector, crisp at any size — no bitmaps):
 *   enclosure(), screw(), metalKnob(), batToggle(), footswitchRound(),
 *   footswitchBar(), ledDot(), accentLine(), label()/title().
 */
#ifndef PEDALKIT_HPP
#define PEDALKIT_HPP
#include "DistrhoUI.hpp"
#include "fonts_data.hpp"
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

class PedalKitUI : public UI
{
protected:
    struct Ctl { int id; float cx, cy, r; int kind; float cr, cg, cb; int style; }; // kind 0=knob,1=toggle2,2=toggle3; style 0=pointer 1=chief 2=davies 3=knurled 4=chicken
    static const int kMaxCtl = 16;
    Ctl  ctl[kMaxCtl];
    int  nctl = 0;
    const char* const* names_ = nullptr;
    float fValues[kMaxCtl];
    int  nParams_ = 0;
    int  baseW_, baseH_;
    int  fDrag = -1;
    double fLastY = 0;
    float fDragVal = 0.f;
    // colours the subclass sets for control rendering — warm off-white, not
    // pure white (more realistic). Pure black is likewise avoided in bodies.
    Color labelClr = Color(206, 204, 194);
    Color tickClr  = Color(150, 150, 150);
    Color pointerClr = Color(214, 210, 198);
    // embedded fonts (set in ctor): condensed-caps, modern-sans, heavy-display
    int fBebas = -1, fBarlow = -1, fAnton = -1, fSerif = -1;
    int labelFont_ = -1;   // font used for knob labels (defaults to modern sans)
    bool knobLabels_ = true; // when false, metalKnob skips its auto label (subclass draws its own)
    void face(int id) { if (id >= 0) fontFaceId(id); else fontFace(NANOVG_DEJAVU_SANS_TTF); }

    // ── pre-generated wear (scratches + grime), stable across repaints ────
    struct Scr { float x0, y0, x1, y1; bool light; int a; };
    struct Dirt { float x, y, r; int a; };
    Scr  scr_[28]; Dirt dirt_[16]; int nscr_ = 0, ndirt_ = 0;
    uint32_t rng_ = 0x2545F491u;
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return ((rng_ >> 8) & 0xFFFFFF) / 16777216.0f; }
    void setWearSeed(uint32_t s) { rng_ = s ? s : 1u; genWear(); }
    void genWear() {
        nscr_ = 14;
        for (int i = 0; i < nscr_; ++i) {
            float x0 = rnd(), y0 = rnd(), ang = rnd() * 6.2831853f, len = 0.025f + rnd() * 0.08f;
            scr_[i] = { x0, y0, x0 + std::cos(ang) * len, y0 + std::sin(ang) * len, rnd() > 0.5f, 2 + (int)(rnd() * 3) };
        }
        ndirt_ = 7;
        for (int i = 0; i < ndirt_; ++i)
            dirt_[i] = { rnd(), rnd(), 0.012f + rnd() * 0.025f, 2 + (int)(rnd() * 3) };
    }
    bool wearOn_ = false;   // disabled by user request — clean bodies
    void wear() {
        if (!wearOn_) return;
        const float f = sc();
        // grime blobs
        for (int i = 0; i < ndirt_; ++i)
            { beginPath(); circle(W()*dirt_[i].x, H()*dirt_[i].y, W()*dirt_[i].r); fillColor(Color(0,0,0,dirt_[i].a)); fill(); }
        // fine scratches
        for (int i = 0; i < nscr_; ++i) {
            beginPath(); moveTo(W()*scr_[i].x0, H()*scr_[i].y0); lineTo(W()*scr_[i].x1, H()*scr_[i].y1);
            strokeColor(scr_[i].light ? Color(255,255,255,scr_[i].a) : Color(0,0,0,scr_[i].a+3));
            strokeWidth(0.7f*f); stroke();
        }
        // corner vignette (barely-there, ~4%)
        Paint v = radialGradient(W()*0.5f, H()*0.5f, W()*0.34f, W()*0.84f, Color(0,0,0,0), Color(0,0,0,11));
        beginPath(); rect(0,0,W(),H()); fillPaint(v); fill();
    }

    float W() const { return getWidth(); }
    float H() const { return getHeight(); }
    float sc() const { return getWidth() / (float)baseW_; }
    static float angleFor(float n) { return (135.f + n * 270.f) * 3.14159265f / 180.f; }
    static int cl(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

    void addKnob(int id, float cx, float cy, float r, int cr, int cg, int cb, int style = 0) {
        if (nctl < kMaxCtl) ctl[nctl++] = { id, cx, cy, r, 0, (float)cr, (float)cg, (float)cb, style };
    }
    void addToggle(int id, float cx, float cy, float r, int states) {
        if (nctl < kMaxCtl) ctl[nctl++] = { id, cx, cy, r, states >= 3 ? 2 : 1, 0, 0, 0, 0 };
    }

    // ── primitives ───────────────────────────────────────────────────────
    void screw(float cx, float cy, float r) {
        const float f = sc();
        Paint p = radialGradient(cx - r * 0.3f, cy - r * 0.3f, r * 0.15f, r * 1.2f,
                                 Color(214, 216, 220), Color(120, 122, 128));
        beginPath(); circle(cx, cy, r); fillPaint(p); fill();
        beginPath(); circle(cx, cy, r); strokeColor(Color(64, 66, 72)); strokeWidth(1.f * f); stroke();
        const float a = 0.6f; // fixed slot angle
        beginPath();
        moveTo(cx - r * 0.62f * std::cos(a), cy - r * 0.62f * std::sin(a));
        lineTo(cx + r * 0.62f * std::cos(a), cy + r * 0.62f * std::sin(a));
        strokeColor(Color(70, 72, 78)); strokeWidth(1.4f * f); stroke();
    }

    // enclosure with a soft vertical metallic gradient + bevel highlight
    void enclosure(int r, int g, int b) {
        const float f = sc(), w = W(), h = H();
        beginPath(); rect(0, 0, w, h); fillColor(Color(10, 10, 12)); fill();
        const float m = 8 * f;
        Paint body = linearGradient(0, m, 0, h - m,
                                    Color(cl(r + 22), cl(g + 22), cl(b + 22)),
                                    Color(cl(r - 18), cl(g - 18), cl(b - 18)));
        beginPath(); roundedRect(m, m, w - 2 * m, h - 2 * m, 14 * f); fillPaint(body); fill();
        // edge
        beginPath(); roundedRect(m, m, w - 2 * m, h - 2 * m, 14 * f);
        strokeColor(Color(0, 0, 0, 120)); strokeWidth(2 * f); stroke();
        // 4 screws
        const float sr = 6 * f, off = 22 * f;
        screw(m + off, m + off); screw(w - m - off, m + off);
        screw(m + off, h - m - off); screw(w - m - off, h - m - off);
        (void)sr;
    }
    void screw(float cx, float cy) { screw(cx, cy, 6 * sc()); }

    // a thin accent line/stripe (silkscreen)
    void accentLine(float x0, float y0, float x1, float y1, Color c, float wpx) {
        beginPath(); moveTo(x0, y0); lineTo(x1, y1); strokeColor(c); strokeWidth(wpx * sc()); stroke();
    }

    void ledDot(float cx, float cy, float r, bool on, int cr, int cg, int cb) {
        if (on) { beginPath(); circle(cx, cy, r * 2.0f); fillColor(Color(cr, cg, cb, 60)); fill(); }
        beginPath(); circle(cx, cy, r);
        fillColor(on ? Color(cr, cg, cb) : Color(60, 30, 28)); fill();
        beginPath(); circle(cx - r*0.3f, cy - r*0.3f, r*0.35f); fillColor(Color(255,255,255, on?150:40)); fill();
    }

    // ── knob styles (matched to each real pedal) ──────────────────────────
    void knobPointer(float cx, float cy, float R, float n, const Ctl& k) {   // round + white pointer + tick fan
        const float f = sc();
        beginPath(); circle(cx, cy, R*1.16f); fillColor(Color(16,16,18)); fill();
        strokeColor(tickClr); strokeWidth(1.4f*f);
        for (int t=0;t<=10;++t){ float a=angleFor(t/10.f),r0=R*1.22f,r1=R*1.36f;
            beginPath(); moveTo(cx+r0*std::cos(a),cy+r0*std::sin(a)); lineTo(cx+r1*std::cos(a),cy+r1*std::sin(a)); stroke(); }
        Paint cap=radialGradient(cx-R*0.4f,cy-R*0.5f,R*0.1f,R*1.3f,
            Color(cl((int)k.cr+45),cl((int)k.cg+45),cl((int)k.cb+45)),Color((int)(k.cr*0.45f),(int)(k.cg*0.45f),(int)(k.cb*0.45f)));
        beginPath(); circle(cx,cy,R); fillPaint(cap); fill();
        beginPath(); circle(cx,cy,R); strokeColor(Color(8,8,10)); strokeWidth(1.5f*f); stroke();
        float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.18f*std::cos(a),cy+R*0.18f*std::sin(a)); lineTo(cx+R*0.86f*std::cos(a),cy+R*0.86f*std::sin(a));
        strokeColor(pointerClr); strokeWidth(3*f); stroke();
    }
    void knobChief(float cx, float cy, float R, float n, const Ctl&) {        // black knurled cylinder + white line
        const float f = sc();
        beginPath(); circle(cx,cy,R); fillColor(Color(22,22,24)); fill();
        strokeColor(Color(58,60,64)); strokeWidth(1.0f*f);
        for (int i=0;i<36;++i){ float a=i/36.f*6.2831853f,r0=R*0.80f,r1=R*0.99f;
            beginPath(); moveTo(cx+r0*std::cos(a),cy+r0*std::sin(a)); lineTo(cx+r1*std::cos(a),cy+r1*std::sin(a)); stroke(); }
        Paint cap=radialGradient(cx-R*0.3f,cy-R*0.4f,R*0.1f,R*0.85f,Color(52,53,58),Color(22,22,25));
        beginPath(); circle(cx,cy,R*0.74f); fillPaint(cap); fill();
        beginPath(); circle(cx,cy,R); strokeColor(Color(6,6,8)); strokeWidth(1.4f*f); stroke();
        float a=angleFor(n);
        beginPath(); moveTo(cx,cy); lineTo(cx+R*0.92f*std::cos(a),cy+R*0.92f*std::sin(a));
        strokeColor(pointerClr); strokeWidth(2.6f*f); stroke();
        beginPath(); circle(cx+R*0.84f*std::cos(a),cy+R*0.84f*std::sin(a),2.0f*f); fillColor(pointerClr); fill();
    }
    void knobDavies(float cx, float cy, float R, float n, const Ctl&) {      // wide skirt + domed cap (fuzz/big-box)
        const float f = sc();
        beginPath(); circle(cx,cy,R*1.30f); fillColor(Color(14,14,16)); fill();
        beginPath(); circle(cx,cy,R*1.30f); strokeColor(Color(0,0,0,150)); strokeWidth(1.5f*f); stroke();
        Paint cap=radialGradient(cx-R*0.4f,cy-R*0.5f,R*0.1f,R*1.15f,Color(50,50,54),Color(18,18,20));
        beginPath(); circle(cx,cy,R); fillPaint(cap); fill();
        beginPath(); circle(cx,cy,R); strokeColor(Color(6,6,8)); strokeWidth(1.5f*f); stroke();
        float a=angleFor(n);
        beginPath(); moveTo(cx,cy); lineTo(cx+R*1.20f*std::cos(a),cy+R*1.20f*std::sin(a));
        strokeColor(pointerClr); strokeWidth(3.4f*f); stroke();
    }
    void knobKnurled(float cx, float cy, float R, float n, const Ctl&) {     // aluminium knurled (modern OD)
        const float f = sc();
        Paint cap=radialGradient(cx-R*0.4f,cy-R*0.5f,R*0.1f,R*1.2f,Color(98,100,106),Color(38,39,43));
        beginPath(); circle(cx,cy,R); fillPaint(cap); fill();
        strokeColor(Color(150,152,158,120)); strokeWidth(0.8f*f);
        for (int i=0;i<48;++i){ float a=i/48.f*6.2831853f,r0=R*0.86f,r1=R*0.99f;
            beginPath(); moveTo(cx+r0*std::cos(a),cy+r0*std::sin(a)); lineTo(cx+r1*std::cos(a),cy+r1*std::sin(a)); stroke(); }
        beginPath(); circle(cx,cy,R); strokeColor(Color(10,10,12)); strokeWidth(1.4f*f); stroke();
        float a=angleFor(n);
        beginPath(); moveTo(cx+R*0.15f*std::cos(a),cy+R*0.15f*std::sin(a)); lineTo(cx+R*0.9f*std::cos(a),cy+R*0.9f*std::sin(a));
        strokeColor(Color(22,22,24)); strokeWidth(2.6f*f); stroke();
    }
    void knobChicken(float cx, float cy, float R, float n, const Ctl&) {     // chickenhead beak
        const float f = sc(); float a=angleFor(n);
        Paint cap=radialGradient(cx-R*0.2f,cy-R*0.3f,R*0.1f,R*0.8f,Color(40,40,44),Color(14,14,16));
        beginPath(); circle(cx,cy,R*0.62f); fillPaint(cap); fill();
        float bx=cx+R*1.08f*std::cos(a), by=cy+R*1.08f*std::sin(a), pa=a+1.5708f;
        beginPath(); moveTo(bx,by);
        lineTo(cx+R*0.5f*std::cos(a)+R*0.32f*std::cos(pa), cy+R*0.5f*std::sin(a)+R*0.32f*std::sin(pa));
        lineTo(cx+R*0.5f*std::cos(a)-R*0.32f*std::cos(pa), cy+R*0.5f*std::sin(a)-R*0.32f*std::sin(pa));
        closePath(); fillColor(Color(236,232,224)); fill();
        beginPath(); circle(cx,cy,R*0.62f); strokeColor(Color(8,8,10)); strokeWidth(1.4f*f); stroke();
    }
    void metalKnob(const Ctl& k) {
        const float f = sc(), cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r, n = fValues[k.id]; (void)f;
        switch (k.style) {
            case 1: knobChief(cx,cy,R,n,k); break;
            case 2: knobDavies(cx,cy,R,n,k); break;
            case 3: knobKnurled(cx,cy,R,n,k); break;
            case 4: knobChicken(cx,cy,R,n,k); break;
            default: knobPointer(cx,cy,R,n,k); break;
        }
        if (names_ && knobLabels_) {
            face(labelFont_); textAlign(ALIGN_CENTER | ALIGN_TOP); fontSize(12.5f*sc()); fillColor(labelClr);
            text(cx, cy + R*1.45f + 2*sc(), names_[k.id], NULL);
        }
    }

    void batToggle(const Ctl& k) {
        const float f = sc(), cx = W()*k.cx, cy = H()*k.cy, R = W()*k.r;
        const int states = (k.kind == 2) ? 3 : 2;
        // pos: 0 (up) .. states-1 (down)
        int pos = (states == 3) ? (fValues[k.id] < 0.25f ? 0 : (fValues[k.id] < 0.75f ? 1 : 2))
                                : (fValues[k.id] > 0.5f ? 0 : 1);
        // body plate
        beginPath(); roundedRect(cx - R*0.8f, cy - R*1.5f, R*1.6f, R*3.0f, R*0.5f);
        fillColor(Color(30,30,34)); fill();
        beginPath(); roundedRect(cx - R*0.8f, cy - R*1.5f, R*1.6f, R*3.0f, R*0.5f);
        strokeColor(Color(0,0,0,140)); strokeWidth(1.2f*f); stroke();
        // chrome bat in the selected position
        float ty = cy + (pos - (states-1)*0.5f) * R*0.9f;
        Paint bat = linearGradient(cx-R*0.4f, ty-R*0.6f, cx+R*0.4f, ty+R*0.6f, Color(235,237,240), Color(120,122,128));
        beginPath(); roundedRect(cx - R*0.42f, ty - R*0.7f, R*0.84f, R*1.4f, R*0.3f); fillPaint(bat); fill();
        beginPath(); roundedRect(cx - R*0.42f, ty - R*0.7f, R*0.84f, R*1.4f, R*0.3f); strokeColor(Color(70,72,78)); strokeWidth(1*f); stroke();
        if (names_) { face(labelFont_); textAlign(ALIGN_CENTER|ALIGN_TOP); fontSize(11*f); fillColor(labelClr);
            text(cx, cy + R*1.7f + 2*f, names_[k.id], NULL); }
    }

    void footswitchRound(float cx, float cy, float R) {
        const float f = sc();
        Paint ring = radialGradient(cx, cy, R*0.4f, R*1.15f, Color(176,178,184), Color(96,98,104));
        beginPath(); circle(cx, cy, R*1.12f); fillPaint(ring); fill();
        beginPath(); circle(cx, cy, R*1.12f); strokeColor(Color(40,42,46)); strokeWidth(2*f); stroke();
        beginPath(); circle(cx, cy, R*0.78f); fillColor(Color(150,153,159)); fill();
        beginPath(); circle(cx - R*0.25f, cy - R*0.3f, R*0.34f); fillColor(Color(255,255,255,70)); fill();
    }
    void footswitchBar(float cx, float cy, float w) {
        const float f = sc();
        Paint g = linearGradient(0, cy - w*0.28f, 0, cy + w*0.28f, Color(206,208,214), Color(120,122,128));
        beginPath(); roundedRect(cx - w*0.5f, cy - w*0.28f, w, w*0.56f, w*0.12f); fillPaint(g); fill();
        beginPath(); roundedRect(cx - w*0.5f, cy - w*0.28f, w, w*0.56f, w*0.12f); strokeColor(Color(60,62,66)); strokeWidth(2*f); stroke();
    }

    // text inside a thin outlined rectangle (the boxed labels on many pedals)
    void boxedLabel(float cx, float cy, float hw, float hh, const char* s, float size,
                    Color line, Color txt, int fid) {
        const float f = sc();
        const float x0 = (cx - hw) * W(), y0 = (cy - hh) * H(), w = 2*hw*W(), h = 2*hh*H();
        beginPath(); roundedRect(x0, y0, w, h, 3*f); strokeColor(line); strokeWidth(1.6f*f); stroke();
        face(fid); fontSize(size*f); fillColor(txt); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx*W(), cy*H(), s, NULL);
    }
    // Chief-compact style enclosure: coloured body, a darker name strip near the
    // top, and the big rubber treadle footswitch over the lower half. The
    // subclass draws the strip text + knobs in the upper area (y ~0.17..0.40).
    void chiefPedal(int r, int g, int b) {
        const float f = sc(), w = W(), h = H(), m = 7*f;
        beginPath(); rect(0, 0, w, h); fillColor(Color(10, 10, 12)); fill();
        Paint body = linearGradient(0, m, 0, h - m, Color(cl(r+18),cl(g+18),cl(b+18)), Color(cl(r-14),cl(g-14),cl(b-14)));
        beginPath(); roundedRect(m, m, w-2*m, h-2*m, 12*f); fillPaint(body); fill();
        beginPath(); roundedRect(m, m, w-2*m, h-2*m, 12*f); strokeColor(Color(0,0,0,120)); strokeWidth(2*f); stroke();
        // black control plate behind the knobs (the recessed top section)
        beginPath(); roundedRect(m+11*f, h*0.10f, w-2*m-22*f, h*0.235f, 6*f);
        fillColor(Color(20,20,22)); fill();
        beginPath(); roundedRect(m+11*f, h*0.10f, w-2*m-22*f, h*0.235f, 6*f);
        strokeColor(Color(0,0,0,120)); strokeWidth(1.2f*f); stroke();
        // status LED top-centre (no top screws — chief compacts have none up top)
        ledDot(w*0.5f, h*0.072f, 4.5f*f, true, 224, 70, 58);
        // big treadle (the raised footswitch pad) — the BODY colour (slightly
        // darker), shown raised via a border + front-lip highlight. The subclass
        // engraves the big name on it. A black band runs along its bottom.
        // treadle = FULL pedal width (just inside the enclosure edge), body colour.
        const float tx = m+4*f, tw = w-2*m-8*f, tyTop = h*0.42f, tBot = h - m - 6*f;
        Paint tre = linearGradient(0, tyTop, 0, tBot, Color(cl(r-2),cl(g-2),cl(b-2)), Color(cl(r-16),cl(g-16),cl(b-16)));
        beginPath(); roundedRect(tx, tyTop, tw, tBot - tyTop, 12*f); fillPaint(tre); fill();
        beginPath(); roundedRect(tx, tyTop, tw, 12*f, 12*f); fillColor(Color(255,255,255,22)); fill();   // raised front lip
        beginPath(); roundedRect(tx, tyTop, tw, tBot - tyTop, 12*f); strokeColor(Color(0,0,0,120)); strokeWidth(1.6f*f); stroke();
        // lower half = BLACK step pad, inset so the body-colour border shows around it
        const float padTop = h*0.705f, padX = tx + 12*f, padW = tw - 24*f, padBot = tBot - 9*f;
        beginPath(); roundedRect(padX, padTop, padW, padBot - padTop, 10*f); fillColor(Color(20,20,22)); fill();
        beginPath(); roundedRect(padX, padTop, padW, padBot - padTop, 10*f); strokeColor(Color(0,0,0,90)); strokeWidth(1*f); stroke();
        treadleTop_ = tyTop / h; treadleBot_ = tBot / h;
    }
    float treadleTop_ = 0.42f, treadleBot_ = 0.93f;
    // flat dark name text (no white halo) — for the pedal name on the treadle
    void embossText(float cx, float cy, float size, const char* s, int fid) {
        const float f = sc();
        face(fid); fontSize(size*f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(14,16,22)); text(cx*W(), cy*H(), s, NULL);
    }
    // text with a contrasting outline (e.g. red fill + white outline wordmarks)
    void outlineText(float cx, float cy, float size, Color fillC, Color outC, const char* s, int fid) {
        const float o = 1.8f * sc();
        face(fid); fontSize(size*sc()); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(outC);
        for (float dx=-o; dx<=o; dx+=o) for (float dy=-o; dy<=o; dy+=o)
            if (dx||dy) text(cx*W()+dx, cy*H()+dy, s, NULL);
        fillColor(fillC); text(cx*W(), cy*H(), s, NULL);
    }
    void title(const char* s, Color c, float cy, float size, int fid) {
        face(fid); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fontSize(size * sc()); fillColor(c);
        text(W()*0.5f, H()*cy, s, NULL);
    }
    void textC(float cx, float cy, float size, Color c, const char* s, int fid) {
        face(fid); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fontSize(size * sc()); fillColor(c);
        text(W()*cx, H()*cy, s, NULL);
    }
    // letter-spaced caps line (industrial label look)
    void textSpaced(float cx, float cy, float size, Color c, const char* s, int fid, float spacing) {
        face(fid); fontSize(size * sc()); fillColor(c);
        textLetterSpacing(spacing * sc()); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(W()*cx, H()*cy, s, NULL); textLetterSpacing(0.f);
    }

    int knobAt(double px, double py) const {
        for (int i = 0; i < nctl; ++i) {
            const float dx = px - W()*ctl[i].cx, dy = py - H()*ctl[i].cy;
            const float R = W()*ctl[i].r * (ctl[i].kind == 0 ? 1.2f : 1.6f) + 6;
            if (dx*dx + dy*dy <= R*R) return i;
        }
        return -1;
    }

    virtual void drawFace() = 0;

public:
    PedalKitUI(int w, int h, int nparams, const float* defs)
        : UI(w, h), baseW_(w), baseH_(h), nParams_(nparams) {
        loadSharedResources();
        fBebas  = createFontFromMemory("pk_bebas",  pk_bebas_ttf,  pk_bebas_ttf_len,  false);
        fBarlow = createFontFromMemory("pk_barlow", pk_barlow_ttf, pk_barlow_ttf_len, false);
        fAnton  = createFontFromMemory("pk_anton",  pk_anton_ttf,  pk_anton_ttf_len,  false);
        fSerif  = createFontFromMemory("pk_serif",  pk_serif_ttf,  pk_serif_ttf_len,  false);
        labelFont_ = fBarlow;
        for (int i = 0; i < nparams && i < kMaxCtl; ++i) fValues[i] = defs[i];
        genWear();
        setGeometryConstraints(w * 3 / 4, h * 3 / 4, true, false);
    }
protected:
    void parameterChanged(uint32_t i, float v) override { if (i < (uint32_t)nParams_) { fValues[i] = v; repaint(); } }

    void onNanoDisplay() override {
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        drawFace();
        wear();
        for (int i = 0; i < nctl; ++i) {
            if (ctl[i].kind == 0) metalKnob(ctl[i]);
            else                  batToggle(ctl[i]);
        }
    }

    bool onMouse(const MouseEvent& ev) override {
        if (ev.button != 1) return false;
        if (ev.press) {
            const int k = knobAt(ev.pos.getX(), ev.pos.getY());
            if (k >= 0) {
                if (ctl[k].kind == 0) {
                    fDrag = k; fLastY = ev.pos.getY(); fDragVal = fValues[ctl[k].id];
                    editParameter(ctl[k].id, true); return true;
                } else {
                    const int id = ctl[k].id; float v = fValues[id];
                    if (ctl[k].kind == 2) v = (v < 0.25f) ? 0.5f : (v < 0.75f ? 1.0f : 0.0f);
                    else                  v = (v > 0.5f) ? 0.0f : 1.0f;
                    fValues[id] = v; setParameterValue(id, v); repaint(); return true;
                }
            }
        } else if (fDrag >= 0) { editParameter(ctl[fDrag].id, false); fDrag = -1; return true; }
        return false;
    }
    bool onMotion(const MotionEvent& ev) override {
        if (fDrag >= 0) {
            const double dy = fLastY - ev.pos.getY(); fLastY = ev.pos.getY();
            fDragVal += (float)dy / (170.f * sc());
            if (fDragVal < 0.f) fDragVal = 0.f; if (fDragVal > 1.f) fDragVal = 1.f;
            fValues[ctl[fDrag].id] = fDragVal; setParameterValue(ctl[fDrag].id, fDragVal); repaint();
            return true;
        }
        return false;
    }
};

END_NAMESPACE_DISTRHO
#endif // PEDALKIT_HPP
