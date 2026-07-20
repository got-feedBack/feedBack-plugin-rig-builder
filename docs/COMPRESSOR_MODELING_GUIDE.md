# Guia de modelado circuit-real para compresores

Estado: 2026-07-19

Esta guia documenta el metodo usado para reconstruir y calibrar el MXR Dyna
Comp. Su objetivo es conservar lo aprendido para futuros compresores OTA y
evitar volver a implementar un compresor generico de threshold/ratio que solo
se parezca al pedal por el nombre de las perillas.

La regla principal es:

> El esquema define la ruta de audio, el detector y la realimentacion. Los
> audios de referencia calibran la envolvente, el sustain, el nivel y el color
> que no pueden deducirse con precision del papel por si solo.

No se deben copiar las constantes del Dyna Comp a compresores opticos, FET,
VCA, digitales ni multibanda. Se pueden reutilizar primitivas DSP, pero cada
topologia debe reconstruirse por separado.

Esta guía complementa el workflow obligatorio de
[`REFERENCE_MATCHING_WORKFLOW.md`](REFERENCE_MATCHING_WORKFLOW.md). El workflow
común define DI, alineación, ventanas, estabilidad y despliegue; aquí se agregan
las mediciones propias de detectores y elementos de control de ganancia.

## 1. Fuentes del caso Dyna Comp

Esquema principal:

- `../pedals/dynamics compression_2.jpg`: circuito stock CA3080, usado como
  objetivo.
- `../pedals/dynamics compression.jpg`: variante modificada con controles de
  Bright/Attack. Sirve como referencia secundaria, no como panel del modelo
  stock.

Datasheets:

- `../componentes/CI/info-uica3080e.pdf`
- `../componentes/transistores/2N5089.pdf`
- `../componentes/transistores/2N5088.pdf`
- `../componentes/diode/1N914.PDF`

Audios de referencia:

- `../test logic/dyna comp/out_half_sens_min.wav`
- `../test logic/dyna comp/out_half_sens_half.wav`
- `../test logic/dyna comp/out_half_sens_max.wav`

Los tres renders usan Output a la mitad y Sensitivity en minimo, mitad y
maximo. El DI es `../test logic/ce1_ref/brit_di.wav`; se comprobo que los
renders comienzan en la misma muestra, sin offset de alineacion.

Implementacion:

- `vst/src/pedals/dynamics_compression/DynamicsCompressionPlugin.cpp`
- `vst/src/pedals/dynamics_compression/DynamicsCompressionParams.h`
- `vst/pedals/dyna comp.vst3`

## 2. Topologia que debe conservarse

El Dyna Comp stock no contiene un op-amp de audio ni un bloque digital de
makeup automatico. Sus bloques funcionales son:

```text
Input
  -> C1 / red de bias
  -> Q1, entrada y adaptacion de impedancia
  -> Tr1 y red de balance
  -> CA3080, celda OTA controlada por Iabc
  -> Q5, recuperacion de la salida de alta impedancia
  -> C14
  -> VR2 Level 50k log
  -> Output

Salida de la celda
  -> D2/D3 + Q2/Q3, rectificacion y detector
  -> C16, memoria de envolvente
  -> Q4 + VR1 Sustain 500k linear
  -> corriente Iabc del CA3080
```

La segunda ruta es una realimentacion de control. La salida detectada cambia la
transconductancia de la misma celda que genero esa salida. Por eso el pedal no
se comporta igual que un compresor feed-forward con threshold, ratio y makeup
independientes.

### 2.1 CA3080

El CA3080 es un operational transconductance amplifier. De forma simplificada:

```text
Iout ~= Iabc * tanh(Vdiff / (2 * Vt))
gm   proporcional a Iabc
```

La red Tr1/R7/R8/R11 mantiene pequeno `Vdiff`, por lo que la celda trabaja casi
linealmente en uso normal. Su no linealidad diferencial debe aparecer de manera
gradual; no corresponde reemplazarla por clipping fuerte de diodos o por un
`tanh` usado como distorsion principal.

En DSP se separan dos funciones:

1. Ley diferencial del OTA: redondeo suave de la entrada alrededor del punto de
   bias.
2. Ganancia variable: estado controlado por la corriente equivalente `Iabc`.

El datasheet confirma que la transconductancia sigue la corriente de control y
que la salida es de alta impedancia. Q5 no es decorativo: recupera esa salida y
forma parte del nivel, color y headroom posteriores.

