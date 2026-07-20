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
- `12AT7.pdf` (Tung-Sol): input G-(H+K) = 2.2 pF, Cgp = 1.5 pF.
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
- `PhaseInverterLTP12AT7`: long-tail-pair Fender con 12AT7/ECC81 real. Usar cuando el
  esquematico indique 12AT7 en el PI (ej. Super-Sonic 22), no aproximarlo con 12AX7.
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

Auditoria 2026-07-18 contra `amps/vox ac30 (en30)/Vox_ac30c2.pdf`: el AC30 no es una
cascada comun de tres triodos. Los dos canales nacen en mitades independientes de V1 y se
unen recien antes del inversor de fase:

| ruta | etapa real | componentes/funcion modelada |
|------|------------|-------------------------------|
| Normal | V1 12AX7 common-cathode | placa R14 220k, catodo 1k5, C7 47n, VR1 A500K |
| Top Boost | V1 12AX7 common-cathode | placa R12 100k, catodo 1k5, C9 470p, VR2 A500K |
| Top Boost | V2 12AX7 cathode follower | buffer de baja ganancia con compresion por corriente de rejilla |
| Top Boost | stack Treble/Bass + U1B | VR3/VR4 A1M, R47 10k, R19 100k, C23 56p, C28/C38 22n; recovery limpio ~2.7x |
| comun | V3 12AX7 LTP | inversor long-tail-pair AC30, no una etapa common-cathode adicional |

Cada V1 usa su Miller y acople reales por separado. Normal evita por completo el stack Top
Boost. Para otro amp hay que reconstruir las rutas y puntos de mezcla del esquematico; no
se debe inferir una cascada solo por contar tubos.

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
2. **Confirmar si la referencia tiene cabina.** Los renders Ruby de 2026-07-18 son
   explicitamente `direct`, sin cabina, por lo que se comparan con `Cab Sim=0`. No meter
   un scoop/rolloff de parlante en el amp: se duplicaria al conectar un IR externo.

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
2. **Comparar en el mismo punto de salida.** Para los renders Ruby directos se mide el amp
   pelado (`Cab Sim=0`); la cabina fallback se valida por separado.
3. **El reparto de distorsión preamp↔power se recalibra.** Cuando agregás el power amp real,
   el preamp ya hacía toda la dirt → hay que rebalancear (más breakup de potencia a volumen
   alto) para que el TOTAL siga matcheando. Medí crest a gain 2/5/10 y ajustá `drive` + `out`.

Class A (AC30/Vox, bias caliente ~−7.5, sin NFB) vs AB (Marshall/Fender, bias frío + NFB):
cambia el bias, el sag y si hay NFB. El esquema AC30C2 local usa rectificacion de silicio;
el core conserva deliberadamente la respuesta GZ34 del AC30 vintage para la referencia
Ruby. No cambiar esta decision sin volver a medir sag y transientes.

---

## 8. Calibración objetiva (NO afinar solo de oído)

Aplicar primero el workflow común obligatorio de
[`docs/REFERENCE_MATCHING_WORKFLOW.md`](../../../docs/REFERENCE_MATCHING_WORKFLOW.md).
Esta sección conserva las reglas y resultados específicos de amplificadores.

Workflow para igualar a una referencia comercial (ver [[amp-reference-renders]]):

1. Identificar primero el **DI exacto usado por la referencia** y renderizar ese mismo
   archivo por nuestro core. No comparar una referencia Fast Thrash contra Brit DI ni
   asumir que dos archivos de igual duracion contienen la misma interpretacion:
   - referencias generales de `test logic/dualrect/*.wav` y UAD Ruby AC30:
     `test logic/ce1_ref/brit_di.wav` (Brit DI);
   - referencias `test logic/dualrect/fast_trash/*.wav`:
     `ui_public_inputs_Fast Thrash - Guitar.wav`.
   Recortar DI y DSP a la duracion comun de la referencia, compensar la latencia por
   correlacion y comparar las mismas muestras. Si la referencia termina con silencio,
   excluirlo de las metricas activas en ambos archivos; no medir el DI completo contra
   un render recortado.
2. Comparar contra el **render comercial** (p.ej. UAD Ruby = AC30) las tres métricas:
   - **crest factor** (pico/RMS, dB) = proxy de distorsión. Bajo = más saturado.
   - **RMS** = nivel (para mantener volumen constante entre gains).
   - **espectro por bandas de octava** = voicing/EQ.
   Estas tres métricas no bastan por sí solas: un compresor limpio puede igualar el crest
   sin generar el carácter armónico de la referencia. También medir:
   - **correlación de forma de onda entre gain min/half/max**, nivelando antes de comparar;
     si permanece cerca de 1.0, la perilla apenas está cambiando la no linealidad.
   - **crecimiento espectral con gain**, especialmente 2–4, 4–8 y 8–16 kHz. No basta
     con que el espectro estático sea parecido: los armónicos deben aparecer al subir gain.
   - **trim de loudness después de toda no linealidad**. Una tabla RMS colocada antes de
     un limiter/clipper cambia su drive y vuelve inválida la calibración de distorsión.
   - **comparacion temporal uno-a-uno**: ademas del valor global, medir envolvente RMS
     por ventanas de 50 ms y revisar ataques/sustain en las mismas posiciones. Una
     coincidencia de crest global puede ocultar gating, compresion excesiva o un modo
     que distorsiona en una seccion distinta del riff.
   - **distribuciones por ventanas y cuantiles de pico**: medir crest p10/p50/p90 en
     ventanas de 20-50 ms y p90/p99/p99.9 de `abs(sample)/RMS_ventana`. Esto distingue
     una saturacion sostenida de un limiter que solo captura transientes extremos.
   - **clipping y cortes**: contar muestras no finitas o sobre el techo digital y ventanas
     activas de la DI cuya salida desaparece. Un crest bajo no es valido si viene de
     glitches, hard clipping o una cola cortada.
   - **nivel antes de V1**: verificar la calibracion de entrada del wrapper, no solo la
     salida del core. Un power amp, limiter o tabla de makeup puede bajar crest y hacer
     parecer saturado un render aunque el preamp reciba la DI demasiado baja. Comparar
     tambien una reduccion controlada de input y confirmar que cambien los armonicos,
     no solamente el RMS final.
