# Cómo hacer los amps de Slopsmith "circuito-real" (guía para Claude + humano)

Esta guía explica cómo convertir un amp VST de Slopsmith desde el modelo viejo de
**caja-negra (`tanh`)** a un modelo **circuito-real** (física real de tubos / diodos /
transformadores), que suena mucho más fiel. Está escrita para dársela a un agente de
Claude como contexto + para que un humano la siga.

> **TL;DR para el agente:** el objetivo es reemplazar las no-linealidades `tanh`
> (`asymTube`, `softClip`) por los bloques físicos del framework `tube_stage.hpp`
> (tubos Koren reales + tonestack Yeh + power push-pull + diode clipper Shockley),
> leyendo el **esquemático real** del amp para los valores, y calibrando con un banco
> de pruebas offline que mide **crest factor**. Ya hay 13 amps hechos como ejemplo.

---

## 0. Filosofía (no negociable)

1. **Hacemos VSTs con DSP propio, NUNCA capturas NAM.** El entregable es el plugin que
   modela el circuito; un NAM es una foto estática de un setting y no sirve como solución.
2. **Circuito-real = modelar los componentes reales**, no ajustar un `tanh` a oído. Cada
   etapa del amp se modela con su física:
   - Tubo (triodo/pentodo) → tabla de transferencia Koren + loop de cátodo físico.
   - Tonestack pasivo (Bass/Mid/Treble) → la transferencia analógica real (modelo Yeh).
   - Power amp → push-pull real con sag + transformador de salida.
   - Distorsión de estado sólido (op-amp/diodos) → clipper de diodo Shockley (no `tanh`).
3. **Los valores salen del ESQUEMÁTICO real del amp**, no inventados.
4. **Verificación objetiva, no solo de oído:** medir crest factor vs un render de
   referencia (cuando existe) o por carácter (cuando no).

---

## 1. Requisitos (qué hay que tener antes de empezar)

| Recurso | Para qué | Dónde |
|---|---|---|
| **El repo `rig_builder`** | el código de los amps | este repo |
| **Framework `tube_stage.hpp`** | los bloques físicos a reusar (NO reinventar) | `vst/src/_shared/tube_stage.hpp` |
| **Tablas Koren** (`koren*_ftube.h`) | las tablas de transferencia de cada tubo, ya generadas | `vst/src/_shared/` |
| **`oversampler.hpp`** | el anti-alias 2× | `vst/src/_shared/` |
| **DPF** (DISTRHO Plugin Framework) | compilar el VST3 | `vst/src/amps/DPF` (clonar de github.com/DISTRHO/DPF + `git submodule update --init`) |
| **Repo de Guitarix** (GPL) | **referencia de arquitectura** + tablas de tonestack documentadas (`tonestack.dsp` tiene jcm800/jcm2000/mesa/twin/ac30/soldano/etc.) | clonar `github.com/brummer10/guitarix`; acá está en `/Users/nacho/Files/slopsmith/guitarix-master` |
| **Brit DI** (`ui_public_inputs_Brit - Guitar.wav`) | la señal de guitarra seca para medir | raíz del proyecto |
| **`tools/woodrow.cpp`** | mide crest/RMS de un WAV | `vst/src/amps/tools/` (compilar: `c++ -O2 woodrow.cpp -o /tmp/woodrow`) |
| **`tools/amp_harness_template.cpp`** | plantilla del banco de pruebas offline | `vst/src/amps/tools/` |
| **`tools/gx_tube.py`** | genera tablas Koren de tubos NUEVOS (si hace falta uno que no existe) | `vst/src/amps/tools/` |
| **Renders de referencia** (opcional) | A/B objetivo si el amp tiene uno | `test logic/done/` (ej. `jcm800_preamp_*`, `dual_ch*`) |
| **Esquemático del amp** | los valores reales del circuito | `amps/<Nombre del amp>/` (PDF/gif) |
| **Datos de uso** (para priorizar) | qué amps se usan más en la librería | `~/Library/Application Support/slopsmith-desktop/slopsmith-config/cloud_loader/index.db` (tabla `remote_index`, columna `tones_json`) |

