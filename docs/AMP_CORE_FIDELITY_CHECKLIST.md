# Amp Core Fidelity Checklist

Procedimiento para revisar/calibrar un core de amp a **alta fidelidad**, cruzando
el **esquemático** (circuit-real) con el **audio de referencia** (suena bien).
Destilado de Plexi / AC30 / Deluxe / SuperSonic / Bassman / JCM800 / DualRect.

> Regla de oro: **UN amp a la vez**. Medir objetivamente + validar de oído. Nunca
> "voicing a ciegas". El usuario es el árbitro final por oído.

---

## 0. Insumos que necesitas

- **Esquemático**: `amps/<Amp ...>/*.pdf|gif` (lee los valores R/C reales, válvulas, modos).
- **Referencias amp-only**: `test logic/done/<amp>_*.wav` (SIN cab; por canal y por gain).
- **DI calibrado** (la entrada canónica = lo que entrega el Calibration Wizard):
  `ui_public_inputs_Brit - Guitar.wav` → peak −12 dBFS / RMS −28 dBFS.
- **Herramientas**:
  - `rig_builder/tools/render_amp_wav.py <ampdir> <in.wav> <out.wav> "Param=Valor" …`
    (compila el plugin REAL con DPF desde fuente; imprime clip_frac/dropout).
  - `tmp/ampdrive/ampmeas.py {gensine,char,thd,ltas}` (lee float32).

---

## 1. FASE A — Revisión del esquemático (circuit-real)

Para cada punto, compara el valor del core vs el esquemático y marca MATCH / DESVIACIÓN / MAL:

