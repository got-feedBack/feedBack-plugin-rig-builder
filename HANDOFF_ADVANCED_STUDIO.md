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