**Guía técnica profunda (complementaria a esta):** `REAL_TUBE_AMP_GUIDE.md` (mismo dir).

---

## 2. Reglas de licencia (CRÍTICO — leer)

- **Guitarix es GPL-2.0.** Se usa SOLO como **referencia de arquitectura/técnica** y para
  los **valores de componentes** (R/C de tonestacks — los valores no son copyrightables).
  **NUNCA copiar su código fuente** al repo. Nuestras tablas de tubo son OURS (modelo
  Koren público + solve de load-line Yeh/Karjalainen académico), no las de Guitarix.
- **Marcas:** todo amp lleva un rename de parodia y la cara NUNCA debe mostrar la marca
  real. Convención: Marshall→Marsten, Fender→Bender, Vox→Box, Mesa→Silla, Roland→Ronald,
  Hiwatt→Lovolt, Laney→Raney, etc.
- **NO** enviar IRs/modelos derivados de Rocksmith/AmpliTube/UAD. **NO** ayudar a sortear
  el DRM ni el takedown DMCA de los PSARC (eso lo maneja el dueño del proyecto).

---

## 3. El framework (qué reusar — está todo en `tube_stage.hpp`)

No reinventes nada de esto. `#include "../../_shared/tube_stage.hpp"` y usá:

- **`rbtube::TubeStage`** (= `TubeStageT<Tube12AX7>`) — una etapa de triodo real
  (tabla Koren pura `Ftube(Vgrid)→Vplate` + loop de cátodo físico auto-bias). Alias por
  tubo: `TubeStage` (12AX7), `TubeStageAY7` (12AY7).
  `set(sr, Ri_tab, vplus, divider, fck, Rk)` — Ri_tab=1 (grid-leak 250k) casi siempre,
  vplus≈250, divider≈40, fck = corner del cap de bypass de cátodo (Hz; más alto = menos
  graves/más ganancia tardía), Rk≈1500.
- **`rbtube::MillerLowPassT<TUBE>`** — carga Miller por tubo: `Cin = Cgk + Cgp*(1+|Av|)`
  y `fc = 1/(2*pi*Rsource*Cin)`. Usar `Miller12AX7`/`Miller12AY7` entre etapas cuando el
  esquemático permita estimar la resistencia de fuente; esto reemplaza los low-pass fijos
  estilo Guitarix (`~6531 Hz`) por un techo de agudos derivado del circuito.
- **`rbtube::PowerAmpPPT<TUBE>`** — power push-pull real con sag + bias drift + OT.
  Alias: `PowerAmpPP` (EL84), `PowerAmp6V6`, `PowerAmp6L6` legacy, `PowerAmp6L6G`,
  `PowerAmp5881`, `PowerAmp6L6GC`, `PowerAmpKT66`, `PowerAmpEL34`.
  `set(sr, drive, biasV, sagDepth, otHP, otLP)` + `.out` (escala salida) + `.biasShift`.
  **sagDepth bajo (~0.1) = fuente rígida/limpia; alto (~0.5) = saggy (rectificador de tubo).**
- **`rbtube::ToneStackYeh`** — tonestack TMB de 3 bandas (modelo Yeh, **doble precisión**).
  `setComponents(R1,R2,R3,R4,C1,C2,C3)` (R1=treble pot, R2=bass, R3=mid, R4=slope, C1=treble
  cap, C2=bass, C3=mid) + `update(sr, treble, mid, bass)` + `process(x)`.
- **`rbtube::TweedTone`** — el control de tono de 1 perilla estilo 5E3 (para tweeds).
- **`rbshared::Oversampler4x`** (de `oversampler.hpp`) — anti-alias 2× (OS=2 → 96 kHz).
- **`DiodeClipper`** (Shockley, solve Newton) — para distorsión de estado sólido / híbridos.
  No está en el shared todavía; está inline en `jc90/`, `jc120_ronald/`, `aor50/`. Copiá
  esa struct (par de diodos anti-paralelo a masa, resuelta por Newton).

