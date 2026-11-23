# Estado del proyecto — Excellecu (EQ por DFT, bandas dinámicas)

> Para retomar: "lee el ESTADO.md de excellecu". Actualizado: 23/11/2026.
> Plan común de Javhastudios (venta, firma, web): ESTADO.md del proyecto de Claude
> "Javhastudios Plugins". Antecesor: boboongecu (v0.4, borrado tras medirlo en Plugin Doctor).

## Qué es

Prototipo de EQ por ventanas (FFT → ganancia por bin → FFT inversa → suma solapada).
Original: `web/eq-dft-proto.html` (v0.17, "49 bandas dinámicas"). Objetivo: medirlo en Plugin
Doctor y seguir iterando.

## Lo que hay (23/11/2026)

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
