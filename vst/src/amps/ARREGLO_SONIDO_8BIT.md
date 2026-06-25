# Arreglo del sonido "8-bit" / áspero en amps de bajo

**Caso de referencia:** Lovolt 100 (Custom Hiwatt 100 / DR103), bass voicing
(`vst/src/amps/ht100b_lovolt/`). Sonaba "pésimo, casi 8-bit" in-app. Arreglado el
2026-06-24 reescribiendo el core al patrón limpio del framework.

Este doc explica **qué** lo causó y **cómo arreglarlo** para que apliques lo mismo
a tus amps si te pasa.

---

## 1. El síntoma

El amp suena áspero / crujiente / "8-bit" / como bitcrusheado, sobre todo con
bajo (notas graves fuertes). No es zumbido ni distorsión musical: es textura
**digital sucia** que aparece y empeora cuando le metes señal caliente.

## 2. La causa (importante: NO era el oversampling)

El Lovolt venía portado **tal cual del amp de guitarra** `dr103_lovolt`, que usa
una cadena nodal pesada:

- `rbtube::MultiNodeBPlus supply;` — fuente B+ con **sag** (caída de tensión).
- `rbtube::Miller12AX7` en cada etapa (bright / normal / recovery).
- Un **lazo de realimentación de carga de 1 muestra**: el core calcula la carga
  de cada etapa y la mete a la fuente en la muestra siguiente:

```cpp
// dr103_lovolt/Dr103Plugin.cpp (el patrón PESADO — lo que causó el problema)
const rbtube::SupplyScales bplus =
    supply.process(lastPowerLoad, lastScreenLoad, lastPreampLoad);  // <- lazo
...
y = power.process(y * bplus.power * bplus.screen);
lastPowerLoad  = 0.60f * std::fabs(y) + 0.10f * pushed;             // <- se realimenta
lastScreenLoad = 0.38f * std::fabs(y) + 0.05f * preDrive;
lastPreampLoad = 0.07f * std::fabs(y) + 0.03f * preDrive;
```

Ese lazo `carga → supply → ganancia → carga` modula el B+ a velocidad de audio.
Con **guitarra** se porta bien (señal con menos energía en graves). Con **bajo**,
las excursiones graves grandes empujan el lazo a un régimen de intermodulación /
casi-inestabilidad → eso es el "8-bit".

> El `dr103_lovolt` **ya corre a 2× oversampling** y aun así sonaba mal portado a
> bajo. Conclusión clave: **subir el oversampling NO arregla esto.** El problema
> es la topología del lazo de sag + Miller, no el aliasing.

## 3. El arreglo: patrón limpio del framework

Reescribir el core con los bloques **estables y acotados** de
`_shared/tube_stage.hpp` — los mismos que usan `CitrusCore` / `RumbleCore` (que
suenan bien). **Sin** `MultiNodeBPlus`, **sin** `Miller`, **sin** lazo de carga:

```cpp
// ht100b_lovolt/LovoltCore.h (el patrón LIMPIO — el arreglo)
rbtube::HP1 inCoupling;              // acople de entrada (graves)
rbtube::TubeStage vN, vB, v3;       // 12AX7: canales + recovery
rbtube::ToneStackYeh tone;          // tone stack pasivo (Yeh)
rbtube::PhaseInverterLTP12AT7 pi;   // PI LTP
rbtube::PowerAmpEL34 power;         // etapa de potencia (tabla real)
rbtube::LP1 otVoice;               // roll-off del OT

inline float process(float x){
    x = inCoupling.process(x);
    const float n = vN.process(x * gN);
    const float b = vB.process(brightShelf.process(x) * gB);
    float y = 0.6f*n + 0.6f*b;
    y = tone.process(y);
    y = presenceShelf.process(y);
    y = v3.process(y);
    y = pi.process(y * piDrive);
    y = power.process(y);
    y = otVoice.process(y);
    return y * outLevel;            // makeup de loudness, gain-dependiente
}
```

Sigue oversampleado 2× en el wrapper (`LovoltPlugin` usa `rbshared::Oversampler4x`,
`OS=2`) y con protección de denormales (`rbtube::dn(...)` en los biquads). El
sonido pasa a ser limpio y con cuerpo, sin la textura digital.

La fidelidad al circuito **no se pierde**: el carácter Hiwatt vive en el tone
stack (componentes reales), las etapas 12AX7, el PI y la EL34 — no en el lazo de
sag. El sag aporta un matiz muy sutil que no justifica el riesgo de aspereza.

## 4. Resumen antes → después

| | Pesado (causaba 8-bit) | Limpio (arreglo) |
|---|---|---|
| Fuente B+ | `MultiNodeBPlus` con sag | sin sag (supply rígido implícito) |
| Capacitancia Miller | `Miller12AX7` por etapa | no se usa |
| Lazo de carga | `lastPowerLoad/Screen/Preamp` realimentados | **eliminado** |
| Etapas | nodales a mano | `TubeStage` / `PowerAmp*` del framework |
| Oversampling | 2× | 2× (igual) |

## 5. Checklist para tus amps

Si un amp tuyo suena "8-bit" / áspero, sobre todo con bajo:

1. **Mira si portaste un core de guitarra con `MultiNodeBPlus` + `Miller` + lazo
   de `lastXxxLoad`.** Ese es el sospechoso #1. (`grep -rn "MultiNodeBPlus\|lastPowerLoad" vst/src/amps/TU_AMP/`)
2. **No subas el oversampling esperando arreglarlo** — no es aliasing.
3. **Reescribe al patrón limpio** (mira `CitrusCore.h`, `RumbleCore.h`,
   `LovoltCore.h` como plantillas): `HP1` entrada → `TubeStage` × etapas →
   `ToneStackYeh` (componentes reales del esquemático) → presence shelf →
   `TubeStage` recovery → `PhaseInverterLTP*` → `PowerAmp{EL34,6L6GC,6550,KT88}`
   → `LP1` OT → `outLevel`.
4. **Mantén** 2× oversampling en el wrapper + `rbtube::dn()` en los biquads
   (anti-denormal).
5. **Loudness:** `outLevel` gain-dependiente dentro del core + `rbAmpLvl` (soft
   knee) + `kXxxMakeup` en el wrapper. Mide con `tools/measure_amp_loudness.py`
   y apunta a la familia (~−14 LUFS tras el trim del rig). Ver `AMP_LOUDNESS`.
6. Verifica DI offline con el harness de audit antes de dar por bueno (peak <
   ~0.9 = limpio, sin pinchar el knee de `rbAmpLvl`).

> Regla general: el sag/lazo de carga es un lujo frágil. Para bajo, prioriza la
> ruta estable del framework. El carácter del amp está en el tone stack + las
> tablas de válvula, no en el lazo de la fuente.

Ver también: `REAL_TUBE_AMP_GUIDE.md`, `COMO_PORTAR_AMPS.md`, `INPUT_CALIBRATION.md`.
