# Workflow canónico para modelado y matching de audio

Estado: 2026-07-19

Esta es la metodología obligatoria para construir o corregir amplificadores y
pedales de Slopsmith. Las guías por familia explican la electrónica específica;
este documento define cómo demostrar que el resultado sonoro coincide con las
referencias y cómo evitar arreglos que solo igualan volumen o EQ.

La regla principal es:

> El esquema define la topología. Los datasheets definen los límites de los
> componentes. Las referencias alineadas con el DI exacto calibran el resultado.

No declarar terminado un modelo porque compila, porque el RMS coincide o porque
"suena parecido" en una sola posición.

## 1. Condiciones mínimas antes de programar

Reunir y registrar:

1. Esquema y revisión exacta del equipo.
2. Datasheets de tubos, transistores, JFET, op-amps, OTA/VCA, BBD, diodos,
   optoacopladores y otros componentes que condicionen el audio.
3. Valores y tapers de todos los potenciómetros y posiciones de switches.
4. Ruta de señal por canal y por modo.
5. DI exacto usado para crear cada referencia.
6. Sample rate, canal, duración y posición de todas las perillas de la referencia.
7. Si la referencia es amp-only, con gabinete, mono, estéreo o contiene efectos.

Crear una tabla antes de medir:

| Referencia | DI | Canal/modo | Controles | Master/Output | Cab | Sample rate |
|---|---|---|---|---|---|---|
| `archivo.wav` | `di.wav` | nombre | valores | valor | on/off | Hz |

Si falta el DI exacto o la configuración, se puede construir desde el esquema,
pero no afirmar que el sonido está matched. Documentar el faltante y detener la
calibración fina hasta obtener esa información.

## 2. Traducir el esquema antes de afinar

Dividir el circuito en bloques y anotar para cada uno entrada, salida, bias,
impedancias y componentes:

- entrada, protección y bias;
- etapas de ganancia;
- acoples y grid/current blocking;
- clipping o elemento de control de ganancia;
- tone stacks y filtros;
- mezclas dry/wet y rutas paralelas;
- phase inverter y power amp, si corresponde;
- fuente de poder, sag y nodos de alimentación;
- buffers y etapa de salida;
- controles y switches;
- matriz mono/estéreo.

Clasificar cada bloque:

- **Lineal:** R/C, filtros, mezcladores, pérdidas, transformador en región lineal.
- **No lineal:** tubo, transistor, diodo, op-amp contra rieles, OT saturado.
- **Dinámico:** sag, bias shift, detector, compander, envelope, LFO, BBD.

Implementar en el mismo orden eléctrico. No reemplazar una etapa desconocida por
`tanh`, un filtro genérico o un compresor final. Cada no linealidad debe poder
señalarse en el esquema o justificarse mediante el datasheet.

Mantener una escala explícita entre unidades de audio y voltios. Un solver de
diodo u op-amp con umbral físico deja de representar el componente si recibe una
unidad arbitraria sin conversión documentada.

## 3. Orden obligatorio de implementación

1. Ruta y selección de canales/modos.
2. Circuito lineal con pérdidas y respuesta de frecuencia.
3. Bias y headroom de cada etapa.
4. No linealidades en su nodo real.
5. Memoria: capacitores, sag, blocking, attack/release, BBD o LFO.
6. Potenciómetros con valor, orientación y taper reales.
7. Oversampling alrededor de las no linealidades que lo necesiten.
8. Nivel de salida y, solo si el producto lo requiere, loudness compensation.

El gain de salida nunca sustituye al drive de V1, PI, power amp, clipper, OTA o
transistor. Un limiter posterior tampoco demuestra que el circuito distorsiona.

## 4. Protocolo de render reproducible

Usar el mismo DI, canal, sample rate, duración y controles de la referencia.
Desactivar cabina cuando la referencia sea directa. Mantener Master/Output en el
valor usado por la referencia; por ejemplo, una comparación creada con Master
`0.60` no valida un render a `1.0`.

Render offline de un amp:

```sh
cd rig_builder
python3 tools/render_amp_wav.py tw26 \
  '../test logic/ce1_ref/brit_di.wav' \
  /tmp/tw26_candidate.wav \
  'Inst Vol=1' 'Mic Vol=0' 'Tone=1' 'Cab Sim=0'
```

Render offline de un pedal:

