# Bass Amp Rework Handoff

Fecha: 2026-06-22
Branch esperado: `feat/amps-rework`

Este documento es para quien continue los amps de bajo. La idea es reutilizar el
framework nuevo de amps reales sin copiar codigo GPL de Guitarix ni volver a hacer
tablas/tubos desde cero.

## Que ya existe

Archivos base:

- `vst/src/_shared/tube_stage.hpp`
  Framework fisico reusable: `TubeStageT`, `PowerAmpPPT`, `ToneStackYeh`,
  `TweedTone`, `CouplingCapGridLeak`, `PhaseInverterLTP12AX7`,
  `PhaseInverterCathodyne12AX7`, `MultiNodeBPlus`, `PotTaper`.
- `vst/src/_shared/koren*_ftube.h`
  Tablas Koren generadas para 12AX7, 12AY7, EF86, EL84, 6V6, 5881, 6L6GC,
  KT66 y EL34.
- `../tubes/*.pdf` en la raiz local de `slopsmith`
  Datasheets usados para capacitancias Miller y puntos de tabla. Ojo: esa
  carpeta esta fuera del repo Git `rig_builder`; no viaja con `git push origin
  feat/amps-rework` salvo que se copie dentro de `rig_builder/` o se comparta por
  otro medio. Los headers `koren*_ftube.h` si quedan dentro de `rig_builder`.
- `vst/src/amps/tools/gx_tube.py`
  Generador/auditoria de tablas Koren.
- `vst/src/amps/tools/calibrate_amp_core.py`
  Harness offline para estabilidad, gain sweep, crest, THD aproximado y puntos
  de espectro. Hoy trae spec para `en30`; agregar specs de bajo ahi.
- `vst/src/amps/REAL_TUBE_AMP_GUIDE.md`
  Guia tecnica completa del flujo nuevo.
- `vst/src/amps/en30/BoxDC30Core.h`
  Ejemplo piloto avanzado: BOX AC30 con Miller, blocking distortion, LTP,
  supply multi-nodo GZ34, pot tapers y salida reactiva.

Ejemplos de bajo existentes para auditar o portar:

- `vst/src/amps/sampleg_sbtcl/` - SVT-CL style.
- `vst/src/amps/cs75b_v4b/` - Ampeg V-4B style.

No asumir que esos dos ya estan "terminados" por compilar: revisar esquematico,
topologia, knobs, loudness, crest y prueba en vivo.

## Flujo recomendado para un amp de bajo

1. Identificar el amp real y conseguir esquematico legible.
2. Hacer inventario componente a componente:
   - tubos de preamp y power
   - caps de acople y grid leaks
   - cathode resistors/bypass caps
   - tonestack y switches
   - phase inverter
   - rectificador/supply
   - NFB, presence/deep/ultra-low/ultra-high
   - power amp y carga/speaker esperada
3. Si aparece un tubo nuevo:
   - agregar PDF a `tubes/`
   - agregar constantes en `vst/src/amps/tools/gx_tube.py`
   - generar `vst/src/_shared/koren_<tube>_ftube.h`
   - agregar trait en `tube_stage.hpp`
4. Elegir bloques:
   - Preamp: `TubeStageT<Tube...>`
   - Miller: `MillerLowPassT<Tube...>`
   - Caps entre etapas: `CouplingCapGridLeak`
   - Tonestack: `ToneStackYeh` o bloque propio si el circuito no es TMB normal
   - PI LTP: `PhaseInverterLTP12AX7`
   - PI cathodyne: `PhaseInverterCathodyne12AX7`
   - Supply: `MultiNodeBPlus` con perfil propio del amp
   - Power: `PowerAmpPPT<Tube...>`
   - Knobs: `PotTaper::audio`, `reverseAudio`, `sCurve`, `switchBlend`
5. Mapear Rocksmith a controles reales. No mapear linealmente por defecto:
   los potes reales suelen ser audio/log, reverse log o switches.
6. Agregar spec en `calibrate_amp_core.py` para el amp.
7. Validar:
   - estabilidad 48/96/192 kHz sin NaN/inf
   - gain sweep con crest y THD coherentes
   - RMS/loudness usable
   - espectro en puntos graves y medios
   - prueba en vivo dentro de Slopsmith

## Consideraciones especificas de bajo

- El low-end manda. No uses filtros HP agresivos de guitarra. Revisa los caps de
  acople reales: muchos amps de bajo usan valores grandes para no adelgazar.
- El power amp y el OT deben tener headroom y low-frequency behavior creibles.
  Un bajo puede excitar sag y blocking de manera distinta a guitarra.
- Los switches tipo Ultra Low / Ultra High / Deep / Bright no son simples EQ
  genericos: suelen cambiar caps/resistencias concretas alrededor del tonestack o
  feedback loop.
