# Pedal Rework Status

Ultima actualizacion: 2026-07-01.

Este archivo deja la cola de pedales antes de cambiar el foco a racks.

## No tocar por ahora

Estos suenan bien y quedan fuera de la cola activa:

- `Pedal_MultiPitch` / `Multi Pitch.vst3`
- `Pedal_OctaveUp` / `OCTUP.vst3`
- `Pedal_Octavius` / `OC-5.vst3`
- `Bass_Pedal_BassSubOctave` / `SO-2.vst3`

## Pendientes conocidos

Estos quedan anotados para una pasada futura, pero no son foco inmediato porque ahora el trabajo sigue en racks:

- `Pedal_AutoFilter` / `BU-TRON III.vst3`: revisar el comportamiento de envelope/auto-wah. El usuario reporto que no suena como envelope filter real.
- `Pedal_BobFilter` / `FM105.vst3`: revisar el barrido/envelope. El usuario reporto que no se percibe el efecto auto-wah esperado.
- `Pedal_LineDrive` / `OS-2.vst3`: revisar la distorsion/blend OD+dist; quedo reportado que no suena bien.
- `Pedal_RingMod` / `RingMod.vst3`: no es prioridad; suena usable, pero podria mejorarse mas adelante.
- `Bass_Pedal_NYRBS103` / `NYRBS103.vst3`: tiene VST bundled, pero falta entrada dedicada en `rig_builder/data/rs_knob_to_vst_param.json`.

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

Continuar con `rig_builder/vst/src/racks/`.
