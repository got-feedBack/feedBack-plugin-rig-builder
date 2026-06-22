# Guía: amps de tubo "circuito-real" estilo Guitarix

Cómo construimos amps que **simulan el circuito real** (componente a componente), en
vez del viejo enfoque caja-negra (`tanh`). Es la arquitectura de **Guitarix** + **nuestras
tablas y nuestro código**. El piloto terminado es **BOX AC30** (`en30/`, Vox AC30 Top
Boost). Esta guía es para portar el resto de amps al modelo circuito-real.

> **Estado actualizado (2026-06-22):** BOX AC30 sigue siendo la plantilla completa, pero ya
> hay varios ports circuito-real adicionales en `vst/src/amps/`. La cola activa de Nacho
> excluye los `Bass_Amp_*`; los amps de bajo quedan para el hermano. `Amp_AT20` es un caso
> especial: aunque vive bajo `Amp_`, el mapeo local lo identifica como Ampeg SVT-CL y comparte
> `SamplegSBTCL` con `Bass_Amp_BT975B`, por lo que se trata como bajo salvo excepcion explicita.

---

## 0. Licencia — leer primero

Guitarix es **GPL-2.0**. **NO copiamos su código fuente** (ni sus `.dsp`, ni su `valve.h`,
ni sus tablas). Copiar eso obligaría a liberar TODO nuestro proyecto como GPL.

Lo que SÍ usamos, y es legal, es la **física y la técnica**, que no son copyrightables:
- **Modelo de Koren** del tubo → publicado por Norman Koren (dominio público).
- **Constantes SPICE** de cada tubo → datasheets / modelos públicos (§9).
- **Solver de línea de carga** (Yeh/Karjalainen) → papers académicos (DAFx).
- **Topología del circuito** (Rp, Rk, cátodo, tonestack) → es el amp real.

Regla práctica: leemos Guitarix para **entender la técnica**, y la reimplementamos con
**nuestro propio código** (`/tmp/gx_tube.py`, `tube_stage.hpp`). El número de un componente
(Rk=2700) o una constante de datasheet (mu=100) no es de Guitarix, es del amp/tubo real.

---

## 1. Arquitectura de un amp

Cadena de señal (mono core, corre a 2× oversampling = 96 kHz, ver `_shared/oversampler.hpp`):

```
in → input coupling (HP) → [PREAMP: N etapas de tubo con pre-gain entre ellas]
   → TONESTACK → POWER AMP → speaker/cab voicing → (tremolo) → out
```

- **Oversampling 2×** (no 4×): a 4× los cores se iban a 192 kHz e inestabilizaban (NaN).
  Guitarix corre a 96 kHz. Ver `oversampler.hpp` (`OS=2`).
- El amp se **voicea PRE-cab**: el cab IR añade el color del parlante/caja. El amp solo
  lleva su propio color de parlante suave (los biquads `spk*`).

---

## 2. El modelo de tubo = una tabla 1-D PURA

**La idea central** (y donde fallaba el enfoque viejo): la tabla del tubo es la
transferencia de placa **pura**, en **voltios reales**:

```
Ftube(Vgrid) → Vplaca      // Vgrid en volts (p.ej. −5..+5), Vplaca en volts (0..250)
```

**El cátodo NO está en la tabla.** El cátodo (auto-bias) se modela **afuera**, en el lazo
de la etapa (§3), con los valores físicos reales (Rk, Ck, Vk0). Si horneas el cátodo en la
tabla, ya no puedes modelar su dinámica → terminas parchando. (Ese fue el error de la v1.)

### Las ecuaciones (modelo Koren, público)

```
# corriente de placa de un triodo (Koren):
E1   = (Vpk/kp) · ln(1 + exp(kp·(1/mu + Vgk/√(kvb + Vpk²))))
Ipk  = 2·E1^ex / kg1            (si E1>0, si no 0)

# conducción de rejilla (grid current), estándar:
Igk(Vgk) = exp(7.75·Vgk − 10.3)
```

### El circuito que resuelve la tabla (por cada Vgrid de entrada)

```
1) KVL en la rejilla:   Vi = Vgk + Ri·Igk(Vgk)            → Newton para Vgk
2) Línea de carga:      Vsupply = Vpk + Rp·Ipk(Vgk,Vpk)   → Newton para Vpk
   Ftube = Vpk
```

- `Ri` = resistencia de fuga de rejilla (grid-leak). Se generan **dos tablas**: Ri=68k y
  Ri=250k (las dos posiciones típicas). El amp elige cuál usa cada etapa.
- `Rp` = resistencia de placa (100k en preamp de 12AX7).
- `Vsupply` = B+ (250 V típico preamp).

**Tabla `Ranode`** (segunda tabla): `dVp/dIp` = resistencia de placa dinámica. Se usa para
escalar correctamente el feedback de cátodo (§3).

### Generarla: `/tmp/gx_tube.py`

Nuestro generador (no el de Guitarix). Produce `_shared/koren12ax7_ftube.h` con:
`AX7_ftube(int tab, float v)` y `AX7_ranode(int tab, float v)` (tab 0=Ri68k, 1=Ri250k).

