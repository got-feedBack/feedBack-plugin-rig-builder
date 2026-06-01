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

  function box(d, r, g, b) { const {ctx:c, W, H, s} = d; const m=8*s;
    c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
    const grad=c.createLinearGradient(0,m,0,H-m);
    grad.addColorStop(0,rgb(clamp(r+22,0,255),clamp(g+22,0,255),clamp(b+22,0,255)));
    grad.addColorStop(1,rgb(clamp(r-18,0,255),clamp(g-18,0,255),clamp(b-18,0,255)));
    rr(c,m,m,W-2*m,H-2*m,14*s); c.fillStyle=grad; c.fill();
    rr(c,m,m,W-2*m,H-2*m,14*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=2*s; c.stroke();
    const o=22*s; screw(d,m+o,m+o); screw(d,W-m-o,m+o); screw(d,m+o,H-m-o); screw(d,W-m-o,H-m-o); }

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

  function setFont(d, family, px) { d.ctx.font = `${px*d.s}px ${family}, sans-serif`; }
  function textC(d, cx, cy, family, px, col, str, align) {
    const c=d.ctx; setFont(d,family,px); c.fillStyle=col; c.textAlign=align||'center'; c.textBaseline='middle';
    c.fillText(str, cx, cy); }
  function textSpaced(d, cx, cy, family, px, col, str, sp) {
    const c=d.ctx; setFont(d,family,px); c.fillStyle=col; c.textBaseline='middle';
    sp*=d.s; const ws=[...str].map(ch=>c.measureText(ch).width+sp); const tot=ws.reduce((a,b)=>a+b,0)-sp;
    let x=cx-tot/2; c.textAlign='left'; for(let i=0;i<str.length;i++){ c.fillText(str[i],x,cy); x+=ws[i]; } }
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
  function chiefBody(d, r, g, b) { const {ctx:c,W,H,s}=d, m=7*s, cl=v=>clamp(v,0,255);
    c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
    const grad=c.createLinearGradient(0,m,0,H-m); grad.addColorStop(0,rgb(cl(r+18),cl(g+18),cl(b+18))); grad.addColorStop(1,rgb(cl(r-14),cl(g-14),cl(b-14)));
    rr(c,m,m,W-2*m,H-2*m,12*s); c.fillStyle=grad; c.fill();
    rr(c,m,m,W-2*m,H-2*m,12*s); c.strokeStyle='rgba(0,0,0,0.47)'; c.lineWidth=2*s; c.stroke();
    // black knob plate
    rr(c,m+11*s,H*0.10,W-2*m-22*s,H*0.235,6*s); c.fillStyle=rgb(20,20,22); c.fill();
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
    chiefBadge(d, padT, padBot);
  }
  // Wide engraved 'CHIEF' badge across the black step pad (parody Boss logo):
  // pad-colour fill + black outline; much wider than tall via big letter spacing.
  function chiefBadge(d, padT, padBot) { const W = d.W;
    outlineText(d, W*0.5, padT+(padBot-padT)*0.30, FONTS.bebas, 40, rgb(20,20,22), rgb(0,0,0), 'CHIEF', 13);
  }
  // n1/n2 = two-word model name (n1 left, n2 right); code = parody model number
  // (e.g. 'CB-3'), a bit smaller, bottom-RIGHT corner. dy shifts everything down
  // (the EQ treadle sits lower, so it passes a positive dy).
  function chiefName(d, n1, n2, code, dy, codeDy) { const {W,H}=d; dy = dy || 0; codeDy = codeDy || 0; const dk = rgb(16,16,20);
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

  P.bassdistortion = { w:320,h:500, knobs:[
      {id:0,cx:.215,cy:.305,r:.105,style:'pointer',cap:[26,26,28]},
      {id:1,cx:.500,cy:.305,r:.105,style:'pointer',cap:[26,26,28]},
      {id:2,cx:.785,cy:.305,r:.105,style:'pointer',cap:[26,26,28]}],
    tick:rgb(232,233,236), ptr:rgb(240,241,244),
    draw(d){ box(d,18,18,20); const w=rgb(238,239,242);
      boxedLabel(d,.215,.135,.115,.028,F.barlow,12.5,w,w,'GAIN');
      boxedLabel(d,.500,.135,.110,.028,F.barlow,12.5,w,w,'TONE');
      boxedLabel(d,.785,.135,.125,.028,F.barlow,12.5,w,w,'FILTER');
      boxedLabel(d,.5,.55,.255,.092,F.anton,56,w,w,'MOUSE');
      ledDot(d,d.W*.5,d.H*.71,true,210,70,58); footRound(d,d.W*.5,d.H*.83,24*d.s); } };

  // Bass Overdrive — Darkglass Microtubes B3K look, RS params (4 knobs):
  // Blend0 Drive1 Grunt2 Attack3. (Real B3K has Grunt/Attack as switches, but RS
  // exposes them as continuous knobs, so we keep knobs.)
  P.bassoverdrive = { w:300,h:490,
    knobs:[
      {id:0,cx:.30,cy:.285,r:.10,style:'boss'},   // BLEND
      {id:1,cx:.70,cy:.285,r:.10,style:'boss'},   // DRIVE
      {id:2,cx:.30,cy:.555,r:.10,style:'boss'},   // GRUNT
      {id:3,cx:.70,cy:.555,r:.10,style:'boss'}],  // ATTACK
    ptr:rgb(238,239,242),
    draw(d){ box(d,18,18,20); const w=rgb(235,236,239), dim=rgb(150,151,154);
      textSpaced(d,.5*d.W,.10*d.H,F.bebas,24,w,'BLACKBRASS',2);           // parody brand
      textSpaced(d,.30*d.W,.40*d.H,F.barlow,11,w,'BLEND',1.4);
      textSpaced(d,.70*d.W,.40*d.H,F.barlow,11,w,'DRIVE',1.4);
      textSpaced(d,.30*d.W,.67*d.H,F.barlow,11,w,'GRUNT',1.4);
      textSpaced(d,.70*d.W,.67*d.H,F.barlow,11,w,'ATTACK',1.4);
      textSpaced(d,.5*d.W,.76*d.H,F.barlow,13,w,'MINITUBES B3X',2);       // parody model
      textSpaced(d,.5*d.W,.80*d.H,F.barlow,8,dim,'CMOS BASS OVERDRIVE',1.4);
      ledDot(d,d.W*.5,d.H*.86,true,196,72,60); footRound(d,d.W*.5,d.H*.93,18*d.s); } };

  P.bassfuzz = { w:320,h:400, knobs:[
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
      outlineText(d,.5*W,.675*H,F.anton,48,rgb(242,242,244),rgb(12,14,16),'BIG BUZZ',5);
      textC(d,.30*W,.565*H,F.crete,34,rgb(16,20,14),'bass');
      // LED at top-centre (above the knobs), clear of the FUZZ wordmark
      ledDot(d,W*.5,H*.105,true,224,60,52); footRound(d,W*.5,H*.81,21*s); } };

  // Big Buzz — silver/red vintage fuzz face inspired by a triangle-era fuzz box.
  // Param order: Gain0 Tone1.
  P.bigbuzz = { w:320,h:430, knobs:[
      {id:0,cx:.30,cy:.245,r:.092,style:'davies'},
      {id:1,cx:.70,cy:.245,r:.092,style:'davies'}],
    ptr:rgb(236,238,238),
    draw(d){ const {ctx:c,W,H,s}=d; box(d,202,201,190);
      const panelX=W*.105, panelY=H*.095, panelW=W*.79, panelH=H*.79;
      const pg=c.createLinearGradient(0,panelY,0,panelY+panelH);
      pg.addColorStop(0,rgb(228,226,214)); pg.addColorStop(1,rgb(178,176,164));
      rr(c,panelX,panelY,panelW,panelH,9*s); c.fillStyle=pg; c.fill();
      rr(c,panelX,panelY,panelW,panelH,9*s); c.strokeStyle=rgb(126,44,36); c.lineWidth=2.4*s; c.stroke();
      c.strokeStyle=rgb(154,48,38); c.lineWidth=2.2*s;
      c.beginPath(); c.moveTo(W*.20,H*.455); c.lineTo(W*.50,H*.555); c.lineTo(W*.80,H*.455); c.stroke();
      c.beginPath(); c.moveTo(W*.20,H*.705); c.lineTo(W*.50,H*.610); c.lineTo(W*.80,H*.705); c.stroke();
      textSpaced(d,.30*W,.360*H,F.barlow,10.5,rgb(78,50,42),'GAIN',1.0);
      textSpaced(d,.70*W,.360*H,F.barlow,10.5,rgb(78,50,42),'TONE',1.0);
      outlineText(d,.5*W,.555*H,F.anton,54,rgb(236,232,218),rgb(132,38,32),'BIG',5.2);
      outlineText(d,.5*W,.655*H,F.anton,54,rgb(236,232,218),rgb(132,38,32),'BUZZ',4.8);
      textSpaced(d,.5*W,.760*H,F.barlow,8.5,rgb(82,64,54),'SUSTAIN  FUZZ',1.9);
      ledDot(d,W*.50,H*.820,true,224,62,52); footRound(d,W*.50,H*.905,20*s); } };

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

  // Buzz-Tone - gold wedge-style vintage two-knob fuzz face.
  // Param order: Gain0 Tone1.
  P.buzztone = { w:470,h:300, knobs:[
      {id:0,cx:.355,cy:.285,r:.073,style:'davies'},
      {id:1,cx:.645,cy:.285,r:.073,style:'davies'}],
    tick:rgb(72,42,24), ptr:rgb(238,230,204),
    draw(d){ const {ctx:c,W,H,s}=d;
      c.fillStyle=rgb(10,10,12); c.fillRect(0,0,W,H);
      const x0=W*.075, x1=W*.925, y0=H*.105, y1=H*.875;
      const topInset=W*.070, botInset=W*.020;
      const body=c.createLinearGradient(0,y0,0,y1);
      body.addColorStop(0,rgb(188,142,68)); body.addColorStop(.48,rgb(151,102,46)); body.addColorStop(1,rgb(86,59,34));
      c.beginPath();
      c.moveTo(x0+topInset,y0); c.lineTo(x1-topInset,y0);
      c.lineTo(x1-botInset,y1); c.lineTo(x0+botInset,y1); c.closePath();
      c.fillStyle=body; c.fill();
      c.strokeStyle=rgb(48,34,24); c.lineWidth=2.2*s; c.stroke();
      c.save();
      c.clip();
      const glow=c.createLinearGradient(x0,y0,x1,y1);
      glow.addColorStop(0,rgb(245,207,118,0.22)); glow.addColorStop(.55,rgb(255,232,150,0.035)); glow.addColorStop(1,rgb(34,24,18,0.22));
      c.fillStyle=glow; c.fillRect(x0,y0,x1-x0,y1-y0);
      c.strokeStyle=rgb(208,154,76,0.28); c.lineWidth=1.2*s;
      for(let i=0;i<9;i++){ const yy=y0+(y1-y0)*(i+1)/10; c.beginPath(); c.moveTo(x0+topInset*.65,yy); c.lineTo(x1-topInset*.65,yy+2*s); c.stroke(); }
      c.restore();
      screw(d,W*.155,H*.170); screw(d,W*.845,H*.170); screw(d,W*.180,H*.805); screw(d,W*.820,H*.805);
      const panelY=H*.170; c.beginPath(); c.moveTo(W*.250,panelY); c.lineTo(W*.750,panelY);
      c.strokeStyle=rgb(70,42,26); c.lineWidth=1.4*s; c.stroke();
      textSpaced(d,.355*W,.475*H,F.barlow,10.5,rgb(42,30,22),'GAIN',1.2);
      textSpaced(d,.645*W,.475*H,F.barlow,10.5,rgb(42,30,22),'TONE',1.2);
      c.save();
      c.translate(W*.5,H*.640); c.transform(1,0,-0.10,1,0,0);
      outlineText(d,0,0,F.anton,50,rgb(235,211,139),rgb(64,38,25),'BUZZ-TONE',4.0);
      c.restore();
      textSpaced(d,.50*W,.735*H,F.barlow,9,rgb(226,190,118),'GERMANIUM  FUZZ',2.3);
      ledDot(d,W*.50,H*.800,true,224,56,45);
      footRound(d,W*.50,H*.885,18*s); } };

  function chiefSpec(w,h,col,knobIds,n1,n2,code){ return { w,h, knobs: knobIds.map(k=>({id:k.id,cx:k.cx,cy:.235,r:.072,style:'boss'})),
    ptr:rgb(238,240,242), draw(d){ chiefBody(d,col[0],col[1],col[2]); const wc=rgb(238,240,242);
      knobIds.forEach(k=> textSpaced(d,k.cx*d.W,.135*d.H,F.barlow,k.lblPx||8.5,wc,k.lbl,0.2));
      chiefName(d,n1,n2,code); } }; }

  P.basschorus = chiefSpec(300,480,[40,158,150],
    [{id:0,cx:.205,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.595,lbl:'LO FILTER',lblPx:8},{id:3,cx:.79,lbl:'MIX'}],
    'Bass','Chorus','CB-3');
  P.basssuboctave = { w:300,h:480, knobs:[{id:0,cx:.34,cy:.235,r:.088,style:'boss'},{id:1,cx:.66,cy:.235,r:.088,style:'boss'}],
    ptr:rgb(236,232,224), draw(d){ chiefBody(d,112,70,66); const w=rgb(236,232,224);
      textSpaced(d,.34*d.W,.12*d.H,F.barlow,9,w,'MIX',0.2); textSpaced(d,.66*d.W,.12*d.H,F.barlow,9,w,'TONE',0.2);
      chiefName(d,'Bass','Suboctave','SO-2'); } };
  P.bassfilterdelay = chiefSpec(300,480,[156,64,72],
    [{id:0,cx:.205,lbl:'TIME'},{id:1,cx:.40,lbl:'FEEDBACK',lblPx:7.5},{id:2,cx:.595,lbl:'MIX'},{id:3,cx:.79,lbl:'FILTER',lblPx:8}],
    'Bass','Delay','DL-3');
  P.bassflanger = chiefSpec(300,480,[96,80,134],
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

  P.bassphase = boxSpec(320,470,[124,92,68],
    [{id:0,cx:.20,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.60,lbl:'MIX'},{id:3,cx:.80,lbl:'FILTER'}],
    'PHASE','BASS  PHASER',[244,236,220]);
  P.bassfilterecho = boxSpec(320,470,[96,58,42],
    [{id:0,cx:.20,lbl:'TIME'},{id:1,cx:.40,lbl:'FEEDBACK'},{id:2,cx:.60,lbl:'MIX'},{id:3,cx:.80,lbl:'FILTER'}],
    'ECHO','TAPE  ECHO',[212,176,104]);
  P.bassenbig = boxSpec(320,470,[58,64,72],
    [{id:0,cx:.20,lbl:'RATE'},{id:1,cx:.40,lbl:'DEPTH'},{id:2,cx:.60,lbl:'MIX'},{id:3,cx:.80,lbl:'FILTER'}],
    'ENBIGGEN','MOD  FILTER',[110,210,224]);
  // Bass MultiComp — EBS MultiComp (Blue Label): BLACK body with blue accent
  // lines across the bottom; stylised 'MultiComp' logo (big C…P flanking a
  // stacked MULTI/OM) under the knobs, EBX above the footswitch, blue lines
  // running behind EBX + footswitch. RS params (3 knobs): Compress0 Filter1 Rate2.
  P.bassmulticomp = { w:300,h:470,
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
      // stylised 'MultiComp': wide — big C … P; MULTI (up top) + OM both
      // letter-spaced to fill the gap between C and P
      textSpaced(d,.50*W,.460*H,F.barlow,19,w,'MULTI',18);
      textC(d,.24*W,.530*H,F.anton,78,w,'C');
      textSpaced(d,.50*W,.555*H,F.anton,48,w,'OM',54);
      textC(d,.76*W,.530*H,F.anton,78,w,'P');
      // blue accent lines at the bottom — thick + short (behind EBX + footswitch)
      c.strokeStyle='rgba(46,124,228,0.92)'; c.lineWidth=8;
      for(let i=0;i<7;i++){ const y=(.63+i*0.05)*H; c.beginPath(); c.moveTo(W*0.18,y); c.lineTo(W*0.82,y); c.stroke(); }
      textC(d,.50*W,.75*H,F.ink,30,w,'EBX');     // ink/marker-style brand logo
      footRound(d,W*0.5,H*0.86,22); } };

  // Dyna Compress — Dyna Comp-style optical compressor. MXR-inspired look
  // (red box + cursive logo) recreated, not branded. Param order: Comp0 Attack1 Release2.
  P.dynamicscompression = { w:300, h:460, knobs:[
      {id:0,cx:.25,cy:.235,r:.080,style:'davies',cap:[26,26,28]},
      {id:1,cx:.50,cy:.235,r:.080,style:'davies',cap:[26,26,28]},
      {id:2,cx:.75,cy:.235,r:.080,style:'davies',cap:[26,26,28]}],
    tick:rgb(122,30,28), ptr:rgb(242,236,224),
    draw(d){ const {W,H,s}=d; box(d,172,48,44); const cream=rgb(242,236,224);
      const ly=(.235+0.080*1.45+0.014)*H;
      textSpaced(d,.25*W,ly,F.barlow,10,cream,'COMP',0.6);
      textSpaced(d,.50*W,ly,F.barlow,10,cream,'ATTACK',0.6);
      textSpaced(d,.75*W,ly,F.barlow,9,cream,'RELEASE',0.4);
      // Cursive logo, staggered down-right like the original's script.
      textC(d,.46*W,.565*H,F.crete,44,cream,'Dyna');
      textC(d,.56*W,.665*H,F.crete,40,cream,'Compress');
      ledDot(d,W*.5,H*.775,true,224,60,50);
      footRound(d,W*.5,H*.885,23*s); } };

  // Holy Spring — Holy Grail-style spring reverb. Bright chrome box + ornate
  // serif logo (EHX-inspired, recreated brand-free). Params: Time0 Mix1 Depth2.
  P.springreverb = { w:300, h:470, knobs:[
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
      footRound(d,W*.5,H*.760,24*s); } };

  // Deja Chorus — Fulltone Deja'Vibe-style: matte-black landscape box, white
  // pinstripe border + script logo, two top knobs + two mode toggles, a big
  // offset knob + BYPASS stomp + blue LED. Recreated brand-free. Rate0 Depth1 Mix2.
  P.chorus20 = { w:480, h:300, knobs:[
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
      // white script logo + parody model code
      textC(d,.375*W,.560*H,F.crete,40,wt,"Deja Chorus");
      textC(d,.585*W,.655*H,F.barlow,12,wt,'DC-1');
      // big MIX knob label + blue status LED
      textSpaced(d,.825*W,.815*H,F.barlow,9,wt,'MIX',0.6);
      ledDot(d,W*.655,H*.610,true,70,150,234);
      // bypass footswitch
      footRound(d,W*.205,H*.730,20*s);
      textSpaced(d,.205*W,.910*H,F.barlow,8,wt,'BYPASS',0.6); } };

  // Eden WTDI — landscape gold-panel bass preamp (mirrors eden_wtdi/EdenWtdi_ui.cpp).
  // Param order: Gain0 Enhance1 Comp2 Master3 Bass4 Mid5 Treble6 BassBoost7 MidShift8.
  P.edenwtdi = { w:560, h:360,
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
  P.autosweep = { w:560, h:340,
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
  P.eq8     = eqSpec({ w: 320, h: 500, style: 0, db: 15, col: [188, 190, 186], label: 'Equalizer', code: 'GE-8',
                       bands: ['50', '100', '200', '400', '800', '1600', '3200', '6400'] });
  P.basseq8 = eqSpec({ w: 320, h: 500, style: 0, db: 15, col: [210, 206, 194], name1: 'Bass', name2: 'Equalizer', code: 'GEB-8',
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
    let drag = -1, lastY = 0, dv = 0;
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
      if (drag < 0) return;
      if (spec.eq) { const p = toSpec(e.clientX, e.clientY); const v = G.yToVal(p.y);
        values[drag] = v; drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(drag, v); return; }
      const dy = lastY - e.clientY; lastY = e.clientY; dv = clamp(dv + dy / 170, 0, 1);
      const id = spec.knobs[drag].id; values[id] = dv;
      drawSpec(canvas, spec, values); if (opts.onChange) opts.onChange(id, dv);
    });
    window.addEventListener('mouseup', () => { drag = -1; });
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