Tubos con tabla ya generada: **12AX7, 12AY7, 12AT7, 6EU7, 6SL7, 6SF5, 12AU7, 7199,
EF86, 5879, EL84, 6BM8/ECL82, 6V6, 6L6 legacy, 6L6G, 5881, 6L6GC, KT66, EL34**.
Los PDFs base viven en `tubes/` en la raiz del proyecto. Si necesitás uno nuevo
(ej. 6550, KT88): agregá sus constantes Koren en `tools/gx_tube.py`, corré el script para
emitir el `.h`, y agregá el trait+alias en `tube_stage.hpp`.

---

## 4. Receta paso a paso para portar UN amp

### Paso 1 — Identificar y ubicar
- Encontrá el dir del amp en `vst/src/amps/<nombre>/` y su VST instalado en `vst/amps/`.
- Encontrá su esquemático en `amps/<Nombre> (CODE)/`.
- ¿Hay render de referencia? Buscá en `test logic/done/` (ej. `<amp>_*.wav`).
- Confirmá que sigue en `tanh`: `grep -l tube_stage.hpp <dir>/*.cpp` (si no aparece, es tanh).

### Paso 2 — Leer el esquemático (ver §5) y anotar:
- Qué tubos (12AX7 preamp casi siempre; EL34/6L6/6V6/EL84 power) y cuántas etapas.
- Los valores del **tonestack** (3 pots + 3 caps + slope R).
- Si hay distorsión de **diodos** (op-amp + 1N4148) → es híbrido o solid-state.
- Rectificador (tubo = saggy / silicio = tight) → mapea a `sagDepth`.

### Paso 3 — Editar el `<Amp>Plugin.cpp` (o `<Amp>Core.h`)
1. **Includes:** agregar `tube_stage.hpp` + `oversampler.hpp`.
2. **Miembros:** agregar los `rbtube::TubeStage` (uno por etapa), el `PowerAmpXXX`, y
   `rbtube::ToneStackYeh tone;` (reemplazando los shelves falsos o el tonestack `float`).
   Agregar `void setupTubes()` que llama `vX.set(sr, 1, 250, 40, fck, 1500)` por etapa.
3. **`updateFilters()`:** `tone.setComponents(...valores reales...); tone.update(sr,t,m,b);`
   y `power.set(sr, drive, bias, sagDepth, otHP, otLP); power.out=...;`
4. **`reset()`:** resetear cada tubo + `power` + `tone` + llamar `setupTubes()`.
5. **`setSampleRate`:** si había `toneStack.setSampleRate(...)` de un tonestack float, sacarlo.
6. **`process()`:** reemplazar cada `asymTube(x, drive, bias)` por `vX.process(x * driveVolts)`
   y el power `asymTube(...)` por `y = power.process(y);`. El tonestack falso por
   `y = tone.process(y) * toneMk;` (toneMk≈13 compensa la pérdida de inserción del Yeh).
   **Quitar el `cleanMakeup` viejo** (ver §6).
7. **Plugin (clase `<Amp>Plugin`):** agregar `Oversampler4x osL,osR;` + `kOS`, multiplicar
   `setSampleRate` por `kOS`, y en `run()` upsamplear/downsamplear (ver cualquier amp ya
   hecho, ej. `jcm800_marsten/Jcm800Plugin.cpp`).

### Paso 4 — Calibrar (ver §7)
- Compilar: `make -C vst/src/amps/<dir> BASE_PATH=vst/src/amps/<dir>`
- Extraer el Core a un header + correr el harness → medir crest vs referencia/carácter.
- Ajustar los `driveVolts` de cada etapa hasta que la curva de crest sea **monotónica**
  (gain bajo = limpio, alto = sucio) y matchee la referencia.