3. Matchear a los **tres niveles de gain** (la referencia tiene gain 2/5/10).

Lecciones:
- Un **DI de guitarra no mide 4–8 kHz** (no hay energía ahí) → para la curva de EQ del amp,
  usar **ruido blanco** por el core (`/tmp/wn.cpp`), no el DI.
- Para el **feel del gain**, sí usar el DI (crest factor por nivel).
- Settings Ruby 2026-07-18: Treble 5 / Bass 5 en Top Boost; Normal no tiene esas perillas.
  Renders `Cut min/half/max`, todos directos sin cabina.

Harnesses permanentes:

- `tools/render_amp_wav.py` — compila el source del amp y procesa el DI sin UI/presets.
- `tools/compare_amp_reference.py` — alinea y reporta RMS, ventanas, bandas, crest,
  cuantiles de peak, correlacion y coherencia DI/salida.

Ejemplo reproducible:

```sh
python3 tools/render_amp_wav.py tw26 '<DI>.wav' /tmp/tw26.wav \
  'Inst Vol=1' 'Mic Vol=0' 'Tone=1' 'Cab Sim=0'
uv run --with numpy --with scipy python tools/compare_amp_reference.py \
  '<DI>.wav' '<referencia>.wav' /tmp/tw26.wav
```

Probes auxiliares que aun pueden regenerarse en `/tmp`: `gx_tube.py` para tablas,
`stagegain.cpp` para una etapa, `stab.cpp` para estrés y `wn.cpp` para respuesta
con ruido. Nunca dejar la unica evidencia final en un probe temporal.

BOX AC30 2026-07-18, DI Brit, `Cab Sim=0`, Volume min/half/max (revision 1.2.0):
- Normal DSP `15.5/10.8/9.0 dB` vs Ruby `16.6/10.8/10.0 dB`.
- Top Boost DSP `15.4/11.2/10.4 dB` vs Ruby `15.5/11.3/10.8 dB`.
- RMS interno: `-16.00 dBFS` en los tres puntos de ambos canales mediante compensacion
  estatica post-circuito; no cambia drive, sag ni armonicos.
- Revision 1.1.1: Volume alimenta el cathode follower/PI/power con curvas monotonicas;
  la compensacion se aplica solo despues del circuito. Los 11 pasos mantienen nivel plano
  mientras el crest baja continuamente al subir Volume.
- Revision 1.2.0: se detecto que el wrapper limitaba **despues** de la tabla RMS, por lo
  que bajar el nivel también quitaba distorsion. `outputMakeup()` ahora se aplica linealmente
  despues del core y de `rbAmpLvl`. Top Boost crest DSP `15.70/11.53/10.98 dB` contra Ruby
  `15.53/11.31/10.79 dB`; los 11 puntos Top Boost y Normal quedan en `-17.5 dBFS RMS`
  para conservar headroom incluso en el Normal limpio, cuyo crest supera 17 dB.
- Auditoria estricta 2026-07-18, revision 1.3.0: el crest por si solo ocultaba una
  diferencia real. Top Boost maximo da `10.89 dB` vs Ruby `10.79 dB`, pero el centroide/
  flatness son `3325 Hz/0.187` vs `4915 Hz/0.416`; correlacion half-max `0.903` vs
  `0.725`. El AC30 conserva menos crecimiento armonico que Ruby al subir Volume.
- Revision 1.4.0, 2026-07-19: la normalizacion interna del cathode follower que reducia
  su excitacion al subir Volume fue eliminada. El OT ahora aumenta magnetizacion con el
  pote real y usa el ancho amp-only `38 Hz-19 kHz` en vez del limite generico de parlante.
  Top Boost maximo pasa a crest/centroide/flatness `10.47 dB/4102 Hz/0.287`, frente a
  Ruby `10.79 dB/4915 Hz/0.416`; a mitad queda `11.23 dB/2930 Hz/0.159`, frente a
  `11.30 dB/3334 Hz/0.230`. Normal maximo queda muy cerca en dinamica y densidad:
  `10.07 dB/0.195` frente a `10.03 dB/0.196`. La diferencia espectral restante se
  conserva documentada; no se compensa con EQ posterior.
- Las nuevas tablas de Volume se midieron con la DI Brit completa y Master `0.60`.
  Normal y Top Boost quedan en `-18.600 +/- 0.001 dBFS RMS` en min/half/max; el objetivo
  mas bajo conserva `0.31 dB` de margen en el transiente extremo de Normal limpio.
