# Excellecu — EQ por DFT con bandas dinámicas (prototipo v0.18)

Ecualizador de fase cero que trabaja en el dominio de la frecuencia, con curvas gaussianas.
Sale del prototipo web `web/eq-dft-proto.html` (v0.18, `eq_dft_proto_50_motor_exacto.html`).

- Hasta 12 bandas, de cinco tipos: campana, low shelf, high shelf, high pass y low pass.
- Cada banda puede ir en Estéreo, L/R (un ajuste para cada canal) o M/S (Mid y Side).
- Se crean y se mueven sobre la gráfica: doble clic crea, arrastrar mueve, Mayús + arrastrar
  cambia Q o pendiente, Alt afina, rueda = Q/pendiente, clic derecho = menú, Supr = borrar.
- Dos motores, seleccionables:
  - **Exacto** (por defecto): las curvas se convierten en un filtro fijo de fase cero de N+1
    muestras y se aplican por convolución lineal exacta (bloques de N/2 sin ventana, FFT de 2N).
    Sin aliasing temporal ni huella de bloque: el residuo del HPF en graves baja de −70 a
    −155 dBFS.
  - **Clásico**: ventana Hann (periódica), FFT, ganancia en cada bin, FFT inversa y suma
    solapada (25, 50 o 75 %).
- Ventana N de 512, 1024, 4096 u 8192 muestras. Latencia N en los dos motores.

Formatos: VST3 (Windows, macOS, Linux) y AU (macOS). Efecto estéreo o mono.

## Diferencias con la web

- La web procesa un archivo entero; el plugin procesa en tiempo real con una latencia igual al
  tamaño de ventana (se declara al DAW). El resultado es idéntico al de la web (lo comprueba
  `tests/`), solo retrasado.
- No hay carga de archivo, reproducción ni exportación WAV (lo hace el DAW).
- Cada banda ocupa un "hueco" fijo (1 a 12) para que el DAW pueda automatizarla. Al borrar una
  banda, las demás no se renumeran como en la web; la siguiente nueva ocupa el hueco libre.
- Todos los ajustes son parámetros automatizables (12 bandas × 5 juegos).

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