Sanity check del 12AX7 (debe dar esto, es la curva real):
```
Vplaca @ reposo (Vgrid=0)  ≈ 110 V
Vplaca @ Vgrid=−3 (corte)  ≈ 243 V   (tubo casi cortado → placa sube al B+)
Vplaca @ Vgrid=+3 (conduce)≈  87 V   (grid conduction comprime)
```
La **asimetría** (comprime arriba, se expande al corte) **es** la distorsión del tubo.

---

## 3. La etapa: `rbtube::TubeStage` (`_shared/tube_stage.hpp`)

Una etapa = un 12AX7 con cátodo. Topología idéntica al `tubestageF` de Guitarix, código nuestro:

```cpp
// process(): x = señal de rejilla en VOLTIOS
x = antiAlias(x);                              // LP anti-alias
float u = x + vk;                              // rejilla + realimentación de cátodo
float s = Ftube((u) − Vk0) + (VkC − vplus);    // voltaje de placa recentrado a ~0
vk = cathodeLP(s · kFb);                        // cátodo del próximo sample (filtrado por Ck) — 1 sample delay
return dcBlock(s / divider);                    // baja de "voltios de placa" a señal
```

Parámetros de `set(sr, Ri_tab, vplus, divider, fck, Rk)`:
- **`Vk0`** (voltaje de cátodo en reposo) se **resuelve** con Newton:
  `(Ftube(−v) − vplus)·(Rk/Rp) + v = 0`. Validación: Rk=2700 → **Vk0=1.582** (= 1.58 de
  Guitarix ✓). Es el auto-bias real de la etapa.
- **`VkC` = Vk0·(Rp/Rk)** — recentra la salida.
- **`kFb` = Rk/(Rp + Ranode)** — convierte corriente de cátodo en voltaje de realimentación
  (el factor real del divisor). Pequeño (~0.01–0.02) → el lazo es estable, no oscila.
- **`fck`** = corte del cátodo-bypass cap. Derivar del esquemático: **fck = 1/(2π·Rk·Ck)**.
- **`divider`** = 40 (baja el swing de placa ~±100 V a señal ~±2.5).
- **`vplus`** = 250 (B+).

**Por qué suena a amp de verdad subiendo el gain:** cada etapa satura contra su propia línea
de carga, y el lazo de cátodo desplaza el bias con el nivel (compresión + asimetría que
"respira"). La meseta al máximo **emerge** de eso, no de una curva ad-hoc.

### Miller / capacitancia de entrada por tubo

Guitarix no resuelve una capacitancia Miller dinámica dentro de `tubestageF`; la aproxima
en sus amps Faust con low-pass fijos entre etapas, típicamente `fi.lowpass(1,6531.0)`.
Nuestro framework ahora tiene `rbtube::MillerLowPassT<TUBE>`:

```
Cin ~= Cgk + Cgp * (1 + |Av|)
fc  = 1 / (2*pi*Rsource*Cin)
```

Esto convierte la capacitancia real del tubo y la resistencia que lo maneja en el techo de
agudos de la etapa. En `BOX AC30`, los antiguos low-pass fijos entre etapas fueron cambiados
por `Miller12AX7` usando `Cgp/Cgk` del 12AX7: entrada ~22 kHz, V2 ~7 kHz y V3 ~8 kHz.

Capacitancias auditadas contra los PDFs locales en `tubes/`:

- `12AX7A.pdf` (RCA): Cgk = 1.6 pF, Cgp = 1.7 pF.
- `12AY7.pdf` (Tung-Sol): Cgk = 1.3 pF, Cgp = 1.3 pF.
- `EF86.pdf` (Jan 1970): Cg1(all except anode) = 3.8 pF, Cag1 max = 0.05 pF.

Si el amp usa un tubo que no esta en `tubes/`, no inventar el Miller ni la tabla: agregar el
datasheet primero. El `DC30` usa `TubeStageEF86` en su canal 2, con tabla Koren pentodo
generada desde `EF86.pdf` y el load-line real del DC30: placa 330k, catodo 2k2 + 25uF.

Ganancia medida de una etapa: **~1.38× lineal**, comprime sobre **±1.5 V** de rejilla
(usar `/tmp/stagegain.cpp` para medir al portar otros tubos).

### Bloques avanzados reutilizables

El piloto `BOX AC30` ya usa los bloques nuevos; los otros amps deben ir migrando caso a caso,
no por copy-paste ciego:

- `CouplingCapGridLeak`: capacitor de acople + grid-leak + conduccion de grilla. Modela
  blocking distortion y recuperacion entre etapas.
- `PhaseInverterLTP12AX7`: long-tail-pair configurable (`setVoxAc30`, `setMarshall`,
  `setFenderAB763`) antes del push-pull. Usar en Vox/Marshall/Fender AB763, con valores del
  esquematico.
- `PhaseInverterCathodyne12AX7`: split-load/cathodyne para 5E3/Princeton. No usar en AC30.
- `MultiNodeBPlus`: cadena power/screen/preamp para rectificador + filtros + dropping
  resistors. `setGZ34Ac30()` es el primer perfil; agregar perfiles por amp cuando toque.
