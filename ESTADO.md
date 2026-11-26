# Estado del proyecto — Excellecu (EQ por DFT, bandas dinámicas)

> Para retomar: "lee el ESTADO.md de excellecu". Actualizado: 26/11/2026.
> Plan común de Javhastudios (venta, firma, web): ESTADO.md del proyecto de Claude
> "Javhastudios Plugins". Antecesor: boboongecu (v0.4, borrado tras medirlo en Plugin Doctor).

## Qué es

Prototipo de EQ de fase cero con curvas gaussianas y dos motores: Exacto (convolución con
zero-padding, por defecto) y Clásico (FFT → ganancia por bin → FFT inversa → suma solapada).
Original: `web/eq-dft-proto.html` (ahora v0.21). Objetivo: medirlo en Plugin Doctor y seguir
iterando. **Acuerdo (26/11/2026): cada avance del plugin se hace también en el HTML**, que Javier
guarda como archivo; el HTML conserva además los motores Clásico, la ventana y el overlap.

## v0.22 (26/11/2026): nueva disposición, escala vertical y RMS AES-17

- Ventana sin gráfica de fase (queda "fase lineal ✓" en la barra de estado; la comprobación sigue
  en `ChartData`). Gráfica de magnitud a todo lo ancho y casi el doble de alta: ventana por defecto
  1120×1000 (mínimo 960×760), lienzo de ~560 px (antes ~315). Chips de bandas en la cabecera de
  la gráfica. Debajo, `BandPanel` (cabecera, tipo y canal en una fila, deslizadores en fila) y
  `OutputPanel`, uno al lado del otro. Medidores a la derecha. Desaparece la columna izquierda
  (`ControlsPanel` y su barra de desplazamiento) y la nota del motor (ya está en la barra de estado).
- Escala vertical ±3 / ±6 / ±12 / ±24 dB (por defecto ±12), con un 10 % de margen; rejilla cada
  1 / 2 / 3 / 6 dB. Arrastrar en vertical es proporcionalmente más fino con más zoom. Se guarda con
  la sesión (`zoomDb` en el estado).
- Medidores: selector RMS AES-17 (por defecto, +3,01 dB) / sin ponderar, guardado con la sesión
  (`rmsAes17`). El pico y la saturación no cambian.
- Web v0.22: misma disposición; lienzo de 320 px (antes 160); debajo, tira propia de la web
  (señal de prueba, procesar, motor) y la fase plegada en "Fase (diagnóstico)" hasta el final del
  desarrollo. Escala y modo RMS se recuerdan en el navegador.
- Pruebas: humo (AES-17 por defecto y sin ponderar, escala guardada, capturas 07 escala ±3 y
  08 tamaño mínimo) y Chrome (gráfica 320 px con la banda debajo, fase plegada que se despliega,
  escalas ±3/±24, selector RMS).

## v0.21 (26/11/2026): funciones para la beta

- **Ganancia de salida L/R–M/S** (sección "Salida" del panel). Parámetros `out_l`, `out_r`, `out_m`,
  `out_s` (−24 a +12 dB), `out_mode` y `out_link`. Las cuatro actúan siempre, en serie (L/R y luego
  M/S); el modo solo elige qué pareja se edita, así cambiar de modo no altera lo que suena. Si la
  otra pareja no está a 0 dB se avisa debajo y su botón se marca. "Enlazar" mueve las dos lo mismo.
  Suavizado de 50 ms. En mono actúan L y Mid. Doble clic en un deslizador = 0 dB.
- **Medidores** (columna "Nivel"): entrada antes del EQ y salida después de la ganancia. Pico
  (caída de 20 dB/s), RMS de ~330 ms, pico retenido 1,5 s, piloto de saturación (≥ 0 dBFS) y
  máximo; clic para borrarlos. El audio acumula sin bloqueos y la interfaz recoge a 30 Hz.
  Pendiente: true peak, LUFS y vista M/S.
