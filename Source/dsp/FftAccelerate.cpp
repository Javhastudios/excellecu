// FFT de Accelerate (vDSP) para el motor en macOS. Va en su propio archivo para que la cabecera
// de Accelerate no se mezcle con JUCE (ver EqEngine.h). Fuera de macOS no compila nada.
#if defined(__APPLE__) && defined(EX_USE_ACCELERATE)
#include <Accelerate/Accelerate.h>

namespace eq::accel
{
void* create (int log2n) { return vDSP_create_fftsetupD ((vDSP_Length) log2n, kFFTRadix2); }

void destroy (void* setup) { vDSP_destroy_fftsetupD ((FFTSetupD) setup); }

void forward (void* setup, double* re, double* im, int log2n)
{
    DSPDoubleSplitComplex sc { re, im };   // transformada directa sin escalar, como la portable
    vDSP_fft_zipD ((FFTSetupD) setup, &sc, 1, (vDSP_Length) log2n, kFFTDirection_Forward);
}
}
#endif
