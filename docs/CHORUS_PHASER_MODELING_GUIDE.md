# Guía de modelado circuit-real para chorus y phasers

Estado: 2026-07-15

Esta guía documenta el método aprendido al rehacer y calibrar el Boss CE-1,
MXR Phase 90, Ibanez PH99, Boss PH-1R y Roland AP-7. El objetivo es que los
próximos chorus y phasers se construyan desde el esquema y terminen con un
comportamiento musical verificable, sin caer en un efecto genérico o exagerado.

La regla principal es:

> El esquema define la topología y los controles. Las referencias de audio
> calibran los comportamientos que el papel no describe por completo. No se
> deben copiar las constantes de un pedal a otro.

## 1. Qué significa traducir un esquema a DSP

No se traduce una lista de componentes aislados. Primero se reconstruye el
grafo de señal completo:

1. Entrada, impedancia y acoplamiento.
2. Ganancia, bias y headroom del preamplificador.
3. Separación de las rutas dry y effect.
4. Elemento que produce la modulación: BBD en chorus, all-pass en phaser.
5. LFO, clock y elemento de control: JFET, LDR, optoacoplador o transistor.
6. Feedback, resonance, compander, noise killer y filtros auxiliares.
7. Mezclador de salida y matriz mono/estéreo.
8. Etapa de salida, capacitores de acoplamiento y nivel final.

Cada cable importa. Una ruta tomada antes o después de un filtro cambia el
resultado aunque todos los valores R/C estén presentes en el código.

### 1.1 Traducción de componentes

| Elemento del esquema | Representación DSP | Qué no hay que inventar |
|---|---|---|
| Resistencia + capacitor | Polo/cero derivado de sus valores, normalmente `fc = 1/(2*pi*R*C)` | Un cutoff elegido de oído sin comprobar la red |
| Capacitor de acoplamiento | High-pass junto a la impedancia que ve a continuación | Un high-pass global que ignore la carga real |
| Potenciómetro | Valor variable con taper lineal, log, audio o reverse-log | Mapear siempre `0..1` linealmente |
| Switch | Cambio de topología, componentes o rutas | Mezclar dos modos si el switch real los conmuta |
| Op-amp/CI | Ganancia cerrada, ancho de banda, filtros alrededor, bias, rails y saturación si es alcanzable | Un `tanh` genérico en cada etapa |
| Transistor | Bias, ganancia local, headroom asimétrico y redes de entrada/salida | Distorsión fuerte si trabaja como buffer limpio |
| JFET como VCR | Resistencia no lineal controlada por voltaje, con rango y matching | Convertir cada etapa en un sweep distinto |
| LDR/optoacoplador | Curva luz-resistencia, lag de ataque/relajación y tolerancia entre unidades | Invertir un opto respecto del otro sin que lo indique el esquema |
| BBD | Delay por etapas, clock variable, ancho de banda, pérdida, ruido y pequeña THD | Un delay digital limpio sin límites de clock |
| Compander | Compresión antes del bloque ruidoso y expansión inversa después | Usar el compresor como drive y olvidar la expansión |
| Feedback | Estado recirculado con signo y punto de inyección del esquema | Subir wet o makeup para simular resonance |
| Mezclador | Suma/resta con relaciones de resistencias reales | Wet/dry arbitrario o un boost accidental |
| Etapa de salida | EQ residual, impedancia, coupling caps y headroom | Usar un cabinet/IR para esconder errores del circuito |

Los datasheets sirven para completar lo que el esquema no contiene: rango de
clock, número de etapas BBD, curvas de VCR/LDR, bandwidth, slew, ruido, THD,
insertion loss y límites eléctricos. Si falta un componente determinante, se
debe documentar la aproximación o pedir el datasheet antes de inventarlo.

## 2. Flujo obligatorio para un pedal nuevo

### Fase A: inventario del circuito

Crear una tabla antes de programar:

| Bloque | Componentes y valores | Entrada/salida | Control | Aproximación DSP | Evidencia |
|---|---|---|---|---|---|
| Input | R/C/CI/Q del esquema | jack -> preamp | sens/level | HP + gain/headroom | página y referencias |
| Modulación | BBD o etapas all-pass | preamp -> wet | clock/CV | delay o cascada all-pass | esquema + datasheet |
| Mezcla | resistencias/transistores | dry + wet | mix/mode | matriz firmada | esquema |

No comenzar el voicing hasta poder seguir dry y wet desde el jack de entrada
hasta cada salida.

### Fase B: implementación por bloques

Implementar primero una versión lineal y estable:

1. Input y output a nivel correcto.
2. Bloque modulado sin feedback.
3. Mezcla dry/effect con el signo correcto.
4. LFO y taper de controles.
5. Feedback/resonance.
6. No linealidades que realmente existen.
7. Ruido, compander, gate y tolerancias.

Agregar saturación al comienzo dificulta detectar errores de fase, ganancia y
ruteo. El `tanh` no debe usarse como compensación de volumen.

### Fase C: calibración

Usar el mismo DI para referencia y modelo. En este proyecto el DI canónico es:

`test logic/ce1_ref/brit_di.wav`

Es el mismo archivo que `ui_public_inputs_Brit - Guitar.wav`, con peak cercano a
-12.1 dBFS y RMS cercano a -28 dBFS.

Capturar al menos estos puntos:

| Control | Puntos mínimos |
|---|---|
| Rate/Speed | mínimo, mitad, máximo |
| Depth/Intensity | mínimo, mitad, máximo |
| Feedback/Resonance | cero, mitad, máximo |
| Mix/Level | cero, unidad esperada, máximo |
| Mode/Switch | cada posición real |
| Stereo | salida mono y todas las salidas L/R reales |

No validar únicamente un preset por defecto.

### Fase D: prueba de oído

Las métricas descartan errores y acotan el circuito, pero el usuario sigue
siendo el árbitro final. Escuchar especialmente:

- notas sostenidas;
- acordes abiertos;
- transientes fuertes;
- silencios entre frases;
- controles alrededor de las 9, 12 y 15 horas;
- extremos de Rate, Depth, Feedback y Mix;
- mono, estéreo y suma a mono.

## 3. Chorus: del esquema al sonido

Un chorus analógico no es solo `dry + delay modulado`. Su carácter final sale de
cinco partes que interactúan:

1. Color y headroom del preamp.
2. Filtros antes y después del BBD.
3. Relación entre clock, número de etapas y delay.
4. Forma/rango del LFO y taper de los pots.
5. Matriz dry/effect de salida.

### 3.1 BBD y clock

Para un BBD de `N` etapas con clock bifásico, verificar en el datasheet la
convención exacta. En el MN3002 del CE-1 se usó:

```text
delay_seconds = N / (2 * clock_hz)
N = 512
```

Con el clock real aproximado de 60 a 200 kHz, el rango útil queda cerca de 1.3 a
4.3 ms. No se debe elegir un delay de chorus genérico de 10 a 30 ms si el BBD y
su clock no pueden producirlo.

El clock también determina:

- bandwidth disponible;
- alias/clock feedthrough;
- cuánto filtrado necesita la entrada y salida;
- pitch spread producido por la variación del delay;
- pérdida y ruido del camino wet.

El filtro pre-BBD solo debe afectar la ruta que entra al BBD. Si el esquema toma
el dry antes de ese filtro, filtrar dry y wet juntos oscurece el pedal completo.

### 3.2 Filtros BBD

Una cadena útil suele contener:

```text
input -> preamp -> dry tap
                -> anti-alias LP -> BBD -> reconstruction LP -> wet
```

Los cutoffs se derivan de los R/C y se validan contra el clock. La coloración de
salida restante puede modelarse con polos/ceros o biquads que representen las
redes de transistores y coupling caps. No usar un low-pass oscuro global para
simular "calidez analógica".