- `PotTaper`: helpers `audio`, `reverseAudio`, `sCurve`, `switchBlend` para que los knobs de
  Rocksmith no se traduzcan como potes lineales cuando el amp real no lo hace.

---

## 4. Encadenar etapas (gain staging)

Estilo Guitarix: el control de volumen/gain es un **pre-gain LINEAL entre etapas**
(`*(preamp)`), no una curva de distorsión. Más volumen → más señal a cada tubo → satura en
cascada. En `BoxDC30Core::recalc()`:

```cpp
float vol = pow(pTBVol, 0.58);                 // pote logarítmico (audio taper) — el knee llega a mediodía
inScale   = 0.50·(0.7 + 0.6·pNVol);            // audio → voltios de rejilla en V1 (mantener V1 ~lineal)
preGain   = 0.65 + 1.45·vol;                   // pre-gain entre etapas (mete V2/V3 a clip)
gainOut   = 0.60 + 0.55·vol;                   // nivel post-V3 hacia el power amp
```

Cadena en `process()`:
```
in·inGain → coupling → bright → ·inScale → V1 → [tonestack] → couple12·preGain → V2
          → couple23·preGain → V3 → ·gainOut → cut → power → speaker → trem → ·outLevel
```

- `couple12/couple23` = LP(6531 Hz), el rolloff de acoplamiento entre etapas (Guitarix).
- **Calibrar el rango, no inventar la curva:** ajustar `inScale/preGain/gainOut` para que el
  crest factor por nivel matchee la referencia (§8). El piso de `preGain` controla qué tan
  limpio queda el gain bajo; el techo, qué tan saturado el gain alto.

---

## 5. Valores por etapa del AC30 (ejemplo trabajado, BOX AC30)

Los tres 12AX7 del Top Boost (mismos valores que Guitarix usa para el "12ax7"):

| etapa | tabla Ri | Rk | fck | vplus | divider |
|-------|----------|------|------|-------|---------|
| V1    | 68k (0)  | 2700 | 86   | 250   | 40      |
| V2    | 250k (1) | 1500 | 132  | 250   | 40      |
| V3    | 250k (1) | 820  | 194  | 250   | 40      |

Para otro amp: sacar `Rk`, `Ck` (→fck), si la rejilla es 68k o 250k, y cuántas etapas, del
**esquemático real**. El resto del modelo no cambia.

---

## 6. Tonestack — circuito real ✅ (`rbtube::ToneStackYeh`)

Modelo de **D.T. Yeh** (Yeh & Smith, DAFx-06 — académico/público): la función de
transferencia analógica de **3er orden** del circuito R/C de 3 bandas, calculada de los
componentes + las posiciones de los pots (t,m,l), discretizada por **bilinear**. En
`tube_stage.hpp`. Verificado: la respuesta digital == la analógica a **<0.01 dB**
(`/tmp/tonestack.py`). Reusable: `setComponents(R1..R4, C1..C3)` con los valores del amp.

Valores por amp en `guitarix-master/trunk/src/faust/tonestack.dsp` (R1–R4, C1–C3):
bassman, mesa, twin, **ac15 (Vox)**, marshall (`mlead`), soldano, engl, princeton, etc.
Vox: R1=R2=R3=220k, R4=100k, C1=470pF, C2=100nF, C3=47nF.

**Dos lecciones clave (valen para otros amps):**
1. **Mapeo de pots según el amp.** Con los valores Vox, el nodo "bass" (l) del TMB está
   casi inactivo (la perilla no hace nada), mientras que el "mid" (m) controla el
   cuerpo/scoop. El AC30 tiene Treble + Bass (sin Mid), así que mapeamos **Treble→t**,
   **Bass→m** (es el control de cuerpo real del circuito), y fijamos **l=0.5**. Cada amp:
   ver qué nodo está activo con sus valores y mapear las perillas reales a esos.
2. **NO calibrar el tonestack contra el render-con-parlante.** El tonestack solo es una
   pieza; el render de referencia (Ruby) incluye el parlante. Comparar el amp-pelado vs
   Ruby da una diferencia grande (≈±8 dB: faltan graves, sobra low-mid, falta brillo) que
   es **el parlante**, NO un error del tonestack. Como el amp se vozea PRE-cab (§1) y el
   cab IR aporta esa curva, **no metas el scoop/realce del parlante en el amp** (se
   duplicaría con el cab). Además, un EQ fuerte post-distorsión **infla el crest factor**
   (parece más limpio) → enmascara la medición de distorsión. Mantené el voicing de
   parlante suave/pre-cab y validá la distorsión con el crest ANTES de ese EQ.

---

## 7. Power amp — circuito real ✅ (`rbtube::PowerAmpPP`)