```sh
cd rig_builder
python3 tools/render_amp_wav.py pedals/phaser_363 \
  '../test logic/ce1_ref/brit_di.wav' \
  /tmp/phase90_candidate.wav \
  'Rate=0.5'
```

El harness imprime nivel, peak, clipping, near-zero y ventanas de dropout. Los
nombres de parámetros deben salir del `*Params.h`; no usar índices copiados de
otro plugin.

Comparación común:

```sh
uv run --with numpy --with scipy python tools/compare_amp_reference.py \
  '<DI exacto>.wav' '<referencia>.wav' '<candidate>.wav' \
  --sample-rate 48000 --window-ms 50
```

Aunque el script conserva su nombre histórico, sirve para amps y pedales cuyo
render usa el mismo DI. El reporte separa nivel, dinámica, espectro y coherencia.

Cuando dos renders conservan diferencias audibles aunque las bandas y el crest
global parezcan cercanos, usar la comparación no lineal por ventanas:

```sh
uv run --with numpy --with scipy python tools/compare_nonlinear_reference.py \
  '<DI exacto>.wav' '<referencia>.wav' '<candidate>.wav' \
  --sample-rate 48000 --window-ms 20
```

Este segundo reporte iguala nivel solo para el análisis y separa:

- `spectrum_delta_db`: balance tonal realmente audible;
- `linear_transfer_delta_db`: parte linealmente predecible desde el DI;
- `nonlinear_residual_delta_db`: saturación, compresión temporal y ruido no
  explicables por una transferencia lineal;
- `attack/steady/decay`: diferencias de ganancia en los mismos eventos.

No corregir un déficit de `nonlinear_residual_delta_db` con EQ. Tampoco subir
drive si solo difiere `spectrum_delta_db`: primero identificar cuál de las dos
categorías está fuera de referencia.

Para riffs que comienzan tocados suavemente, no basta el tercil global. Aislar
el inicio y revisar `input_level_bins.quiet`:

```sh
tools/compare_nonlinear_reference.py '<DI>.wav' '<ref>.wav' '<candidate>.wav' \
  --start-seconds 0 --duration-seconds 3
```

Comparar `crest_p50_delta_db`, `nonlinear_residual_delta_db` y su desglose por
banda. Esto detecta amps que coinciden en los golpes fuertes pero permanecen
limpios en notas suaves.

### 4.1 Comparación obligatoria por nivel de entrada

El rework del DR103 demostró que una coincidencia global puede ocultar un error
grave: el candidato coincidía en golpes fuertes, pero el inicio suave del Brit DI
permanecía limpio. Desde ese caso, todo amp nuevo o corregido debe validarse en
estas cuatro vistas, siempre con el mismo DI y las mismas muestras:

1. Archivo activo completo, para balance y dinámica general.
2. Inicio o pasaje suave aislado con `--start-seconds` y `--duration-seconds`.
3. `input_level_bins.quiet`, `.medium` y `.strong`, comparando crest y residuo no
   lineal por banda.
4. Sweep de cada control de gain/volume en al menos 11 puntos, comprobando que
   el carácter cambia sin introducir dropouts, clipping digital ni saltos de
   volumen ajenos al comportamiento definido para Slopsmith.

No aprobar un amp si solo coincide el bin `strong`. Las notas suaves deben tener
el mismo tipo de breakup, compresión y cuerpo que la referencia. Tampoco aprobar
un resultado porque la compensación de salida reduzca el crest: confirmar que la
no linealidad se genera en V1, etapas intermedias, PI o power amp según el esquema.

Cuando no existe una referencia directa, no declarar el modelo como *matched*.
Se puede usar como límite comparativo un amp de la misma familia únicamente si
se documenta qué circuito comparten. Renderizar ambos con el mismo DI/controles,
conservar las partes eléctricamente idénticas y justificar cada diferencia desde
el esquema. Ejemplo: DR504 contra DR103 para el preamp común, pero con su propia
fuente, bias, resistencias de pantalla, par de EL34 y transformador de salida.

Después del core offline, compilar, instalar y volver a probar el VST3 real. El
render del source no detecta un bundle viejo, un mapeo incorrecto de parámetros
o un wrapper que altera la señal después del core.

## 5. Alineación y selección de audio

Antes de interpretar métricas:

1. Convertir DI, referencia y candidato al mismo sample rate sin sumar canales.
2. Seleccionar un canal cuando L/R sean copias; sumarlos añade 3.01 dB falsos.
3. Alinear referencia y candidato por correlación de envolvente o waveform.
4. Compensar latencia y recortar los tres archivos a la duración común.
5. Excluir silencios finales o iniciales que no estén presentes en los tres.
6. Elegir ventanas activas desde el DI, no desde la salida procesada.
7. Comparar siempre los mismos eventos y muestras del riff.

No comparar archivos solo porque duran lo mismo. No usar Fast Thrash DI para una
referencia creada con Brit DI ni viceversa.

## 6. Métricas obligatorias

### 6.1 Nivel y estabilidad

- RMS y peak crudos.
- `clip_frac`, no-finitos y denormals.
- near-zero y ventanas activas que desaparecen.
- ganancia de salida por ventanas de 20 a 50 ms.
- p10/p50/p90 de gain y crest.
- p90/p99/p99.9 del peak normalizado por RMS local.

### 6.2 Tono

- energía por bandas relevantes;
- respuesta con ruido blanco para filtros donde el DI no tenga excitación;
- centroide/flatness solo como métricas auxiliares;
- dirección y rango de Tone, Cut, Filter, EQ y switches.

Un DI de guitarra con poca energía sobre 8 kHz no puede demostrar por sí solo la
respuesta de un filtro en esa zona. Usar ruido o sweep para el bloque lineal y el
DI para comprobar cómo crecen los armónicos reales.

### 6.3 Distorsión y carácter no lineal

- crest por ventanas, no solo crest global;
- crecimiento armónico entre gain mínimo/mitad/máximo;
- THD y distribución armónica con senos a varios niveles;
- coherencia DI/salida por `80-800`, `800-2500` y `2500-8000 Hz`;
- correlación de waveform después de alinear y nivelar;
- ataques, sustain, decay, bias shift, gating y blocking en las mismas ventanas.

La coherencia mide cuánto de la salida sigue siendo explicable por una relación
lineal con el DI. No se minimiza: debe seguir a la referencia. Una coherencia
mucho mayor que la referencia suele significar que el modelo permanece limpio;
una mucho menor puede indicar saturación excesiva, ruido o artefactos.

RMS, EQ y crest pueden coincidir con un compresor o limiter limpio. Por eso no
validan solos una distorsión.

### 6.4 Controles

- barrido de 11 o 21 puntos para Gain/Drive/Volume/Sustain;
- mínimo, cuarto, mitad, tres cuartos y máximo para cada control continuo;
- todas las posiciones de cada switch;
- combinaciones 2D donde los potes interactúan eléctricamente;
- automatización continua sin zipper, clicks ni saltos de estado.

El recorrido debe compararse completo. Afinar solo el máximo puede destruir el
clean; afinar solo la mitad puede dejar el extremo sin breakup.

## 7. Orden obligatorio de calibración

Corregir una categoría a la vez, en este orden:

1. **Routing y settings:** DI, canal, modo, cab, Master y perillas correctas.
2. **Nivel de entrada:** comprobar lo que recibe la primera etapa no lineal.
3. **Topología y drive:** hacer coincidir coherencia, armónicos, crest temporal y decay.
4. **Dinámica:** sag, bias, blocking, detector, attack/release o compander.
5. **Tono:** ajustar redes del circuito y respuesta del componente.
6. **Tapers:** ubicar clean, breakup y extremos en el mismo punto de la perilla.
7. **Nivel de salida:** aplicar Volume o compensación estática después de toda
   no linealidad cuando el producto necesite loudness constante.
8. **Multirate y VST3 real:** validar sample rates, automatización y bundle instalado.

Después de cada cambio de drive o filtro dentro de la cadena no lineal, volver a
medir. No conservar tablas de makeup calculadas con una versión anterior.

## 8. Loudness compensation sin alterar el circuito

Cuando Slopsmith requiera volumen constante entre gains:

- medir la salida completa en 11 o 21 puntos;
- construir una curva estática e interpolada;
- aplicarla después del último bloque no lineal y después del oversampling;
- mantener separado el Volume/Master real si forma parte del equipo;
- volver a comprobar que crest, coherencia y armónicos no cambian al activar el trim.

Prohibido:

- usar makeup antes del clipper o power amp;
- normalizar muestra a muestra o con envelope follower;
- usar un limiter para esconder peaks;
- cancelar Drive con una ganancia inversa que cambie lo que recibe la siguiente etapa;
- aplicar una tabla vieja después de modificar drive, filtros o oversampling.

## 9. Pruebas específicas por familia