### 2.2 Detector y capacitor C16

D2 y D3 son diodos 1N914 del sidechain. No forman un clipper antiparalelo sobre
el audio. Su funcion es rectificar la senal que controla Q2/Q3; C16 almacena la
envolvente y la red resistiva determina su descarga.

La representacion DSP necesita al menos:

```text
rectified = abs(detector_source)
envelope += coeff_attack_or_release * (rectified - envelope)
Iabc_target = sidechain(envelope, sensitivity)
Iabc_state += coeff_control * (Iabc_target - Iabc_state)
```

Hay dos inercias distintas:

- carga/descarga del detector, dominada por diodos, C16 y resistencias;
- respuesta de Q4 y de la corriente de control del OTA.

Usar una sola envolvente instantanea produce transientes duros. Usar releases
demasiado largos mantiene el OTA cerrado durante frases completas y causa el
fallo que tenia el modelo anterior: al subir Sensitivity el pedal perdia casi
todo el volumen.

### 2.3 Controles reales

| Control | Esquema | Funcion DSP |
|---|---|---|
| Sensitivity/Sustain | VR1, 500k linear | Cambia la relacion entre envolvente, Q4 e Iabc |
| Output/Level | VR2, 50k log | Atenuacion/ganancia final con taper logaritmico |

Sensitivity no es un threshold lineal ni un makeup gain. Al subirlo aumenta el
sustain de contenido debil mientras los ataques fuertes permanecen controlados.
El resultado audible combina ganancia de notas suaves y reduccion dinamica de
notas fuertes.

Output debe llegar a silencio en cero. Su posicion media es el ancla usada por
las referencias, no necesariamente unity electrico respecto del bypass. Sobre
la mitad puede entregar nivel suficiente para empujar el siguiente pedal o el
amplificador; no se debe normalizar cada muestra a 0 dBFS.

## 3. Traduccion implementada

La implementacion actual es un modelo por bloques guiado por componentes, no un
solver SPICE muestra a muestra. Conserva la topologia y calibra la ley de
control contra audio real/plugin de referencia.

### 3.1 Ruta de audio

1. `RcHighPass` representa C1 de 0.01 uF con una carga efectiva cercana a
   1.01 Mohm.
2. `bjtStage` representa el color leve y asimetrico de Q1.
3. La entrada diferencial del CA3080 usa su ley `tanh`, escalada para permanecer
   mayormente lineal con niveles de guitarra.
4. `otaBandwidth` limita el ancho de banda de la celda.
5. `gainCell` representa la transconductancia controlada por Iabc.
6. Un segundo `bjtStage` representa Q5.
7. `outputTone` aproxima el ancho de banda residual de la salida.
8. El taper de VR2 aplica el nivel final y una aproximacion suave a los rails de
   9 V evita discontinuidades numericas.

No hay M5218 en esta ruta. El core anterior lo usaba como recovery amp aunque
no aparece en el esquema stock. Tambien se elimino el par de 1N914 puesto como
clipper de audio, porque D2/D3 pertenecen al detector.

### 3.2 Sidechain actual

La envolvente se toma despues de Q1. Los coeficientes a 48 kHz equivalen
aproximadamente a:

| Estado | Rango actual |
|---|---:|
| Detector attack | 1.15 ms |
| Detector release | 35 a 45 ms |
| Control-current attack | 2.1 a 5.1 ms |
| Control-current release | 20 a 35 ms |

Los tiempos cambian con Sensitivity porque la impedancia y corriente efectiva
del sidechain cambian con VR1. Estas cifras son anclas calibradas; no deben
copiarse a otro compresor sin reconstruir su detector.

La conversion de envolvente a ganancia usa una curva suave en dB. Esa curva
resume el lazo Q2/Q3/Q4/CA3080 y fue ajustada en minimo, mitad y maximo. No se
expone como threshold/ratio porque esos controles no existen en el pedal.

### 3.3 Taper de Output

VR2 se implementa como un taper logaritmico con la posicion 0.5 como ancla:

```text
level = pow(2 * output, 2.75)
```

Por lo tanto:

- `Output = 0` silencia;
- `Output = 0.5` conserva el nivel usado para calibrar las referencias;
- `Output > 0.5` agrega el margen real de salida del pedal.

