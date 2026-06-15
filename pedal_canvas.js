/*
 * pedal_canvas.js — in-app HTML5 canvas recreations of the bundled pedal VST
 * UIs (so the app can show & edit the pedal inline, no native window, and use
 * the rendered face as the gear "photo"). Mirrors the C++ pedalkit look:
 * box / chief (Boss-compact) / fuzz bodies, the knob styles, embedded fonts.
 *
 * API (global RBPedalCanvas):
 *   ready()                         -> Promise (fonts loaded)
 *   has(stem)                       -> bool (stem = lowercased .vst3 basename)
 *   attach(canvasEl, stem, opts)    -> renders + (if opts.interactive) wires drag
 *        opts = { values:{paramId:0..1}, onChange:(id,val)=>{}, interactive:false }
 *   dataURL(stem, values)           -> PNG data URL of the face (for <img> photos)
 */
(function () {
  'use strict';
  const API = '/api/plugins/rig_builder';
  const FONTS = { bebas: 'PKBebas', barlow: 'PKBarlow', anton: 'PKAnton', crete: 'PKCrete', graffiti: 'PKGraffiti', ink: 'PKInk' };
  let _fontsP = null;
  function ready() {
    if (_fontsP) return _fontsP;
    _fontsP = Promise.all(Object.keys(FONTS).map(k => {
      try {
        const ff = new FontFace(FONTS[k], `url(${API}/asset/font/${k})`);
        return ff.load().then(f => { document.fonts.add(f); }).catch(() => {});
      } catch (_) { return Promise.resolve(); }
    }));
    return _fontsP;
  }

  // ── helpers ────────────────────────────────────────────────────────────
  const A0 = 135, ASPAN = 270;            // knob sweep (deg)
  const ang = v => (A0 + v * ASPAN) * Math.PI / 180;
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const rgb = (r, g, b, a) => a == null ? `rgb(${r|0},${g|0},${b|0})` : `rgba(${r|0},${g|0},${b|0},${a})`;
  function rr(ctx, x, y, w, h, r) { r = Math.min(r, w/2, h/2); ctx.beginPath();
    ctx.moveTo(x+r,y); ctx.arcTo(x+w,y,x+w,y+h,r); ctx.arcTo(x+w,y+h,x,y+h,r);
    ctx.arcTo(x,y+h,x,y,r); ctx.arcTo(x,y,x+w,y,r); ctx.closePath(); }

  // primitives operate in a {ctx, W, H, s} drawing context (s = scale = W/baseW)
  function screw(d, cx, cy) { const s = d.s, c = d.ctx, r = 6*s;
    const g = c.createRadialGradient(cx-r*0.3, cy-r*0.3, r*0.15, cx, cy, r*1.2);
    g.addColorStop(0, rgb(214,216,220)); g.addColorStop(1, rgb(120,122,128));
    c.beginPath(); c.arc(cx,cy,r,0,7); c.fillStyle=g; c.fill();
    c.strokeStyle=rgb(64,66,72); c.lineWidth=s; c.stroke();
    c.beginPath(); c.moveTo(cx-r*0.55,cy-r*0.3); c.lineTo(cx+r*0.55,cy+r*0.3);
    c.strokeStyle=rgb(70,72,78); c.lineWidth=1.4*s; c.stroke(); }

  function box(d, r, g, b, screws) { const {ctx:c, W, H, s} = d; const m=8*s;
    if (screws === undefined) screws = true;
    c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
    const grad=c.createLinearGradient(0,m,0,H-m);
    grad.addColorStop(0,rgb(clamp(r+22,0,255),clamp(g+22,0,255),clamp(b+22,0,255)));
    grad.addColorStop(1,rgb(clamp(r-18,0,255),clamp(g-18,0,255),clamp(b-18,0,255)));
    rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=grad; c.fill();
    rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=2*s; c.stroke();
    if (screws) { const o=22*s; screw(d,m+o,m+o); screw(d,W-m-o,m+o); screw(d,m+o,H-m-o); screw(d,W-m-o,H-m-o); } }

  function ledDot(d, cx, cy, on, r, g, b) { const c=d.ctx, s=d.s, R=4.6*s;
    if (on){ c.beginPath(); c.arc(cx,cy,R*2,0,7); c.fillStyle=rgb(r,g,b,0.24); c.fill(); }
    c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=on?rgb(r,g,b):rgb(60,30,28); c.fill();
    c.beginPath(); c.arc(cx-R*0.3,cy-R*0.3,R*0.35,0,7); c.fillStyle=rgb(255,255,255,on?0.6:0.16); c.fill(); }

  function footRound(d, cx, cy, R) { const c=d.ctx, s=d.s;
    const g=c.createRadialGradient(cx,cy,R*0.4,cx,cy,R*1.15);
    g.addColorStop(0,rgb(176,178,184)); g.addColorStop(1,rgb(96,98,104));
    c.beginPath(); c.arc(cx,cy,R*1.12,0,7); c.fillStyle=g; c.fill();
    c.strokeStyle=rgb(40,42,46); c.lineWidth=2*s; c.stroke();
    c.beginPath(); c.arc(cx,cy,R*0.78,0,7); c.fillStyle=rgb(150,153,159); c.fill();
    c.beginPath(); c.arc(cx-R*0.25,cy-R*0.3,R*0.34,0,7); c.fillStyle=rgb(255,255,255,0.27); c.fill(); }

  // square toggle switch. Default = red (Eden Bass Boost / Mid Shift, Wah Auto…).
  // dark=true = the black push-button look of the GK 800RB voicing/pad switches:
  // black cap with a grey rocker nub that rides up when engaged + a green pip.
  function switchSquare(d, cx, cy, hs, on, dark) { const c = d.ctx;
    if (dark) {
      rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.fillStyle = on ? rgb(54,58,54) : rgb(26,26,28); c.fill();
      rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.strokeStyle = rgb(96,98,102); c.lineWidth = 1.2; c.stroke();
      const ny = on ? cy - hs*0.34 : cy + hs*0.30;
      rr(c, cx-hs*0.58, ny-hs*0.34, hs*1.16, hs*0.68, 2); c.fillStyle = rgb(150,152,156); c.fill();
      if (on) { c.beginPath(); c.arc(cx, cy+hs*0.55, hs*0.18, 0, 7); c.fillStyle = rgb(70,210,80); c.fill(); }
      return;
    }
    rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.fillStyle = on ? rgb(208,40,36) : rgb(78,22,20); c.fill();
    rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.strokeStyle = rgb(20,12,10); c.lineWidth = 1.5; c.stroke();
    if (on) { c.beginPath(); c.arc(cx, cy, hs*0.34, 0, 7); c.fillStyle = rgb(255,180,170); c.fill(); } }

  // VOX/amp chrome bat-handle toggle (Bright + Standby/Power). Lever points UP
  // when on, DOWN when off; hw = nut radius in px.
  function batToggle(d, cx, cy, hw, on) {
    const c = d.ctx, s = d.s;
    c.beginPath(); c.arc(cx, cy, hw*1.18, 0, 7); c.fillStyle = rgb(22,20,20); c.fill();
    c.strokeStyle = rgb(8,6,6); c.lineWidth = 1.2*s; c.stroke();
    const ng = c.createRadialGradient(cx-hw*0.35, cy-hw*0.35, hw*0.1, cx, cy, hw);
    ng.addColorStop(0, rgb(176,179,186)); ng.addColorStop(1, rgb(78,80,86));
    c.beginPath(); c.arc(cx, cy, hw*0.92, 0, 7); c.fillStyle = ng; c.fill();
    const dir = on ? -1 : 1, len = hw*2.1, lw = hw*0.78;
    c.save(); c.translate(cx, cy);
    rr(c, -lw*0.5, dir<0 ? -len : 0, lw, len, lw*0.5);
    const lg = c.createLinearGradient(-lw*0.5, 0, lw*0.5, 0);
    lg.addColorStop(0, rgb(118,121,128)); lg.addColorStop(0.45, rgb(238,241,246)); lg.addColorStop(1, rgb(118,121,128));
    c.fillStyle = lg; c.fill(); c.strokeStyle = rgb(74,76,82); c.lineWidth = 0.8*s; c.stroke();
    const ty = dir<0 ? -len : len;
    const tg = c.createRadialGradient(-lw*0.25, ty-lw*0.25, lw*0.1, 0, ty, lw*0.85);
    tg.addColorStop(0, rgb(248,250,253)); tg.addColorStop(1, rgb(150,153,160));
    c.beginPath(); c.arc(0, ty, lw*0.8, 0, 7); c.fillStyle = tg; c.fill();
    c.restore();
  }

  // 3-position mini bat toggle (Darkglass Grunt/Attack): lever at top (val 1),
  // middle (0.5) or bottom (0). Click cycles 0→0.5→1→0.
  function switch3(d, cx, cy, val) { const c = d.ctx, w = 13, h = 32;
    rr(c, cx-w/2, cy-h/2, w, h, 4); c.fillStyle = rgb(26,26,28); c.fill();
    rr(c, cx-w/2, cy-h/2, w, h, 4); c.strokeStyle = rgb(8,8,10); c.lineWidth = 1.2; c.stroke();
    const ly = cy + (0.5 - val) * (h - 14);
    const g = c.createLinearGradient(cx-5, ly-6, cx+5, ly+6); g.addColorStop(0, rgb(234,236,240)); g.addColorStop(1, rgb(150,153,160));
    rr(c, cx-5, ly-7, 10, 14, 3); c.fillStyle = g; c.fill();
    rr(c, cx-5, ly-7, 10, 14, 3); c.strokeStyle = rgb(70,72,78); c.lineWidth = 1; c.stroke(); }

  // horizontal slider/fader (Maestro-style): recessed track + draggable cap with
  // a white indicator line. val 0..1 left→right. Wired via spec.sliders in attach().
  function hSlider(d, x0, x1, y, val) { const c = d.ctx, s = d.s;
    rr(c, x0, y-2.5*s, x1-x0, 5*s, 2.5*s); c.fillStyle = rgb(18,18,20); c.fill();
    rr(c, x0, y-2.5*s, x1-x0, 5*s, 2.5*s); c.strokeStyle = rgb(64,66,70); c.lineWidth = 0.8*s; c.stroke();
    const cx = x0 + clamp(val,0,1)*(x1-x0), cw = 13*s, ch = 22*s;
    const g = c.createLinearGradient(cx-cw/2, y-ch/2, cx+cw/2, y+ch/2); g.addColorStop(0, rgb(48,48,52)); g.addColorStop(1, rgb(18,18,20));
    rr(c, cx-cw/2, y-ch/2, cw, ch, 3*s); c.fillStyle = g; c.fill();
    rr(c, cx-cw/2, y-ch/2, cw, ch, 3*s); c.strokeStyle = rgb(8,8,10); c.lineWidth = 1*s; c.stroke();
    c.beginPath(); c.moveTo(cx, y-ch/2+3*s); c.lineTo(cx, y+ch/2-3*s); c.strokeStyle = rgb(228,230,234); c.lineWidth = 1.4*s; c.stroke(); }

  // vertical fader (graphic-EQ band): recessed track + draggable cap with a
  // white indicator line. val 0..1 bottom->top. Wired via spec.faders in attach.
  function vfader(d, cx, y0, y1, val) { const c = d.ctx, s = d.s;
    rr(c, cx-2.5*s, y0, 5*s, y1-y0, 2.5*s); c.fillStyle = rgb(18,18,20); c.fill();
    rr(c, cx-2.5*s, y0, 5*s, y1-y0, 2.5*s); c.strokeStyle = rgb(70,72,76); c.lineWidth = 0.8*s; c.stroke();
    const cy = y1 - clamp(val,0,1)*(y1-y0), cw = 16*s, ch = 11*s;
    const g = c.createLinearGradient(cx-cw/2, cy-ch/2, cx+cw/2, cy+ch/2); g.addColorStop(0, rgb(66,68,72)); g.addColorStop(1, rgb(26,26,28));
    rr(c, cx-cw/2, cy-ch/2, cw, ch, 2*s); c.fillStyle = g; c.fill();
    rr(c, cx-cw/2, cy-ch/2, cw, ch, 2*s); c.strokeStyle = rgb(10,10,12); c.lineWidth = 1*s; c.stroke();
    c.beginPath(); c.moveTo(cx-cw/2+2*s, cy); c.lineTo(cx+cw/2-2*s, cy); c.strokeStyle = rgb(228,230,234); c.lineWidth = 1.3*s; c.stroke(); }

  function setFont(d, family, px) { d.ctx.font = `${px*d.s}px ${family}, sans-serif`; }
  function textC(d, cx, cy, family, px, col, str, align) {
    const c=d.ctx; setFont(d,family,px); c.fillStyle=col; c.textAlign=align||'center'; c.textBaseline='middle';
    c.fillText(str, cx, cy); }
  function textSpaced(d, cx, cy, family, px, col, str, sp) {
    const c=d.ctx; setFont(d,family,px); c.fillStyle=col; c.textBaseline='middle';
    sp*=d.s; const ws=[...str].map(ch=>c.measureText(ch).width+sp); const tot=ws.reduce((a,b)=>a+b,0)-sp;
    let x=cx-tot/2; c.textAlign='left'; for(let i=0;i<str.length;i++){ c.fillText(str[i],x,cy); x+=ws[i]; } }
  // centred text stretched horizontally by sx (wider/less-tall than the font)
  function textWide(d, cx, cy, family, px, col, str, sx) {
    const c=d.ctx; c.save(); c.translate(cx, cy); c.scale(sx, 1);
    setFont(d,family,px); c.fillStyle=col; c.textAlign='center'; c.textBaseline='middle';
    c.fillText(str, 0, 0); c.restore(); }
  function outlineText(d, cx, cy, family, px, fill, out, str, sp) {
    const c=d.ctx; setFont(d,family,px); c.textBaseline='middle'; const o=1.8*d.s;
    sp=(sp||0)*d.s; const ws=[...str].map(ch=>c.measureText(ch).width+sp); const tot=ws.reduce((a,b)=>a+b,0)-sp;
    const draw=(color,ox,oy)=>{ let x=cx-tot/2+ox; c.fillStyle=color; c.textAlign='left';
      for(let i=0;i<str.length;i++){ c.fillText(str[i],x,cy+oy); x+=ws[i]; } };
    for(const dx of [-o,0,o]) for(const dy of [-o,0,o]) if(dx||dy) draw(out,dx,dy);
    draw(fill,0,0); }
  function boxedLabel(d, cx, cy, hw, hh, family, px, line, txt, str) {
    const {ctx:c,W,H,s}=d; rr(c, (cx-hw)*W,(cy-hh)*H, 2*hw*W, 2*hh*H, 3*s);
    c.strokeStyle=line; c.lineWidth=1.6*s; c.stroke(); textC(d, cx*W, cy*H, family, px, txt, str); }

  // ── knob styles ──────────────────────────────────────────────────────────
  function knob(d, cx, cy, R, val, style, capR, capG, capB, tickCol, ptrCol) {
    const c=d.ctx, s=d.s, a=ang(val);
    ptrCol = ptrCol || rgb(214,210,198); tickCol = tickCol || rgb(150,150,150);
    if (style==='bat') { batToggle(d, cx, cy, R*0.42, val >= 0.5); return; }
    if (style==='boss') {
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=rgb(22,22,24); c.fill();
      c.strokeStyle=rgb(58,60,64); c.lineWidth=s;
      for(let i=0;i<36;i++){ const t=i/36*Math.PI*2; c.beginPath();
        c.moveTo(cx+R*0.80*Math.cos(t),cy+R*0.80*Math.sin(t)); c.lineTo(cx+R*0.99*Math.cos(t),cy+R*0.99*Math.sin(t)); c.stroke(); }
      const g=c.createRadialGradient(cx-R*0.3,cy-R*0.4,R*0.1,cx,cy,R*0.85);
      g.addColorStop(0,rgb(52,53,58)); g.addColorStop(1,rgb(22,22,25));
      c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.fillStyle=g; c.fill();
      c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+R*0.92*Math.cos(a),cy+R*0.92*Math.sin(a));
      c.strokeStyle=ptrCol; c.lineWidth=2.6*s; c.stroke();
      c.beginPath(); c.arc(cx+R*0.84*Math.cos(a),cy+R*0.84*Math.sin(a),2*s,0,7); c.fillStyle=ptrCol; c.fill();
      return;
    }
    if (style==='davies') {
      c.beginPath(); c.arc(cx,cy,R*1.30,0,7); c.fillStyle=rgb(14,14,16); c.fill();
      const g=c.createRadialGradient(cx-R*0.4,cy-R*0.5,R*0.1,cx,cy,R*1.15);
      g.addColorStop(0,rgb(50,50,54)); g.addColorStop(1,rgb(18,18,20));
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=g; c.fill();
      c.strokeStyle=rgb(6,6,8); c.lineWidth=1.5*s; c.stroke();
      c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+R*1.2*Math.cos(a),cy+R*1.2*Math.sin(a));
      c.strokeStyle=ptrCol; c.lineWidth=3.4*s; c.stroke(); return;
    }
    if (style==='knurled') {
      const g=c.createRadialGradient(cx-R*0.4,cy-R*0.5,R*0.1,cx,cy,R*1.2);
      g.addColorStop(0,rgb(98,100,106)); g.addColorStop(1,rgb(38,39,43));
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=g; c.fill();
      c.strokeStyle=rgb(150,152,158,0.47); c.lineWidth=0.8*s;
      for(let i=0;i<48;i++){ const t=i/48*Math.PI*2; c.beginPath();
        c.moveTo(cx+R*0.86*Math.cos(t),cy+R*0.86*Math.sin(t)); c.lineTo(cx+R*0.99*Math.cos(t),cy+R*0.99*Math.sin(t)); c.stroke(); }
      c.strokeStyle=rgb(10,10,12); c.lineWidth=1.4*s; c.beginPath(); c.arc(cx,cy,R,0,7); c.stroke();
      c.beginPath(); c.moveTo(cx+R*0.15*Math.cos(a),cy+R*0.15*Math.sin(a)); c.lineTo(cx+R*0.9*Math.cos(a),cy+R*0.9*Math.sin(a));
      c.strokeStyle=rgb(22,22,24); c.lineWidth=2.6*s; c.stroke(); return;
    }
    if (style==='moog') {
      // black fluted skirt
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=rgb(20,20,22); c.fill();
      c.strokeStyle=rgb(50,50,54); c.lineWidth=s;
      for(let i=0;i<40;i++){ const t=i/40*Math.PI*2; c.beginPath();
        c.moveTo(cx+R*0.82*Math.cos(t),cy+R*0.82*Math.sin(t)); c.lineTo(cx+R*0.99*Math.cos(t),cy+R*0.99*Math.sin(t)); c.stroke(); }
      // 0..10 tick scale around the knob
      c.strokeStyle=tickCol; c.lineWidth=1.3*s;
      for(let t=0;t<=10;t++){ const aa=ang(t/10); c.beginPath();
        c.moveTo(cx+R*1.06*Math.cos(aa),cy+R*1.06*Math.sin(aa)); c.lineTo(cx+R*1.20*Math.cos(aa),cy+R*1.20*Math.sin(aa)); c.stroke(); }
      // big brushed-aluminium dome
      const mg=c.createRadialGradient(cx-R*0.3,cy-R*0.35,R*0.1,cx,cy,R*0.80);
      mg.addColorStop(0,rgb(238,240,244)); mg.addColorStop(0.55,rgb(188,191,197)); mg.addColorStop(1,rgb(120,123,130));
      c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.fillStyle=mg; c.fill();
      c.save(); c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.clip();
      c.strokeStyle=rgb(255,255,255,0.10); c.lineWidth=0.8*s;
      for(let i=-6;i<=6;i++){ c.beginPath(); c.moveTo(cx-R,cy+i*R*0.12); c.lineTo(cx+R,cy+i*R*0.12); c.stroke(); }
      c.restore();
      c.strokeStyle=rgb(92,94,100); c.lineWidth=1*s; c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.stroke();
      // engraved dark pointer on the aluminium
      c.beginPath(); c.moveTo(cx+R*0.10*Math.cos(a),cy+R*0.10*Math.sin(a)); c.lineTo(cx+R*0.70*Math.cos(a),cy+R*0.70*Math.sin(a));
      c.strokeStyle=rgb(46,48,52); c.lineWidth=2.8*s; c.stroke(); return;
    }
    if (style==='api') {
      // API 550 stepped knob: 4-point grey star skirt + blue ring + cream centre,
      // top point elongated = indicator (rotates with value).
      for(let i=0;i<4;i++){ const pa=a+i*Math.PI/2, len=(i===0?1.46:1.30)*R;
        c.beginPath();
        c.moveTo(cx+Math.cos(pa)*len, cy+Math.sin(pa)*len);
        c.lineTo(cx+Math.cos(pa+0.30)*R*0.92, cy+Math.sin(pa+0.30)*R*0.92);
        c.lineTo(cx+Math.cos(pa-0.30)*R*0.92, cy+Math.sin(pa-0.30)*R*0.92);
        c.closePath(); c.fillStyle=rgb(170,176,186); c.fill(); }
      c.beginPath(); c.arc(cx,cy,R*0.96,0,7); c.fillStyle=rgb(132,140,156); c.fill();
      c.strokeStyle=rgb(58,62,70); c.lineWidth=1*s; c.stroke();
      c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.fillStyle=rgb(86,116,186); c.fill();
      const ag=c.createRadialGradient(cx-R*0.22,cy-R*0.24,R*0.1,cx,cy,R*0.60);
      ag.addColorStop(0,rgb(240,242,238)); ag.addColorStop(1,rgb(198,202,198));
      c.beginPath(); c.arc(cx,cy,R*0.56,0,7); c.fillStyle=ag; c.fill();
      c.beginPath(); c.arc(cx+Math.cos(a)*R*0.40, cy+Math.sin(a)*R*0.40, R*0.09,0,7); c.fillStyle=rgb(70,74,82); c.fill();
      return;
    }
    if (style==='ampeg') {
      // Ampeg SVT knob: chrome/silver skirt, black top insert, white pointer,
      // dark 0–10 tick fan engraved on the silver panel just outside the skirt.
      c.strokeStyle=tickCol; c.lineWidth=1.2*s;
      for(let t=0;t<=10;t++){ const aa=ang(t/10);
        c.beginPath(); c.moveTo(cx+R*1.12*Math.cos(aa),cy+R*1.12*Math.sin(aa)); c.lineTo(cx+R*1.28*Math.cos(aa),cy+R*1.28*Math.sin(aa)); c.stroke(); }
      const sg=c.createRadialGradient(cx-R*0.3,cy-R*0.4,R*0.15,cx,cy,R*1.1);
      sg.addColorStop(0,rgb(228,230,234)); sg.addColorStop(0.55,rgb(176,178,184)); sg.addColorStop(1,rgb(118,120,126));
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=sg; c.fill();
      c.strokeStyle=rgb(92,94,98); c.lineWidth=1*s; c.beginPath(); c.arc(cx,cy,R,0,7); c.stroke();
      const bg=c.createRadialGradient(cx-R*0.22,cy-R*0.28,R*0.1,cx,cy,R*0.72);
      bg.addColorStop(0,rgb(56,57,62)); bg.addColorStop(1,rgb(18,18,20));
      c.beginPath(); c.arc(cx,cy,R*0.64,0,7); c.fillStyle=bg; c.fill();
      c.strokeStyle=rgb(8,8,10); c.lineWidth=1*s; c.beginPath(); c.arc(cx,cy,R*0.64,0,7); c.stroke();
      c.beginPath(); c.moveTo(cx,cy); c.lineTo(cx+R*0.60*Math.cos(a),cy+R*0.60*Math.sin(a));
      c.strokeStyle=ptrCol; c.lineWidth=2.2*s; c.stroke();
      return;
    }
    if (style==='vox') {
      // VOX AC30 chicken-head (top view): round skirt + a glossy pointer wing
      // with a pointed BEAK (value side) and a rounded TAIL (back side).
      const ca=Math.cos(a), sa=Math.sin(a), nx=-sa, ny=ca;
      // round skirt
      const sg=c.createRadialGradient(cx,cy,R*0.15,cx,cy,R*1.18);
      sg.addColorStop(0,rgb(38,38,42)); sg.addColorStop(0.7,rgb(18,18,21)); sg.addColorStop(1,rgb(7,7,9));
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=sg; c.fill();
      c.strokeStyle=rgb(3,3,4); c.lineWidth=1*s; c.stroke();
      // pointer wing: beak (front) + rounded tail (back)
      const beak=R*1.5, tail=R*0.72, hw=R*0.40, bx=cx+ca*beak, by=cy+sa*beak;
      const t1x=cx-ca*tail+nx*hw, t1y=cy-sa*tail+ny*hw, t2x=cx-ca*tail-nx*hw, t2y=cy-sa*tail-ny*hw;
      const wing=()=>{ c.beginPath(); c.moveTo(bx,by);
        c.quadraticCurveTo(cx+nx*hw*1.15, cy+ny*hw*1.15, t1x, t1y);
        c.quadraticCurveTo(cx-ca*tail*1.55, cy-sa*tail*1.55, t2x, t2y);
        c.quadraticCurveTo(cx-nx*hw*1.15, cy-ny*hw*1.15, bx, by); c.closePath(); };
      wing();
      const wg=c.createLinearGradient(cx-ca*tail, cy-sa*tail, bx, by);
      wg.addColorStop(0,rgb(28,28,32)); wg.addColorStop(0.5,rgb(54,54,59)); wg.addColorStop(1,rgb(15,15,18));
      c.fillStyle=wg; c.fill(); c.strokeStyle=rgb(4,4,5); c.lineWidth=0.8*s; c.stroke();
      // glossy streak along the wing
      c.save(); wing(); c.clip();
      c.beginPath(); c.ellipse(cx+ca*R*0.25-R*0.06, cy+sa*R*0.25-R*0.10, R*0.78, R*0.15, a, 0, 7);
      c.fillStyle='rgba(255,255,255,0.14)'; c.fill();
      c.restore();
      return;
    }
    if (style==='wedge') {
      // Budda triangular "fin" pointer knob (top view): a wide rounded base near
      // the centre that tapers to a pointed tip (the indicator) in the value
      // direction. Glossy black, no skirt — just the wedge.
      const ca=Math.cos(a), sa=Math.sin(a), nx=-sa, ny=ca;
      const tip=R*1.70, back=R*0.55, hw=R*0.72, bb=R*1.05;
      const tx=cx+ca*tip, ty=cy+sa*tip;                                  // pointed tip
      const bLx=cx-ca*back+nx*hw, bLy=cy-sa*back+ny*hw;                  // back corners
      const bRx=cx-ca*back-nx*hw, bRy=cy-sa*back-ny*hw;
      const fin=()=>{ c.beginPath(); c.moveTo(tx,ty); c.lineTo(bLx,bLy);
        c.quadraticCurveTo(cx-ca*bb, cy-sa*bb, bRx,bRy); c.closePath(); };
      // soft drop shadow on the panel
      c.save(); c.translate(2.2*s,2.6*s); fin(); c.fillStyle='rgba(0,0,0,0.28)'; c.fill(); c.restore();
      fin();
      const wg=c.createLinearGradient(cx-ca*back+ny*hw, cy-sa*back-nx*hw, tx, ty);
      wg.addColorStop(0,rgb(64,64,70)); wg.addColorStop(0.45,rgb(30,30,34)); wg.addColorStop(1,rgb(9,9,11));
      c.fillStyle=wg; c.fill(); c.strokeStyle=rgb(4,4,5); c.lineWidth=0.9*s; c.stroke();
      // glossy streak along the upper edge
      c.save(); fin(); c.clip();
      c.beginPath(); c.ellipse(cx+ca*R*0.55, cy+sa*R*0.55, R*1.0, R*0.20, a, 0, 7);
      c.fillStyle='rgba(255,255,255,0.16)'; c.fill(); c.restore();
      return;
    }
    if (style==='fender') {
      // Fender black skirted amp knob (top view): fluted black skirt, glossy
      // domed cap, single white pointer line. The 1-10 numerals are printed on
      // the faceplate around the knob by the draw() routine, not on the cap.
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=rgb(14,14,16); c.fill();
      c.strokeStyle='rgba(120,122,128,0.30)'; c.lineWidth=0.7*s;
      for(let i=0;i<30;i++){ const t=i/30*Math.PI*2; c.beginPath();
        c.moveTo(cx+R*0.86*Math.cos(t),cy+R*0.86*Math.sin(t)); c.lineTo(cx+R*0.99*Math.cos(t),cy+R*0.99*Math.sin(t)); c.stroke(); }
      c.strokeStyle=rgb(4,4,5); c.lineWidth=1.2*s; c.beginPath(); c.arc(cx,cy,R,0,7); c.stroke();
      const cg=c.createRadialGradient(cx-R*0.34,cy-R*0.40,R*0.10,cx,cy,R*0.82);
      cg.addColorStop(0,rgb(72,72,78)); cg.addColorStop(0.55,rgb(30,30,34)); cg.addColorStop(1,rgb(10,10,12));
      c.beginPath(); c.arc(cx,cy,R*0.78,0,7); c.fillStyle=cg; c.fill();
      c.strokeStyle=rgb(2,2,3); c.lineWidth=0.8*s; c.stroke();
      c.beginPath(); c.moveTo(cx+R*0.10*Math.cos(a),cy+R*0.10*Math.sin(a));
      c.lineTo(cx+R*0.96*Math.cos(a),cy+R*0.96*Math.sin(a));
      c.lineCap='round'; c.strokeStyle=rgb(238,238,234); c.lineWidth=2.6*s; c.stroke(); c.lineCap='butt';
      c.beginPath(); c.arc(cx-R*0.22,cy-R*0.26,R*0.10,0,7); c.fillStyle='rgba(255,255,255,0.18)'; c.fill();
      return;
    }
    if (style==='cream') {
      // Fender ivory/cream skirted amp knob (top view): fluted cream skirt, domed
      // cream cap, dark molded pointer line. Numerals printed on the faceplate.
      const sg=c.createRadialGradient(cx-R*0.32,cy-R*0.38,R*0.1,cx,cy,R*1.16);
      sg.addColorStop(0,rgb(240,233,209)); sg.addColorStop(0.7,rgb(222,213,184)); sg.addColorStop(1,rgb(170,160,131));
      c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=sg; c.fill();
      c.strokeStyle='rgba(120,110,82,0.45)'; c.lineWidth=0.7*s;
      for(let i=0;i<46;i++){ const t=i/46*Math.PI*2; c.beginPath();
        c.moveTo(cx+R*0.88*Math.cos(t),cy+R*0.88*Math.sin(t)); c.lineTo(cx+R*0.995*Math.cos(t),cy+R*0.995*Math.sin(t)); c.stroke(); }
      c.strokeStyle=rgb(120,112,84); c.lineWidth=1*s; c.beginPath(); c.arc(cx,cy,R,0,7); c.stroke();
      const cg=c.createRadialGradient(cx-R*0.3,cy-R*0.38,R*0.1,cx,cy,R*0.82);
      cg.addColorStop(0,rgb(248,242,222)); cg.addColorStop(0.6,rgb(231,222,194)); cg.addColorStop(1,rgb(196,186,156));
      c.beginPath(); c.arc(cx,cy,R*0.74,0,7); c.fillStyle=cg; c.fill();
      c.strokeStyle='rgba(150,140,108,0.6)'; c.lineWidth=0.8*s; c.stroke();
      c.beginPath(); c.moveTo(cx+R*0.08*Math.cos(a),cy+R*0.08*Math.sin(a));
      c.lineTo(cx+R*0.92*Math.cos(a),cy+R*0.92*Math.sin(a));
      c.lineCap='round'; c.strokeStyle=rgb(58,50,36); c.lineWidth=2.4*s; c.stroke(); c.lineCap='butt';
      c.beginPath(); c.arc(cx-R*0.24,cy-R*0.27,R*0.12,0,7); c.fillStyle='rgba(255,253,246,0.42)'; c.fill();
      return;
    }
    // pointer + tick fan (default)
    c.beginPath(); c.arc(cx,cy,R*1.16,0,7); c.fillStyle=rgb(16,16,18); c.fill();
    c.strokeStyle=tickCol; c.lineWidth=1.4*s;
    for(let t=0;t<=10;t++){ const aa=ang(t/10); c.beginPath();
      c.moveTo(cx+R*1.22*Math.cos(aa),cy+R*1.22*Math.sin(aa)); c.lineTo(cx+R*1.36*Math.cos(aa),cy+R*1.36*Math.sin(aa)); c.stroke(); }
    const g=c.createRadialGradient(cx-R*0.4,cy-R*0.5,R*0.1,cx,cy,R*1.3);
    g.addColorStop(0,rgb(clamp(capR+45,0,255),clamp(capG+45,0,255),clamp(capB+45,0,255)));
    g.addColorStop(1,rgb(capR*0.45,capG*0.45,capB*0.45));
    c.beginPath(); c.arc(cx,cy,R,0,7); c.fillStyle=g; c.fill();
    c.strokeStyle=rgb(8,8,10); c.lineWidth=1.5*s; c.stroke();
    c.beginPath(); c.moveTo(cx+R*0.18*Math.cos(a),cy+R*0.18*Math.sin(a)); c.lineTo(cx+R*0.86*Math.cos(a),cy+R*0.86*Math.sin(a));
    c.strokeStyle=ptrCol; c.lineWidth=3*s; c.stroke();
  }

  // chief (Boss-compact) body: coloured body, black knob plate, treadle w/ name
  function chiefBody(d, r, g, b, plate) { const {ctx:c,W,H,s}=d, m=7*s, cl=v=>clamp(v,0,255);
    const lum=0.299*r+0.587*g+0.114*b, pc=plate||[20,20,22];
    c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
    const grad=c.createLinearGradient(0,m,0,H-m); grad.addColorStop(0,rgb(cl(r+18),cl(g+18),cl(b+18))); grad.addColorStop(1,rgb(cl(r-14),cl(g-14),cl(b-14)));
    rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=grad; c.fill();
    rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=2*s; c.stroke();
    // black knob plate
    rr(c,m+11*s,H*0.10,W-2*m-22*s,H*0.235,6*s); c.fillStyle=rgb(pc[0],pc[1],pc[2]); c.fill();
    rr(c,m+11*s,H*0.10,W-2*m-22*s,H*0.235,6*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=1.2*s; c.stroke();
    ledDot(d, W*0.5, H*0.072, true, 224,70,58);
    // treadle (body colour) + black step pad (lower half)
    const tx=m+4*s, tw=W-2*m-8*s, tyTop=H*0.42, tBot=H-m-6*s;
    const tg=c.createLinearGradient(0,tyTop,0,tBot); tg.addColorStop(0,rgb(cl(r-2),cl(g-2),cl(b-2))); tg.addColorStop(1,rgb(cl(r-16),cl(g-16),cl(b-16)));
    rr(c,tx,tyTop,tw,tBot-tyTop,12*s); c.fillStyle=tg; c.fill();
    rr(c,tx,tyTop,tw,12*s,12*s); c.fillStyle='rgba(255,255,255,0.086)'; c.fill();
    rr(c,tx,tyTop,tw,tBot-tyTop,12*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=1.6*s; c.stroke();
    const padT=tyTop+(tBot-tyTop)*0.50, padBot=tBot-9*s; rr(c,tx+12*s,padT,tw-24*s,padBot-padT,9*s); c.fillStyle=rgb(20,20,22); c.fill();
    // brand badge on the black step pad (parody of Boss's logo): same near-black
    // as the pad, with a blacker outline so it reads as engraved. Up near the
    // top of the pad, big, all caps.
    chiefBadge(d, padT, padBot, lum);
  }
  // Wide engraved 'CHIEF' badge across the black step pad (parody Boss logo):
  // pad-colour fill + black outline; much wider than tall via big letter spacing.
  function chiefBadge(d, padT, padBot) { const W = d.W;   // engraved CHIEF on the black step pad (same on every chief pedal)
    outlineText(d, W*0.5, padT+(padBot-padT)*0.30, FONTS.bebas, 40, rgb(20,20,22), rgb(0,0,0), 'CHIEF', 13);
  }

  // ── Ibanez Tonelok-style body (parallels chiefBody): silver enclosure, jack
  //    nubs at the seam, a big footswitch treadle with diagonal screw slots +
  //    an embossed wordmark. Parody, brand-free apart from the chosen name. ──
  function ibanezBody(d) { const {ctx:c,W,H,s}=d, m=7*s;
    c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
    const grad=c.createLinearGradient(0,m,0,H-m);
    grad.addColorStop(0,rgb(212,214,218)); grad.addColorStop(.45,rgb(186,189,194)); grad.addColorStop(1,rgb(150,153,159));
    rr(c,m,m,W-2*m,H-2*m,13*s); c.fillStyle=grad; c.fill();
    rr(c,m,m,W-2*m,H-2*m,13*s); c.strokeStyle='rgba(0,0,0,0.42)'; c.lineWidth=2*s; c.stroke();
    // jack nubs at the panel/treadle seam
    const sy=H*0.395, nh=24*s;
    rr(c,1*s,sy,m+9*s,nh,4*s); c.fillStyle=rgb(150,153,159); c.fill(); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=1.3*s; c.stroke();
    rr(c,W-m-10*s,sy,m+10*s,nh,4*s); c.fillStyle=rgb(150,153,159); c.fill(); c.stroke();
    // big footswitch treadle (lower part)
    const tx=m+5*s, tyTop=H*0.455, tw=W-2*m-10*s, tBot=H-m-6*s;
    const tg=c.createLinearGradient(0,tyTop,0,tBot);
    tg.addColorStop(0,rgb(200,202,207)); tg.addColorStop(1,rgb(160,163,169));
    rr(c,tx,tyTop,tw,tBot-tyTop,11*s); c.fillStyle=tg; c.fill();
    rr(c,tx,tyTop,tw,12*s,11*s); c.fillStyle='rgba(255,255,255,0.10)'; c.fill();
    rr(c,tx,tyTop,tw,tBot-tyTop,11*s); c.strokeStyle='rgba(0,0,0,0.32)'; c.lineWidth=1.6*s; c.stroke();
    const sd=(x,y)=>{ c.save(); c.translate(x,y); c.rotate(0.6);
      rr(c,-5*s,-1.5*s,10*s,3*s,1.5*s); c.fillStyle='rgba(0,0,0,0.20)'; c.fill(); c.restore(); };
    sd(tx+20*s,tyTop+22*s); sd(tx+tw-20*s,tyTop+22*s); sd(tx+20*s,tBot-20*s); sd(tx+tw-20*s,tBot-20*s);
    outlineText(d, W*0.5, tyTop+(tBot-tyTop)*0.66, FONTS.crete, 42, rgb(168,171,176), rgb(118,121,127), 'Ibañez', 1);
  }
  // Builder for Ibanez Tonelok pedals (parallels chiefSpec). knobIds:[{id,cx,lbl,lblPx?}];
  // model = salmon code badge ('LF6'), fxname = effect tag ('LO FI'). Labels use
  // the RS knob names so the face matches what the knob actually does.
  function ibanezSpec(w,h,knobIds,model,fxname){ return { w,h,
    knobs: knobIds.map(k=>({id:k.id,cx:k.cx,cy:.135,r:k.r||.060,style:'pointer',cap:[224,226,230]})),
    tick:rgb(74,76,82), ptr:rgb(40,42,48),
    draw(d){ const {ctx:c,W,H,s}=d; ibanezBody(d); const dk=rgb(46,48,54);
      knobIds.forEach(k=> textSpaced(d,k.cx*d.W,.235*d.H,F.barlow,k.lblPx||8,dk,k.lbl,0.2));
      // decorative MODE slider on the panel
      const mx=W*.26, my=H*.315, mw=W*.20, mh=8*s;
      rr(c,mx,my-mh/2,mw,mh,3*s); c.fillStyle=rgb(70,72,78); c.fill();
      rr(c,mx+mw*0.34,my-mh*1.1,mw*0.22,mh*2.2,2*s); c.fillStyle=rgb(228,230,234); c.fill();
      rr(c,mx+mw*0.34,my-mh*1.1,mw*0.22,mh*2.2,2*s); c.strokeStyle=rgb(90,92,98); c.lineWidth=0.8*s; c.stroke();
      textSpaced(d,mx+mw/2,my+13*s,F.barlow,6.5,dk,'MODE',0.4);
      // red status LED
      ledDot(d, W*.66, H*.315, true, 224,60,52);
      // OUT / IN jack legends
      textSpaced(d,.115*W,.378*H,F.barlow,7,dk,'OUT',0.3);
      textSpaced(d,.885*W,.378*H,F.barlow,7,dk,'IN',0.3);
      // salmon model badge + effect tag at the treadle top
      const bx=W*.235, by=H*.495, bw=W*.235, bh=H*.072;
      rr(c,bx,by,bw,bh,5*s); c.fillStyle=rgb(208,140,122); c.fill();
      rr(c,bx,by,bw,bh,5*s); c.strokeStyle=rgb(138,80,66); c.lineWidth=1.2*s; c.stroke();
      textC(d,bx+bw/2,by+bh*0.52,F.anton,26,rgb(26,26,28),model);
      textC(d,bx+bw+W*.12,by+bh*0.52,F.anton,20,dk,fxname); } };
  }

  // ── Foog (moogerfooger)-style body: dark granite face with wood side panels.
  //    Parallels chiefBody/ibanezBody. Parody, brand-free. ──
  function foogBody(d) { const {ctx:c,W,H,s}=d, woodW=W*.075, ty=W*.012, th=H-W*.024;
    c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
    [0, W-woodW].forEach(x=>{ const wg=c.createLinearGradient(x,0,x+woodW,0);
      wg.addColorStop(0,rgb(118,70,34)); wg.addColorStop(.5,rgb(152,98,54)); wg.addColorStop(1,rgb(108,62,30));
      rr(c,x,ty,woodW,th,4*s); c.fillStyle=wg; c.fill();
      rr(c,x,ty,woodW,th,4*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=1.4*s; c.stroke(); });
    const fx=woodW+4*s, fw=W-2*(woodW+4*s);
    const fg=c.createLinearGradient(0,0,0,H); fg.addColorStop(0,rgb(66,66,70)); fg.addColorStop(1,rgb(46,46,50));
    rr(c,fx,ty,fw,th,4*s); c.fillStyle=fg; c.fill();
    c.save(); rr(c,fx,ty,fw,th,4*s); c.clip();
    c.fillStyle=rgb(255,255,255,0.035);
    for(let i=0;i<320;i++){ c.fillRect(fx+((i*71)%Math.floor(fw)), ty+((i*131)%Math.floor(th)),1.3*s,1.3*s); }
    c.fillStyle=rgb(0,0,0,0.10);
    for(let i=0;i<320;i++){ c.fillRect(fx+((i*97)%Math.floor(fw)), ty+((i*53)%Math.floor(th)),1.3*s,1.3*s); }
    c.restore();
    rr(c,fx,ty,fw,th,4*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=1.5*s; c.stroke();
  }
  // Builder for Foog (moogerfooger) pedals (parallels chiefSpec/ibanezSpec).
  // knobIds:[{id,cx,cy,r?,lbl,lblPx?}] (free 2D placement); model = e.g. 'FM107'.
  // Labels (RS knob names) sit ABOVE each knob, moogerfooger-style.
  function foogSpec(w,h,knobIds,model){ return { w,h,
    knobs: knobIds.map(k=>({id:k.id,cx:k.cx,cy:k.cy,r:k.r||.10,style:'moog'})),
    tick:rgb(150,152,158), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; foogBody(d); const wt=rgb(226,228,232);
      textC(d,.40*W,.065*H,F.crete,18,wt,'foogermooger');
      textC(d,.74*W,.065*H,F.crete,16,wt,model);
      // white control-group outline box (moogerfooger signature)
      const rr0=Math.max.apply(null,knobIds.map(k=>k.r||.10));
      const xs=knobIds.map(k=>k.cx), ys=knobIds.map(k=>k.cy);
      const x0=(Math.min.apply(null,xs)-rr0-.045)*W, x1=(Math.max.apply(null,xs)+rr0+.045)*W;
      const y0=(Math.min.apply(null,ys)-rr0-.085)*H, y1=(Math.max.apply(null,ys)+rr0+.04)*H;
      rr(c,x0,y0,x1-x0,y1-y0,9*s); c.strokeStyle=wt; c.lineWidth=1.6*s; c.stroke();
      // knob labels just above each knob
      knobIds.forEach(k=> textSpaced(d,k.cx*d.W,(k.cy-(k.r||.10)-0.022)*d.H,F.barlow,k.lblPx||8.5,wt,k.lbl,0.3));
      textC(d,.78*W,.815*H,F.crete,22,wt,'foog');
      footRound(d,W*.46,H*.865,18*s); } };
  }
  // n1/n2 = two-word model name (n1 left, n2 right); code = parody model number
  // (e.g. 'CB-3'), a bit smaller, bottom-RIGHT corner. dy shifts everything down
  // (the EQ treadle sits lower, so it passes a positive dy).
  function chiefName(d, n1, n2, code, dy, codeDy, ink) { const {W,H}=d; dy = dy || 0; codeDy = codeDy || 0; const dk = ink || rgb(16,16,20);
    if (n2){ const s2 = n2.length > 7 ? 32 : 44, sc = s2 - 12;
             textC(d, 0.29*W, (0.50+dy)*H, FONTS.crete, 48, dk, n1);
             textC(d, 0.62*W, (0.58+dy)*H, FONTS.crete, s2, dk, n2);
             if (code) textC(d, 0.76*W, (0.665+dy+codeDy)*H, FONTS.barlow, sc, dk, code); }
    else   { textC(d, 0.46*W, (0.56+dy)*H, FONTS.crete, 44, dk, n1);
             if (code) textC(d, 0.78*W, (0.66+dy+codeDy)*H, FONTS.barlow, 30, dk, code); } }

  // ── pedal specs ───────────────────────────────────────────────────────────
  // each: {w,h, knobs:[{id,cx,cy,r,style,cap:[r,g,b]}], draw(d,vals)}
  const F = FONTS;
  const P = {};
  function defKnobs(arr){ return arr; }

  // ── Freddy Krueger 800BR — faithful Gallien-Krueger 800RB front panel ───────
  // Wide rack face traced from the real amp: a grey brushed control PLATE inset
  // into the black chassis, section brackets + vertical dividers, black knurled
  // knobs with 0–10 tick fans + a frequency scale under each EQ knob, the black
  // square voicing/pad/boost/biamp switches, FK / FREDDY-KRUEGER wordmark with
  // the trailing rule + 800BR on the black chassis below the plate.
  // Logical param ids (Buffer Size/Sample Rate already filtered out):
  //  0 Volume 1 Treble 2 Hi-Mid 3 Lo-Mid 4 Bass 5 Boost(level) 6 Crossover
  //  7 100W 8 300W | 9 -10dB 10 LoCut 11 Contour 12 Hi Boost 13 Boost(footsw) 14 Bi-Amp
  P.freddykrueger800br = { w:840, h:256,
    knobs:[
      {id:0,cx:.175,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.385,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.445,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:3,cx:.505,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:4,cx:.565,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:5,cx:.640,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:6,cx:.760,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:7,cx:.862,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]},
      {id:8,cx:.918,cy:.40,r:.0175,style:'pointer',cap:[26,26,28]}],
    switches:[
      {id:9, cx:.108,cy:.40,hs:.0105,dark:true},
      {id:10,cx:.230,cy:.40,hs:.0110,dark:true},
      {id:11,cx:.262,cy:.40,hs:.0110,dark:true},
      {id:12,cx:.294,cy:.40,hs:.0110,dark:true},
      {id:13,cx:.700,cy:.40,hs:.0120,dark:true},
      {id:14,cx:.812,cy:.40,hs:.0110,dark:true}],
    tick:rgb(74,76,80), ptr:rgb(242,243,246),
    draw(d, vals){ vals = vals || {}; const {ctx:c,W,H}=d;
      const ink=rgb(22,22,24), dim=rgb(46,47,50), wht=rgb(236,238,242), plate=rgb(150,152,156);
      box(d, 30,31,34, true);
      c.strokeStyle=rgb(64,66,70); c.lineWidth=1.2; c.beginPath(); c.moveTo(.02*W,.10*H); c.lineTo(.98*W,.10*H); c.stroke();
      // grey control plate
      const PL=.029*W, PT=.175*H, PW=.918*W, PH=.436*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(162,164,168)); pg.addColorStop(1,rgb(138,140,144));
      rr(c,PL,PT,PW,PH,7); c.fillStyle=pg; c.fill();
      rr(c,PL,PT,PW,PH,7); c.strokeStyle=rgb(40,41,44); c.lineWidth=1.5; c.stroke();
      // ── section helpers ──
      const bracket=(cx,half,label)=>{ const y=.225*H; c.strokeStyle=dim; c.lineWidth=1.1;
        c.beginPath(); c.moveTo((cx-half)*W,y); c.lineTo((cx+half)*W,y);
        c.moveTo((cx-half)*W,y); c.lineTo((cx-half)*W,y+5); c.moveTo((cx+half)*W,y); c.lineTo((cx+half)*W,y+5); c.stroke();
        setFont(d,F.barlow,9); const tw=c.measureText(label).width;
        c.fillStyle=plate; c.fillRect(cx*W-tw/2-4,y-7,tw+8,13); textC(d,cx*W,y,F.barlow,9,ink,label); };
      const divider=(x)=>{ c.strokeStyle=rgb(118,120,124); c.lineWidth=1; c.beginPath(); c.moveTo(x*W,PT+8); c.lineTo(x*W,PT+PH-8); c.stroke(); };
      const klabel=(cx,t)=>textC(d,cx*W,.285*H,F.barlow,8.5,ink,t);
      const scale=(cx,t)=>textC(d,cx*W,.535*H,F.barlow,7.5,dim,t);
      const jack=(cx)=>{ c.beginPath(); c.arc(cx*W,.40*H,8.5,0,7); c.fillStyle=rgb(16,16,18); c.fill();
        c.strokeStyle=rgb(96,98,102); c.lineWidth=1.5; c.stroke(); c.beginPath(); c.arc(cx*W,.40*H,3.6,0,7); c.fillStyle=rgb(36,36,40); c.fill(); };
      // INPUT
      bracket(.085,.040,'INPUT'); jack(.058); textC(d,.108*W,.50*H,F.barlow,6.6,ink,'-10dB'); divider(.135);
      // VOLUME
      bracket(.175,.034,'VOLUME'); klabel(.175,'VOLUME'); scale(.175,'0      10'); divider(.205);
      // VOICING FILTERS
      bracket(.262,.052,'VOICING FILTERS');
      [[.230,'LO','CUT'],[.262,'MID','CONT'],[.294,'HI','BOOST']].forEach(s=>{
        textC(d,s[0]*W,.50*H,F.barlow,6.2,ink,s[1]); textC(d,s[0]*W,.565*H,F.barlow,6.2,ink,s[2]); });
      divider(.325);
      // ACTIVE EQUALIZATION
      bracket(.475,.110,'ACTIVE EQUALIZATION');
      [[.385,'TREBLE','4.0kHz'],[.445,'HI-MID','1.0kHz'],[.505,'LO-MID','250Hz'],[.565,'BASS','60Hz']]
        .forEach(e=>{ klabel(e[0],e[1]); scale(e[0],'– '+e[2]+' +'); });
      divider(.605);
      // BOOST  (LEVEL knob + footswitch button + on-LED)
      bracket(.660,.044,'BOOST'); klabel(.640,'LEVEL');
      textC(d,.700*W,.555*H,F.barlow,6.0,ink,'FOOTSWITCH'); ledDot(d,.700*W,.27*H,(vals[13]>.5),70,210,80);
      divider(.722);
      // CROSSOVER
      bracket(.778,.052,'CROSSOVER'); klabel(.760,'FREQUENCY'); scale(.760,'100Hz   1.0K');
      textC(d,.812*W,.50*H,F.barlow,6.0,ink,'FULL'); textC(d,.812*W,.565*H,F.barlow,6.0,ink,'BIAMP');
      divider(.832);
      // MASTER VOLUMES
      bracket(.890,.052,'MASTER VOLUMES'); klabel(.862,'100W AMP'); klabel(.918,'300W AMP');
      ledDot(d,.936*W,.555*H,true,70,210,80);   // status LED, clear below/right of the 300W knob
      // black power rocker on the chassis, right of the plate
      const px=.968*W, py=.40*H;
      rr(c,px-11,py-23,22,46,3); c.fillStyle=rgb(16,16,18); c.fill();
      rr(c,px-11,py-23,22,46,3); c.strokeStyle=rgb(80,82,86); c.lineWidth=1.2; c.stroke();
      rr(c,px-7,py-21,14,21,2); c.fillStyle=rgb(150,30,28); c.fill();
      textC(d,px,.585*H,F.barlow,6.6,rgb(150,152,156),'O');
      // ── branding on the black chassis below the plate ──
      const by=.79*H;
      rr(c,.030*W,by-12,30,21,3); c.strokeStyle=wht; c.lineWidth=2; c.stroke(); textC(d,.030*W+15,by-1,F.bebas,15,wht,'FK');
      textC(d,.075*W,by-1,F.bebas,19,wht,'FREDDY-KRUEGER','left');
      setFont(d,F.bebas,19); const fkw=c.measureText('FREDDY-KRUEGER').width;
      c.strokeStyle=wht; c.lineWidth=2; c.beginPath(); c.moveTo(.075*W+fkw+10,by+6); c.lineTo(.66*W,by+6); c.stroke();
      textC(d,.945*W,by-1,F.bebas,15,wht,'800BR','right');
      textC(d,.945*W,by-14,F.barlow,6.0,rgb(150,152,156),'320 + 100W BIAMP BASS SYSTEM','right'); } };

  // ── Sampleg SBT-CL — faithful Ampeg SVT-CL all-tube head panel (parody) ─────
  // Brushed-aluminium SILVER control panel (the SVT face) with an engraved
  // recessed border, a recessed input box (diamond logo + Normal/-15dB jacks +
  // pad), chrome black-top knobs (style 'ampeg') with 0–10 tick fans, the Ultra
  // Lo/Hi push switches, an engraved SAMPLEG · SBT-CL area + black standby/power
  // rockers, and a black grille below carrying the Sampleg wordmark + diamond.
  // Logical ids: 0 Gain 1 Bass 2 Midrange 3 Frequency(5-pos) 4 Treble 5 Master |
  //   6 -15dB 7 Ultra Lo 8 Ultra Hi
  P.samplegsbtcl = { w:900, h:256,
    knobs:[
      {id:0,cx:.205,cy:.38,r:.022,style:'ampeg'},
      {id:1,cx:.365,cy:.38,r:.022,style:'ampeg'},
      {id:2,cx:.439,cy:.38,r:.022,style:'ampeg'},
      {id:3,cx:.513,cy:.38,r:.022,style:'ampeg',select:5},
      {id:4,cx:.587,cy:.38,r:.022,style:'ampeg'},
      {id:5,cx:.661,cy:.38,r:.022,style:'ampeg'}],
    switches:[
      {id:6,cx:.137,cy:.38,hs:.0095,dark:true},
      {id:7,cx:.270,cy:.38,hs:.0100,dark:true},
      {id:8,cx:.302,cy:.38,hs:.0100,dark:true}],
    tick:rgb(74,76,82), ptr:rgb(245,246,249),
    draw(d, vals){ vals=vals||{}; const {ctx:c,W,H}=d;
      const ink=rgb(30,31,35), dim=rgb(92,94,100);
      box(d, 26,27,30, true);                              // black tolex shell
      // brushed-aluminium control panel
      const PL=.03*W, PT=.09*H, PW=.94*W, PH=.55*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(202,204,208)); pg.addColorStop(.5,rgb(180,182,188)); pg.addColorStop(1,rgb(158,160,166));
      rr(c,PL,PT,PW,PH,4); c.fillStyle=pg; c.fill();
      c.save(); rr(c,PL,PT,PW,PH,4); c.clip();              // faint vertical brush
      for(let x=PL;x<PL+PW;x+=2){ c.strokeStyle=(((x|0)%4)?'rgba(255,255,255,0.05)':'rgba(0,0,0,0.045)'); c.lineWidth=1; c.beginPath(); c.moveTo(x,PT); c.lineTo(x,PT+PH); c.stroke(); }
      c.restore();
      rr(c,PL,PT,PW,PH,4); c.strokeStyle=rgb(118,120,126); c.lineWidth=1.5; c.stroke();
      const engrave=(x,y,w,h)=>{ rr(c,x,y+1.5,w,h,5); c.strokeStyle='rgba(255,255,255,0.5)'; c.lineWidth=1; c.stroke();
        rr(c,x,y,w,h,5); c.strokeStyle=rgb(112,114,120); c.lineWidth=1.2; c.stroke(); };
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // ── left recessed input box ──
      const ibx=.045*W, iby=.17*H, ibw=.118*W, ibh=.40*H;
      rr(c,ibx,iby,ibw,ibh,4); c.fillStyle=rgb(150,152,158); c.fill();
      rr(c,ibx,iby,ibw,ibh,4); c.strokeStyle=rgb(106,108,114); c.lineWidth=1.2; c.stroke();
      const diamond=(x,y,r,letter,lsz)=>{ c.save(); c.translate(x,y); c.rotate(Math.PI/4); rr(c,-r,-r,2*r,2*r,2); c.fillStyle=rgb(40,52,96); c.fill(); c.strokeStyle=rgb(208,212,220); c.lineWidth=1.3; c.stroke(); c.restore(); textC(d,x,y,F.bebas,lsz,rgb(222,226,234),letter); };
      diamond(ibx+ibw*0.30, iby+ibh*0.28, 9, 'S', 11);
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,7,0,7); c.fillStyle=rgb(14,14,16); c.fill(); c.strokeStyle=rgb(88,90,96); c.lineWidth=1.3; c.stroke(); c.beginPath(); c.arc(x,y,3,0,7); c.fillStyle=rgb(34,34,38); c.fill(); };
      jack(ibx+ibw*0.30, iby+ibh*0.70); jack(ibx+ibw*0.62, iby+ibh*0.70);
      textC(d,ibx+ibw*0.30,iby+ibh-6,F.barlow,8,ink,'0'); textC(d,ibx+ibw*0.62,iby+ibh-6,F.barlow,8,ink,'-15');
      lab(.137,.63,8.5,'-15dB');
      // ── engraved frames around the knob bank and the right plate ──
      engrave(.168*W, PT+8, .527*W, PH-16);
      engrave(.705*W, PT+8, PL+PW-8 - .705*W, PH-16);
      // ── knob labels (engraved) + the 1–5 frequency selector marks ──
      [[.205,'GAIN'],[.365,'BASS'],[.439,'MIDRANGE'],[.513,'FREQUENCY'],[.587,'TREBLE'],[.661,'MASTER']].forEach(k=>lab(k[0],.555,10.5,k[1]));
      for(let i=0;i<5;i++) textC(d,(.513+(i-2)*0.014)*W,.19*H,F.barlow,8,dim,String(i+1));
      // ── ultra push switches (single ULTRA over both, LO / HI under each) ──
      textC(d,.286*W,.495*H,F.barlow,8,ink,'ULTRA');
      textC(d,.270*W,.565*H,F.barlow,8,ink,'LO'); textC(d,.302*W,.565*H,F.barlow,8,ink,'HI');
      // ── right engraved SAMPLEG · SBT-CL + standby/power rockers ──
      textC(d,.772*W,.29*H,F.bebas,22,ink,'SAMPLEG'); textC(d,.772*W,.44*H,F.barlow,12.5,dim,'SBT-CL');
      const rock=(cx,red,lbl)=>{ const x=cx*W,y=.38*H; rr(c,x-10,y-19,20,38,3); c.fillStyle=rgb(20,20,22); c.fill();
        rr(c,x-10,y-19,20,38,3); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2; c.stroke();
        rr(c,x-7,y-17,14,17,2); c.fillStyle=red?rgb(176,32,30):rgb(54,56,60); c.fill(); textC(d,x,.525*H,F.barlow,8,ink,lbl); };
      rock(.872,false,'STANDBY'); rock(.915,true,'POWER');
      // ── black grille below: Sampleg diamond + wordmark, Heritage script ──
      const gy=.83*H;
      diamond(.052*W, gy, 12, 'S', 14);
      textC(d,.097*W,gy,F.bebas,28,rgb(232,234,238),'Sampleg','left');
      textC(d,.955*W,gy,F.crete,17,rgb(150,152,158),'Heritage','right'); } };

  // ── Sampleg V-4B — faithful Ampeg V-4B reissue layout (parody branding):
  //   black tolex head, brushed-silver control strip. Left: diamond logo + 0dB
  //   / -15dB input jacks. Centre: an "EQUALIZATION" framed box with GAIN BASS
  //   MIDRANGE TREBLE MASTER. Right: a "MODEL V-4B" box with STANDBY/POWER
  //   toggles. The V-4B reissue front panel has NO Ultra/Freq controls, so the
  //   canvas shows only the 5 knobs (Frequency id3 / Ultra id7,8 keep defaults).
  //   Logical ids shown: 0 Gain 1 Bass 2 Midrange 4 Treble 5 Master.
  P.samplegv4b = { w:900, h:256,
    knobs:[
      {id:0,cx:.345,cy:.41,r:.030,style:'ampeg'},
      {id:1,cx:.435,cy:.41,r:.030,style:'ampeg'},
      {id:2,cx:.525,cy:.41,r:.030,style:'ampeg'},
      {id:4,cx:.615,cy:.41,r:.030,style:'ampeg'},
      {id:5,cx:.705,cy:.41,r:.030,style:'ampeg'}],
    switches:[],
    tick:rgb(74,76,82), ptr:rgb(245,246,249),
    draw(d, vals){ vals=vals||{}; const {ctx:c,W,H}=d;
      const ink=rgb(32,34,38), dim=rgb(96,98,104);
      box(d, 24,25,28, true);                                  // black tolex shell
      // ── brushed-aluminium control strip ──
      const PL=.025*W, PT=.20*H, PW=.95*W, PH=.42*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(216,218,222)); pg.addColorStop(.5,rgb(190,192,198)); pg.addColorStop(1,rgb(166,168,174));
      rr(c,PL,PT,PW,PH,4); c.fillStyle=pg; c.fill();
      c.save(); rr(c,PL,PT,PW,PH,4); c.clip();
      for(let x=PL;x<PL+PW;x+=2){ c.strokeStyle=(((x|0)%4)?'rgba(255,255,255,0.06)':'rgba(0,0,0,0.04)'); c.lineWidth=1; c.beginPath(); c.moveTo(x,PT); c.lineTo(x,PT+PH); c.stroke(); }
      c.restore();
      rr(c,PL,PT,PW,PH,4); c.strokeStyle=rgb(120,122,128); c.lineWidth=1.5; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      const frame=(x,y,w,h)=>{ rr(c,x,y+1.4,w,h,4); c.strokeStyle='rgba(255,255,255,0.5)'; c.lineWidth=1; c.stroke();
        rr(c,x,y,w,h,4); c.strokeStyle=rgb(118,120,126); c.lineWidth=1.3; c.stroke(); };
      const diamond=(x,y,r,letter,lsz)=>{ c.save(); c.translate(x,y); c.rotate(Math.PI/4); rr(c,-r,-r,2*r,2*r,2); c.fillStyle=rgb(40,52,96); c.fill(); c.strokeStyle=rgb(208,212,220); c.lineWidth=1.3; c.stroke(); c.restore(); textC(d,x,y,F.bebas,lsz,rgb(222,226,234),letter); };
      // ── left: diamond logo + 0dB / -15dB jacks ──
      diamond(.200*W, .31*H, 11, 'S', 13);
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,9,0,7); c.fillStyle=rgb(18,18,20); c.fill(); c.strokeStyle=rgb(110,112,118); c.lineWidth=1.6; c.stroke(); c.beginPath(); c.arc(x,y,3.5,0,7); c.fillStyle=rgb(42,42,46); c.fill(); };
      jack(.068*W,.41*H); jack(.130*W,.41*H);
      lab(.068,.585,8.5,'0 dB'); lab(.130,.585,8.5,'-15 dB');
      // ── centre: EQUALIZATION frame + 5 knobs ──
      frame(.285*W, PT+.05*PH, .463*W, PH-.10*PH);
      lab(.5165,.235,8,'EQUALIZATION',dim);
      [[.345,'GAIN'],[.435,'BASS'],[.525,'MIDRANGE'],[.615,'TREBLE'],[.705,'MASTER']].forEach(k=>lab(k[0],.56,9.5,k[1]));
      // ── right: MODEL V-4B box + STANDBY / POWER toggles ──
      const mbx=.760*W, mby=PT+.10*PH, mbw=.196*W, mbh=PH-.20*PH;
      frame(mbx,mby,mbw,mbh);
      textC(d,mbx+mbw*0.24,.31*H,F.barlow,7.5,dim,'MODEL'); textC(d,mbx+mbw*0.24,.43*H,F.bebas,16,ink,'V-4B');
      const tog=(cx,lbl)=>{ const x=cx*W,y=.39*H; rr(c,x-5,y-14,10,28,2); c.fillStyle=rgb(152,154,160); c.fill(); rr(c,x-5,y-14,10,28,2); c.strokeStyle=rgb(96,98,104); c.lineWidth=1; c.stroke(); rr(c,x-4,y-13,8,11,1); c.fillStyle=rgb(58,60,64); c.fill(); textC(d,x,.56*H,F.barlow,7,ink,lbl); };
      tog(.877,'STANDBY'); tog(.925,'POWER');
      // ── black grille below: Sampleg diamond + script wordmark ──
      const gy=.82*H;
      diamond(.055*W, gy, 12, 'S', 14);
      textC(d,.100*W,gy,F.bebas,26,rgb(232,234,238),'Sampleg','left');
      textC(d,.955*W,gy,F.crete,15,rgb(150,152,158),'Heritage','right'); } };

  // ── Marsten DBS 7400 — faithful Marshall DBS 7400 (Dynamic Bass System) look
  //   (parody): gold/champagne chassis, black control panel, ALL controls REAL.
  //   Faithful to the real 7400: Primary EQ = Lo/Hi only (no mid); Compression =
  //   Depth knob + Threshold INDICATOR LED (threshold is fixed internally);
  //   graphic EQ = the real 9 bands 50/80/160/320/640/1.25k/2.5k/5k/8k.
  //   Logical ids: 0 Gain 1 Pre-amp Blend 2 Lo 3 Hi 4 Depth 5 Volume
  //   | 6..14 graphic 50/80/160/320/640/1k25/2k5/5k/8k 15 Graphic Level
  //   | 16 Bright 17 Deep 18 Graphic 19 Lo Input
  P.marstendbs7400 = { w:960, h:300,
    knobs:[
      {id:0,cx:.092,cy:.31,r:.021,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.152,cy:.31,r:.021,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.266,cy:.31,r:.021,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.322,cy:.31,r:.021,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.460,cy:.31,r:.021,style:'pointer',cap:[20,20,22]},
      {id:5,cx:.903,cy:.31,r:.032,style:'pointer',cap:[20,20,22]}],
    switches:[
      {id:16,cx:.205,cy:.22,hs:.0090,dark:true},
      {id:17,cx:.205,cy:.40,hs:.0090,dark:true},
      {id:18,cx:.820,cy:.31,hs:.0090,dark:true},
      {id:19,cx:.092,cy:.76,hs:.0090,dark:true}],
    faders:[
      {id:6,cx:.528,y0:.18,y1:.42},{id:7,cx:.557,y0:.18,y1:.42},{id:8,cx:.586,y0:.18,y1:.42},
      {id:9,cx:.615,y0:.18,y1:.42},{id:10,cx:.644,y0:.18,y1:.42},{id:11,cx:.673,y0:.18,y1:.42},
      {id:12,cx:.702,y0:.18,y1:.42},{id:13,cx:.731,y0:.18,y1:.42},{id:14,cx:.760,y0:.18,y1:.42},
      {id:15,cx:.789,y0:.18,y1:.42}],
    // logical id -> real VST param name, so a NAME-keyed saved state (from
    // apply_vst_state) resolves onto these numeric-id controls in the thumbnail.
    names:['Gain','Pre-amp Blend','Lo','Hi','Depth','Volume','50 Hz','80 Hz','160 Hz','320 Hz','640 Hz','1.25 kHz','2.5 kHz','5 kHz','8 kHz','Graphic Level','Bright','Deep','Graphic','Lo Input'],
    tick:rgb(150,148,135), ptr:rgb(236,233,221),
    draw(d,vals){ vals=vals||{}; const {ctx:c,W,H}=d;
      const cream=rgb(220,216,200), dim=rgb(150,148,132), goldInk=rgb(52,44,28);
      // ── gold / champagne brushed chassis ──
      box(d, 182,162,112, true);
      const cg=c.createLinearGradient(0,0,0,H); cg.addColorStop(0,'rgba(255,250,230,0.30)'); cg.addColorStop(.5,'rgba(255,250,230,0.0)'); cg.addColorStop(1,'rgba(60,46,18,0.22)');
      c.fillStyle=cg; c.fillRect(0,0,W,H);
      [[.020,.10],[.020,.90],[.980,.10],[.980,.90]].forEach(s=>{ c.beginPath(); c.arc(s[0]*W,s[1]*H,5,0,7); c.fillStyle=rgb(120,104,64); c.fill(); c.strokeStyle=rgb(70,58,30); c.lineWidth=1; c.stroke(); });
      // ── black control panel ──
      const PL=.045*W, PT=.06*H, PW=.910*W, PH=.52*H;
      rr(c,PL,PT,PW,PH,5); c.fillStyle=rgb(19,19,21); c.fill();
      rr(c,PL,PT,PW,PH,5); c.strokeStyle=rgb(6,6,7); c.lineWidth=2; c.stroke();
      rr(c,PL+3,PT+3,PW-6,PH-6,4); c.strokeStyle='rgba(150,148,132,0.45)'; c.lineWidth=1; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||cream,t);
      const frame=(x,y,w,h)=>{ rr(c,x,y,w,h,3); c.strokeStyle='rgba(176,174,156,0.6)'; c.lineWidth=1; c.stroke(); };
      // PEAK LED + GAIN + PRE-AMP BLEND (real knobs)
      c.beginPath(); c.arc(.080*W,.135*H,4,0,7); c.fillStyle=rgb(214,64,42); c.fill();
      lab(.080,.205,6.5,'PEAK',dim);
      lab(.092,.47,7.5,'GAIN');
      lab(.152,.45,6,'PRE-AMP'); lab(.152,.50,6,'BLEND');
      // BRIGHT / DEEP switch labels
      lab(.205,.135,6.5,'BRIGHT'); lab(.205,.475,6.5,'DEEP');
      // PRIMARY EQ frame (Lo / Hi — 2-band, no mid)
      frame(.234*W,.10*H,.120*W,.40*H); lab(.294,.155,7.5,'PRIMARY EQ',dim);
      [[.266,'LO'],[.322,'HI']].forEach(k=>lab(k[0],.47,6.5,k[1]));
      // COMPRESSION frame (Depth knob right + Threshold INDICATOR LED left, same row — no threshold pot)
      frame(.372*W,.10*H,.120*W,.40*H); lab(.432,.155,7.5,'COMPRESSION',dim);
      c.beginPath(); c.arc(.404*W,.31*H,4,0,7); c.fillStyle=rgb(150,46,38); c.fill();
      lab(.404,.43,5.5,'THRESHOLD',dim);
      lab(.460,.47,6.5,'DEPTH');
      // GRAPHIC EQUALIZER frame (encloses the GRAPHIC switch) + band labels (real 9 bands) + LEVEL
      frame(.510*W,.10*H,.328*W,.40*H); lab(.674,.155,7.5,'GRAPHIC EQUALIZER',dim);
      [['50',.528],['80',.557],['160',.586],['320',.615],['640',.644],['1k25',.673],['2k5',.702],['5k',.731],['8k',.760],['LVL',.789]].forEach(b=>lab(b[1],.465,5.5,b[0],dim));
      lab(.820,.45,6.5,'GRAPHIC');
      // VOLUME (big, right)
      lab(.903,.47,7.5,'VOLUME');
      // ── bottom gold strip: INPUT jack + Marsten script + power ──
      const gy=.78*H;
      const jack=(x)=>{ c.beginPath();c.arc(x*W,gy,7,0,7);c.fillStyle=rgb(28,26,20);c.fill();c.strokeStyle=rgb(110,96,58);c.lineWidth=1.4;c.stroke();c.beginPath();c.arc(x*W,gy,3,0,7);c.fillStyle=rgb(58,52,36);c.fill(); };
      jack(.150); lab(.092,.74,6,'INPUT',goldInk);
      textC(d,.215*W,gy+5,F.crete,30,rgb(248,244,234),'Marsten','left');
      textC(d,.415*W,gy+3,F.barlow,15,goldInk,'DYNAMIC BASS SYSTEM','left');
      rr(c,.945*W-9,gy-12,18,24,2); c.fillStyle=rgb(22,22,24); c.fill(); rr(c,.945*W-6,gy-10,12,9,1); c.fillStyle=rgb(202,52,40); c.fill();
      textC(d,.945*W,.945*H,F.barlow,7,goldInk,'POWER'); } };

  // ── Sharke HB3500 — faithful Hartke HA3500 silver panel (parody) ────────────
  // Silver control panel: Passive/Active inputs + Active pad, Tube + Solid State
  // + Compression knobs, a 10-band graphic EQ (vertical faders 30..16k) with an
  // EQ-In switch, Low Pass / High Pass / Volume knobs, a power rocker, and the
  // Sharke wordmark + MODEL HB3500 along the bottom.
  // Logical ids: 0 Tube 1 Solid 2 Comp 3 LowPass 4 HighPass 5 Volume |
  //   6..15 EQ 30/64/125/250/500/1k/2k/4k/8k/16k | 16 Active 17 EQ In
  P.sharkehb3500 = { w:960, h:300,
    knobs:[
      {id:0,cx:.170,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.245,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.320,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:3,cx:.775,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:4,cx:.840,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:5,cx:.905,cy:.40,r:.023,style:'pointer',cap:[26,26,28]}],
    switches:[
      {id:16,cx:.103,cy:.40,hs:.011,dark:true},
      {id:17,cx:.388,cy:.40,hs:.011,dark:true}],
    faders:[
      {id:6,cx:.420,y0:.27,y1:.56},{id:7,cx:.4512,y0:.27,y1:.56},{id:8,cx:.4824,y0:.27,y1:.56},
      {id:9,cx:.5136,y0:.27,y1:.56},{id:10,cx:.5448,y0:.27,y1:.56},{id:11,cx:.576,y0:.27,y1:.56},
      {id:12,cx:.6072,y0:.27,y1:.56},{id:13,cx:.6384,y0:.27,y1:.56},{id:14,cx:.6696,y0:.27,y1:.56},
      {id:15,cx:.7008,y0:.27,y1:.56}],
    tick:rgb(74,76,82), ptr:rgb(245,246,249),
    draw(d,vals){ vals=vals||{}; const {ctx:c,W,H}=d;
      const ink=rgb(30,31,35), dim=rgb(92,94,100);
      box(d, 26,27,30, true);
      const PL=.03*W,PT=.07*H,PW=.94*W,PH=.62*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(202,204,208)); pg.addColorStop(.5,rgb(180,182,188)); pg.addColorStop(1,rgb(158,160,166));
      rr(c,PL,PT,PW,PH,4); c.fillStyle=pg; c.fill();
      c.save(); rr(c,PL,PT,PW,PH,4); c.clip();
      for(let x=PL;x<PL+PW;x+=2){ c.strokeStyle=(((x|0)%4)?'rgba(255,255,255,0.05)':'rgba(0,0,0,0.045)'); c.lineWidth=1; c.beginPath(); c.moveTo(x,PT); c.lineTo(x,PT+PH); c.stroke(); }
      c.restore();
      rr(c,PL,PT,PW,PH,4); c.strokeStyle=rgb(118,120,126); c.lineWidth=1.5; c.stroke();
      const engrave=(x,y,w,h)=>{ rr(c,x,y+1.5,w,h,5); c.strokeStyle='rgba(255,255,255,0.5)'; c.lineWidth=1; c.stroke(); rr(c,x,y,w,h,5); c.strokeStyle=rgb(112,114,120); c.lineWidth=1.2; c.stroke(); };
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // input box
      const ibx=.04*W,iby=.13*H,ibw=.092*W,ibh=.44*H;
      rr(c,ibx,iby,ibw,ibh,4); c.fillStyle=rgb(150,152,158); c.fill(); rr(c,ibx,iby,ibw,ibh,4); c.strokeStyle=rgb(106,108,114); c.lineWidth=1.2; c.stroke();
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,7,0,7); c.fillStyle=rgb(14,14,16); c.fill(); c.strokeStyle=rgb(88,90,96); c.lineWidth=1.3; c.stroke(); c.beginPath(); c.arc(x,y,3,0,7); c.fillStyle=rgb(34,34,38); c.fill(); };
      jack(ibx+ibw*0.30, iby+ibh*0.24); jack(ibx+ibw*0.30, iby+ibh*0.58);
      textC(d,ibx+ibw*0.58,iby+ibh*0.24,F.barlow,7.5,ink,'PASS','left'); textC(d,ibx+ibw*0.58,iby+ibh*0.58,F.barlow,7.5,ink,'ACT','left');
      lab(.103,.61,9,'ACTIVE');
      // engraved frames — each fully encloses its controls (knobs/faders/switch)
      engrave(.133*W, PT+8, .227*W, PH-16);                  // Tube / Solid / Comp
      engrave(.368*W, PT+8, .352*W, PH-16);                  // EQ-In switch + 10 faders
      engrave(.738*W, PT+8, .205*W, PH-16);                  // Low Pass / High Pass / Volume
      // left knob labels
      [[.170,'TUBE'],[.245,'SOLID ST'],[.320,'COMP']].forEach(k=>lab(k[0],.59,10,k[1]));
      // EQ band freq labels above the faders + section legend below
      const ef=['30','64','125','250','500','1k','2k','4k','8k','16k'];
      const fx=[.420,.4512,.4824,.5136,.5448,.576,.6072,.6384,.6696,.7008];
      for(let i=0;i<10;i++) textC(d,fx[i]*W,.225*H,F.barlow,8.5,dim,ef[i]);
      lab(.560,.62,9.5,'GRAPHIC EQUALIZER');
      // right knob labels
      [[.775,'LOW PASS'],[.840,'HIGH PASS'],[.905,'VOLUME']].forEach(k=>lab(k[0],.59,10,k[1]));
      // power rocker (on the bare panel, right of the right-hand frame)
      const px=.957*W,py=.40*H; rr(c,px-10,py-19,20,38,3); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,px-10,py-19,20,38,3); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2; c.stroke(); rr(c,px-7,py-17,14,17,2); c.fillStyle=rgb(176,32,30); c.fill();
      textC(d,px,.61*H,F.barlow,8.5,ink,'POWER');
      // wordmark + model below the panel
      const gy=.85*H;
      textC(d,.04*W,gy,F.bebas,30,rgb(232,234,238),'Sharke','left');
      textC(d,.955*W,gy,F.barlow,11,rgb(150,152,158),'MODEL HB3500  ·  350 WATTS','right'); } };

  // ── Sharke HB5000 — faithful Hartke HA5000 BLACK panel w/ white accent (parody)
  // Same control surface + geometry as the HB3500, but the HA5000 ships a dark
  // charcoal face (blue pinstripe, white text, green comp LED) instead of the
  // HA3500 silver. EQ centres …2k/3k/5k/8k; MODEL HB5000 / 250+250 WATTS.
  // Logical ids: 0 Tube 1 Solid 2 Comp 3 LowPass 4 HighPass 5 Volume |
  //   6..15 EQ 30/64/125/250/500/1k/2k/3k/5k/8k | 16 Active 17 EQ In
  P.sharkehb5000 = { w:960, h:300,
    knobs:[
      {id:0,cx:.170,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.245,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.320,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:3,cx:.775,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:4,cx:.840,cy:.40,r:.023,style:'pointer',cap:[26,26,28]},
      {id:5,cx:.905,cy:.40,r:.023,style:'pointer',cap:[26,26,28]}],
    switches:[
      {id:16,cx:.103,cy:.40,hs:.011,dark:true},
      {id:17,cx:.388,cy:.40,hs:.011,dark:true}],
    faders:[
      {id:6,cx:.420,y0:.27,y1:.56},{id:7,cx:.4512,y0:.27,y1:.56},{id:8,cx:.4824,y0:.27,y1:.56},
      {id:9,cx:.5136,y0:.27,y1:.56},{id:10,cx:.5448,y0:.27,y1:.56},{id:11,cx:.576,y0:.27,y1:.56},
      {id:12,cx:.6072,y0:.27,y1:.56},{id:13,cx:.6384,y0:.27,y1:.56},{id:14,cx:.6696,y0:.27,y1:.56},
      {id:15,cx:.7008,y0:.27,y1:.56}],
    tick:rgb(150,152,158), ptr:rgb(238,240,244),
    draw(d,vals){ vals=vals||{}; const {ctx:c,W,H}=d;
      const ink=rgb(228,231,237), dim=rgb(120,130,150), stripe=rgb(236,238,242);
      box(d, 15,16,19, true);
      const PL=.03*W,PT=.07*H,PW=.94*W,PH=.62*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(48,50,57)); pg.addColorStop(.5,rgb(34,36,42)); pg.addColorStop(1,rgb(23,25,30));
      rr(c,PL,PT,PW,PH,4); c.fillStyle=pg; c.fill();
      c.save(); rr(c,PL,PT,PW,PH,4); c.clip();
      for(let x=PL;x<PL+PW;x+=2){ c.strokeStyle=(((x|0)%4)?'rgba(255,255,255,0.035)':'rgba(0,0,0,0.13)'); c.lineWidth=1; c.beginPath(); c.moveTo(x,PT); c.lineTo(x,PT+PH); c.stroke(); }
      // white pinstripe across the top of the panel (Hartke "Transient Attack" line)
      c.fillStyle=stripe; c.fillRect(PL,PT+.045*H,PW,2);
      c.restore();
      rr(c,PL,PT,PW,PH,4); c.strokeStyle=rgb(60,64,72); c.lineWidth=1.5; c.stroke();
      const engrave=(x,y,w,h)=>{ rr(c,x,y+1.5,w,h,5); c.strokeStyle='rgba(255,255,255,0.08)'; c.lineWidth=1; c.stroke(); rr(c,x,y,w,h,5); c.strokeStyle=rgb(112,116,124); c.lineWidth=1.2; c.stroke(); };
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      const ibx=.04*W,iby=.13*H,ibw=.092*W,ibh=.44*H;
      rr(c,ibx,iby,ibw,ibh,4); c.fillStyle=rgb(40,42,48); c.fill(); rr(c,ibx,iby,ibw,ibh,4); c.strokeStyle=rgb(66,70,78); c.lineWidth=1.2; c.stroke();
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,7,0,7); c.fillStyle=rgb(10,10,12); c.fill(); c.strokeStyle=rgb(96,100,108); c.lineWidth=1.3; c.stroke(); c.beginPath(); c.arc(x,y,3,0,7); c.fillStyle=rgb(30,30,34); c.fill(); };
      jack(ibx+ibw*0.30, iby+ibh*0.24); jack(ibx+ibw*0.30, iby+ibh*0.58);
      textC(d,ibx+ibw*0.58,iby+ibh*0.24,F.barlow,7.5,ink,'PASS','left'); textC(d,ibx+ibw*0.58,iby+ibh*0.58,F.barlow,7.5,ink,'ACT','left');
      lab(.103,.61,9,'ACTIVE');
      engrave(.133*W, PT+8, .227*W, PH-16);
      engrave(.368*W, PT+8, .352*W, PH-16);
      engrave(.738*W, PT+8, .205*W, PH-16);
      [[.170,'TUBE'],[.245,'SOLID ST'],[.320,'COMP']].forEach(k=>lab(k[0],.59,10,k[1]));
      // green compression LED (above/right of the Comp knob, as on the HA5000)
      c.beginPath(); c.arc(.357*W,.305*H,4.5,0,7); c.fillStyle=rgb(74,214,96); c.fill(); c.strokeStyle=rgb(20,90,30); c.lineWidth=1; c.stroke();
      const ef=['30','64','125','250','500','1k','2k','3k','5k','8k'];
      const fx=[.420,.4512,.4824,.5136,.5448,.576,.6072,.6384,.6696,.7008];
      for(let i=0;i<10;i++) textC(d,fx[i]*W,.225*H,F.barlow,8.5,dim,ef[i]);
      lab(.560,.62,9.5,'GRAPHIC EQUALIZER');
      [[.775,'LOW PASS'],[.840,'HIGH PASS'],[.905,'VOLUME']].forEach(k=>lab(k[0],.59,10,k[1]));
      const px=.957*W,py=.40*H; rr(c,px-10,py-19,20,38,3); c.fillStyle=rgb(16,16,18); c.fill();
      rr(c,px-10,py-19,20,38,3); c.strokeStyle=rgb(64,66,72); c.lineWidth=1.2; c.stroke(); rr(c,px-7,py-17,14,17,2); c.fillStyle=rgb(196,40,36); c.fill();
      textC(d,px,.61*H,F.barlow,8.5,ink,'POWER');
      const gy=.85*H;
      textC(d,.04*W,gy,F.bebas,30,rgb(232,234,238),'Sharke','left');
      textC(d,.955*W,gy,F.barlow,11,rgb(150,152,158),'MODEL HB5000  ·  250+250 WATTS','right'); } };

  P.mouse = { w:320,h:500, knobs:[
      {id:0,cx:.215,cy:.305,r:.105,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.500,cy:.305,r:.105,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.785,cy:.305,r:.105,style:'pointer',cap:[26,26,28]}],
    tick:rgb(232,233,236), ptr:rgb(240,241,244),
    draw(d){ box(d,18,18,20,false); const w=rgb(238,239,242);
      boxedLabel(d,.215,.135,.115,.028,F.barlow,12.5,w,w,'GAIN');
      boxedLabel(d,.500,.135,.110,.028,F.barlow,12.5,w,w,'TONE');
      boxedLabel(d,.785,.135,.125,.028,F.barlow,12.5,w,w,'FILTER');
      boxedLabel(d,.5,.55,.255,.092,F.anton,56,w,w,'MOUSE');
      ledDot(d,d.W*.5,d.H*.71,true,210,70,58); footRound(d,d.W*.5,d.H*.83,24*d.s); } };

  // Bass Overdrive — Darkglass Microtubes B3K look, RS params (4 knobs):
  // Blend0 Drive1 Grunt2 Attack3. (Real B3K has Grunt/Attack as switches, but RS
  // exposes them as continuous knobs, so we keep knobs.)
  P.blackbrass = { w:300,h:490,
    knobs:[
      {id:0,cx:.30,cy:.285,r:.10,style:'boss'},   // BLEND
      {id:1,cx:.70,cy:.285,r:.10,style:'boss'},   // DRIVE
      {id:2,cx:.30,cy:.555,r:.10,style:'boss'},   // GRUNT
      {id:3,cx:.70,cy:.555,r:.10,style:'boss'}],  // ATTACK
    ptr:rgb(238,239,242),
    draw(d){ box(d,18,18,20,false); const w=rgb(235,236,239), dim=rgb(150,151,154);
      textSpaced(d,.5*d.W,.10*d.H,F.bebas,24,w,'BLACKBRASS',2);           // parody brand
      textSpaced(d,.30*d.W,.40*d.H,F.barlow,11,w,'BLEND',1.4);
      textSpaced(d,.70*d.W,.40*d.H,F.barlow,11,w,'DRIVE',1.4);
      textSpaced(d,.30*d.W,.67*d.H,F.barlow,11,w,'GRUNT',1.4);
      textSpaced(d,.70*d.W,.67*d.H,F.barlow,11,w,'ATTACK',1.4);
      textSpaced(d,.5*d.W,.76*d.H,F.barlow,13,w,'MINITUBES B3X',2);       // parody model
      textSpaced(d,.5*d.W,.80*d.H,F.barlow,8,dim,'CMOS BASS OVERDRIVE',1.4);
      ledDot(d,d.W*.5,d.H*.86,true,196,72,60); footRound(d,d.W*.5,d.H*.93,18*d.s); } };

  P.bassbigbuzz = { w:320,h:400, knobs:[
      {id:0,cx:.26,cy:.225,r:.085,style:'davies'},{id:1,cx:.50,cy:.225,r:.085,style:'davies'},
      {id:2,cx:.74,cy:.225,r:.085,style:'davies'}],
    ptr:rgb(236,238,238),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,190,192,196);
      const fg=c.createLinearGradient(0,H*.085,0,H*.915); fg.addColorStop(0,rgb(106,188,64)); fg.addColorStop(1,rgb(74,152,34));
      rr(c,W*.105,H*.085,W*.79,H*.83,10*s); c.fillStyle=fg; c.fill();
      rr(c,W*.105,H*.085,W*.79,H*.83,10*s); c.strokeStyle='rgba(0,0,0,0.27)'; c.lineWidth=1.5*s; c.stroke();
      // mode toggle (static)
      const tx=W*.40, ty=H*.42; rr(c,tx-9*s,ty-6*s,18*s,12*s,3*s); c.fillStyle=rgb(24,24,26); c.fill();
      rr(c,tx-4*s,ty-7*s,8*s,9*s,2*s); c.fillStyle=rgb(220,222,226); c.fill();
      setFont(d,F.barlow,7.5); c.fillStyle=rgb(22,32,16); c.textBaseline='middle';
      c.textAlign='right'; c.fillText('NORM',tx-12*s,ty);
      c.textAlign='left'; c.fillText('BASS BOOST',tx+12*s,ty-5*s); c.fillText('DRY',tx+12*s,ty+5*s);
      // knob labels (Gain / Tone / Filter) — dark green, like the C++ UI
      const gl=rgb(22,32,16);
      textC(d,.26*W,.355*H,F.barlow,11,gl,'GAIN');
      textC(d,.50*W,.355*H,F.barlow,11,gl,'TONE');
      textC(d,.74*W,.355*H,F.barlow,11,gl,'FILTER');
      // 'bass' (script) sits over the 'BIG' word (left), like the real pedal
      outlineText(d,.5*W,.635*H,F.anton,48,rgb(242,242,244),rgb(12,14,16),'BIG BUZZ',5);
      textC(d,.30*W,.525*H,F.crete,34,rgb(16,20,14),'bass');
      // LED at top-centre (above the knobs), clear of the FUZZ wordmark
      ledDot(d,W*.5,H*.105,true,224,60,52); footRound(d,W*.5,H*.77,21*s);
      textC(d,.5*W,.875*H,F.crete,15,rgb(16,20,14),'quimical-harmony');   // brand (Big Muff = E-H at the bottom)
    } };

  // Big Buzz — silver/red vintage fuzz face inspired by a triangle-era fuzz box.
  // Param order: Gain0 Tone1.
  // Big Buzz — Big Muff π-style: brushed-silver box, black logo panel with the
  // red wordmark + π symbol, quimical-harmony parody. RS knob names: Gain0 Tone1.
  P.bigbuzz = { w:320,h:430, knobs:[
      {id:0,cx:.30,cy:.155,r:.082,style:'davies'},
      {id:1,cx:.70,cy:.155,r:.082,style:'davies'}],
    tick:rgb(150,152,158), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, ink=rgb(40,40,44), red=rgb(196,40,42);
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(206,208,212)); g.addColorStop(.5,rgb(184,186,192)); g.addColorStop(1,rgb(160,162,168));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=g; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip();
      c.strokeStyle=rgb(150,153,160,0.18); c.lineWidth=1;
      for(let y=m;y<H-m;y+=3*s){ c.beginPath(); c.moveTo(m,y); c.lineTo(W-m,y); c.stroke(); }
      c.restore();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // knob labels (RS names) + red LED
      textSpaced(d,.30*W,.255*H,F.barlow,10,ink,'GAIN',0.6);
      textSpaced(d,.70*W,.255*H,F.barlow,10,ink,'TONE',0.6);
      ledDot(d,W*.50,H*.135,true,224,52,46);
      // black logo panel
      rr(c,W*.085,H*.315,W*.83,H*.42,10*s); c.fillStyle=rgb(18,18,20); c.fill();
      rr(c,W*.085,H*.315,W*.83,H*.42,10*s); c.strokeStyle=rgb(44,44,48); c.lineWidth=1.4*s; c.stroke();
      // red wordmark + π symbol (Big Muff style)
      outlineText(d,.5*W,.43*H,F.anton,46,red,rgb(244,232,228),'BIG BUZZ',3);
      const px=W*.50, py=H*.585, pw=46*s, ph=40*s;
      c.fillStyle=red;
      c.fillRect(px-pw/2, py-ph/2, pw, 8*s);
      c.fillRect(px-pw/2+6*s, py-ph/2+8*s, 9*s, ph-8*s);
      c.fillRect(px+pw/2-15*s, py-ph/2+8*s, 9*s, ph-8*s);
      // footswitch + brand
      footRound(d,W*.50,H*.815,20*s);
      textC(d,.30*W,.935*H,F.crete,14,ink,'quimical-harmony');
      textSpaced(d,.66*W,.935*H,F.barlow,8,ink,'USA',0.5); } };

  // Super-Buzz — copper/blue octave-fuzz face inspired by the local schematic.
  // Param order: Gain0 Tone1.
  P.superbuzz = { w:320,h:430, knobs:[
      {id:0,cx:.285,cy:.245,r:.096,style:'pointer',cap:[22,24,30]},
      {id:1,cx:.715,cy:.245,r:.096,style:'pointer',cap:[22,24,30]}],
    tick:rgb(36,48,84), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d;
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      const bodyX=W*.085, bodyY=H*.045, bodyW=W*.83, bodyH=H*.89;
      const bg=c.createLinearGradient(0,bodyY,0,bodyY+bodyH);
      bg.addColorStop(0,rgb(184,105,52)); bg.addColorStop(0.48,rgb(142,82,43)); bg.addColorStop(1,rgb(92,60,38));
      rr(c,bodyX,bodyY,bodyW,bodyH,11*s); c.fillStyle=bg; c.fill();
      rr(c,bodyX,bodyY,bodyW,bodyH,11*s); c.strokeStyle=rgb(58,42,34); c.lineWidth=2*s; c.stroke();
      c.fillStyle=rgb(255,236,188,0.075);
      for(let i=0;i<34;i++){ const x=bodyX+12*s+((i*47)%Math.floor(bodyW-24*s)); const y=bodyY+14*s+((i*83)%Math.floor(bodyH-28*s));
        c.fillRect(x,y,1.2*s,1.2*s); }
      screw(d,W*.50,H*.095); screw(d,W*.50,H*.180);
      textSpaced(d,.285*W,.375*H,F.barlow,10.5,rgb(25,36,74),'GAIN',0.8);
      textSpaced(d,.715*W,.375*H,F.barlow,10.5,rgb(25,36,74),'TONE',0.8);
      c.save();
      c.translate(W*.5,H*.625); c.transform(1,0,-0.16,1,0,0);
      setFont(d,F.anton,48); c.textAlign='center'; c.textBaseline='middle'; c.lineWidth=5.0*s;
      c.strokeStyle=rgb(224,210,148); c.fillStyle=rgb(22,58,118); c.strokeText('SUPER',0,-28*s); c.fillText('SUPER',0,-28*s);
      setFont(d,F.anton,58); c.lineWidth=5.5*s; c.strokeText('BUZZ',0,34*s); c.fillText('BUZZ',0,34*s);
      c.restore();
      textSpaced(d,.50*W,.805*H,F.barlow,8.5,rgb(228,216,174),'OCTAVE  FUZZ',2.0);
      ledDot(d,W*.50,H*.845,true,224,62,52); footRound(d,W*.50,H*.910,20*s); } };

  // BZ-1 — Chief compact silicon fuzz face.
  // Param order: Gain0 Tone1.
  // BZ-1 — Chief (Boss-compact) silicon fuzz: same body/treadle/CHIEF-badge
  // styling as the brother's chiefSpec pedals. 2 RS knobs: Gain0 Tone1.
  P.bz1 = chiefSpec(300,480,[188,191,196],
    [{id:0,cx:.33,lbl:'GAIN'},{id:1,cx:.67,lbl:'TONE'}],
    'Fuzz',null,'BZ-1');

  // Chorus — Boss CE-2-style: Chief (Boss-compact) body, CE-2 cyan blue.
  // RS knob names. 3 RS knobs: Rate0 Depth1 Mix2.
  P.ch2 = chiefSpec(300,480,[66,178,210],
    [{id:0,cx:.25,lbl:'RATE'},{id:1,cx:.50,lbl:'DEPTH'},{id:2,cx:.75,lbl:'MIX'}],
    'Chorus',null,'CH-2');

  // Digital Chorus — Boss CE-5-style: Chief body in the CE-5 pale powder-blue,
  // 5 small knobs (RS exposes more than the CE-5's 4). RS knob names.
  // Rate0 Depth1 LoFilter2 HiFilter3 Mix4.
  P.ch5 = { w:300,h:480, knobs:[
      {id:0,cx:.130,cy:.235,r:.052,style:'boss'},
      {id:1,cx:.315,cy:.235,r:.052,style:'boss'},
      {id:2,cx:.500,cy:.235,r:.052,style:'boss'},
      {id:3,cx:.685,cy:.235,r:.052,style:'boss'},
      {id:4,cx:.870,cy:.235,r:.052,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ chiefBody(d,150,196,222); const w=rgb(238,240,242);
      textSpaced(d,.130*d.W,.135*d.H,F.barlow,6.5,w,'RATE',0.1);
      textSpaced(d,.315*d.W,.135*d.H,F.barlow,6.5,w,'DEPTH',0.1);
      textSpaced(d,.500*d.W,.135*d.H,F.barlow,5.5,w,'LO FILTER',0.1);
      textSpaced(d,.685*d.W,.135*d.H,F.barlow,5.5,w,'HI FILTER',0.1);
      textSpaced(d,.870*d.W,.135*d.H,F.barlow,6.5,w,'MIX',0.1);
      chiefName(d,'Chorus','Ensemble','CH-5'); } };

  // Classic Flanger — Boss BF-2-style: Chief body in the BF-2 purple.
  // RS knob names. 3 RS knobs: Rate0 Depth1 Mix2.
  P.fl2 = chiefSpec(300,480,[116,50,126],
    [{id:0,cx:.25,lbl:'RATE'},{id:1,cx:.50,lbl:'DEPTH'},{id:2,cx:.75,lbl:'MIX'}],
    'Flanger',null,'FL-2');

  // Shaver Phaser — Boss PH-1-style: Chief body in the PH-1 grass green.
  // RS knob names. 2 RS knobs: Rate0 Depth1.
  P.ph1 = chiefSpec(300,480,[66,176,70],
    [{id:0,cx:.33,lbl:'RATE'},{id:1,cx:.67,lbl:'DEPTH'}],
    'Phaser',null,'PH-1');

  // Multi-Trem — Boss TR-2-style: Chief body in the TR-2 teal/turquoise.
  // RS knob names. 3 RS knobs: Speed0 Mix1 Waveform2.
  P.tr2 = chiefSpec(300,480,[34,150,146],
    [{id:0,cx:.25,lbl:'SPEED'},{id:1,cx:.50,lbl:'MIX'},{id:2,cx:.75,lbl:'WAVEFORM',lblPx:7}],
    'Tremolo',null,'TR-2');

  // Multi-Vibe — Boss VB-2-style: Chief body in the VB-2 bright blue.
  // RS knob names. 3 RS knobs: Speed0 Mix1 Waveform2.
  P.vb2 = chiefSpec(300,480,[50,140,212],
    [{id:0,cx:.25,lbl:'SPEED'},{id:1,cx:.50,lbl:'MIX'},{id:2,cx:.75,lbl:'WAVEFORM',lblPx:7}],
    'Vibrato',null,'VB-2');

  // Baked Rotatoe — Boss RT-2/RT-20-style rotary: Chief body in the RT silver/
  // champagne (the black knob plate matches the real panel). RS knob names.
  // 4 RS knobs: Rate0 Depth1 Mix2 Balance3.
  P.rt2 = chiefSpec(300,480,[198,194,182],
    [{id:0,cx:.205,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.595,lbl:'MIX'},{id:3,cx:.79,lbl:'BALANCE',lblPx:7}],
    'Rotary','Ensemble','RT-2');

  // NPN Delay — Boss DM-2-style: Chief body in the DM-2 hot pink/red.
  // RS knob names. 3 RS knobs: Time0 Feedback1 Mix2.
  P.dm2 = chiefSpec(300,480,[216,82,114],
    [{id:0,cx:.25,lbl:'TIME'},{id:1,cx:.50,lbl:'FEEDBACK',lblPx:7},{id:2,cx:.75,lbl:'MIX'}],
    'Delay',null,'DM-2');

  // Digital Verb — Boss RV-2-style: Chief body in the RV-2 gunmetal grey.
  // RS knob names. 4 RS knobs: Time0 Mix1 Depth2 Tone3.
  P.rv2 = chiefSpec(300,480,[92,96,102],
    [{id:0,cx:.205,lbl:'TIME'},{id:1,cx:.40,lbl:'MIX'},{id:2,cx:.595,lbl:'DEPTH'},{id:3,cx:.79,lbl:'TONE'}],
    'Digital','Reverb','RV-2');

  // Noise Gate — Boss NF-1-style: Chief body in the NF-1 pale silver-grey.
  // RS knob names. 2 RS knobs: Thresh0 Rate1.
  P.nf1 = chiefSpec(300,480,[198,200,202],
    [{id:0,cx:.33,lbl:'THRESH'},{id:1,cx:.67,lbl:'RATE'}],
    'Noise','Gate','NF-1');

  // Limiter — Boss LM-2-style: Chief body in the LM-2 sky cyan.
  // RS knob names. 2 RS knobs: Limit0 Rate1.
  P.lm2 = chiefSpec(300,480,[64,184,228],
    [{id:0,cx:.33,lbl:'LIMIT'},{id:1,cx:.67,lbl:'RATE'}],
    'Limiter',null,'LM-2');

  // Line Drive — Boss OS-2-style: custom Chief body in OS-2 yellow (the long
  // 'OverDrive/Distortion' name needs its own wordmark sizing). RS knob names.
  // 2 RS knobs: Gain0 Tone1.
  P.os2 = { w:300,h:480, knobs:[
      {id:0,cx:.33,cy:.235,r:.072,style:'boss'},
      {id:1,cx:.67,cy:.235,r:.072,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ chiefBody(d,245,205,30); const wc=rgb(238,240,242), dk=rgb(26,22,10);
      textSpaced(d,.33*d.W,.135*d.H,F.barlow,9,wc,'GAIN',0.2);
      textSpaced(d,.67*d.W,.135*d.H,F.barlow,9,wc,'TONE',0.2);
      textC(d,.45*d.W,.535*d.H,F.crete,34,dk,'OverDrive');
      textC(d,.44*d.W,.610*d.H,F.crete,28,dk,'Distortion');
      textC(d,.74*d.W,.645*d.H,F.barlow,22,dk,'OS-2'); } };

  // Super Drive — Boss SD-1-style: Chief body in the SD-1 amber/golden yellow.
  // RS knob names. 2 RS knobs: Gain0 Tone1.
  P.sd1 = chiefSpec(300,480,[242,180,44],
    [{id:0,cx:.33,lbl:'GAIN'},{id:1,cx:.67,lbl:'TONE'}],
    'Super','OverDrive','SD-1');

  // Standard Distortion — Boss DS-1-style: Chief body in the DS-1 vivid orange.
  // RS knob names. 2 RS knobs: Gain0 Tone1.
  P.ds1 = chiefSpec(300,480,[240,120,34],
    [{id:0,cx:.33,lbl:'GAIN'},{id:1,cx:.67,lbl:'TONE'}],
    'Distortion',null,'DS-1');

  // Metal Distortion — Boss HM-2 Heavy Metal-style: custom Chief in dark charcoal
  // with ORANGE knobs + orange name/labels (the HM-2 signature). RS knob names.
  // 2 RS knobs: Gain0 Tone1.
  P.hm2 = { w:300,h:480, knobs:[
      {id:0,cx:.33,cy:.235,r:.072,style:'pointer',cap:[236,142,42]},
      {id:1,cx:.67,cy:.235,r:.072,style:'pointer',cap:[236,142,42]}],
    tick:rgb(120,100,60), ptr:rgb(38,28,12),
    draw(d){ chiefBody(d,52,54,60); const or=rgb(238,146,46);
      textSpaced(d,.33*d.W,.135*d.H,F.barlow,9,or,'GAIN',0.2);
      textSpaced(d,.67*d.W,.135*d.H,F.barlow,9,or,'TONE',0.2);
      chiefName(d,'Heavy','Metal','HM-2',0,0,or); } };

  // Octavius — Boss OC-5 Octave-style: Chief body in the OC-5 dark chocolate brown.
  // RS knob names. 2 RS knobs: Tone0 Mix1.
  P.oc5 = chiefSpec(300,480,[82,52,40],
    [{id:0,cx:.33,lbl:'TONE'},{id:1,cx:.67,lbl:'MIX'}],
    'Octave',null,'OC-5');

  // Shred Zone — Boss MT-2 Metal Zone-style: custom Chief in MT-2 gunmetal with
  // ORANGE name + labels (black knobs, like the real MT-2). RS knob names.
  // 4 RS knobs: Gain0 Bass1 Mid2 Treble3.
  P.mt2 = { w:300,h:480, knobs:[
      {id:0,cx:.205,cy:.235,r:.072,style:'boss'},
      {id:1,cx:.40,cy:.235,r:.072,style:'boss'},
      {id:2,cx:.595,cy:.235,r:.072,style:'boss'},
      {id:3,cx:.79,cy:.235,r:.072,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ chiefBody(d,60,62,68); const or=rgb(240,132,42);
      textSpaced(d,.205*d.W,.135*d.H,F.barlow,8.5,or,'GAIN',0.2);
      textSpaced(d,.40*d.W,.135*d.H,F.barlow,8.5,or,'BASS',0.2);
      textSpaced(d,.595*d.W,.135*d.H,F.barlow,8.5,or,'MID',0.2);
      textSpaced(d,.79*d.W,.135*d.H,F.barlow,8,or,'TREBLE',0.2);
      chiefName(d,'Metal','Zone','MT-2',0,0,or); } };

  // Amp EQ — Boss FBM-1 Fender '59 Bassman-style: custom Chief in tweed gold with
  // the knob plate + treadle recoloured to the oxblood/café (not black), black
  // knobs, cream script. 6 RS knobs (2 rows). Parody ('59 Bassmate / FBM-1).
  // Bass0 Mid1 Treble2 BassFreq3 MidShift4 TrebleFreq5.
  P.fbm1 = { w:300,h:480, knobs:[
      {id:0,cx:.22,cy:.165,r:.060,style:'boss'},
      {id:1,cx:.50,cy:.165,r:.060,style:'boss'},
      {id:2,cx:.78,cy:.165,r:.060,style:'boss'},
      {id:3,cx:.22,cy:.345,r:.060,style:'boss'},
      {id:4,cx:.50,cy:.345,r:.060,style:'boss'},
      {id:5,cx:.78,cy:.345,r:.060,style:'boss'}],
    ptr:rgb(236,232,220),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, cafe=rgb(96,50,46), cream=rgb(230,224,208);
      c.fillStyle=rgb(10,8,6); c.fillRect(0,0,W,H);
      // gold tweed body + cross-hatch
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(206,166,84)); bg.addColorStop(1,rgb(180,140,62));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(120,80,30,0.18)'; for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(255,240,200,0.10)'; for(let x=0;x<W+H;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x-H,H); c.stroke(); }
      c.restore();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // café knob plate (the 'black' part, recoloured) — taller for 2 rows
      rr(c,W*.07,H*.07,W*.86,H*.39,7*s); c.fillStyle=cafe; c.fill();
      rr(c,W*.07,H*.07,W*.86,H*.39,7*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=1.4*s; c.stroke();
      ledDot(d,W*.85,H*.105,true,224,60,52);
      // knob labels (RS names), cream
      [['BASS',.22,.165,8],['MID',.50,.165,8],['TREBLE',.78,.165,8],['BASS FREQ',.22,.345,6],['MID SHIFT',.50,.345,6],['TREBLE FREQ',.78,.345,6]]
        .forEach(k=> textSpaced(d,k[1]*W,(k[2]+.082)*H,F.barlow,k[3],cream,k[0],0.15));
      // café treadle + cream script name + engraved CHIEF
      const tx=m+4*s, tyTop=H*.50, tw=W-2*m-8*s, tBot=H-m-6*s;
      const tg=c.createLinearGradient(0,tyTop,0,tBot); tg.addColorStop(0,rgb(108,60,54)); tg.addColorStop(1,rgb(82,40,36));
      rr(c,tx,tyTop,tw,tBot-tyTop,12*s); c.fillStyle=tg; c.fill();
      rr(c,tx,tyTop,tw,12*s,12*s); c.fillStyle='rgba(255,255,255,0.08)'; c.fill();
      rr(c,tx,tyTop,tw,tBot-tyTop,12*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=1.6*s; c.stroke();
      textC(d,.46*W,.585*H,F.ink,34,cream,"'59 Bassmate");
      textC(d,.74*W,.648*H,F.barlow,16,cream,'FBM-1');
      outlineText(d,W*.5,H*.805,F.bebas,40,cafe,rgb(56,28,26),'CHIEF',13); } };

  // Bass Emulator — original brand-free design (no real-world counterpart): a
  // synthwave 'BASSQUAKE' — deep indigo box, retro gradient sun, neon perspective
  // grid, glowing wordmark, chrome knobs with neon rings. RS knob names. Body0 Tone1.
  P.bassemulator = { w:280,h:470, knobs:[
      {id:0,cx:.27,cy:.160,r:.066,style:'knurled'},
      {id:1,cx:.73,cy:.160,r:.066,style:'knurled'}],
    tick:rgb(120,200,255), ptr:rgb(40,30,50),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, cyan=rgb(90,220,255), mag=rgb(255,70,160);
      c.fillStyle=rgb(6,6,12); c.fillRect(0,0,W,H);
      // deep indigo->black body
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(38,26,72)); bg.addColorStop(.55,rgb(22,16,44)); bg.addColorStop(1,rgb(12,10,22));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle=rgb(90,220,255,0.35); c.lineWidth=2*s; c.stroke();
      // neon rings behind the knobs
      [.27,.73].forEach(x=>{ c.beginPath(); c.arc(x*W,.160*H,.066*W*1.42,0,7); c.strokeStyle=rgb(90,220,255,0.5); c.lineWidth=2*s; c.stroke(); });
      // knob labels (RS names)
      textSpaced(d,.27*W,.275*H,F.barlow,9.5,cyan,'BODY',0.5);
      textSpaced(d,.73*W,.275*H,F.barlow,9.5,cyan,'TONE',0.5);
      // retro gradient sun
      const sx=.50*W, sy=.45*H, sr=.20*W;
      c.save(); c.beginPath(); c.arc(sx,sy,sr,0,7); c.clip();
      const sg=c.createLinearGradient(0,sy-sr,0,sy+sr); sg.addColorStop(0,rgb(255,212,92)); sg.addColorStop(.5,rgb(255,92,140)); sg.addColorStop(1,rgb(150,60,200));
      c.fillStyle=sg; c.fillRect(sx-sr,sy-sr,sr*2,sr*2);
      c.fillStyle=rgb(16,10,26);
      for(let i=0;i<6;i++){ const yy=sy+sr*0.12+i*(sr*0.88/6); c.fillRect(sx-sr,yy,sr*2,Math.max(1.5*s,(i+1)*0.95*s)); }
      c.restore();
      c.beginPath(); c.arc(sx,sy,sr,0,7); c.strokeStyle=rgb(255,120,170,0.55); c.lineWidth=2*s; c.stroke();
      // glowing wordmark over the sun
      c.save(); setFont(d,F.anton,30); c.textAlign='center'; c.textBaseline='middle';
      c.shadowColor=rgb(255,40,160); c.shadowBlur=14*s; c.fillStyle=rgb(245,248,255);
      c.fillText('BASSQUAKE',sx,.455*H); c.shadowBlur=0; c.restore();
      textSpaced(d,.50*W,.565*H,F.barlow,8,cyan,'BASS  EMULATOR',2.0);
      // neon perspective grid (synthwave floor)
      const gy0=.66*H, gy1=H-m-4*s, vp=.50*W;
      c.save(); rr(c,m,gy0,W-2*m,gy1-gy0,10*s); c.clip();
      c.strokeStyle=rgb(150,80,220,0.55); c.lineWidth=1.2*s;
      for(let i=0;i<=7;i++){ const t=i/7, y=gy0+(gy1-gy0)*t*t; c.beginPath(); c.moveTo(m,y); c.lineTo(W-m,y); c.stroke(); }
      for(let i=-6;i<=6;i++){ c.beginPath(); c.moveTo(vp+i*(W*.025),gy0); c.lineTo(vp+i*(W*.16),gy1); c.stroke(); }
      c.restore();
      // status LED + footswitch
      ledDot(d,W*.50,H*.705,true,90,220,255);
      footRound(d,W*.50,H*.84,20*s); } };

  // Vintage Chorus — MXR Stereo Chorus-style: yellow landscape box, three black
  // knobs in outlined cells, the parody 'NYR' logo box + 'stereo chorus' tag,
  // round footswitch, side jack legends. RS knob names. Rate0 Depth1 Mix2.
  // (Pedal_VintageChorus → AnalogChorus.vst3.)
  P['134stereochorus'] = { w:460,h:330, knobs:[
      {id:0,cx:.205,cy:.275,r:.080,style:'davies'},
      {id:1,cx:.500,cy:.275,r:.080,style:'davies'},
      {id:2,cx:.795,cy:.275,r:.080,style:'davies'}],
    tick:rgb(40,38,30), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const ink=rgb(28,26,20), m=7*s;
      // yellow enclosure (no face screws — MXR-style folded box)
      c.fillStyle=rgb(12,12,10); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(246,214,52)); g.addColorStop(1,rgb(214,182,30));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black pinstripe border
      c.strokeStyle=ink; c.lineWidth=2*s; rr(c,W*.035,H*.06,W*.93,H*.88,10*s); c.stroke();
      // three outlined knob cells
      const cellY=H*.10, cellH=H*.50;
      [[.05,.30],[.35,.30],[.65,.30]].forEach(p=>{ rr(c,W*p[0],cellY,W*p[1],cellH,8*s); c.strokeStyle=ink; c.lineWidth=1.6*s; c.stroke(); });
      // knob labels (RS names)
      textSpaced(d,.205*W,.540*H,F.barlow,12,ink,'RATE',0.5);
      textSpaced(d,.500*W,.540*H,F.barlow,12,ink,'DEPTH',0.5);
      textSpaced(d,.795*W,.540*H,F.barlow,12,ink,'MIX',0.5);
      // NYR logo box (bottom-left)
      rr(c,W*.06,H*.735,W*.215,H*.155,8*s); c.strokeStyle=ink; c.lineWidth=2.6*s; c.stroke();
      textC(d,W*.1675,H*.815,F.anton,30,ink,'NYR');
      // round footswitch (centre)
      footRound(d,W*.50,H*.805,16*s);
      // 'stereo chorus' tag (bottom-right, lowercase)
      textC(d,W*.755,H*.775,F.barlow,23,ink,'stereo');
      textC(d,W*.755,H*.850,F.barlow,23,ink,'chorus');
      // side jack legends (rotated, right edge)
      c.save(); c.translate(W*.965,H*.40); c.rotate(Math.PI/2); textSpaced(d,0,0,F.barlow,8,ink,'OUT',0.4); c.restore();
      c.save(); c.translate(W*.965,H*.66); c.rotate(Math.PI/2); textSpaced(d,0,0,F.barlow,8,ink,'IN',0.4); c.restore(); } };

  // Buzz-Tone — Maestro Fuzz-Tone FZ-1A-style: brown box, two gold knobs poking
  // from the top, white script + shadowed wordmark, big chrome footswitch button,
  // maker text. Parody (Master Buzz-Tone / Hudson). RS knob names. Gain0 Tone1.
  P.buzztone = { w:300,h:480, knobs:[
      {id:0,cx:.28,cy:.095,r:.082,style:'pointer',cap:[206,168,92]},
      {id:1,cx:.72,cy:.095,r:.082,style:'pointer',cap:[206,168,92]}],
    tick:rgb(120,92,50), ptr:rgb(40,28,18),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, cream=rgb(232,224,210);
      c.fillStyle=rgb(8,7,6); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(104,76,56)); bg.addColorStop(1,rgb(74,52,38));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=2*s; c.stroke();
      // knob labels (RS names)
      textSpaced(d,.28*W,.205*H,F.barlow,9,cream,'GAIN',0.4);
      textSpaced(d,.72*W,.205*H,F.barlow,9,cream,'TONE',0.4);
      // 'Master' script + 'Buzz-Tone' shadowed wordmark (parody of Maestro Fuzz-Tone)
      textC(d,.50*W,.315*H,F.ink,40,cream,'Master');
      c.save(); c.translate(.50*W,.420*H); c.transform(1,0,-0.08,1,0,0);
      outlineText(d,0,0,F.anton,44,cream,rgb(46,32,22),'Buzz-Tone',2);
      c.restore();
      // Maestro-style footswitch button (black ring + chrome dome)
      const fx=.50*W, fy=.595*H, R=24*s;
      c.beginPath(); c.arc(fx,fy,R*1.28,0,7); c.fillStyle=rgb(20,20,22); c.fill();
      c.strokeStyle=rgb(8,8,10); c.lineWidth=1.5*s; c.stroke();
      const cg=c.createRadialGradient(fx-R*.3,fy-R*.3,R*.1,fx,fy,R); cg.addColorStop(0,rgb(234,236,240)); cg.addColorStop(1,rgb(150,153,160));
      c.beginPath(); c.arc(fx,fy,R*0.80,0,7); c.fillStyle=cg; c.fill();
      c.beginPath(); c.arc(fx-R*.26,fy-R*.30,R*.30,0,7); c.fillStyle='rgba(255,255,255,0.5)'; c.fill();
      // maker text (Gibson -> Hudson)
      textC(d,.50*W,.805*H,F.crete,16,cream,'Hudson');
      textSpaced(d,.50*W,.850*H,F.barlow,7,cream,'KALAMAZOO  MICHIGAN',0.3);
      textSpaced(d,.50*W,.890*H,F.barlow,7.5,cream,'MODEL BZ-1A',0.4); } };

  // Octave Up — Foxrox Octron3-style: cream box, black knobs + flanking toggles
  // inside a printed outline, bold wordmark + brand, red LED + chrome stomp.
  // Parody (Falcon / Octup). RS knob names. Tone0 Mix1.
  P.octup = { w:280,h:470, knobs:[
      {id:0,cx:.35,cy:.135,r:.062,style:'boss'},
      {id:1,cx:.65,cy:.135,r:.062,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, ink=rgb(34,32,28);
      c.fillStyle=rgb(8,8,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(228,224,210)); bg.addColorStop(1,rgb(208,204,190));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=2*s; c.stroke();
      // printed black outline panel (Octron signature)
      rr(c,W*.08,H*.05,W*.84,H*.27,10*s); c.strokeStyle=ink; c.lineWidth=1.6*s; c.stroke();
      // knob labels (RS names)
      textSpaced(d,.35*W,.225*H,F.barlow,9,ink,'TONE',0.3);
      textSpaced(d,.65*W,.225*H,F.barlow,9,ink,'MIX',0.3);
      // decorative toggles (Bright/Pure, Mids/Low)
      const tog=(tx,up)=>{ const tw=8*s,th=18*s,ty=H*.155; rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.fillStyle=rgb(24,24,26); c.fill();
        rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.strokeStyle=rgb(8,8,10); c.lineWidth=0.8*s; c.stroke();
        const ly=ty+(up?-1:1)*th*0.2; const lg=c.createLinearGradient(tx-3*s,ly-4*s,tx+3*s,ly+4*s); lg.addColorStop(0,rgb(228,230,234)); lg.addColorStop(1,rgb(150,153,160));
        rr(c,tx-3.2*s,ly-5*s,6.4*s,10*s,2*s); c.fillStyle=lg; c.fill(); };
      tog(.165*W,true); tog(.835*W,false);
      textSpaced(d,.165*W,.235*H,F.barlow,5.5,ink,'BRIGHT',0.1); textSpaced(d,.165*W,.265*H,F.barlow,5.5,ink,'PURE',0.1);
      textSpaced(d,.835*W,.235*H,F.barlow,5.5,ink,'MIDS',0.1); textSpaced(d,.835*W,.265*H,F.barlow,5.5,ink,'LOW',0.1);
      // wordmark + brand
      textC(d,.46*W,.420*H,F.anton,48,ink,'OCTUP');
      textSpaced(d,.50*W,.500*H,F.barlow,11,ink,'FALCON',1.5);
      // LED + chrome footswitch
      ledDot(d,W*.50,H*.610,true,224,56,46);
      footRound(d,W*.50,H*.770,20*s); } };

  // Multi Pitch — Moog MF-102 Ring Modulator-style: custom foog (moogerfooger)
  // layout — two white control-group boxes flanking a centre knob + LED column,
  // blue slide switches, foog logo. Parody (FM102). RS knob names.
  // Pitch1-0 Tone1 Mix2.
  P.multipitch = { w:320,h:480, knobs:[
      {id:0,cx:.245,cy:.275,r:.072,style:'moog'},
      {id:1,cx:.500,cy:.205,r:.055,style:'moog'},
      {id:2,cx:.755,cy:.275,r:.072,style:'moog'}],
    tick:rgb(150,152,158), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; foogBody(d); const wt=rgb(226,228,232);
      textC(d,.31*W,.050*H,F.crete,16,wt,'foogermooger');
      textC(d,.79*W,.042*H,F.crete,13,wt,'MULTI');
      textC(d,.79*W,.076*H,F.crete,13,wt,'PITCH');
      // two white control-group boxes (LFO | MODULATOR positions)
      rr(c,W*.085,H*.115,W*.32,H*.40,8*s); c.strokeStyle=wt; c.lineWidth=1.6*s; c.stroke();
      rr(c,W*.595,H*.115,W*.32,H*.40,8*s); c.strokeStyle=wt; c.lineWidth=1.6*s; c.stroke();
      // knob labels (RS names)
      textSpaced(d,.245*W,.165*H,F.barlow,8.5,wt,'PITCH1',0.2);
      textSpaced(d,.500*W,.120*H,F.barlow,8,wt,'TONE',0.3);
      textSpaced(d,.755*W,.165*H,F.barlow,8.5,wt,'MIX',0.3);
      // blue slide switches (MF-102 signature)
      const blueSlide=(sx)=>{ const sw=22*s, sh=11*s, sy=H*.43; rr(c,sx-sw/2,sy-sh/2,sw,sh,3*s); c.fillStyle=rgb(28,30,36); c.fill();
        rr(c,sx-sw/2,sy-sh/2,sw,sh,3*s); c.strokeStyle=rgb(10,10,12); c.lineWidth=0.8*s; c.stroke();
        rr(c,sx-sw*0.40,sy-sh*0.30,sw*0.5,sh*0.6,2*s); c.fillStyle=rgb(74,142,212); c.fill(); };
      blueSlide(.245*W); blueSlide(.755*W);
      // centre LED column (LEVEL / LFO / BYPASS)
      [['LEVEL',.34],['LFO',.43],['BYPASS',.52]].forEach(p=>{ textSpaced(d,.50*W,(p[1]-0.035)*H,F.barlow,5.5,wt,p[0],0.2); ledDot(d,.50*W,p[1]*H,true,150,196,255); });
      // foog logo + model + footswitch
      textC(d,.72*W,.625*H,F.crete,22,wt,'foog');
      textSpaced(d,.30*W,.625*H,F.barlow,9,wt,'FM102',0.3);
      footRound(d,W*.50,H*.800,20*s); } };

  // Send in the Clones — EHX Clone Theory-style: brushed-silver box, black control
  // panel with three knobs, bubble graphics, outline wordmark + green tag, script
  // brand. Parody (quimical-harmony / Attack of the Clones). RS knob names.
  // Clones0 Depth1 Mix2.
  P.attackoftheclones = { w:300,h:420, knobs:[
      {id:0,cx:.20,cy:.155,r:.060,style:'boss'},
      {id:1,cx:.50,cy:.155,r:.060,style:'boss'},
      {id:2,cx:.80,cy:.155,r:.060,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(236,238,242), grn=rgb(122,212,184);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(202,204,208)); bg.addColorStop(1,rgb(168,170,176));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.10)'; c.lineWidth=1;
      for(let y=m;y<H-m;y+=3*s){ c.beginPath(); c.moveTo(m,y); c.lineTo(W-m,y); c.stroke(); } c.restore();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black control panel
      rr(c,W*.06,H*.04,W*.88,H*.66,8*s); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,W*.06,H*.04,W*.88,H*.66,8*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1.4*s; c.stroke();
      // 9V symbol
      c.strokeStyle=wt; c.lineWidth=1.2*s; c.beginPath(); c.arc(W*.475,H*.072,4*s,0,7); c.stroke();
      textSpaced(d,.545*W,.072*H,F.barlow,7,wt,'9V',0.2);
      // knob labels (RS names)
      textSpaced(d,.20*W,.245*H,F.barlow,8.5,wt,'CLONES',0.2);
      textSpaced(d,.50*W,.245*H,F.barlow,8.5,wt,'DEPTH',0.2);
      textSpaced(d,.80*W,.245*H,F.barlow,8.5,wt,'MIX',0.3);
      ledDot(d,W*.50,H*.305,true,224,56,46);
      // bubble accents (Clone Theory signature)
      [[.66,.40,5],[.72,.445,3],[.30,.52,4],[.355,.47,3],[.70,.55,4],[.28,.585,3]].forEach(b=>{ c.beginPath(); c.arc(b[0]*W,b[1]*H,b[2]*s,0,7); c.strokeStyle=grn; c.lineWidth=1.2*s; c.stroke(); });
      // wordmark + tag
      textSpaced(d,.46*W,.395*H,F.barlow,11,wt,'ATTACK OF THE',0.5);
      outlineText(d,.46*W,.485*H,F.anton,42,wt,rgb(12,12,14),'CLONES',2);
      textSpaced(d,.50*W,.560*H,F.barlow,8,grn,'STEREO  CHORUS  VIBRATO',0.5);
      // footswitch
      footRound(d,W*.50,H*.638,20*s);
      // brand on the silver below the panel
      textC(d,.50*W,.825*H,F.crete,16,rgb(28,28,30),'quimical-harmony');
      textSpaced(d,.50*W,.875*H,F.barlow,7.5,rgb(40,40,44),'MADE IN NYC, USA',0.4);
      // side jack legends (rotated)
      c.save(); c.translate(W*.105,H*.42); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6,wt,'STEREO OUT',0.2); c.restore();
      c.save(); c.translate(W*.895,H*.42); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6.5,wt,'INPUT',0.2); c.restore(); } };

  // Bit Cruncher — Parasit Studio Beverly Bitcrusher-style: matte-black box, silver
  // knobs, a small delta insignia, bold white wordmark + LED + stomp. Parody
  // (virustudio / Rockford Bit Crusher). RS knob names.
  // Attack0 FilterType1 Mix2 Release3 Sens4.
  P.bitcruncher = { w:280,h:470, knobs:[
      {id:0,cx:.21,cy:.215,r:.072,style:'knurled'},
      {id:1,cx:.50,cy:.215,r:.072,style:'knurled'},
      {id:2,cx:.79,cy:.215,r:.072,style:'knurled'},
      {id:3,cx:.34,cy:.410,r:.072,style:'knurled'},
      {id:4,cx:.66,cy:.410,r:.072,style:'knurled'}],
    tick:rgb(90,92,98), ptr:rgb(30,30,32),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(232,234,238);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(34,34,36)); bg.addColorStop(1,rgb(20,20,22));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      // brand + delta insignia
      const dx=.315*W, dy=.062*H;
      c.beginPath(); c.moveTo(dx,dy-7*s); c.quadraticCurveTo(dx+6*s,dy+5*s,dx,dy+4*s); c.quadraticCurveTo(dx-6*s,dy+5*s,dx,dy-7*s); c.closePath(); c.fillStyle=wt; c.fill();
      textSpaced(d,.56*W,.062*H,F.barlow,8.5,wt,'VIRUSTUDIO',0.5);
      // knob labels (RS names)
      textSpaced(d,.21*W,.300*H,F.barlow,7,wt,'ATTACK',0.2);
      textSpaced(d,.50*W,.300*H,F.barlow,6,wt,'FILTER TYPE',0.1);
      textSpaced(d,.79*W,.300*H,F.barlow,7.5,wt,'MIX',0.2);
      textSpaced(d,.34*W,.495*H,F.barlow,7,wt,'RELEASE',0.2);
      textSpaced(d,.66*W,.495*H,F.barlow,7.5,wt,'SENS',0.2);
      // wordmark
      textSpaced(d,.50*W,.595*H,F.anton,30,wt,'ROCKFORD',1.0);
      textSpaced(d,.50*W,.660*H,F.anton,22,wt,'BIT CRUSHER',1.5);
      // LED + footswitch
      ledDot(d,W*.50,H*.745,true,120,200,255);
      footRound(d,W*.50,H*.855,20*s); } };

  // Ring Mod — Maestro Ring Modulator-style: silver wedge, black control panel,
  // colour-triangle mark + waveform wordmark, four functional horizontal sliders
  // over silver scales, a PITCH RANGE toggle, footswitch + pedal jacks. Parody
  // (Jefe). RS knob names on the sliders. Depth0 Waveform1 Sensitivity2 Attack3.
  P.ringmod = { w:560,h:340, knobs:[],
    sliders:[
      {id:0,x0:.33,x1:.92,y:.30},
      {id:1,x0:.33,x1:.92,y:.42},
      {id:2,x0:.33,x1:.92,y:.54},
      {id:3,x0:.33,x1:.92,y:.66}],
    ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(232,234,238);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(206,208,212)); bg.addColorStop(1,rgb(170,172,178));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black control panel
      rr(c,W*.035,H*.05,W*.93,H*.66,8*s); c.fillStyle=rgb(22,22,24); c.fill();
      // colour-triangle mark + 'Jefe' + RING MODULATOR + waveform
      const lx=.065*W, ly=.135*H;
      c.fillStyle=rgb(214,52,52); c.beginPath(); c.moveTo(lx,ly+9*s); c.lineTo(lx+9*s,ly-8*s); c.lineTo(lx+18*s,ly+9*s); c.closePath(); c.fill();
      c.fillStyle=rgb(232,192,40); c.beginPath(); c.moveTo(lx+8*s,ly+9*s); c.lineTo(lx+17*s,ly-8*s); c.lineTo(lx+26*s,ly+9*s); c.closePath(); c.fill();
      c.fillStyle=rgb(64,170,210); c.beginPath(); c.moveTo(lx+16*s,ly+9*s); c.lineTo(lx+25*s,ly-8*s); c.lineTo(lx+34*s,ly+9*s); c.closePath(); c.fill();
      textSpaced(d,.155*W,.135*H,F.crete,15,wt,'Jefe',0.2);
      textSpaced(d,.40*W,.095*H,F.anton,18,wt,'RING MODULATOR',0.5);
      c.strokeStyle=wt; c.lineWidth=1.4*s; c.beginPath();
      for(let i=0;i<=30;i++){ const x=.31*W+i*(.20*W/30), y=.155*H+Math.sin(i*0.8)*3.5*s*(0.4+i/30); i?c.lineTo(x,y):c.moveTo(x,y); } c.stroke();
      // PITCH RANGE HIGH/LOW toggle (decorative)
      const tx=.905*W, ty=.135*H, tw=8*s, th=18*s; rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.fillStyle=rgb(150,153,160); c.fill();
      rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.8*s; c.stroke();
      c.beginPath(); c.arc(tx,ty-th*0.22,2.4*s,0,7); c.fillStyle=rgb(232,234,238); c.fill();
      textSpaced(d,.905*W,.052*H,F.barlow,6,wt,'HIGH',0.2); textSpaced(d,.905*W,.215*H,F.barlow,6,wt,'LOW',0.2);
      textSpaced(d,.795*W,.135*H,F.barlow,6,wt,'PITCH RANGE',0.1);
      // silver scale strips behind each slider (engine draws the sliders on top)
      [.30,.42,.54,.66].forEach(y=>{ rr(c,.33*W,(y-0.045)*H,.59*W,.09*H,4*s); c.fillStyle=rgb(182,184,190); c.fill();
        c.strokeStyle=rgb(64,66,70); c.lineWidth=0.8*s; for(let i=0;i<=10;i++){ const xx=.33*W+i*(.59*W/10); c.beginPath(); c.moveTo(xx,(y-0.028)*H); c.lineTo(xx,(y+0.028)*H); c.stroke(); } });
      // slider labels (RS names)
      [['DEPTH',.30],['WAVEFORM',.42],['SENSITIVITY',.54],['ATTACK',.66]].forEach(p=> textC(d,.305*W,p[1]*H,F.barlow,8,wt,p[0],'right'));
      // footswitch + pedal jacks on the silver
      footRound(d,W*.18,H*.85,18*s);
      textSpaced(d,.18*W,.965*H,F.barlow,7,rgb(30,30,32),'MODULATE',0.3);
      const jack=(jx)=>{ const jy=H*.85, R=9*s; const g2=c.createRadialGradient(jx-R*.3,jy-R*.3,R*.1,jx,jy,R); g2.addColorStop(0,rgb(210,212,216)); g2.addColorStop(1,rgb(120,122,128));
        c.beginPath(); c.arc(jx,jy,R,0,7); c.fillStyle=g2; c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=1.2*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,R*0.42,0,7); c.fillStyle=rgb(20,20,22); c.fill(); };
      jack(.50*W); jack(.74*W);
      textSpaced(d,.50*W,.955*H,F.barlow,6.5,rgb(30,30,32),'PITCH PEDAL IN',0.2);
      textSpaced(d,.74*W,.955*H,F.barlow,6,rgb(30,30,32),'MODULATION PEDAL IN',0.1); } };

  // Swole — Aphex Punch Factory-style optical compressor: orange box, black top
  // banner, dB gain-reduction meter, two scaled knobs, distressed wordmark + LED
  // + stomp. Parody (Beta Fist Factory). RS knob names. Smash0 Rate1.
  P.betafist = { w:460,h:330, knobs:[
      {id:0,cx:.16,cy:.55,r:.070,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.80,cy:.32,r:.070,style:'pointer',cap:[26,26,28]}],
    tick:rgb(40,30,20), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, ink=rgb(24,20,16), wt=rgb(240,240,242);
      c.fillStyle=rgb(8,7,6); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(238,116,40)); bg.addColorStop(1,rgb(214,92,26));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black top banner + 'Optical Compressor'
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip();
      c.beginPath(); c.moveTo(m,H*.055); c.lineTo(W-m,H*.035); c.lineTo(W-m,H*.195); c.lineTo(m,H*.235); c.closePath(); c.fillStyle=rgb(18,18,20); c.fill();
      c.restore();
      textSpaced(d,.40*W,.130*H,F.crete,20,wt,'Optical Compressor',0.2);
      // dB gain-reduction meter
      const mx=.38*W, my=.30*H, mw=W*.045, mh=H*.34;
      rr(c,mx,my,mw,mh,4*s); c.fillStyle=rgb(20,16,12); c.fill();
      for(let i=0;i<9;i++){ const segY=my+mh-(i+1)*(mh/9)+1.2*s; rr(c,mx+2*s,segY,mw-4*s,mh/9-2.4*s,1.5*s); c.fillStyle=(i<5)?rgb(255,182,62):rgb(72,52,32); c.fill(); }
      textSpaced(d,.335*W,.275*H,F.barlow,8,ink,'dB',0.2);
      ['2','6','10','14','20'].forEach((n,i)=> textSpaced(d,.335*W,(.345+i*.062)*H,F.barlow,5.5,ink,n,0.1));
      // knob labels (RS names)
      textSpaced(d,.16*W,.705*H,F.barlow,9,ink,'SMASH',0.3);
      textSpaced(d,.80*W,.475*H,F.barlow,9,ink,'RATE',0.3);
      // distressed wordmark + model
      textSpaced(d,.43*W,.725*H,F.anton,26,wt,'Beta Fist',0.5);
      outlineText(d,.43*W,.825*H,F.anton,34,rgb(18,18,20),rgb(250,250,250),'FACTORY',2);
      textSpaced(d,.43*W,.925*H,F.barlow,8,ink,'Model 1404',0.3);
      // LED + footswitch
      ledDot(d,W*.80,H*.555,true,224,40,40);
      footRound(d,W*.80,H*.705,20*s); } };

  // Enbiggenator — TC Electronic Mimiq Doubler-style: cream box, three black
  // knobs, a DUBS 1/2/3 toggle, big grey block wordmark, true-bypass stomp,
  // side jacks. Parody (LC Quimical / Mime). RS knob names. Rate0 Depth1 Mix2.
  P.mime = { w:280,h:470, knobs:[
      {id:0,cx:.55,cy:.155,r:.066,style:'davies'},
      {id:1,cx:.34,cy:.335,r:.066,style:'davies'},
      {id:2,cx:.68,cy:.335,r:.066,style:'davies'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, ink=rgb(58,58,62), gry=rgb(150,150,156);
      c.fillStyle=rgb(8,8,8); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(228,224,212)); bg.addColorStop(1,rgb(206,202,190));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=2*s; c.stroke();
      // DUBS 1/2/3 toggle (decorative)
      const tx=.20*W, ty=.165*H, tw=8*s, th=24*s;
      rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.fillStyle=rgb(40,40,44); c.fill();
      rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.strokeStyle=rgb(10,10,12); c.lineWidth=0.8*s; c.stroke();
      const lg=c.createLinearGradient(tx-4*s,ty-th*0.32,tx+4*s,ty-th*0.12); lg.addColorStop(0,rgb(228,230,234)); lg.addColorStop(1,rgb(150,153,160));
      rr(c,tx-3.5*s,ty-th*0.40,7*s,9*s,2*s); c.fillStyle=lg; c.fill();
      textSpaced(d,.295*W,.095*H,F.barlow,7,ink,'DUBS',0.3);
      ['1','2','3'].forEach((n,i)=> textSpaced(d,.285*W,(.128+i*.034)*H,F.barlow,6,ink,n,0.1));
      // knob labels (RS names)
      textSpaced(d,.55*W,.072*H,F.barlow,8,ink,'RATE',0.3);
      textSpaced(d,.34*W,.252*H,F.barlow,8,ink,'DEPTH',0.3);
      textSpaced(d,.68*W,.252*H,F.barlow,8,ink,'MIX',0.3);
      // LED + grey block wordmark + DOUBLER
      ledDot(d,W*.50,H*.445,true,224,56,46);
      textSpaced(d,.50*W,.545*H,F.anton,48,gry,'MIME',2);
      textSpaced(d,.50*W,.615*H,F.barlow,11,ink,'DOUBLER',2.5);
      // true-bypass footswitch + label
      footRound(d,W*.42,H*.760,20*s);
      textSpaced(d,.71*W,.745*H,F.barlow,8,ink,'true',0.3); textSpaced(d,.71*W,.778*H,F.barlow,8,ink,'bypass',0.3);
      // brand + side jack legends
      textSpaced(d,.50*W,.905*H,F.barlow,9,ink,'LC QUIMICAL',1.0);
      c.save(); c.translate(W*.07,H*.40); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,5.5,ink,'STEREO OUT MONO',0.1); c.restore();
      c.save(); c.translate(W*.93,H*.40); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,5.5,ink,'STEREO IN MONO',0.1); c.restore(); } };

  // US Wah — Dunlop Cry Baby-style wah treadle: black ribbed rocker tread + a
  // top control strip (AUTO toggle + 3 knobs) + a logo badge. Parody (cry man).
  // RS knob names. Auto0 Pedal1 Sens2 Speed3.
  P.cryman = { w:280,h:480,
    knobs:[
      {id:1,cx:.38,cy:.160,r:.052,style:'boss'},
      {id:2,cx:.60,cy:.160,r:.052,style:'boss'},
      {id:3,cx:.82,cy:.160,r:.052,style:'boss'}],
    switches:[{id:0,cx:.15,cy:.155,hs:.030}],
    ptr:rgb(238,240,242),
    draw(d,values){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(236,238,242);
      c.fillStyle=rgb(6,6,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(34,34,36)); bg.addColorStop(1,rgb(20,20,22));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      // top control panel
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.fillStyle=rgb(16,16,18); c.fill();
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1.2*s; c.stroke();
      // control labels (RS names); AUTO label brightens with its toggle state
      const av=(values&&values[0]!=null)?values[0]:1;
      textSpaced(d,.15*W,.245*H,F.barlow,7,av>=0.5?wt:rgb(150,150,156),'AUTO',0.2);
      textSpaced(d,.38*W,.245*H,F.barlow,7,wt,'PEDAL',0.2);
      textSpaced(d,.60*W,.245*H,F.barlow,7,wt,'SENS',0.2);
      textSpaced(d,.82*W,.245*H,F.barlow,7,wt,'SPEED',0.2);
      // ribbed treadle (the iconic Cry Baby tread)
      const tx0=.10*W, tx1=.90*W, ty0=.335*H, ty1=.95*H;
      rr(c,tx0,ty0,tx1-tx0,ty1-ty0,10*s); c.fillStyle=rgb(24,24,26); c.fill();
      rr(c,tx0,ty0,tx1-tx0,ty1-ty0,10*s); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.5*s; c.stroke();
      c.save(); rr(c,tx0,ty0,tx1-tx0,ty1-ty0,10*s); c.clip();
      for(let y=ty0+7*s;y<ty1;y+=7*s){ c.beginPath(); c.moveTo(tx0,y-1*s); c.lineTo(tx1,y-1*s); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=2.4*s; c.stroke();
        c.beginPath(); c.moveTo(tx0,y+2*s); c.lineTo(tx1,y+2*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=1.4*s; c.stroke(); }
      c.restore();
      // 'Cry Man' badge
      const bx=W*.23, by=H*.555, bw=W*.54, bh=H*.105;
      rr(c,bx,by,bw,bh,4*s); c.fillStyle=rgb(10,10,12); c.fill();
      rr(c,bx,by,bw,bh,4*s); c.strokeStyle=rgb(70,70,74); c.lineWidth=1*s; c.stroke();
      textC(d,.50*W,by+bh*0.42,F.crete,26,wt,'Cry Man');
      textSpaced(d,.50*W,by+bh*0.80,F.barlow,7.5,wt,'WAH 535',1.5); } };

  // UK Wah — Vox V847-style wah: black treadle with a chrome frame + big chrome
  // 'BOX' letters down the ribbed tread. Parody (BOX). RS knob names.
  // Auto0 Pedal1 Sens2 Speed3.
  P.boxb847 = { w:280,h:480,
    knobs:[
      {id:1,cx:.38,cy:.160,r:.052,style:'boss'},
      {id:2,cx:.60,cy:.160,r:.052,style:'boss'},
      {id:3,cx:.82,cy:.160,r:.052,style:'boss'}],
    switches:[{id:0,cx:.15,cy:.155,hs:.030}],
    ptr:rgb(238,240,242),
    draw(d,values){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(236,238,242), chrome=rgb(214,216,220);
      c.fillStyle=rgb(6,6,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(40,40,44)); bg.addColorStop(1,rgb(24,24,28));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.fillStyle=rgb(16,16,18); c.fill();
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1.2*s; c.stroke();
      const av=(values&&values[0]!=null)?values[0]:1;
      textSpaced(d,.15*W,.245*H,F.barlow,7,av>=0.5?wt:rgb(150,150,156),'AUTO',0.2);
      textSpaced(d,.38*W,.245*H,F.barlow,7,wt,'PEDAL',0.2);
      textSpaced(d,.60*W,.245*H,F.barlow,7,wt,'SENS',0.2);
      textSpaced(d,.82*W,.245*H,F.barlow,7,wt,'SPEED',0.2);
      // chrome frame + ribbed black tread
      const tx0=.13*W, tx1=.87*W, ty0=.335*H, ty1=.95*H;
      rr(c,tx0-5*s,ty0-5*s,(tx1-tx0)+10*s,(ty1-ty0)+10*s,12*s);
      const fg=c.createLinearGradient(tx0,ty0,tx1,ty1); fg.addColorStop(0,rgb(234,236,240)); fg.addColorStop(.5,rgb(168,170,176)); fg.addColorStop(1,rgb(212,214,218));
      c.fillStyle=fg; c.fill();
      rr(c,tx0,ty0,tx1-tx0,ty1-ty0,9*s); c.fillStyle=rgb(22,22,24); c.fill();
      c.save(); rr(c,tx0,ty0,tx1-tx0,ty1-ty0,9*s); c.clip();
      for(let y=ty0+7*s;y<ty1;y+=7*s){ c.beginPath(); c.moveTo(tx0,y-1*s); c.lineTo(tx1,y-1*s); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=2.4*s; c.stroke();
        c.beginPath(); c.moveTo(tx0,y+2*s); c.lineTo(tx1,y+2*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=1.4*s; c.stroke(); }
      c.restore();
      ['B','O','X'].forEach((ch,i)=> outlineText(d,.50*W,(.475+i*.145)*H,F.anton,52,chrome,rgb(18,18,20),ch,0));
      textSpaced(d,.50*W,.895*H,F.barlow,11,chrome,'B847',0.6);   // model number (parody)
      c.save(); c.translate(.175*W,.64*H); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6,wt,'MUTE',0.3); c.restore();
      c.save(); c.translate(.825*W,.64*H); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6,wt,'WAH',0.3); c.restore(); } };

  // Modern Wah — Morley Bad Horsie-style wah: all-black treadle, big white-outline
  // 'JOCKEY' down the ribbed tread + red 'Bad' script, red labels. Parody (Jockey).
  // RS knob names. Auto0 Pedal1 Sens2 Speed3.
  P.jockeybad = { w:280,h:480,
    knobs:[
      {id:1,cx:.38,cy:.160,r:.052,style:'boss'},
      {id:2,cx:.60,cy:.160,r:.052,style:'boss'},
      {id:3,cx:.82,cy:.160,r:.052,style:'boss'}],
    switches:[{id:0,cx:.15,cy:.155,hs:.030}],
    ptr:rgb(238,240,242),
    draw(d,values){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(238,240,244), red=rgb(216,42,46);
      c.fillStyle=rgb(6,6,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(26,26,28)); bg.addColorStop(1,rgb(14,14,16));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.fillStyle=rgb(14,14,16); c.fill();
      rr(c,W*.06,H*.04,W*.88,H*.255,8*s); c.strokeStyle=rgb(60,60,64); c.lineWidth=1.2*s; c.stroke();
      const av=(values&&values[0]!=null)?values[0]:1;
      textSpaced(d,.15*W,.245*H,F.barlow,7,av>=0.5?red:rgb(120,58,60),'AUTO',0.2);
      textSpaced(d,.38*W,.245*H,F.barlow,7,red,'PEDAL',0.2);
      textSpaced(d,.60*W,.245*H,F.barlow,7,red,'SENS',0.2);
      textSpaced(d,.82*W,.245*H,F.barlow,7,red,'SPEED',0.2);
      const tx0=.12*W, tx1=.88*W, ty0=.335*H, ty1=.95*H;
      rr(c,tx0,ty0,tx1-tx0,ty1-ty0,9*s); c.fillStyle=rgb(18,18,20); c.fill();
      rr(c,tx0,ty0,tx1-tx0,ty1-ty0,9*s); c.strokeStyle=rgb(60,60,64); c.lineWidth=1.5*s; c.stroke();
      c.save(); rr(c,tx0,ty0,tx1-tx0,ty1-ty0,9*s); c.clip();
      for(let y=ty0+7*s;y<ty1;y+=7*s){ c.beginPath(); c.moveTo(tx0,y-1*s); c.lineTo(tx1,y-1*s); c.strokeStyle='rgba(255,255,255,0.05)'; c.lineWidth=2.4*s; c.stroke();
        c.beginPath(); c.moveTo(tx0,y+2*s); c.lineTo(tx1,y+2*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=1.4*s; c.stroke(); }
      c.translate(.50*W,.64*H); c.rotate(-Math.PI/2);
      outlineText(d,0,0,F.anton,46,rgb(18,18,20),wt,'JOCKEY',3);
      c.restore();
      textC(d,.50*W,.385*H,F.ink,22,red,'Bad');
      textSpaced(d,.50*W,.925*H,F.barlow,7,wt,'CONTOUR WAH',1.0); } };

  function chiefSpec(w,h,col,knobIds,n1,n2,code,plate){
    const lum=0.299*col[0]+0.587*col[1]+0.114*col[2], ink=lum>120?rgb(16,16,20):rgb(232,234,238);
    return { w,h, knobs: knobIds.map(k=>({id:k.id,cx:k.cx,cy:.235,r:.072,style:'boss'})),
    ptr:rgb(238,240,242), draw(d){ chiefBody(d,col[0],col[1],col[2],plate); const wc=rgb(238,240,242);
      knobIds.forEach(k=> textSpaced(d,k.cx*d.W,.135*d.H,F.barlow,k.lblPx||8.5,wc,k.lbl,0.2));
      chiefName(d,n1,n2,code,0,0,ink); } }; }

  P.cb3 = chiefSpec(300,480,[40,158,150],
    [{id:0,cx:.205,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.595,lbl:'LO FILTER',lblPx:8},{id:3,cx:.79,lbl:'MIX'}],
    'Bass','Chorus','CB-3');
  P.so2 = { w:300,h:480, knobs:[{id:0,cx:.34,cy:.235,r:.088,style:'boss'},{id:1,cx:.66,cy:.235,r:.088,style:'boss'}],
    ptr:rgb(236,232,224), draw(d){ chiefBody(d,112,70,66); const w=rgb(236,232,224);
      textSpaced(d,.34*d.W,.12*d.H,F.barlow,9,w,'MIX',0.2); textSpaced(d,.66*d.W,.12*d.H,F.barlow,9,w,'TONE',0.2);
      chiefName(d,'Bass','Suboctave','SO-2'); } };
  P.dl3 = chiefSpec(300,480,[156,64,72],
    [{id:0,cx:.205,lbl:'TIME'},{id:1,cx:.40,lbl:'FEEDBACK',lblPx:7.5},{id:2,cx:.595,lbl:'MIX'},{id:3,cx:.79,lbl:'FILTER',lblPx:8}],
    'Bass','Delay','DL-3');
  P.fl3 = chiefSpec(300,480,[96,80,134],
    [{id:0,cx:.205,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.595,lbl:'FILTER',lblPx:8},{id:3,cx:.79,lbl:'MIX'}],
    'Bass','Flanger','FL-3');

  function boxSpec(w,h,col,knobs,wordmark,sub,accent,wfont){ return { w,h,
    knobs: knobs.map(k=>({id:k.id,cx:k.cx,cy:.27,r:.082,style:'pointer',cap:k.cap||[40,40,44]})),
    tick: accent? rgb(accent[0]*0.6,accent[1]*0.6,accent[2]*0.6): rgb(150,150,150),
    draw(d){ box(d,col[0],col[1],col[2]); const lc=accent?rgb(accent[0],accent[1],accent[2]):rgb(238,238,240);
      knobs.forEach(k=> textC(d,k.cx*d.W,(.27+0.082*1.45+0.012)*d.H,F.barlow,11,lc,k.lbl));
      textC(d,.5*d.W,.60*d.H,wfont||F.bebas,44,lc,wordmark);
      if(sub) textC(d,.5*d.W,.68*d.H,F.barlow,10,rgb(170,170,176),sub);
      ledDot(d,d.W*.5,d.H*.77,true,210,70,58); footRound(d,d.W*.5,d.H*.88,23*d.s); } }; }

  // Bass Phase — MXR-style orange box (NYR parody, matches the Dyna Comp).
  // 2x2 knobs; NYR logo centred with the LED above it; name below the footswitch.
  // RS params: Rate0 Depth1 Mix2 Filter3.
  P.phase99 = { w:300,h:460,
    knobs:[
      {id:0,cx:.30,cy:.175,r:.088,style:'davies'},  // RATE
      {id:1,cx:.70,cy:.175,r:.088,style:'davies'},  // DEPTH
      {id:2,cx:.30,cy:.395,r:.088,style:'davies'},  // MIX
      {id:3,cx:.70,cy:.395,r:.088,style:'davies'}], // FILTER
    tick:rgb(46,24,4), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const ink=rgb(42,22,6), m=7*s;
      c.fillStyle=rgb(10,8,6); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(240,134,32)); g.addColorStop(1,rgb(210,96,14));
      rr(c,m,m,W-2*m,H-2*m,13*s); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,13*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      textSpaced(d,.30*W,.275*H,F.barlow,9.5,ink,'RATE',0.4);
      textSpaced(d,.70*W,.275*H,F.barlow,9.5,ink,'DEPTH',0.4);
      textSpaced(d,.30*W,.495*H,F.barlow,9.5,ink,'MIX',0.4);
      textSpaced(d,.70*W,.495*H,F.barlow,9,ink,'FILTER',0.3);
      ledDot(d,W*.50,H*.565,true,224,52,46);                    // LED above the NYR logo
      const bw=W*.28,bh=H*.09,bx=W*.5-bw/2,by=H*.635-bh/2;
      rr(c,bx,by,bw,bh,8*s); c.strokeStyle=ink; c.lineWidth=2.6*s; c.stroke();
      textC(d,W*.5,H*.635,F.anton,30,ink,'NYR');
      footRound(d,W*.50,H*.80,16*s);
      textC(d,W*.50,H*.91,F.crete,24,ink,'phase 99'); } };  // name below the footswitch

  // Phaser — MXR Phase 90-style: orange box, single SPEED knob, NYR logo box,
  // vertical jacks, red LED, footswitch, 'phase 90' tag. RS knob name. Rate0.
  P.phase90 = { w:300,h:460, knobs:[
      {id:0,cx:.50,cy:.20,r:.11,style:'davies'}],
    tick:rgb(46,24,4), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const ink=rgb(42,22,6), m=7*s;
      c.fillStyle=rgb(10,8,6); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(240,134,32)); g.addColorStop(1,rgb(210,96,14));
      rr(c,m,m,W-2*m,H-2*m,13*s); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,13*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      textSpaced(d,.50*W,.345*H,F.barlow,10,ink,'RATE',0.5);
      c.save(); c.translate(W*.075,H*.46); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,8,ink,'OUTPUT',0.4); c.restore();
      c.save(); c.translate(W*.925,H*.46); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,8,ink,'INPUT',0.4); c.restore();
      const bw=W*.30,bh=H*.095,bx=W*.5-bw/2,by=H*.475-bh/2;
      rr(c,bx,by,bw,bh,8*s); c.strokeStyle=ink; c.lineWidth=2.6*s; c.stroke();
      textC(d,W*.5,H*.475,F.anton,30,ink,'NYR');
      ledDot(d,W*.50,H*.60,true,224,52,46);
      footRound(d,W*.50,H*.725,16*s);
      textC(d,W*.50,H*.88,F.crete,24,ink,'phase 90'); } };

  // Plane Phase — Roland Jet Phaser AP-7-style: brown wedge body, black control
  // panel with an orange logo + AP.7, 3 knobs, two footswitches. Parody
  // (Ronald / Rocket Phase). RS knob names. Rate0 Depth1 Mix2.
  P.rocketphase = { w:480,h:300, knobs:[
      {id:0,cx:.27,cy:.295,r:.058,style:'boss'},
      {id:1,cx:.50,cy:.295,r:.058,style:'boss'},
      {id:2,cx:.73,cy:.295,r:.058,style:'boss'}],
    ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, org=rgb(230,112,30), wt=rgb(226,226,222);
      c.fillStyle=rgb(8,8,8); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(128,92,58)); bg.addColorStop(1,rgb(96,66,40));
      rr(c,m,m,W-2*m,H-2*m,10*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,10*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black control panel + orange border
      rr(c,W*.045,H*.05,W*.91,H*.46,7*s); c.fillStyle=rgb(22,20,22); c.fill();
      rr(c,W*.045,H*.05,W*.91,H*.46,7*s); c.strokeStyle=org; c.lineWidth=1.6*s; c.stroke();
      // orange italic logo + model
      c.save(); c.translate(W*.30,H*.155); c.transform(1,0,-0.18,1,0,0);
      outlineText(d,0,0,F.anton,28,org,rgb(40,18,4),'ROCKET PHASE',1.5); c.restore();
      textC(d,W*.85,H*.155,F.anton,18,org,'AP.7');
      // knob labels (RS names) in orange
      textSpaced(d,.27*W,.43*H,F.barlow,9,org,'RATE',0.4);
      textSpaced(d,.50*W,.43*H,F.barlow,9,org,'DEPTH',0.4);
      textSpaced(d,.73*W,.43*H,F.barlow,9,org,'MIX',0.4);
      // 'Ronald' brand (bottom-right of the panel)
      textC(d,W*.85,H*.45,F.crete,16,org,'Ronald');
      // two footswitches on the brown body
      footRound(d,W*.30,H*.74,17*s);
      footRound(d,W*.70,H*.74,17*s);
      textSpaced(d,.30*W,.90*H,F.barlow,8,wt,'EFFECT',0.4);
      textSpaced(d,.70*W,.90*H,F.barlow,8,wt,'FAST',0.4); } };

  // Tremolo — Colorsound Tremolo-style: purple box, bubble logo, two chrome
  // knobs, a comic 'TREMOLO' starburst badge. Parody (Lightaudio). RS knob
  // names. Speed0 Mix1.
  P.tremolo = { w:260,h:470, knobs:[
      {id:0,cx:.30,cy:.37,r:.10,style:'knurled'},
      {id:1,cx:.70,cy:.37,r:.10,style:'knurled'}],
    tick:rgb(70,46,66), ptr:rgb(40,40,44),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, wt=rgb(238,232,240);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(138,96,128)); g.addColorStop(1,rgb(108,74,104));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // top jack labels
      textSpaced(d,.27*W,.06*H,F.barlow,7,wt,'INSTRUMENT',0.2);
      textSpaced(d,.74*W,.06*H,F.barlow,7,wt,'AMPLIFIER',0.2);
      // bubble logo + subtitle
      outlineText(d,.5*W,.155*H,F.anton,28,rgb(120,82,112),wt,'LIGHTAUDIO',1.5);
      textSpaced(d,.5*W,.225*H,F.barlow,7.5,wt,'LIGHT SOUND',0.4);
      textSpaced(d,.5*W,.255*H,F.barlow,7.5,wt,'LONDON ENGLAND',0.4);
      // knob labels (RS names)
      textSpaced(d,.30*W,.49*H,F.barlow,10,wt,'SPEED',0.5);
      textSpaced(d,.70*W,.49*H,F.barlow,10,wt,'MIX',0.5);
      // 'TREMOLO' comic starburst badge
      const bx=W*.5, by=H*.67, Ro=W*.40, Ri=W*.30, n=12;
      c.beginPath();
      for(let i=0;i<2*n;i++){ const r2=(i%2?Ri:Ro), a=i*Math.PI/n - Math.PI/2;
        const x=bx+r2*Math.cos(a), y=by+r2*Math.sin(a)*0.7;
        i?c.lineTo(x,y):c.moveTo(x,y); }
      c.closePath(); c.fillStyle=rgb(70,44,64); c.fill();
      c.strokeStyle=wt; c.lineWidth=2.4*s; c.stroke();
      c.save(); c.translate(bx,by); c.rotate(-0.08);
      outlineText(d,0,0,F.anton,40,wt,rgb(40,24,38),'TREMOLO',1); c.restore();
      // footswitch
      footRound(d,W*.5,H*.89,18*s); } };

  // Amp Trem — Fulltone Supa-Trem ST-1-style: wide satin-black box, two big
  // black DAVIES knobs, white brush-script logo, red+blue LEDs, three foot-
  // switches (Half Speed / Bypass / Hard-Soft) + a small Volume trim. Parody
  // (ExtraTone / Mega-Trem). RS knob names. Speed0 Depth1.
  P.megatrem = { w:560,h:340, knobs:[
      {id:0,cx:.30,cy:.275,r:.070,style:'davies'},
      {id:1,cx:.60,cy:.305,r:.070,style:'davies'}],
    tick:rgb(60,60,64), ptr:rgb(236,238,242),
    draw(d){ const {ctx:c,W,H,s}=d; const wt=rgb(238,240,244);
      box(d,30,30,32);
      // knob labels (RS names) above each knob, clear of the skirt
      textSpaced(d,.30*W,.085*H,F.barlow,10,wt,'SPEED',0.5);
      textSpaced(d,.60*W,.115*H,F.barlow,10,wt,'DEPTH',0.5);
      // small cosmetic VOLUME trim (top-right, like the ST-1)
      const vx=.875*W, vy=.275*H, vr=15*s;
      const vg=c.createRadialGradient(vx-vr*0.4,vy-vr*0.5,vr*0.1,vx,vy,vr*1.1);
      vg.addColorStop(0,rgb(56,56,60)); vg.addColorStop(1,rgb(18,18,20));
      c.beginPath(); c.arc(vx,vy,vr,0,7); c.fillStyle=vg; c.fill();
      c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      c.beginPath(); c.moveTo(vx,vy); c.lineTo(vx+vr*0.92*Math.cos(-2.2),vy+vr*0.92*Math.sin(-2.2));
      c.strokeStyle=wt; c.lineWidth=2*s; c.stroke();
      textSpaced(d,vx,vy+vr+9*s,F.barlow,7.5,wt,'VOLUME',0.3);
      // LEDs: red (near Half Speed) + blue (near Bypass)
      ledDot(d,.175*W,.575*H,true,224,46,42);
      ledDot(d,.36*W,.71*H,true,70,170,238);
      // brush-script logo (parody) + ST-1 badge
      textC(d,.47*W,.58*H,F.ink,50,wt,'Mega-Trem');
      textC(d,.515*W,.70*H,F.ink,22,wt,'by ExtraTone');
      textC(d,.745*W,.635*H,F.anton,26,wt,'ST-1');
      // three footswitches + legends
      footRound(d,W*.18,H*.82,15*s); textSpaced(d,.18*W,.95*H,F.barlow,7.5,wt,'HALF SPEED',0.3);
      footRound(d,W*.50,H*.82,16*s); textSpaced(d,.50*W,.95*H,F.barlow,7.5,wt,'BYPASS',0.3);
      footRound(d,W*.80,H*.82,16*s); textSpaced(d,.80*W,.95*H,F.barlow,7.5,wt,'HARD / SOFT',0.3); } };

  // Trem Ole — Keeley DynaTrem-style: crimson brushed top panel with 4 black
  // fluted knobs + a 3-way mode toggle, over a tan tweed grille-cloth bottom
  // with a gold 'DYNA-TREM' plaque + script brand + chrome stomp. Parody
  // (Peeley). RS knob names. Sens0 Attack1 Release2 Mix3.
  P.dynatrem = { w:280,h:460, knobs:[
      {id:0,cx:.255,cy:.175,r:.078,style:'boss'},
      {id:1,cx:.745,cy:.175,r:.078,style:'boss'},
      {id:2,cx:.255,cy:.375,r:.078,style:'boss'},
      {id:3,cx:.745,cy:.375,r:.078,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, wt=rgb(240,236,230), cream=rgb(238,224,196);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip();
      // crimson brushed top panel
      const rg=c.createRadialGradient(W*.5,H*.08,W*.04,W*.5,H*.28,W*.75);
      rg.addColorStop(0,rgb(208,48,52)); rg.addColorStop(1,rgb(146,20,30));
      c.fillStyle=rg; c.fillRect(0,0,W,H*.52);
      c.strokeStyle='rgba(255,255,255,0.05)'; c.lineWidth=1;
      for(let i=1;i<11;i++){ c.beginPath(); c.arc(W*.5,H*.06,W*.085*i,0,Math.PI); c.stroke(); }
      // tan tweed grille-cloth bottom
      c.fillStyle=rgb(150,128,96); c.fillRect(0,H*.52,W,H*.48);
      c.lineWidth=1; c.strokeStyle='rgba(66,52,34,0.5)';
      for(let x=0;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,H*.52); c.lineTo(x,H); c.stroke(); }
      for(let y=H*.52;y<H;y+=5*s){ c.beginPath(); c.moveTo(0,y); c.lineTo(W,y); c.stroke(); }
      c.strokeStyle='rgba(212,192,152,0.28)';
      for(let x=2*s;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,H*.52); c.lineTo(x,H); c.stroke(); }
      c.restore();
      // borders + gold divider trim
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.moveTo(m,H*.52); c.lineTo(W-m,H*.52); c.strokeStyle=rgb(122,98,42); c.lineWidth=2.5*s; c.stroke();
      // status LED (top centre)
      ledDot(d,W*.50,H*.055,true,150,196,255);
      // knob labels (RS names)
      textSpaced(d,.255*W,.080*H,F.barlow,9,wt,'SENS',0.4);
      textSpaced(d,.745*W,.080*H,F.barlow,9,wt,'ATTACK',0.4);
      textSpaced(d,.255*W,.280*H,F.barlow,9,wt,'RELEASE',0.3);
      textSpaced(d,.745*W,.280*H,F.barlow,9,wt,'MIX',0.4);
      // 3-way mode toggle + printed mode legends (centre, between top knobs)
      textC(d,.455*W,.145*H,F.barlow,5,wt,'DYNAMIC RATE','right');
      textC(d,.455*W,.178*H,F.barlow,5,wt,'DYNAMIC DEPTH','right');
      textC(d,.455*W,.211*H,F.barlow,5,wt,'HARMONIC TREM','right');
      const tx=.585*W, ty=.178*H, tw=8*s, th=21*s;
      rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.fillStyle=rgb(16,16,18); c.fill();
      rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=s; c.stroke();
      const lvy=ty-th*0.15;
      const lg=c.createLinearGradient(tx-4*s,lvy-5*s,tx+4*s,lvy+5*s); lg.addColorStop(0,rgb(228,230,236)); lg.addColorStop(1,rgb(150,153,160));
      rr(c,tx-3.5*s,lvy-5.5*s,7*s,11*s,2*s); c.fillStyle=lg; c.fill();
      rr(c,tx-3.5*s,lvy-5.5*s,7*s,11*s,2*s); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.8*s; c.stroke();
      // 'REVERB' printed label (lower-left of red panel)
      textSpaced(d,.255*W,.460*H,F.barlow,7,cream,'REVERB',0.4);
      // brand script + gold DYNA-TREM plaque on the tweed
      textC(d,.50*W,.595*H,F.ink,22,cream,'Peeley');
      const px=W*.18, py=H*.625, pw=W*.64, ph=H*.062;
      const gg=c.createLinearGradient(0,py,0,py+ph); gg.addColorStop(0,rgb(216,180,98)); gg.addColorStop(.5,rgb(178,140,60)); gg.addColorStop(1,rgb(150,114,46));
      rr(c,px,py,pw,ph,6*s); c.fillStyle=gg; c.fill();
      rr(c,px,py,pw,ph,6*s); c.strokeStyle=rgb(96,72,28); c.lineWidth=1.6*s; c.stroke();
      textSpaced(d,W*.50,py+ph*0.54,F.anton,20,rgb(42,28,10),'DYNA-TREM',1.0);
      // chrome footswitch
      footRound(d,W*.50,H*.82,19*s); } };

  // Amp Vibe — MXR Uni-Vibe-style: grey hammertone body, big black face panel
  // with two glossy black knobs, a Vibe LED + mini toggle, the brand box logo,
  // a script wordmark, side jack legends, status LED + chrome stomp. Parody
  // (NYR / Multi-Vibe). RS knob names. Speed0 Mix1.
  P.multivibe = { w:280,h:470, knobs:[
      {id:0,cx:.29,cy:.29,r:.085,style:'davies'},
      {id:1,cx:.71,cy:.29,r:.085,style:'davies'}],
    ptr:rgb(244,244,246),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(238,240,242);
      // grey hammertone enclosure
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(170,172,176)); bg.addColorStop(1,rgb(130,132,136));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // black face panel with thin white border
      const px=W*.085, py=H*.085, pw=W*.83, ph=H*.80;
      rr(c,px,py,pw,ph,8*s); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,px,py,pw,ph,8*s); c.strokeStyle=rgb(224,226,230); c.lineWidth=1.6*s; c.stroke();
      // Vibe LED + label + mini toggle (top-left)
      ledDot(d,W*.175,H*.150,true,234,238,242);
      textSpaced(d,W*.265,H*.150,F.barlow,7,wt,'VIBE',0.3);
      const vt=W*.355, vy=H*.150, vw=7*s, vh=17*s;
      rr(c,vt-vw/2,vy-vh/2,vw,vh,2.5*s); c.fillStyle=rgb(150,153,160); c.fill();
      rr(c,vt-vw/2,vy-vh/2,vw,vh,2.5*s); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.8*s; c.stroke();
      c.beginPath(); c.arc(vt,vy-vh*0.22,2.4*s,0,7); c.fillStyle=rgb(232,234,238); c.fill();
      // knob labels (RS names)
      textSpaced(d,.29*W,.435*H,F.barlow,9.5,wt,'SPEED',0.5);
      textSpaced(d,.71*W,.435*H,F.barlow,9.5,wt,'MIX',0.5);
      // brand box logo (NYR) — white-outlined rounded rect + bold letters
      const lx=W*.345, ly=H*.505, lw=W*.31, lh=H*.072;
      rr(c,lx,ly,lw,lh,4*s); c.strokeStyle=wt; c.lineWidth=2.6*s; c.stroke();
      textC(d,W*.50,ly+lh*0.55,F.anton,30,wt,'NYR');
      // side jack legends (rotated)
      c.save(); c.translate(W*.135,H*.50); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,7.5,wt,'OUTPUT',0.5); c.restore();
      c.save(); c.translate(W*.865,H*.50); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,7.5,wt,'INPUT',0.5); c.restore();
      // status LED + chrome footswitch
      ledDot(d,W*.50,H*.665,true,210,210,90);
      footRound(d,W*.50,H*.755,19*s);
      // script wordmark on the grey below the panel
      outlineText(d,W*.50,H*.945,F.ink,24,wt,rgb(40,40,44),'Multi-Vibe',0); } };

  // Auto Vibe — EarthQuaker Aqueduct-style: blue sparkle enclosure, cream top
  // stripe (arrows + 9V symbol), white Roman-aqueduct arcade silhouette, four
  // black/chrome knobs, script wordmark + brand. Parody (Eruption / Oceanduct).
  // RS knob names. Sens0 Attack1 Release2 Mix3.
  P.oceanduct = { w:280,h:480, knobs:[
      {id:0,cx:.28,cy:.265,r:.082,style:'moog'},
      {id:3,cx:.72,cy:.265,r:.082,style:'moog'},
      {id:1,cx:.28,cy:.470,r:.082,style:'moog'},
      {id:2,cx:.72,cy:.470,r:.082,style:'moog'}],
    tick:rgb(34,124,184), ptr:rgb(40,42,46),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(238,240,232), cream=rgb(234,236,228), tealD=rgb(34,116,168);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      c.save(); rr(c,m,m,W-2*m,H-2*m,14*s); c.clip();
      // blue sparkle base
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(38,142,198)); bg.addColorStop(1,rgb(20,92,152));
      c.fillStyle=bg; c.fillRect(0,0,W,H);
      // deterministic glitter speckle
      for(let i=0;i<340;i++){ const gx=m+((i*97)%97)/97*(W-2*m); const gy=m+((i*173)%131)/131*(H-2*m); const b=(i*53)%70;
        c.fillStyle='rgba('+(150+b)+','+(192+b%50)+','+(228+b%24)+',0.42)'; c.fillRect(gx,gy,1.3*s,1.3*s); }
      // cream top stripe
      c.fillStyle=cream; c.fillRect(m,m,W-2*m,H*.155-m);
      // white Roman-aqueduct arcade silhouette
      const x0=W*.13, span=W*.74, n=6, cw=span/n, r=cw*0.46, deckY=H*.61, springY=H*.665, footY=H*.735;
      c.strokeStyle='rgba(238,240,232,0.92)'; c.lineCap='round';
      c.lineWidth=3*s; c.beginPath(); c.moveTo(x0,deckY); c.lineTo(x0+span,deckY); c.stroke();
      c.lineWidth=2.4*s;
      for(let i=0;i<n;i++){ const cxA=x0+cw*(i+0.5);
        c.beginPath(); c.moveTo(cxA-r,footY); c.lineTo(cxA-r,springY); c.arc(cxA,springY,r,Math.PI,0); c.lineTo(cxA+r,footY); c.stroke(); }
      c.lineWidth=1.4*s;
      for(let i=0;i<=n;i++){ const dx=x0+cw*i; c.beginPath(); c.moveTo(dx,footY); c.lineTo(dx,footY+H*.035); c.stroke(); }
      c.restore();
      // border
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // top stripe icons: up arrow / 9V / down arrow
      c.fillStyle=tealD;
      c.beginPath(); c.moveTo(W*.40,H*.072); c.lineTo(W*.435,H*.118); c.lineTo(W*.365,H*.118); c.closePath(); c.fill();
      c.beginPath(); c.moveTo(W*.635,H*.118); c.lineTo(W*.60,H*.072); c.lineTo(W*.67,H*.072); c.closePath(); c.fill();
      c.strokeStyle=tealD; c.lineWidth=1.3*s; c.beginPath(); c.arc(W*.515,H*.072,4*s,0,7); c.stroke();
      textC(d,W*.515,H*.118,F.barlow,8,tealD,'9V');
      // knob labels (RS names)
      textSpaced(d,.28*W,.365*H,F.barlow,9.5,wt,'SENS',0.4);
      textSpaced(d,.72*W,.365*H,F.barlow,9.5,wt,'MIX',0.4);
      textSpaced(d,.28*W,.570*H,F.barlow,9.5,wt,'ATTACK',0.3);
      textSpaced(d,.72*W,.570*H,F.barlow,9.5,wt,'RELEASE',0.3);
      // status LED + chrome footswitch
      ledDot(d,W*.345,H*.82,true,210,210,90);
      footRound(d,W*.52,H*.82,18*s);
      // script wordmark + parody brand
      outlineText(d,W*.50,H*.895,F.ink,30,wt,rgb(20,72,120),'Oceanduct',0);
      textSpaced(d,W*.50,H*.945,F.barlow,8.5,wt,'Eruption Devices',0.3); } };

  // Super Vibe — Marshall SV-1 Supervibe-style: pale cream compact body, chrome
  // knob row over a blue label stripe, bubble-script logo, big round chrome
  // stomp + script brand. Parody (Marsten / UltraVibe). RS knob names.
  // Rate0 Depth1 Mix2 Wave3.
  P.uv1 = { w:300,h:360, knobs:[
      {id:0,cx:.155,cy:.165,r:.060,style:'knurled'},
      {id:1,cx:.385,cy:.165,r:.060,style:'knurled'},
      {id:2,cx:.615,cy:.165,r:.060,style:'knurled'},
      {id:3,cx:.845,cy:.165,r:.060,style:'knurled'}],
    ptr:rgb(30,30,32),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, blu=rgb(36,110,196), ink=rgb(36,36,40);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(233,230,220)); bg.addColorStop(1,rgb(205,202,192));
      rr(c,m,m,W-2*m,H-2*m,16*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,16*s); c.strokeStyle='rgba(0,0,0,0.35)'; c.lineWidth=2*s; c.stroke();
      // red LED top centre
      ledDot(d,W*.50,H*.065,true,224,40,40);
      // blue label stripe + white knob labels (RS names)
      const sy=H*.275, sh=H*.055, w=rgb(240,242,246);
      rr(c,W*.055,sy,W*.89,sh,5*s); c.fillStyle=blu; c.fill();
      textSpaced(d,.155*W,sy+sh*0.5,F.barlow,8.5,w,'RATE',0.3);
      textSpaced(d,.385*W,sy+sh*0.5,F.barlow,8.5,w,'DEPTH',0.3);
      textSpaced(d,.615*W,sy+sh*0.5,F.barlow,8.5,w,'MIX',0.3);
      textSpaced(d,.845*W,sy+sh*0.5,F.barlow,8.5,w,'WAVE',0.3);
      // bubble-script logo + model code + chorus tag
      textSpaced(d,.295*W,.420*H,F.barlow,9,ink,'UV-1',0.3);
      outlineText(d,.52*W,.470*H,F.ink,36,w,blu,'UltraVibe',0);
      textSpaced(d,.68*W,.560*H,F.barlow,9,blu,'CHORUS',1.0);
      // big round chrome footswitch
      footRound(d,W*.50,H*.700,30*s);
      // script brand (parody)
      textC(d,.50*W,.915*H,F.ink,24,ink,'Marsten'); } };

  // Omni Mod — vintage Univox Uni-Vibe-style: wide brown-tolex box, black control
  // panel with white frame + corner screws, script logo, two scaled vintage knobs
  // flanking a red/white Chorus-Vibrato rocker, FUSE cap, red power jewel, and a
  // bottom row of jacks / DIN foot-control / power toggle. Parody (UniMod).
  // RS knob names. Rate0 Depth1 Mix2.
  P.unimod = { w:560,h:340, knobs:[
      {id:0,cx:.135,cy:.42,r:.050,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.465,cy:.42,r:.050,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.605,cy:.42,r:.050,style:'pointer',cap:[26,26,28]}],
    tick:rgb(228,224,214), ptr:rgb(232,228,218),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(234,232,224);
      // brown tolex box
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(86,76,66)); bg.addColorStop(1,rgb(60,52,46));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=2*s; c.stroke();
      // black control panel + white double frame + corner screws
      const px=W*.05, py=H*.05, pw=W*.90, ph=H*.84;
      rr(c,px,py,pw,ph,4*s); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,px,py,pw,ph,4*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1.5*s; c.stroke();
      rr(c,px+7*s,py+7*s,pw-14*s,ph-14*s,3*s); c.strokeStyle=rgb(206,204,196); c.lineWidth=1.6*s; c.stroke();
      screw(d,px+16*s,py+16*s); screw(d,px+pw-16*s,py+16*s); screw(d,px+16*s,py+ph-16*s); screw(d,px+pw-16*s,py+ph-16*s);
      // script logo
      textC(d,.45*W,.155*H,F.ink,40,wt,'UniMod');
      // knob scales + labels (RS names)
      const scale=(kx,nm)=>{ textSpaced(d,kx-W*.045,.535*H,F.barlow,5.5,wt,'MIN.',0.2); textSpaced(d,kx+W*.045,.535*H,F.barlow,5.5,wt,'MAX.',0.2);
        textSpaced(d,kx,.600*H,F.barlow,8,wt,nm,0.3); };
      scale(.135*W,'RATE'); scale(.465*W,'DEPTH');
      textSpaced(d,.605*W,.600*H,F.barlow,8,wt,'MIX',0.3);
      // Chorus/Vibrato red-white rocker
      const rx=.295*W, ry=.40*H, rw=30*s, rh=20*s;
      rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); c.fillStyle=rgb(40,40,42); c.fill();
      rr(c,rx-rw/2+2*s,ry-rh/2+2*s,rw/2-3*s,rh-4*s,2*s); c.fillStyle=rgb(198,42,42); c.fill();
      rr(c,rx+1*s,ry-rh/2+2*s,rw/2-3*s,rh-4*s,2*s); c.fillStyle=rgb(226,224,218); c.fill();
      rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); c.strokeStyle=rgb(16,16,18); c.lineWidth=1.2*s; c.stroke();
      textSpaced(d,.252*W,.55*H,F.barlow,6.5,wt,'CHORUS',0.2); textSpaced(d,.345*W,.55*H,F.barlow,6.5,wt,'VIBRATO',0.2);
      // FUSE cap
      c.beginPath(); c.arc(.735*W,.39*H,13*s,0,7); c.fillStyle=rgb(28,28,30); c.fill(); c.strokeStyle=rgb(64,66,70); c.lineWidth=1.4*s; c.stroke();
      textC(d,.735*W,.39*H,F.barlow,5.5,wt,'FUSE');
      // red power jewel
      const jx=.865*W, jy=.385*H, jr=13*s;
      c.beginPath(); c.arc(jx,jy,jr,0,7); c.fillStyle=rgb(150,152,158); c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=1.6*s; c.stroke();
      const jg=c.createRadialGradient(jx-jr*0.3,jy-jr*0.3,jr*0.1,jx,jy,jr*0.7); jg.addColorStop(0,rgb(255,130,118)); jg.addColorStop(1,rgb(150,12,16));
      c.beginPath(); c.arc(jx,jy,jr*0.62,0,7); c.fillStyle=jg; c.fill();
      // bottom row jacks
      const jack=(jx2,jy2)=>{ const R=11*s; const g2=c.createRadialGradient(jx2-R*0.3,jy2-R*0.3,R*0.1,jx2,jy2,R); g2.addColorStop(0,rgb(212,214,218)); g2.addColorStop(1,rgb(120,122,128));
        c.beginPath(); c.arc(jx2,jy2,R,0,7); c.fillStyle=g2; c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=1.4*s; c.stroke();
        c.beginPath(); c.arc(jx2,jy2,R*0.42,0,7); c.fillStyle=rgb(18,18,20); c.fill(); };
      jack(.115*W,.72*H); jack(.215*W,.72*H); jack(.39*W,.72*H);
      textSpaced(d,.115*W,.80*H,F.barlow,6.5,wt,'1',0.2); textSpaced(d,.215*W,.80*H,F.barlow,6.5,wt,'2',0.2);
      textSpaced(d,.165*W,.85*H,F.barlow,7,wt,'INSTRUMENTS',0.2);
      textSpaced(d,.39*W,.835*H,F.barlow,7,wt,'OUTPUT',0.2);
      // DIN foot control
      const dx=.565*W, dy=.72*H, dR=14*s;
      c.beginPath(); c.arc(dx,dy,dR,0,7); c.fillStyle=rgb(58,60,64); c.fill(); c.strokeStyle=rgb(150,152,158); c.lineWidth=1.6*s; c.stroke();
      for(let i=0;i<5;i++){ const aa=Math.PI*0.80 + i*Math.PI*0.35; c.beginPath(); c.arc(dx+dR*0.5*Math.cos(aa),dy+dR*0.5*Math.sin(aa),1.6*s,0,7); c.fillStyle=rgb(20,20,22); c.fill(); }
      textSpaced(d,.565*W,.835*H,F.barlow,7,wt,'FOOT CONTROL',0.2);
      // power toggle (up)
      const tx=.85*W, ty=.71*H;
      rr(c,tx-5*s,ty-3*s,10*s,16*s,3*s); c.fillStyle=rgb(40,40,42); c.fill();
      const lg2=c.createLinearGradient(tx-4*s,ty-18*s,tx+4*s,ty); lg2.addColorStop(0,rgb(228,230,236)); lg2.addColorStop(1,rgb(140,143,150));
      rr(c,tx-3.5*s,ty-18*s,7*s,17*s,3*s); c.fillStyle=lg2; c.fill();
      rr(c,tx-3.5*s,ty-18*s,7*s,17*s,3*s); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.8*s; c.stroke();
      textSpaced(d,.85*W,.625*H,F.barlow,6.5,wt,'ON',0.2);
      textSpaced(d,.85*W,.835*H,F.barlow,7,wt,'POWER SW.',0.2); } };

  // Valve Echo — Catalinbread Echorec-style: gold metallic body, black knob
  // panel, outline ECHOREC wordmark, column + concentric-arc line-art, chrome
  // stomp + script brand. Parody (Venson). RS knob names. Time0 Feedback1 Mix2.
  P.valveecho = { w:280,h:480, knobs:[
      {id:0,cx:.22,cy:.235,r:.072,style:'davies'},
      {id:1,cx:.50,cy:.235,r:.072,style:'davies'},
      {id:2,cx:.78,cy:.235,r:.072,style:'davies'}],
    ptr:rgb(240,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, goldD=rgb(150,112,28), wt=rgb(240,238,232);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(212,166,58)); bg.addColorStop(1,rgb(176,132,34));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // decorative line-art: columns (left) + concentric arcs (right)
      c.strokeStyle=goldD; c.lineWidth=1.4*s;
      for(let i=0;i<7;i++){ const lx=W*(.105+i*.028); c.beginPath(); c.moveTo(lx,H*.44); c.lineTo(lx,H*.85); c.stroke(); }
      for(let i=1;i<=7;i++){ c.beginPath(); c.arc(W*.80,H*.70,W*.05*i,Math.PI*0.70,Math.PI*1.35); c.stroke(); }
      // black knob panel
      rr(c,W*.07,H*.075,W*.86,H*.31,10*s); c.fillStyle=rgb(22,22,24); c.fill();
      rr(c,W*.07,H*.075,W*.86,H*.31,10*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=1.4*s; c.stroke();
      // knob labels (RS names)
      textSpaced(d,.22*W,.125*H,F.barlow,8.5,wt,'TIME',0.3);
      textSpaced(d,.50*W,.125*H,F.barlow,7.5,wt,'FEEDBACK',0.2);
      textSpaced(d,.78*W,.125*H,F.barlow,8.5,wt,'MIX',0.3);
      // ECHOREC wordmark on a black band
      rr(c,W*.10,H*.475,W*.80,H*.085,6*s); c.fillStyle=rgb(20,20,22); c.fill();
      outlineText(d,.5*W,.518*H,F.anton,40,wt,rgb(20,20,22),'ECHOREC',1);
      // footswitch
      footRound(d,W*.50,H*.685,20*s);
      // power symbol + script brand + tagline
      textSpaced(d,.205*W,.785*H,F.barlow,6.5,goldD,'9V-18V DC',0.2);
      textC(d,.50*W,.885*H,F.ink,26,rgb(28,22,10),'Venson');
      textSpaced(d,.50*W,.935*H,F.barlow,7,rgb(40,30,12),'MECHANISMS OF MUSIC',0.4); } };

  // Oil Can Echo — Tel-Ray AD-N-ECHO-style: vintage amp-head box, black tolex,
  // black top panel with rotary knobs + bat toggles, faded-teal lower panel with
  // jacks, badge + script + red jewel. Parody (Cel-Ray / Oil-Can Echo). RS knob
  // names on the three rotaries. Time0 Feedback1 Mix2.
  P.oilcanecho = { w:600,h:330, knobs:[
      {id:0,cx:.12,cy:.37,r:.050,style:'pointer',cap:[28,28,30]},
      {id:1,cx:.50,cy:.37,r:.044,style:'knurled'},
      {id:2,cx:.88,cy:.37,r:.050,style:'pointer',cap:[28,28,30]}],
    tick:rgb(96,96,100), ptr:rgb(232,232,226),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(228,228,222), brass=rgb(176,142,72);
      // black tolex box
      c.fillStyle=rgb(6,6,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(34,32,30)); bg.addColorStop(1,rgb(20,19,18));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2*s; c.stroke();
      // brass corner protectors
      c.fillStyle=brass; const cz=20*s;
      [[m,m,1,1],[W-m,m,-1,1],[m,H-m,1,-1],[W-m,H-m,-1,-1]].forEach(([qx,qy,sx,sy])=>{
        c.beginPath(); c.moveTo(qx,qy); c.lineTo(qx+sx*cz,qy); c.lineTo(qx,qy+sy*cz); c.closePath(); c.fill(); });
      // black top control panel
      rr(c,W*.035,H*.07,W*.93,H*.50,6*s); c.fillStyle=rgb(16,16,17); c.fill();
      rr(c,W*.035,H*.07,W*.93,H*.50,6*s); c.strokeStyle=rgb(4,4,5); c.lineWidth=1.4*s; c.stroke();
      // sweep arrows above the rotaries + RS knob labels
      const sweep=(kx)=>{ c.beginPath(); c.arc(kx,H*.37,W*.045,Math.PI*1.18,Math.PI*1.82); c.strokeStyle=wt; c.lineWidth=1.1*s; c.stroke(); };
      sweep(.12*W); sweep(.50*W); sweep(.88*W);
      textSpaced(d,.12*W,.165*H,F.barlow,8.5,wt,'TIME',0.3);
      textSpaced(d,.50*W,.165*H,F.barlow,8,wt,'FEEDBACK',0.2);
      textSpaced(d,.88*W,.165*H,F.barlow,8.5,wt,'MIX',0.3);
      // decorative bat toggles
      const tog=(tx,up)=>{ const tw=11*s, th=20*s, ty=H*.37;
        rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.fillStyle=rgb(26,26,28); c.fill();
        rr(c,tx-tw/2,ty-th/2,tw,th,3*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=0.9*s; c.stroke();
        const ly=ty+(up?-1:1)*th*0.20; const lg=c.createLinearGradient(tx-4*s,ly-5*s,tx+4*s,ly+5*s);
        lg.addColorStop(0,rgb(40,40,42)); lg.addColorStop(1,rgb(14,14,16));
        rr(c,tx-4*s,ly-6*s,8*s,12*s,2*s); c.fillStyle=lg; c.fill(); };
      tog(.26*W,true); tog(.38*W,false); tog(.62*W,true); tog(.73*W,false);
      const dim=rgb(176,176,172);
      textSpaced(d,.26*W,.205*H,F.barlow,6.5,dim,'ECHO',0.2); textSpaced(d,.26*W,.535*H,F.barlow,6.5,dim,'REVERB',0.2);
      textSpaced(d,.38*W,.205*H,F.barlow,6.5,dim,'BRIGHT',0.2); textSpaced(d,.38*W,.535*H,F.barlow,6.5,dim,'NORMAL',0.2);
      textSpaced(d,.675*W,.115*H,F.barlow,6.5,dim,'DELAY',0.2);
      textSpaced(d,.62*W,.205*H,F.barlow,6.5,dim,'LONG',0.2); textSpaced(d,.73*W,.205*H,F.barlow,6.5,dim,'SHORT',0.2);
      textSpaced(d,.675*W,.535*H,F.barlow,6.5,dim,'DELAY OFF',0.2);
      // faded-teal lower panel
      const ty0=H*.62, ty1=H*.91; const tg=c.createLinearGradient(0,ty0,0,ty1); tg.addColorStop(0,rgb(156,210,202)); tg.addColorStop(1,rgb(120,184,176));
      rr(c,W*.035,ty0,W*.93,ty1-ty0,5*s); c.fillStyle=tg; c.fill();
      rr(c,W*.035,ty0,W*.93,ty1-ty0,5*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=1.2*s; c.stroke();
      const dk=rgb(26,40,38);
      // jacks + labels
      const jack=(jx)=>{ const jy=H*.715, R=9*s; const g2=c.createRadialGradient(jx-R*0.3,jy-R*0.3,R*0.1,jx,jy,R);
        g2.addColorStop(0,rgb(206,208,212)); g2.addColorStop(1,rgb(116,118,124));
        c.beginPath(); c.arc(jx,jy,R,0,7); c.fillStyle=g2; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=1.2*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,R*0.42,0,7); c.fillStyle=rgb(20,20,22); c.fill(); };
      jack(.095*W); jack(.225*W); jack(.355*W); jack(.485*W);
      textSpaced(d,.095*W,.655*H,F.barlow,6,dk,'MICROPHONE',0.1);
      textSpaced(d,.225*W,.655*H,F.barlow,6,dk,'INSTRUMENT',0.1);
      textSpaced(d,.355*W,.655*H,F.barlow,6,dk,'AMPLIFIER',0.1);
      textSpaced(d,.485*W,.655*H,F.barlow,6,dk,'REMOTE SWITCH',0.1);
      // model + red jewel
      textSpaced(d,.86*W,.66*H,F.barlow,6.5,dk,'MODEL 2001A',0.2);
      const jx2=.93*W, jy2=.715*H; c.beginPath(); c.arc(jx2,jy2,7*s,0,7); c.fillStyle=rgb(120,122,128); c.fill();
      c.beginPath(); c.arc(jx2,jy2,4.5*s,0,7); c.fillStyle=rgb(196,40,40); c.fill();
      // OIL·CAN·ECHO badge + 'Electronic Sound Chamber' script + brand
      const bx=W*.30, by=H*.815, bw=W*.24, bh=H*.075;
      rr(c,bx,by,bw,bh,4*s); c.fillStyle=rgb(18,18,20); c.fill();
      textSpaced(d,bx+bw/2,by+bh*0.54,F.anton,14,wt,'OIL·CAN·ECHO',0.5);
      textC(d,.745*W,.855*H,F.ink,20,dk,'Electronic Sound Chamber');
      textSpaced(d,.135*W,.875*H,F.barlow,5.5,dk,'Cel-Ray Electronics Mfg. Co. Inc.',0.1); } };

  // Cosmic Echo — Tom's Line/Mooer 'Cosmic'-style mini echo: black body, blue
  // atom/galaxy graphic, three blue knobs, outline wordmark + side jack legends.
  // Parody (RocketSynth / Space Echo). RS knob names. Time0 Feedback1 Mix2.
  P.galaxyecho = { w:260,h:420, knobs:[
      {id:0,cx:.22,cy:.46,r:.072,style:'pointer',cap:[54,150,212]},
      {id:1,cx:.50,cy:.46,r:.072,style:'pointer',cap:[54,150,212]},
      {id:2,cx:.78,cy:.46,r:.072,style:'pointer',cap:[54,150,212]}],
    tick:rgb(70,80,92), ptr:rgb(240,242,246),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, wt=rgb(238,240,244), blu=rgb(70,170,224);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(30,30,34)); bg.addColorStop(1,rgb(18,18,20));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.45)'; c.lineWidth=2*s; c.stroke();
      // atom / galaxy graphic
      const ax=.50*W, ay=.16*H, R=W*.10;
      const orbit=(rot)=>{ c.save(); c.translate(ax,ay); c.rotate(rot); c.scale(1,0.42); c.beginPath(); c.arc(0,0,R,0,7); c.strokeStyle=blu; c.lineWidth=2.4*s; c.stroke(); c.restore(); };
      orbit(0); orbit(Math.PI/3); orbit(-Math.PI/3);
      c.beginPath(); c.arc(ax,ay,R*0.34,0,7); c.fillStyle=blu; c.fill();
      [[.30,.10],[.70,.09],[.67,.24],[.33,.25],[.50,.045]].forEach(p=>{ c.beginPath(); c.arc(p[0]*W,p[1]*H,1.6*s,0,7); c.fillStyle=wt; c.fill(); });
      // OUT / IN side legends
      textSpaced(d,.15*W,.305*H,F.barlow,7,wt,'◄ OUT',0.2);
      textSpaced(d,.85*W,.305*H,F.barlow,7,wt,'IN ►',0.2);
      // knob labels (RS names)
      textSpaced(d,.22*W,.585*H,F.barlow,9,wt,'TIME',0.3);
      textSpaced(d,.50*W,.585*H,F.barlow,8,wt,'FEEDBACK',0.2);
      textSpaced(d,.78*W,.585*H,F.barlow,9,wt,'MIX',0.3);
      // white design band
      c.fillStyle=rgb(232,234,238); c.fillRect(W*.05,H*.635,W*.90,H*.016);
      // wordmark + brand + tagline
      textC(d,.50*W,.715*H,F.barlow,11,blu,'Galaxy');
      outlineText(d,.50*W,.790*H,F.anton,46,wt,blu,'ECHO',2);
      textSpaced(d,.50*W,.865*H,F.barlow,8,wt,'ROCKETSYNTH',0.6);
      textSpaced(d,.50*W,.910*H,F.barlow,6.5,rgb(150,182,206),'LO-FI GALAXY REPEATER',0.3); } };

  // Mod Delay — Ibanez DL10 (10-series)-style: blue body, light-blue top panel
  // with mode LEDs + five small black knobs, 'DELAY DL9 digital' branding, big
  // black ribbed treadle with embossed Ibañez wordmark. Parody. RS knob names.
  // Time0 Feedback1 Mix2 Rate3 Depth4.
  P.dl9 = { w:280,h:460, knobs:[
      {id:0,cx:.14,cy:.205,r:.055,style:'boss'},
      {id:1,cx:.32,cy:.205,r:.055,style:'boss'},
      {id:2,cx:.50,cy:.205,r:.055,style:'boss'},
      {id:3,cx:.68,cy:.205,r:.055,style:'boss'},
      {id:4,cx:.86,cy:.205,r:.055,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, wt=rgb(236,240,246), dk=rgb(26,40,58);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(52,130,206)); bg.addColorStop(1,rgb(34,104,178));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // light-blue top control panel
      rr(c,W*.05,H*.04,W*.90,H*.355,8*s); c.fillStyle=rgb(120,178,224); c.fill();
      rr(c,W*.05,H*.04,W*.90,H*.355,8*s); c.strokeStyle='rgba(0,0,0,0.25)'; c.lineWidth=1.2*s; c.stroke();
      // mode LEDs + labels
      [['A·INT',.175],['ADD·DLY',.39],['FBC',.60],['FB',.78]].forEach(p=>{ ledDot(d,p[1]*W,H*.078,true,224,60,52); textSpaced(d,p[1]*W,H*.118,F.barlow,5.5,dk,p[0],0.1); });
      // knob labels (RS names)
      [['TIME',.14],['FEEDBACK',.32],['MIX',.50],['RATE',.68],['DEPTH',.86]].forEach(p=> textSpaced(d,p[1]*W,.295*H,F.barlow,6,dk,p[0],0.05));
      // DELAY / DL9 / digital branding at the seam
      textSpaced(d,.40*W,.440*H,F.bebas,30,wt,'DELAY',1.0);
      textSpaced(d,.73*W,.450*H,F.barlow,12,wt,'DL9',0.5);
      textSpaced(d,.205*W,.480*H,F.barlow,8,wt,'digital',0.3);
      // black ribbed footswitch treadle
      const tx=m+5*s, tyTop=H*.51, tw=W-2*m-10*s, tBot=H-m-6*s;
      const tg=c.createLinearGradient(0,tyTop,0,tBot); tg.addColorStop(0,rgb(40,40,42)); tg.addColorStop(1,rgb(18,18,20));
      rr(c,tx,tyTop,tw,tBot-tyTop,11*s); c.fillStyle=tg; c.fill();
      c.save(); rr(c,tx,tyTop,tw,tBot-tyTop,11*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.05)'; c.lineWidth=2*s;
      for(let x=0;x<tw*2;x+=14*s){ c.beginPath(); c.moveTo(tx+x,tyTop); c.lineTo(tx+x-(tBot-tyTop),tBot); c.stroke(); } c.restore();
      rr(c,tx,tyTop,tw,tBot-tyTop,11*s); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=1.6*s; c.stroke();
      const sd=(x,y)=>{ c.save(); c.translate(x,y); c.rotate(0.6); rr(c,-5*s,-1.5*s,10*s,3*s,1.5*s); c.fillStyle='rgba(255,255,255,0.16)'; c.fill(); c.restore(); };
      sd(tx+20*s,tyTop+20*s); sd(tx+tw-20*s,tyTop+20*s); sd(tx+20*s,tBot-18*s); sd(tx+tw-20*s,tBot-18*s);
      outlineText(d,W*.5,tyTop+(tBot-tyTop)*0.60,F.crete,40,rgb(150,182,214),rgb(18,58,98),'Ibañez',1); } };

  // Bass Filter Echo — Boss RE-2 Space Echo: the chief (Boss) template recoloured
  // (black body + GREEN knob plate), 'Space'/'Echo' + RE-3. RS: Time/Feedback/Mix/Filter.
  P.se3 = chiefSpec(300,480,[26,26,30],
    [{id:0,cx:.205,lbl:'TIME'},{id:1,cx:.40,lbl:'FEEDBACK',lblPx:7.5},{id:2,cx:.595,lbl:'MIX'},{id:3,cx:.79,lbl:'FILTER',lblPx:8}],
    'Galaxy','Echo','SE-3',[70,126,68]);
  P.enbiggen = boxSpec(320,470,[58,64,72],
    [{id:0,cx:.20,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.60,lbl:'MIX'},{id:3,cx:.80,lbl:'FILTER'}],
    'ENBIGGEN','MOD  FILTER',[110,210,224]);
  // Bass MultiComp — EBS MultiComp (Blue Label): BLACK body with blue accent
  // lines across the bottom; stylised 'MultiComp' logo (big C…P flanking a
  // stacked MULTI/OM) under the knobs, EBX above the footswitch, blue lines
  // running behind EBX + footswitch. RS params (3 knobs): Compress0 Filter1 Rate2.
  P.multicomp = { w:300,h:470,
    knobs:[
      {id:0,cx:.22,cy:.255,r:.088,style:'boss'},  // COMPRESS
      {id:1,cx:.50,cy:.255,r:.088,style:'boss'},  // FILTER
      {id:2,cx:.78,cy:.255,r:.088,style:'boss'}], // RATE
    ptr:rgb(236,238,242),
    draw(d){ const {ctx:c,W,H}=d; const w=rgb(236,240,248), m=8;
      // black body, NO screws
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(34,34,38)); bg.addColorStop(1,rgb(14,14,16));
      rr(c,m,m,W-2*m,H-2*m,14); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14); c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=2; c.stroke();
      const R=.088*W+12;
      textC(d,.22*W,.255*H+R,F.barlow,11,w,'COMPRESS');
      textC(d,.50*W,.255*H+R,F.barlow,11,w,'FILTER');
      textC(d,.78*W,.255*H+R,F.barlow,11,w,'RATE');
      // stylised 'MultiComp': letters stretched WIDE (less tall) — C … P with
      // MULTI (up top) + OM filling the gap between C and P
      textWide(d,.50*W,.445*H,F.barlow,20,w,'MULTI',1.3);
      textWide(d,.22*W,.520*H,F.anton,70,w,'C',1.5);
      textWide(d,.50*W,.545*H,F.anton,56,w,'OM',1.45);
      textWide(d,.78*W,.520*H,F.anton,70,w,'P',1.5);
      // blue accent lines at the bottom — thick + short (behind EBX + footswitch)
      c.strokeStyle='rgba(46,124,228,0.92)'; c.lineWidth=8;
      for(let i=0;i<7;i++){ const y=(.63+i*0.05)*H; c.beginPath(); c.moveTo(W*0.18,y); c.lineTo(W*0.82,y); c.stroke(); }
      textC(d,.50*W,.75*H,F.ink,30,w,'EBX');     // ink/marker-style brand logo
      footRound(d,W*0.5,H*0.86,22); } };

  // Dyna Compress — Dyna Comp-style optical compressor. MXR-inspired look
  // (red box + cursive logo) recreated, not branded. Param order: Comp0 Attack1 Release2.
  // Dyna Comp — MXR Dyna Comp-style: vivid-red box, black knobs, the parody
  // 'NYR' logo box, vertical jack legends, red LED, footswitch, 'dyna comp'
  // tag. RS knob names (RS exposes 3, the real pedal has 2). Comp0 Attack1 Release2.
  P.dynacomp = { w:300, h:460, knobs:[
      {id:0,cx:.22,cy:.165,r:.078,style:'davies'},
      {id:1,cx:.50,cy:.165,r:.078,style:'davies'},
      {id:2,cx:.78,cy:.165,r:.078,style:'davies'}],
    tick:rgb(60,16,14), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const ink=rgb(24,20,18), m=7*s;
      // vivid red enclosure (no face screws)
      c.fillStyle=rgb(10,8,8); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(214,46,42)); g.addColorStop(1,rgb(186,28,26));
      rr(c,m,m,W-2*m,H-2*m,13*s); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,13*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // knob labels (RS names)
      textSpaced(d,.22*W,.275*H,F.barlow,9.5,ink,'COMP',0.4);
      textSpaced(d,.50*W,.275*H,F.barlow,9,ink,'ATTACK',0.4);
      textSpaced(d,.78*W,.275*H,F.barlow,8.5,ink,'RELEASE',0.3);
      // vertical jack legends
      c.save(); c.translate(W*.085,H*.405); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,8.5,ink,'OUTPUT',0.4); c.restore();
      c.save(); c.translate(W*.915,H*.405); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,8.5,ink,'INPUT',0.4); c.restore();
      // NYR logo box
      rr(c,W*.30,H*.385,W*.40,H*.085,7*s); c.strokeStyle=ink; c.lineWidth=2.4*s; c.stroke();
      textC(d,W*.50,H*.385+H*.085*0.54,F.anton,30,ink,'NYR');
      // red LED
      ledDot(d,W*.50,H*.555,true,224,52,46);
      // footswitch + 'dyna comp' tag
      footRound(d,W*.50,H*.700,22*s);
      textC(d,W*.50,H*.875,F.barlow,28,ink,'dyna comp'); } };

  // Holy Spring — Holy Grail-style spring reverb. Bright chrome box + ornate
  // serif logo (EHX-inspired, recreated brand-free). Params: Time0 Mix1 Depth2.
  P.holyspring = { w:300, h:470, knobs:[
      {id:0,cx:.215,cy:.155,r:.078,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.500,cy:.155,r:.078,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.785,cy:.155,r:.078,style:'pointer',cap:[26,26,28]}],
    tick:rgb(120,122,128), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,196,199,205);
      const blue=rgb(38,150,216), dk=rgb(40,42,48);
      // brushed-metal: vertical sheen + faint horizontal brush lines up top
      c.save(); rr(c,10*s,10*s,W-20*s,H-20*s,12*s); c.clip();
      const sheen=c.createLinearGradient(0,0,W,0);
      sheen.addColorStop(0,rgb(255,255,255,0)); sheen.addColorStop(.5,rgb(255,255,255,0.22)); sheen.addColorStop(1,rgb(255,255,255,0));
      c.fillStyle=sheen; c.fillRect(0,0,W,H);
      c.strokeStyle=rgb(150,153,160,0.16); c.lineWidth=1;
      for(let y=H*.04;y<H*.30;y+=3*s){ c.beginPath(); c.moveTo(14*s,y); c.lineTo(W-14*s,y); c.stroke(); }
      c.restore();
      // top-edge I/O labels (parody of the EHX jack legends)
      textSpaced(d,.20*W,.050*H,F.barlow,7.5,dk,'OUTPUT',0.4);
      textSpaced(d,.50*W,.050*H,F.barlow,7.5,dk,'9V 500mA',0.4);
      textSpaced(d,.80*W,.050*H,F.barlow,7.5,dk,'INPUT',0.4);
      // knob labels
      textSpaced(d,.215*W,.272*H,F.barlow,9,dk,'TIME',0.4);
      textSpaced(d,.500*W,.272*H,F.barlow,9,dk,'MIX',0.4);
      textSpaced(d,.785*W,.272*H,F.barlow,8.5,dk,'DEPTH',0.3);
      // blue swoosh across the upper-middle
      c.save(); c.lineCap='round';
      c.beginPath(); c.moveTo(W*.10,H*.340); c.quadraticCurveTo(W*.46,H*.300,W*.92,H*.352);
      c.strokeStyle=blue; c.lineWidth=5.5*s; c.stroke();
      c.beginPath(); c.moveTo(W*.12,H*.356); c.quadraticCurveTo(W*.46,H*.318,W*.90,H*.368);
      c.strokeStyle=rgb(38,150,216,0.4); c.lineWidth=2*s; c.stroke();
      c.restore();
      // status LED + label (right side, under the swoosh)
      ledDot(d,W*.815,H*.405,true,224,60,50);
      textSpaced(d,.815*W,.445*H,F.barlow,6.5,dk,'STATUS',0.3);
      // bold slanted blue wordmark (light outline) — HOLY / SPRING
      const word=(y,str,px)=>{ c.save(); c.translate(W*.5,H*y); c.transform(1,0,-0.16,1,0,0);
        outlineText(d,0,0,F.anton,px,blue,rgb(246,249,252),str,2); c.restore(); };
      word(.450,'HOLY',54); word(.560,'SPRING',54);
      // 'reverb' wordmark + footswitch
      textSpaced(d,.5*W,.650*H,F.bebas,26,blue,'REVERB',2);
      footRound(d,W*.5,H*.760,24*s);
      // brand (parody, like the rest) + MADE IN USA tag
      textC(d,.50*W,.905*H,F.crete,15,dk,'quimical-harmony');
      textSpaced(d,.50*W,.950*H,F.barlow,7,dk,'MADE IN USA',0.4); } };

  // Plate Verb — Catalinbread Talisman-style: cool-white box, red occult/talisman
  // graphic (all-seeing eye + symmetric wings + stars), four black knobs, script
  // brand + tagline. Parody (Venson / Voodoo Plate Reverb). RS knob names.
  // Time0 Depth1 Mix2 Voice3.
  P.voodoo = { w:280,h:480, knobs:[
      {id:0,cx:.27,cy:.150,r:.070,style:'davies'},
      {id:1,cx:.73,cy:.150,r:.070,style:'davies'},
      {id:2,cx:.27,cy:.310,r:.070,style:'davies'},
      {id:3,cx:.73,cy:.310,r:.070,style:'davies'}],
    ptr:rgb(180,182,188),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, red=rgb(210,38,44);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(232,233,236)); bg.addColorStop(1,rgb(208,210,214));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=2*s; c.stroke();
      // knob labels (RS names, red)
      textSpaced(d,.27*W,.072*H,F.barlow,8.5,red,'TIME',0.3);
      textSpaced(d,.73*W,.072*H,F.barlow,8.5,red,'DEPTH',0.3);
      textSpaced(d,.27*W,.232*H,F.barlow,8.5,red,'MIX',0.3);
      textSpaced(d,.73*W,.232*H,F.barlow,8.5,red,'VOICE',0.3);
      // all-seeing-eye triangle (top centre)
      const ex=.50*W, ey=.135*H, tr=W*.05;
      c.beginPath(); c.moveTo(ex,ey-tr); c.lineTo(ex-tr*0.92,ey+tr*0.72); c.lineTo(ex+tr*0.92,ey+tr*0.72); c.closePath();
      c.strokeStyle=red; c.lineWidth=1.8*s; c.stroke();
      c.beginPath(); c.arc(ex,ey+tr*0.12,tr*0.2,0,7); c.fillStyle=red; c.fill();
      // red occult talisman graphic: centre axis + symmetric wings + stars
      c.strokeStyle=red; c.lineWidth=1.8*s; c.lineCap='round';
      c.beginPath(); c.moveTo(ex,.40*H); c.lineTo(ex,.86*H); c.stroke();
      const wing=(sx)=>{ c.beginPath(); c.moveTo(ex,.42*H); c.lineTo(ex+sx*W*.30,.60*H);
        c.moveTo(ex,.50*H); c.lineTo(ex+sx*W*.24,.70*H); c.stroke(); };
      wing(1); wing(-1);
      const star=(sx,sy,r0)=>{ c.beginPath(); for(let i=0;i<10;i++){ const rr2=(i%2?r0*0.42:r0), a=-Math.PI/2+i*Math.PI/5;
        const x=sx+rr2*Math.cos(a), y=sy+rr2*Math.sin(a); i?c.lineTo(x,y):c.moveTo(x,y);} c.closePath(); c.fillStyle=red; c.fill(); };
      [[.30,.45],[.70,.45],[.24,.665],[.76,.665]].forEach(p=> star(p[0]*W,p[1]*H,W*.022));
      // VOODOO wordmark + footswitch + PLATE REVERB tag
      textSpaced(d,.50*W,.485*H,F.anton,34,red,'VOODOO',1.0);
      footRound(d,W*.50,H*.665,20*s);
      textSpaced(d,.50*W,.770*H,F.barlow,9,red,'PLATE REVERB',1.0);
      // power symbol + script brand + tagline
      textSpaced(d,.18*W,.575*H,F.barlow,6,red,'9V-18V DC',0.2);
      textC(d,.50*W,.880*H,F.ink,24,red,'Venson');
      textSpaced(d,.50*W,.930*H,F.barlow,6.5,red,'MECHANISMS OF MUSIC',0.4); } };

  // Tube Spring — Source Audio True Spring-style: brushed gunmetal box, two cream
  // knobs, a SHORT/LONG/TANK toggle, script wordmark with a spring squiggle, chrome
  // stomp + brand. Parody (Index Audio / Real Spring Reverb). RS knob names.
  // Mix0 Depth1.
  P.realspring = { w:280,h:420, knobs:[
      {id:0,cx:.27,cy:.25,r:.10,style:'pointer',cap:[230,224,208]},
      {id:1,cx:.73,cy:.25,r:.10,style:'pointer',cap:[230,224,208]}],
    tick:rgb(120,122,128), ptr:rgb(46,46,50),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(238,240,244);
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(92,94,100)); bg.addColorStop(1,rgb(60,62,68));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,14*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.05)'; c.lineWidth=1;
      for(let x=m;x<W-m;x+=3*s){ c.beginPath(); c.moveTo(x,m); c.lineTo(x,H-m); c.stroke(); } c.restore();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // status LED + knob labels (RS names)
      ledDot(d,W*.50,H*.085,true,224,60,50);
      textSpaced(d,.27*W,.085*H,F.barlow,9.5,wt,'MIX',0.4);
      textSpaced(d,.73*W,.085*H,F.barlow,9.5,wt,'DEPTH',0.4);
      // SHORT/LONG/TANK 3-way toggle (decorative)
      const tx=.50*W, ty=.43*H;
      rr(c,tx-5*s,ty-9*s,10*s,18*s,3*s); c.fillStyle=rgb(30,30,32); c.fill();
      rr(c,tx-5*s,ty-9*s,10*s,18*s,3*s); c.strokeStyle=rgb(8,8,10); c.lineWidth=s; c.stroke();
      const lg=c.createLinearGradient(tx-4*s,ty-6*s,tx+4*s,ty); lg.addColorStop(0,rgb(228,230,236)); lg.addColorStop(1,rgb(140,143,150));
      rr(c,tx-3.5*s,ty-7*s,7*s,8*s,2*s); c.fillStyle=lg; c.fill();
      textSpaced(d,.355*W,.415*H,F.barlow,6.5,wt,'SHORT',0.2);
      textSpaced(d,.50*W,.375*H,F.barlow,6.5,wt,'LONG',0.2);
      textSpaced(d,.645*W,.415*H,F.barlow,6.5,wt,'TANK',0.2);
      // 'Real Spring' script + spring squiggle + 'REVERB'
      textC(d,.50*W,.575*H,F.ink,38,wt,'Real Spring');
      c.strokeStyle=wt; c.lineWidth=1.8*s; c.beginPath();
      for(let i=0;i<=24;i++){ const x=.40*W+i*(.22*W/24), y=.620*H+Math.sin(i*0.9)*4*s; i?c.lineTo(x,y):c.moveTo(x,y); } c.stroke();
      textSpaced(d,.50*W,.665*H,F.barlow,10,wt,'REVERB',1.4);
      // footswitch + brand
      footRound(d,W*.50,H*.78,20*s);
      textSpaced(d,.50*W,.905*H,F.barlow,9,wt,'INDEX AUDIO',1.2); } };

  // Deja Chorus — Fulltone Deja'Vibe-style: matte-black landscape box, white
  // pinstripe border + script logo, two top knobs + two mode toggles, a big
  // offset knob + BYPASS stomp + blue LED. Recreated brand-free. Rate0 Depth1 Mix2.
  P.dejachorus = { w:480, h:300, knobs:[
      {id:0,cx:.155,cy:.205,r:.066,style:'boss'},
      {id:1,cx:.430,cy:.205,r:.066,style:'boss'},
      {id:2,cx:.825,cy:.560,r:.110,style:'boss'}],
    ptr:rgb(236,238,242),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,26,26,28); const wt=rgb(232,234,238);
      // white pinstripe border + a divider under the top knob strip
      c.save(); rr(c,16*s,16*s,W-32*s,H-32*s,10*s); c.strokeStyle=rgb(214,216,222); c.lineWidth=1.8*s; c.stroke(); c.restore();
      c.beginPath(); c.moveTo(22*s,H*.415); c.lineTo(W-22*s,H*.415); c.strokeStyle=rgb(214,216,222); c.lineWidth=1.3*s; c.stroke();
      // two decorative mode toggles between the top knobs (Modern/Vintage, Vibrato/Chorus)
      const tog=(tx,ty,up)=>{ const w0=9*s,h0=20*s;
        rr(c,tx-w0/2,ty-h0/2,w0,h0,3*s); c.fillStyle=rgb(16,16,18); c.fill();
        rr(c,tx-w0/2,ty-h0/2,w0,h0,3*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1*s; c.stroke();
        const ly=ty+(up?-1:1)*h0*0.18;
        const g=c.createLinearGradient(tx-4*s,ly-6*s,tx+4*s,ly+6*s); g.addColorStop(0,rgb(228,230,236)); g.addColorStop(1,rgb(150,153,160));
        rr(c,tx-4*s,ly-6*s,8*s,12*s,2*s); c.fillStyle=g; c.fill();
        rr(c,tx-4*s,ly-6*s,8*s,12*s,2*s); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.8*s; c.stroke(); };
      tog(.270*W,.205*H,false); tog(.330*W,.205*H,true);
      // top knob labels
      textSpaced(d,.155*W,.370*H,F.barlow,8.5,wt,'RATE',0.6);
      textSpaced(d,.430*W,.370*H,F.barlow,8.5,wt,'DEPTH',0.5);
      // white script logo + parody model code + brand
      textC(d,.375*W,.560*H,F.crete,40,wt,"Deja Chorus");
      textC(d,.585*W,.655*H,F.barlow,12,wt,'DC-1');
      textC(d,.46*W,.815*H,F.ink,22,wt,'ExtraTone');
      // big MIX knob label + blue status LED
      textSpaced(d,.825*W,.815*H,F.barlow,9,wt,'MIX',0.6);
      ledDot(d,W*.655,H*.610,true,70,150,234);
      // bypass footswitch
      footRound(d,W*.205,H*.730,20*s);
      textSpaced(d,.205*W,.910*H,F.barlow,8,wt,'BYPASS',0.6); } };

  // Acoustic Guitar Pedal — Rockman-style acoustic simulator: black box, 4 top
  // knobs with tick scales, the iconic glowing blue window + bold white wordmark.
  // Recreated brand-free. Params: Tone0 MidShift1 Body2 Mid3.
  P.acousticemulator = { w:300, h:360, knobs:[
      {id:0,cx:.145,cy:.160,r:.078,style:'pointer',cap:[32,32,34]},
      {id:1,cx:.385,cy:.160,r:.078,style:'pointer',cap:[32,32,34]},
      {id:2,cx:.625,cy:.160,r:.078,style:'pointer',cap:[32,32,34]},
      {id:3,cx:.865,cy:.160,r:.078,style:'pointer',cap:[32,32,34]}],
    tick:rgb(150,152,158), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,22,22,24); const wt=rgb(236,238,242);
      // knob labels
      textSpaced(d,.145*W,.282*H,F.barlow,8.5,wt,'TONE',0.3);
      textSpaced(d,.385*W,.282*H,F.barlow,7,wt,'MIDSHIFT',0.2);
      textSpaced(d,.625*W,.282*H,F.barlow,8.5,wt,'BODY',0.3);
      textSpaced(d,.865*W,.282*H,F.barlow,8.5,wt,'MID',0.3);
      // status legends (parody of the Rockman's printed text)
      textSpaced(d,.20*W,.350*H,F.barlow,6.5,rgb(150,152,158),'LOW BATTERY',0.2);
      textSpaced(d,.80*W,.350*H,F.barlow,6.5,rgb(150,152,158),'EFFECT ON',0.2);
      // blue rectangular push-button (the bypass switch, raised/glossy)
      const bx=W*.32, by=H*.405, bw=W*.36, bh=H*.140;
      rr(c,bx-3*s,by-3*s,bw+6*s,bh+6*s,8*s); c.fillStyle=rgb(8,8,10); c.fill();   // recess
      const bg=c.createLinearGradient(0,by,0,by+bh);
      bg.addColorStop(0,rgb(96,196,255)); bg.addColorStop(.5,rgb(42,150,228)); bg.addColorStop(1,rgb(20,94,168));
      rr(c,bx,by,bw,bh,6*s); c.fillStyle=bg; c.fill();
      rr(c,bx,by,bw,bh,6*s); c.strokeStyle=rgb(12,44,80); c.lineWidth=1.6*s; c.stroke();
      rr(c,bx+5*s,by+4*s,bw-10*s,bh*0.30,4*s); c.fillStyle=rgb(255,255,255,0.26); c.fill();   // gloss
      // bold white wordmark + parody brand (Rockman -> Stoneman)
      textC(d,.5*W,.680*H,F.anton,42,wt,'ACOUSTIC');
      textSpaced(d,.5*W,.775*H,F.anton,18,wt,'GUITAR  PEDAL',1.5);
      textSpaced(d,.5*W,.910*H,F.barlow,10,wt,'STONEMAN',1.4); } };

  // Carl Unlimited — Chandler Germanium Drive-style: royal-blue enclosure, gold
  // pinstripe frames, two yellow pointer knobs (white scale ticks), two 3-way
  // mode toggles, gold serif logo, red LED in a chrome bezel, footswitch.
  // Recreated brand-free. Params: Gain0 Tone1.
  P.germaniumdrive = { w:300, h:500, knobs:[
      {id:0,cx:.32,cy:.250,r:.105,style:'pointer',cap:[230,202,74]},
      {id:1,cx:.70,cy:.250,r:.105,style:'pointer',cap:[230,202,74]}],
    tick:rgb(234,238,244), ptr:rgb(36,38,44),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,40,54,132); const gold=rgb(216,178,68), wt=rgb(236,238,244);
      // two gold pinstripe frames (controls section + footswitch section)
      c.strokeStyle=gold; c.lineWidth=1.5*s;
      rr(c,W*.055,H*.095,W*.89,H*.625,9*s); c.stroke();
      rr(c,W*.055,H*.745,W*.89,H*.205,9*s); c.stroke();
      // top I/O legends
      textSpaced(d,.135*W,.062*H,F.barlow,9,gold,'OUT',0.5);
      textSpaced(d,.865*W,.062*H,F.barlow,9,gold,'IN',0.5);
      // scale numbers ('goes to 11' nod on the left knob)
      textC(d,.455*W,.150*H,F.barlow,8.5,wt,'11');
      textC(d,.79*W,.150*H,F.barlow,8.5,wt,'5');
      // gold italic knob labels
      textC(d,.32*W,.392*H,F.crete,17,gold,'Gain');
      textC(d,.70*W,.392*H,F.crete,17,gold,'Tone');
      // two decorative 3-way mode toggles
      const tog=(tx,ty)=>{ const w0=11*s,h0=25*s;
        rr(c,tx-w0/2,ty-h0/2,w0,h0,4*s); c.fillStyle=rgb(18,20,28); c.fill();
        rr(c,tx-w0/2,ty-h0/2,w0,h0,4*s); c.strokeStyle=rgb(8,9,14); c.lineWidth=1*s; c.stroke();
        const g=c.createLinearGradient(tx-5*s,ty-8*s,tx+5*s,ty+8*s); g.addColorStop(0,rgb(232,234,240)); g.addColorStop(1,rgb(150,153,162));
        rr(c,tx-5*s,ty-9*s,10*s,15*s,3*s); c.fillStyle=g; c.fill();
        rr(c,tx-5*s,ty-9*s,10*s,15*s,3*s); c.strokeStyle=rgb(70,72,80); c.lineWidth=0.8*s; c.stroke(); };
      tog(.135*W,.435*H); tog(.865*W,.435*H);
      textC(d,.135*W,.498*H,F.crete,13,gold,'Highs');
      textC(d,.865*W,.498*H,F.crete,13,gold,'Boost');
      // gold serif logo
      textC(d,.5*W,.560*H,F.crete,30,gold,'CARL');
      textC(d,.5*W,.610*H,F.crete,30,gold,'UNLIMITED');
      // red status LED in a chrome bezel
      const lx=W*.5, ly=H*.672;
      const cr=c.createRadialGradient(lx-3*s,ly-3*s,1,lx,ly,9*s); cr.addColorStop(0,rgb(228,230,236)); cr.addColorStop(1,rgb(120,124,132));
      c.beginPath(); c.arc(lx,ly,9*s,0,7); c.fillStyle=cr; c.fill(); c.strokeStyle=rgb(70,72,80); c.lineWidth=1*s; c.stroke();
      ledDot(d,lx,ly,true,228,52,46);
      // footswitch + bottom name
      footRound(d,W*.5,H*.825,23*s);
      textC(d,.5*W,.908*H,F.anton,17,gold,'GERMANIUM');
      textC(d,.5*W,.940*H,F.anton,17,gold,'DRIVE'); } };

  // Ibañez LF6 "Lo Fi" — Ibanez LF7-style lo-fi filter on the Tonelok body.
  // Labels use the RS knob names. Params: FilterType0 Mix1.
  P.lofifilter = ibanezSpec(280,460,
    [{id:0,cx:.32,r:.090,lbl:'FILTER TYPE',lblPx:7},{id:1,cx:.68,r:.090,lbl:'MIX'}],
    'LF6','LO FI');

  // NoFi Echo — Ibanez DE7-style: the Ibanez Tonelok (silver) template.
  // RS knob names. 3 RS knobs: Time0 Feedback1 Mix2.
  P.nofiecho = ibanezSpec(280,460,
    [{id:0,cx:.20,r:.070,lbl:'TIME'},{id:1,cx:.50,r:.070,lbl:'FEEDBACK',lblPx:7},{id:2,cx:.80,r:.070,lbl:'MIX'}],
    'DE6','DELAY/ECHO');

  // Foog FM107 — Moog MF107 (moogerfooger)-style: dark granite face + wood side
  // panels, big metallic knobs, foogerfooger/foog parody. RS knob names
  // (Modern Flanger has 4). Rate0 Depth1 Regen2 Mix3.
  P.fm107 = foogSpec(300,420,
    [{id:0,cx:.33,cy:.30,lbl:'RATE'},{id:1,cx:.67,cy:.30,lbl:'DEPTH'},
     {id:2,cx:.33,cy:.62,lbl:'REGEN'},{id:3,cx:.67,cy:.62,lbl:'MIX'}],
    'FM107');

  // Analog Delay — Moog MF-104M-style: the foog (moogerfooger) template.
  // RS knob names. 3 RS knobs: Time0 Feedback1 Mix2.
  P.fm104 = foogSpec(300,420,
    [{id:0,cx:.32,cy:.33,lbl:'TIME'},{id:1,cx:.68,cy:.33,lbl:'FEEDBACK',lblPx:7.5},
     {id:2,cx:.50,cy:.63,lbl:'MIX'}],
    'FM104');

  // Bob Filter — Moog MF-105 MuRF-style: custom foog layout (not the simple
  // foogSpec box) — two knob rows, decorative LFO/FREQ slide switches, the
  // signature 8-band FILTERS slider bank, an LED row + foog logo + stomp.
  // Parody (foogermooger / FM105). RS knob names. Sens0 Attack1 Release2 Mix3 Filter4.
  P.fm105 = { w:320,h:480, knobs:[
      {id:0,cx:.22,cy:.165,r:.072,style:'moog'},
      {id:3,cx:.50,cy:.165,r:.072,style:'moog'},
      {id:1,cx:.30,cy:.345,r:.072,style:'moog'},
      {id:2,cx:.70,cy:.345,r:.072,style:'moog'}],
    switches:[{id:4,cx:.78,cy:.175,hs:.036}],
    tick:rgb(150,152,158), ptr:rgb(238,240,244),
    draw(d,values){ const {ctx:c,W,H,s}=d; foogBody(d); const wt=rgb(226,228,232), dim=rgb(150,152,158);
      // brand + model
      textC(d,.34*W,.052*H,F.crete,17,wt,'foogermooger');
      textC(d,.74*W,.052*H,F.crete,15,wt,'FM105');
      // knob labels (RS names) above each knob
      const KL=[['SENS',.22,.165],['MIX',.50,.165],['ATTACK',.30,.345],['RELEASE',.70,.345]];
      KL.forEach(k=> textSpaced(d,k[1]*W,(k[2]-.072-.018)*H,F.barlow,8,wt,k[0],0.3));
      // FILTER is a 2-mode selector (engine draws the toggle via `switches`):
      // WAH sweep (value<0.5) vs voiced MuRF BANK (>=0.5). Active label brightens.
      const fv=(values&&values[4]!=null)?values[4]:1;
      textSpaced(d,.78*W,(.165-.072-.018)*H,F.barlow,8,wt,'FILTER',0.3);
      textSpaced(d,.78*W,.252*H,F.barlow,6.5,fv<0.5?wt:dim,'WAH',0.2);
      textSpaced(d,.78*W,.286*H,F.barlow,6.5,fv>=0.5?wt:dim,'BANK',0.2);
      // decorative slide switches (LFO OFF/ON, FREQ BASS/MIDS) between the 2nd-row knobs
      const slide=(sx,on,lbl)=>{ const sw=15*s, sh=8*s, sy=.345*H;
        rr(c,sx-sw/2,sy-sh/2,sw,sh,3*s); c.fillStyle=rgb(38,38,42); c.fill();
        rr(c,sx-sw/2,sy-sh/2,sw,sh,3*s); c.strokeStyle=rgb(10,10,12); c.lineWidth=0.8*s; c.stroke();
        rr(c,sx+(on?1:-1)*sw*0.18-sw*0.16,sy-sh/2+1.2*s,sw*0.32,sh-2.4*s,2*s); c.fillStyle=rgb(228,230,234); c.fill();
        textSpaced(d,sx,(.345-.030)*H,F.barlow,5.5,wt,lbl,0.2); };
      slide(.445*W,true,'LFO'); slide(.560*W,false,'FREQ');
      // FILTERS slider bank (decorative, MuRF signature)
      const bx=W*.105, bw=W*.81, by=H*.45, bh=H*.20;
      rr(c,bx,by,bw,bh,7*s); c.strokeStyle=wt; c.lineWidth=1.6*s; c.stroke();
      c.save(); c.translate(bx+10*s,by+bh/2); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,8,wt,'FILTERS',0.6); c.restore();
      const caps=[.30,.60,.25,.82,.18,.48,.70,.38], n=8, tx0=W*.24, tx1=W*.86, tTop=by+bh*0.20, tBot=by+bh*0.86;
      for(let i=0;i<n;i++){ const x=tx0+i*(tx1-tx0)/(n-1);
        c.strokeStyle=rgb(150,152,158); c.lineWidth=1.4*s; c.beginPath(); c.moveTo(x,tTop); c.lineTo(x,tBot); c.stroke();
        const cy2=tTop+caps[i]*(tBot-tTop);
        const cg=c.createRadialGradient(x-2*s,cy2-2*s,1*s,x,cy2,7*s); cg.addColorStop(0,rgb(238,240,244)); cg.addColorStop(1,rgb(150,153,160));
        rr(c,x-6*s,cy2-4*s,12*s,8*s,2.5*s); c.fillStyle=cg; c.fill();
        rr(c,x-6*s,cy2-4*s,12*s,8*s,2.5*s); c.strokeStyle=rgb(40,42,46); c.lineWidth=0.8*s; c.stroke(); }
      // LED row + labels
      [['DRIVE',.30],['BYPASS',.50],['RATE',.70]].forEach(p=>{ ledDot(d,p[1]*W,.695*H,true,150,196,255); textSpaced(d,p[1]*W,.730*H,F.barlow,6.5,wt,p[0],0.2); });
      // foog logo + footswitch
      textC(d,.50*W,.795*H,F.crete,22,wt,'foog');
      footRound(d,W*.50,H*.885,18*s); } };

  // Auto Filter — Mu-Tron III-style: brushed-silver box, blue control panel with
  // black scaled knobs, rainbow maker logo + a red POWER lever, wordmark + brand
  // + chrome stomp. Parody (Bu-Tron III / auditronics). RS knob names.
  // FilterType0 Res1 Sens2 Attack3 Release4.
  P.butroniii = { w:300,h:470, knobs:[
      {id:1,cx:.22,cy:.290,r:.058,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.22,cy:.440,r:.058,style:'pointer',cap:[26,26,28]},
      {id:3,cx:.55,cy:.180,r:.058,style:'pointer',cap:[26,26,28]},
      {id:4,cx:.55,cy:.360,r:.058,style:'pointer',cap:[26,26,28]}],
    sw3:[{id:0,cx:.185,cy:.150}],
    tick:rgb(200,210,230), ptr:rgb(240,242,246),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, wt=rgb(238,240,244), dk=rgb(30,30,34);
      // brushed-silver box
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(196,198,202)); bg.addColorStop(1,rgb(162,164,170));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=bg; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.10)'; c.lineWidth=1;
      for(let y=m;y<H*.58;y+=3*s){ c.beginPath(); c.moveTo(m,y); c.lineTo(W-m,y); c.stroke(); } c.restore();
      rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // blue control panel
      rr(c,W*.07,H*.05,W*.86,H*.505,8*s); c.fillStyle=rgb(34,70,150); c.fill();
      rr(c,W*.07,H*.05,W*.86,H*.505,8*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=1.4*s; c.stroke();
      // rainbow maker logo (top-right of panel)
      const lx=W*.74, ly=H*.085, bw=6*s, bh=20*s;
      [[214,52,52],[70,176,90],[64,120,212]].forEach((col,i)=>{ rr(c,lx+i*(bw+2*s),ly,bw,bh,1.5*s); c.fillStyle=rgb(col[0],col[1],col[2]); c.fill(); });
      // knob labels (RS names) below each knob
      [['RES',.22,.290,8],['SENS',.22,.440,8],['ATTACK',.55,.180,7.5],['RELEASE',.55,.360,7.5]]
        .forEach(k=> textSpaced(d,k[1]*W,(k[2]+.085)*H,F.barlow,k[3],wt,k[0],0.2));
      // FilterType 3-way MODE selector (LP/BP/HP) — lever drawn by engine via sw3
      textSpaced(d,.185*W,.072*H,F.barlow,7,wt,'MODE',0.3);
      textSpaced(d,.305*W,.118*H,F.barlow,6.5,wt,'HP',0.2);
      textSpaced(d,.305*W,.150*H,F.barlow,6.5,wt,'BP',0.2);
      textSpaced(d,.305*W,.182*H,F.barlow,6.5,wt,'LP',0.2);
      // decorative POWER lever (red, ON) lower-right of panel
      const px=W*.80, py=H*.40;
      rr(c,px-9*s,py-7*s,18*s,14*s,3*s); c.fillStyle=rgb(28,30,38); c.fill();
      rr(c,px+1*s,py-9*s,8*s,13*s,2*s); c.fillStyle=rgb(206,44,44); c.fill();
      textSpaced(d,px,py+15*s,F.barlow,6,wt,'POWER',0.2);
      textSpaced(d,px-16*s,py,F.barlow,5,wt,'OFF',0.1); textSpaced(d,px+16*s,py,F.barlow,5,wt,'ON',0.1);
      // wordmark + chrome stomp + brand
      textSpaced(d,.42*W,.625*H,F.anton,24,dk,'BU-TRON III',0.5);
      footRound(d,W*.50,H*.79,22*s);
      textSpaced(d,.78*W,.93*H,F.barlow,8.5,dk,'auditronics',0.2); } };

  // Custom Drive — Fulltone OCD-style: cream box, two black knobs, an HP/LP voice
  // toggle + blue LED, fat black wordmark + script brand. Parody (ExtraTone / CDO).
  // RS knob names; Voice is the binary HP/LP selector. Gain0 Tone1 Voice2.
  P.cdo = { w:280,h:480, knobs:[
      {id:0,cx:.26,cy:.215,r:.082,style:'davies'},
      {id:1,cx:.74,cy:.215,r:.082,style:'davies'}],
    switches:[{id:2,cx:.50,cy:.150,hs:.030}],
    ptr:rgb(240,240,242),
    draw(d,values){ const {ctx:c,W,H,s}=d; const m=8*s, dk=rgb(30,28,22), dim=rgb(150,146,132);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(224,214,186)); bg.addColorStop(1,rgb(198,188,160));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=2*s; c.stroke();
      // knob labels (RS names, OCD-style script)
      textC(d,.26*W,.105*H,F.crete,17,dk,'Gain');
      textC(d,.74*W,.105*H,F.crete,17,dk,'Tone');
      // HP/LP voice selector (toggle drawn by engine via switches) + blue LED
      const vv=(values&&values[2]!=null)?values[2]:0;
      textSpaced(d,.50*W,.082*H,F.barlow,8,vv>=0.5?dk:dim,'HP',0.3);
      textSpaced(d,.50*W,.218*H,F.barlow,8,vv<0.5?dk:dim,'LP',0.3);
      ledDot(d,.50*W,.278*H,true,70,150,234);
      // fat wordmark + footswitch + script brand
      textC(d,.50*W,.500*H,F.anton,78,rgb(18,16,12),'CDO');
      footRound(d,W*.50,H*.715,22*s);
      textC(d,.50*W,.855*H,F.ink,24,dk,'ExtraTone');
      textSpaced(d,.50*W,.905*H,F.barlow,7,dk,'Built in the USA',0.3); } };

  // Marshall GV-2 Guv'nor Plus-style: cream Marshall-compact body, five gold
  // knobs, black oval gold-edged badge, gold stomp, script brand. Parody
  // (Marsten / GM-2 Guvnor Minus). RS knob names. Gain0 Bass1 Mid2 Treble3 Deep4.
  P.gm2 = { w:300,h:470, knobs:[
      {id:0,cx:.12,cy:.150,r:.052,style:'pointer',cap:[200,164,84]},
      {id:1,cx:.31,cy:.150,r:.052,style:'pointer',cap:[200,164,84]},
      {id:2,cx:.50,cy:.150,r:.052,style:'pointer',cap:[200,164,84]},
      {id:3,cx:.69,cy:.150,r:.052,style:'pointer',cap:[200,164,84]},
      {id:4,cx:.88,cy:.150,r:.052,style:'pointer',cap:[200,164,84]}],
    tick:rgb(150,140,110), ptr:rgb(40,36,26),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, dk=rgb(40,36,26), gold=rgb(202,166,86);
      c.fillStyle=rgb(8,8,9); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(224,216,196)); bg.addColorStop(1,rgb(198,190,168));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.3)'; c.lineWidth=2*s; c.stroke();
      // red CHECK LED + knob labels (RS names)
      ledDot(d,W*.50,H*.045,true,224,40,40);
      [['GAIN',.12],['BASS',.31],['MID',.50],['TREBLE',.69,6.5],['DEEP',.88]].forEach(k=>
        textSpaced(d,k[1]*W,.235*H,F.barlow,k[2]||7.5,dk,k[0],0.2));
      // black oval gold-edged badge: GM-2 / GUVNOR / MINUS
      const ox=.50*W, oy=.46*H, orx=W*.27, ory=H*.105;
      c.save(); c.translate(ox,oy); c.scale(1,ory/orx); c.beginPath(); c.arc(0,0,orx,0,7); c.restore();
      c.fillStyle=rgb(18,18,18); c.fill(); c.strokeStyle=gold; c.lineWidth=2.6*s; c.stroke();
      c.strokeStyle=rgb(120,96,40); c.lineWidth=0.8*s; c.stroke();
      textSpaced(d,ox,oy-ory*0.52,F.barlow,8,gold,'GM-2',0.4);
      textC(d,ox,oy+ory*0.02,F.anton,30,gold,'GUVNOR');
      textSpaced(d,ox,oy+ory*0.58,F.barlow,8,gold,'MINUS',1.0);
      // gold footswitch
      const fx=.50*W, fy=.69*H, R=20*s;
      const fg=c.createRadialGradient(fx,fy,R*0.4,fx,fy,R*1.15); fg.addColorStop(0,rgb(232,200,120)); fg.addColorStop(1,rgb(166,130,58));
      c.beginPath(); c.arc(fx,fy,R*1.12,0,7); c.fillStyle=fg; c.fill(); c.strokeStyle=rgb(116,90,38); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(fx,fy,R*0.78,0,7); c.fillStyle=rgb(212,178,98); c.fill();
      c.beginPath(); c.arc(fx-R*0.25,fy-R*0.3,R*0.34,0,7); c.fillStyle='rgba(255,242,205,0.45)'; c.fill();
      // script brand (parody)
      textC(d,.50*W,.885*H,F.ink,26,dk,'Marsten'); } };

  // Range Booster — Dallas Rangemaster Treble Booster-style: wide silver chassis
  // with a folded chrome lip, bold wordmark, ascending music staff, OFF/ON toggle,
  // a single BOOST/SET knob and a GUITAR input jack. Recreated brand-free. Boost0.
  P.rangebooster = { w:520,h:330, knobs:[
      {id:0,cx:.50,cy:.655,r:.072,style:'boss'}],
    ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, ink=rgb(20,20,22);
      c.fillStyle=rgb(6,6,7); c.fillRect(0,0,W,H);
      // chrome chassis
      const og=c.createLinearGradient(0,m,0,H-m); og.addColorStop(0,rgb(214,216,220)); og.addColorStop(1,rgb(168,170,176));
      rr(c,m,m,W-2*m,H-2*m,10*s); c.fillStyle=og; c.fill();
      rr(c,m,m,W-2*m,H-2*m,10*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // folded top lip
      rr(c,m,m,W-2*m,H*.085,8*s); c.fillStyle=rgb(150,152,158); c.fill();
      // grey face inset
      rr(c,W*.035,H*.135,W*.93,H*.80,6*s); c.fillStyle=rgb(210,210,208); c.fill();
      rr(c,W*.035,H*.135,W*.93,H*.80,6*s); c.strokeStyle='rgba(0,0,0,0.18)'; c.lineWidth=1.2*s; c.stroke();
      // wordmark
      textSpaced(d,.275*W,.265*H,F.bebas,34,ink,'RANGE BOOSTER',1.0);
      textSpaced(d,.225*W,.405*H,F.bebas,20,ink,'TREBLE BOOSTER',1.0);
      // ascending music staff + notes
      c.strokeStyle=ink; c.lineWidth=1.3*s;
      for(let i=0;i<5;i++){ const lift=i*8*s; c.beginPath(); c.moveTo(W*.55,H*.47-lift); c.lineTo(W*.93,H*.37-lift); c.stroke(); }
      [[.63,.41],[.71,.38],[.79,.345],[.86,.315],[.905,.295]].forEach(p=>{
        c.beginPath(); c.arc(p[0]*W,p[1]*H,4*s,0,7); c.fillStyle=ink; c.fill();
        c.beginPath(); c.moveTo(p[0]*W+3.6*s,p[1]*H); c.lineTo(p[0]*W+3.6*s,p[1]*H-16*s); c.strokeStyle=ink; c.lineWidth=1.4*s; c.stroke(); });
      // OFF/ON toggle (knurled) + screws
      screw(d,.105*W,.40*H); screw(d,.105*W,.86*H);
      const tx=.105*W, ty=.63*H, tw=14*s, th=30*s;
      rr(c,tx-tw/2,ty-th/2,tw,th,4*s); c.fillStyle=rgb(24,24,26); c.fill();
      rr(c,tx-tw/2,ty-th/2,tw,th,4*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1*s; c.stroke();
      c.strokeStyle='rgba(255,255,255,0.12)'; c.lineWidth=0.8*s;
      for(let y=ty-th/2+3*s;y<ty+th/2;y+=3*s){ c.beginPath(); c.moveTo(tx-tw/2+2*s,y); c.lineTo(tx+tw/2-2*s,y); c.stroke(); }
      textSpaced(d,.105*W,.505*H,F.barlow,8,ink,'OFF',0.3);
      textSpaced(d,.105*W,.765*H,F.barlow,8,ink,'ON',0.3);
      // BOOST / SET knob labels (RS knob = Boost)
      textC(d,.405*W,.655*H,F.bebas,20,ink,'BOOST','right');
      textC(d,.595*W,.655*H,F.bebas,20,ink,'SET','left');
      // GUITAR input jack
      const gx=.84*W, gy=.605*H, R=18*s;
      const jg=c.createRadialGradient(gx-R*.3,gy-R*.3,R*.1,gx,gy,R); jg.addColorStop(0,rgb(222,224,228)); jg.addColorStop(1,rgb(140,142,148));
      c.beginPath(); c.arc(gx,gy,R,0,7); c.fillStyle=jg; c.fill(); c.strokeStyle=rgb(80,82,88); c.lineWidth=1.6*s; c.stroke();
      c.beginPath(); c.arc(gx,gy,R*0.42,0,7); c.fillStyle=rgb(28,28,30); c.fill();
      textSpaced(d,.84*W,.79*H,F.bebas,18,ink,'GUITAR',0.5); } };

  // Vintage Distortion — DOD 250 Overdrive Preamp-style: mustard-yellow box, two
  // chrome knobs, lowercase logo, 'Overdrive Preamp/250' text, the horizontal vent
  // grille + footswitch. Parody (sos 250). RS knob names. Gain0 Tone1.
  P.vintagedistortion = { w:280,h:470, knobs:[
      {id:0,cx:.28,cy:.210,r:.078,style:'boss'},
      {id:1,cx:.72,cy:.210,r:.078,style:'boss'}],
    ptr:rgb(238,240,242),
    draw(d){ const {ctx:c,W,H,s}=d; const m=8*s, ink=rgb(28,26,16);
      c.fillStyle=rgb(8,8,7); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(238,198,58)); bg.addColorStop(1,rgb(214,172,36));
      rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.35)'; c.lineWidth=2*s; c.stroke();
      // lowercase brand logo (parody) + knob labels (RS names)
      textC(d,.50*W,.072*H,F.anton,22,ink,'sos');
      textSpaced(d,.28*W,.115*H,F.barlow,8,ink,'GAIN',0.3);
      textSpaced(d,.72*W,.115*H,F.barlow,8,ink,'TONE',0.3);
      // model text
      textC(d,.50*W,.395*H,F.crete,30,ink,'Overdrive');
      textC(d,.53*W,.460*H,F.crete,22,ink,'Preamp/250');
      // horizontal vent grille
      const gx=W*.27, gw=W*.46, gy=H*.525, gh=H*.155;
      rr(c,gx,gy,gw,gh,4*s); c.strokeStyle=ink; c.lineWidth=1.2*s; c.stroke();
      c.lineWidth=2.2*s;
      for(let y=gy+6*s;y<gy+gh-3*s;y+=6.5*s){ c.beginPath(); c.moveTo(gx+5*s,y); c.lineTo(gx+gw-5*s,y); c.stroke(); }
      // footswitch
      footRound(d,W*.50,H*.815,22*s); } };

  // 80s Flanger — MXR M117R-style: hammered-grey landscape box, black knobs,
  // POWER label, 'NYR' logo box + 'flanger' tag, side jacks. RS knob names
  // (EightiesFlanger exposes 3). Rate0 Depth1 Mix2.
  P.n117rflanger = { w:460,h:320, knobs:[
      {id:0,cx:.25,cy:.255,r:.075,style:'davies'},
      {id:1,cx:.50,cy:.255,r:.075,style:'davies'},
      {id:2,cx:.75,cy:.255,r:.075,style:'davies'}],
    ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const lt=rgb(228,230,234), m=7*s;
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(112,114,120)); g.addColorStop(.5,rgb(92,94,100)); g.addColorStop(1,rgb(74,76,82));
      rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=g; c.fill();
      // hammered speckle
      c.save(); rr(c,m,m,W-2*m,H-2*m,12*s); c.clip();
      c.fillStyle=rgb(255,255,255,0.05);
      for(let i=0;i<260;i++){ c.fillRect(m+((i*71)%Math.floor(W-2*m)),m+((i*131)%Math.floor(H-2*m)),1.4*s,1.4*s); }
      c.fillStyle=rgb(0,0,0,0.07);
      for(let i=0;i<260;i++){ c.fillRect(m+((i*97)%Math.floor(W-2*m)),m+((i*53)%Math.floor(H-2*m)),1.4*s,1.4*s); }
      c.restore();
      // silver frame
      c.strokeStyle=rgb(178,180,186); c.lineWidth=2*s; rr(c,W*.035,H*.06,W*.93,H*.88,9*s); c.stroke();
      // POWER label + knob labels (RS names)
      textSpaced(d,.50*W,.105*H,F.barlow,7.5,lt,'POWER',1.2);
      textSpaced(d,.25*W,.435*H,F.barlow,11,lt,'RATE',0.5);
      textSpaced(d,.50*W,.435*H,F.barlow,11,lt,'DEPTH',0.5);
      textSpaced(d,.75*W,.435*H,F.barlow,11,lt,'MIX',0.5);
      // red LED
      ledDot(d,W*.50,H*.555,true,224,52,46);
      // NYR logo box + footswitch + 'flanger'
      rr(c,W*.06,H*.705,W*.215,H*.16,7*s); c.strokeStyle=lt; c.lineWidth=2.4*s; c.stroke();
      textC(d,W*.1675,H*.787,F.anton,30,lt,'NYR');
      footRound(d,W*.50,H*.79,16*s);
      textC(d,W*.77,H*.79,F.barlow,26,lt,'flanger');
      // side jack legends (OUTPUT left, INPUT right)
      c.save(); c.translate(W*.04,H*.40); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,7.5,lt,'OUTPUT',0.4); c.restore();
      c.save(); c.translate(W*.965,H*.40); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,7.5,lt,'INPUT',0.4); c.restore(); } };

  // Deluxe Servant — EH Deluxe Electric Mistress-style: brushed-silver landscape
  // box, black diagonal wedge with the retro logo, 3 black knobs in a right
  // column. Parody (quimical-harmony / DELUXE SERVANT). RS knob names.
  // Rate0 Depth1 Mix2.  (Pedal_VintageFlanger → VintageFlanger.vst3.)
  P.deluxeservant = { w:480,h:360, knobs:[
      {id:0,cx:.815,cy:.335,r:.058,style:'davies'},
      {id:1,cx:.815,cy:.575,r:.058,style:'davies'},
      {id:2,cx:.815,cy:.815,r:.058,style:'davies'}],
    tick:rgb(150,152,158), ptr:rgb(244,244,240),
    draw(d){ const {ctx:c,W,H,s}=d; const m=7*s, ink=rgb(30,30,34), wt=rgb(234,236,240);
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,m,0,H-m); g.addColorStop(0,rgb(200,202,207)); g.addColorStop(.5,rgb(178,180,186)); g.addColorStop(1,rgb(158,160,166));
      rr(c,m,m,W-2*m,H-2*m,10*s); c.fillStyle=g; c.fill();
      c.save(); rr(c,m,m,W-2*m,H-2*m,10*s); c.clip();
      c.strokeStyle=rgb(150,153,160,0.18); c.lineWidth=1;
      for(let y=m;y<H-m;y+=3*s){ c.beginPath(); c.moveTo(m,y); c.lineTo(W-m,y); c.stroke(); }
      // black diagonal wedge (upper-left)
      c.beginPath(); c.moveTo(m,m); c.lineTo(W*.66,m); c.lineTo(W*.30,H*.74); c.lineTo(m,H*.74); c.closePath();
      c.fillStyle=rgb(20,20,22); c.fill(); c.restore();
      rr(c,m,m,W-2*m,H-2*m,10*s); c.strokeStyle='rgba(0,0,0,0.4)'; c.lineWidth=2*s; c.stroke();
      // top jack legends
      textSpaced(d,.22*W,.075*H,F.barlow,6.5,ink,'FLANGED OUT',0.2);
      textSpaced(d,.44*W,.075*H,F.barlow,6.5,ink,'DIRECT OUT',0.2);
      textSpaced(d,.61*W,.075*H,F.barlow,6.5,ink,'INPUT',0.2);
      textSpaced(d,.85*W,.075*H,F.barlow,6.5,ink,'FILTER MATRIX',0.2);
      // retro logo on the black wedge
      textSpaced(d,.295*W,.205*H,F.bebas,22,wt,'DELUXE',1.0);
      textC(d,.295*W,.355*H,F.bebas,52,wt,'SERVANT');
      textSpaced(d,.295*W,.470*H,F.barlow,8,wt,'FLANGER / FILTER MATRIX',0.3);
      // knob labels (RS names)
      // knob labels (RS names) to the LEFT of each knob, vertically centred
      textC(d,.71*W,.335*H,F.barlow,11,ink,'RATE','right');
      textC(d,.71*W,.575*H,F.barlow,11,ink,'DEPTH','right');
      textC(d,.71*W,.815*H,F.barlow,11,ink,'MIX','right');
      // footswitch + bottom legends
      footRound(d,W*.145,H*.70,15*s);
      textC(d,.30*W,.885*H,F.crete,15,ink,'quimical-harmony');
      textSpaced(d,.62*W,.895*H,F.barlow,6.5,ink,'MADE IN NEW YORK CITY  U.S.A.',0.2); } };

  // Eden WTDI — landscape gold-panel bass preamp (mirrors eden_wtdi/EdenWtdi_ui.cpp).
  // Param order: Gain0 Enhance1 Comp2 Master3 Bass4 Mid5 Treble6 BassBoost7 MidShift8.
  P.wtdx = { w:560, h:360,
    knobs:[
      {id:4,cx:.190,cy:.300,r:.058,style:'boss'}, {id:5,cx:.500,cy:.300,r:.058,style:'boss'}, {id:6,cx:.810,cy:.300,r:.058,style:'boss'},
      {id:0,cx:.160,cy:.660,r:.058,style:'boss'}, {id:1,cx:.385,cy:.660,r:.058,style:'boss'},
      {id:2,cx:.610,cy:.660,r:.058,style:'boss'}, {id:3,cx:.835,cy:.660,r:.058,style:'boss'}],
    switches:[ {id:7,cx:.345,cy:.330,hs:.024}, {id:8,cx:.655,cy:.330,hs:.024} ],
    ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H}=d; const dark=rgb(40,28,12);
      c.fillStyle=rgb(12,12,14); c.fillRect(0,0,W,H);
      rr(c,10,8,W-20,H-16,14); c.fillStyle=rgb(24,22,20); c.fill();
      const px=18,py=16,pw=W-36,ph=(H-16)*0.80;
      const g=c.createLinearGradient(0,py,0,py+ph); g.addColorStop(0,rgb(214,178,96)); g.addColorStop(1,rgb(168,132,60));
      rr(c,px,py,pw,ph,8); c.fillStyle=g; c.fill();
      rr(c,px,py,pw,ph,8); c.strokeStyle=rgb(120,92,40); c.lineWidth=1.5; c.stroke();
      // red logo box top-left (parody brand) + model top-right
      rr(c,px+8,py+8,98,30,4); c.strokeStyle=rgb(180,30,28); c.lineWidth=2; c.stroke();
      textC(d,px+8+49,py+23,F.anton,16,rgb(180,30,28),'GARDEN');
      textC(d,px+pw-10,py+14,F.bebas,18,dark,'WT-DX','right');
      textC(d,px+pw-10,py+30,F.barlow,9,rgb(70,52,28),'Bass Guitar Pre Amplifier','right');
      // knob labels (drawn here; 'boss' knobs don't self-label)
      const R=.058*W+13;
      const lab=(cx,cy,t)=>textC(d,cx*W,cy*H+R,F.barlow,11,dark,t);
      lab(.190,.300,'BASS'); lab(.500,.300,'MID'); lab(.810,.300,'TREBLE');
      lab(.160,.660,'GAIN'); lab(.385,.660,'ENHANCE'); lab(.610,.660,'COMP'); lab(.835,.660,'MASTER');
      // switch labels (two lines, below the squares)
      const sl=(cx,a,b)=>{ const y=.330*H+.024*W+9; textC(d,cx*W,y,F.barlow,8,dark,a); textC(d,cx*W,y+9,F.barlow,8,dark,b); };
      sl(.345,'BASS','BOOST'); sl(.655,'MID','SHIFT');
      ledDot(d,px+pw*0.5,py+14,true,230,60,50);
      footRound(d,W*0.5,H*0.90,17); } };

  // Bass Wah — Cry-Baby-style brass treadle (mirrors bass_wah/BassWah_ui.cpp).
  // Param order: Auto0 Pedal1 Sens2 Speed3. Treadle tilt follows Pedal.
  P.basswah = { w:300, h:460,
    knobs:[ {id:1,cx:.375,cy:.835,r:.072,style:'boss'}, {id:2,cx:.610,cy:.835,r:.072,style:'boss'}, {id:3,cx:.845,cy:.835,r:.072,style:'boss'} ],
    switches:[ {id:0,cx:.135,cy:.835,hs:.045} ],
    ptr:rgb(238,240,244),
    draw(d,values){ const {ctx:c,W,H}=d;
      c.fillStyle=rgb(12,11,9); c.fillRect(0,0,W,H);
      const bx=10,by=8,bw=W-20,bh=H-16;
      const brass=c.createLinearGradient(bx,by,bx+bw,by+bh); brass.addColorStop(0,rgb(150,120,70)); brass.addColorStop(1,rgb(96,74,42));
      rr(c,bx,by,bw,bh,16); c.fillStyle=brass; c.fill();
      rr(c,bx,by,bw,bh,16); c.strokeStyle=rgb(60,46,24); c.lineWidth=2; c.stroke();
      // chrome-framed black ribbed rocker treadle
      const tx=bx+26,ty=by+18,tw=bw-52,th=bh*0.60;
      rr(c,tx-6,ty-6,tw+12,th+12,12); const chrome=c.createLinearGradient(0,ty-6,0,ty+th+6); chrome.addColorStop(0,rgb(225,228,232)); chrome.addColorStop(1,rgb(120,124,130)); c.fillStyle=chrome; c.fill();
      rr(c,tx,ty,tw,th,9); c.fillStyle=rgb(24,24,26); c.fill();
      const pedal=(values&&values[1]!=null)?values[1]:0.25, tilt=(pedal-0.5)*0.16;
      c.strokeStyle=rgb(60,62,66); c.lineWidth=2;
      for(let i=1;i<16;i++){ const yy=ty+th*i/16, dx=(yy-(ty+th*0.5))*tilt; c.beginPath(); c.moveTo(tx+6+dx,yy); c.lineTo(tx+tw-6+dx,yy); c.stroke(); }
      textC(d,tx+tw*0.5,ty+th*0.5,F.bebas,20,rgb(150,120,70),'BASS WAH');
      const on=(values&&values[0]!=null)?values[0]>0.5:true;
      ledDot(d,W*0.5,by+bh*0.66,on,255,70,60);
      // control panel + labels (Auto square drawn by drawSpec)
      const py0=by+bh*0.70, pH=bh*0.28; rr(c,bx+10,py0,bw-20,pH,10); c.fillStyle=rgb(26,24,22); c.fill();
      textC(d,.135*W,.835*H+.045*W+10,F.barlow,10,rgb(225,210,175),'Auto');
      const R=.072*W+12;
      textC(d,.375*W,.835*H+R,F.barlow,11,rgb(225,210,175),'PEDAL');
      textC(d,.610*W,.835*H+R,F.barlow,11,rgb(225,210,175),'SENS');
      textC(d,.845*W,.835*H+R,F.barlow,11,rgb(225,210,175),'SPEED'); } };

  // Bass Auto Filter — AutoSweep / QTron envelope filter. EHX Q-Tron+ look: black
  // landscape box, a row of 6 knobs, big two-colour graffiti 'Q-TRIX' logo (parody;
  // Q orange + -TRIX purple). RS params (6 knobs): FilterType0 Res4 Sens6 Attack1
  // Release2 Mix5.
  P.qtrix = { w:560, h:340,
    knobs:[
      {id:0,cx:.105,cy:.26,r:.052,style:'boss',select:3},  // MODE (FilterType: LP/BP/HP selector)
      {id:4,cx:.262,cy:.26,r:.052,style:'boss'},  // PEAK  (Res)
      {id:6,cx:.419,cy:.26,r:.052,style:'boss'},  // GAIN  (Sens)
      {id:1,cx:.576,cy:.26,r:.052,style:'boss'},  // ATTACK
      {id:2,cx:.733,cy:.26,r:.052,style:'boss'},  // RELEASE
      {id:5,cx:.890,cy:.26,r:.052,style:'boss'}], // MIX
    ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H}=d; const w=rgb(228,230,236), m=7;
      // black body, NO screws, with a very subtle metallic rim (Big-Muff-style frame)
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,m,0,H-m); bg.addColorStop(0,rgb(44,44,48)); bg.addColorStop(1,rgb(20,20,24));
      rr(c,m,m,W-2*m,H-2*m,14); c.fillStyle=bg; c.fill();
      const mb=c.createLinearGradient(0,m,0,H-m); mb.addColorStop(0,rgb(178,180,186)); mb.addColorStop(0.5,rgb(108,110,116)); mb.addColorStop(1,rgb(72,74,80));
      rr(c,m,m,W-2*m,H-2*m,14); c.strokeStyle=mb; c.lineWidth=2.5; c.stroke();
      const names=['MODE','PEAK','GAIN','ATTACK','RELEASE','MIX'], cxs=[.105,.262,.419,.576,.733,.890];
      cxs.forEach((cx,i)=> textC(d, cx*W, .26*H + .052*W + 12, F.barlow, 11, w, names[i]));
      // MODE is a 3-way selector: mark LP / BP / HP at the knob's detent angles
      const mkx=.105*W, mky=.26*H, mr=.052*W+13;
      setFont(d, F.barlow, 10.5); c.fillStyle=rgb(206,208,214); c.textAlign='center'; c.textBaseline='middle';
      [['LP',0],['BP',0.5],['HP',1]].forEach(p=>{ const a=(135+p[1]*270)*Math.PI/180;
        c.fillText(p[0], mkx+mr*Math.cos(a), mky+mr*Math.sin(a)); });
      // two-colour graffiti logo: big 'Q' orange + slightly smaller 'TRIX' purple,
      // no hyphen, scaled to fill most of the bottom width (capped so it fits).
      c.textAlign='left'; c.textBaseline='alphabetic';
      setFont(d, F.graffiti, 100); const refQ = c.measureText('Q').width;
      setFont(d, F.graffiti, 72);  const refT = c.measureText('TRIX').width;
      const qSize = Math.min(190, 100 * (0.92*W) / (refQ + refT)), tSize = qSize * 0.72;
      setFont(d, F.graffiti, qSize); const wq = c.measureText('Q').width;
      setFont(d, F.graffiti, tSize); const wt = c.measureText('TRIX').width;
      const gap = qSize * 0.04, by = .83*H;          // baseline very low: letters span ~labels → ~bottom
      let x = .5*W - (wq + gap + wt) / 2;
      setFont(d, F.graffiti, qSize); c.fillStyle = rgb(244,150,46); c.fillText('Q', x, by);
      setFont(d, F.graffiti, tSize); c.fillStyle = rgb(152,88,208); c.fillText('TRIX', x + wq + gap, by);
      textC(d, W*0.90, H*0.47, F.crete, 14, rgb(206,208,214), 'quimical-harmony', 'right');  // brand: above TRIX, below the knobs, right
      ledDot(d, W*0.5, H*0.10, true, 255,80,70);
      footRound(d, W*0.5, H*0.89, 16);   // footswitch centred at the bottom (over the logo if needed)
    } };

  // ── graphic-EQ faders (mirrors graphic_eq_ui.hpp) ─────────────────────────
  // Geometry in spec-units (W=spec.w, H=spec.h). Boss = portrait/tall,
  // Mesa = landscape/wide. Param id == band index.
  function eqGeom(spec) {
    const W = spec.w, H = spec.h, mesa = spec.mesa, n = spec.bands.length;
    const plateX = (mesa ? 0.100 : 0.085) * W, plateW = (mesa ? 0.800 : 0.830) * W;
    const plateY = (mesa ? 0.170 : 0.135) * H, plateH = (mesa ? 0.440 : 0.305) * H;
    const faderL = plateX + 0.085 * W, faderW = plateW - 0.105 * W, colW = faderW / n;
    const tT = plateY + (mesa ? 0.095 : 0.072) * H, tB = plateY + plateH - (mesa ? 0.055 : 0.038) * H;
    return { W, H, mesa, n, plateX, plateW, plateY, plateH, faderL, faderW, colW, tT, tB,
      colX: i => faderL + (i + 0.5) * colW,
      valToY: v => tT + (1 - v) * (tB - tT),
      yToVal: y => clamp(1 - (y - tT) / (tB - tT), 0, 1) };
  }
  function eqDraw(d, spec, values) {
    const c = d.ctx, W = spec.w, H = spec.h, m = 7, mesa = spec.mesa, G = eqGeom(spec);
    const R = spec.col[0], Gc = spec.col[1], B = spec.col[2], cl = v => clamp(v, 0, 255);
    c.fillStyle = rgb(10, 10, 12); c.fillRect(0, 0, W, H);
    const grad = c.createLinearGradient(0, m, 0, H - m);
    grad.addColorStop(0, rgb(cl(R + 16), cl(Gc + 16), cl(B + 16)));
    grad.addColorStop(1, rgb(cl(R - 12), cl(Gc - 12), cl(B - 12)));
    rr(c, m, m, W - 2 * m, H - 2 * m, 12); c.fillStyle = grad; c.fill();
    rr(c, m, m, W - 2 * m, H - 2 * m, 12); c.strokeStyle = 'rgba(0,0,0,0.47)'; c.lineWidth = 2; c.stroke();
    const tc = spec.textCol;
    setFont(d, F.barlow, 10.5); c.fillStyle = tc; c.textAlign = 'left'; c.textBaseline = 'middle';
    c.fillText('GRAPHIC  EQUALIZER', W * 0.10, H * 0.075);
    ledDot(d, W * 0.90, H * 0.075, true, 224, 70, 58);
    // plate
    rr(c, G.plateX, G.plateY, G.plateW, G.plateH, 7); c.fillStyle = mesa ? rgb(15, 15, 17) : rgb(20, 20, 22); c.fill();
    rr(c, G.plateX, G.plateY, G.plateW, G.plateH, 7); c.strokeStyle = mesa ? 'rgba(78,80,86,0.59)' : 'rgba(0,0,0,0.55)'; c.lineWidth = 1.4; c.stroke();
    const tT = G.tT, tB = G.tB, midY = (tT + tB) / 2;
    setFont(d, F.barlow, 8); c.textAlign = 'right'; c.textBaseline = 'middle'; c.fillStyle = mesa ? rgb(180, 182, 188) : rgb(160, 162, 170);
    c.fillText('+' + spec.db, G.faderL - W * 0.045, tT);
    c.fillText('0', G.faderL - W * 0.045, midY);
    c.fillText('-' + spec.db, G.faderL - W * 0.045, tB);
    if (!mesa) for (let s = 0; s <= 4; s++) { const yy = tT + (tB - tT) * s / 4;
      c.beginPath(); c.moveTo(G.faderL - W * 0.04, yy); c.lineTo(G.faderL + G.faderW, yy);
      c.strokeStyle = 'rgba(255,255,255,0.16)'; c.lineWidth = 1; c.stroke(); }
    for (let i = 0; i < G.n; i++) {
      // Resolve by band index OR by the band's frequency name ("50","100",…)
      // — the engine names EQ params by frequency, so the saved/restored
      // values may be keyed either way.
      let v = 0.5;
      if (values) { if (values[i] != null) v = values[i]; else if (values[spec.bands[i]] != null) v = values[spec.bands[i]]; }
      const cx = G.colX(i), hy = G.valToY(v);
      setFont(d, F.barlow, 8); c.textAlign = 'center'; c.textBaseline = 'top';
      c.fillStyle = mesa ? rgb(200, 202, 208) : rgb(182, 184, 192);
      c.fillText(spec.bands[i], cx, G.plateY + 4); c.textBaseline = 'middle';
      if (mesa) {
        rr(c, cx - 3.5, tT, 7, tB - tT, 3.5); c.fillStyle = rgb(226, 227, 231); c.fill();
        rr(c, cx - 11, hy - 7, 22, 14, 3); c.fillStyle = rgb(22, 22, 24); c.fill();
        rr(c, cx - 11, hy - 7, 22, 14, 3); c.strokeStyle = 'rgba(0,0,0,0.67)'; c.lineWidth = 1; c.stroke();
        c.fillStyle = rgb(150, 152, 158); c.fillRect(cx - 9, hy - 0.9, 18, 1.8);
      } else {
        rr(c, cx - 2, tT, 4, tB - tT, 2); c.fillStyle = rgb(46, 48, 56); c.fill();
        const cp = c.createLinearGradient(0, hy - 6, 0, hy + 6); cp.addColorStop(0, rgb(236, 238, 242)); cp.addColorStop(1, rgb(150, 153, 160));
        rr(c, cx - 10, hy - 6, 20, 12, 2.5); c.fillStyle = cp; c.fill();
        rr(c, cx - 10, hy - 6, 20, 12, 2.5); c.strokeStyle = rgb(70, 72, 78); c.lineWidth = 1; c.stroke();
        c.fillStyle = rgb(60, 62, 68); c.fillRect(cx - 8, hy - 0.7, 16, 1.4);
      }
    }
    if (mesa) {
      rr(c, W * 0.30, H * 0.66, W * 0.40, H * 0.135, 5); c.fillStyle = rgb(236, 237, 240); c.fill();
      textC(d, W * 0.5, H * 0.727, F.crete, 22, rgb(20, 20, 24), spec.label);
      c.beginPath(); c.arc(W * 0.5, H * 0.905, 15, 0, 7); c.fillStyle = rgb(150, 153, 159); c.fill();
      c.strokeStyle = rgb(90, 92, 98); c.lineWidth = 2; c.stroke();
    } else {
      const tx = m + 4, tw = W - 2 * m - 8, tyTop = H * 0.49, tBot = H - m - 6;
      const tre = c.createLinearGradient(0, tyTop, 0, tBot);
      tre.addColorStop(0, rgb(cl(R - 2), cl(Gc - 2), cl(B - 2))); tre.addColorStop(1, rgb(cl(R - 14), cl(Gc - 14), cl(B - 14)));
      rr(c, tx, tyTop, tw, tBot - tyTop, 12); c.fillStyle = tre; c.fill();
      rr(c, tx, tyTop, tw, 10, 12); c.fillStyle = 'rgba(255,255,255,0.08)'; c.fill();
      rr(c, tx, tyTop, tw, tBot - tyTop, 12); c.strokeStyle = 'rgba(0,0,0,0.47)'; c.lineWidth = 1.6; c.stroke();
      const padT = tyTop + (tBot - tyTop) * 0.50, padBot = tBot - 9; rr(c, tx + 12, padT, tw - 24, padBot - padT, 9); c.fillStyle = rgb(20, 20, 22); c.fill();
      chiefName(d, spec.name1 || spec.label, spec.name2, spec.code, 0.075, -0.04);  // dy down (names), codeDy up (code)
      chiefBadge(d, padT, padBot);                                            // CHIEF badge on the pad
    }
  }
  function eqSpec(o) {
    const lum = 0.299 * o.col[0] + 0.587 * o.col[1] + 0.114 * o.col[2];
    const spec = { w: o.w, h: o.h, mesa: o.style === 1, bands: o.bands, db: o.db, col: o.col,
      label: o.label || '', name1: o.name1, name2: o.name2, code: o.code,
      textCol: lum > 140 ? rgb(34, 34, 38) : rgb(232, 234, 240),
      eq: true, knobs: [], ptr: rgb(0, 0, 0), tick: rgb(0, 0, 0) };
    spec.draw = (d, values) => eqDraw(d, spec, values);
    return spec;
  }
  P.ge8     = eqSpec({ w: 320, h: 500, style: 0, db: 15, col: [188, 190, 186], label: 'Equalizer', code: 'GE-8',
                       bands: ['50', '100', '200', '400', '800', '1600', '3200', '6400'] });
  P.geb8 = eqSpec({ w: 320, h: 500, style: 0, db: 15, col: [210, 206, 194], name1: 'Bass', name2: 'Equalizer', code: 'GEB-8',
                       bands: ['30', '75', '185', '460', '1100', '2700', '6800', '16000'] });
  P.eq5     = eqSpec({ w: 440, h: 300, style: 1, db: 15, col: [30, 30, 33], label: '5-BAND GRAPHIC',
                       bands: ['63', '250', '750', '2200', '5700'] });

  // ════════ Bass-amp VST faces (mirror vst/src/amps/*) ════════════════════════

  // PeeBee T-Minus — Peavey T-Max two-channel bass system (parody). White panel,
  // dual gain (Tube Pre/Post + SS Pre), shelving Low/High, 7-band graphic EQ,
  // biamp Balance/X-Over, Master. ids 0..18 = the VST enum order.
  P.peebeetminus = { w:1100, h:280,
    knobs:[
      {id:0,cx:.115,cy:.50,r:.028,style:'pointer',cap:[24,24,26]},
      {id:1,cx:.185,cy:.50,r:.028,style:'pointer',cap:[24,24,26]},
      {id:2,cx:.270,cy:.50,r:.028,style:'pointer',cap:[24,24,26]},
      {id:4,cx:.395,cy:.40,r:.023,style:'pointer',cap:[24,24,26]},
      {id:3,cx:.395,cy:.62,r:.023,style:'pointer',cap:[24,24,26]},
      {id:5,cx:.773,cy:.50,r:.028,style:'pointer',cap:[24,24,26]},
      {id:6,cx:.853,cy:.50,r:.028,style:'pointer',cap:[24,24,26]},
      {id:7,cx:.933,cy:.50,r:.031,style:'pointer',cap:[24,24,26]}],
    faders:[{id:8,cx:.455,y0:.30,y1:.68},{id:9,cx:.490,y0:.30,y1:.68},{id:10,cx:.525,y0:.30,y1:.68},
      {id:11,cx:.560,y0:.30,y1:.68},{id:12,cx:.595,y0:.30,y1:.68},{id:13,cx:.630,y0:.30,y1:.68},{id:14,cx:.665,y0:.30,y1:.68}],
    switches:[{id:15,cx:.045,cy:.74,hs:.014,dark:true},{id:16,cx:.340,cy:.37,hs:.014,dark:true},
      {id:17,cx:.340,cy:.64,hs:.014,dark:true},{id:18,cx:.710,cy:.43,hs:.014,dark:true}],
    names:['Tube Pre','Tube Post','Solid State','Shelving Low','Shelving High','Balance','X-Over','Master','40 Hz','100 Hz','250 Hz','625 Hz','1.6 kHz','4 kHz','10 kHz','Active','Channel Sel','Combine','Graphic In'],
    tick:rgb(120,122,128), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(28,30,36), dim=rgb(86,90,98);
      box(d, 20,22,26, true);
      const PL=.012*W,PT=.04*H,PW=.976*W,PH=.92*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(238,241,245)); pg.addColorStop(1,rgb(204,210,218));
      rr(c,PL,PT,PW,PH,7); c.fillStyle=pg; c.fill(); rr(c,PL,PT,PW,PH,7); c.strokeStyle=rgb(118,122,128); c.lineWidth=1.5; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // header: brand + title + subtitle (stacked top-left)
      textC(d,.030*W,.135*H,F.crete,24,rgb(26,28,34),'PeeBee','left');
      textC(d,.200*W,.130*H,F.bebas,26,rgb(24,26,32),'T-MINUS','left');
      textC(d,.030*W,.230*H,F.barlow,9.5,dim,'TWO CHANNEL BASS SYSTEM','left');
      // input + Active
      c.beginPath();c.arc(.045*W,.50*H,11,0,7);c.fillStyle=rgb(16,16,18);c.fill();c.strokeStyle=rgb(90,92,98);c.lineWidth=1.6;c.stroke();
      lab(.045,.355,9.5,'INPUT'); lab(.045,.86,8.5,'ACTIVE');
      // TUBE + SOLID STATE group headers (raised above the knobs)
      ledDot(d,.082*W,.305*H,true,60,210,80); lab(.150,.305,10,'TUBE');
      lab(.115,.70,9,'PRE'); lab(.185,.70,9,'POST');
      ledDot(d,.212*W,.305*H,true,220,200,40); lab(.268,.305,10,'SOLID STATE');
      lab(.270,.70,9,'PRE');
      // channel switches (spaced between SS Pre and Shelving)
      lab(.340,.275,8.5,'TUBE/SS'); lab(.340,.77,8.5,'COMBINE');
      // shelving (stacked pair)
      lab(.395,.205,9.5,'SHELVING',dim); lab(.437,.40,8.5,'HI'); lab(.437,.62,8.5,'LO');
      // graphic EQ
      lab(.560,.205,9.5,'GRAPHIC EQ   (±15 dB)',dim); lab(.710,.31,8.5,'GRAPHIC');
      [['40',.455],['100',.490],['250',.525],['625',.560],['1.6k',.595],['4k',.630],['10k',.665]].forEach(b=>lab(b[1],.745,7.5,b[0],dim));
      // right cluster labels
      [[.773,'BALANCE'],[.853,'X-OVER'],[.933,'MASTER']].forEach(k=>lab(k[0],.70,9,k[1]));
      // power LED
      c.beginPath();c.arc(.965*W,.135*H,5,0,7);c.fillStyle=rgb(220,40,30);c.fill(); } };

  // Bender Fumble 800 — Fender Rumble 800 modern Class-D head (parody). Black
  // face, cream knobs: Gain, Bright/Contour/Vintage buttons, Overdrive Drive+
  // Level, 4-band EQ (Bass/Low Mid/High Mid/Treble), Master. ids 0..10.
  P.benderfumble800 = { w:920, h:200,
    knobs:[
      {id:0,cx:.115,cy:.48,r:.038,style:'cream'},
      {id:1,cx:.300,cy:.48,r:.038,style:'cream'},
      {id:2,cx:.390,cy:.48,r:.038,style:'cream'},
      {id:3,cx:.475,cy:.48,r:.038,style:'cream'},
      {id:4,cx:.560,cy:.48,r:.038,style:'cream'},
      {id:5,cx:.645,cy:.48,r:.038,style:'cream'},
      {id:6,cx:.730,cy:.48,r:.038,style:'cream'},
      {id:7,cx:.845,cy:.48,r:.038,style:'cream'}],
    switches:[{id:8,cx:.190,cy:.28,hs:.010,dark:true},{id:9,cx:.190,cy:.50,hs:.010,dark:true},{id:10,cx:.190,cy:.72,hs:.010,dark:true}],
    names:['Gain','Drive','Level','Bass','Low Mid','High Mid','Treble','Master','Bright','Contour','Vintage'],
    tick:rgb(120,116,104), ptr:rgb(40,38,34),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(232,233,236), dim=rgb(150,152,156);
      c.fillStyle=rgb(180,182,186); c.fillRect(0,0,W,H);
      rr(c,4,4,W-8,H-8,5); c.fillStyle=rgb(20,20,22); c.fill(); rr(c,4,4,W-8,H-8,5); c.strokeStyle=rgb(60,60,64); c.lineWidth=1.2; c.stroke();
      const lab=(cx,y,sz,t,col,al)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t,al);
      c.beginPath();c.arc(.045*W,.48*H,9,0,7);c.fillStyle=rgb(40,40,44);c.fill();c.strokeStyle=rgb(150,152,156);c.lineWidth=1.5;c.stroke();
      c.beginPath();c.arc(.045*W,.48*H,3.5,0,7);c.fillStyle=rgb(16,16,18);c.fill();
      lab(.045,.30,9,'INPUT');
      [[.115,'GAIN'],[.300,'DRIVE'],[.390,'LEVEL'],[.475,'BASS'],[.560,'LOW MID'],[.645,'HIGH MID'],[.730,'TREBLE'],[.845,'MASTER']].forEach(k=>lab(k[0],.20,9,k[1]));
      [[.28,'BRIGHT'],[.50,'CONTOUR'],[.72,'VINTAGE']].forEach(b=>lab(.208,b[0],8.5,b[1],ink,'left'));
      c.beginPath();c.arc(.345*W,.255*H,4,0,7);c.fillStyle=rgb(80,28,26);c.fill();
      c.strokeStyle=dim; c.lineWidth=1.2;
      c.beginPath(); c.moveTo(.268*W,.74*H); c.lineTo(.268*W,.78*H); c.lineTo(.422*W,.78*H); c.lineTo(.422*W,.74*H); c.stroke();
      lab(.345,.84,8.5,'OVERDRIVE',rgb(210,212,216));
      c.beginPath(); c.moveTo(.445*W,.74*H); c.lineTo(.445*W,.78*H); c.lineTo(.760*W,.78*H); c.lineTo(.760*W,.74*H); c.stroke();
      lab(.6025,.84,8.5,'EQUALIZATION',rgb(210,212,216));
      c.beginPath();c.arc(.945*W,.30*H,5,0,7);c.fillStyle=rgb(230,40,30);c.fill();
      textC(d,(W-16),(H-18),F.crete,19,rgb(236,237,240),'Fumble 800','right'); } };

  // Aiden GT-300/550/880 — Eden WT Valve-Tech hybrid preamp (parody). Gold/tan
  // panel: Gain (blue) + Enhance (white) + Bass (red) + 3-band semi-parametric
  // EQ (Freq row over Level row) + Treble (red) + Master (blue).
  const EDEN_BLUE=[70,120,200], EDEN_WHITE=[232,232,228], EDEN_RED=[200,54,46], EDEN_GREY=[150,150,150];
  function edenFace(d, vals, model, levLbl){ const {ctx:c,W,H}=d; const ink=rgb(40,30,18), dim=rgb(60,50,32);
    box(d, 28,26,22, true);
    const PL=.012*W,PT=.04*H,PW=.976*W,PH=.92*H;
    const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(206,176,96)); pg.addColorStop(1,rgb(176,146,72));
    rr(c,PL,PT,PW,PH,6); c.fillStyle=pg; c.fill(); rr(c,PL,PT,PW,PH,6); c.strokeStyle=rgb(180,52,44); c.lineWidth=2; c.stroke();
    const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
    textC(d,PL+60,PT+14,F.barlow,12.5,rgb(180,52,44),'Valve-Tech Series','left');
    textC(d,PL+60,PT+32,F.barlow,10.5,ink,'Twin Triode Tube Pre Amplifier   '+model,'left');
    textC(d,(PL+PW)-12,PT+16,F.crete,20.5,rgb(28,24,18),'Aiden','right');
    const jack=(x,y)=>{ c.beginPath();c.arc(x*W,y*H,8,0,7);c.fillStyle=rgb(16,16,18);c.fill();c.strokeStyle=rgb(60,50,36);c.lineWidth=1.4;c.stroke(); };
    jack(.035,.66); lab(.035,.79,8.5,'INPUT'); jack(.955,.66); lab(.955,.79,8,'PHONES');
    lab(.560,.93,9.5,'Semi-Parametric Bass Equalizer');
    [[.460,'FREQ'],[.560,'FREQ'],[.660,'FREQ']].forEach(k=>lab(k[0],.44,9.5,k[1]));
    [[.105,'GAIN'],[.195,'ENHANCE'],[.320,'BASS'],[.460,levLbl[0]],[.560,levLbl[1]],[.660,levLbl[2]],[.790,'TREBLE'],[.875,'MASTER']].forEach(k=>lab(k[0],.82,9.5,k[1])); }
  const EDEN_KNOBS=[
    {id:3,cx:.460,cy:.30,r:.024,style:'pointer',cap:EDEN_RED},
    {id:5,cx:.560,cy:.30,r:.024,style:'pointer',cap:EDEN_RED},
    {id:7,cx:.660,cy:.30,r:.024,style:'pointer',cap:EDEN_RED},
    {id:0,cx:.105,cy:.66,r:.026,style:'pointer',cap:EDEN_BLUE},
    {id:1,cx:.195,cy:.66,r:.026,style:'pointer',cap:EDEN_WHITE},
    {id:2,cx:.320,cy:.66,r:.024,style:'pointer',cap:EDEN_RED},
    {id:4,cx:.460,cy:.66,r:.024,style:'pointer',cap:EDEN_RED},
    {id:6,cx:.560,cy:.66,r:.024,style:'pointer',cap:EDEN_RED},
    {id:8,cx:.660,cy:.66,r:.024,style:'pointer',cap:EDEN_RED},
    {id:9,cx:.790,cy:.66,r:.024,style:'pointer',cap:EDEN_RED},
    {id:10,cx:.875,cy:.66,r:.026,style:'pointer',cap:EDEN_BLUE}];
  const EDEN_NAMES=['Gain','Enhance','Bass','EQ1 Freq','EQ1 Level','EQ2 Freq','EQ2 Level','EQ3 Freq','EQ3 Level','Treble','Master'];
  P.aidengt300 = { w:1000, h:300, knobs:EDEN_KNOBS, names:EDEN_NAMES, tick:rgb(120,118,108), ptr:rgb(245,245,245),
    draw(d,vals){ edenFace(d,vals,'GT-300',['LEVEL','LEVEL','LEVEL']); } };
  P.aidengt550 = { w:1000, h:300, knobs:EDEN_KNOBS, names:EDEN_NAMES, tick:rgb(120,118,108), ptr:rgb(245,245,245),
    draw(d,vals){ edenFace(d,vals,'GT-550',['LOW','MID','HIGH']); } };

  // Aiden GT-880 — adds the bi-amp X-Over Freq + Balance + X-Over switch.
  P.aidengt880 = { w:1100, h:300,
    knobs:[
      {id:3,cx:.420,cy:.30,r:.022,style:'pointer',cap:EDEN_RED},
      {id:5,cx:.500,cy:.30,r:.022,style:'pointer',cap:EDEN_RED},
      {id:7,cx:.580,cy:.30,r:.022,style:'pointer',cap:EDEN_RED},
      {id:11,cx:.700,cy:.30,r:.022,style:'pointer',cap:EDEN_GREY},
      {id:0,cx:.095,cy:.66,r:.024,style:'pointer',cap:EDEN_BLUE},
      {id:1,cx:.170,cy:.66,r:.024,style:'pointer',cap:EDEN_WHITE},
      {id:2,cx:.280,cy:.66,r:.022,style:'pointer',cap:EDEN_RED},
      {id:4,cx:.420,cy:.66,r:.022,style:'pointer',cap:EDEN_RED},
      {id:6,cx:.500,cy:.66,r:.022,style:'pointer',cap:EDEN_RED},
      {id:8,cx:.580,cy:.66,r:.022,style:'pointer',cap:EDEN_RED},
      {id:9,cx:.675,cy:.66,r:.022,style:'pointer',cap:EDEN_RED},
      {id:10,cx:.775,cy:.66,r:.024,style:'pointer',cap:EDEN_BLUE},
      {id:12,cx:.860,cy:.66,r:.022,style:'pointer',cap:EDEN_BLUE}],
    switches:[{id:13,cx:.760,cy:.30,hs:.012,dark:true}],
    names:['Gain','Enhance','Bass','Low Freq','Low Level','Mid Freq','Mid Level','High Freq','High Level','Treble','Master','X-Over Freq','Balance','X-Over'],
    tick:rgb(120,118,108), ptr:rgb(245,245,245),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(40,30,18), dim=rgb(60,50,32);
      box(d, 30,28,22, true);
      const PL=.012*W,PT=.04*H,PW=.976*W,PH=.92*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(212,182,104)); pg.addColorStop(1,rgb(170,138,66));
      rr(c,PL,PT,PW,PH,6); c.fillStyle=pg; c.fill(); rr(c,PL,PT,PW,PH,6); c.strokeStyle=rgb(150,120,52); c.lineWidth=2; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      textC(d,PL+60,PT+14,F.barlow,10.5,ink,'Hybrid Bass Guitar Amplifier   Valve-Tech Series','left');
      textC(d,(PL+PW)-12,PT+14,F.bebas,20.5,rgb(28,24,18),'WORLD TOUR','right');
      textC(d,(PL+PW)-12,PT+34,F.barlow,12.5,rgb(170,40,34),'Aiden  GT-880','right');
      const jack=(x,y)=>{ c.beginPath();c.arc(x*W,y*H,8,0,7);c.fillStyle=rgb(16,16,18);c.fill();c.strokeStyle=rgb(60,50,36);c.lineWidth=1.4;c.stroke(); };
      jack(.033,.66); lab(.033,.79,8.5,'INPUT');
      lab(.500,.94,9.5,'Semi-Parametric Bass Equalizer');
      [[.420,'FREQ'],[.500,'FREQ'],[.580,'FREQ'],[.700,'X-OVER'],[.760,'ON']].forEach(k=>lab(k[0],.44,9,k[1]));
      [[.095,'GAIN'],[.170,'ENHANCE'],[.280,'BASS'],[.420,'LOW'],[.500,'MID'],[.580,'HIGH'],[.675,'TREBLE'],[.775,'MASTER'],[.860,'BALANCE']].forEach(k=>lab(k[0],.82,9,k[1])); } };

  // Lovolt 100 — Custom Hiwatt 100 (DR103, parody). Black face, Normal/Bright
  // vols + Bass/Treble/Middle + Presence + Master. ids 0..6.
  P.lovolt100 = { w:900, h:230,
    knobs:[
      {id:0,cx:.180,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:1,cx:.265,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:2,cx:.380,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:3,cx:.465,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:4,cx:.550,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:5,cx:.635,cy:.52,r:.026,style:'pointer',cap:[22,22,24]},
      {id:6,cx:.720,cy:.52,r:.026,style:'pointer',cap:[22,22,24]}],
    names:['Normal Vol','Bright Vol','Bass','Treble','Middle','Presence','Master Vol'],
    tick:rgb(150,152,158), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(214,216,220);
      box(d, 26,26,28, true);
      const lx=.020*W,ly=.10*H,lw=.135*W,lh=.24*H;
      rr(c,lx,ly,lw,lh,3); c.strokeStyle=rgb(210,212,216); c.lineWidth=1.6; c.stroke();
      textC(d,lx+lw*0.5,ly+lh*0.5,F.bebas,14,rgb(220,222,226),'LOVOLT');
      textC(d,.45*W,.10*H,F.bebas,13,rgb(210,212,216),'CUSTOM LOVOLT 100');
      [[.180,'NORMAL'],[.265,'BRIGHT'],[.380,'BASS'],[.465,'TREBLE'],[.550,'MIDDLE'],[.635,'PRESENCE'],[.720,'MASTER']].forEach(k=>textC(d,k[0]*W,.70*H,F.barlow,8.5,ink,k[1]));
      ledDot(d,.80*W,.50*H,true,220,40,36); } };

  // Silla Boogie 400 — Mesa/Boogie Bass 400+ (parody). Black face, Mesa 6-band
  // graphic EQ (faders) + Middle/Bass/Treble/Master/Vol2/Vol1 + pull switches.
  P.sillaboogiebass400 = { w:1000, h:300,
    knobs:[
      {id:2,cx:.300,cy:.76,r:.025,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.390,cy:.76,r:.025,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.480,cy:.76,r:.025,style:'pointer',cap:[20,20,22]},
      {id:5,cx:.580,cy:.76,r:.025,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.680,cy:.76,r:.025,style:'pointer',cap:[20,20,22]},
      {id:0,cx:.770,cy:.76,r:.025,style:'pointer',cap:[20,20,22]}],
    faders:[{id:6,cx:.530,y0:.28,y1:.47},{id:7,cx:.570,y0:.28,y1:.47},{id:8,cx:.610,y0:.28,y1:.47},
      {id:9,cx:.650,y0:.28,y1:.47},{id:10,cx:.690,y0:.28,y1:.47},{id:11,cx:.730,y0:.28,y1:.47}],
    switches:[{id:12,cx:.812,cy:.37,hs:.012,dark:true},{id:15,cx:.390,cy:.89,hs:.010,dark:true},
      {id:16,cx:.480,cy:.89,hs:.010,dark:true},{id:14,cx:.680,cy:.89,hs:.010,dark:true},{id:13,cx:.770,cy:.89,hs:.010,dark:true}],
    names:['Volume 1','Volume 2','Middle','Bass','Treble','Master','40 Hz','100 Hz','250 Hz','625 Hz','1560 Hz','3900 Hz','EQ In','Bright 1','Bright 2','Bass Shift','Treble Shift'],
    tick:rgb(150,152,156), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(210,212,216), dim=rgb(150,140,90), white=rgb(234,235,238);
      box(d, 24,24,26, false);
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // ── big logo across the top (name above the EQ) ──
      textC(d,.055*W,.135*H,F.anton,32,white,'SILLA/BOOGIE','left');
      textC(d,.510*W,.135*H,F.crete,30,white,'BASS 400+','left');
      // ── graphic-EQ recessed box (upper right) ──
      rr(c,.500*W,.24*H,.282*W,.26*H,5); c.fillStyle=rgb(14,14,16); c.fill();
      rr(c,.500*W,.24*H,.282*W,.26*H,5); c.strokeStyle=dim; c.lineWidth=1.6; c.stroke();
      [['40',.530],['100',.570],['250',.610],['625',.650],['1560',.690],['3900',.730]].forEach(b=>lab(b[1],.545,7,b[0],rgb(202,204,208)));
      lab(.835,.30,7.5,'EQ IN'); lab(.835,.44,7.5,'OUT');
      // ── bottom control strip ──
      rr(c,.025*W,.58*H,.950*W,.39*H,6); c.strokeStyle=rgb(96,94,88); c.lineWidth=1.4; c.stroke();
      [[.300,'MIDDLE'],[.390,'BASS'],[.480,'TREBLE'],[.580,'MASTER'],[.680,'VOLUME 2'],[.770,'VOLUME 1']].forEach(k=>lab(k[0],.65,8.5,k[1]));
      lab(.390,.945,6,'PULL SHIFT',dim); lab(.480,.945,6,'PULL SHIFT',dim);
      lab(.680,.945,6,'PULL BRIGHT',dim); lab(.770,.945,6,'PULL BRIGHT',dim);
      // ── power section + jacks (left to right) ──
      ledDot(d,.050*W,.76*H,true,60,130,230); lab(.050,.89,6,'SLO-BLO');
      const rocker=(x)=>{ const y=.76*H; rr(c,x*W-8,y-12,16,24,2); c.fillStyle=rgb(40,40,44); c.fill(); rr(c,x*W-8,y-12,16,24,2); c.strokeStyle=rgb(92,94,98); c.lineWidth=1; c.stroke(); };
      rocker(.092); rocker(.142); lab(.092,.89,6.5,'POWER'); lab(.142,.89,6.5,'STANDBY');
      const jack=(x,y)=>{ c.beginPath();c.arc(x*W,y*H,7,0,7);c.fillStyle=rgb(16,16,18);c.fill();c.strokeStyle=rgb(120,122,126);c.lineWidth=1.3;c.stroke(); };
      jack(.205,.72); jack(.205,.84); lab(.205,.645,6,'SEND'); lab(.205,.915,6,'RETURN');
      jack(.885,.72); jack(.885,.84); lab(.930,.72,6.5,'IN 1'); lab(.930,.84,6.5,'IN 2'); } };

  // Citrus AD200 — Orange AD200B (parody). Orange tolex + cream panel, CITRUS
  // bubble logo + AD200 + crest, black control strip: Power/Standby, Gain, an
  // orange tone section (Bass/Middle/Treble), Master, Passive/Active. ids 0..5.
  P.citrusad200 = { w:900, h:300,
    knobs:[
      {id:0,cx:.230,cy:.67,r:.044,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.395,cy:.70,r:.030,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.475,cy:.70,r:.030,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.555,cy:.70,r:.030,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.690,cy:.67,r:.044,style:'pointer',cap:[20,20,22]}],
    switches:[{id:5,cx:.885,cy:.70,hs:.011,dark:true}],
    names:['Gain','Bass','Middle','Treble','Master','Active'],
    tick:rgb(120,116,104), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; vals=vals||{};
      c.fillStyle=rgb(236,118,24); c.fillRect(0,0,W,H);                 // orange tolex
      rr(c,.035*W,.07*H,.930*W,.86*H,6); c.fillStyle=rgb(238,234,222); c.fill();   // cream panel
      rr(c,.035*W,.07*H,.930*W,.86*H,6); c.strokeStyle=rgb(150,146,134); c.lineWidth=1.4; c.stroke();
      textC(d,.075*W,.270*H,F.graffiti,98,rgb(24,22,20),'Citrus','left');
      textC(d,.620*W,.235*H,F.barlow,22,rgb(58,54,48),'AD200','left');
      textC(d,.620*W,.315*H,F.barlow,22,rgb(58,54,48),'BASS','left');
      textC(d,.620*W,.395*H,F.barlow,22,rgb(58,54,48),'MK II','left');
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||rgb(232,233,236),t);
      rr(c,.05*W,.46*H,.90*W,.46*H,5); c.fillStyle=rgb(18,18,20); c.fill();            // black strip
      rr(c,.350*W,.50*H,.255*W,.39*H,4); c.fillStyle=rgb(232,112,22); c.fill();        // orange tone section
      const rocker=(x)=>{ rr(c,x*W-8,.70*H-14,16,28,2); c.fillStyle=rgb(40,40,44); c.fill(); rr(c,x*W-8,.70*H-14,16,28,2); c.strokeStyle=rgb(90,92,96); c.lineWidth=1; c.stroke(); };
      rocker(.105); rocker(.150); lab(.105,.89,6.5,'POWER',rgb(206,150,40)); lab(.150,.89,6.5,'STANDBY',rgb(206,150,40));
      ledDot(d,.078*W,.70*H,true,236,140,30);
      [[.230,'GAIN'],[.690,'MASTER']].forEach(k=>lab(k[0],.86,11.5,k[1]));
      [[.395,'BASS'],[.475,'MIDDLE'],[.555,'TREBLE']].forEach(k=>lab(k[0],.86,9,k[1]));
      const jack=(x,y)=>{ c.beginPath();c.arc(x*W,y*H,8,0,7);c.fillStyle=rgb(16,16,18);c.fill();c.strokeStyle=rgb(120,122,126);c.lineWidth=1.4;c.stroke(); };
      jack(.825,.62); jack(.825,.80);
      const act=(vals[5]||0)>0.5;
      lab(.885,.59,7,'PASSIVE',act?rgb(150,150,154):rgb(236,200,120));
      lab(.885,.83,7,'ACTIVE',act?rgb(236,200,120):rgb(150,150,154)); } };

  // Tracer V8 — Trace Elliot V-Type V8 (parody). Black head, green V8 badge +
  // V-Type logo; PREAMP (Gain I+Bright, Gain II+Pull, Level), TONE (Bass+Deep,
  // Middle+Shift, Treble), COMPRESSOR (On/Off + Level), Master. ids 0..13.
  P.tracerv8 = { w:960, h:280,
    knobs:[
      {id:0,cx:.150,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:1,cx:.225,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:2,cx:.300,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:3,cx:.405,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:4,cx:.480,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:5,cx:.555,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:6,cx:.685,cy:.55,r:.026,style:'pointer',cap:[22,22,24]},
      {id:7,cx:.775,cy:.55,r:.029,style:'pointer',cap:[22,22,24]}],
    switches:[{id:8,cx:.055,cy:.55,hs:.011,dark:true},{id:9,cx:.150,cy:.82,hs:.010,dark:true},
      {id:10,cx:.225,cy:.82,hs:.010,dark:true},{id:11,cx:.405,cy:.82,hs:.010,dark:true},
      {id:12,cx:.480,cy:.82,hs:.010,dark:true},{id:13,cx:.615,cy:.55,hs:.011,dark:true}],
    names:['Gain I','Gain II','Level','Bass','Middle','Treble','Comp Level','Master','Active','Bright','Gain II Pull','Deep','Mid Shift','Compressor'],
    tick:rgb(150,152,156), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const grn=rgb(120,200,60), dim=rgb(150,152,156), ink=rgb(206,208,212);
      box(d, 22,22,24, false);
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // section headers + brackets
      lab(.055,.20,8.5,'INPUTS',dim); lab(.225,.20,8.5,'PREAMP',dim); lab(.480,.20,8.5,'TONE CONTROL',dim); lab(.650,.20,8.5,'COMPRESSOR',dim);
      c.strokeStyle=rgb(80,82,86); c.lineWidth=1.2;
      c.beginPath(); c.moveTo(.135*W,.27*H); c.lineTo(.330*W,.27*H); c.stroke();
      c.beginPath(); c.moveTo(.390*W,.27*H); c.lineTo(.580*W,.27*H); c.stroke();
      [[.150,'GAIN I'],[.225,'GAIN II'],[.300,'LEVEL'],[.405,'BASS'],[.480,'MIDDLE'],[.555,'TREBLE'],[.685,'LEVEL'],[.775,'MASTER']].forEach(k=>lab(k[0],.69,8,k[1]));
      // V8 badge + V-Type logo (green)
      const vx=.870*W, vy=.42*H;
      rr(c,vx-22,vy-16,44,32,3); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,vx-22,vy-16,44,32,3); c.strokeStyle=grn; c.lineWidth=1.8; c.stroke();
      textC(d,vx,vy,F.anton,24,grn,'V8');
      textC(d,.835*W,.78*H,F.crete,22,grn,'V-Type','left');
      lab(.835,.90,7,'400W ALL VALVE',dim); } };

  // SBR Super Redhead — SWR Super Redhead (parody). Red rack face, SBR circle
  // logo; Gain (+Turbo), Aural Enhancer, Bass, semi-parametric Mid (Level+Freq),
  // Treble (+Transparency), Master. ids 0..9.
  P.sbrredhead = { w:980, h:200,
    knobs:[
      {id:0,cx:.100,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.190,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.280,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.370,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.460,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:5,cx:.550,cy:.48,r:.030,style:'pointer',cap:[20,20,22]},
      {id:6,cx:.645,cy:.48,r:.033,style:'pointer',cap:[20,20,22]}],
    switches:[{id:7,cx:.040,cy:.48,hs:.010,dark:true},{id:8,cx:.100,cy:.80,hs:.010,dark:true},{id:9,cx:.550,cy:.80,hs:.010,dark:true}],
    names:['Gain','Aural Enhancer','Bass','Mid Level','Mid Freq','Treble','Master','Active','Turbo','Transparency'],
    tick:rgb(150,60,60), ptr:rgb(246,246,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(245,235,235);
      c.fillStyle=rgb(40,8,8); c.fillRect(0,0,W,H);
      const pg=c.createLinearGradient(0,5,0,H-5); pg.addColorStop(0,rgb(214,34,34)); pg.addColorStop(1,rgb(176,22,22));
      rr(c,5,5,W-10,H-10,6); c.fillStyle=pg; c.fill(); rr(c,5,5,W-10,H-10,6); c.strokeStyle=rgb(120,16,16); c.lineWidth=1.5; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      textC(d,21,16,F.barlow,8.5,rgb(250,235,235),'ALL TUBE PREAMP / 350 WATT POWER AMP','left');
      for(let i=0;i<2;++i){ const jy=H*(0.35+i*0.26); c.beginPath(); c.arc(.040*W,jy,7,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(120,30,30); c.lineWidth=1.4; c.stroke(); }
      lab(.040,.72,6.5,'INPUTS');
      lab(.415,.20,7.5,'MID-RANGE');
      c.strokeStyle=rgb(120,20,20); c.lineWidth=1.2; c.beginPath(); c.moveTo(.345*W,.275*H); c.lineTo(.485*W,.275*H); c.stroke();
      [[.100,'GAIN'],[.190,'AURAL'],[.280,'BASS'],[.370,'LEVEL'],[.460,'FREQ'],[.550,'TREBLE'],[.645,'MASTER']].forEach(k=>lab(k[0],.68,8,k[1]));
      lab(.100,.90,6,'TURBO'); lab(.550,.90,6,'TRANSP');
      // SBR circle logo
      const lx=.875*W, ly=.50*H, lr=27;
      c.beginPath(); c.arc(lx,ly,lr,0,7); c.fillStyle=rgb(238,232,232); c.fill();
      c.beginPath(); c.arc(lx,ly,lr,0,7); c.strokeStyle=rgb(120,16,16); c.lineWidth=2; c.stroke();
      textC(d,lx,ly,F.anton,20,rgb(190,24,24),'SBR');
      lab(.875,.13,7,'SUPER REDHEAD'); lab(.875,.86,7,'INTEGRATED BASS SYSTEM'); } };

  // Dustup CDN — Ashdown ABM EVO (parody). Blue-grey face, two VU meters; Input,
  // Bass/Middle/Treble with a 6-band graphic EQ between them, Valve Drive/Sub/
  // Comp, Output, and Active/Shape/EQ/Sub/Comp switches. ids 0..18.
  // Dustup CDN — Ashdown ABM EVO V (parody), laid out 1:1 with the real panel:
  // top row INPUT · [VU] · BASS ·[100/180/340]· MIDDLE ·[1.3k/3.6k/5k]· TREBLE ·
  // [VU] · OUTPUT (the 6 graphic-EQ faders are INTERSPERSED between the tone
  // knobs, two VUs flank them); bottom row VALVE DRIVE · COMP · SUB HARMONICS
  // aligned under Bass/Middle/Treble, with the input jack + Passive/Active +
  // Shape on the far left and the EQ/Comp/Sub defeat buttons in the gaps. ids 0..18.
  P.dustupcdn = { w:1100, h:280,
    knobs:[
      {id:0,cx:.075,cy:.37,r:.030,style:'pointer',cap:[16,16,18]},
      {id:1,cx:.345,cy:.37,r:.034,style:'pointer',cap:[16,16,18]},
      {id:2,cx:.545,cy:.37,r:.034,style:'pointer',cap:[16,16,18]},
      {id:3,cx:.745,cy:.37,r:.034,style:'pointer',cap:[16,16,18]},
      {id:4,cx:.345,cy:.71,r:.034,style:'pointer',cap:[16,16,18]},
      {id:5,cx:.745,cy:.71,r:.034,style:'pointer',cap:[16,16,18]},
      {id:6,cx:.545,cy:.71,r:.034,style:'pointer',cap:[16,16,18]},
      {id:7,cx:.945,cy:.37,r:.030,style:'pointer',cap:[16,16,18]}],
    faders:[{id:8,cx:.420,y0:.18,y1:.45},{id:9,cx:.450,y0:.18,y1:.45},{id:10,cx:.480,y0:.18,y1:.45},
      {id:11,cx:.620,y0:.18,y1:.45},{id:12,cx:.650,y0:.18,y1:.45},{id:13,cx:.680,y0:.18,y1:.45}],
    switches:[{id:14,cx:.110,cy:.74,hs:.009,dark:true},{id:15,cx:.110,cy:.60,hs:.009,dark:true},
      {id:16,cx:.448,cy:.71,hs:.009,dark:true},{id:17,cx:.690,cy:.71,hs:.009,dark:true},{id:18,cx:.622,cy:.71,hs:.009,dark:true}],
    names:['Input','Bass','Middle','Treble','Valve Drive','Sub Harmonics','Comp','Output','100 Hz','180 Hz','340 Hz','1.3 kHz','3.6 kHz','5 kHz','Active','Shape','EQ In','Sub','Compressor'],
    tick:rgb(90,100,110), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(28,36,44), dim=rgb(64,76,86);
      box(d, 28,28,30, false);
      const PL=.012*W,PT=.04*H,PW=.976*W,PH=.92*H;
      const pg=c.createLinearGradient(0,PT,0,PT+PH); pg.addColorStop(0,rgb(196,212,220)); pg.addColorStop(1,rgb(150,166,178));
      rr(c,PL,PT,PW,PH,7); c.fillStyle=pg; c.fill(); rr(c,PL,PT,PW,PH,7); c.strokeStyle=rgb(90,100,110); c.lineWidth=1.5; c.stroke();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      // two analog VU meters flanking the EQ section (left after Input, right before Output)
      const vu=(cx)=>{ const w=72,h=40,x=cx*W-w/2,y=.37*H-h/2; rr(c,x,y,w,h,3); c.fillStyle=rgb(240,212,98); c.fill(); rr(c,x,y,w,h,3); c.strokeStyle=rgb(18,18,20); c.lineWidth=2; c.stroke();
        c.strokeStyle=rgb(60,60,64); c.lineWidth=1; for(let i=-2;i<=3;++i){ const a=-0.5+i*0.18; c.beginPath(); c.moveTo(x+w*0.5,y+h-5); c.lineTo(x+w*0.5+Math.sin(a)*22,y+h-5-Math.cos(a)*22); c.stroke(); }
        c.strokeStyle=rgb(40,40,44); c.lineWidth=1.6; c.beginPath(); c.moveTo(x+w*0.5,y+h-5); c.lineTo(x+w*0.5+Math.sin(0.32)*20,y+h-5-Math.cos(0.32)*20); c.stroke();
        c.fillStyle=rgb(60,60,64); textC(d,x+w*0.5,y+h-7,F.barlow,6,rgb(60,60,64),'VU'); };
      vu(.180); vu(.840);
      // parody wordmark where the ABM EVO badge sits (between left VU and Bass)
      textC(d,.262*W,.30*H,F.barlow,15,ink,'Dustup'); textC(d,.262*W,.43*H,F.barlow,11,dim,'CDN · EVO V');
      // top tone-knob labels ABOVE (as on the real panel) + Input/Output
      [[.075,'INPUT'],[.345,'BASS'],[.545,'MIDDLE'],[.745,'TREBLE'],[.945,'OUTPUT']].forEach(k=>lab(k[0],.115,9,k[1]));
      // graphic-EQ frequency labels under each fader
      [['100',.420],['180',.450],['340',.480],['1.3k',.620],['3.6k',.650],['5k',.680]].forEach(b=>lab(b[1],.50,6.5,b[0],dim));
      // input jack + far-left switch group
      c.beginPath(); c.arc(.045*W,.71*H,9,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(80,90,100); c.lineWidth=1.5; c.stroke();
      lab(.045,.86,6.5,'INPUT'); lab(.150,.60,6,'SHAPE'); lab(.150,.74,6,'ACTIVE');
      // bottom section labels (one row, like the panel): aligned to their controls
      lab(.345,.92,8.5,'VALVE DRIVE'); lab(.448,.82,7,'EQ'); lab(.560,.92,8.5,'COMP'); lab(.745,.92,8.5,'SUB HARMONICS'); } };

  // Electric B600F — Acoustic B600H (parody). Black face + cyan accent: Passive/
  // Active + Mute, PREAMP (Gain/Volume), NOTCH (Freq+On), 6-band EQ (40/120/350/
  // 800/2k/5k), 'electric' / B600F wordmarks. ids 0..11.
  P.electricb600f = { w:1100, h:200,
    knobs:[
      {id:0,cx:.180,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:1,cx:.255,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:2,cx:.330,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:3,cx:.455,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:4,cx:.527,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:5,cx:.599,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:6,cx:.671,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:7,cx:.743,cy:.50,r:.027,style:'pointer',cap:[18,18,20]},
      {id:8,cx:.815,cy:.50,r:.027,style:'pointer',cap:[18,18,20]}],
    switches:[{id:9,cx:.110,cy:.46,hs:.009,dark:true},{id:10,cx:.130,cy:.72,hs:.009,dark:true},{id:11,cx:.388,cy:.50,hs:.009,dark:true}],
    names:['Gain','Volume','Notch Freq','40 Hz','120 Hz','350 Hz','800 Hz','2 kHz','5 kHz','Active','Mute','Notch'],
    tick:rgb(40,180,205), ptr:rgb(80,200,220),
    draw(d,vals){ const {ctx:c,W,H}=d; const ink=rgb(210,212,216), cyan=rgb(40,180,205);
      box(d, 22,22,24, false);
      const PL=.012*W,PT=.05*H,PW=.976*W,PH=.90*H;
      rr(c,PL,PT,PW,PH,6); c.fillStyle=rgb(20,20,22); c.fill(); rr(c,PL,PT,PW,PH,6); c.strokeStyle=rgb(50,52,56); c.lineWidth=1.4; c.stroke();
      c.fillStyle=cyan; c.fillRect(PL,PT+8,PW,2); c.fillRect(PL,PT+PH-10,PW,2);
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||ink,t);
      for(let i=0;i<2;++i){ const jx=W*(0.045+i*0.035); c.beginPath(); c.arc(jx,H*0.50,7,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(80,90,100); c.lineWidth=1.4; c.stroke(); }
      lab(.062,.34,6,'INPUT',rgb(150,200,210)); lab(.110,.34,6,'ACTIVE',rgb(150,200,210)); lab(.130,.62,6,'MUTE',rgb(150,200,210));
      lab(.218,.78,7.5,'PREAMP',rgb(150,200,210)); lab(.635,.78,7.5,'EQUALIZER',rgb(150,200,210)); lab(.388,.64,6,'NOTCH',rgb(150,200,210));
      [[.180,'GAIN'],[.255,'VOLUME'],[.330,'FREQ'],[.455,'40'],[.527,'120'],[.599,'350'],[.671,'800'],[.743,'2K'],[.815,'5K']].forEach(k=>lab(k[0],.24,8,k[1]));
      textC(d,PL+16,(PT+PH)-4,F.barlow,18,rgb(228,230,234),'electric','left');
      textC(d,(PL+PW)-60,H*0.5,F.barlow,20,rgb(228,230,234),'B600F','right');
      c.beginPath(); c.arc(.945*W,H*0.5,4.5,0,7); c.fillStyle=rgb(40,90,220); c.fill(); } };

  // Citrus Big Tremor — Orange Tiny Terror (parody). White tolex head, cream
  // panel with the 'Citrus' bubble logo + BIG TREMOR + crest, a black/orange
  // control strip (POWER · OUTPUT · VOLUME · TONE · GAIN · GUITAR), Volume/Tone/
  // Gain knobs, the 15W/7W Half-power toggle, a red pilot jewel + input jack.
  P.citrusbigtremor = { w:1000, h:300,
    knobs:[
      {id:0,cx:.470,cy:.79,r:.032,style:'pointer',cap:[18,18,20]},
      {id:1,cx:.600,cy:.79,r:.032,style:'pointer',cap:[18,18,20]},
      {id:2,cx:.730,cy:.79,r:.032,style:'pointer',cap:[18,18,20]}],
    switches:[{id:3,cx:.185,cy:.78,hs:.012,dark:true}],
    names:['Volume','Tone','Gain','Half Power'],
    tick:rgb(120,120,124), ptr:rgb(20,20,22),
    draw(d,vals){ const {ctx:c,W,H}=d; vals=vals||{};
      c.fillStyle=rgb(228,228,224); c.fillRect(0,0,W,H);                          // white tolex
      rr(c,.030*W,.05*H,.940*W,.42*H,6); c.fillStyle=rgb(240,239,234); c.fill();  // cream upper panel
      rr(c,.030*W,.05*H,.940*W,.42*H,6); c.strokeStyle=rgb(150,148,140); c.lineWidth=1.4; c.stroke();
      textC(d,.065*W,.245*H,F.graffiti,86,rgb(22,20,18),'Citrus','left');         // bubble logo
      textC(d,.585*W,.175*H,F.bebas,30,rgb(40,38,34),'BIG','left');
      textC(d,.585*W,.300*H,F.bebas,30,rgb(40,38,34),'TREMOR','left');
      rr(c,.855*W,.10*H,.085*W,.30*H,3); c.fillStyle=rgb(232,120,28); c.fill();   // crest badge
      rr(c,.855*W,.10*H,.085*W,.30*H,3); c.strokeStyle=rgb(150,80,20); c.lineWidth=1; c.stroke();
      // black control strip with an orange band + white icon labels
      const SY=.50*H, SH=.50*H;
      rr(c,.030*W,SY,.940*W,SH-.04*H,5); c.fillStyle=rgb(18,18,20); c.fill();
      c.fillStyle=rgb(232,120,28); c.fillRect(.030*W,.55*H,.940*W,.075*H);        // orange band
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||rgb(232,233,236),t);
      [[.085,'POWER'],[.185,'OUTPUT'],[.470,'VOLUME'],[.600,'TONE'],[.730,'GAIN'],[.900,'GUITAR']]
        .forEach(s=>lab(s[0],.592,11.5,s[1],rgb(28,22,16)));
      // power toggle (cosmetic) + OFF/ON
      const tog=(x,up)=>{ rr(c,x*W-9,.78*H-16,18,32,2); c.fillStyle=rgb(36,36,40); c.fill(); rr(c,x*W-9,.78*H-16,18,32,2); c.strokeStyle=rgb(90,92,96); c.lineWidth=1; c.stroke();
        rr(c,x*W-7,up?.78*H-14:.78*H,14,14,2); c.fillStyle=rgb(200,200,206); c.fill(); };
      tog(.085,true); lab(.085,.93,8,'ON / OFF',rgb(60,58,54));
      lab(.185,.93,8,'15W / 7W',rgb(60,58,54));
      ledDot(d,.300*W,.78*H,true,232,60,40);                                       // red pilot jewel
      c.beginPath(); c.arc(.900*W,.78*H,9,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.5; c.stroke(); } };

  // Citrus Rumbleverb 50 — Orange Rockerverb 50 MkII (parody). Orange tolex head,
  // cream strip with the 'Citrus' logo + RUMBLEVERB 50, a black control panel:
  // MASTER (Output, Reverb) · DIRTY channel (Volume/Treble/Middle/Bass/Gain) ·
  // CLEAN channel (Volume/Bass/Treble), channel toggle + input jack. ids 0..10.
  P.citrusrumbleverb50 = { w:1200, h:300,
    knobs:[
      {id:9,cx:.150,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:8,cx:.220,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:4,cx:.330,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:3,cx:.395,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:2,cx:.460,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:1,cx:.525,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:0,cx:.590,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:5,cx:.705,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:6,cx:.770,cy:.66,r:.024,style:'pointer',cap:[18,18,20]},
      {id:7,cx:.835,cy:.66,r:.024,style:'pointer',cap:[18,18,20]}],
    switches:[{id:10,cx:.078,cy:.66,hs:.012,dark:true}],
    names:['Gain','Bass','Middle','Treble','Volume','Clean Volume','Clean Bass','Clean Treble','Reverb','Output','Channel'],
    tick:rgb(120,116,104), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; vals=vals||{};
      c.fillStyle=rgb(236,118,24); c.fillRect(0,0,W,H);                            // orange tolex
      rr(c,.025*W,.05*H,.950*W,.24*H,5); c.fillStyle=rgb(238,235,228); c.fill();   // cream logo strip
      rr(c,.025*W,.05*H,.950*W,.24*H,5); c.strokeStyle=rgb(150,146,134); c.lineWidth=1.3; c.stroke();
      textC(d,.055*W,.175*H,F.graffiti,62,rgb(24,22,20),'Citrus','left');
      textC(d,.470*W,.135*H,F.barlow,20,rgb(58,54,48),'RUMBLEVERB 50','left');
      textC(d,.470*W,.215*H,F.barlow,13,rgb(120,80,40),'MkII','left');
      rr(c,.880*W,.085*H,.080*W,.17*H,3); c.fillStyle=rgb(232,120,28); c.fill();   // crest badge
      rr(c,.880*W,.085*H,.080*W,.17*H,3); c.strokeStyle=rgb(150,80,20); c.lineWidth=1; c.stroke();
      // black control panel
      rr(c,.025*W,.34*H,.950*W,.60*H,5); c.fillStyle=rgb(16,16,18); c.fill();
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||rgb(232,233,236),t);
      // orange section blocks behind each group
      const block=(x0,x1)=>{ rr(c,x0*W,.40*H,(x1-x0)*W,.46*H,4); c.fillStyle=rgb(232,112,22); c.fill(); };
      block(.118,.258); block(.295,.625); block(.672,.870);
      // knob labels (above each)
      [[.150,'OUTPUT'],[.220,'REVERB'],[.330,'VOLUME'],[.395,'TREBLE'],[.460,'MIDDLE'],[.525,'BASS'],[.590,'GAIN'],
       [.705,'VOLUME'],[.770,'BASS'],[.835,'TREBLE']].forEach(k=>lab(k[0],.47,9.5,k[1],rgb(28,22,16)));
      // section titles (below)
      lab(.188,.90,11.5,'MASTER',rgb(238,235,228)); lab(.460,.90,11.5,'OVERDRIVE',rgb(238,235,228)); lab(.771,.90,11.5,'CLEAN',rgb(238,235,228));
      // channel toggle (Clean/Dirty) + power (cosmetic)
      const dirty=(vals[10]||0)>0.5;
      lab(.078,.49,8.5,'CHANNEL',rgb(232,233,236));
      lab(.078,.85,8,dirty?'DIRTY':'CLEAN',rgb(236,170,60));
      ledDot(d,.045*W,.66*H,true,232,60,40);                                        // pilot jewel
      // input jack (far right)
      c.beginPath(); c.arc(.935*W,.66*H,9,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.5; c.stroke();
      lab(.935,.82,8,'INPUT',rgb(238,235,228)); } };

  // Epicall Centura — Epiphone Electar Century (parody). Amber tweed head, black
  // control strip: Tone · Volume · Voice knobs, Boost toggle, pilot jewel +
  // Power/Speaker/Footswitch + Bright/Normal/Dark input jacks. ids 0..3.
  P.epicallcentura = { w:1000, h:300,
    knobs:[
      {id:1,cx:.430,cy:.74,r:.034,style:'pointer',cap:[18,18,20]},
      {id:0,cx:.560,cy:.74,r:.034,style:'pointer',cap:[18,18,20]},
      {id:2,cx:.690,cy:.74,r:.030,style:'pointer',cap:[18,18,20]}],
    switches:[{id:3,cx:.820,cy:.74,hs:.012,dark:true}],
    names:['Volume','Tone','Voice','Boost'],
    tick:rgb(120,116,104), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d; vals=vals||{};
      const cg=c.createLinearGradient(0,0,0,H); cg.addColorStop(0,rgb(214,150,78)); cg.addColorStop(1,rgb(184,118,52));
      c.fillStyle=cg; c.fillRect(0,0,W,H);                                          // amber tweed cabinet
      rr(c,.04*W,.07*H,.92*W,.34*H,6); c.fillStyle=rgb(40,30,22); c.fill();         // grille
      c.strokeStyle=rgb(150,110,60); c.lineWidth=1.2; for(let i=1;i<22;++i){ c.beginPath(); c.moveTo(.04*W+i*.92*W/22,.07*H); c.lineTo(.04*W+i*.92*W/22,.41*H); c.stroke(); }
      textC(d,.50*W,.245*H,F.anton,44,rgb(236,225,200),'EPICALL');
      // black control strip
      rr(c,.04*W,.50*H,.92*W,.44*H,5); c.fillStyle=rgb(18,18,20); c.fill();
      c.fillStyle=rgb(214,150,78); c.fillRect(.04*W,.535*H,.92*W,.045*H);           // amber band
      const lab=(cx,y,sz,t,col)=>textC(d,cx*W,y*H,F.barlow,sz,col||rgb(232,233,236),t);
      textC(d,.22*W,.558*H,F.crete,15,rgb(28,22,16),'Centura');
      // left jacks + jewel
      const jack=(x)=>{ c.beginPath(); c.arc(x*W,.74*H,8,0,7); c.fillStyle=rgb(14,14,16); c.fill(); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.4; c.stroke(); };
      const tog=(x)=>{ rr(c,x*W-8,.74*H-15,16,30,2); c.fillStyle=rgb(36,36,40); c.fill(); rr(c,x*W-8,.74*H-15,16,30,2); c.strokeStyle=rgb(90,92,96); c.lineWidth=1; c.stroke(); };
      tog(.075); jack(.135); jack(.190); ledDot(d,.245*W,.74*H,true,232,60,40);
      lab(.075,.90,7,'POWER',rgb(236,225,200)); lab(.135,.90,6.5,'SPKR',rgb(236,225,200)); lab(.190,.90,6.5,'FTSW',rgb(236,225,200));
      // knob labels + boost + input voicing jacks
      lab(.430,.90,10,'TONE',rgb(236,225,200)); lab(.560,.90,10,'VOLUME',rgb(236,225,200)); lab(.690,.90,9,'VOICE',rgb(236,225,200));
      lab(.820,.90,8,'BOOST',rgb(236,225,200));
      jack(.905); jack(.950); lab(.905,.90,6,'BRT',rgb(236,225,200)); lab(.950,.90,6,'DRK',rgb(236,225,200)); } };

  // Epicall Ruby — Epiphone Electar Zephyr Amp20 (parody). Vintage walnut combo,
  // silver top panel with the Electar-style script, Volume · Bass · Treble knobs,
  // input jacks + pilot. ids 0..2.
  P.epicallruby = { w:900, h:300,
    knobs:[
      {id:0,cx:.380,cy:.60,r:.050,style:'pointer',cap:[224,220,206]},
      {id:1,cx:.545,cy:.60,r:.050,style:'pointer',cap:[224,220,206]},
      {id:2,cx:.705,cy:.60,r:.050,style:'pointer',cap:[224,220,206]}],
    switches:[],
    names:['Volume','Bass','Treble'],
    tick:rgb(70,60,48), ptr:rgb(40,36,30),
    draw(d,vals){ const {ctx:c,W,H}=d; vals=vals||{};
      const wg=c.createLinearGradient(0,0,0,H); wg.addColorStop(0,rgb(150,100,56)); wg.addColorStop(1,rgb(110,72,40));
      c.fillStyle=wg; c.fillRect(0,0,W,H);                                          // walnut cabinet
      rr(c,.10*W,.10*H,.80*W,.80*H,8); c.fillStyle=rgb(198,194,182); c.fill();      // silver top panel
      rr(c,.10*W,.10*H,.80*W,.80*H,8); c.strokeStyle=rgb(120,118,110); c.lineWidth=1.6; c.stroke();
      textC(d,.50*W,.27*H,F.crete,30,rgb(40,36,30),'Epicall  Ruby');
      const lab=(cx,y,sz,t)=>textC(d,cx*W,y*H,F.barlow,sz,rgb(40,36,30),t);
      lab(.380,.80,11,'VOLUME'); lab(.545,.80,11,'BASS'); lab(.705,.80,11,'TREBLE');
      // input jacks + pilot (left of the knobs)
      const jack=(x,y)=>{ c.beginPath(); c.arc(x*W,y*H,7,0,7); c.fillStyle=rgb(40,38,40); c.fill(); c.strokeStyle=rgb(90,88,82); c.lineWidth=1.3; c.stroke(); };
      jack(.180,.52); jack(.180,.68); lab(.180,.80,7,'INPUTS');
      ledDot(d,.820*W,.52*H,true,232,60,40); } };

  // Hipzon GA-8 — Gibson GA-8 Discoverer (parody). Cream tolex, silver control
  // plate: Volume · Bass · Treble · Speed · Depth (tremolo). ids 0..4.
  P.hipzonga8 = { w:1000, h:300,
    knobs:[
      {id:0,cx:.200,cy:.62,r:.044,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.340,cy:.62,r:.044,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.480,cy:.62,r:.044,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.620,cy:.62,r:.044,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.760,cy:.62,r:.044,style:'pointer',cap:[20,20,22]}],
    switches:[],
    names:['Volume','Bass','Treble','Speed','Depth'],
    tick:rgb(90,86,78), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d;
      c.fillStyle=rgb(226,221,206); c.fillRect(0,0,W,H);                            // cream tolex
      rr(c,.05*W,.30*H,.90*W,.62*H,6); c.fillStyle=rgb(196,196,190); c.fill();      // silver plate
      rr(c,.05*W,.30*H,.90*W,.62*H,6); c.strokeStyle=rgb(120,120,116); c.lineWidth=1.4; c.stroke();
      textC(d,.50*W,.165*H,F.crete,30,rgb(40,38,34),'Hipzon  GA-8');
      textC(d,.50*W,.40*H,F.barlow,11,rgb(90,86,78),'DISCOVERER');
      const lab=(cx,t)=>textC(d,cx*W,.86*H,F.barlow,10.5,rgb(40,38,34),t);
      [[.200,'VOLUME'],[.340,'BASS'],[.480,'TREBLE'],[.620,'SPEED'],[.760,'DEPTH']].forEach(k=>lab(k[0],k[1]));
      ledDot(d,.90*W,.40*H,true,232,60,40); } };

  // Hipzon GA-79 RVT — Gibson GA-79 RVT Multi Stereo (parody). Brown tolex, silver
  // panel: Volume · Bass · Treble · Reverb · Speed · Depth. ids 0..5.
  P.hipzonga79rvt = { w:1100, h:300,
    knobs:[
      {id:0,cx:.150,cy:.62,r:.038,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.265,cy:.62,r:.038,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.380,cy:.62,r:.038,style:'pointer',cap:[20,20,22]},
      {id:3,cx:.500,cy:.62,r:.038,style:'pointer',cap:[20,20,22]},
      {id:4,cx:.615,cy:.62,r:.038,style:'pointer',cap:[20,20,22]},
      {id:5,cx:.730,cy:.62,r:.038,style:'pointer',cap:[20,20,22]}],
    switches:[],
    names:['Volume','Bass','Treble','Reverb','Speed','Depth'],
    tick:rgb(90,86,78), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d;
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(96,78,64)); bg.addColorStop(1,rgb(74,58,46));
      c.fillStyle=bg; c.fillRect(0,0,W,H);                                          // brown tolex
      rr(c,.04*W,.30*H,.92*W,.62*H,6); c.fillStyle=rgb(198,196,188); c.fill();      // silver panel
      rr(c,.04*W,.30*H,.92*W,.62*H,6); c.strokeStyle=rgb(120,118,112); c.lineWidth=1.4; c.stroke();
      textC(d,.40*W,.165*H,F.crete,28,rgb(236,232,222),'Hipzon');
      textC(d,.66*W,.175*H,F.barlow,16,rgb(236,232,222),'GA-79 RVT  ·  MULTI STEREO','left');
      const lab=(cx,t)=>textC(d,cx*W,.86*H,F.barlow,10,rgb(40,38,34),t);
      [[.150,'VOLUME'],[.265,'BASS'],[.380,'TREBLE'],[.500,'REVERB'],[.615,'SPEED'],[.730,'DEPTH']].forEach(k=>lab(k[0],k[1]));
      const jack=(x,y)=>{ c.beginPath(); c.arc(x*W,y*H,7,0,7); c.fillStyle=rgb(40,38,40); c.fill(); c.strokeStyle=rgb(90,88,82); c.lineWidth=1.3; c.stroke(); };
      jack(.860,.52); jack(.910,.52); textC(d,.885*W,.70*H,F.barlow,8,rgb(40,38,34),'STEREO');
      ledDot(d,.93*W,.40*H,true,232,60,40); } };

  // Hipzon GA-88 — Gibson GA-88S stereo (parody, recreated from RS art). Grey
  // tolex, silver plate: Volume · Bass · Treble. ids 0..2.
  P.hipzonga88 = { w:900, h:300,
    knobs:[
      {id:0,cx:.350,cy:.60,r:.050,style:'pointer',cap:[20,20,22]},
      {id:1,cx:.520,cy:.60,r:.050,style:'pointer',cap:[20,20,22]},
      {id:2,cx:.680,cy:.60,r:.050,style:'pointer',cap:[20,20,22]}],
    switches:[],
    names:['Volume','Bass','Treble'],
    tick:rgb(90,86,78), ptr:rgb(244,245,248),
    draw(d,vals){ const {ctx:c,W,H}=d;
      c.fillStyle=rgb(96,98,102); c.fillRect(0,0,W,H);                              // grey tolex
      rr(c,.07*W,.12*H,.86*W,.30*H,6); c.fillStyle=rgb(198,196,188); c.fill();      // silver name plate
      textC(d,.50*W,.27*H,F.crete,30,rgb(40,38,34),'Hipzon  GA-88');
      rr(c,.07*W,.46*H,.86*W,.46*H,6); c.fillStyle=rgb(40,40,42); c.fill();         // black control strip
      const lab=(cx,t)=>textC(d,cx*W,.86*H,F.barlow,11,rgb(224,224,218),t);
      [[.350,'VOLUME'],[.520,'BASS'],[.680,'TREBLE']].forEach(k=>lab(k[0],k[1]));
      const jack=(x)=>{ c.beginPath(); c.arc(x*W,.60*H,8,0,7); c.fillStyle=rgb(16,16,18); c.fill(); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.4; c.stroke(); };
      jack(.150); jack(.840); textC(d,.150*W,.78*H,F.barlow,8,rgb(224,224,218),'IN');
      ledDot(d,.840*W,.78*H,true,232,60,40); } };

  // ── generic fallback: any VST without a hand-built spec gets a clean knob
  //    grid built from its live parameter metadata (so nothing opens in a
  //    native window). params = [{id|paramId|index, name, value}, …]. ──────────
  function buildGeneric(stem, params) {
    const ps = (params || []).filter(p => p && (p.id ?? p.paramId ?? p.index) != null).slice(0, 12);
    const n = ps.length; if (!n) return null;
    const cols = n <= 4 ? n : (n <= 6 ? 3 : 4), rows = Math.ceil(n / cols);
    const cellW = 96, cellH = 124, padTop = 56, padBot = 22;
    const w = cols * cellW, h = padTop + rows * cellH + padBot, rPx = Math.min(cellW, cellH) * 0.27;
    const knobs = ps.map((p, i) => {
      const cc = i % cols, rw = Math.floor(i / cols);
      const cxPx = (cc + 0.5) * cellW, cyPx = padTop + rw * cellH + cellH * 0.40;
      return { id: p.id ?? p.paramId ?? p.index, cxPx, cyPx, rPx,
        cx: cxPx / w, cy: cyPx / h, r: rPx / w, style: 'pointer', cap: [66, 68, 74],
        label: (p.name || p.label || ('P' + (i + 1))) };
    });
    return { w, h, knobs, generic: true, ptr: rgb(226, 227, 231), tick: rgb(118, 120, 126),
      draw(d) { box(d, 42, 44, 50);
        textC(d, d.W * 0.5, 32, F.bebas, 24, rgb(228, 229, 233), (stem || 'plugin').toUpperCase());
        knobs.forEach(k => { let lbl = k.label; if (lbl.length > 12) lbl = lbl.slice(0, 11) + '…';
          textC(d, k.cxPx, k.cyPx + k.rPx * 1.5 + 13, F.barlow, 10.5, rgb(202, 204, 210), lbl); }); } };
  }

  // ── Studio racks: shared 1U faceplate (mirrors _shared/rack_ui.hpp) ─────────
  // rackSpec({title, accent:[r,g,b], names:[...]}) — brushed dark-metal face with
  // rack ears + screws, POWER button, accent knob sub-panel (knobs auto-laid in
  // 1–2 rows), green LCD nameplate with the title, and a decorative INPUT knob.
  function rackFace(d, o, knobs) {
    const c=d.ctx, W=d.W, H=d.H, A=o.accent, ew=W*0.06;
    const face=c.createLinearGradient(0,0,0,H); face.addColorStop(0,rgb(58,60,66)); face.addColorStop(1,rgb(34,35,40));
    c.fillStyle=face; c.fillRect(0,0,W,H);
    c.strokeStyle=rgb(12,13,15); c.lineWidth=3; c.strokeRect(1.5,1.5,W-3,H-3);
    [0, W-ew].forEach(ex=>{ c.fillStyle=rgb(26,27,31); c.fillRect(ex,0,ew,H);
      screw(d, ex+ew*0.5, H*0.17); screw(d, ex+ew*0.5, H*0.83); });
    // POWER button
    c.beginPath(); c.arc(ew+18,H*0.30,9,0,7); c.fillStyle=rgb(22,23,27); c.fill();
    c.strokeStyle=rgb(90,92,98); c.lineWidth=2; c.stroke();
    textC(d, ew+18, H*0.30+16, F.barlow, 7.5, rgb(150,152,158), 'POWER');
    // accent knob sub-panel + knob labels
    const pX=ew+36, pY=H*0.12, pW=W*0.42, pH=H*0.76;
    rr(c,pX,pY,pW,pH,6); c.fillStyle=rgb(A[0],A[1],A[2]); c.fill();
    rr(c,pX,pY,pW,pH,6); c.strokeStyle='rgba(0,0,0,0.28)'; c.lineWidth=1.5; c.stroke();
    const lum=0.299*A[0]+0.587*A[1]+0.114*A[2], lc=lum>140?rgb(28,28,32):rgb(238,240,244);
    knobs.forEach(k=>{ let l=k._lbl; if(l.length>10) l=l.slice(0,10);
      const ly = k._above ? k.cy*H - k.r*W - 6 : k.cy*H + k.r*W + 11;   // top row labels ABOVE its knobs
      textC(d, k.cx*W, ly, F.barlow, 11, lc, l); });
    // green LCD nameplate
    const lX=pX+pW+22, lY=H*0.22, lW=W*0.30, lH=H*0.56;
    rr(c,lX,lY,lW,lH,4); c.fillStyle=rgb(8,20,10); c.fill();
    rr(c,lX,lY,lW,lH,4); c.strokeStyle=rgb(40,90,45); c.lineWidth=1.5; c.stroke();
    textC(d, lX+10, lY+15, F.bebas, 17, rgb(120,255,130), o.title, 'left');
    textC(d, lX+10, lY+lH-12, F.barlow, 8, rgb(70,180,80), 'USER PROG · CHIEF', 'left');
    // decorative INPUT knob (far right)
    const ix=W-ew-30, iy=H*0.5, iR=H*0.26;
    c.beginPath(); c.arc(ix,iy,iR,0,7); c.fillStyle=rgb(24,25,29); c.fill();
    c.strokeStyle=rgb(80,82,88); c.lineWidth=2; c.stroke();
    c.beginPath(); c.arc(ix,iy,iR*0.55,0,7); c.fillStyle=rgb(40,42,48); c.fill();
    textC(d, ix-iR-6, iy, F.barlow, 8, rgb(150,152,158), 'INPUT', 'right');
  }
  function rackSpec(o) {
    const n=o.names.length, ew=0.06, pXf=ew+0.047, pWf=0.42, pYf=0.12, pHf=0.76;
    const cols=n<=5?n:Math.ceil(n/2), rows=n<=5?1:2;
    const r=Math.max(0.016, Math.min(0.030, pWf/cols*0.30));
    const knobs=o.names.map((nm,i)=>{ const cc=i%cols, rw=Math.floor(i/cols);
      return { id:i, cx:pXf+pWf*((cc+0.5)/cols),
        cy: rows===1 ? pYf+pHf*0.42 : (rw===0 ? pYf+pHf*0.32 : pYf+pHf*0.70),
        r, style:'boss', _lbl:nm, _above:(rows===2 && rw===0) }; });
    return { w:760, h:172, knobs, ptr:rgb(238,240,242), draw(d){ rackFace(d,o,knobs); } };
  }
  P.rotavibe        = rackSpec({title:'ROTA VIBE',         accent:[205,135,120], names:['Rate','Depth','Mix','Balance']});
  // Stereo Vibrato — Shin-ei Uni-Vibe look: grey metal chassis, black control
  // plate, cursive script logo, black knobs w/ MIN-MAX scales, red CHORUS/VIBRATO
  // slider, red jewel lamp, bottom jack row + POWER SW. Parody "Astro-Vibe".
  // RS params (3 knobs): Speed0 Waveform1 Mix2.
  P.stereoanalogvibe = { w:900, h:340,
    knobs:[
      {id:0,cx:.140,cy:.43,r:.037,style:'davies'},  // Speed
      {id:1,cx:.440,cy:.43,r:.037,style:'davies'},  // Waveform
      {id:2,cx:.580,cy:.43,r:.037,style:'davies'}], // Mix
    tick:rgb(150,150,150), ptr:rgb(226,226,222),
    draw(d){ const {ctx:c,W,H}=d, m=7;
      // grey metal chassis
      const cg=c.createLinearGradient(0,0,0,H); cg.addColorStop(0,rgb(172,172,170)); cg.addColorStop(1,rgb(120,120,118));
      c.fillStyle=cg; c.fillRect(0,0,W,H);
      c.strokeStyle=rgb(70,70,68); c.lineWidth=2; c.strokeRect(1,1,W-2,H-2);
      // black control plate
      const px=.035*W, py=.12*H, pw=.93*W, ph=.76*H;
      rr(c,px,py,pw,ph,5); c.fillStyle=rgb(20,20,22); c.fill();
      rr(c,px,py,pw,ph,5); c.strokeStyle=rgb(5,5,7); c.lineWidth=1.5; c.stroke();
      [[px+12,py+12],[px+pw-12,py+12],[px+12,py+ph-12],[px+pw-12,py+ph-12]].forEach(p=>screw(d,p[0],p[1]));
      const wt=rgb(230,230,228), dim=rgb(170,172,174);
      // cursive script logo (parody)
      textC(d,.42*W,.185*H,F.ink,32,wt,'Astro-Vibe');
      // knob MIN-MAX tick scales
      [.140,.440,.580].forEach(cx=>{ const KX=cx*W, KY=.43*H, R=.037*W*1.5;
        c.strokeStyle=dim; c.lineWidth=1;
        for(let i=0;i<=10;i++){ const an=(135+i/10*270)*Math.PI/180;
          c.beginPath(); c.moveTo(KX+Math.cos(an)*R,KY+Math.sin(an)*R); c.lineTo(KX+Math.cos(an)*(R+5),KY+Math.sin(an)*(R+5)); c.stroke(); }
        const aMin=135*Math.PI/180, aMax=405*Math.PI/180;
        textC(d,KX+Math.cos(aMin)*(R+15),KY+Math.sin(aMin)*(R+15)+3,F.barlow,8,dim,'MIN');
        textC(d,KX+Math.cos(aMax)*(R+15),KY+Math.sin(aMax)*(R+15)+3,F.barlow,8,dim,'MAX'); });
      textC(d,.140*W,.635*H,F.barlow,18,wt,'SPEED');
      textC(d,.440*W,.635*H,F.barlow,18,wt,'WAVEFORM');
      textC(d,.580*W,.635*H,F.barlow,18,wt,'MIX');
      // CHORUS / VIBRATO red slider
      const swx=.290*W, swy=.41*H;
      rr(c,swx-.024*W,swy-.016*H,.048*W,.032*H,3); c.fillStyle=rgb(42,42,44); c.fill();
      rr(c,swx-.024*W,swy-.016*H,.048*W,.032*H,3); c.strokeStyle=rgb(80,82,84); c.lineWidth=1; c.stroke();
      rr(c,swx-.022*W,swy-.013*H,.020*W,.026*H,2); c.fillStyle=rgb(202,42,38); c.fill();
      textC(d,swx-.012*W,.50*H,F.barlow,8.5,dim,'CHORUS');
      textC(d,swx+.030*W,.50*H,F.barlow,8.5,dim,'VIBRATO');
      // big socket + red jewel lamp (right)
      c.beginPath(); c.arc(.710*W,.43*H,.028*W,0,7); c.fillStyle=rgb(24,24,26); c.fill();
      c.strokeStyle=rgb(70,72,74); c.lineWidth=2; c.stroke();
      c.beginPath(); c.arc(.820*W,.41*H,.020*W,0,7); c.fillStyle=rgb(208,46,40); c.fill();
      c.strokeStyle=rgb(120,40,36); c.lineWidth=2; c.stroke();
      const hl=c.createRadialGradient(.815*W,.395*H,1,.820*W,.41*H,.020*W);
      hl.addColorStop(0,'rgba(255,180,170,0.85)'); hl.addColorStop(1,'rgba(255,180,170,0)');
      c.beginPath(); c.arc(.820*W,.41*H,.020*W,0,7); c.fillStyle=hl; c.fill();
      // bottom jack row
      const jack=(cx,lbl)=>{ const JX=cx*W, JY=.76*H;
        c.beginPath(); c.arc(JX,JY,.022*W,0,7); c.fillStyle=rgb(150,152,154); c.fill();
        c.strokeStyle=rgb(88,90,92); c.lineWidth=1.5; c.stroke();
        c.beginPath(); c.arc(JX,JY,.011*W,0,7); c.fillStyle=rgb(18,18,20); c.fill();
        textC(d,JX,.855*H,F.barlow,9.5,dim,lbl); };
      jack(.150,'INSTRUMENTS'); jack(.320,'OUTPUT');
      // foot-control multipin (smaller)
      const fx=.520*W, fy=.76*H; c.beginPath(); c.arc(fx,fy,.021*W,0,7); c.fillStyle=rgb(38,38,40); c.fill();
      c.strokeStyle=rgb(120,122,124); c.lineWidth=1.5; c.stroke();
      for(let i=0;i<5;i++){ const an=i/5*Math.PI*2-Math.PI/2; c.beginPath();
        c.arc(fx+Math.cos(an)*.0095*W,fy+Math.sin(an)*.0095*W,.0026*W,0,7); c.fillStyle=rgb(155,157,159); c.fill(); }
      textC(d,fx,.855*H,F.barlow,9.5,dim,'FOOT CONTROL');
      // POWER SW. toggle (right)
      const pwx=.840*W, pwy=.74*H;
      rr(c,pwx-.013*W,pwy-.045*H,.026*W,.09*H,3); c.fillStyle=rgb(28,28,30); c.fill();
      rr(c,pwx-.013*W,pwy-.045*H,.026*W,.09*H,3); c.strokeStyle=rgb(80,82,84); c.lineWidth=1; c.stroke();
      rr(c,pwx-.010*W,pwy-.042*H,.020*W,.040*H,2); c.fillStyle=rgb(158,160,162); c.fill();
      textC(d,pwx,(pwy-.075)*H,F.barlow,8.5,dim,'ON');
      textC(d,pwx,.855*H,F.barlow,9.5,dim,'POWER SW.'); } };
  P.stereophaser    = rackSpec({title:'STEREO PHASER',     accent:[90,175,178],  names:['Rate','Depth','Mix']});
  P.stereotubetrem  = rackSpec({title:'STEREO TUBE TREM',  accent:[150,180,160], names:['Speed','Mix','Waveform']});
  P.studiochamber   = rackSpec({title:'STUDIO CHAMBER',    accent:[140,175,200], names:['Time','Tone','Depth','Mix']});
  // Studio Chorus — Boss RCE-10 Digital Chorus Ensemble look: charcoal body, big
  // blue wireframe CHIEF logo + circuit traces up top, 3 sections (PRE DELAY /
  // MODULATION / EFFECT) of colour-capped knobs, right logo block + POWER.
  // Parody CHIEF / "DIGITAL CHORUS ENSEMBLE RCE-12".
  // RS params (7 knobs): Rate0 Depth1 Mix2 LoFilter3 HiFilter4 Stereo5 Delay6.
  P.studiochorus = { w:960, h:300,
    knobs:[
      {id:6,cx:.120,cy:.66,r:.032,style:'pointer',cap:[42,182,120]},  // Delay  (PRE DELAY / TIME, green)
      {id:0,cx:.230,cy:.66,r:.032,style:'pointer',cap:[68,150,212]},  // Rate   (blue)
      {id:1,cx:.310,cy:.66,r:.032,style:'pointer',cap:[68,150,212]},  // Depth  (blue)
      {id:3,cx:.420,cy:.66,r:.032,style:'pointer',cap:[232,200,44]},  // Lo Filter (yellow)
      {id:4,cx:.500,cy:.66,r:.032,style:'pointer',cap:[232,200,44]},  // Hi Filter (yellow)
      {id:5,cx:.580,cy:.66,r:.032,style:'pointer',cap:[235,140,42]},  // Stereo (orange)
      {id:2,cx:.660,cy:.66,r:.032,style:'pointer',cap:[235,140,42]}], // Mix    (orange)
    tick:rgb(150,154,160), ptr:rgb(236,238,240),
    draw(d){ const {ctx:c,W,H}=d, m=7;
      c.fillStyle=rgb(14,15,16); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(58,60,64)); bg.addColorStop(1,rgb(40,42,46));
      rr(c,m,m,W-2*m,H-2*m,9); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,9); c.strokeStyle=rgb(10,11,12); c.lineWidth=2; c.stroke();
      const blu=rgb(70,150,212), cyn=rgb(120,200,236), wt=rgb(222,224,228), dim=rgb(170,174,180), body=rgb(46,48,52);
      // faint circuit-trace pattern (top)
      c.save(); c.globalAlpha=0.5; c.strokeStyle=rgb(56,96,134); c.lineWidth=1;
      rr(c,.30*W,.10*H,.60*W,.30*H,4); c.stroke();
      for(const yy of [.16,.24,.32]){ c.beginPath(); c.moveTo(.33*W,yy*H); c.lineTo(.62*W,yy*H);
        c.lineTo(.66*W,(yy+.05)*H); c.lineTo(.88*W,(yy+.05)*H); c.stroke(); }
      for(const xx of [.40,.52,.70,.82]){ c.beginPath(); c.moveTo(xx*W,.12*H); c.lineTo(xx*W,.38*H); c.stroke(); }
      c.restore();
      // big wireframe CHIEF logo
      outlineText(d,.40*W,.255*H,F.bebas,52,body,blu,'CHIEF',3);
      // section dividers
      c.strokeStyle=rgb(96,100,106); c.lineWidth=1.2;
      for(const xx of [.180,.375]){ c.beginPath(); c.moveTo(xx*W,.52*H); c.lineTo(xx*W,.84*H); c.stroke(); }
      // section labels (+ underline)
      const sec=(cx,t)=>{ textC(d,cx*W,.50*H,F.barlow,13,wt,t);
        c.strokeStyle=blu; c.lineWidth=1.4; const hw=t.length*3.6;
        c.beginPath(); c.moveTo(cx*W-hw,.560*H); c.lineTo(cx*W+hw,.560*H); c.stroke(); };
      sec(.120,'PRE DELAY'); sec(.270,'MODULATION'); sec(.540,'EFFECT');
      // knob sub-labels
      [[.120,'TIME'],[.230,'RATE'],[.310,'DEPTH'],[.420,'LO FILTER'],[.500,'HI FILTER'],[.580,'STEREO'],[.660,'MIX']]
        .forEach(p=> textC(d,p[0]*W,.820*H,F.barlow,12,wt,p[1]));
      // slide switch helper
      const slide=(cx,cy)=>{ rr(c,cx-.011*W,cy-.05*H,.022*W,.10*H,3); c.fillStyle=rgb(26,27,29); c.fill();
        rr(c,cx-.011*W,cy-.05*H,.022*W,.10*H,3); c.strokeStyle=rgb(70,72,75); c.lineWidth=1; c.stroke();
        rr(c,cx-.0085*W,cy-.045*H,.017*W,.045*H,2); c.fillStyle=rgb(150,152,156); c.fill(); };
      // EFFECT on/off (left) + LED
      ledDot(d,.040*W,.50*H,true,210,52,42);
      slide(.040*W,.66*H); textC(d,.040*W,.80*H,F.barlow,9.5,dim,'EFFECT');
      textC(d,.040*W,.858*H,F.barlow,7.5,dim,'ON / OFF');
      // right logo block
      const rx=.815*W;
      c.beginPath(); c.moveTo(rx-.090*W,.48*H); c.lineTo(rx-.076*W,.45*H); c.lineTo(rx-.076*W,.51*H); c.closePath();
      c.fillStyle=blu; c.fill();
      textC(d,rx-.048*W,.485*H,F.bebas,17,blu,'CHIEF','left');
      textC(d,rx,.575*H,F.bebas,22,cyn,'DIGITAL');
      textC(d,rx,.648*H,F.bebas,17,blu,'CHORUS');
      textC(d,rx,.700*H,F.bebas,17,blu,'ENSEMBLE');
      textC(d,rx,.782*H,F.bebas,26,blu,'CE-12');
      // POWER (far right) + LED
      ledDot(d,.935*W,.52*H,true,210,52,42);
      slide(.935*W,.66*H); textC(d,.935*W,.80*H,F.barlow,9.5,dim,'POWER');
      textC(d,.935*W,.858*H,F.barlow,7.5,dim,'ON / OFF'); } };
  // Studio Comp — dbx 160 look: walnut sides, brushed-silver rails, black face,
  // silver knurled knobs, big amber VU meter, "HZX 165 COMPRESSOR/LIMITER" logo.
  // RS params (5 knobs): Threshold0 Ratio1 Attack2 Release3 Output4.
  P.hzx = { w:980, h:300,
    knobs:[
      {id:0,cx:.105,cy:.51,r:.039,style:'knurled'},  // Threshold
      {id:1,cx:.210,cy:.51,r:.039,style:'knurled'},  // Ratio (= COMPRESSION)
      {id:2,cx:.315,cy:.51,r:.039,style:'knurled'},  // Attack
      {id:3,cx:.420,cy:.51,r:.039,style:'knurled'},  // Release
      {id:4,cx:.525,cy:.51,r:.039,style:'knurled'}], // Output (= OUTPUT GAIN)
    tick:rgb(150,152,158), ptr:rgb(30,30,32),
    draw(d){ const {ctx:c,W,H}=d, ew=W*0.045;
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      // walnut side panels
      const wood=(x)=>{ const wg=c.createLinearGradient(x,0,x+ew,0);
        wg.addColorStop(0,rgb(92,54,28)); wg.addColorStop(.5,rgb(128,82,44)); wg.addColorStop(1,rgb(88,52,27));
        c.fillStyle=wg; c.fillRect(x,0,ew,H); };
      wood(0); wood(W-ew);
      // black faceplate (between the wood sides)
      const fg=c.createLinearGradient(0,0,0,H); fg.addColorStop(0,rgb(32,32,35)); fg.addColorStop(1,rgb(15,15,18));
      c.fillStyle=fg; c.fillRect(ew,0,W-2*ew,H);
      // brushed-silver rack rails — span ONLY the black face (stop at the wood)
      const rail=(y,h)=>{ const rg=c.createLinearGradient(0,y,0,y+h);
        rg.addColorStop(0,rgb(222,224,228)); rg.addColorStop(.5,rgb(146,148,154)); rg.addColorStop(1,rgb(196,198,204));
        c.fillStyle=rg; c.fillRect(ew,y,W-2*ew,h); c.strokeStyle=rgb(86,88,92); c.lineWidth=1; c.strokeRect(ew+.5,y+.5,W-2*ew-1,h-1); };
      rail(H*0.035,H*0.05); rail(H*0.915,H*0.05);
      const w=rgb(226,228,232), dim=rgb(150,152,158);
      // section labels
      textC(d,.105*W,.230*H,F.barlow,16,w,'THRESHOLD');
      textC(d,.210*W,.230*H,F.barlow,16,w,'COMPRESSION');
      textC(d,.315*W,.230*H,F.barlow,16,w,'ATTACK');
      textC(d,.420*W,.230*H,F.barlow,16,w,'RELEASE');
      textC(d,.525*W,.230*H,F.barlow,16,w,'OUTPUT');
      // BELOW / ABOVE indicator LEDs over THRESHOLD
      ledDot(d,.065*W,.345*H,true,224,196,40);  textC(d,.065*W,.398*H,F.barlow,9.5,dim,'BELOW');
      ledDot(d,.145*W,.345*H,false,200,40,40);  textC(d,.147*W,.398*H,F.barlow,9.5,dim,'ABOVE');
      // POWER button
      rr(c,.067*W,.690*H,.070*W,.085*H,3); c.fillStyle=rgb(40,40,44); c.fill();
      rr(c,.077*W,.705*H,.050*W,.055*H,2); c.fillStyle=rgb(170,172,176); c.fill();
      textC(d,.102*W,.822*H,F.barlow,10,dim,'POWER');
      // METER selector buttons + bracket
      ['INPUT','OUTPUT','GAIN'].forEach((t,i)=>{ const bx=(.280+i*.070)*W;
        rr(c,bx,.695*H,.056*W,.075*H,3); c.fillStyle=rgb(40,40,44); c.fill();
        rr(c,bx+.008*W,.708*H,.040*W,.048*H,2); c.fillStyle=rgb(150,152,158); c.fill();
        textC(d,bx+.028*W,.668*H,F.barlow,8.5,dim,i===2?'CHANGE':t); });
      textC(d,.350*W,.842*H,F.barlow,10,dim,'METER');
      // ── amber VU meter ──
      const vx=.630*W, vy=.205*H, vw=.270*W, vh=.42*H;
      rr(c,vx-7,vy-7,vw+14,vh+14,6); c.fillStyle=rgb(16,16,18); c.fill();
      const ag=c.createLinearGradient(0,vy,0,vy+vh); ag.addColorStop(0,rgb(255,214,128)); ag.addColorStop(1,rgb(230,168,68));
      rr(c,vx,vy,vw,vh,4); c.fillStyle=ag; c.fill();
      rr(c,vx,vy,vw,vh,4); c.strokeStyle=rgb(120,80,30); c.lineWidth=1.2; c.stroke();
      const mcx=vx+vw/2, mcy=vy+vh*1.16, mR=vh*0.96, a0=Math.PI*1.20, a1=Math.PI*1.80;
      c.strokeStyle=rgb(38,66,142); c.lineWidth=1.6; c.beginPath(); c.arc(mcx,mcy,mR,a0,a1); c.stroke();
      const nums=['-40','-30','-20','-10','0','+10','+20'];
      nums.forEach((n,i)=>{ const t=a0+(a1-a0)*(i/(nums.length-1));
        c.strokeStyle=i>=4?rgb(170,40,40):rgb(38,66,142); c.lineWidth=1.4; c.beginPath();
        c.moveTo(mcx+Math.cos(t)*(mR-5),mcy+Math.sin(t)*(mR-5)); c.lineTo(mcx+Math.cos(t)*mR,mcy+Math.sin(t)*mR); c.stroke();
        textC(d,mcx+Math.cos(t)*(mR+11),mcy+Math.sin(t)*(mR+11)+3,F.barlow,8,i>=4?rgb(150,30,30):rgb(30,55,128),n); });
      textC(d,mcx,vy+vh*0.55,F.barlow,10,rgb(38,66,142),'DECIBELS');
      textC(d,mcx,vy+vh*0.78,F.bebas,16,rgb(38,66,142),'HZX');
      const nt=a0+(a1-a0)*0.42; c.strokeStyle=rgb(22,22,26); c.lineWidth=2;
      c.beginPath(); c.moveTo(mcx,mcy); c.lineTo(mcx+Math.cos(nt)*mR*0.98,mcy+Math.sin(nt)*mR*0.98); c.stroke();
      c.beginPath(); c.arc(mcx,mcy,3,0,7); c.fillStyle=rgb(22,22,26); c.fill();
      // ── brand logo ──
      textC(d,.690*W,.815*H,F.bebas,37,w,'HZX');
      c.beginPath(); c.arc(.767*W,.790*H,4,0,7); c.fillStyle=dim; c.fill();
      textC(d,.825*W,.815*H,F.bebas,37,w,'165');
      textC(d,.768*W,.882*H,F.barlow,11,dim,'COMPRESSOR / LIMITER'); } };
  P.studiodelay     = rackSpec({title:'STUDIO DELAY',      accent:[105,135,205], names:['Time L','Time R','Feedback','Filter','Mix']});
  // Parametric EQ — GML 8200 look: black wide rack, COLOUR-coded knobs per band
  // (centred), centre LNG logo, "MODEL 8300 PARAMETRIC EQUALIZER" bottom.
  // RS params: Bass0 BassFreq1 LoMid2 LoMidFreq3 LoMidQ4 HiMid5 HiMidFreq6 HiMidQ7 Treble8 TrebleFreq9.
  P.lng = { w:960, h:300,
    knobs:[
      {id:0,cx:.15,cy:.26,r:.028,style:'pointer',cap:[196,44,44]},   // Bass gain (red)
      {id:1,cx:.15,cy:.62,r:.026,style:'pointer',cap:[196,44,44]},   // BassFreq
      {id:2,cx:.31,cy:.26,r:.024,style:'pointer',cap:[214,178,46]},  // LoMid gain (yellow)
      {id:3,cx:.31,cy:.52,r:.024,style:'pointer',cap:[214,178,46]},  // LoMidFreq
      {id:4,cx:.31,cy:.78,r:.024,style:'pointer',cap:[214,178,46]},  // LoMidQ
      {id:5,cx:.69,cy:.26,r:.024,style:'pointer',cap:[60,150,70]},   // HiMid gain (green)
      {id:6,cx:.69,cy:.52,r:.024,style:'pointer',cap:[60,150,70]},   // HiMidFreq
      {id:7,cx:.69,cy:.78,r:.024,style:'pointer',cap:[60,150,70]},   // HiMidQ
      {id:8,cx:.85,cy:.26,r:.028,style:'pointer',cap:[64,112,200]},  // Treble gain (blue)
      {id:9,cx:.85,cy:.62,r:.026,style:'pointer',cap:[64,112,200]}], // TrebleFreq
    tick:rgb(110,112,118), ptr:rgb(238,240,244),
    draw(d){ const {ctx:c,W,H}=d, m=7;
      c.fillStyle=rgb(8,8,10); c.fillRect(0,0,W,H);
      const g=c.createLinearGradient(0,0,0,H); g.addColorStop(0,rgb(30,30,34)); g.addColorStop(1,rgb(15,15,18));
      rr(c,m,m,W-2*m,H-2*m,8); c.fillStyle=g; c.fill();
      rr(c,m,m,W-2*m,H-2*m,8); c.strokeStyle=rgb(10,10,12); c.lineWidth=2; c.stroke();
      [W*.03,W*.97].forEach(ex=>{ screw(d,ex,H*.13); screw(d,ex,H*.87); });
      const w=rgb(228,230,234), dim=rgb(166,168,174);
      // band headers
      textC(d,.15*W,.085*H,F.barlow,16,w,'LOW EQ');
      textC(d,.31*W,.085*H,F.barlow,16,w,'LOW MID');
      textC(d,.69*W,.085*H,F.barlow,16,w,'HIGH MID');
      textC(d,.85*W,.085*H,F.barlow,16,w,'HIGH EQ');
      // per-knob function labels (below each knob)
      const sub=(cx,cy,r,t)=> textC(d, cx*W, cy*H + r*W + 14, F.barlow, 12.5, dim, t);
      sub(.15,.26,.028,'GAIN'); sub(.15,.62,.026,'FREQ');
      sub(.31,.26,.024,'GAIN'); sub(.31,.52,.024,'FREQ'); sub(.31,.78,.024,'Q');
      sub(.69,.26,.024,'GAIN'); sub(.69,.52,.024,'FREQ'); sub(.69,.78,.024,'Q');
      sub(.85,.26,.028,'GAIN'); sub(.85,.62,.026,'FREQ');
      // centre: LNG logo + green LED + EQ-IN button
      ledDot(d,.50*W,.13*H,true,90,220,90);
      textC(d,.50*W,.40*H,F.bebas,38,w,'LNG');
      rr(c,.475*W,.57*H,.05*W,.12*H,3); c.fillStyle=rgb(196,44,44); c.fill();
      textC(d,.50*W,.75*H,F.barlow,11,dim,'EQ IN');
      textC(d,W-m-16,H*0.93,F.barlow,11,dim,'MODEL 8300   PARAMETRIC EQUALIZER   SERIES II','right'); } };
  // Graphic EQ — API 550b look: tall black 500-series module, column of API star
  // knobs (gain) each with a small freq knob, blue freq scales, HF/LF toggles, IN.
  // Parody: API arrow + "G-550". RS params (10): Bass0 BassFreq1 LoMid2 LoMidFreq3
  // Mid4 MidFreq5 HiMid6 HiMidFreq7 Treble8 TrebleFreq9 (HIGH at top → LOW bottom).
  P.g550 = { w:300, h:740,
    knobs:[
      {id:8,cx:.40,cy:.165,r:.090,style:'api'}, {id:9,cx:.78,cy:.165,r:.052,style:'api'},  // HIGH (Treble) + freq
      {id:6,cx:.40,cy:.310,r:.090,style:'api'}, {id:7,cx:.78,cy:.310,r:.052,style:'api'},  // HI-MID
      {id:4,cx:.40,cy:.455,r:.090,style:'api'}, {id:5,cx:.78,cy:.455,r:.052,style:'api'},  // MID
      {id:2,cx:.40,cy:.600,r:.090,style:'api'}, {id:3,cx:.78,cy:.600,r:.052,style:'api'},  // LO-MID
      {id:0,cx:.40,cy:.745,r:.090,style:'api'}, {id:1,cx:.78,cy:.745,r:.052,style:'api'}], // LOW (Bass)
    tick:rgb(150,154,160), ptr:rgb(40,44,52),
    draw(d){ const {ctx:c,W,H}=d, m=6;
      c.fillStyle=rgb(8,9,11); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(30,31,34)); bg.addColorStop(1,rgb(18,19,22));
      rr(c,m,m,W-2*m,H-2*m,8); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,8); c.strokeStyle=rgb(6,7,9); c.lineWidth=2; c.stroke();
      screw(d,.5*W,.032*H); screw(d,.5*W,.968*H);
      const blu=rgb(86,150,214), wt=rgb(228,230,234), dim=rgb(150,154,160);
      // APX arrow logo + model (parody)
      c.beginPath(); c.moveTo(.16*W,.062*H); c.lineTo(.25*W,.044*H); c.lineTo(.25*W,.080*H); c.closePath();
      c.fillStyle=blu; c.fill(); c.fillRect(.25*W,.056*H,.05*W,.012*H);
      textC(d,.44*W,.062*H,F.bebas,26,blu,'APX');
      textC(d,.70*W,.062*H,F.bebas,30,blu,'G-550');
      // bands
      const bands=[['HIGH',.165],['HI-MID',.310],['MID',.455],['LO-MID',.600],['LOW',.745]];
      const frng=['2.5k–20k','800–12.5k','300–5k','75–1k','30–400'];
      // boost/cut scale (dB): 0 at top, −2/+2 then 4/6/9/12 down each side — matches ang()
      const gscale=[[.5,'0'],[.4,'−2'],[.3,'−4'],[.2,'−6'],[.1,'−9'],[0,'−12'],[.6,'+2'],[.7,'+4'],[.8,'+6'],[.9,'+9'],[1,'+12']];
      const gr=.090*W*1.66;
      bands.forEach((b,i)=>{ const cy=b[1]*H;
        textC(d,.070*W,cy,F.barlow,13,wt,b[0],'left');
        gscale.forEach(s=>{ const an=(135+s[0]*270)*Math.PI/180;
          textC(d,.40*W+Math.cos(an)*gr, cy+Math.sin(an)*gr+2, F.barlow,9.5, s[1]==='0'?wt:dim, s[1]); });
        // frequency range under the small freq knob (blue)
        textC(d,.78*W,(b[1]+.060)*H,F.barlow,9.5,blu,frng[i]+' Hz'); });
      // HF / LF peak-shelf toggles
      const tog=(cy,lbl)=>{ rr(c,.895*W,(cy-.022)*H,.06*W,.05*H,2); c.fillStyle=rgb(28,29,32); c.fill();
        rr(c,.895*W,(cy-.022)*H,.06*W,.05*H,2); c.strokeStyle=rgb(70,72,76); c.lineWidth=1; c.stroke();
        c.beginPath(); c.arc(.925*W,(cy-.008)*H,.016*W,0,7); c.fillStyle=rgb(160,162,166); c.fill();
        textC(d,.925*W,(cy+.04)*H,F.barlow,9,dim,lbl); };
      tog(.235,'HF'); tog(.675,'LF');
      // IN bypass + LED (bottom)
      ledDot(d,.305*W,.905*H,false,210,52,42);
      rr(c,.42*W,.882*H,.07*W,.05*H,3); c.fillStyle=rgb(232,234,232); c.fill();
      rr(c,.42*W,.882*H,.07*W,.05*H,3); c.strokeStyle=rgb(120,122,124); c.lineWidth=1; c.stroke();
      textC(d,.50*W,.952*H,F.bebas,17,wt,'IN'); } };
  P.studiopitch     = rackSpec({title:'STUDIO PITCH',      accent:[160,185,150], names:['Pitch','Tone','Mix','Pan']});
  P.studioplate     = rackSpec({title:'STUDIO PLATE',      accent:[200,180,168], names:['Time','Tone','Depth','Mix']});
  P.studioverb      = rackSpec({title:'STUDIO VERB',       accent:[120,195,175], names:['Time','Tone','Depth','Mix']});
  P.studioflanger   = rackSpec({title:'STUDIO FLANGER',    accent:[205,170,75],  names:['Rate','Depth','Regen','Tone','Mix']});
  P.studiowahfilter = rackSpec({title:'STUDIO WAH FILTER', accent:[130,180,155], names:['Sens','Attack','Release','Pedal','Auto']});
  P.synthfilter     = rackSpec({title:'SYNTH FILTER',      accent:[150,185,130], names:['Sens','Attack','Release','Type','Mix']});
  // Tape Echo — Roland RE-201 Space Echo look: dark body, VU + PEAK top-left,
  // big chrome MODE SELECTOR centre (Time), green panel of echo knobs right,
  // red POWER, bottom switch strip. Parody brand TOPLAND / "STARDUST ECHO TE-102".
  // RS params (5 knobs): Time0 Feedback1 Filter2 Stereo3 Mix4.
  P.tapeecho = { w:960, h:300,
    knobs:[
      {id:0,cx:.380,cy:.530,r:.060,style:'moog'},          // Time  (= MODE SELECTOR, chrome)
      {id:1,cx:.585,cy:.495,r:.040,style:'boss'},          // Feedback (= INTENSITY)
      {id:2,cx:.670,cy:.495,r:.040,style:'boss'},          // Filter   (= BASS/TREBLE)
      {id:3,cx:.755,cy:.495,r:.040,style:'boss'},          // Stereo
      {id:4,cx:.840,cy:.495,r:.040,style:'boss'}],         // Mix      (= ECHO VOLUME)
    tick:rgb(60,80,52), ptr:rgb(232,234,230),
    draw(d){ const {ctx:c,W,H}=d, m=7;
      // body
      c.fillStyle=rgb(20,21,20); c.fillRect(0,0,W,H);
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(40,42,40)); bg.addColorStop(1,rgb(24,25,24));
      rr(c,m,m,W-2*m,H-2*m,10); c.fillStyle=bg; c.fill();
      rr(c,m,m,W-2*m,H-2*m,10); c.strokeStyle=rgb(8,9,8); c.lineWidth=2; c.stroke();
      const wt=rgb(232,234,230), dim=rgb(176,180,176), grn=[74,98,62];
      // brushed-silver trim strips (top & bottom) — thick, like the real unit
      const trim=(y,h)=>{ const tg=c.createLinearGradient(0,y,0,y+h);
        tg.addColorStop(0,rgb(222,224,228)); tg.addColorStop(.42,rgb(178,180,186));
        tg.addColorStop(.58,rgb(150,152,158)); tg.addColorStop(1,rgb(202,204,210));
        c.fillStyle=tg; c.fillRect(m+2,y,W-2*m-4,h);
        c.strokeStyle=rgb(84,86,90); c.lineWidth=1; c.strokeRect(m+2.5,y+.5,W-2*m-5,h-1); };
      trim(m+2, H*0.15); trim(H-m-2-H*0.06, H*0.06);
      // equipment + model + brand — BLACK, printed on the top silver strip
      const blk=rgb(24,24,26), tcy=.105*H;
      textC(d,.045*W,tcy,F.bebas,24,blk,'GALAXY ECHO','left');
      textC(d,.205*W,tcy+1,F.barlow,13,blk,'TE-102','left');
      textC(d,.955*W,tcy,F.bebas,22,blk,'TOPLAND','right');
      // VU meter (top-left) — big analog meter filling the left space
      const vux=.05*W, vuy=.31*H, vuw=.155*W, vuh=.34*H;
      ledDot(d,.062*W,.275*H,false,210,50,40); textC(d,.128*W,.278*H,F.barlow,8.5,dim,'PEAK LEVEL','left');
      rr(c,vux-5,vuy-5,vuw+10,vuh+10,4); const bzl=c.createLinearGradient(0,vuy-5,0,vuy+vuh+5);
      bzl.addColorStop(0,rgb(208,210,214)); bzl.addColorStop(1,rgb(148,150,154)); c.fillStyle=bzl; c.fill();
      rr(c,vux-5,vuy-5,vuw+10,vuh+10,4); c.strokeStyle=rgb(68,70,74); c.lineWidth=1; c.stroke();
      const vg=c.createLinearGradient(0,vuy,0,vuy+vuh); vg.addColorStop(0,rgb(216,226,198)); vg.addColorStop(1,rgb(186,200,168));
      rr(c,vux,vuy,vuw,vuh,2); c.fillStyle=vg; c.fill();
      rr(c,vux,vuy,vuw,vuh,2); c.strokeStyle=rgb(118,126,108); c.lineWidth=1; c.stroke();
      const vcx=vux+vuw/2, vcy=vuy+vuh*1.22, vR=vuh*0.98, va0=Math.PI*1.28, va1=Math.PI*1.72, vk=0.70;
      c.lineWidth=2; c.strokeStyle=rgb(42,46,40); c.beginPath(); c.arc(vcx,vcy,vR,va0,va0+(va1-va0)*vk); c.stroke();
      c.strokeStyle=rgb(182,42,34); c.beginPath(); c.arc(vcx,vcy,vR,va0+(va1-va0)*vk,va1); c.stroke();
      for(let i=0;i<=8;i++){ const t=va0+(va1-va0)*(i/8); c.strokeStyle=(i/8)>=vk?rgb(182,42,34):rgb(42,46,40); c.lineWidth=1.2;
        c.beginPath(); c.moveTo(vcx+Math.cos(t)*(vR-6),vcy+Math.sin(t)*(vR-6)); c.lineTo(vcx+Math.cos(t)*vR,vcy+Math.sin(t)*vR); c.stroke(); }
      [['20',0.04],['10',0.30],['0',0.60],['+',0.90]].forEach(p=>{ const t=va0+(va1-va0)*p[1];
        textC(d,vcx+Math.cos(t)*(vR-17),vcy+Math.sin(t)*(vR-17)+3,F.barlow,8,p[1]>=vk?rgb(182,42,34):rgb(42,46,40),p[0]); });
      textC(d,vcx,vuy+vuh*0.72,F.barlow,11,rgb(50,54,46),'VU');
      const vnt=va0+(va1-va0)*0.42; c.strokeStyle=rgb(28,30,26); c.lineWidth=1.8;
      c.beginPath(); c.moveTo(vcx,vcy); c.lineTo(vcx+Math.cos(vnt)*vR*0.96,vcy+Math.sin(vnt)*vR*0.96); c.stroke();
      c.beginPath(); c.arc(vcx,vcy,2.8,0,7); c.fillStyle=rgb(28,30,26); c.fill();
      // green panel behind MODE SELECTOR
      const gp=(x,y,wd,ht)=>{ rr(c,x,y,wd,ht,5); c.fillStyle=rgb(grn[0],grn[1],grn[2]); c.fill();
        rr(c,x,y,wd,ht,5); c.strokeStyle=rgb(34,46,28); c.lineWidth=1.5; c.stroke(); };
      gp(.262*W,.250*H,.242*W,.520*H);
      textC(d,.383*W,.272*H,F.barlow,11,rgb(232,238,228),'MODE SELECTOR');
      textC(d,.383*W,.748*H,F.barlow,9,rgb(224,230,218),'TIME / REPEAT');
      // number ring (1-12) around the selector
      const sx=.380*W, sy=.530*H, sR=.060*W*1.30;
      for(let i=0;i<12;i++){ const ta=Math.PI*0.75+(Math.PI*1.5)*(i/11);
        textC(d,sx+Math.cos(ta)*sR,sy+Math.sin(ta)*sR+3,F.barlow,8,rgb(226,232,222),String(i+1)); }
      // green panel behind the echo knobs (right)
      gp(.535*W,.270*H,.350*W,.50*H);
      ['FEEDBACK','FILTER','STEREO','MIX'].forEach((t,i)=>{ const kx=(.585+i*.085)*W;
        textC(d,kx,.350*H,F.barlow,10.5,rgb(232,238,228),t); });
      // POWER (dark body, right of the green panel — as on the real unit)
      textC(d,.934*W,.405*H,F.barlow,10,dim,'POWER');
      rr(c,.913*W,.460*H,.042*W,.120*H,3); c.fillStyle=rgb(40,40,42); c.fill();
      rr(c,.913*W,.460*H,.042*W,.120*H,3); c.strokeStyle=rgb(70,72,72); c.lineWidth=1; c.stroke();
      c.beginPath(); c.arc(.934*W,.520*H,.013*W,0,7); c.fillStyle=rgb(206,58,40); c.fill();
      textC(d,.934*W,.640*H,F.barlow,7,dim,'ON');
      // bottom switch strip (decorative input switches + echo cancel)
      const sy2=.825*H;
      ['MIC','FROM P.A.','INSTRUMENT','MODE','OUT/IN'].forEach((t,i)=>{ const jx=(.055+i*.085)*W;
        rr(c,jx,sy2-.035*H,.05*W,.07*H,2); c.fillStyle=rgb(30,31,30); c.fill();
        rr(c,jx+.012*W,sy2-.02*H,.026*W,.04*H,1); c.fillStyle=rgb(120,122,124); c.fill();
        textC(d,jx+.025*W,sy2+.058*H,F.barlow,6.5,dim,t); });
      rr(c,.86*W,sy2-.04*H,.075*W,.08*H,3); c.fillStyle=rgb(36,37,36); c.fill();
      rr(c,.86*W,sy2-.04*H,.075*W,.08*H,3); c.strokeStyle=rgb(70,72,72); c.lineWidth=1; c.stroke();
      textC(d,.8975*W,sy2+.004*H,F.barlow,7,dim,'ECHO CANCEL'); } };

  // ── BOX DC30 — Vox AC30 Top Boost (head) parody. Black tolex + gold piping,
  // 3 louver vents, diamond handle, MAROON control panel. The 6 the game
  // knobs (Gain/Treble/Bass/Mid/Pres/Bright) wear the real panel names
  // (Volume/Treble/Bass/Mid/Tone Cut/Bright). INPUTS jacks + STANDBY/POWER
  // toggles + 'A BOX PRODUCT'. Brand VOX->BOX, AC30->DC30.
  // ── BOX DC30 (Vox AC30C2, Custom series) — full front panel, 1:1 with the
  //    real amp (modelled from Vox_ac30c2.pdf). 10 knobs; the AC30 has NO Bright
  //    / Mid / EQ control (Standby + Power are real but non-audio). the game is
  //    bridged via rs_knob_to_vst_param.json (Gain→TB Vol, Treble→Treble,
  //    Bass→Bass, Pres→Tone Cut inverted; RS Mid/Bright have no AC30 control).
  //    ids: 0 NormalVol 1 TBVol 2 Treble 3 Bass 4 RevTone 5 RevLevel
  //    6 Speed 7 Depth 8 ToneCut 9 Master.
  P.boxdc30 = { w:1400, h:560, ptr:rgb(240,237,230),
    knobs:[
      {id:0,cx:.140,cy:.738,r:.019,style:'vox'},   // NORMAL VOLUME
      {id:1,cx:.210,cy:.738,r:.019,style:'vox'},   // TOP BOOST VOLUME (RS Gain)
      {id:2,cx:.268,cy:.738,r:.019,style:'vox'},   // TOP BOOST TREBLE (RS Treble)
      {id:3,cx:.326,cy:.738,r:.019,style:'vox'},   // TOP BOOST BASS   (RS Bass)
      {id:4,cx:.405,cy:.738,r:.019,style:'vox'},   // REVERB TONE
      {id:5,cx:.463,cy:.738,r:.019,style:'vox'},   // REVERB LEVEL
      {id:6,cx:.542,cy:.738,r:.019,style:'vox'},   // TREMOLO SPEED
      {id:7,cx:.600,cy:.738,r:.019,style:'vox'},   // TREMOLO DEPTH
      {id:8,cx:.679,cy:.738,r:.019,style:'vox'},   // MASTER TONE CUT  (RS Pres inv)
      {id:9,cx:.737,cy:.738,r:.019,style:'vox'} ], // MASTER VOLUME
    // input cable selector (id 10): click the input area to cycle the cable
    // Normal -> Both(jumpered) -> Top Boost. Drawn as a real plugged-in cable in
    // draw(); `hidden` so the engine doesn't also stamp a lever over it.
    sw3:[{id:10,cx:.075,cy:.753,hw:34,hh:34,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const gold=rgb(190,154,72), wine=rgb(98,24,42), wineHi=rgb(124,34,54),
            ink=rgb(238,228,208), boxLn='rgba(230,216,200,0.55)', chr=rgb(190,194,200);
      // ── tolex body ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(1,rgb(13,12,14));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.022)';
      for(let x=-H;x<W;x+=8*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.20)';
      for(let x=-H;x<W;x+=8*s){ c.beginPath(); c.moveTo(x+4*s,0); c.lineTo(x+4*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(246,248,250)); g.addColorStop(1,rgb(116,120,126));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(52,54,58); c.lineWidth=0.7*s; c.stroke(); };
      // ── maroon panel strip near the bottom ──
      const py=H*.58, ph=H*.36, px=W*.025, pw=W*.95;
      const lblY=py+ph*.85;
      // ── gold piping (top + above the panel) ──
      [H*.05, py-H*.025].forEach(yy=>{ c.beginPath(); c.moveTo(W*.04,yy); c.lineTo(W*.96,yy); c.strokeStyle=gold; c.lineWidth=1.8*s; c.stroke(); });
      // ── louver vents (across the wide head) ──
      const vent=(x0,x1)=>{ const vy=H*.10, vh=H*.20, vw=(x1-x0)*W;
        rr(c,x0*W,vy,vw,vh,4*s); c.fillStyle=rgb(7,7,8); c.fill();
        c.save(); rr(c,x0*W,vy,vw,vh,4*s); c.clip(); c.strokeStyle='rgba(150,152,158,0.15)'; c.lineWidth=2.2*s;
        for(let yy=vy+6*s; yy<vy+vh-3*s; yy+=6.5*s){ c.beginPath(); c.moveTo(x0*W+6*s,yy); c.lineTo(x1*W-6*s,yy); c.stroke(); }
        c.restore(); };
      vent(.20,.34); vent(.37,.49); vent(.52,.64); vent(.67,.80);
      // ── centre diamond handle (just above the panel) ──
      const hx0=.45*W, hx1=.55*W, hy=H*.34, hh=H*.10;
      rr(c,hx0,hy,hx1-hx0,hh,6*s); c.fillStyle=rgb(13,13,14); c.fill();
      c.save(); rr(c,hx0,hy,hx1-hx0,hh,6*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.07)'; c.lineWidth=1;
      for(let x=hx0-hh;x<hx1+hh;x+=7*s){ c.beginPath(); c.moveTo(x,hy); c.lineTo(x+hh,hy+hh); c.stroke();
        c.beginPath(); c.moveTo(x,hy+hh); c.lineTo(x+hh,hy); c.stroke(); }
      c.restore();
      [hx0,hx1].forEach(bx=>{ rr(c,bx-6*s,hy+hh*.22,8*s,hh*.56,3*s); c.fillStyle=chr; c.fill(); bolt(bx,hy+hh*.5,3.2*s); });
      // ── 4 corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.11; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(10,10,11); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── maroon control panel (brushed wine) ──
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,wineHi); pg.addColorStop(0.5,wine); pg.addColorStop(1,rgb(80,18,34));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(150,42,60); c.lineWidth=1.4*s; c.stroke();
      // section group box (rounded rect with the name breaking the top edge)
      const box=(x0,x1,name)=>{ const bx0=x0*W,bx1=x1*W, by0=py+ph*.07, by1=py+ph*.95, mid=(bx0+bx1)/2, r=5*s;
        setFont(d,F.barlow,15); const tw=name?c.measureText(name).width+10*s:0;
        c.strokeStyle=boxLn; c.lineWidth=1.4*s; c.beginPath();
        c.moveTo(mid-tw/2,by0); c.lineTo(bx0+r,by0); c.arcTo(bx0,by0,bx0,by0+r,r);
        c.lineTo(bx0,by1-r); c.arcTo(bx0,by1,bx0+r,by1,r); c.lineTo(bx1-r,by1); c.arcTo(bx1,by1,bx1,by1-r,r);
        c.lineTo(bx1,by0+r); c.arcTo(bx1,by0,bx1-r,by0,r); c.lineTo(mid+tw/2,by0); c.stroke();
        if(name) textSpaced(d,mid,by0,F.barlow,15,ink,name,0.10); };
      const lbl=(cx,t)=>textSpaced(d,cx*W,lblY,F.barlow,10.5,ink,t,0.03);
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,6*s,0,7); c.fillStyle=rgb(18,16,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.2*s,0,7); c.fillStyle=rgb(52,52,56); c.fill(); };
      // INPUTS: HIGH/LOW (rotated) + 2x2 jacks. A guitar cable is plugged into the
      // selected channel; clicking the input cycles Normal -> Both(jumpered) ->
      // Top Boost (id 10). Both = main cable in Top Boost + a jumper patch.
      box(.025,.108,'INPUTS');
      const inp = (vals && vals[10] != null) ? vals[10] : 1.0;
      const jyH=py+ph*.34, jyL=py+ph*.62, ijN=.058*W, ijT=.092*W;
      c.save(); c.translate(.034*W,jyH); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6.5,ink,'HIGH',0.04); c.restore();
      c.save(); c.translate(.034*W,jyL); c.rotate(-Math.PI/2); textSpaced(d,0,0,F.barlow,6.5,ink,'LOW',0.04); c.restore();
      jack(ijN,jyH); jack(ijT,jyH); jack(ijN,jyL); jack(ijT,jyL);
      // plugged-in cable: black barrel + chrome collar in the jack + lead off the bottom
      const plug=(jx,jy)=>{
        rr(c,jx-4.6*s,jy-5.5*s,9.2*s,7*s,2*s); c.fillStyle=rgb(44,44,48); c.fill();          // barrel
        const cg=c.createLinearGradient(jx-4.6*s,jy,jx+4.6*s,jy); cg.addColorStop(0,rgb(182,186,192)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(182,186,192));
        rr(c,jx-4.6*s,jy+1.5*s,9.2*s,4*s,1.5*s); c.fillStyle=cg; c.fill();                    // chrome collar
        c.beginPath(); c.moveTo(jx,jy+6*s);
        c.bezierCurveTo(jx+4*s,jy+34*s, jx-34*s,jy+40*s, jx-40*s,H*0.99);                      // lead curving down off the panel
        c.lineWidth=5.5*s; c.lineCap='round'; c.strokeStyle=rgb(16,16,18); c.stroke();
        c.lineWidth=1.8*s; c.strokeStyle='rgba(255,255,255,0.12)'; c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const mx=(x1+x2)/2, my=Math.max(y1,y2)+16*s;
        c.beginPath(); c.arc(x1,y1,3.2*s,0,7); c.fillStyle=rgb(44,44,48); c.fill();
        c.beginPath(); c.arc(x2,y2,3.2*s,0,7); c.fillStyle=rgb(44,44,48); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo(mx,my,x2,y2);
        c.lineWidth=4*s; c.lineCap='round'; c.strokeStyle=rgb(16,16,18); c.stroke();
        c.lineWidth=1.4*s; c.strokeStyle='rgba(255,255,255,0.10)'; c.stroke(); c.lineCap='butt'; };
      let mode;
      if (inp < 0.25)      { plug(ijN,jyH); mode='NORMAL'; }
      else if (inp < 0.75) { jumper(ijT,jyL,ijN,jyH); plug(ijT,jyH); mode='JUMPERED'; }
      else                 { plug(ijT,jyH); mode='TOP BOOST'; }
      textSpaced(d,ijN,lblY,F.barlow,6.5,ink,'NORMAL',0.02); textSpaced(d,ijT,lblY,F.barlow,5.5,ink,'TOP BOOST',0.02);
      textSpaced(d,.066*W,py+ph*.03,F.barlow,7,gold,mode,0.06);
      // NORMAL channel volume
      box(.110,.172,'NORMAL'); lbl(.140,'VOLUME');
      // TOP BOOST channel (volume + treble + bass)
      box(.180,.356,'TOP BOOST'); lbl(.210,'VOLUME'); lbl(.268,'TREBLE'); lbl(.326,'BASS');
      // REVERB (tone + level)
      box(.376,.492,'REVERB'); lbl(.405,'TONE'); lbl(.463,'LEVEL');
      // TREMOLO (speed + depth)
      box(.513,.629,'TREMOLO'); lbl(.542,'SPEED'); lbl(.600,'DEPTH');
      // MASTER (tone cut + volume)
      box(.650,.766,'MASTER'); lbl(.679,'TONE CUT'); lbl(.737,'VOLUME');
      // STANDBY + POWER (real switches, non-audio) + status jewels
      const swY=py+ph*.42;
      batToggle(d,.820*W,swY,9*s,true); batToggle(d,.884*W,swY,9*s,true);
      textSpaced(d,.820*W,py+ph*.13,F.barlow,8.5,ink,'STANDBY',0.04); textSpaced(d,.884*W,py+ph*.13,F.barlow,8.5,ink,'POWER',0.04);
      textSpaced(d,.820*W,lblY,F.barlow,6.5,ink,'HT  ON',0.03); textSpaced(d,.884*W,lblY,F.barlow,6.5,ink,'OFF  ON',0.03);
      ledDot(d,.840*W,swY+ph*.20,true,70,200,90); ledDot(d,.904*W,swY+ph*.20,true,212,60,52);
      // maker (bottom-right, parody of "a VOX product")
      textSpaced(d,(px+pw)-58*s,py+ph*.90,F.bebas,15,rgb(226,196,160),'BOX',0.06);
      textSpaced(d,(px+pw)-22*s,py+ph*.905,F.barlow,8,rgb(216,188,156),'PRODUCT',0.05); } };

  // ── BENDER SUPERNOVA 22 (Fender Super-Sonic 22) — black tolex head ─────────
  // ── BENDER SUPERNOVA 22 (Fender Super-Sonic 22) — 2-channel head, 1:1 with the
  //    real panel. 10 cream knobs + 2 toggles (Norm/Fat, Vintage/Burn). Presence
  //    (id 12) is hidden. ids: 0 VintVol 1 VintTreble 2 VintBass 3 NormFat
  //    4 Channel 5 Gain1 6 Gain2 7 BurnTreble 8 BurnBass 9 BurnMid 10 BurnVol
  //    11 Reverb. RS: Gain->Channel, Treble/Bass/Mid->Burn, Bright->Norm/Fat.
  P.bendersupernova22 = { w:1520, h:600, ptr:rgb(238,238,234),
    knobs:[
      {id:0, cx:.095,cy:.224,r:.018,style:'cream'},   // VINTAGE Volume
      {id:1, cx:.190,cy:.224,r:.018,style:'cream'},   // VINTAGE Treble
      {id:2, cx:.248,cy:.224,r:.018,style:'cream'},   // VINTAGE Bass
      {id:5, cx:.355,cy:.224,r:.018,style:'cream'},   // BURN Gain 1
      {id:6, cx:.420,cy:.224,r:.018,style:'cream'},   // BURN Gain 2
      {id:7, cx:.485,cy:.224,r:.018,style:'cream'},   // BURN Treble (RS Treble)
      {id:8, cx:.550,cy:.224,r:.018,style:'cream'},   // BURN Bass   (RS Bass)
      {id:9, cx:.615,cy:.224,r:.018,style:'cream'},   // BURN Middle (RS Mid)
      {id:10,cx:.680,cy:.224,r:.018,style:'cream'},   // BURN Volume
      {id:11,cx:.760,cy:.224,r:.018,style:'cream'} ], // REVERB
    switches:[
      {id:3,cx:.140,cy:.224,hs:.009,style:'bat'},     // Norm/Fat   (RS Bright)
      {id:4,cx:.300,cy:.224,hs:.009,style:'bat'} ],   // Vintage/Burn (RS Gain morph)
    draw(d){ const {ctx:c,W,H,s}=d;
      const ink=rgb(230,230,226), chr=rgb(206,210,216), faint='rgba(206,208,214,0.6)';
      // ── black textured tolex body ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(33,32,34)); bgr.addColorStop(0.5,rgb(24,23,25)); bgr.addColorStop(1,rgb(13,12,14));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.020)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.24)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x+3*s,0); c.lineTo(x+3*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(248,250,252)); g.addColorStop(0.6,rgb(176,180,186)); g.addColorStop(1,rgb(96,100,106));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(46,48,52); c.lineWidth=0.7*s; c.stroke(); };
      // ── moulded top strap handle + chrome mounting brackets ──
      const hcx=.5*W, hw=.085*W, htop=H*.006, hh=H*.052;
      [hcx-hw, hcx+hw].forEach(bx=>{ rr(c,bx-13*s,htop+hh*0.1,26*s,hh*0.95,3*s);
        const bg=c.createLinearGradient(0,htop,0,htop+hh); bg.addColorStop(0,rgb(224,228,234)); bg.addColorStop(0.5,rgb(150,154,160)); bg.addColorStop(1,rgb(196,200,206));
        c.fillStyle=bg; c.fill(); c.strokeStyle=rgb(96,98,104); c.lineWidth=0.8*s; c.stroke(); bolt(bx,htop+hh*0.5,2.6*s); });
      rr(c,hcx-hw,htop,2*hw,hh,hh*0.45); const hg=c.createLinearGradient(0,htop,0,htop+hh);
      hg.addColorStop(0,rgb(34,34,37)); hg.addColorStop(0.5,rgb(15,15,17)); hg.addColorStop(1,rgb(40,40,44));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      c.save(); rr(c,hcx-hw,htop,2*hw,hh,hh*0.45); c.clip(); c.strokeStyle='rgba(255,255,255,0.07)'; c.lineWidth=1;
      for(let x=hcx-hw+5*s;x<hcx+hw;x+=6*s){ c.beginPath(); c.moveTo(x,htop); c.lineTo(x,htop+hh); c.stroke(); } c.restore();
      // ── chrome corner caps (4) — prominent, with a diagonal highlight ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.105; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.30,cyy+dy*k*0.30,cxx,cyy+dy*k); c.closePath();
        const gg=c.createLinearGradient(cxx,cyy,cxx+dx*k,cyy+dy*k);
        gg.addColorStop(0,rgb(232,236,240)); gg.addColorStop(0.45,rgb(150,154,160)); gg.addColorStop(0.55,rgb(196,200,206)); gg.addColorStop(1,rgb(110,114,120));
        c.fillStyle=gg; c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.9*s; c.stroke(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── black control faceplate (wide, brushed) ──
      const py=H*.09, ph=H*.32, px=W*.025, pw=W*.95;
      rr(c,px-2*s,py-2*s,pw+4*s,ph+4*s,8*s); c.fillStyle=rgb(6,6,8); c.fill();   // bezel
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(40,40,44)); pg.addColorStop(0.5,rgb(24,24,27)); pg.addColorStop(1,rgb(15,15,18));
      rr(c,px,py,pw,ph,6*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,6*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.035)'; c.lineWidth=0.7*s;
      for(let yy=py+2.5*s; yy<py+ph; yy+=2.6*s){ c.beginPath(); c.moveTo(px,yy); c.lineTo(px+pw,yy); c.stroke(); }
      c.strokeStyle='rgba(255,255,255,0.10)'; c.lineWidth=1.1*s; c.beginPath(); c.moveTo(px,py+1.5*s); c.lineTo(px+pw,py+1.5*s); c.stroke();
      c.restore();
      rr(c,px,py,pw,ph,6*s); c.strokeStyle=rgb(6,6,8); c.lineWidth=1.2*s; c.stroke();
      // section dividers (white hairlines): Vintage | Burn | Reverb
      [.323,.726].forEach(xx=>{ c.beginPath(); c.moveTo(xx*W,py+ph*.12); c.lineTo(xx*W,py+ph*.92); c.strokeStyle='rgba(210,212,218,0.55)'; c.lineWidth=1.1*s; c.stroke(); });
      const cy=py+ph*.42, lblY=py+ph*.84;
      // ── INPUT jack ──
      const ij=.040*W;
      c.beginPath(); c.arc(ij,cy,8*s,0,7); const ig=c.createRadialGradient(ij-2*s,cy-2*s,1*s,ij,cy,8*s); ig.addColorStop(0,rgb(60,58,58)); ig.addColorStop(1,rgb(14,13,13));
      c.fillStyle=ig; c.fill(); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(ij,cy,2.6*s,0,7); c.fillStyle=rgb(54,54,58); c.fill();
      textSpaced(d,ij,lblY,F.barlow,8.5,ink,'INPUT',0.05);
      // ── numerals 1-10 around each cream knob + labels ──
      const numArc=(kx)=>{ setFont(d,F.barlow,6.5); c.fillStyle=faint; c.textAlign='center'; c.textBaseline='middle';
        for(let n=1;n<=10;n++){ const aa=ang((n-1)/9); const rad=.028*W;
          c.fillText(String(n), kx+rad*Math.cos(aa), cy+rad*Math.sin(aa)); } };
      const lbl=(kx,t)=>textSpaced(d,kx*W,lblY,F.barlow,8.5,ink,t,0.02);
      [.095,.190,.248,.355,.420,.485,.550,.615,.680,.760].forEach(kx=>numArc(kx));
      lbl(.095,'VOLUME'); lbl(.190,'TREBLE'); lbl(.248,'BASS');
      lbl(.355,'GAIN 1'); lbl(.420,'GAIN 2'); lbl(.485,'TREBLE'); lbl(.550,'BASS'); lbl(.615,'MIDDLE'); lbl(.680,'VOLUME');
      lbl(.760,'REVERB');
      // ── switch labels (the toggles are drawn by the engine via spec.switches) ──
      textSpaced(d,.140*W,lblY,F.barlow,6,ink,'NORMAL/FAT',0.01);
      textSpaced(d,.300*W,lblY,F.barlow,5.5,ink,'VINTAGE/BURN',0.01);
      // ── RIGHT: "Super-Nova" script + maker + glossy red jewel ──
      const sxc=.880*W, syc=cy;
      c.save(); c.translate(sxc,syc); c.transform(1,0,-0.17,1,0,0); c.textAlign='center'; c.textBaseline='middle';
      const slg=c.createLinearGradient(0,-16*s,0,16*s); slg.addColorStop(0,rgb(248,249,252)); slg.addColorStop(1,rgb(190,194,200));
      setFont(d,F.ink,21); c.lineWidth=2*s; c.strokeStyle=rgb(14,14,16);
      c.strokeText('Super',-20*s,-6*s); c.fillStyle=slg; c.fillText('Super',-20*s,-6*s);
      setFont(d,F.ink,18); c.strokeText('Nova',24*s,8*s); c.fillStyle=slg; c.fillText('Nova',24*s,8*s);
      c.restore();
      textSpaced(d,sxc,syc+ph*.30,F.barlow,6.5,rgb(198,200,206),'BENDER MUSICAL INSTRUMENTS',0.02);
      const jx=.962*W;
      c.beginPath(); c.arc(jx,cy,8*s,0,7); c.fillStyle=rgb(40,40,44); c.fill();
      c.beginPath(); c.arc(jx,cy,8*s,0,7); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
      const jg=c.createRadialGradient(jx-2*s,cy-2*s,0.5*s,jx,cy,5.5*s); jg.addColorStop(0,rgb(255,150,140)); jg.addColorStop(0.5,rgb(220,46,40)); jg.addColorStop(1,rgb(120,16,14));
      c.beginPath(); c.arc(jx,cy,5.5*s,0,7); c.fillStyle=jg; c.fill();
      c.beginPath(); c.arc(jx-2*s,cy-2*s,1.6*s,0,7); c.fillStyle='rgba(255,235,230,0.8)'; c.fill();
      // ── silver/black Fender-style grille cloth (lower portion) ──
      const gy=py+ph+H*.045, gh=H*.94-gy, gx=W*.035, gw=W*.93;
      rr(c,gx-3.5*s,gy-3.5*s,gw+7*s,gh+7*s,9*s); c.fillStyle=rgb(16,16,18); c.fill();   // black frame
      rr(c,gx,gy,gw,gh,6*s); c.fillStyle=rgb(150,151,154); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,6*s); c.clip();
      c.strokeStyle='rgba(232,234,238,0.5)'; c.lineWidth=1;
      for(let x=gx-gh;x<gx+gw;x+=4*s){ c.beginPath(); c.moveTo(x,gy); c.lineTo(x+gh,gy+gh); c.stroke(); }
      c.strokeStyle='rgba(14,14,16,0.5)'; c.lineWidth=1;
      for(let x=gx;x<gx+gw+gh;x+=4*s){ c.beginPath(); c.moveTo(x,gy); c.lineTo(x-gh,gy+gh); c.stroke(); }
      // radial vignette for depth
      const vg=c.createRadialGradient(gx+gw*0.5,gy+gh*0.5,gh*0.2,gx+gw*0.5,gy+gh*0.5,gw*0.62);
      vg.addColorStop(0,'rgba(255,255,255,0.10)'); vg.addColorStop(0.6,'rgba(0,0,0,0.0)'); vg.addColorStop(1,'rgba(0,0,0,0.30)');
      c.fillStyle=vg; c.fillRect(gx,gy,gw,gh);
      c.restore();
      rr(c,gx,gy,gw,gh,6*s); c.strokeStyle=rgb(6,6,7); c.lineWidth=1.6*s; c.stroke();
      // ── "Bender" chrome script logo on a subtle recessed plate (grille, lower-left) ──
      const lx=gx+gw*.18, ly=gy+gh*.55;
      c.save(); c.translate(lx,ly); c.transform(1,0,-0.18,1,0,0); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,72);
      c.fillStyle='rgba(0,0,0,0.28)'; c.fillText('Bender',2.5*s,3*s);                       // drop shadow
      c.lineWidth=2.8*s; c.strokeStyle=rgb(40,42,46); c.strokeText('Bender',0,0);
      const blg=c.createLinearGradient(0,-27*s,0,27*s); blg.addColorStop(0,rgb(252,253,255)); blg.addColorStop(0.45,rgb(214,218,224)); blg.addColorStop(0.55,rgb(176,180,186)); blg.addColorStop(1,rgb(150,154,160));
      c.fillStyle=blg; c.fillText('Bender',0,0);
      c.restore(); } };

  // ── BENDER DELUXE (Fender '57 Deluxe 5E3) — tweed combo, silver panel ─────
  // ── BENDER DELUXE (Fender '57 Deluxe 5E3) — tweed combo, 1:1 with the real
  //    panel: only Tone, Instrument Vol, Mic Vol + 4 jacks. A cable plugs into the
  //    Instrument input (click = Bright input 1 / Normal input 2); turning Mic Vol
  //    up jumpers the Mic channel in. ids: 0 Tone 1 InstVol 2 MicVol 3 Bright
  //    (input). Bass(4)/Presence(5) are hidden. RS: Gain->InstVol, Treble->Tone,
  //    Mid->MicVol, Bright->input, Bass/Pres hidden.
  P.benderdeluxe = { w:860, h:340, ptr:rgb(236,236,232),
    knobs:[
      {id:0,cx:.430,cy:.512,r:.032,style:'vox'},   // TONE       (RS Treble)
      {id:1,cx:.520,cy:.512,r:.032,style:'vox'},   // INST VOL   (RS Gain)
      {id:2,cx:.610,cy:.512,r:.032,style:'vox'} ], // MIC VOL    (RS Mid)
    sw3:[{id:3,cx:.728,cy:.512,hw:30,hh:34,two:true,hidden:true}], // cable: Bright/Normal input (Instrument Hi/Lo)
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const chr=rgb(198,202,208), ink=rgb(42,42,46), faint='rgba(40,40,44,0.62)';
      // ── golden lacquered tweed (diagonal twill weave) ──
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(206,170,98)); bg.addColorStop(0.5,rgb(192,156,88)); bg.addColorStop(1,rgb(170,134,72));
      c.fillStyle=bg; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip();
      c.lineWidth=1.4*s; c.strokeStyle='rgba(232,206,150,0.45)';            // light twill threads
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.lineWidth=1*s; c.strokeStyle='rgba(120,92,46,0.40)';                // dark twill threads
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x+2.5*s,0); c.lineTo(x+2.5*s+H,H); c.stroke(); }
      c.lineWidth=0.8*s; c.strokeStyle='rgba(90,68,34,0.18)';               // faint cross weave
      for(let x=-H;x<W+H;x+=9*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(238,240,244)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.7*s; c.stroke(); };
      // ── leather strap handle (top centre) + chrome end mounts ──
      const hx0=.37*W, hx1=.63*W, hcy=H*.115, hth=H*.07;
      const lg=c.createLinearGradient(0,hcy-hth,0,hcy+hth); lg.addColorStop(0,rgb(108,66,38)); lg.addColorStop(0.5,rgb(78,44,24)); lg.addColorStop(1,rgb(52,28,15));
      rr(c,hx0,hcy-hth,hx1-hx0,2*hth,hth); c.fillStyle=lg; c.fill();
      rr(c,hx0,hcy-hth,hx1-hx0,2*hth,hth); c.strokeStyle=rgb(32,18,10); c.lineWidth=1.2*s; c.stroke();
      c.save(); rr(c,hx0+6*s,hcy-hth+3*s,hx1-hx0-12*s,2*hth-6*s,hth*0.7); c.clip();           // white stitching
      c.setLineDash([5*s,4*s]); c.strokeStyle='rgba(238,228,206,0.7)'; c.lineWidth=1*s;
      c.beginPath(); c.moveTo(hx0+8*s,hcy-hth*0.5); c.lineTo(hx1-8*s,hcy-hth*0.5); c.stroke();
      c.beginPath(); c.moveTo(hx0+8*s,hcy+hth*0.5); c.lineTo(hx1-8*s,hcy+hth*0.5); c.stroke();
      c.setLineDash([]); c.restore();
      [hx0,hx1].forEach(bx=>{ rr(c,bx-9*s,hcy-hth*0.9,18*s,hth*1.8,3*s); c.fillStyle=chr; c.fill();
        c.strokeStyle=rgb(110,112,118); c.lineWidth=0.8*s; c.stroke(); bolt(bx-4*s,hcy-hth*0.4,2.4*s); bolt(bx+4*s,hcy+hth*0.4,2.4*s); });
      // ── brushed-aluminium control panel ──
      const py=H*.30, ph=H*.40, px=W*.035, pw=W*.93;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(208,210,214)); pg.addColorStop(0.5,rgb(184,187,192)); pg.addColorStop(1,rgb(158,161,166));
      rr(c,px,py,pw,ph,4*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,4*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.30)'; c.lineWidth=0.6*s;   // brushed striations
      for(let yy=py+2*s; yy<py+ph; yy+=2.4*s){ c.beginPath(); c.moveTo(px,yy); c.lineTo(px+pw,yy); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,4*s); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.4*s; c.stroke();
      bolt(px+10*s,py+10*s,3*s); bolt(px+pw-10*s,py+10*s,3*s); bolt(px+10*s,py+ph-10*s,3*s); bolt(px+pw-10*s,py+ph-10*s,3*s);
      // ── left cluster: power socket + ground toggle + red jewel ──
      const cy=py+ph*.52, lblY=py+ph*.86;
      c.beginPath(); c.arc(.065*W,cy,9*s,0,7); c.fillStyle=rgb(28,28,30); c.fill(); c.strokeStyle=rgb(120,122,128); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(.065*W-3*s,cy,2*s,0,7); c.fillStyle=rgb(150,152,158); c.fill(); c.beginPath(); c.arc(.065*W+3*s,cy,2*s,0,7); c.fillStyle=rgb(150,152,158); c.fill();
      batToggle(d,.115*W,cy,8*s,true);
      ledDot(d,.158*W,cy,true,224,52,46); c.beginPath(); c.arc(.158*W,cy,8*s,0,7); c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke();
      // ── "Bender Deluxe" script + maker text ──
      c.save(); c.translate(.235*W,py+ph*.34); c.transform(1,0,-0.16,1,0,0);
      setFont(d,F.ink,21); c.textAlign='center'; c.textBaseline='middle';
      c.fillStyle=rgb(40,40,44); c.fillText('Bender',-26*s,0);
      setFont(d,F.ink,17); c.fillText('Deluxe',24*s,8*s);
      c.restore();
      textSpaced(d,.235*W,py+ph*.66,F.barlow,7.5,ink,'BENDER ELECTRIC INSTRUMENT CO.',0.02);
      textSpaced(d,.235*W,py+ph*.80,F.barlow,7,faint,'FULLERTON, CALIFORNIA',0.05);
      // ── 3 chicken-head knobs (Tone / Inst Vol / Mic Vol): numerals 1-12 + labels ──
      const numArc=(kx)=>{ setFont(d,F.barlow,7); c.fillStyle=faint; c.textAlign='center'; c.textBaseline='middle';
        for(let n=1;n<=12;n++){ const aa=ang((n-1)/11); const rad=.046*W;
          c.fillText(String(n), kx+rad*Math.cos(aa), cy+rad*Math.sin(aa)); } };
      const lbl=(kx,t)=>textSpaced(d,kx*W,lblY,F.barlow,10,ink,t,0.03);
      [.430,.520,.610].forEach(kx=>numArc(kx));
      lbl(.430,'TONE'); lbl(.520,'INST. VOL'); lbl(.610,'MIC. VOL');
      // ── right: 2x2 inputs — INSTRUMENT + MIC columns, each with Hi(1)/Lo(2) ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7.5*s,0,7); c.fillStyle=rgb(24,23,23); c.fill(); c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,2.6*s,0,7); c.fillStyle=rgb(60,60,64); c.fill(); };
      const xI=.728*W, xM=.876*W, cyHi=cy-13*s, cyLo=cy+13*s;
      jack(xI,cyHi); jack(xI,cyLo); jack(xM,cyHi); jack(xM,cyLo);
      textSpaced(d,xI,lblY,F.barlow,8.5,ink,'INSTRUMENT',0.02);
      textSpaced(d,xM,lblY,F.barlow,9,ink,'MIC.',0.03);
      textSpaced(d,xI-30*s,cyHi,F.barlow,6,faint,'1',0); textSpaced(d,xI-30*s,cyLo,F.barlow,6,faint,'2',0);
      // guitar cable into the Instrument input (Bright = input 1 / Normal = input 2)
      const brightOn = (vals && vals[3] != null) ? vals[3] >= 0.5 : true;
      const micOn = (vals && vals[2] != null) ? vals[2] > 0.001 : false;
      const plug=(jx,jy)=>{ rr(c,jx-4.4*s,jy-5.5*s,8.8*s,6.5*s,2*s); c.fillStyle=rgb(40,40,44); c.fill();
        const cg=c.createLinearGradient(jx-4.4*s,jy,jx+4.4*s,jy); cg.addColorStop(0,rgb(182,186,192)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(182,186,192));
        rr(c,jx-4.4*s,jy+1*s,8.8*s,3.6*s,1.4*s); c.fillStyle=cg; c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx+4*s,jy+34*s, jx-30*s,jy+42*s, jx-36*s,H*0.99);
        c.lineWidth=5*s; c.lineCap='round'; c.strokeStyle=rgb(20,20,22); c.stroke();
        c.lineWidth=1.6*s; c.strokeStyle='rgba(255,255,255,0.10)'; c.stroke(); c.lineCap='butt'; };
      if (micOn) {  // jumper Mic channel into the Instrument channel (the 5E3 trick)
        const x1=xI, y1=cyLo, x2=xM, y2=cyHi, my=Math.max(y1,y2)+13*s;
        c.beginPath(); c.arc(x1,y1,3*s,0,7); c.fillStyle=rgb(40,40,44); c.fill();
        c.beginPath(); c.arc(x2,y2,3*s,0,7); c.fillStyle=rgb(40,40,44); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo((x1+x2)/2,my,x2,y2);
        c.lineWidth=3.6*s; c.lineCap='round'; c.strokeStyle=rgb(20,20,22); c.stroke(); c.lineCap='butt'; }
      plug(xI, brightOn ? cyHi : cyLo); } };
  P.tw26 = P.benderdeluxe;   // show the face on the current (pre-rename) TW26.vst3 too

  // ── SILLA BOOGIE DUO RECTIFIER (Mesa/Boogie 3-Channel Dual Rectifier Solo
  //    Head) — diamond-plate head, 1:1 with the real panel. Layout L->R:
  //    Power/Standby/jewel, Duo Rectifier logo, Solo/Output, then CH3/CH2/CH1
  //    (each Presence/Bass · Master/Mid · Gain/Treble + mode + LED), Input.
  //    ids: 0 Channel 1 Output 2 Rectifier, Green 3-9, Orange 10-16, Red 17-23.
  P.dualrect = { w:1700, h:680, ptr:rgb(238,240,244),
    knobs:[
      {id:1, cx:.250,cy:.729,r:.014,style:'fender'},                              // OUTPUT
      // CH3 RED
      {id:21,cx:.300,cy:.729,r:.014,style:'fender'},{id:22,cx:.345,cy:.729,r:.014,style:'fender'},{id:17,cx:.390,cy:.729,r:.014,style:'fender'},
      {id:20,cx:.300,cy:.846,r:.014,style:'fender'},{id:19,cx:.345,cy:.846,r:.014,style:'fender'},{id:18,cx:.390,cy:.846,r:.014,style:'fender'},
      // CH2 ORANGE
      {id:14,cx:.475,cy:.729,r:.014,style:'fender'},{id:15,cx:.520,cy:.729,r:.014,style:'fender'},{id:10,cx:.565,cy:.729,r:.014,style:'fender'},
      {id:13,cx:.475,cy:.846,r:.014,style:'fender'},{id:12,cx:.520,cy:.846,r:.014,style:'fender'},{id:11,cx:.565,cy:.846,r:.014,style:'fender'},
      // CH1 GREEN
      {id:7, cx:.650,cy:.729,r:.014,style:'fender'},{id:8, cx:.695,cy:.729,r:.014,style:'fender'},{id:3, cx:.740,cy:.729,r:.014,style:'fender'},
      {id:6, cx:.650,cy:.846,r:.014,style:'fender'},{id:5, cx:.695,cy:.846,r:.014,style:'fender'},{id:4, cx:.740,cy:.846,r:.014,style:'fender'} ],
    sw3:[
      {id:23,cx:.428,cy:.760,hw:11,hh:20},          // Red mode  Raw/Vtg/Modern
      {id:16,cx:.603,cy:.760,hw:11,hh:20},          // Orange mode
      {id:9, cx:.778,cy:.760,hw:11,hh:20,two:true}, // Green mode Clean/Pushed
      {id:0, cx:.880,cy:.787,hw:11,hh:22},          // Channel select 1/2/3
      {id:2, cx:.915,cy:.787,hw:11,hh:22,two:true} ],// Rectifier Spongy/Bold
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,233,236), faint='rgba(220,222,226,0.6)';
      // ── black tolex body ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,26,28)); bgr.addColorStop(1,rgb(12,12,14));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r); g.addColorStop(0,rgb(70,70,74)); g.addColorStop(1,rgb(20,20,22));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); };
      // corner caps
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(8,8,9); c.fill(); bolt(cxx+dx*k*0.45,cyy+dy*k*0.45,2.6*s); };
      // ── diamond-plate upper panel ──
      const dy0=H*.10, dh=H*.46, dx0=W*.03, dw=W*.94;
      const dg=c.createLinearGradient(0,dy0,0,dy0+dh); dg.addColorStop(0,rgb(214,216,220)); dg.addColorStop(0.5,rgb(176,179,184)); dg.addColorStop(1,rgb(198,201,206));
      rr(c,dx0,dy0,dw,dh,5*s); c.fillStyle=dg; c.fill();
      c.save(); rr(c,dx0,dy0,dw,dh,5*s); c.clip();
      // diamond tread: rows of slanted chrome dashes
      const step=24*s;
      for(let yy=dy0; yy<dy0+dh; yy+=step){ const off=((Math.round((yy-dy0)/step))%2)*step*0.5;
        for(let xx=dx0-step; xx<dx0+dw+step; xx+=step){ const px=xx+off;
          c.strokeStyle='rgba(255,255,255,0.55)'; c.lineWidth=2.4*s; c.beginPath(); c.moveTo(px,yy+4*s); c.lineTo(px+9*s,yy-3*s); c.stroke();
          c.strokeStyle='rgba(60,62,66,0.55)'; c.lineWidth=2.4*s; c.beginPath(); c.moveTo(px+1*s,yy+5*s); c.lineTo(px+10*s,yy-2*s); c.stroke();
          c.strokeStyle='rgba(255,255,255,0.45)'; c.lineWidth=2.2*s; c.beginPath(); c.moveTo(px+11*s,yy-3*s); c.lineTo(px+20*s,yy+4*s); c.stroke(); } }
      c.restore();
      rr(c,dx0,dy0,dw,dh,5*s); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.4*s; c.stroke();
      // ── SILLA badge + two vent grilles ──
      const vent=(x0,x1)=>{ const vy=dy0+dh*.30, vh=dh*.42; rr(c,x0*W,vy,(x1-x0)*W,vh,3*s); c.fillStyle=rgb(208,210,214); c.fill();
        c.save(); rr(c,x0*W,vy,(x1-x0)*W,vh,3*s); c.clip(); c.strokeStyle=rgb(20,20,22); c.lineWidth=2.4*s;
        for(let xx=x0*W+6*s; xx<x1*W-3*s; xx+=6.5*s){ c.beginPath(); c.moveTo(xx,vy+3*s); c.lineTo(xx,vy+vh-3*s); c.stroke(); } c.restore();
        rr(c,x0*W,vy,(x1-x0)*W,vh,3*s); c.strokeStyle=rgb(150,152,156); c.lineWidth=1.4*s; c.stroke(); };
      vent(.20,.40); vent(.60,.80);
      const bx=W*.5, by=dy0+dh*.5, bw=W*.085, bh=dh*.34;
      rr(c,bx-bw,by-bh,2*bw,2*bh,5*s); const bgd=c.createLinearGradient(0,by-bh,0,by+bh); bgd.addColorStop(0,rgb(34,34,38)); bgd.addColorStop(1,rgb(10,10,12));
      c.fillStyle=bgd; c.fill(); rr(c,bx-bw,by-bh,2*bw,2*bh,5*s); c.strokeStyle=rgb(150,152,158); c.lineWidth=1.6*s; c.stroke();
      textSpaced(d,bx,by-bh*0.30,F.bebas,30,rgb(232,234,238),'SILLA',0.04);
      textSpaced(d,bx,by+bh*0.45,F.barlow,11,rgb(200,202,208),'ENGINEERING',0.22);
      // ── black control panel ──
      const py=H*.61, ph=H*.34, px=W*.02, pw=W*.96;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(20,20,22)); pg.addColorStop(1,rgb(8,8,10));
      rr(c,px,py,pw,ph,4*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,4*s); c.strokeStyle=rgb(60,62,66); c.lineWidth=1.2*s; c.stroke();
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      const cyT=py+ph*.32, cyB=py+ph*.66, swY=py+ph*.49;
      // ── power / standby toggles + jewel ──
      batToggle(d,.035*W,swY,8*s,true); batToggle(d,.066*W,swY,8*s,true);
      textSpaced(d,.035*W,py+ph*.14,F.barlow,8,ink,'ON',0.05); textSpaced(d,.066*W,py+ph*.14,F.barlow,8,ink,'ON',0.05);
      textSpaced(d,.035*W,py+ph*.92,F.barlow,7.5,ink,'POWER',0.04); textSpaced(d,.066*W,py+ph*.92,F.barlow,7,ink,'STANDBY',0.03);
      ledDot(d,.092*W,swY,true,224,40,36);
      // ── "Duo Rectifier" script logo ──
      c.save(); c.translate(.150*W,py+ph*.46); c.transform(1,0,-0.18,1,0,0); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,30); c.lineWidth=2.4*s; c.strokeStyle=rgb(20,20,22);
      const slg=c.createLinearGradient(0,-18*s,0,18*s); slg.addColorStop(0,rgb(246,242,224)); slg.addColorStop(1,rgb(206,200,170));
      c.strokeText('Rectifier',0,4*s); c.fillStyle=slg; c.fillText('Rectifier',0,4*s);
      setFont(d,F.bebas,14); c.fillStyle=rgb(232,228,210); c.fillText('DUO',-30*s,-16*s);
      c.restore();
      textSpaced(d,.150*W,py+ph*.88,F.barlow,8,rgb(220,216,200),'100W HEAD',0.05);
      // ── SOLO (decorative) + OUTPUT labels ──
      const soloX=.205*W; c.beginPath(); c.arc(soloX,cyT,d.W*.014,0,7); const kg=c.createRadialGradient(soloX-3*s,cyT-3*s,1*s,soloX,cyT,d.W*.014); kg.addColorStop(0,rgb(70,72,76)); kg.addColorStop(1,rgb(14,14,16)); c.fillStyle=kg; c.fill(); c.strokeStyle=rgb(150,152,158); c.lineWidth=1.4*s; c.stroke();
      c.beginPath(); c.arc(soloX,cyT-d.W*.010,2*s,0,7); c.fillStyle=rgb(230,232,236); c.fill();
      textSpaced(d,(soloX+.250*W)/2,py+ph*.12,F.barlow,7,faint,'LOOP ACTIVE',0.06);
      textSpaced(d,soloX,py+ph*.92,F.barlow,8,ink,'SOLO',0.05); textSpaced(d,.250*W,py+ph*.92,F.barlow,8,ink,'OUTPUT',0.04);
      // ── channel blocks: column labels + mode + LED ──
      const lbl2=(cx,t1,t2)=>{ textSpaced(d,cx*W,py+ph*.49,F.barlow,7,ink,t1,0.02); textSpaced(d,cx*W,py+ph*.99,F.barlow,7,ink,t2,0.02); };
      const inp=(vals&&vals[0]!=null)?vals[0]:1;
      const block=(c0,c1,c2,modeT,ledR,ledG,ledB,on,chTxt)=>{
        lbl2(c0,'PRESENCE','BASS'); lbl2(c1,'MASTER','MID'); lbl2(c2,'GAIN','TREBLE');
        const mx=(c2+0.038); // mode/LED column to the right of the block
        textSpaced(d,mx*W,py+ph*.16,F.barlow,5.5,faint,modeT,0.0);
        ledDot(d,mx*W,cyB,on,ledR,ledG,ledB); textSpaced(d,mx*W,py+ph*.99,F.barlow,7,ink,chTxt,0.04);
      };
      block(.300,.345,.390,'RAW VTG MOD',224,40,36, inp>=0.75, 'CH 3');
      block(.475,.520,.565,'RAW VTG MOD',230,150,40, inp>=0.25&&inp<0.75, 'CH 2');
      block(.650,.695,.740,'CLN  PUSH',70,210,90, inp<0.25, 'CH 1');
      // ── channel select + rectifier labels ──
      textSpaced(d,.880*W,py+ph*.14,F.barlow,7,ink,'CHANNEL',0.03);
      textSpaced(d,.915*W,py+ph*.99,F.barlow,6.5,ink,'RECT',0.03);
      // ── INPUT jack ──
      const ijx=.952*W; c.beginPath(); c.arc(ijx,swY,8*s,0,7); c.fillStyle=rgb(18,16,16); c.fill(); c.strokeStyle=rgb(180,182,188); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(ijx,swY,2.6*s,0,7); c.fillStyle=rgb(54,54,58); c.fill();
      textSpaced(d,ijx,py+ph*.92,F.barlow,8,ink,'INPUT',0.04); } };

  // ── MARSTEN DSL100 (Marshall JCM2000 DSL100H) — black tolex head, gold panel ──
  //    Full DSL100HR panel, 1:1. Parody brand "Marsten" (same as GM-2/UV-1 pedals).
  //    ids: 0 Channel(Classic/Ultra) 1 ClassicGain 2 ClassicVol 3 ClassicMode
  //    4 UltraGain 5 UltraVol 6 UltraMode 7 Bass 8 Mid 9 Treble 10 ToneShift
  //    11 Resonance 12 Presence 13 RevClassic 14 RevUltra 15 Master1 16 Master2
  //    17 MasterSel 18 Output. RS: Gain->Channel morph (clean->crunch->ultra).
  P.dsl100 = { w:1560, h:620, ptr:rgb(236,236,232),
    knobs:[
      {id:16,cx:.122,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // MASTER 2
      {id:15,cx:.167,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // MASTER 1
      {id:14,cx:.252,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // REV ULTRA
      {id:13,cx:.297,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // REV CLASSIC
      {id:11,cx:.362,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // RESONANCE
      {id:12,cx:.407,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // PRESENCE  (RS Pres)
      {id:7, cx:.480,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // BASS      (RS Bass)
      {id:8, cx:.525,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // MIDDLE    (RS Mid)
      {id:9, cx:.570,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // TREBLE    (RS Treble)
      {id:5, cx:.662,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // ULTRA VOL
      {id:4, cx:.707,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // ULTRA GAIN
      {id:2, cx:.792,cy:.785,r:.016,style:'pointer',cap:[26,24,24]},  // CLASSIC VOL
      {id:1, cx:.837,cy:.785,r:.016,style:'pointer',cap:[26,24,24]} ],// CLASSIC GAIN
    sw3:[
      {id:18,cx:.072,cy:.775,hw:10,hh:18,two:true},  // OUTPUT  Low/High
      {id:17,cx:.205,cy:.775,hw:10,hh:18,two:true},  // MASTER SELECT 1/2
      {id:10,cx:.440,cy:.775,hw:10,hh:18,two:true},  // TONE SHIFT
      {id:6, cx:.622,cy:.775,hw:10,hh:18,two:true},  // ULTRA  OD1/OD2
      {id:3, cx:.752,cy:.775,hw:10,hh:18,two:true},  // CLASSIC Clean/Crunch
      {id:0, cx:.888,cy:.775,hw:10,hh:18,two:true} ],// CHANNEL Classic/Ultra
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const gold=rgb(198,176,118), goldHi=rgb(220,200,144), goldLo=rgb(150,130,80),
            ink=rgb(34,30,24), inkF='rgba(34,30,24,0.62)', chr=rgb(196,200,206);
      const v=(i,dv)=>(vals&&vals[i]!=null)?vals[i]:dv;
      // ── black tolex body ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,23,25)); bgr.addColorStop(1,rgb(11,10,12));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.02)';
      for(let x=-H;x<W;x+=8*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      // ── panel geometry (control strip along the bottom, like the real head) ──
      const py=H*.66, ph=H*.28, px=W*.018, pw=W*.964, lblY=py+ph*.84;
      // ── salt-and-pepper grille (dominant upper area with the logo) ──
      const gy=H*.05, gh=H*.55, gx=W*.04, gw=W*.92;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(18,17,16); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip();
      for(let yy=gy; yy<gy+gh; yy+=3*s){ for(let xx=gx; xx<gx+gw; xx+=3*s){
        const n=((xx*13+yy*7)%17); if(n<3){ c.fillStyle='rgba(214,206,180,0.16)'; c.fillRect(xx,yy,1.4*s,1.4*s); } } }
      c.restore();
      rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=gold; c.lineWidth=2*s; c.stroke();
      // ── gold piping above the panel ──
      c.beginPath(); c.moveTo(W*.04,py-H*.03); c.lineTo(W*.96,py-H*.03); c.strokeStyle=gold; c.lineWidth=1.8*s; c.stroke();
      // ── "Marsten" white script logo on the grille ──
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,86); c.lineWidth=4*s; c.strokeStyle='rgba(0,0,0,0.35)';
      c.strokeText('Marsten',W*.5,gy+gh*.46); c.fillStyle=rgb(244,242,236); c.fillText('Marsten',W*.5,gy+gh*.46);
      c.restore();
      // centre handle (top)
      const hx0=.455*W,hx1=.545*W,hy=H*.005,hh2=H*.05;
      rr(c,hx0,hy,hx1-hx0,hh2,5*s); c.fillStyle=rgb(12,12,13); c.fill();
      [hx0,hx1].forEach(bx=>bolt(bx,hy+hh2*.5,3*s));
      // ── corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.11; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── gold brushed control panel ──
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,goldHi); pg.addColorStop(0.5,gold); pg.addColorStop(1,goldLo);
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,5*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=1;
      for(let xx=px; xx<px+pw; xx+=2.4*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,5*s); c.strokeStyle=goldLo; c.lineWidth=1.4*s; c.stroke();
      // section group-box (rounded rect, name breaking the top edge) + label helper
      const box=(x0,x1,name)=>{ const bx0=x0*W,bx1=x1*W,by0=py+ph*.07,by1=py+ph*.95,mid=(bx0+bx1)/2,r=5*s;
        setFont(d,F.barlow,13); const tw=name?c.measureText(name).width+10*s:0;
        c.strokeStyle='rgba(34,30,24,0.5)'; c.lineWidth=1.3*s; c.beginPath();
        c.moveTo(mid-tw/2,by0); c.lineTo(bx0+r,by0); c.arcTo(bx0,by0,bx0,by0+r,r);
        c.lineTo(bx0,by1-r); c.arcTo(bx0,by1,bx0+r,by1,r); c.lineTo(bx1-r,by1); c.arcTo(bx1,by1,bx1,by1-r,r);
        c.lineTo(bx1,by0+r); c.arcTo(bx1,by0,bx1-r,by0,r); c.lineTo(mid+tw/2,by0); c.stroke();
        if(name) textSpaced(d,mid,by0,F.barlow,13,ink,name,0.08); };
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||9,ink,t,0.02);
      // ── POWER / OUTPUT (far left) ──
      box(.026,.092,'OUTPUT');
      batToggle(d,.040*W,py+ph*.42,8*s,true);
      textSpaced(d,.040*W,py+ph*.16,F.barlow,7,ink,'POWER',0.03); lbl(.040,'ON',7.5);
      lbl(.072,'LO  HI',7);
      // ── MASTER (2,1 + select) ──
      box(.100,.226,'MASTER'); lbl(.122,'MASTER 2'); lbl(.167,'MASTER 1'); lbl(.205,'SEL 1/2',7.5);
      // ── REVERB (ultra, classic) ──
      box(.236,.320,'REVERB'); lbl(.252,'ULTRA'); lbl(.297,'CLASSIC');
      // ── EQUALISATION (resonance, presence, tone shift, bass, mid, treble) ──
      box(.330,.600,'EQUALISATION');
      lbl(.362,'RESON'); lbl(.407,'PRESENCE',7.5); lbl(.440,'T.SHIFT',7);
      lbl(.480,'BASS'); lbl(.525,'MIDDLE'); lbl(.570,'TREBLE');
      // ── ULTRA GAIN (od1/od2, volume, gain) ──
      box(.610,.732,'ULTRA GAIN'); lbl(.622,'OD1/2',7.5); lbl(.662,'VOLUME'); lbl(.707,'GAIN');
      // red LED for the Ultra channel
      ledDot(d,.690*W,py+ph*.12,v(0,1)>=0.5,224,42,38);
      // ── CLASSIC GAIN (clean/crunch, volume, gain) ──
      box(.740,.862,'CLASSIC GAIN'); lbl(.752,'CLN/CR',7); lbl(.792,'VOLUME'); lbl(.837,'GAIN');
      // green LED for the Classic channel
      ledDot(d,.820*W,py+ph*.12,v(0,1)<0.5,70,206,96);
      // ── CHANNEL select ──
      box(.872,.908,'CH'); lbl(.888,v(0,1)<0.5?'CLASSIC':'ULTRA',6.5);
      // ── INPUT jack ──
      const ijx=.944*W, jy=py+ph*.46; c.beginPath(); c.arc(ijx,jy,7*s,0,7); c.fillStyle=rgb(18,16,16); c.fill();
      c.strokeStyle=chr; c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(ijx,jy,2.4*s,0,7); c.fillStyle=rgb(52,52,56); c.fill();
      lbl(.944,'INPUT');
      // ── model code + parody maker ──
      textSpaced(d,W*.5,py+ph*.045,F.bebas,15,rgb(40,34,26),'DSL100',0.10);
      textSpaced(d,(px+pw)-46*s,lblY,F.bebas,13,rgb(48,40,28),'MARSTEN',0.08); } };

  // ── MARSTEN PLEXI (Marshall 1959 Super Lead 100W) — black tolex head, gold
  //    control panel, "Marsten" gold script. Non-master amp: the Loudness pots
  //    ARE the gain. 6 knobs: 0 Presence 1 Bass 2 Middle 3 Treble 4 Loudness I
  //    5 Loudness II. Pilot jewel + Power/Standby are decorative. RS: Gain->
  //    Loudness I, Bass/Mid/Treble->stack, Pres->Presence.
  P.plexi = { w:1560, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:0,cx:.310,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // PRESENCE
      {id:1,cx:.385,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // BASS
      {id:2,cx:.460,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // MIDDLE
      {id:3,cx:.535,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // TREBLE
      {id:4,cx:.610,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // LOUDNESS I
      {id:5,cx:.685,cy:.770,r:.020,style:'pointer',cap:[24,22,22]} ],// LOUDNESS II
    // clickable input cable (id 6): Bright -> Both(jumpered) -> Normal. hidden so
    // the engine doesn't stamp a lever over the jacks; the cable is drawn below.
    sw3:[{id:6,cx:.857,cy:.788,hw:62,hh:42,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const gold=rgb(200,176,108), goldHi=rgb(224,202,140), goldLo=rgb(150,128,74),
            ink=rgb(30,26,20), inkF='rgba(30,26,20,0.6)';
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(17,16,18)); bgr.addColorStop(1,rgb(10,9,11));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.018)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.22)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x+3.5*s,0); c.lineTo(x+3.5*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      // ── top carry handle ──
      const hx0=.43*W,hx1=.57*W,hcy=H*.05,hh=H*.032;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(13,13,15));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // ── gold piping frame around the upper tolex ──
      rr(c,W*.035,H*.135,W*.93,H*.40,8*s); c.strokeStyle=gold; c.lineWidth=2.4*s; c.stroke();
      rr(c,W*.046,H*.150,W*.908,H*.37,6*s); c.strokeStyle='rgba(120,100,56,0.5)'; c.lineWidth=1*s; c.stroke();
      // ── "Marsten" gold script logo (centre) ──
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,82); c.lineWidth=5*s; c.strokeStyle='rgba(0,0,0,0.4)'; c.strokeText('Marsten',W*.5,H*.315);
      const lg=c.createLinearGradient(0,H*.25,0,H*.38); lg.addColorStop(0,goldHi); lg.addColorStop(1,goldLo);
      c.fillStyle=lg; c.fillText('Marsten',W*.5,H*.315); c.restore();
      // ── chrome-less corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.10; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── gold brushed control panel (lower strip) ──
      const py=H*.62, ph=H*.30, px=W*.05, pw=W*.90, lblY=py+ph*.22;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,goldHi); pg.addColorStop(0.5,gold); pg.addColorStop(1,goldLo);
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,5*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=1;
      for(let xx=px; xx<px+pw; xx+=2.4*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,5*s); c.strokeStyle=goldLo; c.lineWidth=1.4*s; c.stroke();
      bolt(px+12*s,py+ph*.5,2.6*s); bolt(px+pw-12*s,py+ph*.5,2.6*s);
      // knob labels (above each pot) — the vintage 1959 panel: the two volume pots
      // are simply LOUDNESS 1 / LOUDNESS 2 (no "High Treble"/"Normal" text).
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||10,ink,t,0.03);
      lbl(.310,'PRESENCE',8); lbl(.385,'BASS'); lbl(.460,'MIDDLE'); lbl(.535,'TREBLE');
      lbl(.610,'LOUDNESS 1',7.5); lbl(.685,'LOUDNESS 2',7.5);
      // ── left: MAINS + STANDBY toggles + red INDICATOR jewel ──
      batToggle(d,.080*W,py+ph*.46,8*s,true); batToggle(d,.130*W,py+ph*.46,8*s,true);
      textSpaced(d,.080*W,lblY,F.barlow,8,ink,'MAINS',0.03); textSpaced(d,.130*W,lblY,F.barlow,7.5,ink,'STANDBY',0.02);
      textSpaced(d,.080*W,py+ph*.84,F.barlow,6.5,inkF,'ON',0.04); textSpaced(d,.130*W,py+ph*.84,F.barlow,6.5,inkF,'ON',0.04);
      const jcx=.180*W, jcy=py+ph*.44;
      c.beginPath(); c.arc(jcx,jcy,9*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
      const jg=c.createRadialGradient(jcx-2*s,jcy-2*s,1*s,jcx,jcy,9*s); jg.addColorStop(0,rgb(255,130,96)); jg.addColorStop(0.5,rgb(220,42,30)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,jcy,6.5*s,0,7); c.fillStyle=jg; c.fill();
      c.strokeStyle=rgb(150,124,70); c.lineWidth=1.6*s; c.beginPath(); c.arc(jcx,jcy,9*s,0,7); c.stroke();
      textSpaced(d,jcx,py+ph*.84,F.barlow,6.5,inkF,'INDICATOR',0.02);
      // ── right: INPUTS (2x2 — two channels, 1 & 2, high+low) ──
      const ix0=.825*W, ix1=.890*W, iy0=py+ph*.40, iy1=py+ph*.72;
      const jack=(jx2,jy2)=>{ c.beginPath(); c.arc(jx2,jy2,8*s,0,7); c.fillStyle=rgb(16,14,14); c.fill();
        c.strokeStyle=rgb(150,128,74); c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx2,jy2,2.6*s,0,7); c.fillStyle=rgb(54,52,48); c.fill(); };
      jack(ix0,iy0); jack(ix1,iy0); jack(ix0,iy1); jack(ix1,iy1);
      textSpaced(d,(ix0+ix1)/2,lblY,F.barlow,8.5,ink,'INPUTS',0.04);
      textSpaced(d,ix0-15*s,iy0,F.barlow,8,ink,'1',0); textSpaced(d,ix0-15*s,iy1,F.barlow,8,ink,'1',0);
      textSpaced(d,ix1+15*s,iy0,F.barlow,8,ink,'2',0); textSpaced(d,ix1+15*s,iy1,F.barlow,8,ink,'2',0);
      // ── input cable / channel jumper (click cycles Bright -> Both -> Normal) ──
      const inp=(vals&&vals[6]!=null)?vals[6]:0.5;
      const plug=(jx,jy)=>{ rr(c,jx-4.6*s,jy-5.5*s,9.2*s,6.5*s,2*s); c.fillStyle=rgb(36,34,32); c.fill();
        const cg=c.createLinearGradient(jx-4.6*s,jy,jx+4.6*s,jy); cg.addColorStop(0,rgb(188,192,198)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(188,192,198));
        rr(c,jx-4.6*s,jy+1*s,9.2*s,3.6*s,1.4*s); c.fillStyle=cg; c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx+6*s,jy+44*s, jx-34*s,jy+54*s, jx-46*s,H*0.99);
        c.lineWidth=5*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke();
        c.lineWidth=1.6*s; c.strokeStyle='rgba(255,255,255,0.10)'; c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const mx=(x1+x2)/2, my=Math.min(y1,y2)-13*s;
        c.beginPath(); c.arc(x1,y1,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.arc(x2,y2,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo(mx,my,x2,y2);
        c.lineWidth=3.6*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      let mode;
      if (inp < 0.25)      { plug(ix0,iy0); mode='BRIGHT'; }
      else if (inp < 0.75) { jumper(ix0,iy1,ix1,iy0); plug(ix0,iy0); mode='JUMPERED'; }
      else                 { plug(ix1,iy0); mode='NORMAL'; }
      textSpaced(d,(ix0+ix1)/2,py+ph*.06,F.barlow,7,rgb(150,42,30),mode,0.06);
      // ── parody maker ──
      textSpaced(d,(px+pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── MARSTEN VS100 (Marshall Valvestate VS100RH) — black tolex hybrid head,
  //    BLACK brushed control panel with white legends + gold "Marsten" script +
  //    gold "VS100" badge (no "Valvestate" text). 3 modes via the channel
  //    selector. ids: 0 Channel(Clean/OD1/OD2) 1 ClVol 2 ClBass 3 ClMid
  //    4 ClTreble 5 OD1 Gain 6 OD1 Vol 7 OD2 Gain 8 OD2 Contour 9 OD2 Vol
  //    10 OD2 Bass 11 OD2 Mid 12 OD2 Treble 13 FX Mix 14 Clean Rev 15 OD Rev.
  //    RS: Gain->OD2 Gain, Bass/Mid/Treble->OD2 stack (channel pinned OD2).
  P.marstenvs100 = { w:1760, h:600, ptr:rgb(238,236,230),
    knobs:[
      {id:1, cx:.110,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // CLEAN Volume
      {id:2, cx:.152,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // CLEAN Bass
      {id:3, cx:.194,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // CLEAN Middle
      {id:4, cx:.236,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // CLEAN Treble
      {id:5, cx:.305,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD1 Gain
      {id:6, cx:.347,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD1 Volume
      {id:7, cx:.418,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Gain   (RS Gain)
      {id:8, cx:.460,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Contour
      {id:9, cx:.502,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Volume
      {id:10,cx:.544,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Bass    (RS Bass)
      {id:11,cx:.586,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Middle  (RS Mid)
      {id:12,cx:.628,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // OD2 Treble  (RS Treble)
      {id:13,cx:.700,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // FX Mix
      {id:14,cx:.742,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]},  // Clean Reverb
      {id:15,cx:.784,cy:.770,r:.0165,style:'pointer',cap:[22,21,22]} ],// OD Reverb
    // channel selector (id 0): Clean -> OD1 -> OD2 (3-position)
    sw3:[{id:0,cx:.843,cy:.760,hw:11,hh:20}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const gold=rgb(204,182,116), goldHi=rgb(228,206,146), goldLo=rgb(150,128,74),
            wht=rgb(232,230,224), whtF='rgba(232,230,224,0.62)', chr=rgb(196,200,206);
      const v=(i,dv)=>(vals&&vals[i]!=null)?vals[i]:dv;
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(16,15,17)); bgr.addColorStop(1,rgb(10,9,11));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.018)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.22)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x+3.5*s,0); c.lineTo(x+3.5*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      // ── top carry handle ──
      const hx0=.44*W,hx1=.56*W,hcy=H*.045,hh=H*.030;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(13,13,15));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // ── gold piping frame around the upper tolex / grille ──
      rr(c,W*.035,H*.125,W*.93,H*.40,8*s); c.strokeStyle=gold; c.lineWidth=2.4*s; c.stroke();
      rr(c,W*.046,H*.140,W*.908,H*.37,6*s); c.strokeStyle='rgba(120,100,56,0.5)'; c.lineWidth=1*s; c.stroke();
      // salt-and-pepper grille texture
      c.save(); rr(c,W*.046,H*.140,W*.908,H*.37,6*s); c.clip();
      for(let yy=H*.14; yy<H*.51; yy+=3*s){ for(let xx=W*.046; xx<W*.954; xx+=3*s){
        const n=((xx*13+yy*7)%19); if(n<2){ c.fillStyle='rgba(214,206,180,0.10)'; c.fillRect(xx,yy,1.3*s,1.3*s); } } }
      c.restore();
      // ── "Marsten" gold script logo (centre of grille) ──
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,80); c.lineWidth=5*s; c.strokeStyle='rgba(0,0,0,0.4)'; c.strokeText('Marsten',W*.5,H*.295);
      const lg=c.createLinearGradient(0,H*.23,0,H*.36); lg.addColorStop(0,goldHi); lg.addColorStop(1,goldLo);
      c.fillStyle=lg; c.fillText('Marsten',W*.5,H*.295); c.restore();
      // ── "VS100" model badge under the script ──
      textSpaced(d,W*.5,H*.415,F.bebas,20,gold,'VS100',0.22);
      textSpaced(d,W*.5,H*.460,F.barlow,9,rgb(150,128,74),'HYBRID  100W  HEAD',0.18);
      // ── corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.10; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── BLACK brushed control panel (lower strip, white legends) ──
      const py=H*.60, ph=H*.32, px=W*.022, pw=W*.956, lblY=py+ph*.84;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(46,45,48)); pg.addColorStop(0.5,rgb(30,29,31)); pg.addColorStop(1,rgb(18,17,19));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,5*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.04)'; c.lineWidth=1;
      for(let xx=px; xx<px+pw; xx+=2.4*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,5*s); c.strokeStyle=gold; c.lineWidth=1.6*s; c.stroke();
      // section group-box (rounded rect, name breaking the top edge)
      const box=(x0,x1,name)=>{ const bx0=x0*W,bx1=x1*W,by0=py+ph*.07,by1=py+ph*.96,mid=(bx0+bx1)/2,r=5*s;
        setFont(d,F.barlow,12); const tw=name?c.measureText(name).width+10*s:0;
        c.strokeStyle='rgba(204,182,116,0.45)'; c.lineWidth=1.2*s; c.beginPath();
        c.moveTo(mid-tw/2,by0); c.lineTo(bx0+r,by0); c.arcTo(bx0,by0,bx0,by0+r,r);
        c.lineTo(bx0,by1-r); c.arcTo(bx0,by1,bx0+r,by1,r); c.lineTo(bx1-r,by1); c.arcTo(bx1,by1,bx1,by1-r,r);
        c.lineTo(bx1,by0+r); c.arcTo(bx1,by0,bx1-r,by0,r); c.lineTo(mid+tw/2,by0); c.stroke();
        if(name) textSpaced(d,mid,by0,F.barlow,11,gold,name,0.06); };
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||8.5,wht,t,0.02);
      // ── INPUT jack (far left) ──
      const ijx=.052*W, jy=py+ph*.46; c.beginPath(); c.arc(ijx,jy,8*s,0,7); c.fillStyle=rgb(16,14,14); c.fill();
      c.strokeStyle=chr; c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(ijx,jy,2.6*s,0,7); c.fillStyle=rgb(52,52,56); c.fill();
      box(.034,.072,''); lbl(.052,'INPUT');
      // ── CLEAN ──
      box(.085,.262,'CLEAN'); lbl(.110,'VOLUME'); lbl(.152,'BASS'); lbl(.194,'MIDDLE'); lbl(.236,'TREBLE');
      // ── OVERDRIVE 1 ──
      box(.280,.372,'OVERDRIVE 1'); lbl(.305,'GAIN'); lbl(.347,'VOLUME');
      // ── OVERDRIVE 2 ──
      box(.393,.654,'OVERDRIVE 2');
      lbl(.418,'GAIN'); lbl(.460,'CONTOUR',7.5); lbl(.502,'VOLUME'); lbl(.544,'BASS'); lbl(.586,'MIDDLE'); lbl(.628,'TREBLE');
      // ── EFFECTS / REVERB ──
      box(.676,.808,'EFFECTS'); lbl(.700,'FX MIX',7.5); lbl(.742,'CL REV',7.5); lbl(.784,'OD REV',7.5);
      // ── CHANNEL select + LEDs ──
      box(.820,.866,'CHANNEL');
      const chn=v(0,1); const md=chn<0.25?'CLEAN':(chn<0.75?'OD 1':'OD 2');
      lbl(.843,md,7.5);
      ledDot(d,.828*W,py+ph*.14,chn<0.25,70,206,96);     // green = clean
      ledDot(d,.843*W,py+ph*.14,chn>=0.25&&chn<0.75,232,180,40); // amber = OD1
      ledDot(d,.858*W,py+ph*.14,chn>=0.75,224,42,38);    // red = OD2
      // ── POWER / STANDBY (far right) + pilot jewel ──
      box(.878,.976,'POWER');
      batToggle(d,.900*W,py+ph*.46,8*s,true); batToggle(d,.946*W,py+ph*.46,8*s,true);
      lbl(.900,'POWER',7.5); lbl(.946,'STANDBY',7);
      textSpaced(d,.900*W,py+ph*.16,F.barlow,6.5,whtF,'ON',0.04); textSpaced(d,.946*W,py+ph*.16,F.barlow,6.5,whtF,'ON',0.04);
      const jcx=.923*W, jcy=py+ph*.16;
      const jg=c.createRadialGradient(jcx-1.5*s,jcy-1.5*s,1*s,jcx,jcy,6*s); jg.addColorStop(0,rgb(255,130,96)); jg.addColorStop(0.5,rgb(220,42,30)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,jcy,5.5*s,0,7); c.fillStyle=jg; c.fill();
      c.strokeStyle=gold; c.lineWidth=1.4*s; c.beginPath(); c.arc(jcx,jcy,5.5*s,0,7); c.stroke();
      // ── parody maker ──
      textSpaced(d,W*.5,py+ph*.06,F.bebas,12,rgb(150,128,74),'MARSTEN',0.10); } };

  // ── MARSTEN JCM800 (Marshall JCM800 2204, 50W master-volume) — black tolex
  //    head, gold brushed control panel, "Marsten" gold script. 6 knobs:
  //    0 Preamp(Gain) 1 Bass 2 Middle 3 Treble 4 Presence 5 Master + High/Low
  //    inputs. RS: Gain->Preamp Vol, Bass/Mid/Treble->stack, Pres->Presence.
  P.marstenjcm800 = { w:1560, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:4,cx:.330,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // PRESENCE
      {id:1,cx:.405,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // BASS
      {id:2,cx:.480,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // MIDDLE
      {id:3,cx:.555,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // TREBLE
      {id:5,cx:.630,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},  // MASTER VOL
      {id:0,cx:.705,cy:.770,r:.020,style:'pointer',cap:[24,22,22]} ],// PREAMP VOL (gain)
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const gold=rgb(200,176,108), goldHi=rgb(224,202,140), goldLo=rgb(150,128,74),
            ink=rgb(30,26,20), inkF='rgba(30,26,20,0.6)';
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(17,16,18)); bgr.addColorStop(1,rgb(10,9,11));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.018)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.43*W,hx1=.57*W,hcy=H*.05,hh=H*.032;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(13,13,15));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // gold piping frame around the upper tolex + salt-and-pepper grille
      rr(c,W*.035,H*.135,W*.93,H*.40,8*s); c.strokeStyle=gold; c.lineWidth=2.4*s; c.stroke();
      c.save(); rr(c,W*.046,H*.150,W*.908,H*.37,6*s); c.clip();
      for(let yy=H*.15; yy<H*.52; yy+=3*s){ for(let xx=W*.046; xx<W*.954; xx+=3*s){
        const n=((xx*13+yy*7)%19); if(n<2){ c.fillStyle='rgba(214,206,180,0.10)'; c.fillRect(xx,yy,1.3*s,1.3*s); } } }
      c.restore();
      rr(c,W*.046,H*.150,W*.908,H*.37,6*s); c.strokeStyle='rgba(120,100,56,0.5)'; c.lineWidth=1*s; c.stroke();
      // ── "Marsten" gold script logo (centre) ──
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.ink,82); c.lineWidth=5*s; c.strokeStyle='rgba(0,0,0,0.4)'; c.strokeText('Marsten',W*.5,H*.315);
      const lg=c.createLinearGradient(0,H*.25,0,H*.38); lg.addColorStop(0,goldHi); lg.addColorStop(1,goldLo);
      c.fillStyle=lg; c.fillText('Marsten',W*.5,H*.315); c.restore();
      textSpaced(d,W*.5,H*.445,F.bebas,16,gold,'JCM800',0.20);
      // ── corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.10; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── gold brushed control panel (lower strip) ──
      const py=H*.62, ph=H*.30, px=W*.05, pw=W*.90, lblY=py+ph*.22;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,goldHi); pg.addColorStop(0.5,gold); pg.addColorStop(1,goldLo);
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,5*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=1;
      for(let xx=px; xx<px+pw; xx+=2.4*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,5*s); c.strokeStyle=goldLo; c.lineWidth=1.4*s; c.stroke();
      bolt(px+12*s,py+ph*.5,2.6*s); bolt(px+pw-12*s,py+ph*.5,2.6*s);
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||9,ink,t,0.03);
      lbl(.330,'PRESENCE',8); lbl(.405,'BASS'); lbl(.480,'MIDDLE',8.5); lbl(.555,'TREBLE',8.5);
      lbl(.630,'MASTER VOL',7); lbl(.705,'PREAMP VOL',7);
      // ── left: MAINS + STANDBY toggles + red jewel ──
      batToggle(d,.090*W,py+ph*.46,8*s,true); batToggle(d,.140*W,py+ph*.46,8*s,true);
      textSpaced(d,.090*W,lblY,F.barlow,8,ink,'MAINS',0.03); textSpaced(d,.140*W,lblY,F.barlow,7.5,ink,'STANDBY',0.02);
      textSpaced(d,.090*W,py+ph*.84,F.barlow,6.5,inkF,'ON',0.04); textSpaced(d,.140*W,py+ph*.84,F.barlow,6.5,inkF,'ON',0.04);
      const jcx=.190*W, jcy=py+ph*.44;
      c.beginPath(); c.arc(jcx,jcy,9*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
      const jg=c.createRadialGradient(jcx-2*s,jcy-2*s,1*s,jcx,jcy,9*s); jg.addColorStop(0,rgb(255,130,96)); jg.addColorStop(0.5,rgb(220,42,30)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,jcy,6.5*s,0,7); c.fillStyle=jg; c.fill();
      c.strokeStyle=goldLo; c.lineWidth=1.6*s; c.beginPath(); c.arc(jcx,jcy,9*s,0,7); c.stroke();
      // ── right: High / Low INPUT jacks ──
      const ix=.840*W, iy0=py+ph*.40, iy1=py+ph*.72;
      const jack=(jx2,jy2)=>{ c.beginPath(); c.arc(jx2,jy2,8*s,0,7); c.fillStyle=rgb(16,14,14); c.fill();
        c.strokeStyle=rgb(150,128,74); c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx2,jy2,2.6*s,0,7); c.fillStyle=rgb(54,52,48); c.fill(); };
      jack(ix,iy0); jack(ix,iy1);
      textSpaced(d,ix,lblY,F.barlow,8.5,ink,'INPUT',0.04);
      textSpaced(d,ix-16*s,iy0,F.barlow,7.5,ink,'HI',0); textSpaced(d,ix-16*s,iy1,F.barlow,7.5,ink,'LO',0);
      // ── parody maker ──
      textSpaced(d,(px+pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── Shared "Marsten" black-tolex + gold-panel head (Plexi/JCM800/JTM45 family).
  //    Draws the tolex, gold piping, "Marsten" script + model badge, corners and
  //    the empty gold control panel; returns geometry + the bolt() helper.
  function marstenGoldHead(d, model){
    const {ctx:c,W,H,s}=d;
    const gold=rgb(200,176,108), goldHi=rgb(224,202,140), goldLo=rgb(150,128,74),
          ink=rgb(30,26,20), inkF='rgba(30,26,20,0.6)';
    const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(17,16,18)); bgr.addColorStop(1,rgb(10,9,11));
    c.fillStyle=bgr; c.fillRect(0,0,W,H);
    c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
    c.strokeStyle='rgba(255,255,255,0.018)'; for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
    c.strokeStyle='rgba(0,0,0,0.22)'; for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x+3.5*s,0); c.lineTo(x+3.5*s-H,H); c.stroke(); }
    c.restore();
    const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
      g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
      c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
    const hx0=.43*W,hx1=.57*W,hcy=H*.05,hh=H*.032;
    rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(13,13,15));
    c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
    [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
    rr(c,W*.035,H*.135,W*.93,H*.40,8*s); c.strokeStyle=gold; c.lineWidth=2.4*s; c.stroke();
    rr(c,W*.046,H*.150,W*.908,H*.37,6*s); c.strokeStyle='rgba(120,100,56,0.5)'; c.lineWidth=1*s; c.stroke();
    c.save(); rr(c,W*.046,H*.150,W*.908,H*.37,6*s); c.clip();
    for(let yy=H*.15; yy<H*.52; yy+=3*s){ for(let xx=W*.046; xx<W*.954; xx+=3*s){ if(((xx*13+yy*7)%19)<2){ c.fillStyle='rgba(214,206,180,0.10)'; c.fillRect(xx,yy,1.3*s,1.3*s); } } }
    c.restore();
    c.save(); c.textAlign='center'; c.textBaseline='middle';
    setFont(d,F.ink,80); c.lineWidth=5*s; c.strokeStyle='rgba(0,0,0,0.4)'; c.strokeText('Marsten',W*.5,H*.300);
    const lg=c.createLinearGradient(0,H*.24,0,H*.37); lg.addColorStop(0,goldHi); lg.addColorStop(1,goldLo);
    c.fillStyle=lg; c.fillText('Marsten',W*.5,H*.300); c.restore();
    if(model) textSpaced(d,W*.5,H*.455,F.bebas,16,gold,model,0.20);
    const corner=(cxx,cyy,dx,dy)=>{ const k=H*.10; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
      c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
    corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
    const py=H*.62, ph=H*.30, px=W*.035, pw=W*.93, lblY=py+ph*.22;
    const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,goldHi); pg.addColorStop(0.5,gold); pg.addColorStop(1,goldLo);
    rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
    c.save(); rr(c,px,py,pw,ph,5*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.06)'; c.lineWidth=1;
    for(let xx=px; xx<px+pw; xx+=2.4*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); } c.restore();
    rr(c,px,py,pw,ph,5*s); c.strokeStyle=goldLo; c.lineWidth=1.4*s; c.stroke();
    bolt(px+12*s,py+ph*.5,2.6*s); bolt(px+pw-12*s,py+ph*.5,2.6*s);
    return { gold,goldHi,goldLo,ink,inkF, py,ph,px,pw,lblY, bolt };
  }
  // small reusable power block (MAINS+STANDBY toggles + red jewel) for the gold heads
  function marstenPowerBlock(d, G, x0){
    const {ctx:c,W,s}=d; const {ink,inkF,py,ph,lblY,goldLo}=G;
    batToggle(d,(x0)*W,py+ph*.46,8*s,true); batToggle(d,(x0+0.045)*W,py+ph*.46,8*s,true);
    textSpaced(d,x0*W,lblY,F.barlow,7.5,ink,'MAINS',0.03); textSpaced(d,(x0+0.045)*W,lblY,F.barlow,7,ink,'STANDBY',0.02);
    const jcx=(x0+0.090)*W, jcy=py+ph*.44;
    c.beginPath(); c.arc(jcx,jcy,8*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
    const jg=c.createRadialGradient(jcx-2*s,jcy-2*s,1*s,jcx,jcy,8*s); jg.addColorStop(0,rgb(255,130,96)); jg.addColorStop(0.5,rgb(220,42,30)); jg.addColorStop(1,rgb(108,12,10));
    c.beginPath(); c.arc(jcx,jcy,5.5*s,0,7); c.fillStyle=jg; c.fill(); c.strokeStyle=goldLo; c.lineWidth=1.4*s; c.beginPath(); c.arc(jcx,jcy,8*s,0,7); c.stroke();
  }

  // ── MARSTEN JTM45 (Marshall JTM45) — gold-panel head, jumper inputs, Loudness
  //    1/2 (non-master, like the Plexi but 2xKT66/GZ34 ~30W). 0 Presence 1 Bass
  //    2 Middle 3 Treble 4 Loudness1 5 Loudness2 + Input jumper(6).
  P.marstenjtm45 = { w:1560, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:0,cx:.310,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},
      {id:1,cx:.385,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},
      {id:2,cx:.460,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},
      {id:3,cx:.535,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},
      {id:4,cx:.610,cy:.770,r:.020,style:'pointer',cap:[24,22,22]},
      {id:5,cx:.685,cy:.770,r:.020,style:'pointer',cap:[24,22,22]} ],
    sw3:[{id:6,cx:.857,cy:.788,hw:62,hh:42,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d; const G=marstenGoldHead(d,'JTM 45');
      const {gold,ink,inkF,py,ph,lblY,goldLo}=G;
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||10,ink,t,0.03);
      lbl(.310,'PRESENCE',8); lbl(.385,'BASS'); lbl(.460,'MIDDLE'); lbl(.535,'TREBLE');
      lbl(.610,'LOUDNESS 1',7.5); lbl(.685,'LOUDNESS 2',7.5);
      marstenPowerBlock(d,G,0.085);
      // INPUTS (2x2) + clickable jumper cable (id6)
      const ix0=.825*W, ix1=.890*W, iy0=py+ph*.40, iy1=py+ph*.72;
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,8*s,0,7); c.fillStyle=rgb(16,14,14); c.fill();
        c.strokeStyle=rgb(150,128,74); c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.6*s,0,7); c.fillStyle=rgb(54,52,48); c.fill(); };
      jack(ix0,iy0); jack(ix1,iy0); jack(ix0,iy1); jack(ix1,iy1);
      textSpaced(d,(ix0+ix1)/2,lblY,F.barlow,8.5,ink,'INPUTS',0.04);
      const inp=(vals&&vals[6]!=null)?vals[6]:0.5;
      const plug=(jx,jy)=>{ rr(c,jx-4.6*s,jy-5.5*s,9.2*s,6.5*s,2*s); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx+6*s,jy+44*s, jx-34*s,jy+54*s, jx-46*s,H*0.99);
        c.lineWidth=5*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const mx=(x1+x2)/2, my=Math.min(y1,y2)-13*s;
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo(mx,my,x2,y2); c.lineWidth=3.6*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      let mode; if(inp<0.25){plug(ix0,iy0);mode='BRIGHT';} else if(inp<0.75){jumper(ix0,iy1,ix1,iy0);plug(ix0,iy0);mode='JUMPERED';} else {plug(ix1,iy0);mode='NORMAL';}
      textSpaced(d,(ix0+ix1)/2,py+ph*.06,F.barlow,7,rgb(150,42,30),mode,0.06);
      textSpaced(d,(G.px+G.pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── MARSTEN BLUESBREAKER (Marshall 1962) — JTM45 voice + Tremolo, gold combo
  //    panel. 0 Speed 1 Intensity 2 Presence 3 Bass 4 Middle 5 Treble 6 Loudness1
  //    7 Loudness2 + Input jumper(8). RS Gain -> Loudness1.
  P.marstenbluesbreaker = { w:1620, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:0,cx:.235,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // SPEED
      {id:1,cx:.300,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // INTENSITY
      {id:2,cx:.380,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // PRESENCE
      {id:3,cx:.445,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // BASS
      {id:4,cx:.510,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // MIDDLE
      {id:5,cx:.575,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // TREBLE
      {id:6,cx:.640,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // LOUDNESS 1
      {id:7,cx:.705,cy:.770,r:.019,style:'pointer',cap:[24,22,22]} ],// LOUDNESS 2
    sw3:[{id:8,cx:.860,cy:.788,hw:55,hh:42,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d; const G=marstenGoldHead(d,'1962');
      const {ink,inkF,py,ph,lblY}=G;
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||8.5,ink,t,0.02);
      // tremolo bracket over Speed/Intensity
      c.strokeStyle='rgba(30,26,20,0.5)'; c.lineWidth=1.2*s;
      c.beginPath(); c.moveTo(.215*W,py+ph*.10); c.lineTo(.320*W,py+ph*.10); c.stroke();
      textSpaced(d,.2675*W,py+ph*.045,F.barlow,7.5,ink,'TREMOLO',0.06);
      lbl(.235,'SPEED'); lbl(.300,'INTENSITY',6.5); lbl(.380,'PRESENCE',6.5); lbl(.445,'BASS'); lbl(.510,'MIDDLE',8);
      lbl(.575,'TREBLE',8); lbl(.640,'LOUDNESS 1',6); lbl(.705,'LOUDNESS 2',6);
      marstenPowerBlock(d,G,0.060);
      const ix0=.830*W, ix1=.895*W, iy0=py+ph*.40, iy1=py+ph*.72;
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,8*s,0,7); c.fillStyle=rgb(16,14,14); c.fill();
        c.strokeStyle=rgb(150,128,74); c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.6*s,0,7); c.fillStyle=rgb(54,52,48); c.fill(); };
      jack(ix0,iy0); jack(ix1,iy0); jack(ix0,iy1); jack(ix1,iy1);
      textSpaced(d,(ix0+ix1)/2,lblY,F.barlow,8,ink,'INPUTS',0.03);
      const inp=(vals&&vals[8]!=null)?vals[8]:0.5;
      const plug=(jx,jy)=>{ rr(c,jx-4.6*s,jy-5.5*s,9.2*s,6.5*s,2*s); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx+6*s,jy+44*s, jx-34*s,jy+54*s, jx-46*s,H*0.99);
        c.lineWidth=5*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const mx=(x1+x2)/2,my=Math.min(y1,y2)-13*s; c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo(mx,my,x2,y2); c.lineWidth=3.6*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      let mode; if(inp<0.25){plug(ix0,iy0);mode='BRIGHT';} else if(inp<0.75){jumper(ix0,iy1,ix1,iy0);plug(ix0,iy0);mode='JUMP';} else {plug(ix1,iy0);mode='NORMAL';}
      textSpaced(d,(ix0+ix1)/2,py+ph*.06,F.barlow,6.5,rgb(150,42,30),mode,0.05);
      textSpaced(d,(G.px+G.pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── MARSTEN DSL15 (Marshall DSL15H) — gold-panel 2-channel head. 0 Channel
  //    1 ClassicGain 2 ClassicVol 3 UltraGain 4 UltraVol 5 Presence 6 Bass
  //    7 Middle 8 Treble 9 Deep 10 ToneShift. RS Gain -> Ultra Gain.
  P.marstendsl15 = { w:1620, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:5,cx:.300,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // PRESENCE
      {id:6,cx:.365,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // BASS
      {id:7,cx:.430,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // MIDDLE
      {id:8,cx:.495,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // TREBLE
      {id:4,cx:.590,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // ULTRA VOLUME
      {id:3,cx:.655,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // ULTRA GAIN
      {id:2,cx:.760,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // CLASSIC VOLUME
      {id:1,cx:.825,cy:.770,r:.019,style:'pointer',cap:[24,22,22]} ],// CLASSIC GAIN
    sw3:[
      {id:9, cx:.243,cy:.772,hw:9,hh:16,two:true},   // DEEP
      {id:10,cx:.540,cy:.772,hw:9,hh:16,two:true},   // TONE SHIFT
      {id:0, cx:.705,cy:.772,hw:9,hh:16,two:true} ], // CHANNEL Classic/Ultra
    draw(d,vals){ const {ctx:c,W,H,s}=d; const G=marstenGoldHead(d,'DSL 15');
      const {ink,inkF,py,ph,lblY}=G;
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||8,ink,t,0.02);
      marstenPowerBlock(d,G,0.075);
      lbl(.243,'DEEP',7.5); lbl(.300,'PRESENCE',6.5); lbl(.365,'BASS'); lbl(.430,'MIDDLE',8); lbl(.495,'TREBLE',8);
      lbl(.540,'TONE SH',6.5); lbl(.590,'VOLUME',6.5); lbl(.655,'GAIN'); lbl(.760,'VOLUME',6.5); lbl(.825,'GAIN');
      // section brackets/labels: ULTRA GAIN + CLASSIC GAIN
      textSpaced(d,.6225*W,py+ph*.06,F.barlow,7,ink,'ULTRA GAIN',0.04);
      textSpaced(d,.7925*W,py+ph*.06,F.barlow,7,ink,'CLASSIC GAIN',0.03);
      const chU=(vals&&vals[0]!=null)?vals[0]>=0.5:true;
      textSpaced(d,.705*W,lblY,F.barlow,6.5,ink,'CHAN',0.02);
      ledDot(d,.620*W,py+ph*.14,chU,224,52,46); ledDot(d,.790*W,py+ph*.14,!chU,70,206,96);
      textSpaced(d,(G.px+G.pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── MARSTEN JVM410 (Marshall JVM410H) — 4-channel gold-panel head. 0 Channel
  //    1 Mode 2 Gain 3 Volume 4 Bass 5 Middle 6 Treble 7 Presence 8 Resonance
  //    9 Master 10 Reverb. RS Gain -> Gain (Channel OD1 + Mode orange via _static).
  P.marstenjvm410 = { w:1700, h:600, ptr:rgb(244,242,236),
    knobs:[
      {id:0,cx:.175,cy:.770,r:.019,style:'pointer',cap:[24,22,22],select:4},  // CHANNEL
      {id:1,cx:.240,cy:.770,r:.019,style:'pointer',cap:[24,22,22],select:3},  // MODE
      {id:2,cx:.320,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // GAIN
      {id:3,cx:.385,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // VOLUME
      {id:4,cx:.450,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // BASS
      {id:5,cx:.515,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // MIDDLE
      {id:6,cx:.580,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // TREBLE
      {id:7,cx:.645,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // PRESENCE
      {id:8,cx:.710,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // RESONANCE
      {id:9,cx:.775,cy:.770,r:.019,style:'pointer',cap:[24,22,22]},  // MASTER
      {id:10,cx:.840,cy:.770,r:.019,style:'pointer',cap:[24,22,22]} ],// REVERB
    draw(d,vals){ const {ctx:c,W,H,s}=d; const G=marstenGoldHead(d,'JVM 410');
      const {ink,inkF,py,ph,lblY}=G;
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||7.5,ink,t,0.02);
      marstenPowerBlock(d,G,0.055);
      const chn=(vals&&vals[0]!=null)?vals[0]:0.66; const md=(vals&&vals[1]!=null)?vals[1]:0.5;
      const chName=chn<0.25?'CLEAN':chn<0.5?'CRUNCH':chn<0.75?'OD1':'OD2';
      const mdName=md<0.33?'GREEN':md<0.66?'ORANGE':'RED';
      lbl(.175,chName,7); lbl(.240,mdName,7);
      textSpaced(d,.175*W,py+ph*.06,F.barlow,6.5,ink,'CHANNEL',0.03); textSpaced(d,.240*W,py+ph*.06,F.barlow,6.5,ink,'MODE',0.03);
      // mode LED (green/orange/red)
      const mc=md<0.33?[70,206,96]:md<0.66?[236,150,40]:[224,52,46];
      ledDot(d,.240*W,py+ph*.155,true,mc[0],mc[1],mc[2]);
      lbl(.320,'GAIN'); lbl(.385,'VOLUME',6.5); lbl(.450,'BASS'); lbl(.515,'MIDDLE',7); lbl(.580,'TREBLE',7);
      lbl(.645,'PRESENCE',6); lbl(.710,'RESON',6.5); lbl(.775,'MASTER',6.5); lbl(.840,'REVERB',6.5);
      textSpaced(d,(G.px+G.pw)-44*s,py+ph*.90,F.bebas,12,rgb(40,34,24),'MARSTEN',0.08); } };

  // ── RANEY AOR50 (Laney AOR 50 Pro Tube Lead) — black tolex head, "RANEY"
  //    badge, bottom control panel with 8 knobs + 5 pull-switches. ids:
  //    0 Channel(PullAOR) 1 AorPreamp 2 AorMaster 3 AorBright 4 Ch1Preamp
  //    5 Ch1Master 6 Ch1Bright 7 Bass 8 Middle 9 Treble 10 Deep 11 MidBoost
  //    12 Presence. RS: Gain->Channel morph, Bass/Mid/Treble->stack, Pres->Presence.
  P.aor50 = { w:1500, h:600, ptr:rgb(238,239,242),
    knobs:[
      {id:12,cx:.175,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // PRESENCE
      {id:7, cx:.245,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // BASS
      {id:8, cx:.315,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // MIDDLE
      {id:9, cx:.385,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // TREBLE
      {id:5, cx:.485,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // CH1 MASTER
      {id:4, cx:.555,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // CH1 PREAMP
      {id:2, cx:.655,cy:.700,r:.023,style:'pointer',cap:[20,20,22]},  // AOR MASTER
      {id:1, cx:.725,cy:.700,r:.023,style:'pointer',cap:[20,20,22]} ],// AOR PREAMP
    // pull-pot toggles — drawn by draw() as small "PULL …" buttons ABOVE each pot (the real
    // amp uses push-pull pots, NOT separate levers). hidden:true = click-only; two:true = on/off.
    sw3:[
      {id:10,cx:.245,cy:.575,hw:27,hh:9,two:true,hidden:true},  // BASS   Pull-Deep
      {id:11,cx:.315,cy:.575,hw:27,hh:9,two:true,hidden:true},  // MIDDLE Pull-Boost
      {id:6, cx:.485,cy:.575,hw:27,hh:9,two:true,hidden:true},  // CH1    Pull-Bright
      {id:3, cx:.655,cy:.575,hw:27,hh:9,two:true,hidden:true},  // AOR    Pull-Bright
      {id:0, cx:.725,cy:.575,hw:27,hh:9,two:true,hidden:true} ],// AOR    Pull-On (channel)
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,233,236), faint='rgba(214,216,222,0.55)', chr=rgb(186,190,196);
      const ch=(vals&&vals[0]!=null)?vals[0]:1;
      // ── black textured tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(30,29,31)); bgr.addColorStop(0.5,rgb(20,19,21)); bgr.addColorStop(1,rgb(11,10,12));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.018)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.22)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x+3.5*s,0); c.lineTo(x+3.5*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(236,238,242)); g.addColorStop(1,rgb(116,120,126));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      // ── top strap handle ──
      const hx0=.44*W,hx1=.56*W,hcy=H*.045,hh=H*.03;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(14,14,16));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // ── chrome corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.07; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.32,cyy+dy*k*0.32,cxx,cyy+dy*k); c.closePath();
        const gg=c.createLinearGradient(cxx,cyy,cxx+dx*k,cyy+dy*k); gg.addColorStop(0,rgb(228,232,236)); gg.addColorStop(0.5,rgb(150,154,160)); gg.addColorStop(1,rgb(96,100,106));
        c.fillStyle=gg; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.9*s; c.stroke(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.4*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── front baffle (plain black tolex) + cream piping frame (AOR signature) ──
      const gy=H*.085, gh=H*.405, gx=W*.045, gw=W*.91;
      rr(c,gx,gy,gw,gh,6*s); c.fillStyle=rgb(18,17,17); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,6*s); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.014)';
      for(let x=gx-gh;x<gx+gw;x+=6*s){ c.beginPath(); c.moveTo(x,gy); c.lineTo(x+gh,gy+gh); c.stroke(); } c.restore();
      // cream/beige piping border
      rr(c,gx,gy,gw,gh,7*s); c.strokeStyle=rgb(208,198,162); c.lineWidth=3*s; c.stroke();
      rr(c,gx+4*s,gy+4*s,gw-8*s,gh-8*s,5*s); c.strokeStyle='rgba(150,142,112,0.5)'; c.lineWidth=1*s; c.stroke();
      // RANEY badge — black plate with white block letters
      const bw=W*.20, bh=gh*.30, bx=W*.5-bw/2, by=gy+gh*.5-bh/2;
      rr(c,bx,by,bw,bh,5*s); const bgd=c.createLinearGradient(0,by,0,by+bh); bgd.addColorStop(0,rgb(28,28,30)); bgd.addColorStop(1,rgb(8,8,10));
      c.fillStyle=bgd; c.fill(); rr(c,bx,by,bw,bh,5*s); c.strokeStyle=rgb(150,152,158); c.lineWidth=1.6*s; c.stroke();
      bolt(bx+10*s,by+bh*.5,2.6*s); bolt(bx+bw-10*s,by+bh*.5,2.6*s);
      textSpaced(d,W*.5,by+bh*.52,F.anton,46,rgb(238,240,244),'RANEY',0.06);
      // ── black control panel (bottom strip) ──
      const py=H*.55, ph=H*.41, px=W*.03, pw=W*.94;
      const nameY=py+ph*.16, pullY=py+ph*.06;   // labels + PULL buttons ABOVE the pots (real layout)
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(26,26,28)); pg.addColorStop(1,rgb(12,12,14));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2*s; c.stroke();
      bolt(px+9*s,py+9*s,2.6*s); bolt(px+pw-9*s,py+9*s,2.6*s); bolt(px+9*s,py+ph-9*s,2.6*s); bolt(px+pw-9*s,py+ph-9*s,2.6*s);
      // POWER (red rocker) + STANDBY (grey rocker) + model text — far left
      const rocker=(rx,red)=>{ const ry=py+ph*.45, rw=15*s, rh=23*s;
        rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); const g=c.createLinearGradient(0,ry-rh/2,0,ry+rh/2);
        if(red){ g.addColorStop(0,rgb(230,70,56)); g.addColorStop(1,rgb(146,28,24)); } else { g.addColorStop(0,rgb(78,78,82)); g.addColorStop(1,rgb(32,32,36)); }
        c.fillStyle=g; c.fill(); rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); c.strokeStyle=rgb(10,10,12); c.lineWidth=1.3*s; c.stroke();
        rr(c,rx-rw/2+2*s,ry-rh/2+2*s,rw-4*s,rh*0.40,2*s); c.fillStyle='rgba(255,255,255,0.22)'; c.fill(); };
      rocker(.052*W,true); rocker(.092*W,false);
      textSpaced(d,.052*W,pullY,F.barlow,8,ink,'POWER',0.03); textSpaced(d,.092*W,pullY,F.barlow,7.5,ink,'STANDBY',0.02);
      textSpaced(d,.072*W,py+ph*.80,F.barlow,8,faint,'AOR50  SERIES II',0.04);
      textSpaced(d,.072*W,py+ph*.92,F.barlow,6.5,faint,'MADE IN ENGLAND',0.03);
      // knob NAME labels (above each pot)
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,nameY,F.barlow,sz||10.5,ink,t,0.02);
      lbl(.175,'PRESENCE',9); lbl(.245,'BASS'); lbl(.315,'MIDDLE'); lbl(.385,'TREBLE');
      lbl(.485,'MASTER'); lbl(.555,'PREAMP'); lbl(.655,'MASTER'); lbl(.725,'PREAMP');
      // PULL-pot toggles — push-pull pots like the real amp: a small "PULL …" button above
      // the pot that lights green when engaged (no separate lever switches).
      const pull=(cx,id,txt)=>{ const on=(vals&&vals[id]!=null)?vals[id]>0.5:false;
        const lx=cx*W, bw=52*s, bh=14*s; rr(c,lx-bw/2,pullY-bh/2,bw,bh,3*s);
        c.fillStyle= on? rgb(36,84,42) : rgb(28,28,31); c.fill();
        rr(c,lx-bw/2,pullY-bh/2,bw,bh,3*s); c.strokeStyle= on? rgb(96,206,116) : rgb(66,68,72); c.lineWidth=1*s; c.stroke();
        textSpaced(d,lx,pullY,F.barlow,6.5, on? rgb(184,242,190):faint, txt, 0.0); };
      pull(.245,10,'PULL DEEP'); pull(.315,11,'PULL BOOST'); pull(.485,6,'PULL BRIGHT');
      pull(.655,3,'PULL BRIGHT'); pull(.725,0,'PULL AOR ON');
      // channel section brackets (BELOW the channel pots, opening up toward them)
      const brk=(x0,x1,t)=>{ const y=py+ph*.84; c.strokeStyle=faint; c.lineWidth=1.2*s;
        c.beginPath(); c.moveTo(x0*W,y-5*s); c.lineTo(x0*W,y); c.lineTo(x1*W,y); c.lineTo(x1*W,y-5*s); c.stroke();
        textSpaced(d,(x0+x1)/2*W,y+8*s,F.barlow,8.5,ink,t,0.05); };
      brk(.455,.585,'CHANNEL ONE'); brk(.625,.755,'AOR CHANNEL');
      // ── INPUTS (Low/High sensitivity) — far right ──
      const jack=(jx)=>{ c.beginPath(); c.arc(jx,py+ph*.45,9*s,0,7); c.fillStyle=rgb(18,16,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx,py+ph*.45,3*s,0,7); c.fillStyle=rgb(54,54,58); c.fill(); };
      const jL=.850*W, jH=.915*W; jack(jL); jack(jH);
      textSpaced(d,(jL+jH)/2,pullY,F.barlow,8.5,faint,'INPUTS',0.04);
      textSpaced(d,jL,py+ph*.72,F.barlow,8.5,ink,'LOW',0.03); textSpaced(d,jH,py+ph*.72,F.barlow,8.5,ink,'HIGH',0.03); } };

  // ── RONALD JC-90 (Roland JC-90 Jazz Chorus) — black combo, top control strip
  //    + salt-and-pepper grille, "Ronald" logo. ids: 0 Distortion 1 Volume
  //    2 HiTreble 3 Treble 4 Middle 5 Bass 6 Reverb 7 Rate 8 Depth 9 Chorus(3-way).
  //    RS: Gain->Distortion, Bass/Mid/Treble->stack, Pres->HiTreble.
  P.jc90 = { w:1380, h:600, ptr:rgb(244,245,248), tick:rgb(172,174,180),
    // Roland JC-90 knobs: small, silver fluted skirt + black insert + white pointer + 0–10 tick fan ('ampeg').
    knobs:[
      {id:0,cx:.103,cy:.232,r:.018,style:'ampeg'},  // DISTORTION
      {id:1,cx:.158,cy:.232,r:.018,style:'ampeg'},  // VOLUME
      {id:2,cx:.228,cy:.232,r:.018,style:'ampeg'},  // HI-TREBLE
      {id:3,cx:.283,cy:.232,r:.018,style:'ampeg'},  // TREBLE
      {id:4,cx:.338,cy:.232,r:.018,style:'ampeg'},  // MIDDLE
      {id:5,cx:.393,cy:.232,r:.018,style:'ampeg'},  // BASS
      {id:6,cx:.460,cy:.232,r:.018,style:'ampeg'},  // REVERB
      {id:7,cx:.548,cy:.232,r:.018,style:'ampeg'},  // RATE
      {id:8,cx:.603,cy:.232,r:.018,style:'ampeg'},  // DEPTH
      {id:9,cx:.658,cy:.232,r:.018,style:'ampeg',select:3,tick:'rgba(0,0,0,0)'} ],// CHORUS 3-pos SELECTOR (own detents; no 0–10 fan)
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(238,239,242), faint='rgba(214,216,222,0.6)', chr=rgb(186,190,196), gold=rgb(176,150,86);
      // ── black tolex combo ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(28,27,29)); bgr.addColorStop(1,rgb(12,11,13));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.016)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||4*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(96,96,100)); g.addColorStop(0.6,rgb(54,54,58)); g.addColorStop(1,rgb(22,22,24));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(14,14,16); c.lineWidth=0.6*s; c.stroke(); };
      for(let i=0;i<13;i++){ const rx=W*(.06+i*.072); bolt(rx,H*.035,4.2*s); bolt(rx,H*.965,4.2*s); }
      // ── control panel (black strip, top — labels ABOVE the knobs) ──
      const py=H*.07, ph=H*.30, px=W*.035, pw=W*.93, lblY=py+ph*.16, cy=py+ph*.60;
      rr(c,px-2*s,py-2*s,pw+4*s,ph+4*s,6*s); c.fillStyle=rgb(150,152,158); c.fill();   // silver frame
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(30,30,33)); pg.addColorStop(1,rgb(14,14,16));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||12,ink,t,0.02);
      // ── INPUT jacks (HIGH / LOW) ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,9*s,0,7); c.fillStyle=rgb(18,16,16); c.fill(); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,3*s,0,7); c.fillStyle=rgb(52,52,56); c.fill(); };
      jack(.050*W,cy); jack(.082*W,cy);
      textSpaced(d,.066*W,lblY,F.barlow,10,ink,'INPUT',0.03);
      textSpaced(d,.050*W,cy+17*s,F.barlow,8,faint,'HIGH',0.02); textSpaced(d,.082*W,cy+17*s,F.barlow,8,faint,'LOW',0.02);
      // ── knob labels ──
      lbl(.103,'DISTORTION',10); lbl(.158,'VOLUME');
      // EQUALIZER bracket
      const eqL=.198*W, eqR=.422*W, ey=py+ph*.025;
      c.strokeStyle=faint; c.lineWidth=1.4*s; c.beginPath(); c.moveTo(eqL,ey+7*s); c.lineTo(eqL,ey); c.lineTo(eqR,ey); c.lineTo(eqR,ey+7*s); c.stroke();
      textSpaced(d,(eqL+eqR)/2,ey-2*s,F.barlow,10,ink,'EQUALIZER',0.06);
      lbl(.228,'HI-TREBLE',10.5); lbl(.283,'TREBLE'); lbl(.338,'MIDDLE'); lbl(.393,'BASS'); lbl(.460,'REVERB');
      // ── CHORUS green sub-panel (Rate / Depth / Chorus) ──
      const cx0=.512*W, cw=.182*W, cy0=py+ph*.06, ch=ph*.88;
      rr(c,cx0,cy0,cw,ch,4*s); const cg=c.createLinearGradient(0,cy0,0,cy0+ch); cg.addColorStop(0,rgb(40,58,42)); cg.addColorStop(1,rgb(22,36,24));
      c.fillStyle=cg; c.fill(); rr(c,cx0,cy0,cw,ch,4*s); c.strokeStyle=rgb(74,100,76); c.lineWidth=1*s; c.stroke();
      lbl(.548,'RATE'); lbl(.603,'DEPTH');
      // CHORUS = rotary 3-position SELECTOR (FIXED / OFF / MANUAL) — click steps detents
      textSpaced(d,.658*W,py+ph*.10,F.barlow,9.5,ink,'CHORUS',0.02);
      const chx=.658*W, chy=.232*H, kR=.018*W;
      c.strokeStyle='rgba(204,228,204,0.92)'; c.lineWidth=2*s; c.lineCap='round';
      [0,0.5,1].forEach(v=>{ const a=ang(v), r1=kR*1.20, r2=kR*1.50;
        c.beginPath(); c.moveTo(chx+Math.cos(a)*r1,chy+Math.sin(a)*r1); c.lineTo(chx+Math.cos(a)*r2,chy+Math.sin(a)*r2); c.stroke(); });
      c.lineCap='butt';
      setFont(d,F.barlow,7.5); c.fillStyle='rgba(202,230,202,0.95)'; c.textAlign='center'; c.textBaseline='middle';
      const lr=kR*1.92, La=(v,t)=>{ const a=ang(v); c.fillText(t,chx+Math.cos(a)*lr,chy+Math.sin(a)*lr); };
      La(0,'FIX'); La(0.5,'OFF'); La(1,'MAN');
      // ── JAZZ CHORUS-90 wordmark (no box) + power (right) ──
      textSpaced(d,.795*W,cy-ph*.16,F.bebas,24,ink,'JAZZ CHORUS-90',0.03);
      textSpaced(d,.795*W,cy+ph*.16,F.barlow,8.5,faint,'PERSONAL STEREO AMPLIFIER',0.06);
      ledDot(d,.915*W,cy,true,224,52,46);
      batToggle(d,.952*W,cy,10*s,true); textSpaced(d,.952*W,lblY,F.barlow,9.5,ink,'POWER',0.03);
      // ── gold piping line under the panel ──
      c.strokeStyle=gold; c.lineWidth=4*s; c.beginPath(); c.moveTo(px,py+ph+10*s); c.lineTo(px+pw,py+ph+10*s); c.stroke();
      c.strokeStyle='rgba(120,98,52,0.7)'; c.lineWidth=1*s; c.beginPath(); c.moveTo(px,py+ph+13*s); c.lineTo(px+pw,py+ph+13*s); c.stroke();
      // ── salt-and-pepper grille (lower) + Roland-style "Ronald" logo ──
      const gy=py+ph+H*.035, gh=H*.94-gy, gx=W*.04, gw=W*.92;
      rr(c,gx,gy,gw,gh,4*s); c.fillStyle=rgb(15,15,16); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,4*s); c.clip();
      for(let yy=gy; yy<gy+gh; yy+=3*s){ for(let xx=gx; xx<gx+gw; xx+=3*s){
        const n=((xx*13+yy*7)%17); if(n<3){ c.fillStyle='rgba(176,170,120,0.15)'; c.fillRect(xx,yy,1.5*s,1.5*s); } } }
      c.restore();
      rr(c,gx,gy,gw,gh,4*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.4*s; c.stroke();
      // "Ronald" white wordmark + parody square-R mark (lower-left, large)
      const mr=gh*.20, lx=gx+gw*.06+mr, ly=gy+gh*.62;
      c.save(); rr(c,lx-mr,ly-mr,2*mr,2*mr,6*s); c.fillStyle=rgb(244,245,248); c.fill();
      c.strokeStyle=rgb(15,15,16); c.lineWidth=6*s; c.lineCap='round'; c.lineJoin='round';
      c.beginPath(); c.moveTo(lx-mr*0.42,ly-mr*0.5); c.lineTo(lx-mr*0.42,ly+mr*0.5);
      c.lineTo(lx+mr*0.45,ly+mr*0.5); c.lineTo(lx+mr*0.45,ly-mr*0.05); c.lineTo(lx-mr*0.42,ly-mr*0.05); c.stroke();
      c.lineCap='butt'; c.lineJoin='miter'; c.restore();
      c.textAlign='left'; c.textBaseline='middle'; setFont(d,F.anton,Math.round(mr*1.4)); c.fillStyle=rgb(244,245,248);
      c.fillText('Ronald', lx+mr+14*s, ly); } };

  // ── ENGEL FIREBALL (ENGL Fireball 100) — black metal head, red-lit grille,
  //    steel "ENGEL" logo. 8 knobs: 0 Clean Gain 1 Lead Gain 2 Bass 3 Middle
  //    4 Treble 5 Lead Volume 6 Master 7 Presence + sw 8 Channel 9 Bright
  //    10 Bottom 11 Mid Boost. RS Gain -> Lead Gain.
  P.engelfireball = { w:1760, h:560, ptr:rgb(236,238,242), tick:rgb(150,152,158),
    knobs:[
      {id:0,cx:.110,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // CLEAN GAIN
      {id:1,cx:.175,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // LEAD GAIN
      {id:2,cx:.300,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // BASS
      {id:3,cx:.365,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // MIDDLE
      {id:4,cx:.430,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // TREBLE
      {id:5,cx:.520,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // LEAD VOLUME
      {id:6,cx:.640,cy:.770,r:.019,style:'pointer',cap:[26,26,30]},  // MASTER
      {id:7,cx:.705,cy:.770,r:.019,style:'pointer',cap:[26,26,30]} ],// PRESENCE
    sw3:[
      {id:9, cx:.230,cy:.772,hw:9,hh:15,two:true},   // BRIGHT
      {id:10,cx:.265,cy:.772,hw:9,hh:15,two:true},   // BOTTOM
      {id:11,cx:.565,cy:.772,hw:9,hh:15,two:true},   // MID BOOST
      {id:8, cx:.600,cy:.772,hw:9,hh:15,two:true} ], // CHANNEL Clean/Lead
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,234,238), inkF='rgba(232,234,238,0.6)', steel=rgb(206,210,216), red=rgb(210,40,34);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(22,22,24)); bgr.addColorStop(1,rgb(10,10,12));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(150,152,158)); g.addColorStop(1,rgb(60,62,66)); c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); };
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.10; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(40,42,46); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,3*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // red-lit black mesh grille (upper)
      const gy=H*.085, gh=H*.40, gx=W*.05, gw=W*.90;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(14,14,16); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip();
      const rg=c.createRadialGradient(W*.5,gy+gh*.7,gh*.1,W*.5,gy+gh*.7,gw*.5); rg.addColorStop(0,'rgba(200,40,30,0.45)'); rg.addColorStop(1,'rgba(120,16,12,0)');
      c.fillStyle=rg; c.fillRect(gx,gy,gw,gh);
      c.fillStyle='rgba(60,62,66,0.5)'; for(let yy=gy+3*s;yy<gy+gh;yy+=4*s){ for(let xx=gx+3*s;xx<gx+gw;xx+=4*s){ c.beginPath(); c.arc(xx,yy,1*s,0,7); c.fill(); } }
      c.restore(); rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=2*s; c.stroke();
      // steel "ENGEL" logo (centre) + Fireball script
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.anton,68); const sg=c.createLinearGradient(0,gy+gh*.25,0,gy+gh*.62); sg.addColorStop(0,rgb(244,246,250)); sg.addColorStop(0.5,rgb(180,184,190)); sg.addColorStop(1,rgb(120,124,130));
      c.fillStyle=sg; c.fillText('ENGEL',W*.5,gy+gh*.44); c.restore();
      textC(d,W*.86,gy+gh*.84,F.crete,30,steel,'Fireball 100','right');
      // black control panel
      const py=H*.595, ph=H*.30, px=W*.035, pw=W*.93, ly=py+ph*.88;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(34,35,38)); pg.addColorStop(1,rgb(16,17,19));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(74,76,80); c.lineWidth=1.2*s; c.stroke();
      const cy=py+ph*.45;
      // input jack
      c.beginPath(); c.arc(.045*W,cy,7*s,0,7); c.fillStyle=rgb(14,13,14); c.fill(); c.strokeStyle=steel; c.lineWidth=2*s; c.stroke();
      textSpaced(d,.045*W,py+ph*.16,F.barlow,6.5,ink,'INPUT',0.02);
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,ly,F.barlow,sz||7,ink,t,0.02);
      lbl(.110,'CLEAN GAIN',6); lbl(.175,'LEAD GAIN',6); lbl(.300,'BASS'); lbl(.365,'MIDDLE',7); lbl(.430,'TREBLE',7);
      lbl(.520,'LEAD VOL',6.5); lbl(.640,'MASTER',6.5); lbl(.705,'PRESENCE',6);
      lbl(.230,'BRIGHT',5.5); lbl(.265,'BOTTOM',5.5); lbl(.565,'M.BOOST',5); lbl(.600,'CHANNEL',5.5);
      // channel LED (green clean / red lead) + mid-boost LED
      const lead=(vals&&vals[8]!=null)?vals[8]>=0.5:true;
      ledDot(d,.600*W,py+ph*.13,true,lead?224:70,lead?52:206,lead?46:96);
      // power/standby (far right)
      batToggle(d,.800*W,cy,8*s,true); textSpaced(d,.800*W,py+ph*.16,F.barlow,6.5,ink,'STBY',0.02);
      batToggle(d,.850*W,cy,8*s,true); textSpaced(d,.850*W,py+ph*.16,F.barlow,6.5,ink,'POWER',0.02);
      ledDot(d,.888*W,cy,true,224,52,46);
      textSpaced(d,(px+pw)-40*s,py+ph*.90,F.bebas,11,steel,'ENGEL',0.10); } };

  // ── POLYSTONE MINIBRUTE (Polytone Mini-Brute) — small solid-state jazz combo,
  //    brushed-aluminium panel. 0 Volume 1 Bass 2 Treble + Brite(3). RS Gain->Volume.
  P.polystoneminibrute = { w:1200, h:430, ptr:rgb(60,60,64), tick:rgb(120,120,126),
    knobs:[
      {id:1,cx:.255,cy:.470,r:.045,style:'knurled'},  // BASS
      {id:2,cx:.380,cy:.470,r:.045,style:'knurled'},  // TREBLE
      {id:0,cx:.505,cy:.470,r:.045,style:'knurled'} ],// VOLUME
    sw3:[{id:3,cx:.620,cy:.470,hw:11,hh:20,two:true}],  // BRITE
    draw(d,vals){ const {ctx:c,W,H}=d;
      const ink=rgb(34,34,38), dim=rgb(96,96,102);
      // black combo tolex
      c.fillStyle=rgb(20,20,22); c.fillRect(0,0,W,H);
      // brushed-aluminium panel
      const px=W*.04,py=H*.10,pw=W*.92,ph=H*.62;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(214,216,220)); pg.addColorStop(0.5,rgb(188,190,196)); pg.addColorStop(1,rgb(150,152,158));
      rr(c,px,py,pw,ph,5); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,5); c.clip(); c.strokeStyle='rgba(255,255,255,0.18)'; c.lineWidth=1;
      for(let x=px;x<px+pw;x+=2){ c.beginPath(); c.moveTo(x,py); c.lineTo(x,py+ph); c.stroke(); } c.restore();
      rr(c,px,py,pw,ph,5); c.strokeStyle=rgb(110,112,116); c.lineWidth=1.4; c.stroke();
      // POWER toggle + red jewel (left)
      const jcx=W*.075, jcy=H*.30; c.beginPath(); c.arc(jcx,jcy,8,0,7);
      const jg=c.createRadialGradient(jcx-2,jcy-2,1,jcx,jcy,8); jg.addColorStop(0,rgb(255,120,90)); jg.addColorStop(1,rgb(190,40,30)); c.fillStyle=jg; c.fill();
      batToggle(d,W*.075,H*.52,9,true); textC(d,W*.075,H*.70,F.barlow,8,ink,'POWER');
      // "MINI BRUTE" header
      textSpaced(d,W*.38,H*.205,F.barlow,11,ink,'MINI BRUTE',0.18);
      // knob labels
      const lbl=(cx,t)=>textC(d,cx*W,H*.74,F.barlow,9.5,ink,t);
      lbl(.255,'BASS'); lbl(.380,'TREBLE'); lbl(.505,'VOLUME');
      textC(d,.620*W,H*.30,F.barlow,8.5,ink,'BRITE'); textC(d,.620*W,H*.66,F.barlow,7,dim,'HI / LO');
      // Hi/Lo input jacks
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,9,0,7); c.fillStyle=rgb(18,16,16); c.fill(); c.strokeStyle=rgb(120,122,126); c.lineWidth=2; c.stroke();
        c.beginPath(); c.arc(x,y,3,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      jack(W*.730,H*.34); jack(W*.730,H*.58);
      textC(d,W*.690,H*.34,F.barlow,8,ink,'HI'); textC(d,W*.690,H*.58,F.barlow,8,ink,'LO');
      // "Polystone" logo (bottom-right, like the panel)
      textC(d,W*.885,H*.46,F.anton,26,ink,'Polystone'); } };

  // ── RONALD JC-120 (Roland JC-120 Jazz Chorus) — clone of the JC-90, the 2x12
  //    stereo sibling. 0 Volume 1 Treble 2 Middle 3 Bass 4 Distortion 5 Reverb
  //    6 Speed 7 Depth 8 Chorus(select:3). RS Gain -> Distortion.
  P.ronaldjc120 = { w:1380, h:600, ptr:rgb(244,245,248), tick:rgb(172,174,180),
    knobs:[
      {id:4,cx:.105,cy:.232,r:.018,style:'ampeg'},  // DISTORTION
      {id:0,cx:.160,cy:.232,r:.018,style:'ampeg'},  // VOLUME
      {id:1,cx:.235,cy:.232,r:.018,style:'ampeg'},  // TREBLE
      {id:2,cx:.290,cy:.232,r:.018,style:'ampeg'},  // MIDDLE
      {id:3,cx:.345,cy:.232,r:.018,style:'ampeg'},  // BASS
      {id:5,cx:.415,cy:.232,r:.018,style:'ampeg'},  // REVERB
      {id:6,cx:.520,cy:.232,r:.018,style:'ampeg'},  // SPEED
      {id:7,cx:.575,cy:.232,r:.018,style:'ampeg'},  // DEPTH
      {id:8,cx:.630,cy:.232,r:.018,style:'ampeg',select:3,tick:'rgba(0,0,0,0)'} ],// CHORUS 3-pos
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(238,239,242), faint='rgba(214,216,222,0.6)', chr=rgb(186,190,196), gold=rgb(176,150,86);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(28,27,29)); bgr.addColorStop(1,rgb(12,11,13));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.016)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||4*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(96,96,100)); g.addColorStop(0.6,rgb(54,54,58)); g.addColorStop(1,rgb(22,22,24));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(14,14,16); c.lineWidth=0.6*s; c.stroke(); };
      for(let i=0;i<13;i++){ const rx=W*(.06+i*.072); bolt(rx,H*.035,4.2*s); bolt(rx,H*.965,4.2*s); }
      const py=H*.07, ph=H*.30, px=W*.035, pw=W*.93, lblY=py+ph*.16, cy=py+ph*.60;
      rr(c,px-2*s,py-2*s,pw+4*s,ph+4*s,6*s); c.fillStyle=rgb(150,152,158); c.fill();
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(30,30,33)); pg.addColorStop(1,rgb(14,14,16));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill();
      const lbl=(cx,t,sz)=>textSpaced(d,cx*W,lblY,F.barlow,sz||11,ink,t,0.02);
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,9*s,0,7); c.fillStyle=rgb(18,16,16); c.fill(); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,3*s,0,7); c.fillStyle=rgb(52,52,56); c.fill(); };
      jack(.050*W,cy); jack(.082*W,cy);
      textSpaced(d,.066*W,lblY,F.barlow,10,ink,'INPUT',0.03);
      textSpaced(d,.050*W,cy+17*s,F.barlow,8,faint,'HIGH',0.02); textSpaced(d,.082*W,cy+17*s,F.barlow,8,faint,'LOW',0.02);
      lbl(.105,'DISTORTION',9.5); lbl(.160,'VOLUME',10);
      const eqL=.205*W, eqR=.375*W, ey=py+ph*.025;
      c.strokeStyle=faint; c.lineWidth=1.4*s; c.beginPath(); c.moveTo(eqL,ey+7*s); c.lineTo(eqL,ey); c.lineTo(eqR,ey); c.lineTo(eqR,ey+7*s); c.stroke();
      textSpaced(d,(eqL+eqR)/2,ey-2*s,F.barlow,10,ink,'EQUALIZER',0.06);
      lbl(.235,'TREBLE'); lbl(.290,'MIDDLE'); lbl(.345,'BASS'); lbl(.415,'REVERB');
      const cx0=.484*W, cw=.182*W, cy0=py+ph*.06, ch=ph*.88;
      rr(c,cx0,cy0,cw,ch,4*s); const cg=c.createLinearGradient(0,cy0,0,cy0+ch); cg.addColorStop(0,rgb(40,58,42)); cg.addColorStop(1,rgb(22,36,24));
      c.fillStyle=cg; c.fill(); rr(c,cx0,cy0,cw,ch,4*s); c.strokeStyle=rgb(74,100,76); c.lineWidth=1*s; c.stroke();
      lbl(.520,'SPEED'); lbl(.575,'DEPTH');
      textSpaced(d,.630*W,py+ph*.10,F.barlow,9,ink,'CHORUS',0.02);
      const chx=.630*W, chy=.232*H, kR=.018*W;
      c.strokeStyle='rgba(204,228,204,0.92)'; c.lineWidth=2*s; c.lineCap='round';
      [0,0.5,1].forEach(v=>{ const a=ang(v),r1=kR*1.20,r2=kR*1.50; c.beginPath(); c.moveTo(chx+Math.cos(a)*r1,chy+Math.sin(a)*r1); c.lineTo(chx+Math.cos(a)*r2,chy+Math.sin(a)*r2); c.stroke(); });
      c.lineCap='butt'; setFont(d,F.barlow,7.5); c.fillStyle='rgba(202,230,202,0.95)'; c.textAlign='center'; c.textBaseline='middle';
      const lr=kR*1.92, La=(v,t)=>{ const a=ang(v); c.fillText(t,chx+Math.cos(a)*lr,chy+Math.sin(a)*lr); };
      La(0,'CHO'); La(0.5,'OFF'); La(1,'VIB');
      textSpaced(d,.800*W,cy-ph*.16,F.bebas,22,ink,'JAZZ CHORUS-120',0.03);
      textSpaced(d,.800*W,cy+ph*.16,F.barlow,8,faint,'STEREO 2x60W',0.06);
      ledDot(d,.915*W,cy,true,224,52,46);
      batToggle(d,.952*W,cy,10*s,true); textSpaced(d,.952*W,lblY,F.barlow,9.5,ink,'POWER',0.03);
      c.strokeStyle=gold; c.lineWidth=4*s; c.beginPath(); c.moveTo(px,py+ph+10*s); c.lineTo(px+pw,py+ph+10*s); c.stroke();
      const gy=py+ph+H*.035, gh=H*.94-gy, gx=W*.04, gw=W*.92;
      rr(c,gx,gy,gw,gh,4*s); c.fillStyle=rgb(15,15,16); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,4*s); c.clip();
      for(let yy=gy; yy<gy+gh; yy+=3*s){ for(let xx=gx; xx<gx+gw; xx+=3*s){ const n=((xx*13+yy*7)%17); if(n<3){ c.fillStyle='rgba(176,170,120,0.15)'; c.fillRect(xx,yy,1.5*s,1.5*s); } } }
      c.restore(); rr(c,gx,gy,gw,gh,4*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.4*s; c.stroke();
      const mr=gh*.20, lx=gx+gw*.06+mr, ly=gy+gh*.62;
      c.save(); rr(c,lx-mr,ly-mr,2*mr,2*mr,6*s); c.fillStyle=rgb(244,245,248); c.fill();
      c.strokeStyle=rgb(15,15,16); c.lineWidth=6*s; c.lineCap='round'; c.lineJoin='round';
      c.beginPath(); c.moveTo(lx-mr*0.42,ly-mr*0.5); c.lineTo(lx-mr*0.42,ly+mr*0.5);
      c.lineTo(lx+mr*0.45,ly+mr*0.5); c.lineTo(lx+mr*0.45,ly-mr*0.05); c.lineTo(lx-mr*0.42,ly-mr*0.05); c.stroke();
      c.lineCap='butt'; c.lineJoin='miter'; c.restore();
      c.textAlign='left'; c.textBaseline='middle'; setFont(d,F.anton,Math.round(mr*1.4)); c.fillStyle=rgb(244,245,248);
      c.fillText('Ronald', lx+mr+14*s, ly); } };

  // ── BENDER BASSMAN (Fender Bassman 5F6-A tweed) — same family/look as the
  //    Bender Deluxe: golden lacquered tweed + brushed-aluminium panel, vintage
  //    chicken-head knobs with 1-12 numeral arcs + clear labels, leather strap
  //    handle. ids: 0 Input(Bright/Both/Normal) 1 BrightVol 2 NormalVol 3 Treble
  //    4 Bass 5 Middle 6 Presence. RS: Gain->Bright Vol, Treble/Bass/Mid->stack,
  //    Pres->Presence; input pinned to Both (jumpered).
  P.tw40 = { w:1260, h:500, ptr:rgb(236,236,232),
    knobs:[
      {id:6,cx:.330,cy:.512,r:.022,style:'vox'},   // PRESENCE
      {id:5,cx:.398,cy:.512,r:.022,style:'vox'},   // MIDDLE
      {id:4,cx:.466,cy:.512,r:.022,style:'vox'},   // BASS
      {id:3,cx:.534,cy:.512,r:.022,style:'vox'},   // TREBLE
      {id:1,cx:.616,cy:.512,r:.022,style:'vox'},   // VOL. BRIGHT  (RS Gain)
      {id:2,cx:.684,cy:.512,r:.022,style:'vox'} ], // VOL. NORMAL
    // clickable input cable (id 0): Bright -> Both(jumpered) -> Normal. Drawn as
    // a plugged-in cable; `hidden` so the engine doesn't stamp a lever over it.
    sw3:[{id:0,cx:.855,cy:.512,hw:110,hh:34,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const chr=rgb(198,202,208), ink=rgb(42,42,46), faint='rgba(40,40,44,0.62)';
      const inp=(vals&&vals[0]!=null)?vals[0]:0.5;
      // ── golden lacquered tweed (diagonal twill weave) — same as Bender Deluxe ──
      const bg=c.createLinearGradient(0,0,0,H); bg.addColorStop(0,rgb(206,170,98)); bg.addColorStop(0.5,rgb(192,156,88)); bg.addColorStop(1,rgb(170,134,72));
      c.fillStyle=bg; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip();
      c.lineWidth=1.4*s; c.strokeStyle='rgba(232,206,150,0.45)';
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.lineWidth=1*s; c.strokeStyle='rgba(120,92,46,0.40)';
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x+2.5*s,0); c.lineTo(x+2.5*s+H,H); c.stroke(); }
      c.lineWidth=0.8*s; c.strokeStyle='rgba(90,68,34,0.18)';
      for(let x=-H;x<W+H;x+=9*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(238,240,244)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(70,72,78); c.lineWidth=0.7*s; c.stroke(); };
      // ── leather strap handle (top centre) + chrome end mounts ──
      const hx0=.42*W, hx1=.58*W, hcy=H*.105, hth=H*.062;
      const lg=c.createLinearGradient(0,hcy-hth,0,hcy+hth); lg.addColorStop(0,rgb(108,66,38)); lg.addColorStop(0.5,rgb(78,44,24)); lg.addColorStop(1,rgb(52,28,15));
      rr(c,hx0,hcy-hth,hx1-hx0,2*hth,hth); c.fillStyle=lg; c.fill();
      rr(c,hx0,hcy-hth,hx1-hx0,2*hth,hth); c.strokeStyle=rgb(32,18,10); c.lineWidth=1.2*s; c.stroke();
      c.save(); rr(c,hx0+6*s,hcy-hth+3*s,hx1-hx0-12*s,2*hth-6*s,hth*0.7); c.clip();
      c.setLineDash([5*s,4*s]); c.strokeStyle='rgba(238,228,206,0.7)'; c.lineWidth=1*s;
      c.beginPath(); c.moveTo(hx0+8*s,hcy-hth*0.5); c.lineTo(hx1-8*s,hcy-hth*0.5); c.stroke();
      c.beginPath(); c.moveTo(hx0+8*s,hcy+hth*0.5); c.lineTo(hx1-8*s,hcy+hth*0.5); c.stroke();
      c.setLineDash([]); c.restore();
      [hx0,hx1].forEach(bx=>{ rr(c,bx-9*s,hcy-hth*0.9,18*s,hth*1.8,3*s); c.fillStyle=chr; c.fill();
        c.strokeStyle=rgb(110,112,118); c.lineWidth=0.8*s; c.stroke(); bolt(bx-4*s,hcy-hth*0.4,2.4*s); bolt(bx+4*s,hcy+hth*0.4,2.4*s); });
      // ── brushed-aluminium control panel (same finish as the Deluxe) ──
      const py=H*.30, ph=H*.40, px=W*.025, pw=W*.95, cy=py+ph*.50, lblY=py+ph*.88;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(208,210,214)); pg.addColorStop(0.5,rgb(184,187,192)); pg.addColorStop(1,rgb(158,161,166));
      rr(c,px,py,pw,ph,4*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,4*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.30)'; c.lineWidth=0.6*s;
      for(let yy=py+2*s; yy<py+ph; yy+=2.4*s){ c.beginPath(); c.moveTo(px,yy); c.lineTo(px+pw,yy); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,4*s); c.strokeStyle=rgb(120,122,126); c.lineWidth=1.4*s; c.stroke();
      bolt(px+10*s,py+10*s,3*s); bolt(px+pw-10*s,py+10*s,3*s); bolt(px+10*s,py+ph-10*s,3*s); bolt(px+pw-10*s,py+ph-10*s,3*s);
      // ── left cluster: power socket + ground toggle + red jewel ──
      c.beginPath(); c.arc(.052*W,cy,9*s,0,7); c.fillStyle=rgb(28,28,30); c.fill(); c.strokeStyle=rgb(120,122,128); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(.052*W-3*s,cy,2*s,0,7); c.fillStyle=rgb(150,152,158); c.fill(); c.beginPath(); c.arc(.052*W+3*s,cy,2*s,0,7); c.fillStyle=rgb(150,152,158); c.fill();
      batToggle(d,.092*W,cy,8*s,true); textSpaced(d,.092*W,py+ph*.18,F.barlow,6.5,ink,'GND',0.03);
      ledDot(d,.128*W,cy,true,224,52,46); c.beginPath(); c.arc(.128*W,cy,8*s,0,7); c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke();
      textSpaced(d,.128*W,py+ph*.18,F.barlow,6.5,ink,'STBY',0.03);
      // ── "Bender Bassman" script + maker text ──
      c.save(); c.translate(.222*W,py+ph*.34); c.transform(1,0,-0.16,1,0,0);
      setFont(d,F.ink,20); c.textAlign='center'; c.textBaseline='middle';
      c.fillStyle=rgb(40,40,44); c.fillText('Bender',-30*s,0);
      setFont(d,F.ink,15); c.fillText('Bassman',32*s,7*s);
      c.restore();
      textSpaced(d,.222*W,py+ph*.66,F.barlow,7,ink,'BENDER ELECTRIC INSTRUMENT CO.',0.02);
      textSpaced(d,.222*W,py+ph*.80,F.barlow,6.5,faint,'FULLERTON, CALIFORNIA',0.05);
      // ── 6 chicken-head knobs: 1-12 numeral arcs + labels (like the Deluxe) ──
      const numArc=(kxF)=>{ const kx=kxF*W; setFont(d,F.barlow,6.5); c.fillStyle=faint; c.textAlign='center'; c.textBaseline='middle';
        for(let n=1;n<=12;n++){ const aa=ang((n-1)/11); const rad=.030*W;
          c.fillText(String(n), kx+rad*Math.cos(aa), cy+rad*Math.sin(aa)); } };
      const lbl=(kxF,t,sz)=>textSpaced(d,kxF*W,lblY,F.barlow,sz||9.5,ink,t,0.03);
      [.330,.398,.466,.534,.616,.684].forEach(kx=>numArc(kx));
      lbl(.330,'PRESENCE',8.5); lbl(.398,'MIDDLE'); lbl(.466,'BASS'); lbl(.534,'TREBLE');
      lbl(.616,'BRIGHT'); lbl(.684,'NORMAL');
      textSpaced(d,.650*W,py+ph*.10,F.barlow,7.5,ink,'VOLUME',0.06);
      // ── right: 2x2 inputs — BRIGHT + NORMAL columns, each with Hi(1)/Lo(2) ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7*s,0,7); c.fillStyle=rgb(24,23,23); c.fill(); c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,2.4*s,0,7); c.fillStyle=rgb(60,60,64); c.fill(); };
      const xB=.860*W, xN=.928*W, cyHi=cy-13*s, cyLo=cy+13*s;
      jack(xB,cyHi); jack(xB,cyLo); jack(xN,cyHi); jack(xN,cyLo);
      textSpaced(d,xB,lblY,F.barlow,8.5,ink,'BRIGHT',0.02);
      textSpaced(d,xN,lblY,F.barlow,8.5,ink,'NORMAL',0.02);
      textSpaced(d,.832*W,cyHi,F.barlow,6,ink,'1',0); textSpaced(d,.832*W,cyLo,F.barlow,6,ink,'2',0);
      const plug=(jx,jy)=>{ rr(c,jx-4.4*s,jy-5.5*s,8.8*s,6.5*s,2*s); c.fillStyle=rgb(40,40,44); c.fill();
        const cg=c.createLinearGradient(jx-4.4*s,jy,jx+4.4*s,jy); cg.addColorStop(0,rgb(182,186,192)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(182,186,192));
        rr(c,jx-4.4*s,jy+1*s,8.8*s,3.6*s,1.4*s); c.fillStyle=cg; c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx+4*s,jy+34*s, jx-34*s,jy+42*s, jx-40*s,H*0.99);
        c.lineWidth=5*s; c.lineCap='round'; c.strokeStyle=rgb(20,20,22); c.stroke();
        c.lineWidth=1.6*s; c.strokeStyle='rgba(255,255,255,0.10)'; c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const mx=(x1+x2)/2, my=Math.max(y1,y2)+13*s;
        c.beginPath(); c.arc(x1,y1,3*s,0,7); c.fillStyle=rgb(40,40,44); c.fill();
        c.beginPath(); c.arc(x2,y2,3*s,0,7); c.fillStyle=rgb(40,40,44); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo(mx,my,x2,y2);
        c.lineWidth=3.6*s; c.lineCap='round'; c.strokeStyle=rgb(20,20,22); c.stroke(); c.lineCap='butt'; };
      let mode;
      if (inp < 0.25)      { plug(xB,cyHi); mode='BRIGHT'; }
      else if (inp < 0.75) { jumper(xB,cyLo,xN,cyHi); plug(xB,cyHi); mode='JUMPERED'; }
      else                 { plug(xN,cyHi); mode='NORMAL'; }
      textSpaced(d,.894*W,py+ph*.12,F.barlow,7,rgb(120,40,30),mode,0.06); } };

  // ── GANDDI SUPERDRIVE 45 (Budda Superdrive 45 Series II) — black tolex head,
  //    purple control panel, white piping chevron, "GANDDI" oval badge. 6 knobs:
  //    0 Master 1 Bass 2 Mid 3 Treble 4 Drive 5 Rhythm + 3 pull toggles (drawn as
  //    lit "PULL ..." buttons below the pots): 6 Channel(Master) 7 Modern(Mid)
  //    8 Brite(Rhythm). RS: Gain->Drive, Bass/Mid/Treble->stack.
  P.superdrive45 = { w:1500, h:620, ptr:rgb(240,236,242),
    knobs:[
      {id:0,cx:.280,cy:.207,r:.020,style:'wedge'},  // MASTER
      {id:1,cx:.370,cy:.207,r:.020,style:'wedge'},  // BASS
      {id:2,cx:.460,cy:.207,r:.020,style:'wedge'},  // MID
      {id:3,cx:.550,cy:.207,r:.020,style:'wedge'},  // TREBLE
      {id:4,cx:.640,cy:.207,r:.020,style:'wedge'},  // DRIVE
      {id:5,cx:.730,cy:.207,r:.020,style:'wedge'} ],// RHYTHM
    // pull-pots (drawn as "PULL ..." buttons below the pots): Channel/Modern/Brite
    sw3:[
      {id:6,cx:.280,cy:.300,hw:32,hh:10,two:true,hidden:true},  // MASTER pull CHANNEL
      {id:7,cx:.460,cy:.300,hw:32,hh:10,two:true,hidden:true},  // MID    pull MODERN
      {id:8,cx:.730,cy:.300,hw:32,hh:10,two:true,hidden:true} ],// RHYTHM pull BRITE
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const purp=rgb(116,54,124), purpHi=rgb(150,82,158), purpLo=rgb(82,36,90),
            ink=rgb(240,236,242), inkF='rgba(240,236,242,0.62)', chr=rgb(196,200,206);
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(17,16,18)); bgr.addColorStop(1,rgb(10,9,11));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.016)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.22)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x+3.5*s,0); c.lineTo(x+3.5*s-H,H); c.stroke(); }
      c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(110,114,120));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      // top carry handle
      const hx0=.43*W,hx1=.57*W,hcy=H*.045,hh=H*.030;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); const hg=c.createLinearGradient(0,hcy-hh,0,hcy+hh); hg.addColorStop(0,rgb(40,40,44)); hg.addColorStop(1,rgb(13,13,15));
      c.fillStyle=hg; c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // corner caps
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── purple control panel (top strip) ──
      const py=H*.085, ph=H*.32, px=W*.04, pw=W*.92, nameY=py+ph*.89, pullY=py+ph*.67;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,purpHi); pg.addColorStop(0.5,purp); pg.addColorStop(1,purpLo);
      rr(c,px,py,pw,ph,6*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,6*s); c.clip(); c.strokeStyle='rgba(255,255,255,0.05)'; c.lineWidth=1;
      for(let xx=px; xx<px+pw; xx+=3*s){ c.beginPath(); c.moveTo(xx,py); c.lineTo(xx,py+ph); c.stroke(); }
      c.restore();
      rr(c,px,py,pw,ph,6*s); c.strokeStyle=rgb(60,28,66); c.lineWidth=1.6*s; c.stroke();
      const cy=py+ph*.40;
      // ── left: SUPER GANDDI DRIVE badge + ON/OFF (red) + GO/REST (grey) rockers ──
      textSpaced(d,.058*W,py+ph*.30,F.barlow,7.5,ink,'SUPER',0.06);
      textSpaced(d,.058*W,py+ph*.50,F.bebas,9,ink,'GANDDI',0.04);
      textSpaced(d,.058*W,py+ph*.70,F.barlow,7,ink,'DRIVE 45',0.04);
      textSpaced(d,.058*W,py+ph*.86,F.barlow,6,inkF,'SERIES II',0.06);
      const rocker=(rx,red)=>{ const ry=cy, rw=15*s, rh=24*s;
        rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); const g=c.createLinearGradient(0,ry-rh/2,0,ry+rh/2);
        if(red){ g.addColorStop(0,rgb(236,96,40)); g.addColorStop(1,rgb(168,44,16)); } else { g.addColorStop(0,rgb(64,64,68)); g.addColorStop(1,rgb(26,26,30)); }
        c.fillStyle=g; c.fill(); rr(c,rx-rw/2,ry-rh/2,rw,rh,3*s); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.3*s; c.stroke();
        rr(c,rx-rw/2+2*s,ry-rh/2+2*s,rw-4*s,rh*0.40,2*s); c.fillStyle='rgba(255,255,255,0.22)'; c.fill(); };
      rocker(.130*W,true); rocker(.180*W,false);
      textSpaced(d,.130*W,py+ph*.14,F.barlow,7,ink,'ON',0.04); textSpaced(d,.130*W,nameY,F.barlow,6.5,inkF,'OFF',0.04);
      textSpaced(d,.180*W,py+ph*.14,F.barlow,7,ink,'GO',0.04); textSpaced(d,.180*W,nameY,F.barlow,6.5,inkF,'REST',0.04);
      // ── knob names (below) + PULL toggles (lit when engaged) ──
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,nameY,F.barlow,sz||9,ink,t,0.03);
      lbl(.280,'MASTER'); lbl(.370,'BASS'); lbl(.460,'MID'); lbl(.550,'TREBLE'); lbl(.640,'DRIVE'); lbl(.730,'RHYTHM');
      const pull=(cx2,id,txt)=>{ const on=(vals&&vals[id]!=null)?vals[id]>0.5:false;
        const lx=cx2*W, bw=58*s, bh=14*s; rr(c,lx-bw/2,pullY-bh/2,bw,bh,3*s);
        c.fillStyle= on? rgb(150,60,158) : rgb(40,22,44); c.fill();
        rr(c,lx-bw/2,pullY-bh/2,bw,bh,3*s); c.strokeStyle= on? rgb(210,150,216) : rgb(78,44,84); c.lineWidth=1*s; c.stroke();
        textSpaced(d,lx,pullY,F.barlow,6.5, on? rgb(244,222,248):inkF, txt, 0.0); };
      pull(.280,6,'PULL CHANNEL'); pull(.460,7,'PULL MODERN'); pull(.730,8,'PULL BRITE');
      // ── right: INPUT + FOOTSWITCH jacks (stacked) ──
      const jx=.880*W, jHi=cy-15*s, jLo=cy+17*s;
      const jack=(jy)=>{ c.beginPath(); c.arc(jx,jy,8*s,0,7); c.fillStyle=rgb(16,14,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=2*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.6*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      jack(jHi); jack(jLo);
      textSpaced(d,jx+44*s,jHi,F.barlow,7.5,ink,'INPUT',0.03);
      textSpaced(d,jx+52*s,jLo,F.barlow,7,ink,'FOOTSWITCH',0.02);
      // ── white piping chevron (dips toward centre) + GANDDI oval badge ──
      const vy=H*.50;
      c.strokeStyle=rgb(232,232,236); c.lineWidth=2.2*s; c.beginPath();
      c.moveTo(W*.03,vy-H*.02); c.lineTo(W*.40,vy-H*.02); c.lineTo(W*.50,vy+H*.05); c.lineTo(W*.60,vy-H*.02); c.lineTo(W*.97,vy-H*.02); c.stroke();
      const ow=W*.24, oh=H*.16, ocx=W*.5, ocy=H*.74;
      c.beginPath(); c.ellipse(ocx,ocy,ow/2,oh/2,0,0,7); c.fillStyle=purp; c.fill();
      c.strokeStyle=rgb(236,232,238); c.lineWidth=3*s; c.beginPath(); c.ellipse(ocx,ocy,ow/2,oh/2,0,0,7); c.stroke();
      bolt(ocx-ow*0.40,ocy,2.6*s); bolt(ocx+ow*0.40,ocy,2.6*s);
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.ink,46);
      c.fillStyle=rgb(238,232,240); c.fillText('Ganddi',ocx,ocy); c.restore(); } };

  // ── SILLA BOOGIE MARK III (Mesa/Boogie Mark III) — black head, black control
  //    panel, wicker grille, "Silla" badge. 7 knobs: 0 Volume 1 Treble 2 Bass
  //    3 Middle 4 Master 5 Lead Drive 6 Lead Master; 5-band GEQ faders 7..11
  //    (80/240/750/2200/6600); toggles 12 Lead 13 Bright 14 EqIn.
  //    RS: Gain->Lead Drive, Bass/Mid/Treble->stack (Lead + GEQ-V via _static).
  P.markiii = { w:1760, h:620, ptr:rgb(232,233,236),
    knobs:[
      {id:0,cx:.085,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // VOLUME
      {id:1,cx:.145,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // TREBLE
      {id:2,cx:.205,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // BASS
      {id:3,cx:.265,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // MIDDLE
      {id:4,cx:.325,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // MASTER
      {id:5,cx:.385,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // LEAD DRIVE
      {id:6,cx:.445,cy:.215,r:.018,style:'pointer',cap:[22,22,24]} ],// LEAD MASTER
    faders:[
      {id:7, cx:.560,y0:.115,y1:.330},  // EQ 80
      {id:8, cx:.608,y0:.115,y1:.330},  // EQ 240
      {id:9, cx:.656,y0:.115,y1:.330},  // EQ 750
      {id:10,cx:.704,y0:.115,y1:.330},  // EQ 2200
      {id:11,cx:.752,y0:.115,y1:.330} ],// EQ 6600
    sw3:[
      {id:13,cx:.085,cy:.118,hw:30,hh:9,two:true,hidden:true},  // VOLUME pull BRIGHT
      {id:12,cx:.445,cy:.118,hw:30,hh:9,two:true,hidden:true},  // pull LEAD (channel)
      {id:14,cx:.835,cy:.215,hw:16,hh:14,two:true,hidden:true} ],// EQ IN
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(228,229,232), inkF='rgba(228,229,232,0.6)', chr=rgb(196,200,206), tan=rgb(206,182,128);
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,23,25)); bgr.addColorStop(0.5,rgb(16,15,17)); bgr.addColorStop(1,rgb(9,8,10));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.015)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(228,230,234)); g.addColorStop(1,rgb(108,112,118));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.45*W,hx1=.55*W,hcy=H*.04,hh=H*.028;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(26,26,28); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // corner caps
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(8,8,9); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── black control panel ──
      const py=H*.07, ph=H*.34, px=W*.025, pw=W*.95, lblY=py+ph*.80;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(34,34,37)); pg.addColorStop(1,rgb(18,18,20));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(72,74,78); c.lineWidth=1.2*s; c.stroke();
      bolt(px+10*s,py+10*s,2.6*s); bolt(px+pw-10*s,py+10*s,2.6*s); bolt(px+10*s,py+ph-10*s,2.6*s); bolt(px+pw-10*s,py+ph-10*s,2.6*s);
      const cy=py+ph*.45;
      // ── INPUT + FOOTSWITCH jacks (far left) ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7*s,0,7); c.fillStyle=rgb(16,14,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.4*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      jack(.040*W,cy-12*s); jack(.040*W,cy+14*s);
      textSpaced(d,.040*W,py+ph*.10,F.barlow,6,inkF,'INPUT',0.02); textSpaced(d,.040*W,py+ph*.93,F.barlow,5.5,inkF,'FOOT SW',0.02);
      // ── knob labels + pull toggles ──
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,lblY,F.barlow,sz||8,ink,t,0.02);
      lbl(.085,'VOLUME',7.5); lbl(.145,'TREBLE',7.5); lbl(.205,'BASS'); lbl(.265,'MIDDLE',7.5);
      lbl(.325,'MASTER',7.5); lbl(.385,'LEAD DRIVE',6.5); lbl(.445,'LEAD MASTER',6);
      const pull=(cx2,id,txt)=>{ const on=(vals&&vals[id]!=null)?vals[id]>0.5:false;
        const lx=cx2*W, bw=54*s, bh=12*s, ty=py+ph*.115; rr(c,lx-bw/2,ty-bh/2,bw,bh,3*s);
        c.fillStyle= on? rgb(150,120,40) : rgb(30,30,33); c.fill();
        rr(c,lx-bw/2,ty-bh/2,bw,bh,3*s); c.strokeStyle= on? rgb(214,180,90) : rgb(66,68,72); c.lineWidth=1*s; c.stroke();
        textSpaced(d,lx,ty,F.barlow,6, on? rgb(244,224,150):inkF, txt, 0.0); };
      pull(.085,13,'PULL BRIGHT'); pull(.445,12,'PULL LEAD');
      // ── 5-band GRAPHIC EQ (faders drawn by the engine) + freq labels ──
      rr(c,.535*W,py+ph*.05,.245*W,ph*.86,4*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1*s; c.stroke();
      ['80','240','750','2200','6600'].forEach((f,i)=>{ const fx=(.560+i*.048)*W;
        textSpaced(d,fx,py+ph*.95,F.barlow,6.5,inkF,f,0.0); });
      textSpaced(d,.6575*W,py+ph*.02,F.barlow,6.5,inkF,'GRAPHIC EQ',0.04);
      // ── right: EQ IN toggle + STANDBY/POWER + LED + model text ──
      const eqOn=(vals&&vals[14]!=null)?vals[14]>0.5:true;
      batToggle(d,.835*W,cy,8*s,eqOn); textSpaced(d,.835*W,py+ph*.12,F.barlow,7,ink,'EQ IN',0.03);
      batToggle(d,.880*W,cy,8*s,false); textSpaced(d,.880*W,py+ph*.12,F.barlow,6.5,ink,'STANDBY',0.02);
      batToggle(d,.925*W,cy,8*s,true);  textSpaced(d,.925*W,py+ph*.12,F.barlow,7,ink,'POWER',0.03);
      ledDot(d,.958*W,cy,true,224,52,46);
      textSpaced(d,.835*W,py+ph*.93,F.barlow,7,inkF,'SILLA / BOOGIE MARK III',0.03);
      // ── wicker/cane grille (lower) + "Silla" badge ──
      const gy=py+ph+H*.04, gh=H*.96-gy, gx=W*.04, gw=W*.92;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(150,124,78); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(60,44,22,0.5)';
      for(let xx=gx;xx<gx+gw;xx+=8*s){ c.beginPath(); c.moveTo(xx,gy); c.lineTo(xx,gy+gh); c.stroke(); }
      for(let yy=gy;yy<gy+gh;yy+=8*s){ c.beginPath(); c.moveTo(gx,yy); c.lineTo(gx+gw,yy); c.stroke(); }
      c.strokeStyle='rgba(232,210,160,0.35)';
      for(let xx=gx+4*s;xx<gx+gw;xx+=8*s){ c.beginPath(); c.moveTo(xx,gy); c.lineTo(xx,gy+gh); c.stroke(); } c.restore();
      rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=rgb(40,30,16); c.lineWidth=2*s; c.stroke();
      // black "Silla" badge plate (lower-left, like the Boogie badge)
      const bw=W*.16, bh2=gh*.42, bx=gx+gw*.045, by=gy+gh*.5-bh2/2;
      rr(c,bx,by,bw,bh2,5*s); c.fillStyle=rgb(14,14,15); c.fill();
      rr(c,bx,by,bw,bh2,5*s); c.strokeStyle=rgb(120,122,128); c.lineWidth=1.6*s; c.stroke();
      bolt(bx+9*s,by+bh2*.5,2.4*s); bolt(bx+bw-9*s,by+bh2*.5,2.4*s);
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.ink,34);
      c.fillStyle=rgb(232,233,236); c.fillText('Silla',bx+bw*.5,by+bh2*.52); c.restore(); } };

  // ── SILLA BOOGIE MARK II (Mesa/Boogie Mark IIB) — black head, black control
  //    panel, wicker grille, "Silla" badge. The Mark IIB panel (per the photos):
  //    8 knobs (0 Volume1 1 Treble 2 Bass 3 Middle 4 Master1 5 LeadDrive
  //    6 LeadMaster 7 Reverb), 5 pull-switches (8 Lead 9 Bright1 10 Shift
  //    11 GainBoost 12 BrightLead) + 13 HalfPower(100/60), and NO graphic EQ.
  //    RS: Gain->Lead Drive, Bass/Mid/Treble->stack (Lead pinned via _static).
  P.markii = { w:1760, h:620, ptr:rgb(232,233,236),
    knobs:[
      {id:0,cx:.085,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // VOLUME 1
      {id:1,cx:.145,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // TREBLE
      {id:2,cx:.205,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // BASS
      {id:3,cx:.265,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // MIDDLE
      {id:4,cx:.325,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // MASTER 1
      {id:5,cx:.385,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // LEAD DRIVE
      {id:6,cx:.445,cy:.215,r:.018,style:'pointer',cap:[22,22,24]},  // LEAD MASTER
      {id:7,cx:.505,cy:.215,r:.018,style:'pointer',cap:[22,22,24]} ],// REVERB
    sw3:[
      {id:9, cx:.085,cy:.118,hw:30,hh:9,two:true,hidden:true},  // VOLUME 1 pull BRIGHT
      {id:10,cx:.145,cy:.118,hw:30,hh:9,two:true,hidden:true},  // TREBLE pull SHIFT
      {id:11,cx:.325,cy:.118,hw:30,hh:9,two:true,hidden:true},  // MASTER 1 pull GAIN BOOST
      {id:8, cx:.385,cy:.118,hw:30,hh:9,two:true,hidden:true},  // LEAD DRIVE pull LEAD (channel)
      {id:12,cx:.445,cy:.118,hw:30,hh:9,two:true,hidden:true},  // LEAD MASTER pull BRIGHT
      {id:13,cx:.812,cy:.215,hw:16,hh:16,two:true,hidden:true} ],// 1/2 PWR (100/60)
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(228,229,232), inkF='rgba(228,229,232,0.6)', chr=rgb(196,200,206);
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,23,25)); bgr.addColorStop(0.5,rgb(16,15,17)); bgr.addColorStop(1,rgb(9,8,10));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.015)';
      for(let x=-H;x<W;x+=7*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(228,230,234)); g.addColorStop(1,rgb(108,112,118));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.45*W,hx1=.55*W,hcy=H*.04,hh=H*.028;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(26,26,28); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // corner caps
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        c.fillStyle=rgb(8,8,9); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── black control panel ──
      const py=H*.07, ph=H*.34, px=W*.025, pw=W*.95, lblY=py+ph*.80;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(34,34,37)); pg.addColorStop(1,rgb(18,18,20));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(72,74,78); c.lineWidth=1.2*s; c.stroke();
      bolt(px+10*s,py+10*s,2.6*s); bolt(px+pw-10*s,py+10*s,2.6*s); bolt(px+10*s,py+ph-10*s,2.6*s); bolt(px+pw-10*s,py+ph-10*s,2.6*s);
      const cy=py+ph*.45;
      // ── INPUT + FOOTSWITCH jacks (far left) ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7*s,0,7); c.fillStyle=rgb(16,14,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.4*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      jack(.040*W,cy-12*s); jack(.040*W,cy+14*s);
      textSpaced(d,.040*W,py+ph*.10,F.barlow,6,inkF,'INPUT',0.02); textSpaced(d,.040*W,py+ph*.93,F.barlow,5.5,inkF,'FOOT SW',0.02);
      // ── knob labels ──
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,lblY,F.barlow,sz||8,ink,t,0.02);
      lbl(.085,'VOLUME 1',6.5); lbl(.145,'TREBLE',7.5); lbl(.205,'BASS'); lbl(.265,'MIDDLE',7.5);
      lbl(.325,'MASTER 1',6.5); lbl(.385,'LEAD DRIVE',6.5); lbl(.445,'LEAD MASTER',5.8); lbl(.505,'REVERB',7.5);
      // ── pull-switches (lit buttons above the relevant knobs) ──
      const pull=(cx2,id,txt)=>{ const on=(vals&&vals[id]!=null)?vals[id]>0.5:false;
        const lx=cx2*W, bw=52*s, bh=12*s, ty=py+ph*.115; rr(c,lx-bw/2,ty-bh/2,bw,bh,3*s);
        c.fillStyle= on? rgb(150,120,40) : rgb(30,30,33); c.fill();
        rr(c,lx-bw/2,ty-bh/2,bw,bh,3*s); c.strokeStyle= on? rgb(214,180,90) : rgb(66,68,72); c.lineWidth=1*s; c.stroke();
        textSpaced(d,lx,ty,F.barlow,5.5, on? rgb(244,224,150):inkF, txt, 0.0); };
      pull(.085,9,'PULL BRIGHT'); pull(.145,10,'PULL SHIFT'); pull(.325,11,'GAIN BOOST');
      pull(.385,8,'PULL LEAD'); pull(.445,12,'PULL BRIGHT');
      // ── "SILLA ENGINEERING" logo plate (centre-right, like the Mesa badge) ──
      const lx0=.595*W, ly0=py+ph*.30, lw=W*.135, lh=ph*.40;
      rr(c,lx0,ly0,lw,lh,4*s); c.fillStyle=rgb(12,12,13); c.fill(); rr(c,lx0,ly0,lw,lh,4*s); c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke();
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.anton,30); c.fillStyle=rgb(232,233,236); c.fillText('SILLA',lx0+lw*.5,ly0+lh*.40);
      c.restore();
      textSpaced(d,lx0+lw*.5,ly0+lh*.78,F.barlow,8,rgb(150,152,156),'ENGINEERING',0.16);
      // ── right: 1/2-PWR (100/60) + STANDBY + POWER toggles + jewel ──
      const halfOn=(vals&&vals[13]!=null)?vals[13]>0.5:false;
      batToggle(d,.812*W,cy,8*s,!halfOn);  // up = 100 (full), down = 60 (half)
      textSpaced(d,.812*W,py+ph*.10,F.barlow,6,ink,'100 RMS',0.02); textSpaced(d,.812*W,py+ph*.86,F.barlow,6,inkF,'60 RMS',0.02);
      batToggle(d,.862*W,cy,8*s,false); textSpaced(d,.862*W,py+ph*.10,F.barlow,6.5,ink,'STANDBY',0.02);
      batToggle(d,.912*W,cy,8*s,true);  textSpaced(d,.912*W,py+ph*.10,F.barlow,7,ink,'POWER',0.03);
      ledDot(d,.952*W,cy,true,236,150,40);
      textSpaced(d,.882*W,py+ph*.93,F.barlow,6.5,inkF,'SILLA BOOGIE MARK II',0.03);
      // ── wicker/cane grille (lower) + "Silla" badge ──
      const gy=py+ph+H*.04, gh=H*.96-gy, gx=W*.04, gw=W*.92;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(150,124,78); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(60,44,22,0.5)';
      for(let xx=gx;xx<gx+gw;xx+=8*s){ c.beginPath(); c.moveTo(xx,gy); c.lineTo(xx,gy+gh); c.stroke(); }
      for(let yy=gy;yy<gy+gh;yy+=8*s){ c.beginPath(); c.moveTo(gx,yy); c.lineTo(gx+gw,yy); c.stroke(); }
      c.strokeStyle='rgba(232,210,160,0.35)';
      for(let xx=gx+4*s;xx<gx+gw;xx+=8*s){ c.beginPath(); c.moveTo(xx,gy); c.lineTo(xx,gy+gh); c.stroke(); } c.restore();
      rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=rgb(40,30,16); c.lineWidth=2*s; c.stroke();
      // black "Silla" badge plate (lower-left, like the Boogie badge)
      const bw=W*.16, bh2=gh*.42, bx=gx+gw*.045, by=gy+gh*.5-bh2/2;
      rr(c,bx,by,bw,bh2,5*s); c.fillStyle=rgb(14,14,15); c.fill();
      rr(c,bx,by,bw,bh2,5*s); c.strokeStyle=rgb(120,122,128); c.lineWidth=1.6*s; c.stroke();
      bolt(bx+9*s,by+bh2*.5,2.4*s); bolt(bx+bw-9*s,by+bh2*.5,2.4*s);
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.ink,34);
      c.fillStyle=rgb(232,233,236); c.fillText('Silla',bx+bw*.5,by+bh2*.52); c.restore(); } };

  // ── UNPARALLEL DC30 (Matchless DC30) — black tolex head, black control panel,
  //    cream chickenhead knobs, chrome "UNPARALLEL" badge. Two channels:
  //    CH1 Brilliant (Vol/Bass/Treble) + CH2 EF86 (Vol/Tone), shared Cut/Master.
  //    Knobs: 0 Ch1Vol 1 Bass 2 Treble 3 Ch2Vol 4 Tone 5 Cut 6 Master + Channel(7).
  //    RS: Gain->Ch1 Volume, Bass/Treble->Ch1 stack (Channel pinned Ch1 via _static).
  P.unparalleldc30 = { w:1700, h:560, ptr:rgb(238,233,214), tick:rgb(120,112,84),
    knobs:[
      {id:0,cx:.095,cy:.275,r:.020,style:'cream'},  // CH1 VOLUME
      {id:1,cx:.150,cy:.275,r:.020,style:'cream'},  // CH1 BASS
      {id:2,cx:.205,cy:.275,r:.020,style:'cream'},  // CH1 TREBLE
      {id:3,cx:.330,cy:.275,r:.020,style:'cream'},  // CH2 VOLUME
      {id:4,cx:.385,cy:.275,r:.020,style:'cream'},  // CH2 TONE
      {id:5,cx:.500,cy:.275,r:.020,style:'cream'},  // CUT
      {id:6,cx:.555,cy:.275,r:.020,style:'cream'} ],// MASTER
    sw3:[{id:7,cx:.262,cy:.275,hw:11,hh:16,two:true,hidden:true}],  // Channel Ch1/Ch2
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,230,224), inkF='rgba(232,230,224,0.6)', chr=rgb(198,202,208);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(36,38,40)); bgr.addColorStop(0.5,rgb(26,28,30)); bgr.addColorStop(1,rgb(16,17,19));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.02)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.45*W,hx1=.55*W,hcy=H*.045,hh=H*.03;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(20,21,23); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.09; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(10,10,11); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // black control panel
      const py=H*.105, ph=H*.30, px=W*.02, pw=W*.96, ly=py+ph*.16;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(30,31,33)); pg.addColorStop(1,rgb(16,17,19));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(72,74,78); c.lineWidth=1.2*s; c.stroke();
      const cy=py+ph*.55;
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,6.5*s,0,7); c.fillStyle=rgb(14,13,14); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.2*s,0,7); c.fillStyle=rgb(52,50,54); c.fill(); };
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,ly,F.barlow,sz||7.5,ink,t,0.04);
      // CH1 inputs + Vol/Bass/Treble
      jack(.040*W,cy-11*s); jack(.040*W,cy+12*s);
      textSpaced(d,.040*W,ly,F.barlow,6,inkF,'1',0); textSpaced(d,.040*W,py+ph*.92,F.barlow,6,inkF,'2',0);
      lbl(.095,'VOLUME',6.5); lbl(.150,'BASS'); lbl(.205,'TREBLE',7);
      // CH2 inputs + Vol/Tone
      jack(.235*W,cy-11*s); jack(.235*W,cy+12*s);
      textSpaced(d,.235*W,ly,F.barlow,6,inkF,'1',0); textSpaced(d,.235*W,py+ph*.92,F.barlow,6,inkF,'2',0);
      lbl(.330,'VOLUME',6.5); lbl(.385,'TONE',7);
      // channel toggle (CH1/CH2) — small lit indicator above the divider
      const ch2=(vals&&vals[7]!=null)?vals[7]>0.5:false;
      textSpaced(d,.262*W,ly,F.barlow,6,inkF, ch2?'EF86':'BRILL',0.0);
      // divider lines (like the engraved "||" on the real panel)
      c.strokeStyle=rgb(90,92,96); c.lineWidth=1.4*s;
      [.288,.296].forEach(xx=>{ c.beginPath(); c.moveTo(xx*W,py+ph*.30); c.lineTo(xx*W,py+ph*.80); c.stroke(); });
      [.430,.438].forEach(xx=>{ c.beginPath(); c.moveTo(xx*W,py+ph*.30); c.lineTo(xx*W,py+ph*.80); c.stroke(); });
      lbl(.500,'CUT'); lbl(.555,'MASTER',6.5);
      // jewel + OPERATE/STANDBY + ON/OFF (far right)
      const jcx=.815*W, jcy=cy;
      const jg=c.createRadialGradient(jcx-2*s,jcy-2*s,1*s,jcx,jcy,7*s); jg.addColorStop(0,rgb(255,120,96)); jg.addColorStop(0.5,rgb(218,42,32)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,jcy,7*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
      c.beginPath(); c.arc(jcx,jcy,5*s,0,7); c.fillStyle=jg; c.fill(); c.strokeStyle=chr; c.lineWidth=1.2*s; c.beginPath(); c.arc(jcx,jcy,7*s,0,7); c.stroke();
      batToggle(d,.875*W,cy,8*s,true); textSpaced(d,.875*W,ly,F.barlow,6.5,ink,'OPERATE',0.02); textSpaced(d,.875*W,py+ph*.92,F.barlow,6,inkF,'STAND-BY',0.0);
      batToggle(d,.935*W,cy,8*s,true); textSpaced(d,.935*W,ly,F.barlow,7,ink,'ON',0.04); textSpaced(d,.935*W,py+ph*.92,F.barlow,6,inkF,'OFF',0.02);
      // ── black grille + chrome "UNPARALLEL" badge ──
      const gy=py+ph+H*.045, gh=H*.94-gy, gx=W*.035, gw=W*.93;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(20,20,22); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip();
      for(let yy=gy;yy<gy+gh;yy+=3*s){ for(let xx=gx;xx<gx+gw;xx+=3*s){ if(((xx*7+yy*11)%13)<4){ c.fillStyle='rgba(180,180,186,0.05)'; c.fillRect(xx,yy,1.4*s,1.4*s); } } }
      c.restore(); rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=rgb(48,48,52); c.lineWidth=2*s; c.stroke();
      const bw=W*.34, bh2=gh*.40, bx=W*.5-bw/2, by=gy+gh*.5-bh2/2;
      rr(c,bx,by,bw,bh2,4*s); c.fillStyle=rgb(14,14,16); c.fill(); rr(c,bx,by,bw,bh2,4*s); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
      bolt(bx+11*s,by+bh2*.5,2.6*s); bolt(bx+bw-11*s,by+bh2*.5,2.6*s);
      textSpaced(d,W*.5,by+bh2*.52,F.anton,30,rgb(236,237,240),'UNPARALLEL',0.04); } };

  // ── UNPARALLEL CHIEFTAIN (Matchless Chieftain Reverb) — tweed/tan top + black
  //    lower tolex, black panel, cream knobs, "Chieftain" script + "UNPARALLEL"
  //    badge. 7 knobs: 0 Volume 1 Bass 2 Middle 3 Treble 4 Brilliance 5 Master
  //    6 Reverb. RS: Gain->Volume, Bass/Mid/Treble->stack (Brill/Master/Rev static).
  P.unparallelchieftain = { w:1700, h:560, ptr:rgb(238,233,214), tick:rgb(120,112,84),
    knobs:[
      {id:0,cx:.300,cy:.275,r:.020,style:'cream'},  // VOLUME
      {id:1,cx:.370,cy:.275,r:.020,style:'cream'},  // BASS
      {id:2,cx:.440,cy:.275,r:.020,style:'cream'},  // MIDDLE
      {id:3,cx:.510,cy:.275,r:.020,style:'cream'},  // TREBLE
      {id:4,cx:.580,cy:.275,r:.020,style:'cream'},  // BRILLIANCE
      {id:5,cx:.650,cy:.275,r:.020,style:'cream'},  // MASTER
      {id:6,cx:.720,cy:.275,r:.020,style:'cream'} ],// REVERB
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,230,224), inkF='rgba(232,230,224,0.6)', chr=rgb(198,202,208), tan=rgb(196,166,108);
      // tweed/tan top band + black lower tolex
      c.fillStyle=rgb(20,20,22); c.fillRect(0,0,W,H);
      const tg=c.createLinearGradient(0,0,0,H*.46); tg.addColorStop(0,rgb(206,176,116)); tg.addColorStop(1,rgb(176,148,92));
      c.fillStyle=tg; c.fillRect(0,0,W,H*.46);
      c.save(); c.beginPath(); c.rect(0,0,W,H*.46); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(90,70,34,0.18)';
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.45*W,hx1=.55*W,hcy=H*.04,hh=H*.028;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(28,24,18); c.fill(); c.strokeStyle=rgb(10,8,6); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(12,11,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // black control panel
      const py=H*.105, ph=H*.30, px=W*.05, pw=W*.90, ly=py+ph*.16;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(30,31,33)); pg.addColorStop(1,rgb(15,16,18));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(72,74,78); c.lineWidth=1.2*s; c.stroke();
      const cy=py+ph*.55;
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,6.5*s,0,7); c.fillStyle=rgb(14,13,14); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.2*s,0,7); c.fillStyle=rgb(52,50,54); c.fill(); };
      jack(.090*W,cy-11*s); jack(.090*W,cy+12*s);
      textSpaced(d,.090*W,ly,F.barlow,6,inkF,'HIGH',0.0); textSpaced(d,.090*W,py+ph*.92,F.barlow,6,inkF,'LOW',0.0);
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,ly,F.barlow,sz||7,ink,t,0.03);
      lbl(.300,'VOLUME',6.5); lbl(.370,'BASS'); lbl(.440,'MID'); lbl(.510,'TREBLE',6.5);
      lbl(.580,'BRILLIANCE',5.5); lbl(.650,'MASTER',6.5); lbl(.720,'REVERB',6.5);
      // "Chieftain" script (lower-left of panel)
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.crete,26);
      c.fillStyle=rgb(206,176,116); c.fillText('Chieftain',.490*W,py+ph*.83); c.restore();
      // jewel + OPERATE/STANDBY + ON (far right)
      const jcx=.840*W;
      const jg=c.createRadialGradient(jcx-2*s,cy-2*s,1*s,jcx,cy,7*s); jg.addColorStop(0,rgb(255,120,96)); jg.addColorStop(0.5,rgb(218,42,32)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,cy,7*s,0,7); c.fillStyle=rgb(40,8,6); c.fill(); c.beginPath(); c.arc(jcx,cy,5*s,0,7); c.fillStyle=jg; c.fill();
      c.strokeStyle=chr; c.lineWidth=1.2*s; c.beginPath(); c.arc(jcx,cy,7*s,0,7); c.stroke();
      batToggle(d,.890*W,cy,8*s,true); textSpaced(d,.890*W,ly,F.barlow,6.5,ink,'OPERATE',0.02); textSpaced(d,.890*W,py+ph*.92,F.barlow,6,inkF,'STAND-BY',0.0);
      batToggle(d,.945*W,cy,8*s,true); textSpaced(d,.945*W,ly,F.barlow,7,ink,'ON',0.04);
      // ── black grille + chrome "UNPARALLEL" badge ──
      const gy=py+ph+H*.045, gh=H*.94-gy, gx=W*.05, gw=W*.90;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(18,18,20); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip();
      for(let yy=gy;yy<gy+gh;yy+=3*s){ for(let xx=gx;xx<gx+gw;xx+=3*s){ if(((xx*7+yy*11)%13)<4){ c.fillStyle='rgba(180,180,186,0.05)'; c.fillRect(xx,yy,1.4*s,1.4*s); } } }
      c.restore(); rr(c,gx,gy,gw,gh,5*s); c.strokeStyle=tan; c.lineWidth=2*s; c.stroke();
      const bw=W*.34, bh2=gh*.42, bx=W*.5-bw/2, by=gy+gh*.5-bh2/2;
      rr(c,bx,by,bw,bh2,4*s); c.fillStyle=rgb(14,14,16); c.fill(); rr(c,bx,by,bw,bh2,4*s); c.strokeStyle=chr; c.lineWidth=2*s; c.stroke();
      bolt(bx+11*s,by+bh2*.5,2.6*s); bolt(bx+bw-11*s,by+bh2*.5,2.6*s);
      textSpaced(d,W*.5,by+bh2*.52,F.anton,28,rgb(236,237,240),'UNPARALLEL',0.04); } };

  // ── MR. Y MAZ 38 (Dr. Z Maz 38 Senior NR) — black tolex, silver grille with the
  //    big "Y" logo (parody of Dr.Z's "Z") + Legacy badge, black panel, cream knobs.
  //    6 knobs: 0 Volume 1 Treble 2 Middle 3 Bass 4 Cut 5 Master (no reverb).
  //    4xEL84 ~38W. RS: Gain->Volume, Bass/Mid/Treble->stack (Cut/Master static).
  P.mrymaz38 = { w:1700, h:560, ptr:rgb(238,233,214), tick:rgb(120,112,84),
    knobs:[
      {id:5,cx:.420,cy:.770,r:.020,style:'cream'},  // MASTER
      {id:4,cx:.490,cy:.770,r:.020,style:'cream'},  // CUT
      {id:3,cx:.560,cy:.770,r:.020,style:'cream'},  // BASS
      {id:2,cx:.630,cy:.770,r:.020,style:'cream'},  // MIDDLE
      {id:1,cx:.700,cy:.770,r:.020,style:'cream'},  // TREBLE
      {id:0,cx:.770,cy:.770,r:.020,style:'cream'} ],// VOLUME
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,230,224), inkF='rgba(232,230,224,0.6)', chr=rgb(198,202,208), gold=rgb(232,214,150);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,26,28)); bgr.addColorStop(1,rgb(13,13,15));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.02)';
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.44*W,hx1=.56*W,hcy=H*.04,hh=H*.028;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(22,22,24); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.09; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── silver perforated grille (upper) with big "Y" + Legacy badge ──
      const gy=H*.085, gh=H*.46, gx=W*.05, gw=W*.90;
      const sg=c.createLinearGradient(0,gy,0,gy+gh); sg.addColorStop(0,rgb(176,178,184)); sg.addColorStop(1,rgb(120,122,128));
      rr(c,gx,gy,gw,gh,6*s); c.fillStyle=sg; c.fill();
      c.save(); rr(c,gx,gy,gw,gh,6*s); c.clip();
      c.fillStyle='rgba(30,30,34,0.55)';
      for(let yy=gy+4*s;yy<gy+gh;yy+=5*s){ for(let xx=gx+4*s;xx<gx+gw;xx+=5*s){ c.beginPath(); c.arc(xx,yy,1.1*s,0,7); c.fill(); } }
      c.restore(); rr(c,gx,gy,gw,gh,6*s); c.strokeStyle=rgb(232,233,236); c.lineWidth=2.4*s; c.stroke();
      // big "Y" logo (centre)
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.anton,150); c.fillStyle=gold; c.lineWidth=3*s; c.strokeStyle='rgba(40,36,20,0.6)';
      c.strokeText('Y',W*.5,gy+gh*.5); c.fillText('Y',W*.5,gy+gh*.5); c.restore();
      // "Legacy Series" badge (top-right of grille)
      const lbw=W*.10, lbh=gh*.22, lbx=gx+gw-lbw-W*.02, lby=gy+gh*.06;
      rr(c,lbx,lby,lbw,lbh,3*s); c.fillStyle=rgb(248,247,243); c.fill(); rr(c,lbx,lby,lbw,lbh,3*s); c.strokeStyle=rgb(40,40,44); c.lineWidth=1.2*s; c.stroke();
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.crete,16); c.fillStyle=rgb(30,30,34);
      c.fillText('Legacy',lbx+lbw*.5,lby+lbh*.40); c.restore();
      textSpaced(d,lbx+lbw*.5,lby+lbh*.74,F.barlow,7,rgb(90,90,96),'SERIES',0.12);
      // ── black control panel (lower) ──
      const py=H*.595, ph=H*.30, px=W*.035, pw=W*.93, ly=py+ph*.86;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(28,29,31)); pg.addColorStop(1,rgb(14,15,17));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2*s; c.stroke();
      const cy=py+ph*.45;
      // POWER / STANDBY toggles (left) + "MAZ 38" badge
      batToggle(d,.075*W,cy,8*s,true); textSpaced(d,.075*W,py+ph*.16,F.barlow,6.5,ink,'POWER',0.02);
      batToggle(d,.120*W,cy,8*s,true); textSpaced(d,.120*W,py+ph*.16,F.barlow,6.5,ink,'STANDBY',0.0);
      c.save(); c.textAlign='left'; c.textBaseline='middle';
      setFont(d,F.anton,22); c.fillStyle=gold; c.fillText('MAZ',.175*W,cy);
      setFont(d,F.anton,30); c.fillStyle=ink; c.fillText('38',.225*W,cy); c.restore();
      // knob labels
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,ly,F.barlow,sz||7,ink,t,0.03);
      lbl(.420,'MASTER',6.5); lbl(.490,'CUT'); lbl(.560,'BASS'); lbl(.630,'MIDDLE',6.5); lbl(.700,'TREBLE',6.5); lbl(.770,'VOLUME',6.5);
      // INPUT Hi/Lo (far right)
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,6.5*s,0,7); c.fillStyle=rgb(14,13,14); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.2*s,0,7); c.fillStyle=rgb(52,50,54); c.fill(); };
      jack(.880*W,cy-11*s); jack(.880*W,cy+12*s);
      textSpaced(d,.880*W,py+ph*.16,F.barlow,6.5,ink,'INPUT',0.02);
      textSpaced(d,.918*W,cy-11*s,F.barlow,6,inkF,'HI',0); textSpaced(d,.918*W,cy+12*s,F.barlow,6,inkF,'LO',0); } };

  // ── MR. Y EMS (Dr. Z EMS — a JCM800/JTM50-style head) — black tolex w/ gold
  //    piping, silver grille with the big "Y" + Legacy badge, gold "EMS" panel,
  //    cream knobs. 6 knobs: 0 Gain 1 Bass 2 Middle 3 Treble 4 Presence 5 Volume
  //    + HI/LO switch (6). RS: Gain->Gain, Bass/Mid/Treble->stack, Pres->Presence.
  P.mryems = { w:1700, h:560, ptr:rgb(238,233,214), tick:rgb(120,112,84),
    knobs:[
      {id:4,cx:.395,cy:.770,r:.020,style:'cream'},  // PRESENCE
      {id:1,cx:.460,cy:.770,r:.020,style:'cream'},  // BASS
      {id:2,cx:.525,cy:.770,r:.020,style:'cream'},  // MIDDLE
      {id:3,cx:.590,cy:.770,r:.020,style:'cream'},  // TREBLE
      {id:5,cx:.655,cy:.770,r:.020,style:'cream'},  // VOLUME (master)
      {id:0,cx:.720,cy:.770,r:.020,style:'cream'} ],// GAIN (preamp)
    sw3:[{id:6,cx:.885,cy:.770,hw:14,hh:16,two:true,hidden:true}],  // HI/LO gain switch
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,230,224), inkF='rgba(232,230,224,0.6)', chr=rgb(198,202,208), gold=rgb(232,214,150);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,24,26)); bgr.addColorStop(1,rgb(12,12,14));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.02)';
      for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(232,234,238)); g.addColorStop(1,rgb(120,124,130));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.44*W,hx1=.56*W,hcy=H*.04,hh=H*.028;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(22,22,24); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.09; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(9,9,10); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── silver perforated grille (upper) with gold piping + big "Y" + Legacy ──
      const gy=H*.085, gh=H*.46, gx=W*.05, gw=W*.90;
      rr(c,gx-W*.006,gy-H*.012,gw+W*.012,gh+H*.024,7*s); c.strokeStyle=gold; c.lineWidth=2.4*s; c.stroke();  // gold piping
      const sg=c.createLinearGradient(0,gy,0,gy+gh); sg.addColorStop(0,rgb(176,178,184)); sg.addColorStop(1,rgb(120,122,128));
      rr(c,gx,gy,gw,gh,6*s); c.fillStyle=sg; c.fill();
      c.save(); rr(c,gx,gy,gw,gh,6*s); c.clip(); c.fillStyle='rgba(30,30,34,0.55)';
      for(let yy=gy+4*s;yy<gy+gh;yy+=5*s){ for(let xx=gx+4*s;xx<gx+gw;xx+=5*s){ c.beginPath(); c.arc(xx,yy,1.1*s,0,7); c.fill(); } }
      c.restore(); rr(c,gx,gy,gw,gh,6*s); c.strokeStyle=rgb(232,233,236); c.lineWidth=2*s; c.stroke();
      c.save(); c.textAlign='center'; c.textBaseline='middle';
      setFont(d,F.anton,150); c.fillStyle=gold; c.lineWidth=3*s; c.strokeStyle='rgba(40,36,20,0.6)';
      c.strokeText('Y',W*.5,gy+gh*.5); c.fillText('Y',W*.5,gy+gh*.5); c.restore();
      const lbw=W*.10, lbh=gh*.22, lbx=gx+gw-lbw-W*.02, lby=gy+gh*.06;
      rr(c,lbx,lby,lbw,lbh,3*s); c.fillStyle=rgb(248,247,243); c.fill(); rr(c,lbx,lby,lbw,lbh,3*s); c.strokeStyle=rgb(40,40,44); c.lineWidth=1.2*s; c.stroke();
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.crete,16); c.fillStyle=rgb(30,30,34);
      c.fillText('Legacy',lbx+lbw*.5,lby+lbh*.40); c.restore();
      textSpaced(d,lbx+lbw*.5,lby+lbh*.74,F.barlow,7,rgb(90,90,96),'SERIES',0.12);
      // ── black control panel (lower) w/ gold piping ──
      const py=H*.595, ph=H*.30, px=W*.035, pw=W*.93, ly=py+ph*.86;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(26,27,29)); pg.addColorStop(1,rgb(13,14,16));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=gold; c.lineWidth=1.4*s; c.stroke();
      const cy=py+ph*.45;
      // jewel + POWER / STANDBY (left) + gold "EMS"
      const jcx=.045*W; const jg=c.createRadialGradient(jcx-2*s,cy-2*s,1*s,jcx,cy,6*s); jg.addColorStop(0,rgb(255,150,90)); jg.addColorStop(1,rgb(200,70,20));
      c.beginPath(); c.arc(jcx,cy,5.5*s,0,7); c.fillStyle=jg; c.fill(); c.strokeStyle=chr; c.lineWidth=1*s; c.beginPath(); c.arc(jcx,cy,5.5*s,0,7); c.stroke();
      batToggle(d,.085*W,cy,8*s,true); textSpaced(d,.085*W,py+ph*.16,F.barlow,6,ink,'POWER',0.02);
      batToggle(d,.125*W,cy,8*s,true); textSpaced(d,.125*W,py+ph*.16,F.barlow,6,ink,'STANDBY',0.0);
      c.save(); c.textAlign='left'; c.textBaseline='middle'; setFont(d,F.anton,30); c.fillStyle=gold; c.fillText('EMS',.165*W,cy); c.restore();
      // TOUCH jack (decorative)
      const tj=.300*W; c.beginPath(); c.arc(tj,cy,5.5*s,0,7); c.fillStyle=rgb(14,13,14); c.fill(); c.strokeStyle=chr; c.lineWidth=1.4*s; c.stroke();
      textSpaced(d,tj,py+ph*.16,F.barlow,6,inkF,'TOUCH',0.0);
      // knob labels
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,ly,F.barlow,sz||6.5,ink,t,0.03);
      lbl(.395,'PRESENCE',5.5); lbl(.460,'BASS'); lbl(.525,'MIDDLE',6); lbl(.590,'TREBLE',6); lbl(.655,'VOLUME',6); lbl(.720,'GAIN');
      // INPUT jack + HI/LO toggle (right)
      const ij=.820*W; c.beginPath(); c.arc(ij,cy,6.5*s,0,7); c.fillStyle=rgb(14,13,14); c.fill(); c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke();
      c.beginPath(); c.arc(ij,cy,2.2*s,0,7); c.fillStyle=rgb(52,50,54); c.fill();
      textSpaced(d,ij,py+ph*.16,F.barlow,6.5,ink,'INPUT',0.02);
      const hi=(vals&&vals[6]!=null)?vals[6]<0.5:true;  // up = HI, down = LO
      batToggle(d,.885*W,cy,8*s,hi);
      textSpaced(d,.885*W,py+ph*.16,F.barlow,7,gold,'HI',0.04); textSpaced(d,.885*W,py+ph*.86,F.barlow,7,inkF,'LO',0.04); } };

  // ── LOVOLT DR504 (Hiwatt DR504 "Custom 50") — black tolex head, white piping,
  //    "LOVOLT" badge plate, bottom control panel. 7 knobs: 0 NormalVol 1 BrightVol
  //    2 Bass 3 Treble 4 Middle 5 Presence 6 Master + clickable jumper input (7).
  //    RS: Gain->Brilliant Vol, Bass/Mid/Treble->stack, Pres->Presence.
  P.lovoltdr504 = { w:1560, h:600, ptr:rgb(238,239,242),
    knobs:[
      {id:0,cx:.180,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // NORMAL VOL
      {id:1,cx:.250,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // BRILLIANT VOL
      {id:2,cx:.320,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // BASS
      {id:3,cx:.390,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // TREBLE
      {id:4,cx:.460,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // MIDDLE
      {id:5,cx:.530,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // PRESENCE
      {id:6,cx:.600,cy:.745,r:.019,style:'pointer',cap:[22,22,24]} ],// MASTER VOL
    // clickable input cable (id 7): Normal -> Both(jumpered) -> Brilliant.
    sw3:[{id:7,cx:.085,cy:.745,hw:46,hh:34,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,233,236), inkF='rgba(232,233,236,0.6)', chr=rgb(196,200,206);
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,23,25)); bgr.addColorStop(0.5,rgb(16,15,17)); bgr.addColorStop(1,rgb(9,8,10));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.016)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(228,230,234)); g.addColorStop(1,rgb(108,112,118));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.44*W,hx1=.56*W,hcy=H*.045,hh=H*.03;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(26,26,28); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // ── white piping (a frame + two horizontal lines, the Hiwatt look) ──
      c.strokeStyle=rgb(236,236,238); c.lineWidth=2.2*s;
      rr(c,W*.03,H*.11,W*.94,H*.44,8*s); c.stroke();
      c.beginPath(); c.moveTo(W*.03,H*.20); c.lineTo(W*.97,H*.20); c.stroke();
      c.beginPath(); c.moveTo(W*.03,H*.46); c.lineTo(W*.97,H*.46); c.stroke();
      // ── "LOVOLT" badge plate (black, white border, block letters) ──
      const bw=W*.30, bh=H*.13, bx=W*.5-bw/2, by=H*.27;
      rr(c,bx,by,bw,bh,5*s); c.fillStyle=rgb(12,12,13); c.fill();
      rr(c,bx+3*s,by+3*s,bw-6*s,bh-6*s,4*s); c.strokeStyle=rgb(236,236,238); c.lineWidth=2*s; c.stroke();
      bolt(bx+13*s,by+bh*.5,2.6*s); bolt(bx+bw-13*s,by+bh*.5,2.6*s);
      textSpaced(d,W*.5,by+bh*.54,F.anton,40,rgb(238,239,242),'LOVOLT',0.10);
      // ── corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.075; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        const gg=c.createLinearGradient(cxx,cyy,cxx+dx*k,cyy+dy*k); gg.addColorStop(0,rgb(226,228,232)); gg.addColorStop(1,rgb(96,100,106));
        c.fillStyle=gg; c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.4*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── black control panel (bottom strip) ──
      const py=H*.62, ph=H*.30, px=W*.04, pw=W*.92, lblY=py+ph*.80, cy=py+ph*.45;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(26,26,28)); pg.addColorStop(1,rgb(13,13,15));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2*s; c.stroke();
      // ── INPUTS: Normal (Hi/Lo) + Brilliant (Hi/Lo) + clickable jumper cable ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7*s,0,7); c.fillStyle=rgb(16,14,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.4*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      const nx=.060*W, bx2=.110*W, jHi=cy-12*s, jLo=cy+14*s;
      jack(nx,jHi); jack(nx,jLo); jack(bx2,jHi); jack(bx2,jLo);
      textSpaced(d,nx,py+ph*.12,F.barlow,7,ink,'NORMAL',0.02); textSpaced(d,bx2,py+ph*.12,F.barlow,7,ink,'BRIGHT',0.02);
      const inp=(vals&&vals[7]!=null)?vals[7]:0.5;
      const plug=(jx,jy)=>{ rr(c,jx-4.2*s,jy-5*s,8.4*s,6*s,2*s); c.fillStyle=rgb(36,34,32); c.fill();
        const cg=c.createLinearGradient(jx-4.2*s,jy,jx+4.2*s,jy); cg.addColorStop(0,rgb(188,192,198)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(188,192,198));
        rr(c,jx-4.2*s,jy+1*s,8.4*s,3.4*s,1.4*s); c.fillStyle=cg; c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx-6*s,jy+30*s, jx-20*s,jy+36*s, jx-26*s,py+ph-4*s);
        c.lineWidth=4.5*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const my=Math.max(y1,y2)+12*s;
        c.beginPath(); c.arc(x1,y1,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.arc(x2,y2,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo((x1+x2)/2,my,x2,y2);
        c.lineWidth=3.4*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      let mode;
      if (inp < 0.25)      { plug(nx,jHi); mode='NORMAL'; }
      else if (inp < 0.75) { jumper(nx,jLo,bx2,jHi); plug(nx,jHi); mode='JUMPERED'; }
      else                 { plug(bx2,jHi); mode='BRILLIANT'; }
      textSpaced(d,.085*W,py+ph*.95,F.barlow,6,rgb(150,150,156),mode,0.04);
      // ── knob labels ──
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,lblY,F.barlow,sz||8,ink,t,0.02);
      lbl(.180,'NORMAL VOL',6.5); lbl(.250,'BRIGHT VOL',6.5); lbl(.320,'BASS',8.5); lbl(.390,'TREBLE',8);
      lbl(.460,'MIDDLE',8); lbl(.530,'PRESENCE',7); lbl(.600,'MASTER VOL',6.5);
      textSpaced(d,.390*W,py+ph*.13,F.bebas,12,inkF,'CUSTOM LOVOLT 50',0.08);
      textSpaced(d,.390*W,py+ph*.95,F.barlow,5.5,inkF,'MADE IN THE CUSTOM WORKSHOPS',0.04);
      // ── right: red jewel + STANDBY / MAINS toggles ──
      const jcx=.760*W;
      c.beginPath(); c.arc(jcx,cy,8*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
      const jg=c.createRadialGradient(jcx-2*s,cy-2*s,1*s,jcx,cy,8*s); jg.addColorStop(0,rgb(255,120,90)); jg.addColorStop(0.5,rgb(220,40,30)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,cy,6*s,0,7); c.fillStyle=jg; c.fill();
      batToggle(d,.820*W,cy,8*s,true); batToggle(d,.870*W,cy,8*s,true);
      textSpaced(d,.820*W,py+ph*.16,F.barlow,7,ink,'STANDBY',0.02); textSpaced(d,.870*W,py+ph*.16,F.barlow,7.5,ink,'MAINS',0.03);
      textSpaced(d,.845*W,py+ph*.88,F.barlow,6.5,inkF,'ON',0.04); } };

  // ── LOVOLT DR103 (Hiwatt DR103 "Custom 100") — black tolex head, white piping,
  //    "LOVOLT" badge plate, bottom control panel. 7 knobs: 0 NormalVol 1 BrightVol
  //    2 Bass 3 Treble 4 Middle 5 Presence 6 Master + clickable jumper input (7).
  //    RS: Gain->Brilliant Vol, Bass/Mid/Treble->stack, Pres->Presence.
  P.lovoltdr103 = { w:1560, h:600, ptr:rgb(238,239,242),
    knobs:[
      {id:0,cx:.180,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // NORMAL VOL
      {id:1,cx:.250,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // BRILLIANT VOL
      {id:2,cx:.320,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // BASS
      {id:3,cx:.390,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // TREBLE
      {id:4,cx:.460,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // MIDDLE
      {id:5,cx:.530,cy:.745,r:.019,style:'pointer',cap:[22,22,24]},  // PRESENCE
      {id:6,cx:.600,cy:.745,r:.019,style:'pointer',cap:[22,22,24]} ],// MASTER VOL
    // clickable input cable (id 7): Normal -> Both(jumpered) -> Brilliant.
    sw3:[{id:7,cx:.085,cy:.745,hw:46,hh:34,hidden:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(232,233,236), inkF='rgba(232,233,236,0.6)', chr=rgb(196,200,206);
      // ── black tolex head ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(24,23,25)); bgr.addColorStop(0.5,rgb(16,15,17)); bgr.addColorStop(1,rgb(9,8,10));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1; c.strokeStyle='rgba(255,255,255,0.016)';
      for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(228,230,234)); g.addColorStop(1,rgb(108,112,118));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      const hx0=.44*W,hx1=.56*W,hcy=H*.045,hh=H*.03;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(26,26,28); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
      // ── white piping (a frame + two horizontal lines, the Hiwatt look) ──
      c.strokeStyle=rgb(236,236,238); c.lineWidth=2.2*s;
      rr(c,W*.03,H*.11,W*.94,H*.44,8*s); c.stroke();
      c.beginPath(); c.moveTo(W*.03,H*.20); c.lineTo(W*.97,H*.20); c.stroke();
      c.beginPath(); c.moveTo(W*.03,H*.46); c.lineTo(W*.97,H*.46); c.stroke();
      // ── "LOVOLT" badge plate (black, white border, block letters) ──
      const bw=W*.30, bh=H*.13, bx=W*.5-bw/2, by=H*.27;
      rr(c,bx,by,bw,bh,5*s); c.fillStyle=rgb(12,12,13); c.fill();
      rr(c,bx+3*s,by+3*s,bw-6*s,bh-6*s,4*s); c.strokeStyle=rgb(236,236,238); c.lineWidth=2*s; c.stroke();
      bolt(bx+13*s,by+bh*.5,2.6*s); bolt(bx+bw-13*s,by+bh*.5,2.6*s);
      textSpaced(d,W*.5,by+bh*.54,F.anton,40,rgb(238,239,242),'LOVOLT',0.10);
      // ── corner caps ──
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.075; c.beginPath();
        c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath();
        const gg=c.createLinearGradient(cxx,cyy,cxx+dx*k,cyy+dy*k); gg.addColorStop(0,rgb(226,228,232)); gg.addColorStop(1,rgb(96,100,106));
        c.fillStyle=gg; c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.4*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // ── black control panel (bottom strip) ──
      const py=H*.62, ph=H*.30, px=W*.04, pw=W*.92, lblY=py+ph*.80, cy=py+ph*.45;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(26,26,28)); pg.addColorStop(1,rgb(13,13,15));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2*s; c.stroke();
      // ── INPUTS: Normal (Hi/Lo) + Brilliant (Hi/Lo) + clickable jumper cable ──
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,7*s,0,7); c.fillStyle=rgb(16,14,16); c.fill();
        c.strokeStyle=chr; c.lineWidth=1.8*s; c.stroke(); c.beginPath(); c.arc(jx,jy,2.4*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      const nx=.060*W, bx2=.110*W, jHi=cy-12*s, jLo=cy+14*s;
      jack(nx,jHi); jack(nx,jLo); jack(bx2,jHi); jack(bx2,jLo);
      textSpaced(d,nx,py+ph*.12,F.barlow,7,ink,'NORMAL',0.02); textSpaced(d,bx2,py+ph*.12,F.barlow,7,ink,'BRIGHT',0.02);
      const inp=(vals&&vals[7]!=null)?vals[7]:0.5;
      const plug=(jx,jy)=>{ rr(c,jx-4.2*s,jy-5*s,8.4*s,6*s,2*s); c.fillStyle=rgb(36,34,32); c.fill();
        const cg=c.createLinearGradient(jx-4.2*s,jy,jx+4.2*s,jy); cg.addColorStop(0,rgb(188,192,198)); cg.addColorStop(0.5,rgb(120,124,130)); cg.addColorStop(1,rgb(188,192,198));
        rr(c,jx-4.2*s,jy+1*s,8.4*s,3.4*s,1.4*s); c.fillStyle=cg; c.fill();
        c.beginPath(); c.moveTo(jx,jy+6*s); c.bezierCurveTo(jx-6*s,jy+30*s, jx-20*s,jy+36*s, jx-26*s,py+ph-4*s);
        c.lineWidth=4.5*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      const jumper=(x1,y1,x2,y2)=>{ const my=Math.max(y1,y2)+12*s;
        c.beginPath(); c.arc(x1,y1,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.arc(x2,y2,3*s,0,7); c.fillStyle=rgb(36,34,32); c.fill();
        c.beginPath(); c.moveTo(x1,y1); c.quadraticCurveTo((x1+x2)/2,my,x2,y2);
        c.lineWidth=3.4*s; c.lineCap='round'; c.strokeStyle=rgb(18,18,20); c.stroke(); c.lineCap='butt'; };
      let mode;
      if (inp < 0.25)      { plug(nx,jHi); mode='NORMAL'; }
      else if (inp < 0.75) { jumper(nx,jLo,bx2,jHi); plug(nx,jHi); mode='JUMPERED'; }
      else                 { plug(bx2,jHi); mode='BRILLIANT'; }
      textSpaced(d,.085*W,py+ph*.95,F.barlow,6,rgb(150,150,156),mode,0.04);
      // ── knob labels ──
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,lblY,F.barlow,sz||8,ink,t,0.02);
      lbl(.180,'NORMAL VOL',6.5); lbl(.250,'BRIGHT VOL',6.5); lbl(.320,'BASS',8.5); lbl(.390,'TREBLE',8);
      lbl(.460,'MIDDLE',8); lbl(.530,'PRESENCE',7); lbl(.600,'MASTER VOL',6.5);
      textSpaced(d,.390*W,py+ph*.13,F.bebas,12,inkF,'CUSTOM LOVOLT 100',0.08);
      textSpaced(d,.390*W,py+ph*.95,F.barlow,5.5,inkF,'MADE IN THE CUSTOM WORKSHOPS',0.04);
      // ── right: red jewel + STANDBY / MAINS toggles ──
      const jcx=.760*W;
      c.beginPath(); c.arc(jcx,cy,8*s,0,7); c.fillStyle=rgb(40,8,6); c.fill();
      const jg=c.createRadialGradient(jcx-2*s,cy-2*s,1*s,jcx,cy,8*s); jg.addColorStop(0,rgb(255,120,90)); jg.addColorStop(0.5,rgb(220,40,30)); jg.addColorStop(1,rgb(108,12,10));
      c.beginPath(); c.arc(jcx,cy,6*s,0,7); c.fillStyle=jg; c.fill();
      batToggle(d,.820*W,cy,8*s,true); batToggle(d,.870*W,cy,8*s,true);
      textSpaced(d,.820*W,py+ph*.16,F.barlow,7,ink,'STANDBY',0.02); textSpaced(d,.870*W,py+ph*.16,F.barlow,7.5,ink,'MAINS',0.03);
      textSpaced(d,.845*W,py+ph*.88,F.barlow,6.5,inkF,'ON',0.04); } };

  // ── SAMPLEG VH-140C (Ampeg VH-140C) — black solid-state head, "Sampleg" silver
  //    script, wide black control panel. Knobs: 0 Channel(toggle) 1 BGain 2 BLow
  //    3 BMid 4 BHigh 5 BLevel 6 AGain 7 ALow 8 AUltraMid 9 AHigh 10 ALevel
  //    11 ReverbB 12 ReverbA 13 Rate 14 DepthB 15 DepthA. RS: Gain->BGain.
  P.samplegvh140c = { w:1900, h:600, ptr:rgb(236,237,240),
    knobs:[
      {id:13,cx:.085,cy:.760,r:.0135,style:"pointer",cap:[22,22,24]},  // Chorus Rate
      {id:14,cx:.120,cy:.760,r:.0135,style:"pointer",cap:[22,22,24]},  // Depth B
      {id:15,cx:.155,cy:.760,r:.0135,style:"pointer",cap:[22,22,24]},  // Depth A
      {id:11,cx:.265,cy:.760,r:.0135,style:"pointer",cap:[22,22,24]},  // Reverb B
      {id:12,cx:.300,cy:.760,r:.0135,style:"pointer",cap:[22,22,24]},  // Reverb A
      {id:5, cx:.375,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // B Level
      {id:4, cx:.420,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // B High
      {id:3, cx:.465,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // B Mid
      {id:2, cx:.510,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // B Low
      {id:1, cx:.555,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // B Gain
      {id:10,cx:.630,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // A Level
      {id:9, cx:.675,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // A High
      {id:8, cx:.720,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // A Ultra Mid
      {id:7, cx:.765,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]},  // A Low
      {id:6, cx:.810,cy:.760,r:.0145,style:"pointer",cap:[22,22,24]} ],// A Gain
    // channel select (id 0): A <-> B (footswitchable on the real amp)
    sw3:[{id:0,cx:.592,cy:.760,hw:10,hh:18,two:true}],
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(228,229,232), inkF="rgba(228,229,232,0.55)", chr=rgb(196,200,206);
      const chB=(vals&&vals[0]!=null)?vals[0]>0.5:true;
      // black tolex head
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(26,25,27)); bgr.addColorStop(0.5,rgb(17,16,18)); bgr.addColorStop(1,rgb(9,8,10));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(228,230,234)); g.addColorStop(1,rgb(108,112,118));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(48,50,54); c.lineWidth=0.7*s; c.stroke(); };
      // top strap + cream piping frame
      const hx0=.45*W,hx1=.55*W,hcy=H*.04,hh=H*.026;
      rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(26,26,28); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
      rr(c,W*.025,H*.10,W*.95,H*.40,8*s); c.strokeStyle=rgb(206,198,170); c.lineWidth=2*s; c.stroke();
      // black grille with salt-pepper + "Sampleg" silver script
      const gy=H*.13,gh=H*.34,gx=W*.045,gw=W*.91;
      rr(c,gx,gy,gw,gh,5*s); c.fillStyle=rgb(15,15,16); c.fill();
      c.save(); rr(c,gx,gy,gw,gh,5*s); c.clip();
      for(let yy=gy;yy<gy+gh;yy+=3*s){ for(let xx=gx;xx<gx+gw;xx+=3*s){ const n=((xx*13+yy*7)%17); if(n<3){ c.fillStyle="rgba(210,212,216,0.10)"; c.fillRect(xx,yy,1.4*s,1.4*s); } } }
      c.restore();
      c.save(); c.textAlign="center"; c.textBaseline="middle"; setFont(d,F.ink,72);
      c.lineWidth=4*s; c.strokeStyle="rgba(0,0,0,0.4)"; c.strokeText("Sampleg",W*.5,gy+gh*.5);
      const lg=c.createLinearGradient(0,gy+gh*.3,0,gy+gh*.7); lg.addColorStop(0,rgb(238,240,244)); lg.addColorStop(1,rgb(150,154,160));
      c.fillStyle=lg; c.fillText("Sampleg",W*.5,gy+gh*.5); c.restore();
      // corner caps
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.07; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(8,8,9); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.4*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // black control panel
      const py=H*.58,ph=H*.34,px=W*.03,pw=W*.94,lblY=py+ph*.80,cy=py+ph*.45;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,rgb(30,30,33)); pg.addColorStop(1,rgb(15,15,17));
      rr(c,px,py,pw,ph,5*s); c.fillStyle=pg; c.fill(); rr(c,px,py,pw,ph,5*s); c.strokeStyle=rgb(70,72,76); c.lineWidth=1.2*s; c.stroke();
      const lbl=(cx2,t,sz)=>textSpaced(d,cx2*W,lblY,F.barlow,sz||6.5,ink,t,0.02);
      const hdr=(cx2,t)=>textSpaced(d,cx2*W,py+ph*.10,F.barlow,7.5,inkF,t,0.05);
      const brk=(x0,x1,t)=>{ const y=py+ph*.95; c.strokeStyle=inkF; c.lineWidth=1*s; c.beginPath();
        c.moveTo(x0*W,y-4*s); c.lineTo(x0*W,y); c.lineTo(x1*W,y); c.lineTo(x1*W,y-4*s); c.stroke();
        textSpaced(d,(x0+x1)/2*W,y+6*s,F.barlow,6.5,inkF,t,0.04); };
      const jack=(jx,jy)=>{ c.beginPath(); c.arc(jx,jy,6*s,0,7); c.fillStyle=rgb(16,14,16); c.fill(); c.strokeStyle=chr; c.lineWidth=1.6*s; c.stroke();
        c.beginPath(); c.arc(jx,jy,2*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
      // power + model
      batToggle(d,.030*W,cy,8*s,true); textSpaced(d,.030*W,py+ph*.12,F.barlow,6.5,ink,"POWER",0.02);
      textSpaced(d,.030*W,lblY,F.bebas,9,inkF,"VH-140C",0.04);
      // chorus
      hdr(.120,"CHORUS"); lbl(.085,"RATE"); lbl(.120,"DEPTH B"); lbl(.155,"DEPTH A");
      // fx loop
      jack(.205*W,cy-8*s); jack(.205*W,cy+12*s); hdr(.205,"FX LOOP");
      textSpaced(d,.232*W,cy-8*s,F.barlow,5.5,inkF,"IN",0); textSpaced(d,.232*W,cy+12*s,F.barlow,5.5,inkF,"OUT",0);
      // reverb
      hdr(.283,"REVERB"); lbl(.265,"REV B"); lbl(.300,"REV A");
      // channel B
      hdr(.465,"CHANNEL B"); lbl(.375,"LEVEL"); lbl(.420,"HIGH"); lbl(.465,"MID"); lbl(.510,"LOW"); lbl(.555,"GAIN");
      brk(.358,.572,chB?"● LEAD":"LEAD");
      // channel select
      textSpaced(d,.592*W,py+ph*.12,F.barlow,6.5,ink,"CH",0.03);
      textSpaced(d,.592*W,lblY,F.barlow,6,inkF,chB?"B":"A",0.0);
      // channel A
      hdr(.720,"CHANNEL A"); lbl(.630,"LEVEL"); lbl(.675,"HIGH"); lbl(.720,"U.MID"); lbl(.765,"LOW"); lbl(.810,"GAIN");
      brk(.613,.827,(!chB)?"● CLEAN":"CLEAN");
      // input
      jack(.862*W,cy-8*s); jack(.862*W,cy+12*s); hdr(.880,"INPUT");
      textSpaced(d,.888*W,cy-8*s,F.barlow,5.5,inkF,"HI",0); textSpaced(d,.888*W,cy+12*s,F.barlow,5.5,inkF,"LO",0); } };
  // ── CITRUS OR100 (Orange OR100 'Graphic' head) — orange tolex, white logo
  //    panel ("Citrus" bubble logo + OR100 + crest), recessed black power panel
  //    (FULL/HALF + jewel) + orange control panel with black skirted knobs.
  //    Knobs: 0 Gain 1 Bass 2 Middle 3 Treble 4 Depth 5 Volume + FULL/HALF (6).
  // ── CITRUS OR50 / OR100 (Orange OR50 & OR100 vintage "Graphic" heads) ──
  //    Both heads are the SAME single-channel panel (only the power section /
  //    badge differ), so they share ONE draw routine (drawCitrusOR) + ONE knob
  //    layout (OR_CTRL) -> the two faces are pixel-identical except "OR50"/"OR100".
  //    Faithful to the real Orange "pics-only" graphics: orange basket-weave
  //    tolex, white logo panel ("Citrus" bubble script + model + colour crest),
  //    black power sub-panel (FULL/HALF lever + Power/Standby + jewel), three
  //    orange control sub-panels grouped exactly like the photo —
  //    [Volume·Gain] [Treble·Middle·Bass] [Depth] — with the Orange hieroglyph
  //    icons drawn as vector glyphs above each black skirted knob, then a black
  //    INPUT jack panel. Knob ids: 0 Gain 1 Bass 2 Middle 3 Treble 4 Depth
  //    5 Volume + FULL/HALF (6). RS: Gain->GAIN, Bass/Mid/Treble->tone stack.
  const OR_CTRL = [
    { id:5, lbl:'VOLUME', icon:'speaker',  cx:.300, big:true  },
    { id:0, lbl:'GAIN',   icon:'hand',     cx:.392, big:true  },
    { id:3, lbl:'TREBLE', icon:'trebleUp', cx:.508, big:false },
    { id:2, lbl:'MIDDLE', icon:'middle',   cx:.567, big:false },
    { id:1, lbl:'BASS',   icon:'bassDown', cx:.626, big:false },
    { id:4, lbl:'DEPTH',  icon:'shape',    cx:.738, big:true  },
  ];
  const OR_KNOBS = OR_CTRL.map(k => ({ id:k.id, cx:k.cx, cy:.694, r:k.big?.029:.018, style:'pointer', cap:[20,20,22] }));
  const OR_SW3 = [{ id:6, cx:.072, cy:.694, hw:9, hh:16, two:true }];   // FULL / HALF power lever

  // The Orange front-panel "pics-only" hieroglyphs, drawn as crisp vector glyphs
  // (cx,cy = centre px, sz = icon half-extent px). Shared by both heads.
  function orIcon(d, cx, cy, sz, type, col){
    const c=d.ctx, s=d.s; col=col||rgb(20,18,16);
    c.save(); c.strokeStyle=col; c.fillStyle=col; c.lineCap='round'; c.lineJoin='round';
    const lw=(m)=>{ c.lineWidth=Math.max(1.0*s, sz*m); };
    if (type==='speaker'){
      lw(0.0);
      c.beginPath();
      c.moveTo(cx-sz*0.95, cy-sz*0.30); c.lineTo(cx-sz*0.40, cy-sz*0.30);
      c.lineTo(cx+sz*0.05, cy-sz*0.72); c.lineTo(cx+sz*0.05, cy+sz*0.72);
      c.lineTo(cx-sz*0.40, cy+sz*0.30); c.lineTo(cx-sz*0.95, cy+sz*0.30); c.closePath(); c.fill();
      lw(0.13);
      for(let i=0;i<3;i++){ c.beginPath(); c.arc(cx+sz*0.12, cy, sz*(0.42+i*0.30), -0.85, 0.85); c.stroke(); }
    } else if (type==='hand'){
      // open hand (Orange "HF drive" / gain glyph): palm + 4 fingers + thumb
      rr(c, cx-sz*0.55, cy-sz*0.05, sz*1.05, sz*0.80, sz*0.22); c.fill();
      for(let i=0;i<4;i++){ rr(c, cx-sz*0.50+i*sz*0.31, cy-sz*0.80, sz*0.20, sz*0.86, sz*0.10); c.fill(); }
      c.save(); c.translate(cx-sz*0.52, cy+sz*0.18); c.rotate(-0.72);
      rr(c, -sz*0.10, -sz*0.52, sz*0.20, sz*0.62, sz*0.10); c.fill(); c.restore();
    } else if (type==='trebleUp' || type==='bassDown'){
      const up = (type==='trebleUp');
      // musical note (head + stem)
      c.beginPath(); c.ellipse(cx-sz*0.42, cy+sz*0.38, sz*0.30, sz*0.21, -0.5, 0, 7); c.fill();
      lw(0.13); c.beginPath(); c.moveTo(cx-sz*0.13, cy+sz*0.42); c.lineTo(cx-sz*0.13, cy-sz*0.55); c.stroke();
      // up/down arrow
      lw(0.15); const ax=cx+sz*0.52, t=cy-sz*0.66, b=cy+sz*0.66;
      c.beginPath(); c.moveTo(ax, b); c.lineTo(ax, t); c.stroke();
      const ty=up?t:b, hd=up?1:-1;
      c.beginPath(); c.moveTo(ax, ty); c.lineTo(ax-sz*0.22, ty+hd*sz*0.32);
      c.moveTo(ax, ty); c.lineTo(ax+sz*0.22, ty+hd*sz*0.32); c.stroke();
    } else if (type==='middle'){
      // arrows pointing inward to a centre circle:  ->  o  <-
      lw(0.12); c.beginPath(); c.arc(cx, cy, sz*0.26, 0, 7); c.stroke();
      c.beginPath(); c.moveTo(cx-sz*0.95, cy); c.lineTo(cx-sz*0.46, cy);
      c.moveTo(cx-sz*0.46, cy); c.lineTo(cx-sz*0.70, cy-sz*0.20);
      c.moveTo(cx-sz*0.46, cy); c.lineTo(cx-sz*0.70, cy+sz*0.20); c.stroke();
      c.beginPath(); c.moveTo(cx+sz*0.95, cy); c.lineTo(cx+sz*0.46, cy);
      c.moveTo(cx+sz*0.46, cy); c.lineTo(cx+sz*0.70, cy-sz*0.20);
      c.moveTo(cx+sz*0.46, cy); c.lineTo(cx+sz*0.70, cy+sz*0.20); c.stroke();
    } else if (type==='shape'){
      // squiggly waveform  ~~~  (the Orange "shape"/depth glyph)
      lw(0.17);
      c.beginPath(); c.moveTo(cx-sz*0.95, cy);
      c.quadraticCurveTo(cx-sz*0.62, cy-sz*0.80, cx-sz*0.28, cy);
      c.quadraticCurveTo(cx+sz*0.06, cy+sz*0.80, cx+sz*0.40, cy);
      c.quadraticCurveTo(cx+sz*0.68, cy-sz*0.62, cx+sz*0.95, cy); c.stroke();
    } else if (type==='power'){
      // power symbol (circle with top gap + bar)
      lw(0.16);
      c.beginPath(); c.arc(cx, cy, sz*0.62, -Math.PI/2+0.5, -Math.PI/2-0.5+2*Math.PI); c.stroke();
      c.beginPath(); c.moveTo(cx, cy-sz*0.80); c.lineTo(cx, cy-sz*0.05); c.stroke();
    } else if (type==='bolt'){
      lw(0.0);
      c.beginPath();
      c.moveTo(cx+sz*0.28, cy-sz*0.82); c.lineTo(cx-sz*0.38, cy+sz*0.06);
      c.lineTo(cx+sz*0.02, cy+sz*0.06); c.lineTo(cx-sz*0.26, cy+sz*0.86);
      c.lineTo(cx+sz*0.44, cy-sz*0.16); c.lineTo(cx+sz*0.02, cy-sz*0.16); c.closePath(); c.fill();
    } else if (type==='bright'){
      lw(0.16);
      for(let i=0;i<3;i++){ c.beginPath(); c.arc(cx-sz*0.4, cy, sz*(0.36+i*0.30), -0.8, 0.8); c.stroke(); }
    }
    c.restore();
  }

  // Orange's colourful coat-of-arms crest badge (a small shield + crown).
  function orCrest(d, x, y, w, h){
    const c=d.ctx, s=d.s, ink=rgb(22,20,18);
    rr(c, x, y, w, h, 4*s); c.fillStyle=rgb(250,249,245); c.fill();
    rr(c, x, y, w, h, 4*s); c.strokeStyle=ink; c.lineWidth=1.2*s; c.stroke();
    const sx=x+w*0.5, sw=w*0.54, top=y+h*0.30, sh=h*0.50;
    const shield=()=>{ c.beginPath();
      c.moveTo(sx-sw/2, top); c.lineTo(sx+sw/2, top); c.lineTo(sx+sw/2, top+sh*0.5);
      c.quadraticCurveTo(sx+sw/2, top+sh*0.95, sx, top+sh);
      c.quadraticCurveTo(sx-sw/2, top+sh*0.95, sx-sw/2, top+sh*0.5); c.closePath(); };
    c.save(); shield(); c.clip();
    const cols=[rgb(220,58,46),rgb(64,158,88),rgb(58,116,206),rgb(236,168,38)];
    c.fillStyle=cols[0]; c.fillRect(sx-sw/2, top, sw/2, sh*0.5);
    c.fillStyle=cols[1]; c.fillRect(sx, top, sw/2, sh*0.5);
    c.fillStyle=cols[2]; c.fillRect(sx-sw/2, top+sh*0.5, sw/2, sh*0.6);
    c.fillStyle=cols[3]; c.fillRect(sx, top+sh*0.5, sw/2, sh*0.6);
    c.restore();
    shield(); c.strokeStyle=ink; c.lineWidth=1.1*s; c.stroke();
    // crown above the shield
    c.beginPath();
    c.moveTo(sx-sw*0.42, top); c.lineTo(sx-sw*0.42, top-h*0.11); c.lineTo(sx-sw*0.20, top-h*0.02);
    c.lineTo(sx, top-h*0.14); c.lineTo(sx+sw*0.20, top-h*0.02); c.lineTo(sx+sw*0.42, top-h*0.11);
    c.lineTo(sx+sw*0.42, top); c.closePath();
    c.fillStyle=rgb(236,168,38); c.fill(); c.strokeStyle=ink; c.lineWidth=0.8*s; c.stroke();
    textC(d, x+w*0.5, y+h*0.93, F.barlow, 5.5, 'rgba(22,20,18,0.6)', 'CITRUS', 'center');
  }

  // The shared Orange head face. `model` = the badge text ("OR50" / "OR100").
  function drawCitrusOR(d, vals, model){
    const {ctx:c,W,H,s}=d; vals=vals||{};
    const ink=rgb(22,20,18), cream=rgb(246,244,238), goldT=rgb(224,200,160);
    // ── orange basket-weave tolex ──
    const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(246,132,36)); bgr.addColorStop(0.5,rgb(236,118,24)); bgr.addColorStop(1,rgb(202,94,12));
    c.fillStyle=bgr; c.fillRect(0,0,W,H);
    c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
    c.strokeStyle='rgba(0,0,0,0.06)'; for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
    c.strokeStyle='rgba(255,255,255,0.07)'; for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x+3*s,0); c.lineTo(x+3*s-H,H); c.stroke(); } c.restore();
    const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
      g.addColorStop(0,rgb(236,238,242)); g.addColorStop(1,rgb(120,124,130));
      c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
    // top handle + corner caps
    const hx0=.44*W,hx1=.56*W,hcy=H*.045,hh=H*.028;
    rr(c,hx0,hcy-hh,hx1-hx0,2*hh,hh); c.fillStyle=rgb(22,20,20); c.fill(); c.strokeStyle=rgb(8,8,10); c.lineWidth=1.2*s; c.stroke();
    [hx0,hx1].forEach(bx=>bolt(bx,hcy,3*s));
    const corner=(cxx,cyy,dx,dy)=>{ const k=H*.085; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
      c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(16,14,14); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
    corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
    // ── white logo panel: "Citrus" bubble logo + model + crest ──
    rr(c,.035*W,.085*H,.93*W,.30*H,7*s); c.fillStyle=cream; c.fill();
    rr(c,.035*W,.085*H,.93*W,.30*H,7*s); c.strokeStyle=rgb(150,146,136); c.lineWidth=1.6*s; c.stroke();
    textC(d,.062*W,.248*H,F.graffiti,82,ink,'Citrus','left');
    textC(d,.575*W,.248*H,F.barlow,32,ink,model,'left');
    orCrest(d,.858*W,.118*H,.082*W,.232*H);
    // ── control area geometry ──
    const py=.45*H, ph=.47*H, ky=.694*H, iy=py+ph*.135, ly=py+ph*.86;
    const blackPanel=(x0,x1)=>{ const w=(x1-x0)*W; rr(c,x0*W,py,w,ph,6*s); c.fillStyle=rgb(17,15,15); c.fill();
      rr(c,x0*W,py,w,ph,6*s); c.strokeStyle=rgb(64,60,54); c.lineWidth=1.4*s; c.stroke(); };
    const orangePanel=(x0,x1)=>{ const w=(x1-x0)*W; const g=c.createLinearGradient(0,py,0,py+ph);
      g.addColorStop(0,rgb(245,128,30)); g.addColorStop(1,rgb(212,102,16));
      rr(c,x0*W,py,w,ph,6*s); c.fillStyle=g; c.fill();
      rr(c,x0*W,py,w,ph,6*s); c.strokeStyle=rgb(150,72,12); c.lineWidth=1.4*s; c.stroke(); };
    const plbl=(cx,y,t,sz)=>textC(d,cx*W,y,F.barlow,sz||6.5,goldT,t,'center');
    // 1) black POWER panel (FULL/HALF lever id6 rendered by the engine at .072)
    blackPanel(.035,.200);
    orIcon(d,.085*W, py+ph*.05, 6.5*s, 'power', goldT); orIcon(d,.160*W, py+ph*.05, 6.5*s, 'bolt', goldT);
    plbl(.072, iy, 'FULL'); plbl(.072, ly, 'HALF');
    batToggle(d,.112*W,ky,8*s,true); plbl(.112, iy, 'POWER');
    batToggle(d,.150*W,ky,8*s,true); plbl(.150, iy, 'STBY', 6);
    const jx=.186*W; c.beginPath(); c.arc(jx,ky,7*s,0,7); c.fillStyle=rgb(60,12,8); c.fill();
    const jg=c.createRadialGradient(jx-2*s,ky-2*s,1*s,jx,ky,7*s); jg.addColorStop(0,rgb(255,170,90)); jg.addColorStop(1,rgb(190,70,20));
    c.beginPath(); c.arc(jx,ky,5*s,0,7); c.fillStyle=jg; c.fill();
    // 2) three orange control sub-panels, grouped like the photo
    orangePanel(.214,.452);   // Volume · Gain
    orangePanel(.465,.668);   // Treble · Middle · Bass
    orangePanel(.681,.800);   // Depth
    OR_CTRL.forEach(k=>{ const sz=(k.big?11.5:9)*s;
      orIcon(d, k.cx*W, iy, sz, k.icon, ink);
      textC(d, k.cx*W, ly, F.barlow, k.big?9:7.5, ink, k.lbl, 'center'); });
    // 3) black INPUT panel (HF + normal jacks)
    blackPanel(.812,.965);
    plbl(.888, iy, 'INPUT', 7);
    const inJack=(x,y)=>{ c.beginPath(); c.arc(x,y,8.5*s,0,7); c.fillStyle=rgb(14,12,12); c.fill();
      c.strokeStyle=rgb(196,200,206); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(x,y,3*s,0,7); c.fillStyle=rgb(54,52,56); c.fill(); };
    inJack(.890*W, py+ph*.46); inJack(.890*W, py+ph*.74);
    orIcon(d,.852*W, py+ph*.46, 5.5*s, 'bright', goldT);
  }

  P.citrusor100 = { w:1500, h:600, ptr:rgb(244,245,248), tick:rgb(120,116,104),
    knobs:OR_KNOBS, sw3:OR_SW3, draw(d,vals){ drawCitrusOR(d, vals, 'OR100'); } };
  P.citrusor50  = { w:1500, h:600, ptr:rgb(244,245,248), tick:rgb(120,116,104),
    knobs:OR_KNOBS, sw3:OR_SW3, draw(d,vals){ drawCitrusOR(d, vals, 'OR50'); } };

  // ── CITRUS AD50 (Orange AD50 Custom Shop) — orange tolex Citrus head, white logo
  //    panel + crest. Tube 50W AB / 30W A. 0 Gain 1 Bass 2 Treble 3 Presence
  //    4 Master + Sustain(5) + Class A(6). RS Gain -> Gain, Bass/Treble -> EQ, Pres.
  P.citrusad50 = { w:1500, h:560, ptr:rgb(244,245,248), tick:rgb(120,116,104),
    knobs:[
      {id:0,cx:.330,cy:.735,r:.026,style:'pointer',cap:[20,20,22]},  // GAIN (big)
      {id:1,cx:.430,cy:.735,r:.0175,style:'pointer',cap:[20,20,22]}, // BASS
      {id:2,cx:.495,cy:.735,r:.0175,style:'pointer',cap:[20,20,22]}, // TREBLE
      {id:3,cx:.560,cy:.735,r:.0175,style:'pointer',cap:[20,20,22]}, // PRESENCE
      {id:4,cx:.660,cy:.735,r:.026,style:'pointer',cap:[20,20,22]} ],// MASTER (big)
    sw3:[
      {id:5,cx:.760,cy:.735,hw:10,hh:17,two:true},   // SUSTAIN boost
      {id:6,cx:.130,cy:.735,hw:10,hh:17,two:true} ], // CLASS A / AB
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const ink=rgb(24,22,20), inkF='rgba(24,22,20,0.62)', cream=rgb(246,244,238);
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,rgb(246,132,36)); bgr.addColorStop(0.5,rgb(236,118,24)); bgr.addColorStop(1,rgb(202,94,12));
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(0,0,0,0.06)'; for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(255,255,255,0.07)'; for(let x=-H;x<W;x+=6*s){ c.beginPath(); c.moveTo(x+3*s,0); c.lineTo(x+3*s-H,H); c.stroke(); } c.restore();
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r);
        g.addColorStop(0,rgb(236,238,242)); g.addColorStop(1,rgb(120,124,130)); c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=0.7*s; c.stroke(); };
      const corner=(cxx,cyy,dx,dy)=>{ const k=H*.09; c.beginPath(); c.moveTo(cxx,cyy+dy*k); c.lineTo(cxx,cyy); c.lineTo(cxx+dx*k,cyy);
        c.quadraticCurveTo(cxx+dx*k*0.35,cyy+dy*k*0.35,cxx,cyy+dy*k); c.closePath(); c.fillStyle=rgb(16,14,14); c.fill(); bolt(cxx+dx*k*0.42,cyy+dy*k*0.42,2.6*s); };
      corner(0,0,1,1); corner(W,0,-1,1); corner(0,H,1,-1); corner(W,H,-1,-1);
      // white logo panel
      rr(c,.035*W,.085*H,.93*W,.31*H,7*s); c.fillStyle=cream; c.fill();
      rr(c,.035*W,.085*H,.93*W,.31*H,7*s); c.strokeStyle=rgb(150,146,136); c.lineWidth=1.6*s; c.stroke();
      textC(d,.062*W,.255*H,F.graffiti,84,ink,'Citrus','left');
      textC(d,.585*W,.255*H,F.barlow,34,ink,'AD50','left');
      orCrest(d,.860*W,.120*H,.082*W,.235*H);
      // control area
      const py=.46*H, ph=.46*H;
      const blackPanel=(x0,x1)=>{ const w=(x1-x0)*W; rr(c,x0*W,py,w,ph,6*s); c.fillStyle=rgb(17,15,15); c.fill(); rr(c,x0*W,py,w,ph,6*s); c.strokeStyle=rgb(64,60,54); c.lineWidth=1.4*s; c.stroke(); };
      const orangePanel=(x0,x1)=>{ const w=(x1-x0)*W; const g=c.createLinearGradient(0,py,0,py+ph); g.addColorStop(0,rgb(245,128,30)); g.addColorStop(1,rgb(212,102,16)); rr(c,x0*W,py,w,ph,6*s); c.fillStyle=g; c.fill(); rr(c,x0*W,py,w,ph,6*s); c.strokeStyle=rgb(150,72,12); c.lineWidth=1.4*s; c.stroke(); };
      const ly=py+ph*.86, iy=py+ph*.14;
      // power panel (left): jewel + POWER + CLASS A/AB switch (id6)
      blackPanel(.035,.215);
      const jx=.075*W, jcy=py+ph*.46; const jg=c.createRadialGradient(jx-2*s,jcy-2*s,1*s,jx,jcy,7*s); jg.addColorStop(0,rgb(255,170,90)); jg.addColorStop(1,rgb(190,70,20));
      c.beginPath(); c.arc(jx,jcy,6*s,0,7); c.fillStyle=jg; c.fill();
      textC(d,.075*W,iy,F.barlow,7,rgb(224,200,160),'POWER','center');
      textC(d,.130*W,iy,F.barlow,6.5,rgb(224,200,160),'CLASS A','center'); textC(d,.130*W,ly,F.barlow,6,rgb(224,200,160),'A / AB','center');
      // orange control panel
      orangePanel(.230,.820);
      const lbl=(cx2,t,sz)=>textC(d,cx2*W,ly,F.barlow,sz||8.5,ink,t,'center');
      lbl(.330,'GAIN'); lbl(.430,'BASS',7.5); lbl(.495,'TREBLE',7.5); lbl(.560,'PRESENCE',6.5); lbl(.660,'MASTER');
      textC(d,.760*W,iy,F.barlow,7,ink,'SUSTAIN','center'); textC(d,.760*W,ly,F.barlow,6,inkF,'BOOST','center');
      // input panel (right)
      blackPanel(.835,.965);
      c.beginPath(); c.arc(.900*W,jcy,9*s,0,7); c.fillStyle=rgb(16,14,14); c.fill(); c.strokeStyle=rgb(196,200,206); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(.900*W,jcy,3*s,0,7); c.fillStyle=rgb(54,52,56); c.fill();
      textC(d,.900*W,iy,F.barlow,7,rgb(224,200,160),'INPUT','center'); } };

  // ── CITRUS JIMMY BEAN (Orange Jimmy Bean JB150, 1975) — DENIM/LEATHER look:
  //    blue denim tolex + brown leather panel + brass nameplate. Solid-state
  //    twin-channel + tremolo + sustain. 0 Volume 1 Bass 2 Treble 3 Sustain
  //    4 Speed 5 Depth + Channel(6) + Bright(7). RS Gain -> Sustain.
  P.citrusjimmybean = { w:1560, h:560, ptr:rgb(238,230,206), tick:rgb(120,96,60),
    knobs:[
      {id:6,cx:.225,cy:.430,r:.020,style:'cream'},  // CHANNEL (rendered as knob-ish; also sw3 below) -- placeholder removed
      {id:0,cx:.300,cy:.430,r:.020,style:'cream'},  // VOLUME
      {id:1,cx:.375,cy:.430,r:.020,style:'cream'},  // BASS
      {id:2,cx:.450,cy:.430,r:.020,style:'cream'},  // TREBLE
      {id:3,cx:.545,cy:.430,r:.020,style:'cream'},  // SUSTAIN
      {id:4,cx:.640,cy:.430,r:.020,style:'cream'},  // SPEED
      {id:5,cx:.715,cy:.430,r:.020,style:'cream'} ],// DEPTH
    sw3:[
      {id:7,cx:.480,cy:.640,hw:9,hh:15,two:true} ], // BRIGHT
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const denim=rgb(58,86,128), denimD=rgb(40,62,98), leather=rgb(96,62,34), leatherD=rgb(70,44,24),
            brass=rgb(206,170,96), brassHi=rgb(232,202,134), ink=rgb(40,28,16), cream=rgb(238,230,206);
      // ── blue denim tolex (cross-hatch weave) ──
      const bgr=c.createLinearGradient(0,0,0,H); bgr.addColorStop(0,denim); bgr.addColorStop(1,denimD);
      c.fillStyle=bgr; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.lineWidth=1;
      c.strokeStyle='rgba(255,255,255,0.05)'; for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x,0); c.lineTo(x+H,H); c.stroke(); }
      c.strokeStyle='rgba(0,0,0,0.10)'; for(let x=-H;x<W;x+=5*s){ c.beginPath(); c.moveTo(x+2.5*s,0); c.lineTo(x+2.5*s-H,H); c.stroke(); } c.restore();
      // brown leather end panels (left/right)
      [[0,1],[W,-1]].forEach(([ex,dir])=>{ const w=W*.10; const lx=dir>0?0:W-w;
        const lg=c.createLinearGradient(lx,0,lx+w,0); lg.addColorStop(0,dir>0?leatherD:leather); lg.addColorStop(1,dir>0?leather:leatherD);
        c.fillStyle=lg; c.fillRect(lx,0,w,H); c.strokeStyle=rgb(40,26,14); c.lineWidth=2*s; c.strokeRect(lx,0,w,H); });
      const bolt=(x,y,r)=>{ r=r||3*s; const g=c.createRadialGradient(x-r*0.3,y-r*0.3,r*0.15,x,y,r); g.addColorStop(0,brassHi); g.addColorStop(1,rgb(150,118,58));
        c.beginPath(); c.arc(x,y,r,0,7); c.fillStyle=g; c.fill(); };
      // ── brown leather control panel ──
      const py=H*.20, ph=H*.42, px=W*.135, pw=W*.73;
      const pg=c.createLinearGradient(0,py,0,py+ph); pg.addColorStop(0,leather); pg.addColorStop(1,leatherD);
      rr(c,px,py,pw,ph,8*s); c.fillStyle=pg; c.fill();
      c.save(); rr(c,px,py,pw,ph,8*s); c.clip(); c.strokeStyle='rgba(0,0,0,0.12)'; c.lineWidth=1;
      for(let yy=py+4*s;yy<py+ph;yy+=6*s){ c.beginPath(); c.moveTo(px,yy); c.lineTo(px+pw,yy); c.stroke(); } c.restore();
      rr(c,px,py,pw,ph,8*s); c.strokeStyle=rgb(40,26,14); c.lineWidth=2.4*s; c.stroke();
      [px+12*s,px+pw-12*s].forEach(bx=>{ bolt(bx,py+10*s,2.6*s); bolt(bx,py+ph-10*s,2.6*s); });
      // ── brass nameplate "Jimmy Bean" (jeans-label look, upper-left of panel) ──
      const nbw=W*.20, nbh=ph*.30, nbx=px+W*.02, nby=py+ph*.10;
      const ng=c.createLinearGradient(0,nby,0,nby+nbh); ng.addColorStop(0,brassHi); ng.addColorStop(1,rgb(160,128,66));
      rr(c,nbx,nby,nbw,nbh,4*s); c.fillStyle=ng; c.fill(); rr(c,nbx,nby,nbw,nbh,4*s); c.strokeStyle=rgb(120,92,44); c.lineWidth=1.4*s; c.stroke();
      c.save(); c.textAlign='center'; c.textBaseline='middle'; setFont(d,F.crete,26); c.fillStyle=rgb(60,42,18);
      c.fillText('Jimmy Bean',nbx+nbw*.5,nby+nbh*.52); c.restore();
      textC(d,nbx+nbw*.5,nby+nbh+ph*.07,F.barlow,7.5,brass,'JB150  ·  CITRUS','center');
      // ── knob labels (above each cream knob) ──
      const ly=.585*H;
      const lbl=(cx2,t,sz)=>textC(d,cx2*W,ly,F.barlow,sz||8,cream,t);
      const ch2=(vals&&vals[6]!=null)?vals[6]>0.5:false;
      lbl(.225, ch2?'CH 2':'CH 1',7.5); lbl(.300,'VOLUME',7); lbl(.375,'BASS'); lbl(.450,'TREBLE',7);
      // TREMOLO bracket over Speed/Depth
      c.strokeStyle='rgba(238,230,206,0.5)'; c.lineWidth=1.2*s;
      c.beginPath(); c.moveTo(.615*W,.330*H); c.lineTo(.740*W,.330*H); c.stroke();
      textC(d,.6775*W,.305*H,F.barlow,7.5,cream,'TREMOLO');
      lbl(.545,'SUSTAIN',7); lbl(.640,'SPEED'); lbl(.715,'DEPTH');
      // BRIGHT switch label (id7) + channel indicator
      textC(d,.480*W,.700*H,F.barlow,7,cream,'BRIGHT');
      ledDot(d,.225*W,.330*H,true,ch2?224:120,ch2?52:200,ch2?46:90);
      // ── INPUT jacks (Hi/Lo, right of panel) + power ──
      const jack=(x,y)=>{ c.beginPath(); c.arc(x,y,8*s,0,7); c.fillStyle=rgb(18,16,16); c.fill(); c.strokeStyle=brass; c.lineWidth=2*s; c.stroke();
        c.beginPath(); c.arc(x,y,2.6*s,0,7); c.fillStyle=rgb(54,52,48); c.fill(); };
      jack(.810*W,.400*H); jack(.810*W,.520*H);
      textC(d,.810*W,.320*H,F.barlow,7.5,cream,'INPUT'); textC(d,.770*W,.400*H,F.barlow,7,cream,'HI'); textC(d,.770*W,.520*H,F.barlow,7,cream,'LO');
      // power jewel
      const pj=.875*W, pjy=.460*H; const jg2=c.createRadialGradient(pj-2*s,pjy-2*s,1*s,pj,pjy,7*s); jg2.addColorStop(0,rgb(255,160,90)); jg2.addColorStop(1,rgb(190,60,20));
      c.beginPath(); c.arc(pj,pjy,6*s,0,7); c.fillStyle=jg2; c.fill(); textC(d,.875*W,.560*H,F.barlow,6.5,cream,'POWER'); } };

  // ── MULTIVERSAL 610 (UA 610-A console module) — mint-green module strip on
  //    wood rails, black inner panel. 0 Gain (big LEVEL) 1 Low EQ 2 High EQ
  //    3 Level (OUTPUT) + Hi Gain(4) + Mic(5). DI: tube console preamp.
  P.multiversal610 = { w:1500, h:560, ptr:rgb(235,232,225), tick:rgb(150,146,138),
    knobs:[
      {id:1,cx:.385,cy:.42,r:.0175,style:'davies',cap:[24,24,26]},  // L.F.
      {id:2,cx:.475,cy:.42,r:.0175,style:'davies',cap:[24,24,26]},  // H.F.
      {id:0,cx:.625,cy:.50,r:.032,style:'davies',cap:[18,18,20]},   // LEVEL (big = Gain)
      {id:3,cx:.775,cy:.50,r:.019,style:'davies',cap:[24,24,26]} ], // OUTPUT
    sw3:[
      {id:4,cx:.245,cy:.42,hw:10,hh:16,two:true},   // LO / HI gain
      {id:5,cx:.245,cy:.70,hw:10,hh:16,two:true} ], // LINE / MIC
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const mint=rgb(150,184,164), mintD=rgb(122,158,138), ink=rgb(22,22,22), cream=rgb(235,232,225);
      // wood rails
      const wg=c.createLinearGradient(0,0,0,H); wg.addColorStop(0,rgb(168,118,72)); wg.addColorStop(1,rgb(128,84,48));
      c.fillStyle=wg; c.fillRect(0,0,W,H);
      c.save(); c.beginPath(); c.rect(0,0,W,H); c.clip(); c.strokeStyle='rgba(70,40,18,0.25)'; c.lineWidth=2*s;
      for(let yy=8*s; yy<H; yy+=14*s){ c.beginPath(); c.moveTo(0,yy); c.bezierCurveTo(W*.3,yy+5*s,W*.7,yy-5*s,W,yy); c.stroke(); } c.restore();
      // mint module strip
      const mx=.16*W, mw=.74*W;
      const mg=c.createLinearGradient(0,0,0,H); mg.addColorStop(0,mint); mg.addColorStop(1,mintD);
      c.fillStyle=mg; c.fillRect(mx,0,mw,H);
      c.strokeStyle=rgb(70,96,82); c.lineWidth=2.4*s; c.strokeRect(mx,0,mw,H);
      const bolt=(x,y)=>{ const g=c.createRadialGradient(x-1*s,y-1*s,0.5*s,x,y,3*s); g.addColorStop(0,rgb(240,242,244)); g.addColorStop(1,rgb(130,134,138));
        c.beginPath(); c.arc(x,y,2.8*s,0,7); c.fillStyle=g; c.fill(); };
      bolt(mx+12*s,12*s); bolt(mx+mw-12*s,12*s); bolt(mx+12*s,H-12*s); bolt(mx+mw-12*s,H-12*s);
      // black inner panel
      const px=mx+.025*W, pw=mw-.05*W, py=.10*H, ph=.80*H;
      rr(c,px,py,pw,ph,6*s); c.fillStyle=rgb(24,24,26); c.fill();
      rr(c,px,py,pw,ph,6*s); c.strokeStyle=rgb(96,120,106); c.lineWidth=1.6*s; c.stroke();
      textSpaced(d,.305*W,.21*H,F.barlow,9,cream,'MODULAR AMPLIFIER 610',0.25);
      // diamond logo + brand
      const dx=.875*W*0+px+pw-.06*W, dy=.22*H;
      c.save(); c.translate(dx,dy); c.rotate(Math.PI/4); c.strokeStyle=cream; c.lineWidth=2*s; c.strokeRect(-9*s,-9*s,18*s,18*s); c.restore();
      textC(d,dx,dy,F.crete,12,cream,'M');
      textC(d,.625*W,.20*H,F.crete,22,cream,'Multiversal','center');
      // switch legends
      textC(d,.245*W,.27*H,F.barlow,8,cream,'GAIN'); textC(d,.215*W,.42*H,F.barlow,7,cream,'LO','right'); textC(d,.275*W,.42*H,F.barlow,7,cream,'HI','left');
      textC(d,.215*W,.70*H,F.barlow,7,cream,'LINE','right'); textC(d,.275*W,.70*H,F.barlow,7,cream,'MIC','left');
      // EQ legends
      textC(d,.385*W,.62*H,F.barlow,8,cream,'L.F.'); textC(d,.475*W,.62*H,F.barlow,8,cream,'H.F.');
      textC(d,.385*W,.27*H,F.barlow,7,cream,'-6  0  +6'); textC(d,.475*W,.27*H,F.barlow,7,cream,'-6  0  +6');
      // LEVEL arc ticks + legends
      textC(d,.625*W,.30*H,F.barlow,8.5,cream,'LEVEL');
      textC(d,.775*W,.30*H,F.barlow,8,cream,'OUTPUT');
      textC(d,.745*W,.66*H,F.barlow,6.5,cream,'OFF','right'); textC(d,.808*W,.66*H,F.barlow,6.5,cream,'+12','left'); } };

  // ── SINAMP BASS DRIVER (Tech21 SansAmp BDDI v2) — black speckled DI pedal,
  //    yellow legends. 0 Drive 1 Presence 2 Blend 3 Bass 4 Mid 5 Treble
  //    6 Level + Bass Shift(7) + Mid Shift(8).
  P.sinampbassdriver = { w:1100, h:980, ptr:rgb(244,222,38), tick:rgb(120,112,60),
    knobs:[
      {id:6,cx:.155,cy:.255,r:.044,style:'davies',cap:[20,20,20]},  // LEVEL
      {id:2,cx:.330,cy:.255,r:.044,style:'davies',cap:[20,20,20]},  // BLEND
      {id:5,cx:.505,cy:.255,r:.044,style:'davies',cap:[20,20,20]},  // TREBLE
      {id:1,cx:.680,cy:.255,r:.044,style:'davies',cap:[20,20,20]},  // PRESENCE
      {id:0,cx:.860,cy:.255,r:.044,style:'davies',cap:[20,20,20]},  // DRIVE
      {id:4,cx:.420,cy:.450,r:.044,style:'davies',cap:[20,20,20]},  // MID
      {id:3,cx:.595,cy:.450,r:.044,style:'davies',cap:[20,20,20]} ],// BASS
    sw3:[
      {id:8,cx:.330,cy:.620,hw:9,hh:14,two:true},   // MID SHIFT 1000/500
      {id:7,cx:.480,cy:.620,hw:9,hh:14,two:true} ], // BASS SHIFT 80/40
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const yel=rgb(244,222,38), blk=rgb(26,25,24);
      c.fillStyle=blk; c.fillRect(0,0,W,H);
      // speckle texture (deterministic)
      c.save(); for(let i=0;i<560;i++){ const xx=((i*73)%97)/97*W, yy=((i*131)%89)/89*H;
        c.fillStyle = (i%3===0)?'rgba(200,200,200,0.10)':'rgba(0,0,0,0.35)';
        c.fillRect(xx,yy,1.6*s,1.6*s); } c.restore();
      rr(c,.02*W,.015*H,.96*W,.97*H,10*s); c.strokeStyle='rgba(255,255,255,0.10)'; c.lineWidth=2*s; c.stroke();
      const lbl=(x,y,t,sz)=>textSpaced(d,x*W,y*H,F.barlow,sz||10,yel,t,0.4);
      lbl(.155,.135,'LEVEL'); lbl(.330,.135,'BLEND'); lbl(.505,.135,'TREBLE'); lbl(.680,.135,'PRESENCE',9); lbl(.860,.135,'DRIVE');
      lbl(.255,.450,'MID'); lbl(.745,.450,'BASS');
      // shift switches + legends
      lbl(.330,.555,'SHIFT',8); textC(d,.330*W,.690*H,F.barlow,7,yel,'500/1000 Hz');
      lbl(.480,.555,'SHIFT',8); textC(d,.480*W,.690*H,F.barlow,7,yel,'40/80 Hz');
      // LED
      const lg=c.createRadialGradient(.640*W-1*s,.620*H-1*s,1*s,.640*W,.620*H,6*s); lg.addColorStop(0,rgb(255,120,110)); lg.addColorStop(1,rgb(170,30,24));
      c.beginPath(); c.arc(.640*W,.620*H,5.5*s,0,7); c.fillStyle=lg; c.fill();
      // IN/OUT legends
      textC(d,.060*W,.450*H,F.barlow,8,yel,'OUT'); textC(d,.930*W,.450*H,F.barlow,8,yel,'INPUT');
      // brand block
      textC(d,.42*W,.800*H,F.anton,40,yel,'SinAmp','center');
      textSpaced(d,.42*W,.880*H,F.barlow,15,yel,'BASS DRIVER DI',0.5);
      // footswitch
      const fx=.82*W, fy=.840*H;
      const fg=c.createRadialGradient(fx-4*s,fy-4*s,3*s,fx,fy,26*s); fg.addColorStop(0,rgb(235,238,242)); fg.addColorStop(1,rgb(120,124,130));
      c.beginPath(); c.arc(fx,fy,24*s,0,7); c.fillStyle=fg; c.fill(); c.strokeStyle=rgb(60,62,66); c.lineWidth=2*s; c.stroke();
      c.beginPath(); c.arc(fx,fy,16*s,0,7); c.strokeStyle='rgba(0,0,0,0.30)'; c.lineWidth=1.6*s; c.stroke(); } };

  // ── MEVE 1073 (Neve 1073 channel amp) — marengo-grey rack strip: red GAIN,
  //    silver EQ knobs w/ freq selectors, blue HPF. 0 Gain 1 Low 2 LowFreq(4)
  //    3 Mid 4 MidFreq(6) 5 High 6 HPF(5) 7 Output. DI: class-A console pre.
  P.meve1073 = { w:1560, h:430, ptr:rgb(238,238,240), tick:rgb(170,172,176),
    knobs:[
      {id:0,cx:.105,cy:.46,r:.026,style:'moog',cap:[152,32,42]},    // GAIN (red)
      {id:5,cx:.255,cy:.46,r:.024,style:'moog',cap:[148,150,152]},  // HIGH +/- (12k)
      {id:3,cx:.405,cy:.46,r:.024,style:'moog',cap:[148,150,152]},  // MID +/-
      {id:4,cx:.495,cy:.62,r:.015,style:'moog',cap:[110,112,116],select:6},  // MID FREQ
      {id:1,cx:.605,cy:.46,r:.024,style:'moog',cap:[148,150,152]},  // LOW +/-
      {id:2,cx:.695,cy:.62,r:.015,style:'moog',cap:[110,112,116],select:4},  // LOW FREQ
      {id:6,cx:.805,cy:.46,r:.022,style:'moog',cap:[44,74,116],select:5},    // HPF (blue)
      {id:7,cx:.930,cy:.46,r:.018,style:'moog',cap:[148,150,152]} ],// OUTPUT
    draw(d,vals){ const {ctx:c,W,H,s}=d;
      const pan=rgb(54,62,66), panD=rgb(42,50,54), wht=rgb(236,238,240);
      const pg=c.createLinearGradient(0,0,0,H); pg.addColorStop(0,pan); pg.addColorStop(1,panD);
      c.fillStyle=pg; c.fillRect(0,0,W,H);
      c.strokeStyle='rgba(0,0,0,0.5)'; c.lineWidth=3*s; c.strokeRect(1.5*s,1.5*s,W-3*s,H-3*s);
      const bolt=(x,y)=>{ const g=c.createRadialGradient(x-1*s,y-1*s,0.5*s,x,y,4*s); g.addColorStop(0,rgb(220,224,228)); g.addColorStop(1,rgb(100,104,110));
        c.beginPath(); c.arc(x,y,3.4*s,0,7); c.fillStyle=g; c.fill(); };
      bolt(.022*W,.14*H); bolt(.022*W,.86*H); bolt(.978*W,.14*H); bolt(.978*W,.86*H);
      // brand squiggle + name
      c.save(); c.strokeStyle=wht; c.lineWidth=2.2*s; c.beginPath();
      const sx=.045*W, sy=.16*H; c.arc(sx-5*s,sy,5*s,Math.PI,0,false); c.arc(sx+5*s,sy,5*s,0,Math.PI,true); c.stroke(); c.restore();
      textC(d,.105*W,.13*H,F.barlow,9,wht,'MEVE');
      // GAIN ring legends (mic/line dB)
      textC(d,.105*W,.85*H,F.barlow,8.5,wht,'GAIN  dB');
      textC(d,.063*W,.30*H,F.barlow,6.5,wht,'-80','right'); textC(d,.147*W,.30*H,F.barlow,6.5,wht,'LINE','left');
      textC(d,.063*W,.62*H,F.barlow,6.5,wht,'MIC.','right'); textC(d,.147*W,.62*H,F.barlow,6.5,wht,'-20','left');
      // EQ section legends (the 1073 glyphs as text)
      textC(d,.255*W,.16*H,F.barlow,8,wht,'HIGH  12K');
      textC(d,.255*W,.85*H,F.barlow,7,wht,'+        -');
      textC(d,.405*W,.16*H,F.barlow,8,wht,'PRESENCE');
      textC(d,.405*W,.85*H,F.barlow,7,wht,'+        -');
      textC(d,.495*W,.85*H,F.barlow,6.5,wht,'KHz  .36-7.2');
      textC(d,.605*W,.16*H,F.barlow,8,wht,'LOW');
      textC(d,.605*W,.85*H,F.barlow,7,wht,'+        -');
      textC(d,.695*W,.85*H,F.barlow,6.5,wht,'Hz  35-220');
      textC(d,.805*W,.16*H,F.barlow,8,wht,'HPF');
      textC(d,.805*W,.85*H,F.barlow,6.5,wht,'OFF 50 80 160 300');
      textC(d,.930*W,.16*H,F.barlow,8,wht,'OUTPUT');
      // EQL plate
      rr(c,.862*W,.30*H,.034*W,.40*H,3*s); c.fillStyle=rgb(232,234,236); c.fill();
      textC(d,.879*W,.42*H,F.barlow,8,rgb(40,46,50),'EQL'); textC(d,.879*W,.58*H,F.barlow,8,rgb(40,46,50),'1073'); } };


  // ── render / attach ────────────────────────────────────────────────────
  function makeCtx(canvas, spec) {
    const dpr = window.devicePixelRatio || 1;
    const cssW = canvas.clientWidth || spec.w, cssH = cssW * spec.h / spec.w;
    canvas.style.height = cssH + 'px';
    canvas.width = Math.round(cssW*dpr); canvas.height = Math.round(cssH*dpr);
    const ctx = canvas.getContext('2d'); ctx.setTransform(dpr*cssW/spec.w,0,0,dpr*cssH/spec.h,0,0);
    return { ctx, W: spec.w, H: spec.h, s: 1 };
  }
  function drawSpec(canvas, spec, values) {
    const d = makeCtx(canvas, spec);
    spec.draw(d, values || {});         // EQ faders are drawn inside spec.draw
    (spec.knobs || []).forEach(k => {
      const v = (values && values[k.id] != null) ? values[k.id] : 0.5;
      knob(d, k.cx * d.W, k.cy * d.H, k.r * d.W, v, k.style,
           (k.cap || [40, 40, 44])[0], (k.cap || [40, 40, 44])[1], (k.cap || [40, 40, 44])[2],
           (k.tick !== undefined ? k.tick : spec.tick), spec.ptr);
    });
    (spec.switches || []).forEach(s => {
      const on = (values && values[s.id] != null) ? values[s.id] > 0.5 : false;
      if (s.style === 'bat') batToggle(d, s.cx * d.W, s.cy * d.H, (s.hs || .013) * d.W, on);
      else switchSquare(d, s.cx * d.W, s.cy * d.H, s.hs * d.W, on, s.dark);
    });
    (spec.sw3 || []).forEach(s => {
      // `hidden` sw3 entries are click-only (the spec draws its own visual, e.g.
      // a plugged-in cable) — skip the default lever render.
      if (s.hidden) return;
      // `two` toggles only 0/1, so the lever sits at the bottom (0) or top (1)
      // — a 2-position bat lever using the same switch3 renderer that works.
      const v = (values && values[s.id] != null) ? values[s.id] : 0.5;
      switch3(d, s.cx * d.W, s.cy * d.H, v);
    });
    (spec.sliders || []).forEach(sl => {
      const v = (values && values[sl.id] != null) ? values[sl.id] : 0.5;
      hSlider(d, sl.x0 * d.W, sl.x1 * d.W, sl.y * d.H, v);
    });
    (spec.faders || []).forEach(fd => {
      const v = (values && values[fd.id] != null) ? values[fd.id] : 0.5;
      vfader(d, fd.cx * d.W, fd.y0 * d.H, fd.y1 * d.H, v);
    });
  }
  function render(canvas, stem, values) {
    const spec = P[stem]; if (!spec) return false;
    drawSpec(canvas, spec, values); return true;
  }
  function attach(canvas, stem, opts) {
    opts = opts || {};
    const spec = P[stem] || (opts.params ? buildGeneric(stem, opts.params) : null);
    if (!spec) return false;
    const values = opts.values || {};
    drawSpec(canvas, spec, values);
    if (!opts.interactive) return true;
    const G = spec.eq ? eqGeom(spec) : null;
    let drag = -1, sdrag = -1, fdrag = -1, lastY = 0, dv = 0;
    const faderVal = (fd, py) => clamp(1 - (py - fd.y0*spec.h) / ((fd.y1-fd.y0)*spec.h), 0, 1);
    const toSpec = (clientX, clientY) => { const rect = canvas.getBoundingClientRect();
      const sx = spec.w / canvas.clientWidth, sy = spec.h / (canvas.clientHeight || 1);
      return { x: (clientX - rect.left) * sx, y: (clientY - rect.top) * sy }; };
    const hitKnob = (x, y) => { for (let i = 0; i < (spec.knobs || []).length; i++) {
      const k = spec.knobs[i], dx = x - k.cx * spec.w, dy = y - k.cy * spec.h, R = k.r * spec.w * 1.25 + 6;
      if (dx * dx + dy * dy <= R * R) return i; } return -1; };
    const hitFader = (x, y) => { if (y < G.plateY - 8 || y > G.plateY + G.plateH + 8) return -1;
      const i = Math.floor((x - G.faderL) / G.colW); return (i >= 0 && i < G.n) ? i : -1; };
    canvas.addEventListener('mousedown', e => {
      // The editor can attach twice (immediate draw + a redraw after fonts load),
      // leaving two mousedown listeners on the same canvas. They receive the *same*
      // event object, so a single physical click would be processed twice. That's
      // harmless for absolute controls (knobs/sliders land on the same value) but it
      // double-toggles switches straight back to their original state — which is why
      // the 2-position Bright switch looked dead while the 3-way "worked" (two steps
      // land on a different value). Stamp the event so only the first listener acts.
      if (e.__rbHandled) return;
      e.__rbHandled = true;
      const p = toSpec(e.clientX, e.clientY);
      if (spec.eq) { const i = hitFader(p.x, p.y); if (i < 0) return; drag = i;
        const v = G.yToVal(p.y); values[i] = v; drawSpec(canvas, spec, values);
        if (opts.onChange) opts.onChange(i, v); e.preventDefault(); return; }
      // Switches: a click toggles 0↔1 (no drag).
      for (const s of (spec.switches || [])) {
        const hx = s.hs * spec.w + 6;
        // bat-handle toggles draw a tall lever above/below the nut — widen the
        // vertical hit area so clicking the visible lever (not just the nut) works.
        const hy = (s.style === 'bat' ? s.hs * spec.w * 2.6 : s.hs * spec.w) + 6;
        if (Math.abs(p.x - s.cx * spec.w) <= hx && Math.abs(p.y - s.cy * spec.h) <= hy) {
          const nv = (values[s.id] > 0.5) ? 0 : 1; values[s.id] = nv;
          drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(s.id, nv);
          e.preventDefault(); return;
        }
      }
      // Bat toggles: 3-way cycles 0→0.5→1→0; a `two` toggle flips 0↔1.
      for (const s of (spec.sw3 || [])) {
        const hw = s.hw != null ? s.hw : 12, hh = s.hh != null ? s.hh : 22;
        if (Math.abs(p.x - s.cx * spec.w) <= hw && Math.abs(p.y - s.cy * spec.h) <= hh) {
          const cur = (values[s.id] != null) ? values[s.id] : 0.5;
          const nv = s.two ? (cur >= 0.5 ? 0 : 1) : (cur < 0.25 ? 0.5 : cur < 0.75 ? 1 : 0); values[s.id] = nv;
          drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(s.id, nv);
          e.preventDefault(); return;
        }
      }
      // Horizontal sliders: click/drag along the track sets value by x position.
      for (let i = 0; i < (spec.sliders || []).length; i++) {
        const sl = spec.sliders[i], sx0 = sl.x0 * spec.w, sx1 = sl.x1 * spec.w, sy = sl.y * spec.h;
        if (p.x >= sx0 - 12 && p.x <= sx1 + 12 && Math.abs(p.y - sy) <= 16) {
          sdrag = i; const v = clamp((p.x - sx0) / (sx1 - sx0), 0, 1); values[sl.id] = v;
          drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(sl.id, v);
          e.preventDefault(); return;
        }
      }
      // Vertical faders (graphic EQ): click/drag sets value by y position.
      for (let i = 0; i < (spec.faders || []).length; i++) {
        const fd = spec.faders[i];
        if (Math.abs(p.x - fd.cx*spec.w) <= 12 && p.y >= fd.y0*spec.h - 12 && p.y <= fd.y1*spec.h + 12) {
          fdrag = i; const v = faderVal(fd, p.y); values[fd.id] = v;
          drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(fd.id, v);
          e.preventDefault(); return;
        }
      }
      const k = hitKnob(p.x, p.y); if (k < 0) return;
      const kn = spec.knobs[k];
      // Selector knob (e.g. MODE): a click steps through `select` discrete
      // positions (0 … 1) instead of dragging continuously.
      if (kn.select) {
        const n = kn.select, step = 1 / (n - 1);
        const cur = (values[kn.id] != null) ? values[kn.id] : 0;
        const nv = ((Math.round(cur / step) + 1) % n) * step;
        values[kn.id] = nv; drawSpec(canvas, spec, values);
        if (opts.onChange) opts.onChange(kn.id, nv); e.preventDefault(); return;
      }
      drag = k; lastY = e.clientY;
      dv = (values[kn.id] != null) ? values[kn.id] : 0.5; e.preventDefault();
    });
    window.addEventListener('mousemove', e => {
      if (fdrag >= 0) { const p = toSpec(e.clientX, e.clientY); const fd = spec.faders[fdrag];
        const v = faderVal(fd, p.y); values[fd.id] = v; drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(fd.id, v); return; }
      if (sdrag >= 0) { const p = toSpec(e.clientX, e.clientY); const sl = spec.sliders[sdrag];
        const sx0 = sl.x0 * spec.w, sx1 = sl.x1 * spec.w; const v = clamp((p.x - sx0) / (sx1 - sx0), 0, 1);
        values[sl.id] = v; drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(sl.id, v); return; }
      if (drag < 0) return;
      if (spec.eq) { const p = toSpec(e.clientX, e.clientY); const v = G.yToVal(p.y);
        values[drag] = v; drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(drag, v); return; }
      const dy = lastY - e.clientY; lastY = e.clientY; dv = clamp(dv + dy / 170, 0, 1);
      const id = spec.knobs[drag].id; values[id] = dv;
      drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(id, dv);
    });
    window.addEventListener('mouseup', () => { drag = -1; sdrag = -1; fdrag = -1; });
    return true;
  }
  function dataURL(stem, values) {
    const spec = P[stem]; if (!spec) return null;
    const cv = document.createElement('canvas'); cv.style.width = '220px';
    Object.defineProperty(cv, 'clientWidth', { value: 220, configurable: true });
    drawSpec(cv, spec, values || {});
    try { return cv.toDataURL('image/png'); } catch (_) { return null; }
  }

  window.RBPedalCanvas = { ready, has: s => !!P[s], attach, render, dataURL, specs: P };
})();
