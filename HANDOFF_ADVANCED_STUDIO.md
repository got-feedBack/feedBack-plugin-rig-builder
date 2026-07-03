# Handoff — Studio multi-amp + editor de nodos "Advanced"

**Branch:** `feat/advanced-node-editor` · **Versión plugin:** `2.6.28`
**Archivos tocados:** `screen.js`, `screen.html`, `plugin.json` (todo frontend del plugin; no se tocó `routes.py`).

> Recordá: para que un cambio de **`screen.js`/CSS** recargue en la app hay que **subir `plugin.json` `version`** (la app cachea por `?v=version`). Los cambios de `screen.html` recargan sin bump. Reiniciá la app para verlos.

---

## 1. Sala del Studio — múltiples amps (paralelo)

Cuando la cadena tiene 2-4 amps (p.ej. al agregar uno en paralelo desde Advanced), la sala los dibuja a todos, no solo el primero.

- **Layout** (`RB_AMP_EXTRA_SLOTS` en `rbRenderStudioRoom`, `screen.js`): todos los amps son del **mismo tamaño**, en la **misma línea de piso** (`bottom:14%`, `width:168`, `translateZ:-140`, igual que el principal) y **miran al centro**.
  - 2 amps → par espejado (principal `left:28%` rotateY(32) · espejo `left:72%` rotateY(-32)).
  - 3 amps → + uno al centro (`left:50%`, recto).
  - 4 amps → + dos al medio (`left:39%/61%`, rectos, separados).
- **Cajas 3D completas**: cada amp = frente + **cara lateral** (`.rb-amp-stack::before`, espejada al borde derecho para los amps que miran a la izquierda via `.rb-amp-extra::before`) + **techo** (`.rb-amp-stack::after`, `rotateX(-90)`, opaco, `top:2px`).
- **GOTCHA crítico:** **no poner `filter:` en `.rb-amp-extra`** — un `filter` sobre un elemento `preserve-3d` **aplana las caras 3D** (la cara lateral desaparecía y el amp se veía como foto plana). Por eso el amp extra no lleva dimming.
- La sombra de contacto por amp extra (`extraGroundHtml`) espeja la del principal.

---

## 2. Editor de nodos "Advanced"

Editor de grafo (palette de gear a la izquierda + canvas con nodos y cables). Antes era solo visual; ahora edita la cadena real.

### Acceso
- **Ya NO está en la barra superior** (junto a Gear/Master). Se abre con un botón flotante **⚙ Advanced** dentro de la sala del Studio (`.rb-studio-adv-btn`). Se sale con el botón **Studio** de arriba.

### Materializar gear (drag desde el palette)
- `rbAdvMaterializeGear()`: al soltar un gear en el canvas se crea una **pieza real** en la cadena (`rbStudioCurrentChain` + `rbStudioPersist`), con el **VST del catálogo** (`vst_path/format/state` + nombre copyright-free) → suena y muestra su cara real en la sala.
- El palette y los nodos usan la **cara del VST** (`rbAdvGearImg`, vía `RBPedalCanvas`), **nunca** las fotos de RS (`/gear_photo/...`, scrubeadas por DMCA).

### Conectar / desconectar
- **Conectar:** arrastrar desde el jack de salida ● a otro nodo (`rbAdvStartWire`/`rbAdvConnect`).
- **Desconectar:** **doble-click** sobre el cable (`dblclick` en `.rb-adv-cable-hit`). Un click simple no hace nada (evita desconexiones accidentales). No hay botón ✕ ni resaltado rojo.
- **GOTCHA crítico (raíz de "no puedo clickear el cable"):** la capa de nodos (`#rb-adv-nodes`, z-index:2) está **encima** del SVG de cables y, aunque vacía, interceptaba todo el puntero. Fix: `.rb-adv-nodes{ pointer-events:none }` + `.rb-adv-node{ pointer-events:auto }` → las zonas vacías dejan pasar el click a los cables; los nodos siguen interactivos (la delegación de eventos sigue funcionando porque burbujean).

### Borrar nodos
- Botón **✕** en hover de cada nodo (`rbAdvDeleteNode`), o **arrastrar el nodo fuera del canvas** (se pone rojo, se suelta = borra). Re-conecta los vecinos para no romper la cadena, re-indexa `pieceIdx`, persiste y recarga.