Push-pull class-A EL84 (AC30), real. Tabla **pentodo pura** `EL84_ftube(Vg)` generada por
`gx_tube.py` (Koren pentodo forma atan estable; EL84 mu21.3/ex1.24/kg1 401.7/kp111.1/kvb17.9,
B+300, carga 2k, grid-leak 220k). Sanity: Vp cutoff(Vg−20)=297, bias(−8)=233, conduce(0)=149,
grid-cond(+4)=146. El amp = **diferencial** `EL84_ftube(b+x) − EL84_ftube(b−x)` (el OT suma
las dos placas anti-fase) + **sag** (envolvente lenta — físicamente la descarga de los caps;
también empuja el bias a clase AB bajo drive = compresión AC30) + **OT** band-pass (HP 70 Hz
inductancia primaria, LP 12 kHz fuga). **SIN realimentación negativa** — Guitarix SÍ usa NFB
global en su `gxpoweramp.dsp`, pero el Vox no tiene → ese es su carácter crudo/chime.

**Tres lecciones (valen para otros amps):**
1. **El push-pull cancela armónicos PARES** → tiende a sonar MÁS limpio (sube el crest) que
   una etapa single-ended. Para que la etapa de potencia aporte breakup hay que **excitarla
   fuerte** (drive alto). El AC30 no tiene master: el VOLUMEN cocina el power amp → el drive
   debe escalar con el volumen (`6+9·vol`), no ser fijo.
2. **El bare-amp tiene el crest un poco más alto que el Ruby-con-parlante** (el parlante del
   Ruby comprime/rompe y baja el crest; nuestro cab IR lo aporta aparte). No persigas el
   crest del Ruby exactamente con el amp pelado — apuntá un poco por encima.
3. **El reparto de distorsión preamp↔power se recalibra.** Cuando agregás el power amp real,
   el preamp ya hacía toda la dirt → hay que rebalancear (más breakup de potencia a volumen
   alto) para que el TOTAL siga matcheando. Medí crest a gain 2/5/10 y ajustá `drive` + `out`.

Class A (AC30/Vox, bias caliente ~−7.5, sin NFB) vs AB (Marshall/Fender, bias frío + NFB):
cambia el bias, el sag y si hay NFB. BOX AC30 vs Ruby: crest 18.5/13.8/11.2 (limpio→rompe→
mesetea), estable (sin NaN, picos 0.35 @48/96/192k).

---

## 8. Calibración objetiva (NO afinar solo de oído)

Workflow para igualar a una referencia comercial (ver [[amp-reference-renders]]):

1. Render del **DI estándar** (`ui_public_inputs_Brit - Guitar.wav`) por nuestro core, offline.
2. Comparar contra el **render comercial** (p.ej. UAD Ruby = AC30) las tres métricas:
   - **crest factor** (pico/RMS, dB) = proxy de distorsión. Bajo = más saturado.
   - **RMS** = nivel (para mantener volumen constante entre gains).
   - **espectro por bandas de octava** = voicing/EQ.
3. Matchear a los **tres niveles de gain** (la referencia tiene gain 2/5/10).

Lecciones:
- Un **DI de guitarra no mide 4–8 kHz** (no hay energía ahí) → para la curva de EQ del amp,
  usar **ruido blanco** por el core (`/tmp/wn.cpp`), no el DI.
- Para el **feel del gain**, sí usar el DI (crest factor por nivel).
- Settings del Ruby de referencia: Treble 5 / Bass 5 / **Cut 3** (=0.3). Usar exactamente esos.

Harnesses (en `/tmp`, regenerar si se borran):
- `gx_tube.py` — genera las tablas.
- `gain.cpp` — DI → core @ gain 2/5/10 → crest+RMS vs `uadruby_vol_top_{2,5,10}.wav`.
- `stab.cpp` — estabilidad (NaN/picos) a 48/96/192k, todo al máximo. **Siempre correrlo.**
- `stagegain.cpp` — ganancia/compresión de UNA etapa (calibrar tubos nuevos).
- `wn.cpp` — curva de EQ con ruido blanco.

BOX AC30 hoy: crest 16.8/13.5/11.4 vs Ruby 17.5/12.0/12.1 (≤1.5 dB), estable.

---

## 9. Constantes Koren por tubo (datasheet público)

Para portar otros amps necesitas las constantes de SUS tubos. (mu, ex/kx, kg1, kg2, kp, kvb):
Los PDFs base viven en la carpeta `tubes/` en la raiz del proyecto; cuando exista datasheet
local, usar ese como fuente antes de caer a una tabla generica.

**Triodos preamp** (Vsupply 250, Rp 100k, Vgrid −5..5):
| tubo   | mu    | ex   | kg1    | kp    | kvb   |
|--------|-------|------|--------|-------|-------|
| 12AX7  | 100.0 | 1.4  | 1060.0 | 600.0 | 300.0 |
| 12AT7  | 60.0  | 1.35 | 460.0  | 300.0 | 300.0 |
| 12AU7  | 21.5  | 1.3  | 1180.0 | 84.0  | 300.0 |
| 12AY7  | 44.16 | 1.11 | 1192.4 | 409.96| 300.0 |
| 6DJ8   | 28.0  | 1.3  | 330.0  | 320.0 | 300.0 |
| ECC83  | 98.1  | 1.46 | 1734.7 | 754.4 | 119.9 |