### 3.3 LFO y controles

Separar estas funciones:

- `Rate`: frecuencia del LFO.
- `Depth`: amplitud del sweep de delay o clock.
- `Intensity`: comprobar el esquema; puede modificar Rate, Depth, feedback o
  mezcla, y no necesariamente significa wet/dry.

La forma del LFO debe salir del oscilador real. Puede ser seno, triángulo,
triángulo redondeado o una onda asimétrica. Un pot logarítmico se calibra con
anclas mínimo/mitad/máximo; no basta con elegir un exponente por intuición.

Aplicar smoothing a los parámetros continuos. En el CE-1 se usaron 12 ms para
evitar zipper y saltos de clock al automatizar Level, Intensity, Depth y Rate.

### 3.4 Estéreo real

No crear estéreo usando dos chorus iguales con pequeños offsets de LFO salvo que
esa sea la topología real. Muchos pedales estéreo usan una matriz de suma/resta:

```text
L = gain * (dry + side * wet)
R = gain * (dry - side * wet)
```

Eso produce anchura sin dos osciladores independientes. La correlación L/R es
una medición central: permite distinguir una matriz Mid/Side real de un falso
dual-mono o un pseudoestéreo exagerado.

Si el pedal es mono, ambos canales del VST deben compartir el mismo LFO. No se
debe inventar un desfase L/R.

### 3.5 Preamp, BBD y noise killer

- El preamp debe recibir el pot donde aparece físicamente. Un Level de entrada
  no es un trim digital de salida.
- La pequeña THD del BBD se agrega en el camino wet, no al dry completo.
- Un noise killer debe seguir la envolvente de entrada y actuar sobre el piso
  del BBD. Si sigue el wet o tiene umbral alto, bombea notas musicales.
- En bypass/Normal, comprobar si el preamp sigue conectado. El CE-1 conserva su
  color de preamp aunque el efecto esté apagado.

## 4. Caso aprendido: CE-1

Fuentes:

- `pedals/Boss CE-1/BOSS-CE1_ServiceNotes.pdf`
- datasheet MN3002 y componentes locales;
- doce renders en `test logic/ce1_uad/`;
- DI en `test logic/ce1_ref/brit_di.wav`.

Implementación actual:

- `vst/src/pedals/chorus_ensemble/ChorusEnsembleCore.h`
- `vst/src/pedals/chorus_ensemble/ChorusEnsemblePlugin.cpp`

Hallazgos importantes:

1. Las service notes permiten reemplazar TA7504S por TA7136P. El modelo de
   TA7136 usado en el proyecto es una aproximación válida para esa etapa.
2. El dry se toma antes del filtro pre-BBD. Este cambio recuperó claridad.
3. MN3002 tiene 512 etapas, no 1024.
4. Chorus y Vibrato no comparten necesariamente el mismo taper ni la misma
   forma de LFO.
5. En Chorus, Intensity controla principalmente la velocidad en la referencia;
   reducir el sweep junto con Intensity hacía que posiciones bajas casi no
   tuvieran chorus.
6. La salida estéreo se comporta como una matriz Mid/Side. El modelo anterior
   tenía correlación L/R cercana a 0.70; la referencia estaba cerca de 0.18.
7. Las redes Q11-Q14, coupling caps y salida aportan EQ. No deben reemplazarse
   por un único filtro oscuro.
8. Depth debe cambiar el pitch spread sin cambiar mucho el volumen. Se calibró
   una compensación de salida dependiente de Depth/Rate para Vibrato.

Anclas específicas del CE-1 actual, no reutilizables como valores universales:

```text
Chorus Rate:   0.44 + 3.31*x - 0.90*x^2 Hz
Vibrato Rate:  3.4 + 9.0*x^2 Hz
Clock:         60..200 kHz
Delay BBD:     aproximadamente 1.3..4.3 ms
Smoothing:     12 ms
```