### Bypass
- **Doble-click sobre un nodo gear** → `rbAdvToggleBypass`: la pieza se bypassea (pasa de largo como un cable), el nodo se pone **gris (`saturate(0)`)**, se persiste y también se grisea en la sala + se setea el bypass en el slot del motor.

### Pre/Post amp (sincronización en ambos sentidos)
- **Advanced → cadena** (`rbAdvSyncPedalSlots`, en connect/disconnect): el slot pre/post de un pedal se deriva de la **topología del grafo** — si el pedal *llega* al amp = `pre_pedal`; si el amp *llega* al pedal = `post_pedal`. Antes el materializado siempre quedaba `post_pedal`.
- **Cadena → Advanced** (`rbAdvRestore`): si el grafo cacheado tiene un pedal en una posición que **no coincide** con su slot actual (porque lo cambiaste en el main UI), descarta el cache y **reseedea** el grafo desde la cadena.

### Zoom / pan
- Botones **− / +** (`rbAdvZoom`, 40–160%). **Pinch** (gesto Mac = `wheel` + `ctrlKey`) hace zoom. Un **scroll de 2 dedos hace pan** nativo (no zoom). *(El swipe de 3 dedos no es capturable por una página web/Electron.)*
- Wrapper `#rb-adv-zoom` mantiene el tamaño de scroll correcto al escalar; `rbAdvLayerPoint` descuenta el zoom para que drag/drop/wire sigan precisos.

### Persistencia del grafo
- El grafo (posiciones + cableado paralelo) se guarda en **localStorage** por vista del Studio (`rbAdvStorageKey`: default/song/saved), así un rig paralelo sobrevive el reinicio. Es **solo visual** — el audio sigue en serie en el motor actual (el mix paralelo real necesita el motor DAG, diferido).

### Otros
- El nodo de entrada dice **"Input"** (antes "Guitar"); `rbAdvRestore` fuerza la etiqueta fresca para que no quede cacheada.
- **Editar perillas desde Advanced**: botón 🎛 en hover de un nodo → tarjeta flotante con el canvas interactivo del VST (`rbAdvEditNode`, reusa `rbStudioMakeFaceInteractive`); las perillas se mapean al slot vivo del motor.

---

## 3. Audio (no-Advanced, mismo branch)
- **Re-aplicación de params de tono** (`rbStudioFinishMonitorLoad`): al cargar un tono en el monitor del Studio, `loadPreset` no restaura confiablemente los params del VST → se re-aplican con `setParameter` (+ bypasses + input drive + leveler) **antes** de des-mutear. Sin esto, el gear sonaba/mostraba en defaults.

---

## ⚠️ Gotchas para tener en cuenta
1. **`filter` aplana `preserve-3d`** → no usar en amps/racks (caras 3D desaparecen).
2. **`z-index` se ignora bajo `preserve-3d`** → el orden de pintado en la sala es por Z real, no por z-index.
3. **`pointer-events`**: la capa de nodos debe ser `none` (con nodos `auto`) o tapa los cables.
4. **Probar borrados/cambios destructivos sobre CLONES en memoria**, nunca sobre el default tone real — `rbAdvDeleteNode`/`rbStudioPersist` escriben al backend al toque. (Composición correcta del default tone documentada aparte.)
5. **SVG `innerHTML` + gradiente** no repinta hasta un reflow → los cables usan stroke **sólido**, no `url(#gradient)`.

---

# 🎚️ TUTORIAL AUDIO ENGINE — para el compa (cambios en el motor)

> 3 fixes de audio que ya están **aplicados y probados en mi máquina** pero viven en el repo del **engine** (`slopsmith-desktop`) y en la app deployada — hay que portarlos a la rama oficial del engine que buildea la app. Suena todo parejo y sin arañazos. **Regla de oro: NO se toca el DSP de los amps** — son fieles al esquemático; estos fixes son 100% del motor / loudness, nada de amps.
>
> **Repo del engine:** `~/Downloads/slopsmith_release_v0/slopsmith-desktop`, branch `feat/parallel-signal-graph`, remote `github.com/slopsmith/slopsmith-desktop`.
> **Build:** `npm run build:audio` → `build/Release/slopsmith_audio.node`. Gotcha: line-endings CRLF rompen los scripts → `chmod +x scripts/*.sh` si falla el build.
> **OJO:** la app v0.3.0 **deployada corre OTRO build** del engine (reporta 0.2.9). Para probar in-vivo yo hago swap del `.node` + `slopsmith-vst-host` ad-hoc-firmados dentro del `.app`; lo correcto es que el compa aplique los diffs a la rama oficial y re-buildee la app.