**Pentodos power** (kg2; Vsupply/Rp/Vgrid según tubo, ver tabla derecha):
| tubo  | mu    | ex   | kg1   | kg2   | kp    | kvb  | Vsupply | Rp    | Vgrid |
|-------|-------|------|-------|-------|-------|------|---------|-------|-------|
| EL84  | 21.3  | 1.24 | 401.7 | 4500  | 111.1 | 17.9 | 370     | 3.5k  | −10..10 |
| EL34  | 12.3  | 1.17 | 353.9 | 4500  | 61.1  | 29.9 | 495     | 3.5k  | −20..20 |
| 6L6   | 8.7   | 1.35 | 1460  | 4500  | 48.0  | 12.0 | 430     | 2.0k  | −80..6 |
| 5881  | 8.7   | 1.26 | 1210.3| 4500  | 47.5  | 11.6 | 430     | 2.0k  | −80..6 |
| 6L6GC | 8.7   | 1.35 | 1460  | 4500  | 48.0  | 12.0 | 450     | 1.4k  | −80..8 |
| KT66  | 11.7  | 1.98 | 510.9 | 4500  | 34.9  | 22.3 | 470     | 1.65k | −82..8 |
| 6V6   | 12.67 | 1.198| 915.0 | 4500  | 38.07 | 30.2 | 370     | 3.5k  | −10..10 |
| 6550  | 7.9   | 1.35 | 890.0 | 4800  | 60.0  | 24.0 | 450     | 5.0k  | −21..21 |
| KT88  | 8.8   | 1.35 | 730.0 | 4200  | 32.0  | 16.0 | 450     | 5.0k  | −21..21 |

Pentodo conectado clase-A (screen a placa): `Ipk = 2·E1^ex/kg1 · atan(Vpk/kvb)`, con
`E1 = (Vpk/kp)·ln(1+exp(kp·(1/mu + Vgk/Vpk)))`.

---

## 10. Receta para un amp nuevo

1. **Reunir datos** del amp real: cuántas etapas de tubo y qué tubos; por etapa `Rk`, `Ck`
   (→ `fck=1/(2π·Rk·Ck)`), rejilla 68k/250k, `Rp`, `Vsupply`; tipo de tonestack; tubo de
   potencia y clase (A/AB). Conseguir un **render de referencia** (UAD u otro) para calibrar.
2. **Tabla de tubo:** si es un tubo nuevo, agregar sus constantes a `gx_tube.py` (§9) y
   regenerar `koren<tubo>_ftube.h`. (12AX7 ya está.)
3. **Core nuevo** `XxxCore.h`: N× `rbtube::TubeStage` con los valores de §5, pre-gain (§4),
   tonestack, power, voicing. Copiar `BoxDC30Core.h` como plantilla.
4. **Plugin:** apuntar `XxxPlugin.cpp` al nuevo core; mapear los params del panel real.
5. **Calibrar** (§8): `python3 vst/src/amps/tools/calibrate_amp_core.py <amp>` cuando el core
   este soportado; si no, crear el spec del amp ahi. Revisar estabilidad, crest/THD por gain y
   puntos de espectro. Ajustar rangos, no inventar curvas.
6. **Build + firmar:** `make -C <dir> BASE_PATH=<dir>`, copiar `bin/Xxx.vst3` a
   `vst/amps/<NOMBRE>.vst3`, `codesign --force --sign -`. Cmd+Q + reabrir Slopsmith para probar.

---

## 11. Estado por amp / roadmap