### Paso 5 — Instalar (⚠️ ojo con el nombre de deploy)
El `.vst3` que carga la app NO es necesariamente el nombre del bin. El nombre real está en
**`data/rs_gear_to_vst.json`** (campo `bundled`, ej. `vst/amps/BOX AC30.vst3`) — suele ser
el **nombre de PARODÍA**, distinto del `NAME` del Makefile / `getLabel`. Ejemplos:
`en30`→bin `EN30.vst3` pero deploy `BOX AC30.vst3`; `tw26`→`BENDER DELUXE.vst3`; los ports
recientes (jcm800→`MarstenJCM800.vst3`) sí coinciden. **Siempre copiá al path de
`rs_gear_to_vst.json`**, no al `bin/*.vst3` ciegamente.
```
DST=$(python3 -c "import json;print(json.load(open('data/rs_gear_to_vst.json'))['Amp_XXX'][0]['bundled'])")
cp -R vst/src/amps/<dir>/bin/<Bin>.vst3/. "$DST/"   # copia el contenido fresco
codesign --force -s - "$DST"
```
Aplicar en la app: **Cmd+Q y reabrir Slopsmith**.

### Paso 5b — Loudness (que no salte el volumen entre amps)
Todos los amps deben quedar al **mismo nivel** (target del proyecto: **−19 dBFS RMS**),
medido con la Brit DI y **todos los knobs a 0.5**. El loudness final lo fija el `K` de salida
(el multiplicador en `rbAmpLvl(K * core.process(...))` del run loop). Para emparejar un amp:
medí su RMS (con `tools/loudness_check.py`) y reajustá `K_new = K_old · 10^((−19 − medido)/20)`.
El `rbAmpLvl` (knee a 0.9) absorbe los picos, así que hasta boosts de +14 dB llegan sin
clipear. NO toques el tono para emparejar volumen — solo el `K`.

**Y CLAVE: que no derive con la ganancia.** Emparejar a −19 en UN punto no basta — hay que
verificar que el amp mantiene −19 a lo largo del barrido de Gain (mucha gente sube de volumen
a gain alto / baja a gain bajo; el Deluxe llegaba a 24 dB de deriva). Fix: una **compensación
post-distorsión limpia** `gc = std::pow(10, 0.05f*gcDb)` con `gcDb = a + b*g + c*g*g` (g = el
miembro de Gain del amp), ajustada para que el RMS quede plano, **anclada en 0 dB a g=0.5**
(preserva el −19 del punto de calibración), con clamp, y aplicada DESPUÉS del softClip de
salida (es volumen puro — no cambia el tono ni el crest). Método: barrer el Gain a 5 puntos,
`comp_dB(g) = −19 − RMS(g)`, ajustar una cuadrática (least-squares), inyectar, re-medir. Meta:
≤±1.5 dB de deriva en todo el rango de Gain.

### Paso 6 — Documentar
- Actualizar `REAL_TUBE_AMP_GUIDE.md` (estado por amp) y avisar que queda **pendiente
  probar en vivo** (la calibración offline es objetiva pero el oído manda).

---

## 5. Cómo leer un esquemático

- Si el PDF tiene capa de texto: `pdftotext -f P -l P amp.pdf -` para buscar valores.
- Si es imagen (lo normal): renderizar a alta resolución y recortar por zonas. El Read
  de imágenes hace downsample, así que **recortá regiones chicas**:
  ```python
  import fitz
  p = fitz.open("amp.pdf")[0]
  pix = p.get_pixmap(matrix=fitz.Matrix(6,6), clip=fitz.Rect(x0,y0,x1,y1))  # zoom 6, recorte
  pix.save("/tmp/crop.png")
  ```
  (gif/jpg: convertir con PIL a png primero.)
- Buscá el **tonestack**: 3 pots juntos (TREBLE/MIDDLE/BASS) con sus caps. Anotá pot Ω y
  cap F de cada uno + la resistencia de slope.
- **Atajo:** muchos tonestacks ya están en Guitarix `trunk/src/faust/tonestack.dsp`
  (jcm800, jcm2000, mesa, twin, ac30, ac15, soldano, princeton, bassman, engl...). Son
  los valores reales; usalos directo si el amp matchea uno.

---

## 6. Lecciones / errores comunes (⚠️ MUY importante — esto nos costó caro)

1. **Tonestack de 3er orden DEBE ser doble precisión.** Un tonestack en `float` se va a
   **NaN a 192 kHz** (= host a 96k × 2× OS). Usá siempre `rbtube::ToneStackYeh` (double).
   Si ves un `XxxToneStack` custom en `float`, reemplazalo.