Si aparecen referencias en otras posiciones, se debe refinar el taper con esos
puntos sin mover la calibracion dinamica de Sensitivity.

## 4. Que mostraron los audios

Mediciones de los renders de referencia, Output a la mitad:

| Sensitivity | RMS | Peak | Crest |
|---|---:|---:|---:|
| Min | -25.55 dBFS | -10.33 dBFS | 15.23 dB |
| Half | -22.54 dBFS | -4.31 dBFS | 18.23 dB |
| Max | -22.08 dBFS | -2.59 dBFS | 19.50 dB |

El DI tiene aproximadamente -28 dBFS RMS y -12.1 dBFS peak. Los datos muestran
que Sensitivity no solo reduce peaks:

- en minimo, las ventanas fuertes quedan cerca de unity y las debiles ganan
  alrededor de 4 dB;
- en mitad, las colas debiles pueden ganar 14 a 21 dB mientras las ventanas
  fuertes quedan cerca de unity;
- en maximo, el sustain aumenta aun mas, pero el RMS global cambia poco frente
  a la mitad porque los ataques siguen controlados.

Este patron permite detectar un modelo incorrecto. Si Sensitivity maxima baja
todo el pedal, si todas las ventanas reciben el mismo boost o si el peak cae al
mismo ritmo que el RMS, el lazo OTA/sidechain no esta representado bien.

### 4.1 Resultado del modelo actual

En los mismos 32 segundos y con Output a la mitad:

| Sensitivity | RMS modelo | Peak modelo | Error RMS |
|---|---:|---:|---:|
| Min | -25.73 dBFS | -10.87 dBFS | -0.17 dB |
| Half | -22.81 dBFS | -4.35 dBFS | -0.27 dB |
| Max | -21.96 dBFS | -2.74 dBFS | +0.12 dB |

Ademas de RMS y peak se compararon ventanas de 10 ms por nivel de entrada. Esa
prueba fue esencial: dos compresores pueden tener el mismo RMS global y una
envolvente completamente distinta.

## 5. Caso aprendido: EBS MultiComp 2

Fuentes:

- `../pedals/ESB MultiComp 2.pdf`: esquema Black Label MultiComp 2;
- `../pedals/multi bass comp.webp`: copia de menor resolucion del mismo plano;
- `https://www.bajosybajistas.com/ebs-multicomp/`: descripcion de controles,
  ratio y modos del pedal real;
- datasheets locales de PMBFJ113, TS921/TS925, TL064, BAS28 y BAT54;
- DI de bajo `../ui_public_inputs_Frogger - Bass.wav`;
- DI de bajo `../ui_public_inputs_Garden - Bass.wav`.

El MultiComp no es un Dyna Comp duplicado. En modo multibanda contiene dos
rutas de audio y dos detectores:

```text
Input buffer
  -> split HP/LP fijo por U4 y C24-C31
  -> Q2 PMBFJ113 + detector HP
  -> Q3 PMBFJ113 + detector LP
  -> mezcla y Level
```

P2A/P2B es el pot dual 50kB de compresion. Los JFET trabajan como resistencias
variables en shunt. R52 y R63 son trims internos de 500k separados para los
umbrales low/high; no existe una perilla frontal Sens. El VST conserva ambos
ajustes como parametros internos para calibracion, pero la UI muestra solo los
controles fisicos externos.

### 5.1 Bug encontrado

El core anterior calculaba hasta 23 dB de reduccion, pero despues convertia el
estado del detector con:

```text
divider = 47k / (47k + Rds)
Rds = 100 .. 3150 ohm
```

Ese divisor nunca bajaba mucho de 0.94, por lo que toda la reduccion calculada
terminaba convertida en menos de 0.6 dB. En el DI de bajo, barrer Comp completo
cambiaba el RMS solo 0.08 dB y Sens apenas 0.30 dB.

La correccion modela el PMBFJ113 como shunt despues de la resistencia serie:

```text
divider = Rds / (Rseries + Rds)
```

`Rds` se deriva del estado de ganancia pedido por el detector y se limita por
el rango fisico del JFET. Se conserva una no linealidad de canal pequena; el
JFET no se usa como distorsion principal.

### 5.2 Controles y modos

- Comp/Limit controla la pendiente de reduccion desde 1:1 hasta 5:1. La
  interpolacion se hace sobre la pendiente, no directamente sobre el numero de
  ratio, para que la mitad superior de la perilla siga siendo util.
