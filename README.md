# Excellecu — EQ por DFT con bandas dinámicas (prototipo v0.20)

Ecualizador de fase cero que trabaja en el dominio de la frecuencia, con curvas gaussianas.
Sale del prototipo web `web/eq-dft-proto.html` (v0.18, `eq_dft_proto_50_motor_exacto.html`).

- Hasta 12 bandas, de cinco tipos: campana, low shelf, high shelf, high pass y low pass.
- Cada banda puede ir en Estéreo, L/R (un ajuste para cada canal) o M/S (Mid y Side).
- Se crean y se mueven sobre la gráfica: doble clic crea, arrastrar mueve, Mayús + arrastrar
  cambia Q o pendiente, Alt afina, rueda = Q/pendiente, clic derecho = menú, Supr = borrar.
- Motor de convolución exacta: las curvas se convierten en un filtro fijo de fase cero de 8193
  muestras (diseñado con una FFT de 16 384) y se aplican por convolución particionada (trozos de
  256 muestras): coste repartido por igual en cada bloque y latencia de 4352 muestras (98,7 ms a
  44,1 kHz). Los filtros se calculan en un hilo aparte y, al cambiar un ajuste, se funde del
  filtro anterior al nuevo en 2048 muestras. Sin aliasing temporal ni huella de bloque.
- El motor Clásico de la web (STFT con ventana Hann) se quitó del plugin en la v0.20: medía peor
  y en la prueba de nulo apenas se distinguía. Sigue en el prototipo web para comparar.

Formatos: VST3 (Windows, macOS, Linux) y AU (macOS). Efecto estéreo o mono.

## Diferencias con la web

- La web procesa un archivo entero; el plugin procesa en tiempo real con latencia (se declara
  al DAW). El resultado es idéntico al de la web (lo comprueba `tests/`), solo retrasado.
- En un render sin tiempo real, los filtros se calculan en el propio proceso, para que el
  resultado no dependa de la velocidad del render.
- No hay carga de archivo, reproducción ni exportación WAV (lo hace el DAW).
- Cada banda ocupa un "hueco" fijo (1 a 12) para que el DAW pueda automatizarla. Al borrar una
  banda, las demás no se renumeran como en la web; la siguiente nueva ocupa el hueco libre.
- Todos los ajustes son parámetros automatizables (12 bandas × 5 juegos). El tamaño del filtro
  es fijo (8192); la web permite elegir ventana, overlap y motor.

## Compilar

Todo se compila en GitHub Actions (`.github/workflows/build.yml`): primero compara el motor
C++ con la web, luego compila para Windows, macOS y Linux, pasa la prueba de humo y pluginval.
Las descargas están en Actions → la ejecución → Artifacts.

En local:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Excellecu_All
# comparación con la web
node tests/run_js.mjs out && g++ -std=c++17 -O2 -I Source -o compare_cpp tests/compare_cpp.cpp && ./compare_cpp out && node tests/compare.mjs out
```
