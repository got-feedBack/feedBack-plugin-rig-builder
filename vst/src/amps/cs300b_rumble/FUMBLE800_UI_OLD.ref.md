# Old "Bender Fumble 800" UI (Fender Rumble 800 Class-D) — saved for future reuse

The cs300b_rumble slot was repurposed (2026-06-24) from a panel-only Fender Rumble
800 (Class-D) model to a circuit-real Fender Rumble Bass (1995, all-tube dual-channel).
If a real Rumble 800 is added later (own slot), reuse these two UI faces. The 800
panel = Gain, Bright/Contour/Vintage buttons, Overdrive Drive+Level, 4-band EQ
(Bass/Low Mid/High Mid/Treble), Master (params ids 0..10).

- DPF plugin UI: see `Fumble800_ui.cpp.ref` in this folder.
- In-app canvas face (pedal_canvas.js, `P.benderfumble800`): block below.

```js
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
```