1. **Tone stack — ¡POR CANAL!**  R (pots treble/bass/mid + slope) y C (3 caps).
   - Los amps multi-canal tienen stacks **distintos por canal** (ej. DualRect: Green
     250k/250k/25k/150k vs Orange/Red 250k/**25k**/25k/**47k**). No uses uno solo para todos.
   - ⚠️ El tone stack de 3er orden DEBE ser doble precisión (`ToneStackYeh`) — en float
     da NaN a 192k.
2. **Topología de ganancia**: cuenta las etapas 12AX7 reales **por canal**; iguala
   `nStages` + qué etapas reciben drive. Anota cátodos sin bypass (= reducción de
   ganancia intencional) y los coupling caps (tighten de graves entre etapas).
3. **Válvulas**: preamp (12AX7/12AY7), **PI** (12AX7 vs 12AT7) y tipo (Marshall LTP /
   Fender AB763 / cathodyne), potencia (EL34 / 6L6GC / 6V6 / EL84 / 5881).
4. **Etapa de potencia**: **bias = punto de operación** (no el FMV), clase (A/AB),
   NFB/presence (¡el AC30 NO tiene!), **rectificador** (válvula=sag vs silicio=tight).
5. **Voicing**: bright cap, mid-scoop, presence — frecuencias plausibles.
6. **Modos/switches**: **verifica que CADA param de la UI lo LEA el DSP.** (El Mode del
   DualRect estaba muerto: el plugin pasaba base+0..base+5 y omitía base+6.)

Tip: para amps grandes, lanza un sub-agente por amp que lea el core + el esquemático
y reporte solo las discrepancias (no vuelca archivos a tu contexto).

---

## 2. FASE B — Match con el audio de referencia

1. Renderiza por el **DI calibrado** en los settings de canal/gain que igualen la
   referencia, con **Cab Sim=0** (amp-only vs ref amp-only).
2. **DISTORSIÓN → THD de seno (la métrica CONFIABLE, independiente del input).**
   `gensine sine.wav -12 150 2` → renderiza → `thd out.wav 150`. Mide a varios gains.
   - ⚠️ **El crest-sobre-DI ENGAÑA**: (a) un amp con gating muestra crest ALTO (silencio
     + notas fuertes), (b) depende del DI/EQ de la referencia (que NO conoces). Úsalo
     solo como cross-check, nunca como verdad.
3. **TONO → LTAS por bandas de octava** (`ltas`). Pero el EQ de la ref es desconocido,
   así que **no sobre-ajustes el LTAS** — calza el tilt general (graves/medios/top), ±1-2 dB.
   - El top (5k-12.5k) suele quedar ~−2 a −3 dB vs las refs brillantes = **techo armónico**
     (el contenido lo fija el tonestack/treble + generación de armónicos, NO el lowpass del
     OT/cab — subir `otVoice` casi no mueve). Aceptado en toda la familia.
4. **Barrido de gain**: limpio abajo → crunch onset en el punto correcto → full arriba.
   Compara la forma del barrido vs las refs por-gain.

---

## 3. FASE C — Gates de seguridad (DEBEN pasar)

1. **SIN gating**: los settings limpios/bajos DEBEN pasar. Renderiza el gain MÁS BAJO →
   si el rms cae a ~−100 dBFS (silencio con transientes sueltos) = **gating** = red flag.
   Causa típica: **bias de la válvula de potencia demasiado frío** (cutoff; ej. 5881 a −38).
2. **SIN clipping**: el peak de salida debe quedar bajo el soft-knee del wrapper (`rbAmpLvl`
   ~−1 dBFS). Exige `clip_frac=0` en el render.
3. **Loudness**: tras CUALQUIER cambio de ganancia, refitea el makeup (`outLevel`/`gcDb`
   poly) a ~−16 dBFS RMS en el punto de operación. **Nunca** des boost positivo a gain bajo
   (mete la señal limpia en el soft-knee → re-agrega breakup). El leveler final de la app
   nivela tono-a-tono.
4. **Ceiling del core compartido**: `_shared/guitar_amp_core.hpp` aplica el gain **solo a v2**
   (v3 fijo) → distorsión hace plateau ~**10% THD / crest ~9.5**. Cores propios que también
   manejan v3 (`v3.process(y*v3Drive)`, ej. JCM800/Plexi/DualRect) llegan a 35-44%. Si la ref
   pide más saturación que ~10% THD, el amp DEBE tener core propio que drivee v3.

---

## 4. FASE D — Deploy (evita el trap de caché)

```sh
cd vst/src/amps/<ampdir>
rm -rf build_local bin build      # ⚠️ git/edits dejan el mtime de la fuente más viejo que
                                   #    el .o cacheado → make NO recompila → binario STALE
make BUILD_DIR=$PWD/build_local TARGET_DIR=$PWD/bin DPF_PATH=../DPF
# bump getVersion d_version(x,y,z) ANTES de compilar
cp bin/<Bundle>.vst3/Contents/MacOS/<NAME> \
   ../../../amps/<Bundle>.vst3/Contents/MacOS/<NAME>
codesign --force -s - ../../../amps/<Bundle>.vst3/Contents/MacOS/<NAME>
lipo -archs ...   # verifica arm64 + timestamp fresco
```
El usuario aplica con **Cmd+Q + reabrir Slopsmith**, y valida de oído.

---

## 5. Restricciones permanentes

- **Rename parodia NUNCA en la cara in-app** (el canvas). La metadata DAW (`getDescription`)
  sí puede nombrar la marca real (convención existente). Marshall→Marsten, Fender→Bender,
  Vox→Box, Mesa→Silla, Roland→Ronald, Hiwatt→Lovolt, Orange→Citrus, Matchless→Unparallel,
  Dr Z→Mr Y, Ampeg→Sampleg, ENGL→Engel, Universal Audio→Multiversal.
- **Valores de Guitarix, NO código GPL.** DSP propio circuit-real, **nunca NAM.**
- **Los amps de BAJO son del hermano — no tocar.**
- Re-seed: solo si cambia el **layout** de params (agregar/quitar). Hacer que el DSP LEA un
  param que ya existía (ej. Mode muerto) NO requiere re-seed.

---

## Estado (2026-06-26)

Desplegados + ear-validados: **Plexi, AC30, Deluxe, SuperSonic, Bassman, JCM800**.
Desplegado pend. validación: **DualRect** (Mode cableado + tonestack por-canal corregido).
Veredictos de revisión: AC30/Deluxe/SuperSonic = fieles; Plexi (treble cap 220pF) + DualRect
(Mode + tonestack Red) = corregidos. Detalle en la memoria `slopsmith-amp-distortion-calibration`.
Pendiente: resto de la familia core-compartido (DSL100/15, JVM410, EMS, JTM45, Bluesbreaker)
— todos con el ceiling v2-only (subir el `otVoice` compartido 9k→~14k para el top de la familia).
