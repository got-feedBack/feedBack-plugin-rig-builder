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

  // square red toggle switch (Eden Bass Boost / Mid Shift, Wah Auto, …)
  function switchSquare(d, cx, cy, hs, on) { const c = d.ctx;
    rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.fillStyle = on ? rgb(208,40,36) : rgb(78,22,20); c.fill();
    rr(c, cx-hs, cy-hs, hs*2, hs*2, 3); c.strokeStyle = rgb(20,12,10); c.lineWidth = 1.5; c.stroke();
    if (on) { c.beginPath(); c.arc(cx, cy, hs*0.34, 0, 7); c.fillStyle = rgb(255,180,170); c.fill(); } }

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
  // stomp + script brand. Parody (Regis / UltraVibe). RS knob names.
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
      textC(d,.50*W,.915*H,F.ink,24,ink,'Regis'); } };

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
  // (Regis / GM-2 Guvnor Minus). RS knob names. Gain0 Bass1 Mid2 Treble3 Deep4.
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
      textC(d,.50*W,.885*H,F.ink,26,dk,'Regis'); } };

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
           spec.tick, spec.ptr);
    });
    (spec.switches || []).forEach(s => {
      const on = (values && values[s.id] != null) ? values[s.id] > 0.5 : false;
      switchSquare(d, s.cx * d.W, s.cy * d.H, s.hs * d.W, on);
    });
    (spec.sw3 || []).forEach(s => {
      const v = (values && values[s.id] != null) ? values[s.id] : 0.5;
      switch3(d, s.cx * d.W, s.cy * d.H, v);
    });
    (spec.sliders || []).forEach(sl => {
      const v = (values && values[sl.id] != null) ? values[sl.id] : 0.5;
      hSlider(d, sl.x0 * d.W, sl.x1 * d.W, sl.y * d.H, v);
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
    let drag = -1, sdrag = -1, lastY = 0, dv = 0;
    const toSpec = (clientX, clientY) => { const rect = canvas.getBoundingClientRect();
      const sx = spec.w / canvas.clientWidth, sy = spec.h / (canvas.clientHeight || 1);
      return { x: (clientX - rect.left) * sx, y: (clientY - rect.top) * sy }; };
    const hitKnob = (x, y) => { for (let i = 0; i < (spec.knobs || []).length; i++) {
      const k = spec.knobs[i], dx = x - k.cx * spec.w, dy = y - k.cy * spec.h, R = k.r * spec.w * 1.25 + 6;
      if (dx * dx + dy * dy <= R * R) return i; } return -1; };
    const hitFader = (x, y) => { if (y < G.plateY - 8 || y > G.plateY + G.plateH + 8) return -1;
      const i = Math.floor((x - G.faderL) / G.colW); return (i >= 0 && i < G.n) ? i : -1; };
    canvas.addEventListener('mousedown', e => {
      const p = toSpec(e.clientX, e.clientY);
      if (spec.eq) { const i = hitFader(p.x, p.y); if (i < 0) return; drag = i;
        const v = G.yToVal(p.y); values[i] = v; drawSpec(canvas, spec, values);
        if (opts.onChange) opts.onChange(i, v); e.preventDefault(); return; }
      // Switches: a click toggles 0↔1 (no drag).
      for (const s of (spec.switches || [])) {
        const hs = s.hs * spec.w + 5;
        if (Math.abs(p.x - s.cx * spec.w) <= hs && Math.abs(p.y - s.cy * spec.h) <= hs) {
          const nv = (values[s.id] > 0.5) ? 0 : 1; values[s.id] = nv;
          drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(s.id, nv);
          e.preventDefault(); return;
        }
      }
      // 3-way toggles: a click cycles 0→0.5→1→0.
      for (const s of (spec.sw3 || [])) {
        if (Math.abs(p.x - s.cx * spec.w) <= 10 && Math.abs(p.y - s.cy * spec.h) <= 20) {
          const cur = (values[s.id] != null) ? values[s.id] : 0.5;
          const nv = cur < 0.25 ? 0.5 : cur < 0.75 ? 1 : 0; values[s.id] = nv;
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
    window.addEventListener('mouseup', () => { drag = -1; sdrag = -1; });
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