- Comparacion alineada muestra-a-muestra 2026-07-19: se usaron los primeros 32 s activos
  de la misma Brit DI y se compenso la latencia de cada render. Top Boost Volume
  min/half/max da crest `16.13/11.19/10.44 dB` vs Ruby `15.48/11.26/10.76 dB`; por
  tanto la progresion de distorsion queda cerrada. La diferencia pendiente es tonal:
  a Cut minimo el DSP queda aproximadamente `2-6 dB` por debajo de Ruby sobre 4 kHz.
- Revision 1.5.0, 2026-07-19: comparacion directa final contra UAD Ruby con Master
  `0.60`. Dos shelves de respuesta directa, situados antes de la recuperacion U1B,
  devuelven el chime sin ecualizar despues de la cadena no lineal. Top Boost
  min/half/max queda en crest `15.67/11.22/11.00 dB` vs Ruby
  `15.53/11.30/10.79 dB`; Normal queda `17.12/11.42/10.04 dB` vs
  `16.51/10.74/10.00 dB`. La diferencia residual se concentra sobre 8 kHz, donde
  Ruby conserva aproximadamente `4 dB` mas de contenido en los puntos driven; no se
  aumento mas el shelf porque empeoraba los transientes de la DI sin cambiar el
  breakup. El Tone Cut C80/VR9 ahora se procesa entre el LTP y las EL84, con taper
  `pow(1.30)`: en Top Boost maximo su endpoint da crest `7.38 dB` vs Ruby `7.15 dB`.
  Las tablas independientes Normal/Top se recalibraron despues de estos cambios:
  los 11 puntos de cada canal quedan a `-18.600 +/- 0.001 dBFS RMS` a 48 kHz.
  Tambien se valido Normal half y Top Boost max a 44.1/96 kHz, sin clipping ni
  dropouts; la desviacion multirate maxima de nivel fue `0.11 dB`.
  Tone Cut maximo sigue una pendiente similar, pero a mitad nuestro control corta
  alrededor de `2-4 dB` mas entre 2-8 kHz. No modificar sin una prueba auditiva dirigida.
- Cut min→max: DSP/ Ruby entre 1.2–3 kHz `-5.2/-5.3 dB` (Normal) y `-6.7/-6.0 dB`
  (Top); entre 3–8 kHz `-7.9/-8.1 dB` y `-8.6/-8.8 dB` respectivamente.
- Estable sin NaN a 48/96/192 kHz; VST validado a 44.1/96 kHz sin dropouts.

---

## 9. Constantes Koren por tubo (datasheet público)

Para portar otros amps necesitas las constantes de SUS tubos. (mu, ex/kx, kg1, kg2, kp, kvb):
Los PDFs base viven en la carpeta `tubes/` en la raiz del proyecto; cuando exista datasheet
local, usar ese como fuente antes de caer a una tabla generica.

**Triodos preamp generados** (Vsupply 250, Rp 100k; Vgrid segun `gx_tube.py`):
| tubo   | mu    | ex   | kg1    | kp    | kvb   | Vgrid |
|--------|-------|------|--------|-------|-------|-------|
| 12AX7  | 100.0 | 1.40 | 1060.0 | 600.0 | 300.0 | -5..5 |
| 12AY7  | 44.16 | 1.11 | 1192.4 | 409.96| 300.0 | -5..5 |
| 12AT7  | 60.0  | 1.20 | 507.8  | 600.0 | 300.0 | -5..5 |
| 6EU7   | 100.0 | 1.40 | 1060.0 | 600.0 | 300.0 | -5..5 |
| 6SL7GT | 70.0  | 1.35 | 1620.2 | 600.0 | 300.0 | -6..6 |
| 6SF5   | 100.0 | 1.35 | 1148.7 | 600.0 | 300.0 | -6..6 |
| 12AU7  | 17.0  | 1.20 | 1709.7 | 600.0 | 300.0 | -18..8 |
| 7199T  | 17.0  | 1.20 | 1234.6 | 600.0 | 300.0 | -18..8 |

**Pentodos chicos generados** (tabla de etapa con `ranode`, screen doblada al fit):
| tubo  | mu   | ex   | kg1    | kg2   | kp     | kvb | Vsupply | Rp   | Vgrid |
|-------|------|------|--------|-------|--------|-----|---------|------|-------|
| EF86  | 34.9 | 1.35 | 2648.1 | 4267.0| 222.06 | 4.7 | 250     | 330k | -5..5 |
| 5879  | 34.9 | 1.35 | 11933.7| 4267.0| 222.06 | 4.7 | 250     | 220k | -8..5 |
| 7199P | 30.0 | 1.35 | 2996.8 | 4267.0| 222.06 | 4.7 | 250     | 100k | -8..5 |

