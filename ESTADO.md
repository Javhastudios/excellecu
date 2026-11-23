# Estado del proyecto — Excellecu (EQ por DFT, bandas dinámicas)

> Para retomar: "lee el ESTADO.md de excellecu". Actualizado: 23/11/2026.
> Plan común de Javhastudios (venta, firma, web): ESTADO.md del proyecto de Claude
> "Javhastudios Plugins". Antecesor: boboongecu (v0.4, borrado tras medirlo en Plugin Doctor).

## Qué es

Prototipo de EQ de fase cero con curvas gaussianas y dos motores: Exacto (convolución con
zero-padding, por defecto) y Clásico (FFT → ganancia por bin → FFT inversa → suma solapada).
Original: `web/eq-dft-proto.html` (v0.18, "50 motor exacto"). Objetivo: medirlo en Plugin
Doctor y seguir iterando.

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

- Lo que salga de las medidas en Plugin Doctor.