- Low Trim y High Trim mueven independientemente los umbrales de sus detectores
  sin cambiar crossover, ataque, release ni impedancia de los rectificadores.
- Gain es el makeup/output final independiente; Comp no contiene un makeup
  automatico oculto.
- Normal usa detector full-band.
- MultiBand usa detectores HP/LP independientes.
- TubeSim usa detector full-band y agrega solo la coloracion del modo.

Con Gain=0.56 y MultiBand, el DI Frogger mide actualmente:

| Comp | RMS | Peak | Crest |
|---:|---:|---:|---:|
| 0.00 | -26.95 dBFS | -8.21 dBFS | 18.75 dB |
| 0.25 | -27.47 dBFS | -8.61 dBFS | 18.86 dB |
| 0.50 | -27.88 dBFS | -9.01 dBFS | 18.87 dB |
| 0.75 | -28.21 dBFS | -9.41 dBFS | 18.80 dB |
| 1.00 | -28.46 dBFS | -9.80 dBFS | 18.65 dB |

El peak baja de forma continua 1.59 dB. El RMS tambien baja porque el hardware
deja la compensacion al control Gain; el DSP ya no inventa makeup dependiente de
Comp. En Rocksmith, `Compress` mapea a Comp/Limit y `Filter` se reutiliza para
Gain. Los trims quedan en su calibracion de fabrica y Mode queda en MultiBand.

TubeSim conserva la fundamental y mezcla solo un residuo armonico asimetrico
filtrado. No se debe pasar toda la senal por un low-pass, porque eso oscurece el
pedal en vez de agregar la calidez descrita para el modo.

Antes de esta correccion ambos barridos eran casi planos. Los extremos ahora
son audibles, continuos y no producen dropouts. Todavia no existen renders de
hardware/plugin EBS para calibrar fino el release, la mezcla de bandas y el
color TubeSim; esos parametros no deben considerarse cerrados con la misma
confianza que el Dyna Comp.

## 6. Caso aprendido: Boss LM-2

Fuente principal:

- `../pedals/BOSS-LM2_ServiceNotes.pdf`, pagina de circuit diagram.

`../pedals/limiter.png` no corresponde al LM-2: muestra un circuito de delay
con MN3005/MN3101. No debe usarse para calibrar este limiter.

La topologia relevante del LM-2 es:

```text
Input buffer Q2
  -> uPC1252H2 VCA
  -> M5218L y buffer de salida

Sidechain del VCA
  -> BA718 + diodos + capacitor de detector
  -> VR1 Threshold 50kB
  -> VR2 Release 1M
  -> M5223L / control del VCA
```

Threshold mueve la referencia del detector. No debe subir simultaneamente el
ratio desde compresor suave a limitador extremo: el ratio alto pertenece al
gain computer y al ajuste interno VR5. Level es VR4 100kA y debe ser un taper
de salida real.

### 6.1 Bugs encontrados

1. Level=0.58 aplicaba cerca de -12.7 dB antes de la salida. El render resultaba
   en -41.3 dBFS para un DI de -28 dBFS.
2. Threshold usaba un taper arbitrario y cambiaba umbral, ratio y reduccion
   maxima a la vez. De 0 a 0.5 casi no ocurria nada; en 1 podia recortar mas de
   22 dB.

La correccion:

- normaliza el taper 100kA de Level en el valor default 0.58;
- conserva silencio real en Level=0 y boost en la parte alta;
- usa Threshold lineal 50kB para mover principalmente el umbral;
- mantiene ratio de limitador cercano a 10:1-12:1;
- limita la reduccion sostenida a 12 dB;
- aplica taper al pot de Release sin alterar el umbral.

Con Level=0.58, Tone=0.52 y Release=0.34, Threshold produce:

| Threshold | RMS | Peak |
|---:|---:|---:|
| 0.00 | -28.66 dBFS | -13.23 dBFS |
| 0.50 | -30.07 dBFS | -13.52 dBFS |
| 0.75 | -34.03 dBFS | -16.81 dBFS |
| 1.00 | -38.52 dBFS | -21.88 dBFS |

El primer cuarto puede permanecer practicamente inactivo con un DI debil: eso
es coherente con un control de threshold. No se debe fabricar compresion debajo
del umbral solo para que cada milimetro del pot cambie el RMS.

## 7. Flujo para futuros compresores

