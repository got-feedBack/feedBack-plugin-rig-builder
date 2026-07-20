# Marsten amp debug handoff

Fecha: 2026-06-26  
Branch: `feat/amps-rework`  
Contexto: el usuario reporta que varios amps Marsten/Marshall no distorsionan como deberían. Un intento de ajuste posterior reintrodujo el bug anterior de sonido roto/cortado. Ese intento se revirtió.

## Estado actual

No continuar tocando los amps a ciegas. El usuario pidió dejar el tema de amps para que lo revise otro agente y seguir con pedales.

Al momento de este handoff, los cambios peligrosos en Marsten/Marshall fueron revertidos a `HEAD` de la rama:

- `vst/src/_shared/guitar_amp_core.hpp`
- `vst/src/amps/plexi/PlexiCore.h`
- `vst/src/amps/jcm800_marsten/Jcm800Core.h`
- `vst/src/amps/jtm45_marsten/Jtm45Core.h`
- `vst/src/amps/bluesbreaker_marsten/BluesbreakerCore.h`
- `vst/src/amps/dsl100/DSL100Plugin.cpp`
- `vst/src/amps/dsl15_marsten/Dsl15Plugin.cpp`
- `vst/src/amps/jvm410_marsten/Jvm410Plugin.cpp`
- `vst/src/amps/ems_mry/EmsPlugin.cpp`
- los binarios VST3 instalados de esos amps en `vst/amps/`

Verificar con:

```bash
git diff --name-only -- \
  vst/src/_shared/guitar_amp_core.hpp \
  vst/src/amps/plexi/PlexiCore.h \
  vst/src/amps/jcm800_marsten/Jcm800Core.h \
  vst/src/amps/jtm45_marsten/Jtm45Core.h \
  vst/src/amps/bluesbreaker_marsten/BluesbreakerCore.h \
  vst/src/amps/dsl100/DSL100Plugin.cpp \
  vst/src/amps/dsl15_marsten/Dsl15Plugin.cpp \
  vst/src/amps/jvm410_marsten/Jvm410Plugin.cpp \
  vst/src/amps/ems_mry/EmsPlugin.cpp
```

Debe salir vacío antes de retomar.

## Qué NO repetir

No cambiar varios amps a la vez. No volver a hacer una pasada global de gain/PI/power.

El intento que rompió el audio hizo estas cosas y debe considerarse fallido:

- Cambió `guitar_amp_core.hpp` para agregar `piMode` y alternar `PhaseInverterLTP12AX7`/`PhaseInverterLTP12AT7`.
- Forzó topología PI Marshall en amps que usan el core compartido.
- Subió mucho `gainSpan`, `powerDrive`, `piDrive` y `makeupBase`.
- Cambió bias/power en DSL15.
- Subió makeup de canales Classic hasta que la señal dejó de estar muda, pero acercó transientes al clip.

Resultado reportado por el usuario: volvió el bug anterior, con limpios que no suenan y distorsión rota/cortada.

Conclusión: no arreglar falta de distorsión empujando PI/power/makeup en bloque. Eso puede cruzar el umbral donde el modelo de power/sag/limitador se comporta como gate o clipping feo.

## Evidencia del estado restaurado

Después de revertir el intento fallido, estos renders salieron estables:

```bash
python3 tools/render_amp_wav.py plexi '../ui_public_inputs_Brit - Guitar.wav' \
  'test logic/amp_debug/reverted_plexi_di_hot.wav' \
  'Loudness I=0.88' 'Loudness II=0.50' 'Input=0.5' \
  'Bass=0.5' 'Middle=0.55' 'Treble=0.62' 'Presence=0.5'
```

Resultado observado:

- `output_rms_dbfs=-16.6152`
- `output_peak_dbfs=-7.14854`
- `clip_frac=0`
- `dropout_windows=0`

```bash
python3 tools/render_amp_wav.py jcm800_marsten '../ui_public_inputs_Brit - Guitar.wav' \
  'test logic/amp_debug/reverted_jcm800_di_hot.wav' \
  'Gain=0.78' 'Volume=0.6' \
  'Bass=0.5' 'Middle=0.5' 'Treble=0.55' 'Presence=0.5'
```

Resultado observado:

- `output_rms_dbfs=-15.2447`
- `output_peak_dbfs=-6.12486`
- `clip_frac=0`
- `dropout_windows=0`

```bash
python3 tools/render_amp_wav.py dsl100 '../ui_public_inputs_Brit - Guitar.wav' \
  'test logic/amp_debug/reverted_dsl100_di_hot.wav' \
  'Channel=0.85' 'Ultra Gain=0.82' 'Master 1=0.5' \
  'Bass=0.55' 'Middle=0.5' 'Treble=0.62' 'Presence=0.45'
```

Resultado observado:

- `output_rms_dbfs=-16.8592`
- `output_peak_dbfs=-8.81149`
- `clip_frac=0`
- `dropout_windows=0`

Esto demuestra que el estado restaurado no tiene el bug de audio cortado en el harness. El usuario aun así reporta que no distorsiona suficiente en la app.

## Métrica de distorsión aproximada

Con seno `test logic/amp_debug/sine_440_-18dbfs.wav`, el estado restaurado dio:

- Plexi `Loudness I=0.25`: THD aprox. `5.36%`
- Plexi `Loudness I=0.50`: THD aprox. `8.79%`
- Plexi `Loudness I=0.88`: THD aprox. `15.33%`
- JCM800 `Gain=0.25`: THD aprox. `6.21%`
- JCM800 `Gain=0.50`: THD aprox. `10.06%`
- JCM800 `Gain=0.78`: THD aprox. `16.74%`

Advertencia: esta métrica no reemplaza escucha real en feedBack. Solo sirve para confirmar que el DSP genera armónicos y que no está mudo/cortado.

## Siguiente forma correcta de investigar

Trabajar un amp a la vez. Empezar por Plexi, no por todos los Marshall.

Orden recomendado:

1. Confirmar que feedBack está cargando el mismo bundle que el harness.
   - `~/Library/Application Support/slopsmith-desktop/plugins/rig_builder` es symlink a `/Users/nacho/Files/slopsmith/rig_builder`.
   - Comparar hash del binario si hay duda.
2. Reproducir el tono real de una canción problemática.
   - Leer `preset_pieces.vst_state` desde la DB, no inventar knobs manuales.
   - Renderizar el mismo estado con `tools/render_amp_wav.py`.
3. Comparar tres rutas:
   - DI -> amp VST solamente.
   - DI -> amp VST + cab/IR real usado por feedBack.
   - Cadena completa con pedales/racks/leveler si existe harness disponible.
4. Revisar si el problema perceptual viene de cab/leveler/amp trim después del amp.
   - El amp puede distorsionar, pero un bloque posterior puede nivelar/oscurecer/aplastar el resultado.
5. Si se ajusta el DSP, cambiar solo una variable y medir:
   - preamp drive de la etapa relevante,
   - bright cap/input attenuation,
   - tonestack insertion loss,
   - salida del stage previo al power.

No tocar en la misma iteración:

- PI topology,
- power bias,
- power drive,
- global makeup,
- shared `guitar_amp_core.hpp`,
- varios amps.

## Pistas de dónde mirar

Archivos relevantes:

- `vst/src/amps/plexi/PlexiCore.h`
- `vst/src/amps/jcm800_marsten/Jcm800Core.h`
- `vst/src/_shared/tube_stage.hpp`
- `data/rs_knob_to_vst_param.json`
- `tools/render_amp_wav.py`
- `tools/compare_amp_reference.py`
- `docs/REFERENCE_MATCHING_WORKFLOW.md`
- `vst/src/amps/REAL_TUBE_AMP_GUIDE.md`

DB local:

```text
~/Library/Application Support/slopsmith-desktop/slopsmith-config/nam_tone.db
```

Consulta útil:

```sql
select id, rs_gear_type, file, params_json, vst_path, vst_state
from preset_pieces
where rs_gear_type in (
  'Amp_MarshallPlexi',
  'Amp_MarshallJCM800',
  'Amp_MarshallDSL100H',
  'Amp_MarshallJTM45',
  'Amp_Marshall1962Bluesbreaker',
  'Amp_MarshallDSL15H',
  'Amp_MarshallJVM410H'
);
```

En una inspección previa, la DB sí tenía valores altos para muchos tonos:

- `Amp_MarshallPlexi`: mediana de drive aprox. `0.878`
- `Amp_MarshallJCM800`: mediana aprox. `0.780`
- `Amp_MarshallJTM45`: mediana aprox. `0.871`
- `Amp_Marshall1962Bluesbreaker`: mediana aprox. `0.878`
- `Amp_MarshallDSL100H`: mediana de `Channel` aprox. `0.870`

Por eso no asumir inmediatamente que es solo mapping de gain. Hay que validar con el estado real de una canción concreta.

## Criterio de aceptación

Antes de decir que un amp está arreglado:

- Render con DI real debe tener `dropout_windows=0`.
- `clip_frac=0`.
- Limpio/gain bajo debe sonar y no quedar gateado.
- Gain alto debe tener más armónicos medibles y perceptibles.
- Probar dentro de feedBack después de reiniciar/cerrar backend si el VST ya estaba cargado.
- Pedir confirmación auditiva del usuario en un solo amp antes de propagar el patrón a otros.

## Nota para el próximo agente

El usuario está frustrado con razón por cambios que rompieron amps ya corregidos por su hermano. No repetir una estrategia global. Si se retoma amps, hacerlo con un solo amp, idealmente Plexi, y con A/B reproducible desde una canción real.