| Familia | Además de las métricas comunes |
|---|---|
| Amp | Pre-V1, breakup por canal, PI, power, sag, NFB, OT, cab on/off |
| Distorsión/drive | Umbral, tipo de clipper, asimetría, slew/GBW, decay |
| Fuzz | Bias, gating real, bloom, octave, sustain y cancelaciones |
| Compresor | Curva entrada/salida, gain reduction, attack, release, detector |
| Chorus/flanger/vibrato | Delay central, excursión, LFO, wet/dry, feedback, estéreo |
| Phaser | Cantidad y trayectoria de notches, Q, feedback, LFO |
| Wah/envelope filter | Cutoff por tiempo, Q, dirección, detector y sensibilidad |
| Delay/echo | Taps, feedback, bandwidth, saturación, wow/flutter y cola |
| Reverb | Predelay, EDT/RT60 por banda, difusión, cola y matriz estéreo |
| Ring mod | Carrier, sidebands, leakage, dry/wet y aliasing |

Las guías por familia tienen prioridad para la interpretación de esas métricas,
pero no pueden omitir el matching común de DI, alineación y estabilidad.

## 10. Gates de aceptación

Objetivos por defecto cuando existen referencias suficientes:

- cero NaN, infinitos, clipping digital y dropouts no intencionales;
- error RMS menor a `0.25 dB` en puntos calibrados para loudness constante;
- error tonal normalmente menor a `1 dB` por banda y nunca oculto;
- crest p50/p90 dentro de `0.5 dB` cuando la referencia lo permite;
- coherencia por banda dentro de `0.05` absoluto como objetivo;
- desviación multirate de nivel menor a `0.1 dB`;
- controles monotónicos cuando el circuito exige monotonicidad;
- bypass, mono/estéreo y automatización sin discontinuidades.

Estos números no autorizan a modificar una topología correcta con bloques que no
existen. Si una diferencia no puede cerrarse sin inventar circuito, documentarla,
explicar la evidencia faltante y dejar el modelo como pendiente de validación.

## 11. Validación multirate y despliegue

Probar como mínimo:

- 44.1, 48 y 96 kHz;
- bloques de distintos tamaños;
- silencio prolongado;
- impulso y ruido;
- entrada reducida y entrada fuerte;
- feedback/gain máximo;
- cambios de preset y automatización;
- mono, estéreo y suma a mono cuando corresponda.

Después:

1. Incrementar versión del plugin.
2. Compilar desde source limpio del plugin afectado.
3. Copiar al bundle canónico y aliases necesarios.
4. Firmar y verificar el VST3.
5. Reiniciar el host para evitar una instancia antigua.
6. Confirmar que el canvas muestra controles reales y que sus IDs llegan al DSP.
7. Renderizar nuevamente desde el VST3 desplegado si hay cualquier diferencia.

## 12. Evidencia que debe quedar documentada

Cada modelo terminado debe registrar:

- esquema y datasheets usados;
- topología implementada;
- DI y referencias exactas;
- posiciones de perillas y modos;
- tabla modelo/referencia de min/half/max;
- coherencia, crest, bandas, RMS y ventanas;
- sweep de loudness, si existe;
- resultados 44.1/48/96 kHz;
- diferencias residuales conocidas;
- versión, bundle y fecha de validación.

No escribir únicamente "suena bien" o "matched". Guardar números y rutas que
permitan repetir la prueba.

## 13. Checklist final

- [ ] Confirmé DI, referencia, canal, modo, cab y controles exactos.
- [ ] Traducí la ruta completa del esquema antes de afinar.
- [ ] Cada no linealidad corresponde a un componente o bloque identificable.
- [ ] Verifiqué escala de voltios, bias, headroom y nivel antes del clipper/V1.
- [ ] Comparé muestras y ventanas alineadas, no solo estadísticas globales.
- [ ] Medí RMS, bandas, crest, coherencia, armónicos y decay.
- [ ] Probé mínimo, mitad, máximo y el barrido completo de controles críticos.
- [ ] El clean sigue sonando y el extremo driven distorsiona como la referencia.
- [ ] Makeup está después de toda no linealidad y no cambia el carácter.
- [ ] No hay clipping, NaN, glitches ni dropouts no intencionales.
- [ ] Validé 44.1/48/96 kHz y automatización.
- [ ] Compilé, firmé y cargué el VST3 real.
- [ ] Documenté resultados y diferencias residuales.