### Fase A: inventario

Antes de programar, identificar:

1. Ruta de audio completa.
2. Punto exacto donde se toma el sidechain.
3. Tipo de detector: peak, average, RMS aproximado, rectificador de media onda o
   onda completa.
4. Elemento de ganancia: OTA, VCA, FET, opto, diodo, tubo o DSP.
5. Feed-forward o feedback.
6. Capacitores y resistencias que fijan attack/release.
7. Taper y valor de cada pot.
8. Headroom, bias y etapa de salida.

### Fase B: implementacion

1. Implementar la ruta lineal y comprobar nivel/frecuencia.
2. Implementar el detector sin conectarlo a la ganancia.
3. Renderizar la envolvente para verificar attack/release.
4. Conectar el elemento de control con una curva estable.
5. Agregar no linealidades solo donde aparecen en el esquema.
6. Implementar pots y switches reales.
7. Agregar smoothing para automatizacion sin zipper.

### Fase C: referencias

Capturar como minimo:

| Control | Puntos |
|---|---|
| Compression/Sustain/Sensitivity | minimo, mitad, maximo |
| Output/Level | minimo, mitad, maximo |
| Attack/Release, si existen | extremos y mitad |
| Ratio/Mode, si existen | cada posicion real |

Usar siempre el mismo DI, misma frecuencia de muestreo y misma alineacion.
Guardar tambien renders de tonos escalonados y bursts para separar respuesta
estatica de attack/release.

No usar una referencia de compresión como si fuera solo una curva de loudness:
comparar las mismas ventanas de ataque, sustain y cola. El makeup se calibra
después de igualar gain reduction, attack y release, nunca antes.

### Fase D: mediciones

Medir:

- RMS, peak y crest global;
- ganancia por ventanas de 10 a 50 ms;
- percentiles de ganancia;
- ganancia versus nivel de entrada;
- trayectoria de ganancia alrededor de ataques y colas;
- espectro por bandas;
- THD con senos a varios niveles;
- clipping, NaN, denormals y dropout windows;
- respuesta a automatizacion de cada pot.

## 8. Errores que no deben repetirse

- Poner los diodos del detector como clipper en la ruta de audio.
- Agregar un op-amp que no existe en el esquema.
- Reemplazar un OTA feedback por threshold/ratio/makeup genericos.
- Atar un boost fijo a Sensitivity.
- Comprimir y luego recuperar volumen con `tanh` fuerte.
- Usar una sola constante para detector y elemento de control.
- Hacer release tan largo que el pedal se apague durante frases completas.
- Hacer attack instantaneo y destruir todos los transientes.
- Mapear un pot logaritmico como lineal.
- Normalizar Output para impedir que empuje la siguiente etapa.
- Ajustar solo RMS global sin medir ganancia por nivel y crest.
- Copiar los tiempos del Dyna Comp a un optocompresor o FET.

## 9. Checklist final

- [ ] Segui la ruta de audio y el sidechain por separado.
- [ ] Identifique si el circuito es feed-forward o feedback.
- [ ] Modele el elemento de ganancia correcto.
- [ ] Los diodos estan en la rama indicada por el esquema.
- [ ] Attack y release salen de las redes reales y de referencias.
- [ ] Los controles conservan valor y taper reales.
- [ ] Sensitivity/Compression no es un makeup disfrazado.
- [ ] Output llega a silencio y conserva margen sobre la mitad.
- [ ] Medi RMS, peak, crest y ganancia por nivel.
- [ ] Probe minimo, mitad y maximo de cada control.
- [ ] No hay NaN, glitches ni dropouts no intencionales.
- [ ] El VST3 desplegado corresponde al source recien compilado.

## 10. Criterio para reutilizar codigo

Se pueden compartir:

- envelope followers;
- rectificadores con modelos de diodo;
- smoothing;
- celdas OTA/VCA/FET parametrizadas;
- filtros R/C;
- tapers de pot parametrizados;
- harness y metricas.

Cada pedal debe conservar localmente:

- topologia y punto del sidechain;
- elemento de ganancia y su bias;
- valores R/C;
- attack/release;
- ley de control;
- tapers y switches;
- nivel, headroom y color de salida;
- calibracion contra sus propias referencias.

Compartir primitivas reduce errores. Compartir el voicing completo haria que
todos los compresores sonaran como el Dyna Comp.