## Fix 1 — Denormales (los "arañazos" / dropouts aleatorios)
**Causa:** el RT path del engine **no tenía `ScopedNoDenormals`/FTZ en ningún lado**. La cadena es full-IIR (NAM, VSTs de amp/EQ/comp, colas de IR); tras cada nota el estado de los filtros decae hacia ~0 y cae en **denormales** (floats 10-100× más lentos) → **picos de CPU esporádicos → underrun → arañazo + el juego salta frames**. Firma clásica: aleatorio, sin patrón fijo, **peor con buffer grande** (320→450 lo empeoró), independiente de canción/tono, pasa hasta con un solo amp.

**Fix — `const juce::ScopedNoDenormals noDenormals;` (FTZ/DAZ scoped) en los 3 puntos del RT path.** Inaudible (los denormales están sub −300 dBFS); solo baja CPU, **0 cambio de tono/API**.

1. **`src/audio/AudioEngine.cpp`** — al tope de `audioDeviceIOCallbackWithContext` (apenas abre la llave, antes del `callbacksInFlight[0].fetch_add(...)`). Cubre TODO el callback. *(commit `8b175c9`)*
2. **`src/audio/SignalChain.cpp`** — al tope de `SignalChain::process` (antes del `ScopedTryLock`). Cubre serial y graph mode. *(commit `8b175c9`)*
3. **`src/vst-host/main.cpp`** — en el worker de audio (~línea 677), envolviendo el `processBlock`. **Éste es el clave para los amps del usuario**: los VST3 corren **fuera de proceso** (sandbox), así que el FTZ del main process NO los alcanza. *(commit `c640c9e`)*
   ```cpp
   if (auto* p = st.plugin.get())
   {
       const juce::ScopedNoDenormals noDenormals;
       p->processBlock(buffer, midi);
   }
   ```

## Fix 2 — Normalizador de canciones (todas al mismo volumen, sin perder dinámica)
**Objetivo:** que **toda canción suene al mismo loudness** (≈ −12 LUFS para `.psarc`; −15 dBFS RMS para stems), **pre el fader de Song del mixer** (así bajar ese fader igual baja). Es un **normalizador** (un gain por canción, medido), **NO un compresor** — preserva el rango dinámico. El usuario fue explícito en esto.

**Hay DOS caminos de backing y cada uno necesita su normalizador:**

**(a) `.psarc` / stem-less → nativo `backingTransport`.** Nuevo archivo **`src/audio/BackingLeveler.h`** (AGC K-weighted BS.1770 lento + brickwall limiter −1 dBFS, RT-safe, sin alloc en `process()`). Integración en `AudioEngine.cpp`:
   - `#include "BackingLeveler.h"` (tras `AudioSanitize.h`).
   - En `AudioEngine.h`: miembros `BackingLeveler backingLeveler; double backingLevelerSr = 0.0;` (junto a `backingVolume`).
   - En `renderBackingBlockLocked`, **antes** del `return outSamples;` (y antes de que se aplique `bVol`/`backingVolume`):
     ```cpp
     if (outSamples > 0 && sr > 0.0)
     {
         if (sr != backingLevelerSr) { backingLeveler.prepare(sr); backingLevelerSr = sr; }
         backingLeveler.process(backingBuffer, outSamples, -12.0f);
     }
     ```
   *(commit `5e76bdc`. `sr` = `currentSampleRate.load()`.)*

**(b) `.sloppak` (stems) → plugin Web Audio en el RENDERER, NO el transport nativo.** El `BackingLeveler` nativo **no los toca**. Hay que normalizar en el **plugin stems** (`plugins/stems/screen.js` del host — ⚠️ **no está en ningún repo que yo tenga local**, lo edité in-place en la app deployada; el compa tiene que encontrar el source de ese plugin). Cambios:
   - Nuevo `let normGain = null;` (junto a `masterGain`).
   - Tras crear `masterGain`: `normGain = ctx.createGain(); normGain.gain.value = 1.0; normGain.connect(masterGain);`
   - Cada stem rutea a `normGain` en vez de `masterGain` (`gain.connect(normGain)`).
   - Tras decodificar los buffers: medir **RMS mono-sum con stride** (`STRIDE=32`) a los gains iniciales → `gainDb = clamp(TARGET_DBFS(−15) − rmsDb, −12, 12)` → `normGain.gain.value = 10^(gainDb/20)`; **gated** si `rmsDb > −60` (no levantar silencio).
   - Teardown: `if (normGain){ normGain.disconnect(); normGain = null; }` antes de limpiar `masterGain`.
   - Subir version del `plugin.json` para cache-bust (lo dejé en 0.6.1).

   **Cadena:** `stems → normGain → masterGain(fader Song) → destino`. El normalizador queda **pre-fader** ✔.
   **Caveat conocido:** es RMS, no LUFS full; y mutear stems individuales baja el RMS percibido de esa canción. Para el caso de uso (parear canciones) anduvo perfecto ("quedaron parejas").