La matriz de Chorus calibrada usa `side=0.872` y `gain=0.4554`. Vibrato usa una
tabla 3x3 de Depth/Rate porque una sola ganancia no mantuvo simultáneamente el
nivel y la correlación en toda la grilla. Estas constantes pertenecen al CE-1 y
a esas referencias, no a cualquier chorus.

Resultado de la calibración de doce puntos:

| Métrica | Resultado |
|---|---:|
| Error RMS absoluto medio | 0.004 dB |
| Error RMS máximo | 0.016 dB |
| Error de correlación medio | 0.0010 |
| Error de correlación máximo | 0.0056 |
| Error de peak medio | 0.768 dB |
| Error de peak máximo | 1.275 dB |

La lección reutilizable no son esos números. Es calibrar conjuntamente nivel,
espectro y correlación en una grilla completa de controles.

## 5. Phaser: del esquema al sonido

Un phaser analógico combina dry con una cascada all-pass. Cada all-pass tiene
magnitud aproximadamente unitaria, pero rota la fase alrededor de su frecuencia
de quiebre. Al sumarlo con dry aparecen notches.

### 5.1 All-pass correcto

La implementación de primer orden usada actualmente es:

```cpp
t = tan(pi * fc / sampleRate);
a = (t - 1) / (t + 1);
y = a * x + z;
z = x - a * y;
```

Su forma conceptual es:

```text
H(z) = (a + z^-1) / (1 + a*z^-1)
```

El signo importa. La versión anterior con el signo opuesto colocaba el quiebre
cerca de Nyquist y el phaser casi no se oía. No compensar ese bug aumentando
feedback, wet o saturación: se debe corregir el all-pass.

### 5.2 Cantidad y matching de etapas

Contar las celdas reales del esquema. Cuatro, seis y ocho etapas no son voces
intercambiables.

Cuando varias etapas muestran el mismo R/C y comparten CV:

- deben usar la misma frecuencia base;
- se permiten tolerancias pequeñas y plausibles;
- no se les asigna una lista arbitraria de frecuencias separadas por octavas;
- no se desfasa el LFO por etapa si el cable de control es común.

Separar artificialmente las etapas produce un filtro ancho/genérico, reduce los
notches reales y obliga a exagerar resonance para que el efecto aparezca.

La frecuencia eléctrica aproximada parte de:

```text
fc = 1 / (2*pi*R_variable*C)
```

Después se modela la curva no lineal `CV -> R_variable`. En JFET y LDR esa curva
no es lineal; suele ser más útil representar `fc` en dominio logarítmico y
agregar lag/matching que mover `fc` linealmente.

### 5.3 LFO y lectura correcta de velocidad

El movimiento de un notch puede cruzar una banda dos veces por ciclo. Por eso el
espectro o la envolvente suelen mostrar un segundo armónico fuerte. Contar esos
cruces como ciclos entrega exactamente el doble de la velocidad real.

Método correcto:

1. Extraer la modulación en varias bandas o bins espectrales.
2. Buscar fundamental y armónicos conjuntamente.
3. Comprobar visualmente un ciclo completo de subida y bajada.
4. Ajustar el taper con mínimo, mitad y máximo.

No usar `Rate = min + knob*(max-min)` salvo que el pot y el oscilador realmente
sean lineales.

### 5.4 Depth, Mix y Resonance no son lo mismo

- `Depth` cambia la amplitud de CV alrededor de un centro:

```text
cv = center + (lfo - center) * depth
```

- `Mix` cambia la relación dry/all-pass solo si existe ese pot en el mezclador.
- `Resonance/Feedback` recircula la salida all-pass al punto indicado por el
  esquema.

Un mezclador clásico cercano a unidad es:

```text
y = makeup * 0.5 * (dry + wet_allpass)
```