- **Deshacer / rehacer** (`Source/state/UndoHistory.h`): historial propio del procesador (sobrevive
  a cerrar la ventana), 100 pasos. Solo apunta cambios hechos desde la interfaz (los que abren y
  cierran gesto; la automatización no lo hace). Cada paso guarda antes/después solo de los
  parámetros tocados, más lrFresh y la banda seleccionada. Todo lo de una misma acción (crear una
  banda, cargar un preset) y todo un arrastre es un paso; toques seguidos del mismo control en
  menos de 400 ms se juntan (rueda). Botones arriba y ⌘Z / ⌘⇧Z (Ctrl+Z / Ctrl+Mayús+Z / Ctrl+Y).
  Cargar una sesión vacía el historial. Ojo: Ableton se queda ⌘Z si el foco no está en el plugin, y
  también apunta los cambios de parámetros en su propio deshacer.
- **Presets** (`Source/state/Presets.h`, barra superior): de fábrica "Plano" y "TDM"; propios como
  archivos `.excellecu` (XML con `version`) en `~/Library/Audio/Presets/Javhastudios/Excellecu`
  (Mac) o `Documentos/Javhastudios/Excellecu/Presets` (Windows). Anterior / siguiente, menú,
  Guardar (pide nombre, avisa si existe), importar (copia a la carpeta), exportar, abrir carpeta.
  Guardan bandas existentes + lrFresh + salida; lo que falte al cargar vuelve a su valor por
  defecto y lo desconocido se ignora. Cargar es un paso de deshacer. El nombre se guarda con la
  sesión y se marca "modificado" si el estado difiere del preset.
- **TDM de fábrica**, sacada de las capturas de Plugin Doctor del 26/11: 1 Campana 1,55 kHz +1,0
  Q 0,41 · 2 High shelf 20 kHz +1,7 Q 0,49 · 3 High shelf Side 388 Hz +0,5 Q 2,00 (Mid a 0 dB) ·
  4 Campana Mid 60 Hz +1,0 Q 0,62 (Side a 0 dB) · 5 Campana 214 Hz −0,8 Q 4,28.
- Ventana: tooltips activados (antes no había `TooltipWindow`).
- **Prototipo web v0.21** con lo mismo: barra de deshacer/presets, sección Salida y columna Nivel.
  `processAudio` no cambia (las pruebas contra C++ siguen idénticas bit a bit): la ganancia de
  salida se aplica al reproducir el procesado (GainNodes en matriz 2×2, se mueve en directo) y al
  exportar el WAV. Medidores durante la reproducción (entrada = original, salida = lo que suena).
  Deshacer por instantáneas al terminar cada gesto (soltar, clic, tecla; rueda tras 0,4 s).
  Presets en el **mismo formato `.excellecu` que el plugin** (se intercambian en los dos sentidos);
  los propios se guardan en el navegador (localStorage) y se exportan/importan como archivo.
  Importar un archivo que se llama como uno de fábrica le añade " (importado)" (también en el plugin).
- Medidores del plugin, corrección (26/11): antes se acumulaba en atómicos sueltos y, si la interfaz
  recogía a mitad de una suma, un bloque se contaba dos veces (RMS con saltos de hasta +3 dB) y la
  energía y el número de muestras podían quedar descuadrados. Ahora cada bloque deja pico, suma
  y muestras juntos en una cola sin bloqueos (`AbstractFifo`). Prueba nueva con el audio en otro
  hilo. Web: los medidores descuentan la latencia de salida del navegador. RMS sin ponderar ni
  ajuste AES-17 (un seno marca 3 dB por debajo de su pico).
- Prueba nueva `tests/web_ui.mjs` (Chrome con Playwright, en GitHub Actions tras la prueba de humo):
  arrastre real = un paso, Ctrl+Z/Y, salida enlazada y M/S, TDM, la TDM escrita por el plugin abre
  igual en la web, guardar/importar, medidores reproduciendo. Captura `web_v021.png`.
- Prueba de humo: ganancia L/R y M/S, cambio de modo, suavizado sin saltos, mono, medidores
  (pico, RMS, saturación), deshacer (pasos, arrastre, automatización, rueda, lrFresh, selección,
  límite, sesión), presets (ida y vuelta, defecto, incompletos, fábrica = a mano, TDM, sesión) y
  capturas 05 (salida y medidores) y 06 (TDM).
- Por comprobar en tu Mac: atajos de teclado en Ableton, diálogos de guardar/importar/exportar
  en VST3 y AU, y que la TDM de fábrica coincide con la tuya en Plugin Doctor.