**Pentodos/beam power generados** (Vsupply/Rp/Vgrid son los de la tabla DSP):
| tubo  | mu    | ex   | kg1   | kp    | kvb  | Vsupply | Rp    | Vgrid |
|-------|-------|------|-------|-------|------|---------|-------|-------|
| EL84  | 21.3  | 1.24 | 401.7 | 111.1 | 17.9 | 300     | 2.0k  | -30..6 |
| 6BM8  | 9.5   | 1.24 | 647.4 | 111.1 | 17.9 | 300     | 2.8k  | -36..6 |
| 6V6   | 12.67 | 1.198| 915.0 | 38.07 | 30.2 | 350     | 2.0k  | -32..6 |
| 6L6   | 8.7   | 1.35 | 1460  | 48.0  | 12.0 | 430     | 2.0k  | -80..6 |
| 6L6G  | 8.7   | 1.35 | 1460  | 48.0  | 12.0 | 360     | 1.65k | -70..6 |
| 5881  | 8.7   | 1.26 | 1210.3| 47.5  | 11.6 | 430     | 2.0k  | -80..6 |
| 6L6GC | 8.7   | 1.35 | 1460  | 48.0  | 12.0 | 450     | 1.4k  | -80..8 |
| KT66  | 11.7  | 1.98 | 510.9 | 34.9  | 22.3 | 470     | 1.65k | -82..8 |
| EL34  | 12.3  | 1.17 | 353.9 | 61.1  | 29.9 | 470     | 1.7k  | -70..8 |

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
   `vst/amps/<NOMBRE>.vst3`, `codesign --force --sign -`. Cmd+Q + reabrir feedBack para probar.

---

## 11. Estado por amp / roadmap