El signo exacto depende de las inversiones acumuladas. Se debe verificar con el
esquema y con la posición de los notches.

Una forma de feedback acotada es:

```text
u = dry - feedbackAmount * feedbackState
wet = allpassCascade(u)
feedbackState = wet
```

No usar `tanh(y*5)` o un makeup de 8x para hacer audible un phaser. El all-pass
y su mezcla ya producen el efecto. Una soft-knee transparente puede proteger
picos extremos, pero no debe ser parte central del sonido salvo que exista una
etapa real que sature.

### 5.5 Mono y estéreo

Phase 90, PH99, PH-1R y AP-7 son rutas mono en los esquemas usados. En un VST
estéreo se procesan L/R, pero ambos cores deben compartir la misma fase de LFO.
Un offset de 0.015 o 0.025 ciclos crea un estéreo que el pedal no tiene.

Para un phaser realmente estéreo se debe seguir su matriz o sus taps reales. No
se reutiliza automáticamente esta regla mono.

## 6. Casos aprendidos de phaser

### 6.1 Phase 90

Fuentes:

- `pedals/phaser 363.png`;
- `test logic/phase90/speed_min.wav`;
- `test logic/phase90/speed_half.wav`;
- `test logic/phase90/speed_max.wav`.

Implementación: `vst/src/pedals/phaser_363/Phaser363Plugin.cpp`.

El esquema muestra cuatro celdas con redes 10 kOhm/47 nF y cuatro 2N5952 bajo
el mismo control. La versión previa separaba las etapas con multiplicadores
`0.6, 1.0, 1.7, 2.8` y usaba feedback 0.70. Eso no representaba el circuito.

Correcciones:

- cuatro corners iguales con tolerancias cercanas a +/-1%;
- curva JFET no lineal;
- sweep acotado y logarítmico;
- feedback fijo bajo;
- salida dry + all-pass sin drive artificial;
- LFO mono;
- taper calibrado con la referencia.

Anclas específicas actuales:

```text
Rate mínimo: 0.078 Hz
Rate mitad:  0.73 Hz
Rate máximo: 4.77 Hz
fc:          145 * 18^lfo Hz
feedback:    0.10
```

El análisis mostró un segundo armónico cercano a 0.16, 1.45 y 9.5 Hz. Esas no
eran las velocidades del LFO, sino dos movimientos de notch por ciclo.

Resultado RMS frente a las tres referencias:

| Speed | Error RMS modelo - referencia |
|---|---:|
| mínimo | -0.158 dB |
| mitad | +0.018 dB |
| máximo | -0.037 dB |

El match de nivel no significa copiar exactamente el timbre de otro phaser. La
referencia sirve para fijar escala musical, taper y ausencia de exageración.

### 6.2 Ibanez PH99

Fuente: `pedals/ibanez_ph99.pdf`.

Implementación: `vst/src/pedals/bass_phase/BassPhasePlugin.cpp`.

El esquema muestra:

- seis etapas 4558/C4570 con R de 10 kOhm y C de 3.3 nF;
- dos optoacopladores P873-G35;
- NE571 alrededor del camino de fase;
- controles Speed, Depth, Feedback y Level.

Errores encontrados y corregidos:

1. Un opto seguía el LFO y el otro `1-LFO`. El esquema los alimenta desde el
   mismo control; solo corresponde modelar mismatch y lag distintos.
2. Las seis etapas tenían frecuencias base 118, 190, 315, 525, 875 y 1450 Hz.
   El esquema repite la misma celda; ahora comparten corner con tolerancias.
3. El NE571 se usaba como drive con varios `tanh`, sin expansión inversa. El
   resultado era pumping y entre +6.5 y +8.1 dB de salida.
4. Level tenía un piso artificial. La perilla real ahora llega a silencio en 0.

El compander se representa como ganancia de compresión antes del all-pass y su
inversa después. Su función principal es mejorar ruido/rango dinámico, no crear
la distorsión del pedal.

