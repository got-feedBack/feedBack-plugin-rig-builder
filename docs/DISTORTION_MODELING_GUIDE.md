# Guia de modelado para pedales de distorsion

Estado: 2026-07-15

Esta guia registra lo aprendido al reconstruir el Mouse, modelo del Pro Co RAT
II. El RAT se usa como referencia de la familia distortion: clipping firme,
ataque definido y control tonal posterior. No es una voz generica para copiar
a DS-1, HM-2, OS-2 u OCD; cada pedal debe conservar la topologia de su esquema.

## 1. Fuentes del RAT

- `../pedals/Proco-Rat-II-Schematic.png`: esquema principal.
- `../componentes/CI/LM308N.pdf`: GBW, slew rate, compensacion y swing.
- `../componentes/diode/1N4148.pdf`: curva directa de D2/D3.
- `../componentes/transistores/2N5458.pdf`: seguidor de salida Q5.
- `../test logic/rat/`: seis renders con Distortion en mitad/maximo y Filter
  en minimo/mitad/maximo.
- `../ui_public_inputs_Brit - Guitar.wav`: DI alineado con los renders.

Implementacion:

- `vst/src/pedals/bass_distortion/RatCore.h`
- `vst/src/pedals/bass_distortion/BassDistortionPlugin.cpp`
- `vst/pedals/Mouse.vst3`

## 2. Topologia que debe conservarse

```text
Input
  -> C3 con R4 || R5
  -> R6/C4
  -> LM308 no inversor
       -> VR1 y C5 en feedback
       -> R7/C6 y R8/C7 a tierra
       -> C10 de compensacion
  -> R9/C11
  -> D2/D3 1N4148 antiparalelo a tierra
  -> VR2 + R10 + C12, control Filter
  -> C13/R11
  -> Q5 2N5458 source follower
  -> C14 + VR3 Volume
  -> Output
```

Las dos ramas de feedback son esenciales. Sus esquinas son aproximadamente
60.5 Hz para 560 ohm/4.7 uF y 1.54 kHz para 47 ohm/2.2 uF. Por eso el RAT no es
un booster plano seguido de dos diodos: el contenido que llega al clipper ya
tiene una ganancia dependiente de frecuencia.

## 3. LM308 y clipping

El LM308 debe conservar tres limites distintos:

1. Ganancia cerrada definida por VR1 y las dos ramas RC.
2. Ancho de banda `GBW / noiseGain` y compensacion C5/C10.
3. Slew rate y swing alrededor del bias de 4.5 V.

El estado del op-amp se limita por sus rieles antes de aplicar su respuesta
dinamica. Filtrar un objetivo numericamente enorme y limitarlo despues hace que
la senal atraviese el polo demasiado rapido y produce una distorsion brillante
y generica.

D2/D3 son un shunt clipper despues de R9=1k. No estan en el feedback del
LM308. El solver usa la ecuacion exponencial de los 1N4148 y recibe voltios
fisicos; la conversion actual es 3 V por unidad de audio. Omitir esa conversion
cambia el umbral efectivo por un factor de tres.

No se agrega `tanh` despues de los diodos. Q5 trabaja como seguidor a este
nivel de senal y no debe convertirse en una segunda etapa de distorsion.

## 4. Controles reales

| Control | Circuito | Implementacion |
|---|---|---|
| Distortion | VR1 A100k | Resistencia de feedback; curva efectiva de 36 dB calibrada |
| Filter | VR2 A100k | Resistencia serie hacia R10/C12; oscurece en sentido horario |
| Volume | VR3 A100k | Taper logaritmico de 20 dB despues del buffer |

Los controles no comparten una funcion `pow()` generica. El recorrido medido
de Distortion necesita cerca de 1.56% de la resistencia total a mitad de
perilla. Una curva `x^6` daba ese punto, pero dejaba demasiado recorrido
muerto abajo; la exponencial de 36 dB conserva el ancla y mejora el primer
cuarto.

Rocksmith usa `Gain -> Distortion`. Sus controles `Tone` y `Filter` se
invierten al mapear VR2 porque el juego interpreta valores altos como mas
brillo, mientras el RAT real se oscurece al girar Filter en sentido horario.
Volume queda fijado en 0.62 para los tonos de canciones.

## 5. Filter no es un tone stack generico

VR2 se usa como reostato en serie con R10=1.5k y C12=3.3 nF:

```text
fc = 1 / (2*pi*(R_VR2 + 1.5k)*3.3nF)
```

El rango electrico va desde decenas de kHz abierto hasta aproximadamente 475 Hz
cerrado. El control debe reducir agudos y nivel de forma pasiva; no corresponde
mezclar un low-pass y un high-pass ni compensar automaticamente su perdida.

## 6. Calibracion con referencias

Se renderizaron los mismos 32 segundos del DI a 48 kHz y se midieron RMS,
crest factor y centroide espectral. Volume permanecio en 0.62.