- **BOX AC30 (`en30`)** — ✅ **piloto circuito-real avanzado** (preamp + tonestack + blocking + PI + power + carga reactiva). Plantilla de referencia. Desde 2026-06-22 reemplaza los low-pass fijos tipo Guitarix entre etapas por Miller calculado (`Miller12AX7`: Cgk/Cgp + resistencia de fuente + ganancia estimada) para la entrada, V2 y V3. Desde esta prueba suma `CouplingCapGridLeak` (grid-current/blocking en V2/V3/PI), `PhaseInverterLTP12AX7::setVoxAc30`, `MultiNodeBPlus::setGZ34Ac30`, `PotTaper` y `Ac30ReactiveOutput` (OT + resonancia 2x12 + compresion termica). Harness: `python3 vst/src/amps/tools/calibrate_amp_core.py en30`.
- **Bender Deluxe (`tw26`, 5E3)** — ✅ preamp (12AY7+12AX7) + tono **circuito-real** (`rbtube::TweedTone`, control 1-perilla R10 1M/C4 500pF/C5 .0047µF del esquemático 5E3) + power (6V6 PP, sin NFB). Calibrado a oído vs Woodrow.
- **Super-Sonic 22 (`tw22`)** — ✅ Vintage (1×12AX7) + Burn (3×12AX7 cascada) + tonestacks **circuito-real con valores EXACTOS del esquemático** (`ToneStackYeh`): Vintage TMB 250k/250k/6.8k-fijo/100k · 250pF/.1µF/.047µF (mid fijo m=1); Burn TMB 250k/250k/25k/120k · 150pF/.15µF/.022µF. Power 6V6 PP; morph 2-canales. Crest matchea `supersonic_g1/g2` (±1.2 dB). PENDIENTE probar en vivo. (PDF legible a alta resolución con `pdftoppm`/fitz — recortar por zonas.)
- **Bassman (`tw40`, 5F6-A)** — ✅ 2×12AY7 (Bright/Normal) + 12AX7 + tonestack FMV (ya era real) + `PowerAmp5881` PP. Crest matchea `bassman_vol_4/12` (15.3/8.5 vs 15.3/9.1). PENDIENTE probar en vivo.
- **Marshall JTM45 / Bluesbreaker (`jtm45_marsten`, `bluesbreaker_marsten`)** — usan la rama 5881/6L6 de 30W con GZ34, no la tabla KT66. En DSP quedan con `PowerAmp5881`; reservar `PowerAmpKT66` para BT45 u otros esquemas que confirmen KT66.
- **Marshall Plexi (`plexi`, 1959 Super Lead)** — ✅ 2×12AX7 (High-Treble/Normal) + 12AX7 recovery + tonestack Marshall FMV (ya era real) + power (EL34 PP). Core inline en `PlexiPlugin.cpp` (no header). Crest matchea `plex_vol1_*` (9.5/4.9/3.0 vs ref 7.6/4.3/3.6 — ref MUY distorsionada). PENDIENTE probar en vivo. ⚠️ lección: al portar, el `cleanMakeup` viejo (×N a bajo drive, para el tanh limpio) desincroniza con los tubos reales → reducirlo; y `pushed` (smoothstepRange) controla CUÁNDO satura — corrérlo temprano si el ref rompe temprano.
- **Marshall JCM800 (`jcm800_marsten`, 2204 master-volume)** — ✅ 3×12AX7 cascada (V1a→GAIN→V1b→V2) + tonestack Marshall TMB (ya era real: Treble 220k/Bass 1M/Mid 25k/slope 33k · 470pF/22nF/22nF) + power (EL34 PP). Core inline en `Jcm800Plugin.cpp` (estéreo L/R), oversampling 2× + 3.2× input agregados. Crest matchea `jcm800_preamp_1/4/7/10` (13.1/9.6/8.4/7.6 vs ref 12.8/7.3/6.9/6.7 — g1 casi exacto, medio ~2 dB más limpio). PENDIENTE probar en vivo. ⚠️ misma lección Plexi + dos más: (a) el `cleanMakeup` viejo INVIERTE la curva de crest (mete el tono limpio en el `softClip` de salida) → eliminarlo, dejar solo `softClip(y*level)` suave como saturación de OT; (b) el voicing de parlante viejo traía boosts irreales (+16.5 dB fizz) que inflan el crest sin distorsionar → un cab real ATENÚA los agudos (shelf negativo + LP ~11 kHz).
- **Mesa Dual Rectifier (`dual_rect`, 3-channel Solo Head)** — ✅ el más complejo: 3 canales (Green/Orange/Red) × 3 modos (Raw/Vintage/Modern) + Rectifier (Spongy/Bold). Cadena única reconfigurada por canal activo (solo uno suena a la vez). Topología del esquemático (BLOCK DIAGRAM + PREAMP PT1/PT2): **V1a compartido** → Green: tonestack→V1b (limpio, 2 etapas); Orange/Red: **V2a→V2b→V3a→V3b** (4 etapas cascada)→tonestack (post-dist). Power **4×6L6GC PP** con tabla dedicada; el Rectifier mapea directo a `power.sagDepth` (Bold/silicio=tight, Spongy/5U4=saggy) → eliminé el `RectoSupply` aparte. Tonestacks **circuito-real del esquemático**: Clean (CH1) Treble 250k/250pF·Bass 250k/.1µF·Mid 25k/.047µF·slope 100k (≈Mesa Mark); Recto (CH2/CH3) Treble 250k/500pF·Bass 1M/.02µF·Mid 25k/.02µF·slope 47k. Crest vs `dual_ch*`: **Red Modern g10 = 8.9 vs 9.1 (default, exacto)**; Orange/Red vint/modern g10 todos ±1 dB; Green limpio. PENDIENTE probar en vivo. ⚠️ lecciones nuevas: (a) el crest del canal LIMPIO está dominado por el cab-scoop + power-amp floor, no por el preamp → bajar el piso de `power drive` (con fuerte dependencia de `chHot`) para que limpie; (b) ladder de modos Raw<Vint<Modern (no al revés); (c) los pisos de drive de las etapas TARDÍAS del cascade deben escalar con el knob de Gain (pot va ANTES del cascade) para que limpie a bajo gain sin tocar el match de alto gain.
- **Marshall DSL100H (`dsl100`, JCM2000 Dual Super Lead)** — ✅ 2 canales (Classic Clean/Crunch + Ultra OD1/OD2) en una cadena con morph paralelo (el knob de Gain de RS barre Classic clean→crunch→Ultra). 6× `TubeStage` 12AX7 (V1 compartido + paths clean/crunch/ultra + cascade extra) + `PowerAmpEL34` (4×EL34) + tonestack **circuito-real Yeh con valores JCM2000** (Treble 250k/500pF · Bass 1M/22nF · Mid 25k/22nF · slope 56k). Tone Shift (mid-scoop), Presence/Resonance (NFB), Low/High → `sagDepth`. Agregué oversampling 2× (no lo tenía). **SIN render de referencia** → calibrado por carácter+topología (crest monotónico Classic clean 13.8 → crunch 12 → Ultra 7.9, estable). PENDIENTE probar en vivo. ⚠️ mismas lecciones: piso bajo de `power drive` para que el limpio quede limpio; `cleanMakeup` reducido (invertía la curva); cab fizz +9.5 dB → atenúa.
- **Mesa Mark III (`mark_iii`) + Mark II (`mark_ii`)** — ✅ los amps "lead" Boogie (Santana/Metallica). 2 voces en una cadena: RHYTHM (Volume→Master) y LEAD (Lead Drive cascada→Lead Master), elegidas por el switch. 4× `TubeStage` 12AX7 (V1 + rhythm + 2 cascada lead) + `PowerAmp6L6GC` (Simul-Class ~75-100W). El **tonestack ya era circuito-real** (clase propia `MarkToneStack`) PERO en float → **NaN a 192k**; lo cambié a `rbtube::ToneStackYeh` (double, mismos valores: Treble 250k/250pF · Bass 1M/22nF · **Mid 10k** scoopeado/22nF · slope 100k). Mark III tiene el **graphic EQ de 5 bandas** (80/240/750/2200/6600, la "V" Boogie) ya real; Mark II tiene pulls Shift/Bright/GainBoost/HalfPower + reverb spring. Agregué oversampling 2×. **Sin render de referencia** → por carácter: crest monotónico Rhythm 18.3 limpio → Lead 9.8 (III) / 10.0 (II — un pelo menos searing, correcto). PENDIENTE probar en vivo. ⚠️ **LECCIÓN NUEVA: un tonestack de 3er orden en `float` se va a NaN a 192k (= 96k host × 2× OS) — usar siempre `ToneStackYeh` (double) para los TMB de 3 bandas.**
- **Laney AOR 50 (`aor50`, A50 Series II "Pro Tube Lead", = el RS Amp_GB100)** — ✅ **HÍBRIDO tubo+diodo**. 2 canales (Channel One limpio + AOR lead) en morph (RS Gain). Del esquemático `Laney_aor50_series2.pdf`: 5× ECC83 (V1B/V2A/V2B/V3A/V3B) + **clipper de diodos 1N4148** (D3/D4 en torno a un op-amp = el "Advanced Overdrive Response") en el path del lead + power EL34 (rectificador silicio = tight). Porté con `TubeStage` (los 5) + `DiodeClipper` (el mismo Shockley del JC, en serie tras el cascade del AOR) + `ToneStackYeh` con valores reales del esquemático (Treble 220k/470pF · Bass 1M/22nF · Mid 22k/22nF · slope 33k = stack JCM800) + `PowerAmpEL34`. Pull-Deep/Mid-Boost como shelves. Oversampling 2×. **Sin render de referencia** → por carácter: crest Channel One 12.6 (limpio rock) → AOR lead 7.4 (crunch duro tubo+diodo), monotónico, estable. PENDIENTE probar en vivo. ⚠️ **el diode clipper en serie tras los tubos = cómo se modela un overdrive híbrido** (no todo es tubo; el carácter agresivo del AOR es el diodo).
- **Framework `tube_stage.hpp`:** templates `TubeStageT`/`PowerAmpPPT` + traits. Tubos con tabla generada: 12AX7, 12AY7, EL84, 6V6, 6L6 legacy, **5881**, **6L6GC**, **KT66**, EL34. EQ circuito-real: `ToneStackYeh` (TMB de 3 bandas, R/C de cualquier amp) + `TweedTone` (control 1-perilla 5E3). Bloques avanzados: `CouplingCapGridLeak`, `PhaseInverterLTP12AX7`, `PhaseInverterCathodyne12AX7`, `MultiNodeBPlus`, `PotTaper`. Distorsión solid-state/diodo: `DiodeClipper` (Shockley, Newton — JC90/JC120/AOR50). **Los 11 amps tienen el EQ circuito-real** (BOX AC30/Bassman/Plexi/JCM800/DualRect/DSL100/MarkII/MarkIII/AOR50 = FMV/TMB; Deluxe = TweedTone; Super-Sonic = ToneStackYeh ×2). Agregar un tubo = constantes Koren en `gx_tube.py` + traits.
- **Hiwatt DR103 (`dr103_lovolt`, = RS Amp_HG100) + DR504 (`dr504_lovolt`, = RS Amp_HG500)** — ✅ los amps **LIMPIOS** de alto headroom (Townshend/Gilmour), parody "Lovolt" (Hi-watt→Lo-volt). DR103 = 100W 4×EL34, DR504 = 50W 2×EL34, mismo preamp 3×ECC83 (Normal + Brilliant jumpereables). Ya tenían `HiwattToneStack` real PERO en **float → NaN a 192k** (igual que el Mark) → cambiado a `ToneStackYeh` (double): Treble 250k/250pF · Bass 500k/22nF · **Mid 100k/22nF** (el pot de medios grande = los medios fuertes Hiwatt) · slope 56k. 3× `TubeStage` (vBright/vNormal/vRecovery) + `PowerAmpEL34` (sag MUY bajo = stiff supply) + 2× OS. **Sin render** → por carácter: se mantiene LIMPIO (crest ~16→14, rompe tarde) — eso ES el Hiwatt; el RS Gain→Brilliant Vol da una progresión sutil. PENDIENTE en vivo. ⚠️ lección: un amp de alto headroom su crest lo domina el cab/normalización, NO el preamp — no forzar breakup; quedaría irreal. (HG100/HG500 estaban mal curados como Peavey en rs_to_real — son Hiwatt, ya mapeados a LovoltDR103/DR504.)
- **Budda/Ganddi SuperDrive 45 (`superdrive45`, = RS Amp_BT45)** — ✅ port circuito-real 2026-06-22. Local: `amps/Budda SuperDrive 45 (BTQ_45)/Superdrive45_manual.pdf` + `BuddaSuperdrive80Schematic.jpg` + datasheet Marconi `tubes/KT66.pdf`. 2 canales: Rhythm limpio/edge + Hi-gain cascado con Drive; pull Modern = scoop/lift en hi-gain; pull Brite = treble boost en Rhythm. Implementado con `TubeStage` 12AX7 por etapa (input, rhythm, 3 etapas lead, PI/driver), `ToneStackYeh` double con valores Budda TMB (Treble 500k/220pF, Bass 500k/22nF, Mid 50k/22nF, slope 56k), `PowerAmpKT66` propio (tabla Koren generada con punto Marconi intermedio: ~470V, ~6.6k a-a, bias ~-40V) con sag 5AR4, y oversampling 2×. PENDIENTE prueba en vivo y calibracion final por crest/RMS si aparece render de referencia. Nota: el esquematico local parece familia SD80, pero el manual BT45 confirma KT66 + selector tube/solid-state; el modelo sigue el power del 45.
- **Solid-state / híbridos:** JC90/JC120 (Roland Jazz Chorus) = solid-state puro (op-amp lineal + `DiodeClipper` + power lineal sin sag + chorus BBD), NO tubos. AOR50 = híbrido (tubos + `DiodeClipper`). Ver [[slopsmith-amp-dsp-vst-not-nam]].
- **ENGL/Engel Fireball (`engel_fireball`, = RS Amp_EN50)** — ✅ auditado en codigo el 2026-06-22: ya usa `tube_stage.hpp`, 12AX7 Koren, `ToneStackYeh`, `PowerAmp6L6GC` y oversampling 2× contra el esquematico local `amps/ENGL Fireball (EN-50)/`. El roadmap viejo lo marcaba como proximo pendiente; eso estaba desactualizado. PENDIENTE probar en vivo y documentar crest final.
- **Otros ports circuito-real que existen en codigo pero requieren auditoria/calibracion final:** `bluesbreaker_marsten`, `jtm45_marsten`, `jvm410_marsten`, `dsl15_marsten`, `or100_citrus`, `or50_citrus`, `chieftain_unparallel`, `dc30_unparallel`, `maz38_mry`, `ems_mry`, `jc90`, `jc120_ronald`, `vh140_sampleg`, `vs100_marsten`, `ua610_multiversal`. No asumir que estan terminados solo por compilar: revisar esquematico, knobs, loudness, crest y prueba en vivo.
- **Fuera de la cola de Nacho:** todos los `Bass_Amp_*` y `DI_Amp_BassDriver` quedan para la cola de bajo. `Amp_AT20` se trata igual porque el mapeo local dice que es **Ampeg SVT-CL** y reutiliza `SamplegSBTCL` con `Bass_Amp_BT975B`.
- **Pendientes de guitarra/DI por uso, excluyendo bajo:** `Amp_CS100` (Polytone Mini Brute), `Amp_OrangeAD50`, `Amp_OrangeTinyTerror`, `Amp_OrangeRockerverb`, `DI_Amp_MixerPre`, `Amp_OrangeJimmyBean`, `Amp_GibsonGA88`, `Amp_GibsonGA8`.
- **Patrón de port para los pendientes:** swap `triode/asymTube`→`TubeStage`, `power tanh`→`PowerAmpPPT`, agregar oversampling 2× al Plugin, calibrar crest vs render de referencia (o por carácter si no hay render).
- **Bloqueante para portar cualquiera:** necesitamos la **topología/esquemático** del amp
  (valores de §5) + un **render de referencia** para calibrar. Sin eso solo se puede adivinar.

Memorias relacionadas: arquitectura y harnesses en `slopsmith-amp-dsp-guitarix-redo`;
referencias de A/B en `amp-reference-renders`.
