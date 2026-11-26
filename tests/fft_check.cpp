// Comprueba la FFT del motor contra una DFT directa (n ≤ 2048) y un tono puro (n mayores).
// Sale con error si alguna diferencia supera 1e-9.
#include "dsp/EqEngine.h"
#include <cstdio>
#include <random>
#include <cmath>
int main () {
  int fails = 0;
  std::mt19937 g (3); std::uniform_real_distribution<double> u (-1, 1);
  for (int n : { 2, 4, 8, 16, 512, 1024, 2048, 4096, 8192, 16384 }) {
    std::vector<double> r (n), i (n), R (n), I (n);
    for (int k = 0; k < n; ++k) { r[k] = u (g); i[k] = u (g); }
    // DFT directa (para n pequeños) o comparación de energía (Parseval) + sinusoide conocida
    eq::Fft f (n);
    R = r; I = i; f.forward (R.data(), I.data());
    double worst = 0;
    if (n <= 2048) {
      for (int k = 0; k < n; ++k) {
        long double sr = 0, si = 0;
        for (int j = 0; j < n; ++j) { long double a = -2.0L * 3.14159265358979323846264L * (long double) j * k / n; sr += r[j] * cosl (a) - i[j] * sinl (a); si += r[j] * sinl (a) + i[j] * cosl (a); }
        worst = std::max (worst, (double) std::max (fabsl (sr - R[k]), fabsl (si - I[k])));
      }
    } else {
      // tono puro en el bin 37: debe salir n en ese bin y ~0 en el resto
      for (int j = 0; j < n; ++j) { r[j] = std::cos (2 * eq::PI * 37 * j / n); i[j] = std::sin (2 * eq::PI * 37 * j / n); }
      R = r; I = i; f.forward (R.data(), I.data());
      for (int k = 0; k < n; ++k) worst = std::max (worst, std::hypot (R[k] - (k == 37 ? n : 0), I[k]));
    }
    std::printf ("n=%5d  error máx. %.2e\n", n, worst);
    if (! (worst < 1e-9)) ++fails;
  }
  return fails;
}