## Fix 3 — RB Final Leveler "bass-faithful" (tonos de bajo parejos)
El leveler **del tono** (no de la canción) usaba K-weighting estándar BS.1770 que **sub-pondera los graves** → los tonos de bajo medían "más bajo" de lo que se sienten y quedaban disparejos. Se aplanó el K-weighting para que el bajo cuente.

- Archivo: **`plugins/rig_builder/vst/src/RBFinalLeveler/Source/PluginProcessor.cpp`**, función `designKWeighting`:
  - High-shelf: `G = 3.999843853973347 → 1.5` (de +4 dB a +1.5 dB).
  - RLB high-pass: `f0 = 38.13547087602444 → 22.0` Hz.
  *(commit `fc4ade3`)*
- Y warmup del detector para que **NO** haya blast-then-drop al cargar el tono: `kWarmupHold 0.018→0.055s`, `kWarmupFade 0.010→0.022s`. *(commit `512f59d`)*
- Build: `cmake --build build --config Release` en `vst/src/RBFinalLeveler/`, copiar el artefacto a `vst/racks/RB Final Leveler.vst3/Contents/MacOS/`, `codesign --force --sign -`.

> Nota: el `BackingLeveler.h` (Fix 2a) **mantiene K-weighting estándar a propósito** — es música full-mix, ahí el estándar es correcto. Solo el leveler **de tono** (bajo solo) se aplana.

## ⛔ NO tocar: el DSP de los amp VST
Son fieles a los esquemáticos. El modelo de válvula (Newton/sample, MNA 6×6, `Ip()` con log/exp/sqrt/pow) es caro, **pero no se optimiza tocando los amps**. Los arañazos eran denormales del motor (Fix 1), no los amps. Una vía a futuro SOLO con validación A/B y aprobación sería Jacobiano analítico del Koren (numéricamente exacto, ~3× menos transcendentales) — por ahora se deja como está. El plugin JS ya se optimizó lo que correspondía (poll mega-chain `setInterval` 200→350 ms, commit `4211a98`); en idle 0 long-tasks.

## Cómo probar
1. Reiniciá la app (carga `v2.6.28`).
2. **Studio**: agregá un 2º amp (drag en Advanced) → debería verse como par espejado, cajas 3D con techo, ambos apoyados.
3. **Advanced** (botón ⚙ en la sala): arrastrar gear, conectar jacks, **doble-click en cable** para desconectar, **doble-click en gear** para bypass (se pone gris), arrastrar nodo afuera para borrar, **pinch** para zoom.
4. Mover un pedal pre/post (en Advanced o en el main UI) y confirmar que se refleja en el otro lado.

## Commits (branch `feat/advanced-node-editor`)
```
300c890 pedal pre/post syncs both ways + double-click a gear to bypass
de2d3ad drop "pinch to zoom" hint + remove cable red-hover highlight
c2b038a remove the cable ✕ button — double-click only to disconnect
0e13bb1 fix cables being unclickable (nodes layer blocked them) + dbl-click
a96646a disconnect cables, Input label, scroll-pan, Advanced in Studio
851ef99 spread the 4-amp inner pair
10e905f 3rd/4th amps full-size on the front floor line, facing centre
5606a45 amp roof top → 2px
f7062c3 amps are full 3D boxes (mirror + side + top face), rack reposition
0f00573 extra amp = exact mirror of the primary
6ef7777 drag-to-delete nodes, bigger editor
0c22a6f editable extra amp focus, in-canvas knob editor, canvas zoom
2b09dd9 solid cables + materialised amp shows its VST face
8124be3 fix cable render, add node delete, persist the graph
b9e8c33 use VST faces in the node editor, not RS gear photos
```
