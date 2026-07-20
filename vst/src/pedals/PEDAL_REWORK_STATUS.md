# Pedal Rework Status

Ultima actualizacion: 2026-07-17.

Este archivo deja la cola de pedales antes de cambiar el foco a racks.

## No tocar por ahora

Estos suenan bien y quedan fuera de la cola activa:

- `Pedal_MultiPitch` / `Multi Pitch.vst3`
- `Pedal_OctaveUp` / `OCTUP.vst3`
- `Pedal_Octavius` / `OC-5.vst3`
- `Bass_Pedal_BassSubOctave` / `SO-2.vst3`

## Pendientes conocidos

Estos quedan anotados para una pasada futura:

- `Pedal_RingMod` / `RingMod.vst3`: rework Maestro/Oberheim RM-1A implementado con controles reales, oscilador Wien/LM741 senoidal, multiplicador `MC1495`, buffers `MC1458` y squelch `1N4148`/`2N4360`. La revision 2.1 conserva el dry lineal, suma el efecto sin level-hole y evita que el squelch corte notas debiles. Pendiente validacion auditiva del usuario.
- `Bass_Pedal_NYRBS103` / `NYRBS103.vst3`: tiene VST bundled, pero falta entrada dedicada en `rig_builder/data/rs_knob_to_vst_param.json`.

## Confirmados y cerrados

- `Pedal_AutoFilter` / `BU-TRON III.vst3`.
- `Pedal_BobFilter` / `FM105.vst3`.
- `Pedal_AcousticEmulator` / `Acoustic Emulator.vst3`.
- `Pedal_LineDrive` / `OS-2.vst3`: Color mezcla las ramas tipo SD-1/DS-1; la mitad inferior de Drive conserva dinamica y la saturacion fuerte queda al final del recorrido.
- `Pedal_PlanePhase` / `Rocket Phase.vst3`: estabilidad corregida al cambiar presets/automatizar Rate, Depth y Mix; sin clipping ni dropouts a 44.1/48/96 kHz.
- `Bass_Pedal_BassPhase` / `phase 99.vst3`: smoothing corregido para Speed, Depth, Feedback y Level; eliminados los saltos y cortes transitorios al cambiar estados.

## Pendientes de baja prioridad

Estos existen y estan mapeados, pero no han recibido la misma auditoria fina componente-a-componente que los grupos ya reworkeados:

- `Pedal_NoiseGate` / `NF-1.vst3`
- `Pedal_EQ5` / `EQ5.vst3`
- `Pedal_EQ8` / `GE-8.vst3`
- `Bass_Pedal_BassEQ8` / `GEB-8.vst3`
- `Pedal_USWah` / `Cry Man.vst3`
- `Pedal_UKWah` / `BOX B847.vst3`
- `Pedal_ModernWah` / `Jockey Bad.vst3`
- `Bass_Pedal_BassWah` / `Bass Wah.vst3`
- `Bass_Pedal_BassFilterDelay` / `DL-3.vst3`
- `Bass_Pedal_BassFilterEcho` / `SE-3.vst3`

## Foco siguiente

Validar auditivamente `RingMod.vst3` y ajustar solo contra referencias del RM-1A si aparecen diferencias.