| Dist | Filter | RMS modelo/ref | Centroide modelo/ref |
|---|---|---:|---:|
| 0.5 | Min | -19.80 / -19.79 dBFS | 1323 / 1325 Hz |
| 0.5 | Half | -20.42 / -20.41 dBFS | 1069 / 1130 Hz |
| 0.5 | Max | -25.34 / -26.19 dBFS | 605 / 684 Hz |
| 1.0 | Min | -18.82 / -18.19 dBFS | 928 / 860 Hz |
| 1.0 | Half | -19.20 / -18.63 dBFS | 801 / 703 Hz |
| 1.0 | Max | -22.82 / -22.11 dBFS | 458 / 400 Hz |

El modelo conserva el cambio correcto de color y nivel en los seis puntos. La
diferencia residual principal esta en el crest factor a Distortion maximo: el
modelo mantiene entre 2.3 y 3.3 dB mas pico que los renders. No se corrigio con
un limitador final porque ese bloque no existe en el esquema; se debe revisar
si aparecen renders de nivel interno, mediciones del pedal fisico o una
referencia aislada del nodo de diodos.

## 7. Pruebas obligatorias para otra distorsion

1. Identificar donde ocurre el clipping y si es shunt, feedback o por rieles.
2. Derivar la ganancia y filtros anteriores al clipper desde R/C reales.
3. Mantener voltios y unidades de audio consistentes en op-amps y diodos.
4. Modelar GBW, slew y rieles antes de agregar filtros de calibracion.
5. Implementar cada pote con su valor, orientacion y taper.
6. Renderizar el mismo DI en minimo, mitad y maximo de Drive/Tone.
7. Medir RMS, peak, crest, centroide, bandas y envolvente por ventanas.
8. Barrer todas las perillas y comprobar que no haya saltos, NaN ni silencios.
9. Probar silencio, impulso, ruido y bloques largos a 44.1/48/96 kHz.
10. Compilar en un directorio limpio, instalar, firmar y cargar el VST3 real.

Una coincidencia de RMS no valida una distorsion. El ataque, la compresion, el
reparto de armonicos y la perdida del filtro deben coincidir por separado.

## 8. Lecciones del resto de la familia

La pasada sobre DS-1, HM-2, SD-1, Guv'nor Plus, MT-2, DOD 250, OS-2, OCD,
Germanium Drive y B3K agrego estas reglas:

- Un clipper de realimentacion no se puede tratar como un shunt clipper a
  tierra. Se calcula primero la tension desarrollada en la rama de feedback,
  se aplica la curva asimetrica de los diodos y se recompone la salida no
  inversora.
- El orden numerico debe coincidir con el orden analogico. Representar una
  transferencia de primer orden dentro de un IIR de tercer orden agrega pares
  polo-cero cancelados en `z=-1`; durante la automatizacion pueden perder
  precision y producir caidas de volumen o distorsion discontinua.
- No todos los diodos del esquema procesan audio. D001/D002 del MT-2 pertenecen
  al switching de bypass/salida; el hard clipping audible es D003/D004 despues
  de C027/R033.
- No se agrega un `tanh` despues de un op-amp limitado, un clipper fisico o un
  transformador ya saturado. Cada no linealidad debe corresponder a un bloque
  identificable del circuito.
- Las etapas con bias, como el transistor de germanio o los inversores CMOS,
  deben restar su salida quiescente. El bias sigue afectando la asimetria, pero
  una entrada digital cero debe producir salida cero y no un pulso al activar
  el plugin.
- Los controles fijos del circuito siguen siendo fijos. En el Guv'nor, `Gain`
  modifica la primera etapa; la segunda conserva R6/R2 = 680k/10k. Hacer que
  ambas dependan de la perilla cambia la compresion y el recorrido completo.
- Para potes especiales se usa su ley real. El W20k del SD-1 requiere una curva
  en S y su red C4/C5/C6 se discretiza como transferencia activa de tercer
  orden, no como una mezcla arbitraria de low-pass y high-pass.

El proyecto `davemollen/dm-SD1` se uso como referencia conductual para el
recorrido de Drive y para contrastar las transferencias del SD-1. Su codigo es
GPLv3; la implementacion de Slopsmith deriva sus propios coeficientes desde el
esquema local y conserva sus modelos compartidos de uPC4558 y 1S2473.

## 9. Referencias SD-1 y OCD

Las carpetas `../test logic/sd1/` y `../test logic/ocd/` contienen renders
estereo con canales identicos. Para medirlos se debe seleccionar un canal; una
conversion automatica a mono suma ambos y agrega 3.01 dB falsos.

En el SD-1, el clipper aproximado se reemplazo por el solver Shockley compartido
del 1S2473: una rama usa D4 y la otra D5+D6 en serie, con la resistencia real de
feedback y conversion de 3 V por unidad. En los seis puntos el RMS del modelo
queda entre 0.1 y 1.8 dB de la referencia, sin copiar un limitador final.

Los renders del OCD fueron grabados a menor volumen. Se usan para las relaciones
de Drive, Tone y la forma del clipping, no para fijar el nivel absoluto. El DSP
reproduce los 3.6 dB entre Drive mitad/maximo y la respuesta relativa de Tone
dentro de 0.1 dB; Volume a mitad se calibra al RAT del proyecto (`-25.9` frente
a `-25.7` dB RMS). La posicion germanio usa OA90 como sustituto disponible del
1N34A opcional mostrado por el esquema.