### 6.3 Boss PH-1R

Fuente: `pedals/boss_ph-1r_phaser_pedal.png` y datasheets locales TL022,
uPC4558, 2SK30A-GR/Y y transistores de salida.

Implementación: `vst/src/pedals/shaver_phaser/ShaverPhaserPlugin.cpp`.

Reglas aplicadas:

- cuatro etapas repetidas bajo los 2SK30A seleccionados;
- TL022 como origen del LFO;
- onda seno/triángulo redondeada;
- lag para representar la red de control/JFET;
- Resonance modifica feedback, no wet ni makeup;
- ruta limpia sin clipping intencional;
- LFO mono y taper de Rate exponencial.

Las designaciones GR/Y importan por matching y rango de VCR. Se modelan como
tolerancias y forma de curva, no como cuatro frecuencias arbitrarias.

### 6.4 Roland AP-7

Fuentes:

- `pedals/Roland Jet Phaser AP-7 Service Manual.pdf`;
- `pedals/plane phase.gif`.

Implementación: `vst/src/pedals/plane_phase/PlanePhasePlugin.cpp`.

El AP-7 conserva diferencias reales respecto de los otros phasers:

- ocho etapas;
- preamplificación con carácter;
- feedback/jet más fuerte;
- LFO asimétrico;
- mayor tolerancia acumulada entre etapas.

No debe convertirse en un Phase 90 de ocho etapas. Solo se reutilizan las
reglas de estabilidad, escala y lectura del esquema.

El DSP anterior sumaba niveles dry/wet de hasta aproximadamente 1.58 antes de
otro `tanh`; Mix máximo subía cerca de +5.65 dB. La mezcla corregida hace un
crossfade acotado entre dry y all-pass y mantiene el carácter jet mediante el
feedback, no mediante boost.

## 7. Cómo medir sin depender solo del oído

### 7.1 Alineación

La referencia y el render deben usar el mismo DI y comenzar en el mismo sample.
Antes de comparar:

- confirmar sample rate y canales;
- comprobar latencia con correlación;
- recortar a la duración común;
- comparar cada canal por separado;
- comprobar correlación L/R.

### 7.2 Métricas mínimas

| Métrica | Qué detecta |
|---|---|
| RMS global | Boost/cut accidental |
| Peak y `clip_frac` | Saturación o inestabilidad |
| RMS por ventanas de 50 ms | Pumping, profundidad y dropouts |
| Percentiles de gain 5/50/95 | Efecto demasiado plano o exagerado |
| STFT `20*log10(|Y|/|X|)` | Movimiento de notches y color espectral |
| Profundidad/frecuencia de notch | Matching y rango del all-pass |
| Espectro por bandas | Filtros y color de salida |
| Fundamental + armónicos del LFO | Rate real y forma de onda |
| Correlación L/R | Matriz estéreo correcta |
| Near-zero/dropout windows | Gates o cancelación destructiva incorrecta |

Para chorus, medir también delay mínimo/máximo estimado y pitch spread. Para
phaser, medir cantidad, rango y profundidad de notches.

### 7.3 Harness local

El harness actual está en `tools/render_amp_wav.py`, pero puede apuntarse a
`vst/src/pedals`:

```sh
cd rig_builder
python3 -c '
import sys
from pathlib import Path
import tools.render_amp_wav as r
r.AMPS = Path("vst/src/pedals")
raise SystemExit(r.main(sys.argv[1:]))
' phaser_363 \
  '../test logic/ce1_ref/brit_di.wav' \
  /private/tmp/phase90.wav \
  'Rate=0.5'
```

El render imprime RMS, peak, clipping, near-zero y ventanas de dropout. Repetir
para todos los puntos de la grilla.

### 7.4 Gates de aceptación

Antes de desplegar:

1. `clip_frac = 0` en todos los puntos.
2. `dropout_windows = 0`, salvo Level=0 intencional.
3. Sin NaN, infinito ni crecimiento sostenido con Feedback máximo.
4. Ningún control de modulación debe producir boosts de varios dB si el esquema
   no incluye un Level/Boost real.
5. Los controles a las 9 no deben sonar como el máximo.
6. Rate mínimo, mitad y máximo deben medir el taper esperado.
7. Depth mínimo debe reducir el sweep, no apagar accidentalmente el dry.
8. Mix=0 debe conservar la ruta que indica el esquema.
9. Mono debe seguir mono; estéreo debe sobrevivir una suma a mono razonable.
10. Automatización de pots continuos sin zipper, clicks ni saltos de estado.

## 8. Errores que ya no deben repetirse

- Crear etapas all-pass con frecuencias distintas cuando el esquema repite la
  misma red R/C.
- Confundir dos cruces de notch con dos ciclos del LFO.
- Usar offsets L/R para dar estéreo a un pedal mono.
- Poner el filtro pre-BBD también sobre el dry.
- Reducir Depth o Intensity cambiando simultáneamente Rate, Mix y Level sin
  evidencia del esquema.
- Usar feedback extremo para compensar un all-pass con signo incorrecto.
- Hacer makeup con `tanh` fuerte: cambia nivel, THD y dinámica a la vez.
- Convertir un compander en drive y olvidar la expansión.
- Modelar dos optos del mismo control en oposición de fase.
- Oscurecer todo el pedal con un low-pass global para simular analog warmth.
- Ajustar solo a un preset y declarar terminado el pedal.
- Copiar los Hz, feedback, matrices o ganancias del CE-1/Phase 90 a otro modelo.

## 9. Checklist final para futuros chorus

- [ ] Identifiqué BBD, cantidad de etapas y rango de clock.
- [ ] Derivé delay mínimo/máximo desde clock y datasheet.
- [ ] Separé dry antes/después de los filtros según el esquema.
- [ ] Implementé filtros anti-alias y reconstruction por sus redes R/C.
- [ ] Identifiqué forma de LFO y taper de Rate/Depth/Intensity.
- [ ] Implementé compander/noise killer como pareja funcional, sin pumping.
- [ ] Seguí la matriz mono/estéreo real.
- [ ] Modelé preamp y salida sin usar un filtro global de conveniencia.
- [ ] Suavicé parámetros continuos.
- [ ] Medí delay, nivel, espectro, correlación y extremos.

## 10. Checklist final para futuros phasers

- [ ] Conté las etapas all-pass reales.
- [ ] Verifiqué el signo y frecuencia de quiebre de cada all-pass.
- [ ] Derivé corner base desde R/C y rango del JFET/LDR.
- [ ] Las etapas iguales comparten CV y solo difieren por tolerancia plausible.
- [ ] Depth controla CV, Mix controla mezcla y Resonance controla feedback.
- [ ] Medí la fundamental real del LFO, no solo sus cruces/armónicos.
- [ ] No inventé estéreo para una salida mono.
- [ ] No usé saturación o makeup para hacer audible un error de fase.
- [ ] Feedback máximo permanece estable.
- [ ] Medí nivel, notches, espectro, pumping, clipping y dropouts.

## 11. Criterio para reutilizar código

Conviene compartir primitivas matemáticas, no voicings completos:

- all-pass estable;
- delay BBD fraccional;
- filtros R/C y biquads;
- smoothing;
- envelope follower;
- modelos de JFET/LDR/opto;
- compander pareado;
- medición y harness.

Cada pedal debe conservar en su core:

- cantidad y conexión de etapas;
- valores R/C;
- curva y rango de controles;
- rutas dry/effect;
- feedback y signo;
- clock/LFO;
- matriz de salida;
- headroom y color propios.

Así se obtiene consistencia técnica sin hacer que todos los chorus o phasers
suenen como el CE-1 o el Phase 90.