2. **El `cleanMakeup` viejo INVIERTE la curva de crest.** Era `1 + N*exp(-gain/k)` (×N a
   bajo gain) para levantar el tono limpio con el modelo tanh. Con tubos reales mete el
   limpio contra el `softClip` de salida y satura lo que debería estar limpio. **Reducilo
   fuerte (~×2) o eliminalo.**
3. **Los boosts de "fizz" del parlante inflan el crest sin distorsionar.** El viejo voicing
   tenía shelves de +9 a +16 dB en agudos. Un cab real **ATENÚA** los agudos. Cambiá esos
   shelves a negativos + un low-pass ~11 kHz.
4. **El piso de drive del power amp distorsiona los canales limpios.** Si `power.set` tiene
   un drive base alto (ej. 3.0), hasta un canal clean satura. Usá un **piso bajo** que suba
   con el canal/gain (ej. `0.8 + 4.5*chHot + ...`).
5. **El pot de Gain va ANTES del cascade.** En multi-etapa, escalá los drives de las etapas
   tardías con el knob de gain para que a bajo gain TODO el cascade quede limpio.
6. **Ladder de modos:** Raw < Vintage < Modern en ganancia (no al revés).
7. **Amps de alto headroom (Hiwatt) se quedan LIMPIOS.** Su crest lo domina el cab, no el
   preamp. NO fuerces breakup — quedaría irreal. Crest ~14-16 plano ES correcto ahí.
8. **Solid-state ≠ tubos.** JC (Jazz Chorus): op-amp lineal + `DiodeClipper` (knee duro,
   armónicos impares, buzzy) + power lineal SIN sag + chorus BBD. Híbrido (Laney AOR):
   tubos + `DiodeClipper` en serie tras el cascade.
9. **El nudge de entrada `3.2×`.** La guitarra en vivo entra más bajo que la Brit DI, así
   que los amps calibrados a la DI suenan limpios en vivo. Por eso el front-end lleva un
   `3.2×` (ya está en casi todos; mantenelo).

---

## 7. Calibración objetiva (el banco de pruebas)

El **crest factor** (pico/RMS en dB) es el proxy de distorsión: alto = limpio, bajo = saturado.

1. Compilá woodrow una vez: `c++ -O2 tools/woodrow.cpp -o /tmp/woodrow`.
2. Medí la referencia (si existe): `cd "test logic/done" && /tmp/woodrow <amp>_*.wav`.
3. Extraé el Core del Plugin.cpp y armá el harness (plantilla en `tools/amp_harness_template.cpp`):
   corré el Core sobre la Brit DI barriendo el Gain, e imprimí RMS/crest por nivel.
4. **Verificá SIEMPRE:** (a) sin NaN/inf a 48/96/192 kHz y pico < 1.0; (b) curva de crest
   **monotónica** (gain↑ → crest↓); (c) cerca de la referencia (±1-2 dB) o, sin referencia,
   con el carácter correcto (clean→crunch→lead).
5. Ajustá los `driveVolts` por etapa y el `toneMk` y repetí. Convergé en 3-5 iteraciones.

Si NO hay render de referencia (muchos amps): calibrá por carácter conocido del amp
(un Marshall lead llega a crest ~7; un Recto scoopeado ~9; un Fender clean ~16-18; un
Hiwatt se queda ~14-16). El dueño prueba en vivo y ajusta a oído al final.

---

## 8. Cómo usar esto con tu agente de Claude

Decile algo así (y dale esta guía + `REAL_TUBE_AMP_GUIDE.md` como contexto):

> "Portá el amp `<dir>` de Slopsmith a circuito-real siguiendo `COMO_PORTAR_AMPS.md`.
> Leé su esquemático en `amps/<...>` para el tonestack y la topología, reemplazá los
> `asymTube`/`softClip` por los bloques de `tube_stage.hpp` (TubeStage + PowerAmpXXX +
> ToneStackYeh, y DiodeClipper si tiene diodos), agregá oversampling 2×, y calibrá con un
> harness offline midiendo crest vs `<referencia>` (o por carácter si no hay). Quitá el
> `cleanMakeup` viejo y los boosts irreales del cab. Compilá, instalá a `vst/amps/`,
> firmá adhoc, y dejá anotado que queda pendiente probar en vivo."