- Si el amp real tiene NFB fuerte, modelarlo. En bajo, el damping cambia mucho el
  ataque y la sensacion de tightness.
- Validar con DI de bajo, no solo con `ui_public_inputs_Brit - Guitar.wav`.
  Para el harness, agregar una entrada o excitacion con fundamentales de bajo
  (aprox. 41, 55, 73, 82, 110, 220, 440 Hz).
- Separar amp y cab. El amp no debe hornear toda la curva de un cab si Slopsmith
  aplicara IR despues; pero si se modela una carga reactiva, debe representar lo
  que el power amp "ve", no una EQ final decorativa.

## Checklist de implementacion

- Crear o auditar `vst/src/amps/<amp>/`.
- Usar oversampling en el wrapper si hay no-linealidades fuertes.
- Mantener el core offline-testable, idealmente en `<Amp>Core.h`.
- Evitar `tanh` generico como sustituto de tubo cuando hay esquematico.
- Usar `ToneStackYeh` con valores reales cuando aplique.
- Usar `CouplingCapGridLeak` donde una grilla pueda conducir y cargar el cap.
- Usar PI correcto por topologia; no entrar directo a `PowerAmpPPT` si el amp
  tiene phase inverter relevante.
- Usar `MultiNodeBPlus` o crear un perfil propio de supply por amp.
- Documentar en `REAL_TUBE_AMP_GUIDE.md` el estado del amp y que falta probar.
- Recompilar, instalar bundle, firmar y correr harness.

## Comandos utiles

Compilar un amp:

```bash
make -C rig_builder/vst/src/amps/<amp_dir> BASE_PATH=/Users/nacho/Files/slopsmith/rig_builder/vst/src/amps/<amp_dir>
```

Instalar el binario macOS en el bundle:

```bash
install -m 755 rig_builder/vst/src/amps/<amp_dir>/bin/<Plugin>.vst3/Contents/MacOS/<Plugin> "rig_builder/vst/amps/<Bundle Name>.vst3/Contents/MacOS/<Plugin>"
codesign --force -s - "rig_builder/vst/amps/<Bundle Name>.vst3"
codesign --verify --deep --strict --verbose=2 "rig_builder/vst/amps/<Bundle Name>.vst3"
```

Validar AC30 como ejemplo:

```bash
python3 rig_builder/vst/src/amps/tools/calibrate_amp_core.py en30
python3 rig_builder/vst/src/amps/tools/loudness_check.py
```

## Que subir al branch para compartir

La rama local actual es `feat/amps-rework`. Para que otro dev pueda reutilizar
esto, al menos debe llegar al remoto:

- `vst/src/_shared/tube_stage.hpp`
- `vst/src/_shared/koren*_ftube.h`
- `vst/src/amps/tools/gx_tube.py`
- `vst/src/amps/tools/calibrate_amp_core.py`
- `vst/src/amps/REAL_TUBE_AMP_GUIDE.md`
- `docs/BASS_AMP_REWORK_HANDOFF.md`
- los cores/params/plugins que se quieran compartir como referencia
- PDFs de `tubes/*.pdf` por una de estas vias:
  - copiarlos dentro de `rig_builder/tubes/` y commitearlos ahi, o
  - compartir la carpeta `tubes/` fuera de Git, o
  - convertirlos en un repo/submodulo separado si se van a seguir agregando.

No hacer `git add -A` en este worktree sin revisar: ahora hay muchos bundles
macOS modificados, payloads Windows/Linux borrados y directorios `bin/` no
trackeados. Para un push limpio, stagear solo lo necesario.

Ejemplo de push selectivo:

```bash
git -C rig_builder status --short --branch
git -C rig_builder add docs/BASS_AMP_REWORK_HANDOFF.md \
  vst/src/_shared/tube_stage.hpp \
  vst/src/_shared/koren5881_ftube.h \
  vst/src/_shared/koren6l6gc_ftube.h \
  vst/src/_shared/koren_ef86_ftube.h \
  vst/src/_shared/koren_kt66_ftube.h \
  vst/src/amps/tools/calibrate_amp_core.py \
  vst/src/amps/tools/gx_tube.py \
  vst/src/amps/REAL_TUBE_AMP_GUIDE.md
git -C rig_builder diff --cached --check
git -C rig_builder commit -m "Document amp rework framework for bass ports"
git -C rig_builder push origin feat/amps-rework
```

Si tambien se quiere compartir el BOX AC30 piloto, agregar sus archivos de
mapeo/core/bundle de forma explicita, revisando el diff antes de commitear.

Si tambien se quieren subir los PDFs al mismo branch, copiar primero:

```bash
mkdir -p rig_builder/tubes
cp tubes/*.pdf rig_builder/tubes/
git -C rig_builder add tubes/*.pdf
```