- **BOX AC30 (`en30`)** — ✅ **piloto circuito-real avanzado**, revision 1.5.0 re-auditada 2026-07-19 con referencias UAD Ruby directas y **Master 0.60**. Normal y Top Boost son rutas V1 independientes; Normal evita el stack, Top usa C9 470p + cathode follower + stack Treble/Bass + recovery limpio, y ambas alimentan el LTP/EL84/OT comun. Incluye Miller por V1, acoples con grid-current, `PhaseInverterLTP12AX7::setVoxAc30`, B+ GZ34, potes reales, Tone Cut C80/VR9 antes de las EL84, OT amp-only y cab fallback bypassable. Top Boost min/half/max queda en crest `15.67/11.22/11.00 dB` vs Ruby `15.53/11.30/10.79 dB`; sobre 8 kHz Ruby conserva aproximadamente 4 dB mas de contenido driven. Las tablas lineales estrictamente post-no-linealidad mantienen los 11 puntos de ambos canales en `-18.600 +/- 0.001 dBFS RMS`, sin clipping ni dropouts a 44.1/48/96 kHz.
- **Bender Deluxe (`tw26`, 5E3, v2.4)** — recalibrado 2026-07-19 contra las 21 referencias locales enlazadas al Brit DI exacto: Instrument, Mic, jumpered, Tone min/half/max e Input 2. La auditoria visual del esquema Fender 57 corrigio V1A/V1B a 12AX7A (no 12AY7) y reemplazo la suma de canales por una solucion nodal de R10/R11/R12, C2/C3 0.1uF, C4 500pF y C5 4.7nF; esto conserva la conexion real de las senales a los wipers y la interaccion de los dos volumenes. Input 2 usa el divisor 68k/68k. V2A 12AX7 alimenta el cathodyne 56k/56k, `MultiNodeBPlus` modela 5Y3 + 16uF + 4k7/22k y 2x6V6 sin NFB llegan al OT electrico; Cab Sim sigue bypassable y separado. La v2.4 corrige el fallo que dejaba los extremos `inst_max_tone_max` y `mic_max_tone_max` casi limpios: sobre 0.58 el control ahora aumenta progresivamente el drive fisico de V2/PI/6V6, mientras el gain posterior solo nivela. A maximo, la coherencia DI/salida por bandas queda Instrument `0.682/0.379/0.039` vs referencia `0.633/0.397/0.051`, y Mic `0.661/0.378/0.037` vs `0.654/0.345/0.030`; el voicing cranked queda dentro de aproximadamente 1 dB por banda salvo el subgrave de Mic. Las tablas post-circuito de 21 puntos para Instrument, Mic y jumpered mantienen `-21.50 +/- 0.001 dBFS RMS` sin realimentar tubos ni limiter. Validado sin clipping ni dropouts a 44.1/48/96 kHz. La validacion debe conservar comparacion por ventanas y coherencia contra la DI; igualar RMS/crest/bandas por si solos no demuestra identidad de la distorsion.
- **Bender Bassman (`tw40`, 5F6-A, v2.4)** — las seis referencias Bright/Normal min/half/max se comparan con la misma Brit DI. La respuesta de magnitud queda dentro de aproximadamente 1.5 dB por banda en los casos medidos; el empuje cranked del PI se calibro solo sobre el tramo superior del A1M para acercar la coherencia no lineal del 5881 a la referencia sin alterar clean/noon. Las curvas post-circuito Bright, Normal y jumpered se volvieron a medir a 21 puntos para sostener -21.50 dBFS durante el barrido de volumen.
- **Super-Sonic 22 (`tw22`)** — ✅ reauditoria 2026-07-19: el esquema no es Vintage 1× / Burn 3×. V1A es comun y la ruta real queda Vintage `V1A -> tone/Volume -> V1B`; Burn `V1A -> Gain 1 -> V1B -> Gain 2 -> V2A -> divisor 470k/100k -> V2B -> TMB`. Se añadieron el cuarto triodo Burn, Miller por cada entrada y los coupling reales C2=.0022µF, C9=.22µF, C20=.047µF y C14=.22µF. Gain 2 conserva el piso R36=20k/R38=10k y concentra su subida al final del pote. Los tonestacks siguen con valores exactos: Vintage 250k/250k/6.8k-fijo/100k · 250pF/.1µF/.047µF; Burn 250k/250k/25k/120k · 150pF/.15µF/.022µF. PI `PhaseInverterLTP12AT7`, supply silicon `MultiNodeBPlus`, `PowerAmp6V6` y V30 bypassable. Las diez referencias Gain1/Gain2 con Brit DI quedan dentro de 0.9 dB RMS antes del ultimo trim de 0.75 dB; en el punto half/half final, RMS +0.08 dB, bandas 80 Hz-5 kHz dentro de 1.1 dB y sin dropout. La configuracion fija de tubos/PI/power ya no se reinicia al mover parametros. El morph Vintage/Burn tiene compensacion post-circuito para evitar el salto de volumen del Gain del juego.
- **Bassman (`tw40`, 5F6-A, v2.3)** — ✅ recalibrado 2026-07-19 contra las seis referencias locales enlazadas al Brit DI exacto, con alineación y ventanas activas de 50 ms. V1 usa 2×12AY7 con los 100k/820R/250uF del plano, V2A 12AX7 con 100k/820R sin bypass, `ToneStackYeh` 250k/1M/25k/56k · 250pF/20nF/20nF, LTP 12AX7 82k/100k · 470R/10k, `MultiNodeBPlus` GZ34/choke y 2×5881. Presence quedó dentro del lazo de salida y el límite de excursión 5881/OT ocurre antes de la respuesta lineal final del transformador. La revisión fat baja la esquina sintética que llegaba a 120 Hz y añade la resonancia reactiva del OT a 90 Hz: Bright máximo queda a -0.34 dB en 80-250 Hz y el resto de 180 Hz-10 kHz dentro de aproximadamente +/-1.25 dB; el crest p50/p90 queda a +0.60/+0.66 dB. La normalización se aplica después del oversampling: tablas de 21 puntos para Bright, Normal y la ruta real Jumpered (Normal 0.4 + Bright sweep), sin alimentar tubos ni limiter. Bright/Jumpered quedan en -21.50 dBFS RMS; el extremo Normal 0-0.1 se deja deliberadamente más bajo para conservar su crest limpio sin clipping. Validado a 44.1/48/96 kHz sin clipping, no-finitos ni dropouts, con Cab Sim apagado.
- **Marshall JTM45 / Bluesbreaker (`jtm45_marsten`, `bluesbreaker_marsten`)** — ✅ correccion 2026-06-22: usan la rama 5881/6L6 de 30W con GZ34, no la tabla KT66. En DSP quedan con `PowerAmp5881`, `PhaseInverterLTP12AX7`, `MultiNodeBPlus`, Miller por entrada/recovery y ahora `CouplingCapGridLeak` separado en cada canal V1→mixer/V2a y hacia PI. Reservar `PowerAmpKT66` para BT45 u otros esquemas que confirmen KT66. PENDIENTE prueba en vivo/crest post-migracion.
- **Marshall Plexi (`plexi`, 1959 Super Lead)** — ✅ reconstruccion 3.0.0 de 2026-07-19 contra `1959-01-60-02.pdf` y las nueve referencias locales. Rutas V1 High Treble 100k/2k7/.68u y Normal 100k/820R/330u independientes; A1M Loudness despues de V1, acoples 2n2/22n, mixers 470k con bypass 470p, V2A recovery, V2B cathode follower, stack `250k/1M/25k/33k · 470p/22n/22n`, LTP ECC83 100k/82k, 22n/220k hacia 4xEL34 y fuente de silicio mult nodo. Se elimino el LTP 12AT7/Fender y se corrigio el helper que usaba por error el tail 10k como Rk de cada triodo, causa del silencio/glitch de la implementacion anterior. La carga Miller completa del canal Normal y su perdida impulsada reproducen su ancho menor sin oscurecer Bright/jumpered. Crest min/half/max queda Bright `20.50/14.17/7.49` vs referencia `21.49/14.02/7.59`; Normal `15.71/15.23/6.71` vs `15.60/15.51/6.51`; jumpered `18.49/10.94/8.63` vs `18.87/10.76/7.42`. El wrapper trabaja a `0.50x`, fuera de la rodilla de seguridad en los puntos medidos; la distorsion viene del circuito. Tablas lineales estrictamente post-DSP mantienen los 11 pasos de cada perfil a `-21.600 dBFS RMS`. A 44.1/96 kHz, Bright min y jumpered max quedan entre `-21.628` y `-21.438 dBFS`, sin clipping ni dropouts; queda pendiente solamente la prueba auditiva en vivo.
- **Marshall JCM800 (`jcm800_marsten`, 2204 master-volume)** — ✅ 3×12AX7 cascada (V1a→GAIN→V1b→V2) + tonestack Marshall TMB (ya era real: Treble 220k/Bass 1M/Mid 25k/slope 33k · 470pF/22nF/22nF) + power (EL34 PP). Core inline en `Jcm800Plugin.cpp` (estéreo L/R), oversampling 2× + 3.2× input agregados. Correccion 2026-06-22: el cascade 2204 ahora incluye `CouplingCapGridLeak` entre V1a→V1b, V1b→V2 y hacia PI, por lo que el bloqueo/grid-current ya no queda omitido en la zona donde el master-volume empieza a comprimir. Crest matchea `jcm800_preamp_1/4/7/10` (13.1/9.6/8.4/7.6 vs ref 12.8/7.3/6.9/6.7 — g1 casi exacto, medio ~2 dB más limpio). PENDIENTE probar en vivo. ⚠️ misma lección Plexi + dos más: (a) el `cleanMakeup` viejo INVIERTE la curva de crest (mete el tono limpio en el `softClip` de salida) → eliminarlo, dejar solo `softClip(y*level)` suave como saturación de OT; (b) el voicing de parlante viejo traía boosts irreales (+16.5 dB fizz) que inflan el crest sin distorsionar → un cab real ATENÚA los agudos (shelf negativo + LP ~11 kHz).
- **Marshall DSL15 + JVM410 (`dsl15_marsten`, `jvm410_marsten`)** — ✅ correccion 2026-06-22: ambos ya tenian 12AX7, tonestacks Yeh, PI, B+ y power real; faltaban acoples con memoria dentro de las cascadas. DSL15 ahora tiene `CouplingCapGridLeak` por rama Classic/Crunch/Ultra y antes del cascade extra; JVM410 tiene acoples por rama Clean/Crunch y entre OD1/OD2. PENDIENTE prueba en vivo/crest post-migracion.
- **Dr. Z EMS / MrY (`ems_mry`)** — ✅ correccion Marshall-family 2026-06-22: mantiene 12AX7 + EL34 PP, pero el cascade preamp ahora replica el mismo comportamiento de acoplo/bloqueo que el JCM800 (`CouplingCapGridLeak` entre V1a→V1b, V1b→V2 y hacia PI). Esto corrige el caso donde el modelo compilaba con bloques modernos pero aun saltaba directo entre etapas de alta ganancia. PENDIENTE prueba en vivo/crest post-migracion.
- **Mesa Dual Rectifier (`dual_rect`, 3-channel Solo Head)** — ✅ revision 2.9.0 re-auditada 2026-07-19 contra las 12 referencias Fast Thrash con **Master de canal 0.60**. Mantiene la topologia del esquema: Green `V1A -> stack/gain -> V2A -> V1B`; Orange/Red `V1A -> gain -> V2A -> V2B cold clipper 100k/39k -> V3A -> V3B follower -> stack/master`, PI 12AX7 LTP y 4x6L6GC. Conserva la calibracion `3.2x` (`+10.1 dB`) antes de V1 solo en Orange/Red; Green queda sin cambios. La 2.7 corrigio el caso donde crest/centroide globales ocultaban una distorsion demasiado redonda: V2B tiene region lineal y rodilla corta, V3A recibe `0.65-0.88x`, Vintage baja el power drive a `0.90` y Modern abre el power a `1.15/0.95` Orange/Red. La 2.8 corrige el caso donde Modern se enterraba despues de una cabina: el shelf util comienza en `1.8 kHz`, el notch de `4.8 kHz` baja de `-7.5` a `-6 dB`, el hueco de cuerpo de `950 Hz` se reduce y el aire ancho sube `1 dB`. Con la 4x12 Marsten, CH3 Modern queda a `-0.1/+1.0 dB` de la referencia entre `2.5-4 kHz` en gain half/full y a `-0.8/-0.6 dB` entre `6-10 kHz`; antes quedaba hasta `2.4 dB` abajo. La 2.9 conserva Modern sin cambios y repone la resonancia OT/parlante que estaba neutralizada a `0 dB` en Raw/Vintage: `+2.0/+1.5 dB` a `135 Hz`, Q `0.72`, para recuperar el peso del palm mute sin modificar sus agudos. Las cuatro tablas Raw/Vintage se recalibraron en sus 11 pasos despues del cambio. Todos los modos quedan en `-17.500 dBFS` en los puntos medidos, sin clipping ni dropouts. Validado a 44.1/48/96 kHz con desviacion maxima de nivel `0.05 dB`. La asociacion de referencias sigue siendo estricta: Brit DI para las generales y Fast Thrash DI solo para `dualrect/fast_trash`, con alineacion temporal uno-a-uno.
- **Marshall DSL100H (`dsl100`, JCM2000 Dual Super Lead)** — ✅ 2 canales (Classic Clean/Crunch + Ultra OD1/OD2) en una cadena con morph paralelo (el knob de Gain de RS barre Classic clean→crunch→Ultra). 6× `TubeStage` 12AX7 (V1 compartido + paths clean/crunch/ultra + cascade extra) + `PowerAmpEL34` (4×EL34) + tonestack **circuito-real Yeh con valores JCM2000** (Treble 250k/500pF · Bass 1M/22nF · Mid 25k/22nF · slope 56k). Tone Shift (mid-scoop), Presence/Resonance (NFB), Low/High → `sagDepth`. Agregué oversampling 2× (no lo tenía). **SIN render de referencia** → calibrado por carácter+topología (crest monotónico Classic clean 13.8 → crunch 12 → Ultra 7.9, estable). PENDIENTE probar en vivo. ⚠️ mismas lecciones: piso bajo de `power drive` para que el limpio quede limpio; `cleanMakeup` reducido (invertía la curva); cab fizz +9.5 dB → atenúa.
- **Mesa Mark III (`mark_iii`) + Mark II (`mark_ii`)** — ✅ los amps "lead" Boogie (Santana/Metallica). 2 voces en una cadena: RHYTHM (Volume→Master) y LEAD (Lead Drive cascada→Lead Master), elegidas por el switch. 4× `TubeStage` 12AX7 (V1 + rhythm + 2 cascada lead) + `PowerAmp6L6GC` (Simul-Class ~75-100W). El **tonestack ya era circuito-real** (clase propia `MarkToneStack`) PERO en float → **NaN a 192k**; lo cambié a `rbtube::ToneStackYeh` (double, mismos valores: Treble 250k/250pF · Bass 1M/22nF · **Mid 10k** scoopeado/22nF · slope 100k). Mark III tiene el **graphic EQ de 5 bandas** (80/240/750/2200/6600, la "V" Boogie) ya real; Mark II tiene pulls Shift/Bright/GainBoost/HalfPower + reverb spring. Agregué oversampling 2×. **Sin render de referencia** → por carácter: crest monotónico Rhythm 18.3 limpio → Lead 9.8 (III) / 10.0 (II — un pelo menos searing, correcto). PENDIENTE probar en vivo. ⚠️ **LECCIÓN NUEVA: un tonestack de 3er orden en `float` se va a NaN a 192k (= 96k host × 2× OS) — usar siempre `ToneStackYeh` (double) para los TMB de 3 bandas.**
- **Laney AOR 50 (`aor50`, A50 Series II "Pro Tube Lead", = el RS Amp_GB100)** — ✅ **HÍBRIDO tubo+diodo**. 2 canales (Channel One limpio + AOR lead) en morph (RS Gain). Del esquemático `Laney_aor50_series2.pdf`: 5× ECC83 (V1B/V2A/V2B/V3A/V3B) + **clipper de diodos 1N4148** (D3/D4 en torno a un op-amp = el "Advanced Overdrive Response") en el path del lead + power EL34 (rectificador silicio = tight). Porté con `TubeStage` (los 5) + `DiodeClipper` (el mismo Shockley del JC, en serie tras el cascade del AOR) + `ToneStackYeh` con valores reales del esquemático (Treble 220k/470pF · Bass 1M/22nF · Mid 22k/22nF · slope 33k = stack JCM800) + `PowerAmpEL34`. Pull-Deep/Mid-Boost como shelves. Oversampling 2×. **Sin render de referencia** → por carácter: crest Channel One 12.6 (limpio rock) → AOR lead 7.4 (crunch duro tubo+diodo), monotónico, estable. PENDIENTE probar en vivo. ⚠️ **el diode clipper en serie tras los tubos = cómo se modela un overdrive híbrido** (no todo es tubo; el carácter agresivo del AOR es el diodo).
- **Framework `tube_stage.hpp`:** templates `TubeStageT`/`PowerAmpPPT` + traits. Tubos con tabla generada: 12AX7, 12AY7, **12AT7**, **6EU7**, **6SL7**, **6SF5**, **12AU7**, **7199T**, EF86, **5879**, **7199P**, EL84, **6BM8/ECL82**, 6V6, 6L6 legacy, **6L6G**, **5881**, **6L6GC**, **KT66**, EL34. EQ circuito-real: `ToneStackYeh` (TMB de 3 bandas, R/C de cualquier amp) + `TweedTone` (control 1-perilla 5E3). Bloques avanzados: `CouplingCapGridLeak`, `PhaseInverterLTP12AX7`, `PhaseInverterLTP12AT7`, `PhaseInverterLTP12AU7`, `PhaseInverterCathodyne12AX7`, `MultiNodeBPlus`, `PotTaper`. Distorsión solid-state/diodo: `DiodeClipper` (Shockley, Newton — JC90/JC120/AOR50). Agregar un tubo = constantes Koren en `gx_tube.py` + traits.
- **Hiwatt DR103 (`dr103_lovolt`, = RS Amp_HG100) + DR504 (`dr504_lovolt`, = RS Amp_HG500)** — ✅ los amps **LIMPIOS** de alto headroom (Townshend/Gilmour), parody "Lovolt" (Hi-watt→Lo-volt). DR103 = 100W 4×EL34, DR504 = 50W 2×EL34, mismo preamp 3×ECC83 (Normal + Brilliant jumpereables). Ya tenían `HiwattToneStack` real PERO en **float → NaN a 192k** (igual que el Mark) → cambiado a `ToneStackYeh` (double): Treble 250k/250pF · Bass 500k/22nF · **Mid 100k/22nF** (el pot de medios grande = los medios fuertes Hiwatt) · slope 56k. 3× `TubeStage` (vBright/vNormal/vRecovery), PI ahora `PhaseInverterLTP12AT7` real para el ECC81/12AT7 Hiwatt, `PowerAmpEL34` (sag MUY bajo = stiff supply) + 2× OS. **Sin render** → por carácter: se mantiene LIMPIO (crest ~16→14, rompe tarde) — eso ES el Hiwatt; el RS Gain→Brilliant Vol da una progresión sutil. PENDIENTE en vivo. ⚠️ lección: un amp de alto headroom su crest lo domina el cab/normalización, NO el preamp — no forzar breakup; quedaría irreal. (HG100/HG500 estaban mal curados como Peavey en rs_to_real — son Hiwatt, ya mapeados a LovoltDR103/DR504.)
- **Budda/Ganddi SuperDrive 45 (`superdrive45`, = RS Amp_BT45)** — ✅ port circuito-real 2026-06-22. Local: `amps/Budda SuperDrive 45 (BTQ_45)/Superdrive45_manual.pdf` + `BuddaSuperdrive80Schematic.jpg` + datasheet Marconi `tubes/KT66.pdf`. 2 canales: Rhythm limpio/edge + Hi-gain cascado con Drive; pull Modern = scoop/lift en hi-gain; pull Brite = treble boost en Rhythm. Implementado con `TubeStage` 12AX7 por etapa (input, rhythm, 3 etapas lead, PI/driver), `ToneStackYeh` double con valores Budda TMB (Treble 500k/220pF, Bass 500k/22nF, Mid 50k/22nF, slope 56k), `PowerAmpKT66` propio (tabla Koren generada con punto Marconi intermedio: ~470V, ~6.6k a-a, bias ~-40V) con sag 5AR4, y oversampling 2×. PENDIENTE prueba en vivo y calibracion final por crest/RMS si aparece render de referencia. Nota: el esquematico local parece familia SD80, pero el manual BT45 confirma KT66 + selector tube/solid-state; el modelo sigue el power del 45.
- **Orange/Matchless/Dr Z extra (`or50_citrus`, `or100_citrus`, `chieftain_unparallel`, `dc30_unparallel`, `maz38_mry`)** — ✅ correccion 2026-06-22: OR50/OR100 ahora tienen `CouplingCapGridLeak` entre etapas de preamp y hacia recovery/PI, con shelves de fizz bajados para no simular cab artificial excesivo. Chieftain y Maz38 ahora modelan el acople real hacia V2. DC30 reemplaza el HP fijo de la coupling bright 560pF+180pF por `CouplingCapGridLeak` con el mismo corte. PENDIENTE prueba en vivo/crest.
- **Orange/Gibson/Epiphone vintage (`ad50_citrus`, `orangetinyterror_bigtremor`, `orangerockerverb_rumbleverb`, `gibsonga88_hipzon`, `gibsonga8_hipzon`, `epiphonecentury_centura`, `epiphonezephyr_ruby`, `gibsonga79_hipzon`)** — ✅ correcciones 2026-06-22/23: AD50 deja de usar `asymTube` en la ruta principal y pasa a 4× `TubeStage` 12AX7 con Miller + acoples entre etapas; Tiny Terror/Rockerverb/GA88/GA8 mantienen su solver nodal de triodo pero ahora tienen `CouplingCapGridLeak` entre V1/V2 o rama clean/dirty antes del power/PI. GA8 ya no usa EL84 como fallback: `PowerAmp6BM8` generado desde `tubes/6BM8.pdf` (RCA, punto 200V/-16V/35mA). Ruby ya usa 5879 + 6SL7 + 6SF5 con Miller por datasheet y `PowerAmp6L6G` generado desde `tubes/6L6G.pdf` (Sylvania, PP AB1 360V/270V, Raa 6.6k, bias -22.5V). GA79 ya usa 6EU7 en preamp, 7199P/7199T en el circuito de reverb y `PhaseInverterLTP12AU7`; su power queda en `PowerAmpPP` porque 6BQ5/EL84 es la misma familia funcional. PENDIENTE prueba en vivo/crest post-cambio.
- **Solid-state / híbridos / DI:** JC90/JC120 (Roland Jazz Chorus) = solid-state puro (op-amp lineal + `DiodeClipper` + power lineal sin sag + chorus BBD), NO tubos. VH140C = solid-state high-gain con `DiodeClipper`; Polystone MiniBrute/JimmyBean = solid-state limpio/sustain; VS100 = híbrido Valvestate con 12AX7 starved y ahora acople hacia esa grilla; UA610 = 12AX7+12AY7 DI limpio y ahora acople V1→V2; Meve1073 = DI consola class-A transistor/iron. Ver [[slopsmith-amp-dsp-vst-not-nam]].
- **ENGL/Engel Fireball (`engel_fireball`, = RS Amp_EN50)** — ✅ auditado en codigo el 2026-06-22: ya usa `tube_stage.hpp`, 12AX7 Koren, `ToneStackYeh`, `PowerAmp6L6GC` y oversampling 2× contra el esquematico local `amps/ENGL Fireball (EN-50)/`. El roadmap viejo lo marcaba como proximo pendiente; eso estaba desactualizado. PENDIENTE probar en vivo y documentar crest final.
- **Puerta final de todos los ports auditados:** no asumir que estan terminados solo por compilar. Cada uno requiere prueba en vivo, loudness/crest y ajuste fino contra render de referencia si aparece.
- **Fuera de la cola de Nacho:** todos los `Bass_Amp_*` y `DI_Amp_BassDriver` quedan para la cola de bajo. `Amp_AT20` se trata igual porque el mapeo local dice que es **Ampeg SVT-CL** y reutiliza `SamplegSBTCL` con `Bass_Amp_BT975B`.
- **Pendientes de guitarra/DI por uso, excluyendo bajo:** cola inmediata limpia al 2026-06-23; quedan pruebas en vivo/crest y renders de referencia. Ruby/GA79 ya tienen sus tubos raros con tablas locales.
- **Patrón de port para los pendientes:** swap `triode/asymTube`→`TubeStage`, `power tanh`→`PowerAmpPPT`, agregar oversampling 2× al Plugin, calibrar crest vs render de referencia (o por carácter si no hay render).
- **Bloqueante para portar cualquiera:** necesitamos la **topología/esquemático** del amp
  (valores de §5) + un **render de referencia** para calibrar. Sin eso solo se puede adivinar.

Memorias relacionadas: arquitectura y harnesses en `slopsmith-amp-dsp-guitarix-redo`;
referencias de A/B en `amp-reference-renders`.