**Buenos amps de ejemplo para que el agente copie el patrón:**
- Marshall puro (1 tonestack real + EL34): `jcm800_marsten/`, `plexi/`
- Multi-canal con morph (clean/crunch/lead): `dsl100/`, `dual_rect/`, `aor50/`
- Tonestack custom-float → doble (lección #1): `mark_iii/`, `mark_ii/`, `dr103_lovolt/`
- Solid-state (diode clipper, sin tubos): `jc90/`, `jc120_ronald/`
- Híbrido (tubos + diodo): `aor50/`

---

## 9. Estado actual (junio 2026)

**Cola activa de Nacho = amps de guitarra + DI limpios.** Los `Bass_Amp_*` quedan fuera de
esta cola y los hara el hermano. Caso especial: `Amp_AT20` vive como `Amp_`, pero el mapeo
local lo identifica como **Ampeg SVT-CL** y reutiliza `SamplegSBTCL` con `Bass_Amp_BT975B`;
por lo tanto se considera bajo salvo excepcion explicita.

**Base circuito-real ya documentada:**
BOX AC30 (Vox AC30) ✅, Bender Deluxe (5E3) ✅, Super-Sonic 22 (12AT7 PI) ✅, Bassman ✅, Plexi, JCM800, EMS/MrY,
Dual Rectifier, DSL100, Mark III, Mark II, Laney AOR50 (híbrido), Hiwatt DR103 + DR504,
Budda/Ganddi SuperDrive 45 (`Amp_BT45`, con `PowerAmpKT66` generado desde `tubes/KT66.pdf`),
JTM45/Bluesbreaker, DSL15/JVM410, OR50/OR100, Chieftain/DC30/Maz38,
JC90/JC120/VH140/VS100, UA610/Meve1073, Polystone MiniBrute, JimmyBean, AD50,
Tiny Terror/Rockerverb, GA88/GA8, Epiphone Century/Zephyr, Gibson GA79.
La mayoría **pendientes de prueba en vivo**.

**Correccion 2026-06-22:** `engel_fireball` / `Amp_EN50` ya tiene codigo circuito-real
(`tube_stage.hpp`, `ToneStackYeh`, `PowerAmp6L6GC`, oversampling 2×) contra el esquematico local;
el roadmap viejo que lo marcaba como proximo estaba atrasado. Correccion adicional 2026-06-22:
la cola restante de guitarra/DI fue auditada/compilada y los puntos que aun saltaban entre etapas
ahora tienen `CouplingCapGridLeak` donde corresponde. Los solid-state puros se mantienen sin tubos.

**Pendientes de guitarra/DI, excluyendo bajo, por uso actual de `nam_tone.db`:**
cola inmediata limpia; quedan pruebas en vivo, calibracion crest/RMS y renders de referencia para
ajustes finos. No hay port de guitarra/DI usado por `nam_tone.db` sin VST local al 2026-06-23.
Correccion 2026-06-23: Zephyr/Ruby ya tiene tablas 5879, 6SL7, 6SF5 y 6L6G; GA79 ya
tiene 6EU7, 7199 pentodo/triodo y 12AU7. GA79 usa EL84 para 6BQ5 porque es la misma
familia.

**Para sacar el ranking de uso** (priorizar qué portar):
```python
import sqlite3, json, collections, os
idx=os.path.expanduser("~/Library/Application Support/slopsmith-desktop/slopsmith-config/cloud_loader/index.db")
cnt=collections.Counter()
for (tj,) in sqlite3.connect(idx).execute("SELECT tones_json FROM remote_index WHERE tones_json LIKE '%Amp%'"):
    for t in json.loads(tj or "[]"):
        k=((t.get("GearList") or {}).get("Amp") or {}).get("Key")
        if k and not k.startswith(("Bass_","DI_")): cnt[k]+=1
for k,c in cnt.most_common(40): print(f"{c:5d}  {k}")
```