## v0.20 (24/11/2026): solo motor Exacto, filtro fijo de 8192

- Prueba de nulo en Ableton (loops iguales, mismos puntos, uno con polaridad invertida): Exacto
  y Clásico casi anulan; el residuo solo se oye con los monitores al máximo ("fondo de
  botella"). Decisión: quitar el Clásico del plugin y fijar N = 8192.
- Quitados los parámetros `engine`, `winSize` y `overlap` y sus controles (panel "Motor" queda
  como nota). `Params` ya no tiene `overlap` ni `exact`; `latencyFor (n)` = n/2 + 256; el
  procesador usa `kWindow = 8192`. El motor C++ conserva otros N para las pruebas contra la web.
- Pruebas: comparación con la web solo en el motor Exacto (8 escenarios, idénticos); prueba de
  humo adaptada (latencia 4352, sin parámetros de motor, HPF en graves, 12 bandas).
- El título de la ventana muestra la versión del plugin (antes "V0.18" fijo).
- Sesiones guardadas con versiones anteriores: los tres parámetros quitados se ignoran.
- Pendiente: a 88,2/96 kHz el filtro de 8192 resuelve la mitad en graves; valorar 16 384 ahí.

## v0.19.1 (24/11/2026): cuelgue al reabrir la ventana y ventana lenta

- Cuelgue (Ableton, VST3, Mac): al abrir la ventana con una banda seleccionada, el botón
  "Activa" avisaba al conectarse a su parámetro y eso refrescaba la ventana a medio construir
  (`newTypeButtons` aún vacío). Venía de antes de la v0.19. Arreglo: `refreshNow` no hace nada
  hasta que termina el constructor (`constructed`, declarado antes de los componentes).
  Prueba de humo nueva: reabrir la ventana con banda seleccionada y sin selección.
- Ventana lenta (~8 s en abrir y otros ~8 en responder en Ableton): la gráfica de fase dibujaba
  un punto por bin (4096 con ventana 8192), casi todos en los agudos saltando entre ±180°, y
  además a trazos. Ahora, como mucho, un punto por píxel. Medido aquí: pintar la ventana entera
  pasa de ~1,2 s a ~0,37 s en depuración y queda en ~20 ms en la compilación normal.

## v0.19 (24/11/2026): convolución particionada en el motor Exacto

- Motivo: en Plugin Doctor (pestaña Performance) el motor Exacto salía "en escalera": cada N/2
  muestras se calculaba un bloque entero de golpe (FFT de 2N), con picos de CPU que con buffers
  pequeños y muchas instancias pueden provocar cortes.
- `EqEngine.h`: el filtro es el mismo que en la web (mismo `kernelSpectrum`), pero se hace causal
  (retraso N/2) y se parte en trozos de 256 muestras (`PART`), cada uno con su FFT de 512. El
  audio se procesa en bloques de 256 con overlap-save y una línea de retardo en frecuencia.
  Latencia N/2 + 256 (`latencyFor`): 8192 → 4352 muestras (90,7 ms a 48 kHz; antes 8192).
- Filtros calculados fuera del hilo de audio: `KernelBuilder` + ranuras sin bloqueos en
  `Engine` (`buildPending` desde otro hilo, recogida en el audio). En el plugin lo hace
  `KernelThread` (sondea cada 5 ms). Al llegar un filtro nuevo se funde del anterior en 2048
  muestras (`FADE_LEN`). En render sin tiempo real (`isNonRealtime`) se calculan en el propio
  proceso. Si no hay filtro válido (al empezar o al cambiar ventana) también se calcula ahí.
- Medido (N=8192, estéreo, 3 bandas, en la máquina de pruebas): peor llamada con buffer 64,
  0,75 ms (antes 1,83 ms, por encima del presupuesto de 1,33 ms); buffer 1024, 0,41 ms (antes
  6,7 ms). CPU media 1,5 % (antes 2,0 %). Cálculo de filtros con 12 bandas: 10–14 ms en el
  otro hilo; un cambio llega a la salida en ~20 ms y termina de fundirse en ~65 ms.
- Comprobado contra la web: los 16 escenarios salen idénticos bit a bit.
- Prueba de humo: latencias nuevas y prueba en tiempo real (cambio de ganancia desde el hilo de
  filtros, sin saltos en el fundido).
- Por comprobar en Plugin Doctor: latencia 4352 con ventana 8192 (pico de la IR en ~90,7 ms a
  48 kHz); pestaña Performance sin escalera; automatizar una banda sin clics.

## v0.18 (23/11/2026): motor Exacto

- Motivo: en v0.17 los filtros de corte en graves dejaban bandas laterales cada 21,5 Hz
  (convolución circular: la cola del filtro no cabía en el bloque). Medido en Plugin Doctor:
  seno de 29,6 Hz con HPF 50 Hz 48 dB/oct, THD+N −27 dB (Pro-Q 3: −100,5 dB).
- `EqEngine.h`: `Params::exact`; motor exacto en tiempo real (bloque N/2, FFT 2N, filtro de
  ±N/2 con ventana coseno, etapas L/R y M/S combinadas en matriz 2×2 antes de recortar).
  Latencia N, igual que el Clásico. Ventana Hann periódica en el Clásico (como la web).
- Parámetro `engine` (Exacto / Clásico, arranca en Exacto). Selector en el panel Motor DFT; el
  overlap se atenúa en Exacto. Nota con tamaños y latencia.
- Comprobado contra la web v0.18: 8 escenarios × 2 motores, salida idéntica (incluye el caso del
  HPF 48 dB/oct en graves). Prueba de humo: impulso en los dos motores, residuo del HPF en
  graves en modo Exacto < −120 dBFS.
- Por comprobar en Plugin Doctor: latencia 8192 con ventana 8192; THD+N del seno de 29,6 Hz con
  HPF 50 Hz 48 dB/oct (objetivo ≈ −100 dB); curva TDM cerca del corte (≈ 1 dB menos de
  atenuación en Exacto); escucha a ciegas Exacto/Clásico; high shelf cerca de Nyquist.

## Lo que hay (base v0.17)

- Plugin VST3 (+ AU en Mac), efecto estéreo/mono. PLUGIN_CODE `Exeq`.
- Motor `Source/dsp/EqEngine.h` (sin JUCE): curvas y etapas L/R → M/S copiadas de la web; tiempo
  real con latencia = tamaño de ventana, declarada al DAW. Nyquist con la magnitud del último bin.
- Comprobado contra la web (`tests/run_js.mjs` ejecuta `processAudio` del HTML en Node): 7
  escenarios (vacío, estéreo, filtros, L/R, M/S, L/R+M/S, mono). Salida idéntica en todo el archivo.
- Parámetros: 12 huecos `b1..b12` con `used`, `type`, `edit` (Estéreo/L/R/M/S = juego que se
  edita y modo) y por juego `on, fc, gain, q, slope`. `lrFresh` en el estado (propiedad del árbol).
- Interfaz (`Source/ui/Views.cpp`, `BandModel.h`): gráfica con puntos como la web (doble clic,
  arrastre, Mayús, Alt, rueda, menú contextual, Supr), chips de bandas, panel de la banda, fase.
- Prueba de humo (latencia, +6 dB, L/R, M/S, HP/LP −48 dB, cambio de tipo, 12 bandas, estado,
  mono, capturas) y pluginval nivel 5.

## Diferencias con la web

- Sin carga de archivo / reproducción / exportación.
- Bandas en huecos fijos: al borrar no se renumeran las demás.

## Pendiente / ideas

- Quitar la fase plegada de la web al final del desarrollo.
- Confirmar el canal de la banda 3 de la TDM (Side o Mid).

- EQ: codo suave en HPF/LPF; filtro de 16 384 a 88,2/96 kHz; modo de fase mínima opcional.
- Descartado (26/11): igualar el high shelf al del Pro-Q. La TDM es solo una curva de prueba y la
  forma de media gaussiana es parte del carácter. Con la v0.22 la TDM da +1,71 dB a 20 kHz (filtro
  real = curva ideal hasta Nyquist); los −0,3 dB eran de una medida con la v0.17.
- Beta: true peak / LUFS y vista M/S en los medidores; ranuras A/B; inversor de polaridad.
- Ideas en el documento del proyecto (flat tilt, Gain Scale, teclado, forma de campana…).
