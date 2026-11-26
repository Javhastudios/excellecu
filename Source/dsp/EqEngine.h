// Motor del EQ: traducción del prototipo web v0.18 (web/eq-dft-proto.html) a C++ sin JUCE.
//
// Motor Exacto de la web: las curvas se convierten en un filtro fijo de fase cero de N+1
// muestras (idéntico al de la web). En tiempo real se aplica por convolución particionada
// uniforme (trozos de 256 muestras): mismo resultado, coste repartido por igual y latencia
// N/2 + 256, que se declara al DAW. El plugin usa N = 8192. (El motor Clásico de la web, STFT
// con ventana Hann, se quitó del plugin en la v0.20: medía peor y en la prueba de nulo apenas
// se distinguía.)
//
// Bandas: hasta 12, de cinco tipos, cada una con cinco juegos de ajustes (Estéreo, L, R,
// Mid, Side). El juego que se edita decide el modo de la banda: Estéreo (etapa L/R, igual en
// los dos canales), L/R (etapa L/R, un juego por canal) o M/S (etapa M/S). Copia literal de
// las funciones JS, en double y con el mismo orden de operaciones.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

// En macOS el plugin usa la FFT de Accelerate (vDSP), mucho más rápida; las pruebas comparan las
// dos versiones muestra a muestra (paso de macOS en GitHub Actions). La cabecera de Accelerate
// no se incluye aquí (trae tipos del sistema, como Component, que chocan con JUCE): las llamadas
// están en FftAccelerate.cpp, que solo se compila en macOS.
#if defined(__APPLE__) && defined(EX_USE_ACCELERATE)
 #define EX_FFT_ACCELERATE 1
namespace eq::accel
{
void* create (int log2n);
void destroy (void* setup);
void forward (void* setup, double* re, double* im, int log2n);   // directa, sin escalar
}
#else
 #define EX_FFT_ACCELERATE 0
#endif
#include <vector>

#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wfloat-equal"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

namespace eq
{
constexpr double PI = 3.14159265358979323846;

inline const std::array<int, 4> N_OPTIONS { 512, 1024, 4096, 8192 };
inline const std::array<int, 4> SLOPES { 6, 12, 24, 48 };
constexpr int MAX_N = 8192;
constexpr int MAX_NF = 2 * MAX_N;   // FFT del motor exacto
constexpr int MAX_BANDS = 12;

enum Type { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, NUM_TYPES };
enum Key { St = 0, L, R, M, S, NUM_KEYS };           // juegos de ajustes
enum Group { GroupSt, GroupLR, GroupMS };
enum Role { RoleL = 0, RoleR, RoleM, RoleS };

inline bool typeHasGain (int t) { return t == Bell || t == LowShelf || t == HighShelf; }
inline bool typeHasQ (int t) { return typeHasGain (t); }
inline bool typeHasSlope (int t) { return t == HighPass || t == LowPass; }
inline Group modeGroup (int key) { return key == St ? GroupSt : (key == L || key == R) ? GroupLR : GroupMS; }

struct BandSet
{
    bool on = true;
    double fc = 1000, gain = 0, q = 1.0;
    int slope = 12;
    bool operator== (const BandSet& o) const { return on == o.on && fc == o.fc && gain == o.gain && q == o.q && slope == o.slope; }
    bool operator!= (const BandSet& o) const { return ! (*this == o); }
};

struct Band
{
    bool used = false;
    int type = Bell;
    int edit = St;                    // juego que se edita (y por su grupo, el modo)
    std::array<BandSet, NUM_KEYS> sets;
    bool operator== (const Band& o) const { return used == o.used && type == o.type && edit == o.edit && sets == o.sets; }
    bool operator!= (const Band& o) const { return ! (*this == o); }
};

struct Params
{
    std::array<Band, MAX_BANDS> bands;
    int n = 8192;                     // tamaño del filtro (N+1 muestras) y de su FFT de diseño (2N)
    // Escuchar banda (solo del plugin, no está en la web): hueco y juego que se oyen solos; -1 = no
    int soloSlot = -1, soloKey = St;
    bool sameBands (const Params& o) const { return bands == o.bands && soloSlot == o.soloSlot && soloKey == o.soloKey; }
    bool anyGroup (Group g) const
    {
        for (auto& b : bands) if (b.used && modeGroup (b.edit) == g) return true;
        return false;
    }
};

// ── Curvas (ganancia lineal en una frecuencia) ────────────────────────────
inline double highPassAt (double f, double fc, double slopeDb)
{
    if (f <= 0) return 0;
    if (f >= fc) return 1;
    const double octavesBelow = std::log2 (fc / f);
    const double db = -(octavesBelow * slopeDb);
    if (! std::isfinite (db) || db <= -120) return 0;
    const double g = std::pow (10.0, db / 20);
    return std::isfinite (g) && g >= 0 ? g : 0;
}

inline double lowPassAt (double f, double fc, double slopeDb)
{
    if (f <= fc) return 1;
    const double octavesAbove = std::log2 (f / fc);
    const double db = -(octavesAbove * slopeDb);
    if (! std::isfinite (db) || db <= -120) return 0;
    const double g = std::pow (10.0, db / 20);
    return std::isfinite (g) && g >= 0 ? g : 0;
}

inline double gaussianAt (double f, double fc, double gainDb, double Q)
{
    if (gainDb == 0 || f < 1) return 1;
    const double peak = std::pow (10.0, gainDb / 20);
    const double sigma = (1.0 / Q) * std::log10 (2.0) / 2.0;
    const double d = std::log10 (f) - std::log10 (fc);
    return 1.0 + (peak - 1.0) * std::exp (-(d * d) / (2 * sigma * sigma));
}

inline double lowShelfAt (double f, double fc, double gainDb, double Q)
{
    if (gainDb == 0 || f < 1) return 1;
    const double peak = std::pow (10.0, gainDb / 20);
    const double sigma = (1.0 / Q) * std::log10 (2.0) / 2.0;
    const double d = std::log10 (f) - std::log10 (fc);
    if (d <= 0) return std::max (0.0, peak);
    return std::max (0.0, 1.0 + (peak - 1.0) * std::exp (-(d * d) / (2 * sigma * sigma)));
}

inline double highShelfAt (double f, double fc, double gainDb, double Q)
{
    if (gainDb == 0 || f < 1) return 1;
    const double peak = std::pow (10.0, gainDb / 20);
    const double sigma = (1.0 / Q) * std::log10 (2.0) / 2.0;
    const double d = std::log10 (f) - std::log10 (fc);
    if (d >= 0) return std::max (0.0, peak);
    return std::max (0.0, 1.0 + (peak - 1.0) * std::exp (-(d * d) / (2 * sigma * sigma)));
}

inline double bandGainAt (int type, const BandSet& p, double f)
{
    switch (type)
    {
        case Bell:      return gaussianAt (f, p.fc, p.gain, p.q);
        case LowShelf:  return lowShelfAt (f, p.fc, p.gain, p.q);
        case HighShelf: return highShelfAt (f, p.fc, p.gain, p.q);
        case HighPass:  return highPassAt (f, p.fc, p.slope);
        case LowPass:   return lowPassAt (f, p.fc, p.slope);
        default: break;
    }
    return 1;
}

// ¿Tiene efecto un juego?
inline bool setIsLive (int type, const BandSet& p)
{
    if (! p.on) return false;
    return typeHasSlope (type) ? true : p.gain != 0;
}

// Juego que actúa en un rol (nullptr si la banda no actúa ahí)
inline const BandSet* activeSet (const Band& b, int role)
{
    const Group g = modeGroup (b.edit);
    const bool stage1 = role == RoleL || role == RoleR;
    if (g == GroupSt) return stage1 ? &b.sets[St] : nullptr;
    if (g == GroupLR) return stage1 ? &b.sets[role == RoleL ? L : R] : nullptr;
    return stage1 ? nullptr : &b.sets[role == RoleM ? M : S];
}

inline int clampKey (int k) { return k < St ? St : (k > S ? S : k); }

// Escuchar banda: la forma de lo que hace la banda, de 0 a 1, sin su ganancia. Campana → pasa
// banda gaussiano; shelf → su transición (1 en la zona realzada o atenuada); HPF / LPF → lo que
// recortan. Una banda a 0 dB o apagada también suena con su forma.
inline double soloShapeAt (int type, const BandSet& s, double f)
{
    if (f < 1) return type == HighPass ? 1.0 : 0.0;
    const double sigma = (1.0 / s.q) * std::log10 (2.0) / 2.0;
    const double d = std::log10 (f) - std::log10 (s.fc);
    const double gauss = std::exp (-(d * d) / (2 * sigma * sigma));
    switch (type)
    {
        case Bell:      return gauss;
        case LowShelf:  return d <= 0 ? 1.0 : gauss;
        case HighShelf: return d >= 0 ? 1.0 : gauss;
        case HighPass:  return 1.0 - highPassAt (f, s.fc, s.slope);
        case LowPass:   return 1.0 - lowPassAt (f, s.fc, s.slope);
        default:        return 1.0;
    }
}

// Rol en el que suena cada juego al escucharlo solo (el otro rol de su etapa queda en silencio)
inline bool soloRoles (int key, int role, bool& isSolo, bool& silent)
{
    const bool stage1 = role == RoleL || role == RoleR;
    isSolo = false; silent = false;
    switch (key)
    {
        case St: isSolo = stage1; break;
        case L:  isSolo = role == RoleL; silent = role == RoleR; break;
        case R:  isSolo = role == RoleR; silent = role == RoleL; break;
        case M:  isSolo = role == RoleM; silent = role == RoleS; break;
        case S:  isSolo = role == RoleS; silent = role == RoleM; break;
        default: break;
    }
    return isSolo || silent;
}

// Ganancia total de un rol en una lista de frecuencias (responseAt de la web)
inline void responseAt (const Params& p, int role, const double* freqs, int count, double* g)
{
    if (p.soloSlot >= 0 && p.soloSlot < MAX_BANDS && p.bands[(size_t) p.soloSlot].used)
    {
        // Solo: en lugar de la curva, la forma de esa banda en su canal; lo demás, plano o en silencio
        const auto& b = p.bands[(size_t) p.soloSlot];
        const int key = clampKey (p.soloKey);
        bool isSolo = false, silent = false;
        soloRoles (key, role, isSolo, silent);
        for (int i = 0; i < count; ++i)
            g[i] = isSolo ? soloShapeAt (b.type, b.sets[(size_t) key], freqs[i]) : (silent ? 0.0 : 1.0);
        return;
    }
    for (int i = 0; i < count; ++i) g[i] = 1.0;
    for (const auto& b : p.bands)
    {
        if (! b.used) continue;
        const BandSet* s = activeSet (b, role);
        if (s == nullptr || ! setIsLive (b.type, *s)) continue;
        for (int i = 0; i < count; ++i) g[i] *= bandGainAt (b.type, *s, freqs[i]);
    }
}

// Ventana Hann periódica (divide entre n, no n−1), como la web v0.18
inline void hannWindow (int n, double* w)
{
    for (int i = 0; i < n; ++i) w[i] = 0.5 * (1 - std::cos (2 * PI * i / n));
}

// ── FFT compleja radix-2 iterativa ────────────────────────────────────────
class Fft
{
public:
    // FFT en el sitio (sin escalar: X[k] = Σ x[j]·e^(−2πi·jk/n)), con la entrada en orden de bits
    // invertidos y etapas radix 4 (una radix 2 al principio si log2 n es impar): menos
    // multiplicaciones y la mitad de pasadas por memoria que todo radix 2. Las tablas de cada
    // etapa son contiguas para que el compilador vectorice el bucle interior.
    explicit Fft (int size) : n (size)
    {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        log2n = bits;
#if EX_FFT_ACCELERATE
        setup.reset (accel::create (bits));
        return;
#endif
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            if (r > i) { swapA.push_back (i); swapB.push_back (r); }
        }
        // Etapas radix 4: de L/4 a L. Si log2 n es impar, la primera es radix 2 (de 1 a 2).
        int len = (bits % 2 == 1) ? 8 : 4;
        for (; len <= n; len <<= 2)
        {
            stageLen.push_back (len);
            stageOffset.push_back ((int) w1r.size());
            const int q = len / 4;
            for (int k = 0; k < q; ++k)
            {
                const double a1 = -2 * PI * k / len;
                w1r.push_back (std::cos (a1));     w1i.push_back (std::sin (a1));
                w2r.push_back (std::cos (2 * a1)); w2i.push_back (std::sin (2 * a1));
                w3r.push_back (std::cos (3 * a1)); w3i.push_back (std::sin (3 * a1));
            }
        }
    }

    void forward (double* re, double* im) const
    {
#if EX_FFT_ACCELERATE
        accel::forward (setup.get(), re, im, log2n);
        return;
#endif
        for (size_t t = 0; t < swapA.size(); ++t)
        {
            const int i = swapA[t], j = swapB[t];
            std::swap (re[i], re[j]); std::swap (im[i], im[j]);
        }
        if (log2n % 2 == 1)
            for (int s = 0; s + 1 < n; s += 2)
            {
                const double eR = re[s], eI = im[s], oR = re[s + 1], oI = im[s + 1];
                re[s] = eR + oR; im[s] = eI + oI;
                re[s + 1] = eR - oR; im[s + 1] = eI - oI;
            }
        for (size_t st = 0; st < stageLen.size(); ++st)
        {
            const int len = stageLen[st], q = len / 4, off = stageOffset[st];
            const double* __restrict ar1 = w1r.data() + off; const double* __restrict ai1 = w1i.data() + off;
            const double* __restrict ar2 = w2r.data() + off; const double* __restrict ai2 = w2i.data() + off;
            const double* __restrict ar3 = w3r.data() + off; const double* __restrict ai3 = w3i.data() + off;
            for (int s = 0; s < n; s += len)
            {
                double* __restrict r0 = re + s;         double* __restrict i0 = im + s;
                double* __restrict r1 = re + s + q;     double* __restrict i1 = im + s + q;
                double* __restrict r2 = re + s + 2 * q; double* __restrict i2 = im + s + 2 * q;
                double* __restrict r3 = re + s + 3 * q; double* __restrict i3 = im + s + 3 * q;
                for (int k = 0; k < q; ++k)
                {
                    // Subtransformadas (en orden de bits invertidos): a = cuarto 0, b = cuarto 1,
                    // c = cuarto 2, d = cuarto 3. X = a + W^2k·b + W^k·c + W^3k·d, etc.
                    const double aR = r0[k], aI = i0[k];
                    const double bR = r1[k] * ar2[k] - i1[k] * ai2[k], bI = r1[k] * ai2[k] + i1[k] * ar2[k];
                    const double cR = r2[k] * ar1[k] - i2[k] * ai1[k], cI = r2[k] * ai1[k] + i2[k] * ar1[k];
                    const double dR = r3[k] * ar3[k] - i3[k] * ai3[k], dI = r3[k] * ai3[k] + i3[k] * ar3[k];
                    const double s0R = aR + bR, s0I = aI + bI, s1R = aR - bR, s1I = aI - bI;
                    const double t0R = cR + dR, t0I = cI + dI, t1R = cR - dR, t1I = cI - dI;
                    r0[k] = s0R + t0R; i0[k] = s0I + t0I;
                    r2[k] = s0R - t0R; i2[k] = s0I - t0I;
                    r1[k] = s1R + t1I; i1[k] = s1I - t1R;      // (a − W²b) − i·(W c − W³ d)
                    r3[k] = s1R - t1I; i3[k] = s1I + t1R;      // (a − W²b) + i·(W c − W³ d)
                }
            }
        }
    }

private:
    int n, log2n = 0;
    std::vector<int> swapA, swapB, stageLen, stageOffset;
    std::vector<double> w1r, w1i, w2r, w2i, w3r, w3i;
#if EX_FFT_ACCELERATE
    struct SetupDeleter { void operator() (void* p) const { if (p != nullptr) accel::destroy (p); } };
    std::unique_ptr<void, SetupDeleter> setup;
#endif
};

// Respuesta por bin de un rol (real; las curvas tienen fase cero)
struct Response
{
    std::vector<double> g;
    bool identity = true;
};

// Multiplica el espectro por H (applyResponse de la web). Los bins espejo usan conj(H)
// (aquí H es real, así que es el mismo valor). Nyquist: magnitud del último bin.
inline void applyResponse (double* re, double* im, int n, const Response& H)
{
    if (H.identity) return;
    const int half = n / 2;
    for (int k = 0; k < half; ++k)
    {
        const double hr = H.g[(size_t) k];
        re[k] *= hr; im[k] *= hr;
        if (k > 0) { re[n - k] *= hr; im[n - k] *= hr; }
    }
    const double hN = std::abs (H.g[(size_t) (half - 1)]);
    re[half] *= hN; im[half] *= hN;
}

inline void computeResponse (const Params& p, int role, int n, double sr, Response& out, std::vector<double>& freqs)
{
    const int h = n / 2;
    const double binHz = sr / n;
    for (int k = 0; k < h; ++k) freqs[(size_t) k] = k * binHz;
    responseAt (p, role, freqs.data(), h, out.g.data());
    out.identity = true;
    for (int k = 0; k < h; ++k) if (out.g[(size_t) k] != 1) { out.identity = false; break; }
}

// ── Motor exacto: filtros en particiones ──────────────────────────────────
// Las curvas se convierten en el mismo filtro de fase cero de N+1 muestras que en la web
// (kernelSpectrum: respuesta real en una FFT de 2N, recorte a ±N/2 con ventana coseno). En
// tiempo real el filtro, ya causal (retrasado N/2), se aplica por convolución particionada
// NO uniforme (overlap-save en el dominio de la frecuencia):
//   · nivel 0: trozos de PART = 256 muestras para el principio del filtro (taps 0 … 1023);
//   · nivel 1: trozos de 1024 para el resto (desde el tap 1024).
// Un trozo de tamaño B que empieza en el tap B solo necesita entrada ya completa, así que la cola
// se calcula una vez por cada 1024 muestras sin añadir latencia. Es la misma convolución lineal
// que la web (mismo resultado salvo redondeo, ~1e-14), con unas 2,5 veces menos operaciones que
// con todos los trozos de 256. Latencia: N/2 + PART, como antes.
constexpr int PART = 256;                              // tamaño de partición del nivel 0 (bloque de audio)
constexpr int PART_FFT = 2 * PART;
constexpr int PART_BINS = PART + 1;
constexpr int FADE_LEN = 2048;                         // fundido al cambiar de filtro (muestras)
constexpr int MAX_LEVELS = 2;
constexpr int LEVEL_SIZE[MAX_LEVELS] = { 256, 1024 };  // tamaño de trozo de cada nivel

// Cómo se reparte un filtro de n+1 taps entre los niveles
struct Layout
{
    int levels = 0;
    int size[MAX_LEVELS] {}, offset[MAX_LEVELS] {}, parts[MAX_LEVELS] {}, base[MAX_LEVELS] {};
    int total = 0;                                     // bins guardados por filtro (todos los niveles)
};

inline Layout layoutFor (int n)
{
    Layout L;
    const int taps = n + 1;
    int start = 0;
    for (int l = 0; l < MAX_LEVELS && start < taps; ++l)
    {
        const int b = LEVEL_SIZE[l];
        const int end = (l + 1 < MAX_LEVELS) ? std::min (taps, LEVEL_SIZE[l + 1]) : taps;   // el nivel siguiente empieza en su propio tamaño
        L.size[l] = b; L.offset[l] = start;
        L.parts[l] = (end - start + b - 1) / b;
        L.base[l] = L.total;
        L.total += L.parts[l] * (b + 1);
        L.levels = l + 1;
        start = end;
    }
    return L;
}

inline int numParts (int n) { return (n + 1 + PART - 1) / PART; }   // trozos si fueran todos de 256 (informativo)
// Latencia que declara el plugin para unos ajustes
inline int latencyFor (int n) { return n / 2 + PART; }

inline int maxBinsPerKernel() { return layoutFor (MAX_N).total; }

// Filtros de un juego de ajustes, ya partidos y en el dominio de la frecuencia.
// Estéreo: Lout = A·L + B·R, Rout = C·L + D·R (etapas L/R y M/S combinadas). Mono: H.
struct KernelBank
{
    enum { KA = 0, KB, KC, KD, KH, NUM = 5 };
    int n = 0, nc = 0;
    double sr = 0;
    Layout layout;
    Params params;
    std::array<std::vector<double>, NUM> re, im;       // layout.total bins cada uno
    std::array<bool, NUM> null {};

    KernelBank()
    {
        for (auto& v : re) v.assign ((size_t) maxBinsPerKernel(), 0.0);
        for (auto& v : im) v.assign ((size_t) maxBinsPerKernel(), 0.0);
        null.fill (true);
    }
    bool matches (const Params& p, int channels, double rate) const
    {
        return n == p.n && nc == channels && sr == rate && params.sameBands (p);
    }
    bool sameLayout (int size, int channels, double rate) const { return n == size && nc == channels && sr == rate; }
};

// Calcula un KernelBank. No reserva memoria al construir: se puede usar desde el hilo de audio
// (cálculo síncrono) o desde un hilo aparte. Cada instancia, en un solo hilo.
class KernelBuilder
{
public:
    KernelBuilder()
    {
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) ffts2.emplace_back (2 * N_OPTIONS[(size_t) s]);
        for (int l = 0; l < MAX_LEVELS; ++l) fftLevel.emplace_back (2 * LEVEL_SIZE[l]);
        for (auto& v : dense) v.assign (MAX_NF / 2 + 1, 1.0);
        freqs.assign (MAX_NF / 2 + 1, 0.0);
        mixG.assign (MAX_NF / 2 + 1, 0.0);
        tr.assign (MAX_NF, 0.0); ti.assign (MAX_NF, 0.0);
        hw.assign (MAX_NF, 0.0);
        pr.assign (2 * LEVEL_SIZE[MAX_LEVELS - 1], 0.0); pi.assign (2 * LEVEL_SIZE[MAX_LEVELS - 1], 0.0);
    }

    void build (const Params& p, double sr, int nc, KernelBank& out)
    {
        int sizeIdx = 0;
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) if (N_OPTIONS[(size_t) s] == p.n) sizeIdx = s;
        const int n = N_OPTIONS[(size_t) sizeIdx], nf = 2 * n, len = nf / 2 + 1;
        out.n = n; out.nc = nc; out.sr = sr; out.layout = layoutFor (n); out.params = p;

        for (int role = 0; role < 4; ++role)
        {
            const double binHz = sr / nf;
            for (int k = 0; k < len; ++k) freqs[(size_t) k] = k * binHz;
            responseAt (p, role, freqs.data(), len, dense[(size_t) role].data());
        }
        const double *gl = dense[RoleL].data(), *gr = dense[RoleR].data(), *gm = dense[RoleM].data(), *gs = dense[RoleS].data();
        double* G = mixG.data();
        out.null.fill (true);
        if (nc >= 2)
        {
            for (int k = 0; k < len; ++k) G[k] = (gm[k] + gs[k]) / 2 * gl[k];
            makeKernel (G, sizeIdx, out, KernelBank::KA);
            for (int k = 0; k < len; ++k) G[k] = (gm[k] - gs[k]) / 2 * gr[k];
            makeKernel (G, sizeIdx, out, KernelBank::KB);
            for (int k = 0; k < len; ++k) G[k] = (gm[k] - gs[k]) / 2 * gl[k];
            makeKernel (G, sizeIdx, out, KernelBank::KC);
            for (int k = 0; k < len; ++k) G[k] = (gm[k] + gs[k]) / 2 * gr[k];
            makeKernel (G, sizeIdx, out, KernelBank::KD);
        }
        else
        {
            for (int k = 0; k < len; ++k) G[k] = gl[k] * gm[k];   // mono: L en etapa 1, Mid en etapa 2
            makeKernel (G, sizeIdx, out, KernelBank::KH);
        }
    }

private:
    // kernelSpectrum de la web hasta el filtro recortado; luego se hace causal (retraso N/2) y se
    // reparte en los trozos de cada nivel, cada uno con su FFT de 2·tamaño.
    void makeKernel (const double* G, int sizeIdx, KernelBank& out, int kid)
    {
        const int n = N_OPTIONS[(size_t) sizeIdx], nf = 2 * n, half = n / 2;
        bool allZero = true;
        for (int k = 0; k <= nf / 2; ++k) if (std::abs (G[k]) > 1e-12) { allZero = false; break; }
        out.null[(size_t) kid] = allZero;
        if (allZero) return;

        const auto& fft = ffts2[(size_t) sizeIdx];
        double* r = tr.data(); double* i = ti.data();
        std::fill (r, r + nf, 0.0); std::fill (i, i + nf, 0.0);
        for (int k = 0; k <= nf / 2; ++k)
        {
            r[k] = G[k];
            if (k > 0 && k < nf / 2) r[nf - k] = G[k];       // simetría hermítica (real y par)
        }
        fft.forward (r, i);                                   // X real y par: la FFT da n·h
        const double inv = 1.0 / nf;
        // Filtro causal: h_c[j] = h[j − N/2], j = 0 … N
        double* h = hw.data();
        for (int j = 0; j <= n; ++j)
        {
            const int m = j - half;                           // −N/2 … N/2
            const double w = 0.5 * (1 + std::cos (PI * std::abs (m) / (half + 1)));
            h[j] = r[m >= 0 ? m : nf + m] * inv * w;
        }

        const Layout& L = out.layout;
        double* dr = out.re[(size_t) kid].data(); double* di = out.im[(size_t) kid].data();
        for (int l = 0; l < L.levels; ++l)
        {
            const int b = L.size[l], bins = b + 1;
            for (int q = 0; q < L.parts[l]; ++q)
            {
                std::fill (pr.begin(), pr.begin() + 2 * b, 0.0); std::fill (pi.begin(), pi.begin() + 2 * b, 0.0);
                for (int j = 0; j < b; ++j)
                {
                    const int idx = L.offset[l] + q * b + j;
                    if (idx <= n) pr[(size_t) j] = h[idx];
                }
                fftLevel[(size_t) l].forward (pr.data(), pi.data());
                double* kr = dr + L.base[l] + q * bins; double* ki = di + L.base[l] + q * bins;
                for (int k = 0; k < bins; ++k) { kr[k] = pr[(size_t) k]; ki[k] = pi[(size_t) k]; }
            }
        }
    }

    std::vector<Fft> ffts2, fftLevel;
    std::array<std::vector<double>, 4> dense;
    std::vector<double> freqs, mixG, tr, ti, hw, pr, pi;
};

// ── Motor en tiempo real ──────────────────────────────────────────────────
class Engine
{
public:
    static constexpr int NUM_SLOTS = 4;   // filtro actual, anterior (fundido), publicado y en cálculo

    Engine()
    {
        for (int l = 0; l < MAX_LEVELS; ++l)
        {
            auto& v = lv[(size_t) l];
            const int b = LEVEL_SIZE[l];
            v.size = b;
            v.fft = std::make_unique<Fft> (2 * b);
            const int maxParts = std::max (1, layoutFor (MAX_N).parts[l]);
            for (int c = 0; c < 2; ++c)
            {
                v.fdlRe[(size_t) c].assign ((size_t) (maxParts * (b + 1)), 0.0);
                v.fdlIm[(size_t) c].assign ((size_t) (maxParts * (b + 1)), 0.0);
                v.frame[(size_t) c].assign ((size_t) (2 * b), 0.0);
                v.tailCur[(size_t) c].assign ((size_t) b, 0.0);
                v.tailPrev[(size_t) c].assign ((size_t) b, 0.0);
            }
            v.workRe.assign ((size_t) (2 * b), 0.0); v.workIm.assign ((size_t) (2 * b), 0.0);
            v.y0Re.assign ((size_t) (b + 1), 0.0); v.y0Im.assign ((size_t) (b + 1), 0.0);
            v.y1Re.assign ((size_t) (b + 1), 0.0); v.y1Im.assign ((size_t) (b + 1), 0.0);
        }
        for (auto& v : outBlk) v.assign (PART, 0.0);
        for (auto& v : yNew) v.assign (PART, 0.0);
        for (auto& v : yOld) v.assign (PART, 0.0);
        for (auto& s : slotState) s.store (SlotFree);
        applyLayout();
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        srForBuilder.store (sampleRate);
        dropKernels();
        reset();
    }

    void reset()
    {
        for (auto& v : lv)
        {
            for (int c = 0; c < 2; ++c)
            {
                std::fill (v.fdlRe[(size_t) c].begin(), v.fdlRe[(size_t) c].end(), 0.0);
                std::fill (v.fdlIm[(size_t) c].begin(), v.fdlIm[(size_t) c].end(), 0.0);
                std::fill (v.frame[(size_t) c].begin(), v.frame[(size_t) c].end(), 0.0);
                std::fill (v.tailCur[(size_t) c].begin(), v.tailCur[(size_t) c].end(), 0.0);
                std::fill (v.tailPrev[(size_t) c].begin(), v.tailPrev[(size_t) c].end(), 0.0);
            }
            v.head = 0; v.fill = 0;
        }
        for (auto& v : outBlk) std::fill (v.begin(), v.end(), 0.0);
        idx = 0;
    }

    void setParams (const Params& np)
    {
        const bool layout = np.n != p.n;
        p = np;
        if (layout) { applyLayout(); reset(); dropKernels(); }
    }

    // true: los filtros nuevos los calcula otro hilo (buildPending) y aquí solo se recogen.
    // false: se calculan en el propio process() (pruebas, render sin tiempo real).
    void setAsyncKernels (bool a) { async = a; }

    const Params& params() const { return p; }
    int latency() const { return latencyFor (p.n); }
    int hopSize() const { return PART; }

    void process (float* const* ch, int numCh, int numSamples) { processExact (ch, numCh, numSamples); }

    // ── Cálculo de filtros desde otro hilo ───────────────────────────────────
    // Llamar periódicamente con los ajustes actuales (los mismos que recibe setParams). Si hace
    // falta un filtro nuevo, lo calcula y lo publica; el hilo de audio lo recoge en el siguiente
    // bloque y funde del filtro anterior al nuevo. Devuelve true si ha publicado algo.
    bool buildPending (const Params& want, KernelBuilder& builder)
    {
        const int nc = ncForBuilder.load();
        const double rate = srForBuilder.load();
        if (nc <= 0) return false;
        const int epoch = kernelEpoch.load();
        if (lastBuiltEpoch == epoch && lastBuilt.n == want.n && lastBuiltNc == nc && lastBuiltSr == rate && lastBuilt.sameBands (want))
            return false;
        const int s = acquireSlot();
        if (s < 0) return false;
        builder.build (want, rate, nc, banks[(size_t) s]);
        publish (s);
        lastBuilt = want; lastBuiltNc = nc; lastBuiltSr = rate; lastBuiltEpoch = epoch;
        return true;
    }

private:
    enum SlotState { SlotFree = 0, SlotWriting, SlotReady, SlotInUse };

    // Estado de un nivel de la convolución
    struct Level
    {
        int size = 0, head = 0, fill = 0;              // fill: muestras ya puestas en el bloque en curso
        std::unique_ptr<Fft> fft;
        std::array<std::vector<double>, 2> fdlRe, fdlIm, frame, tailCur, tailPrev;
        std::vector<double> workRe, workIm, y0Re, y0Im, y1Re, y1Im;
    };

    void applyLayout()
    {
        int sizeIdx = 0;
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) if (N_OPTIONS[(size_t) s] == p.n) sizeIdx = s;
        p.n = N_OPTIONS[(size_t) sizeIdx];
        layout = layoutFor (p.n);
    }

    // ── Ranuras de filtros (sin bloqueos) ─────────────────────────────────────
    // Cada ranura pasa por Libre → Calculando (un hilo la ha cogido) → Lista (publicada en
    // 'latest') → En uso (el audio la ha recogido) → Libre. 'latest' guarda la última lista
    // sin recoger; si llega otra antes, la anterior vuelve a quedar libre.
    int acquireSlot()
    {
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            int expected = SlotFree;
            if (slotState[(size_t) s].compare_exchange_strong (expected, SlotWriting)) return s;
        }
        return -1;
    }

    void publish (int s)
    {
        slotState[(size_t) s].store (SlotReady);
        const int old = latest.exchange (s);
        if (old >= 0) slotState[(size_t) old].store (SlotFree);
    }

    void releaseSlot (int s) { if (s >= 0) slotState[(size_t) s].store (SlotFree); }

    // Al cambiar de tamaño de ventana o de frecuencia de muestreo: los filtros anteriores ya no
    // sirven (solo desde el hilo de audio).
    void dropKernels()
    {
        releaseSlot (cur); cur = -1;
        releaseSlot (prev); prev = -1;
        fadePos = FADE_LEN;
        ++kernelEpoch;                                        // el otro hilo debe volver a calcular
    }

    // Recoge el filtro publicado más reciente, si encaja con la configuración actual
    void collectPublished (int nc)
    {
        if (fadePos < FADE_LEN) return;                       // primero termina el fundido en curso
        const int s = latest.exchange (-1);
        if (s < 0) return;
        slotState[(size_t) s].store (SlotInUse);
        if (! banks[(size_t) s].sameLayout (p.n, nc, sr)) { releaseSlot (s); return; }
        startFade (s, nc);
    }

    // Filtro nuevo. Las colas de los niveles 1… del bloque grande en curso se calculan ya con él
    // (la entrada que necesitan está en la línea de retardo); las del filtro anterior se guardan
    // para el fundido.
    void startFade (int s, int nc)
    {
        if (cur >= 0)
        {
            releaseSlot (prev);
            prev = cur; fadePos = 0;
            for (int l = 1; l < layout.levels; ++l) std::swap (lv[(size_t) l].tailCur, lv[(size_t) l].tailPrev);
        }
        cur = s;
        for (int l = 1; l < layout.levels; ++l) levelOutput (banks[(size_t) cur], l, nc, lv[(size_t) l].tailCur);
    }

    // Cálculo en el propio hilo de audio
    void buildNow (int nc)
    {
        const int s = acquireSlot();
        if (s < 0) return;
        syncBuilder.build (p, sr, nc, banks[(size_t) s]);
        slotState[(size_t) s].store (SlotInUse);
        startFade (s, nc);
    }

    void processExact (float* const* ch, int numCh, int numSamples)
    {
        const bool stereo = numCh >= 2;
        const int nc = stereo ? 2 : 1;
        ncForBuilder.store (nc);
        collectPublished (nc);
        const bool curOk = cur >= 0 && banks[(size_t) cur].sameLayout (p.n, nc, sr);
        if (! curOk)
        {
            if (cur >= 0) { dropKernels(); reset(); }
            buildNow (nc);                                     // sin filtro válido: no se puede esperar
        }
        else if (! async && ! banks[(size_t) cur].params.sameBands (p) && fadePos >= FADE_LEN)
            buildNow (nc);
        if (cur < 0)                                           // no debería pasar: sin filtro, silencio
        {
            for (int c = 0; c < numCh; ++c) std::memset (ch[c], 0, sizeof (float) * (size_t) numSamples);
            return;
        }

        auto& f0 = lv[0].frame;
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nc; ++c)
            {
                f0[(size_t) c][(size_t) (PART + idx)] = ch[c][i];
                ch[c][i] = (float) outBlk[(size_t) c][(size_t) idx];
            }
            if (++idx == PART)
            {
                idx = 0;
                processPartBlock (nc);
            }
        }
        for (int c = nc; c < numCh; ++c) std::memset (ch[c], 0, sizeof (float) * (size_t) numSamples);
    }

    // Trama de 2·B (bloque anterior + actual) de un nivel → FFT → su línea de retardo. En estéreo,
    // L y R van juntos en una sola FFT compleja (L real, R imaginaria) y luego se separan.
    void pushFrame (Level& v, int parts, int nc)
    {
        const int b = v.size, n2 = 2 * b, bins = b + 1;
        v.head = (v.head + 1) % parts;
        double* ar = v.workRe.data(); double* ai = v.workIm.data();
        const double* fl = v.frame[0].data();
        const double* fr = v.frame[1].data();
        if (nc == 2) { for (int j = 0; j < n2; ++j) { ar[j] = fl[j]; ai[j] = fr[j]; } }
        else         { for (int j = 0; j < n2; ++j) { ar[j] = fl[j]; ai[j] = 0.0; } }
        v.fft->forward (ar, ai);
        double* lr = v.fdlRe[0].data() + v.head * bins; double* li = v.fdlIm[0].data() + v.head * bins;
        if (nc == 2)
        {
            double* rr = v.fdlRe[1].data() + v.head * bins; double* ri = v.fdlIm[1].data() + v.head * bins;
            for (int k = 0; k < bins; ++k)
            {
                const int nk = (n2 - k) & (n2 - 1);
                const double a = ar[k], bb = ai[k], c = ar[nk], d = ai[nk];
                lr[k] = 0.5 * (a + c); li[k] = 0.5 * (bb - d);   // X_L = (Z[k] + conj Z[N−k]) / 2
                rr[k] = 0.5 * (bb + d); ri[k] = 0.5 * (c - a);   // X_R = (Z[k] − conj Z[N−k]) / 2i
            }
        }
        else
        {
            for (int k = 0; k < bins; ++k) { lr[k] = ar[k]; li[k] = ai[k]; }
        }
        for (int c = 0; c < nc; ++c) { double* f = v.frame[(size_t) c].data(); std::copy (f + b, f + n2, f); }
    }

    // Y += Σ_q K[q]·X[head − q] para un filtro (kid) sobre una entrada (src), en un nivel
    void mac (const KernelBank& bank, int l, int kid, int src, double* yr, double* yi)
    {
        if (bank.null[(size_t) kid]) return;
        const Level& v = lv[(size_t) l];
        const int bins = v.size + 1, parts = layout.parts[l];
        const double* kr0 = bank.re[(size_t) kid].data() + layout.base[l];
        const double* ki0 = bank.im[(size_t) kid].data() + layout.base[l];
        const double* xr0 = v.fdlRe[(size_t) src].data();
        const double* xi0 = v.fdlIm[(size_t) src].data();
        for (int q = 0; q < parts; ++q)
        {
            int slot = v.head - q;
            if (slot < 0) slot += parts;
            const double* a = kr0 + q * bins; const double* b = ki0 + q * bins;
            const double* c = xr0 + slot * bins; const double* d = xi0 + slot * bins;
            for (int k = 0; k < bins; ++k)
            {
                yr[k] += a[k] * c[k] - b[k] * d[k];
                yi[k] += a[k] * d[k] + b[k] * c[k];
            }
        }
    }

    // Salida de un nivel (B muestras por canal) con un filtro: en estéreo las dos salidas reales
    // salen de una sola IFFT (W = Y0 + i·Y1 → y0 + i·y1).
    void levelOutput (const KernelBank& bank, int l, int nc, std::array<std::vector<double>, 2>& y)
    {
        Level& v = lv[(size_t) l];
        const int b = v.size, n2 = 2 * b, bins = b + 1;
        std::fill (v.y0Re.begin(), v.y0Re.end(), 0.0); std::fill (v.y0Im.begin(), v.y0Im.end(), 0.0);
        std::fill (v.y1Re.begin(), v.y1Re.end(), 0.0); std::fill (v.y1Im.begin(), v.y1Im.end(), 0.0);
        double* wr = v.workRe.data(); double* wi = v.workIm.data();
        const double inv = 1.0 / n2;
        if (nc == 2)
        {
            mac (bank, l, KernelBank::KA, 0, v.y0Re.data(), v.y0Im.data());
            mac (bank, l, KernelBank::KB, 1, v.y0Re.data(), v.y0Im.data());
            mac (bank, l, KernelBank::KC, 0, v.y1Re.data(), v.y1Im.data());
            mac (bank, l, KernelBank::KD, 1, v.y1Re.data(), v.y1Im.data());
            for (int k = 0; k < bins; ++k)       // conj(W[k]), W[k] = Y0[k] + i·Y1[k]
            {
                wr[k] = v.y0Re[(size_t) k] - v.y1Im[(size_t) k];
                wi[k] = -(v.y0Im[(size_t) k] + v.y1Re[(size_t) k]);
            }
            for (int k = 1; k < b; ++k)          // conj(W[n−k]), W[n−k] = conj(Y0[k]) + i·conj(Y1[k])
            {
                wr[n2 - k] = v.y0Re[(size_t) k] + v.y1Im[(size_t) k];
                wi[n2 - k] = v.y0Im[(size_t) k] - v.y1Re[(size_t) k];
            }
            v.fft->forward (wr, wi);            // ifft(W) = conj(fft(conj W)) / n
            for (int j = 0; j < b; ++j) { y[0][(size_t) j] = wr[b + j] * inv; y[1][(size_t) j] = -wi[b + j] * inv; }
        }
        else
        {
            mac (bank, l, KernelBank::KH, 0, v.y0Re.data(), v.y0Im.data());
            for (int k = 0; k < bins; ++k) { wr[k] = v.y0Re[(size_t) k]; wi[k] = -v.y0Im[(size_t) k]; }
            for (int k = 1; k < b; ++k) { wr[n2 - k] = v.y0Re[(size_t) k]; wi[n2 - k] = v.y0Im[(size_t) k]; }
            v.fft->forward (wr, wi);
            for (int j = 0; j < b; ++j) y[0][(size_t) j] = wr[b + j] * inv;
        }
    }

    void processPartBlock (int nc)
    {
        // 1. El bloque de 256 recién llegado pasa a las tramas de los niveles mayores y a la FFT del nivel 0
        for (int l = 1; l < layout.levels; ++l)
        {
            Level& v = lv[(size_t) l];
            for (int c = 0; c < nc; ++c)
                std::memcpy (v.frame[(size_t) c].data() + v.size + v.fill, lv[0].frame[(size_t) c].data() + PART, sizeof (double) * PART);
            v.fill += PART;
        }
        pushFrame (lv[0], layout.parts[0], nc);

        // 2. Salida de este bloque: nivel 0 + el trozo que toca de las colas de los demás niveles
        auto blockFor = [&] (const KernelBank& bank, bool useCur, std::array<std::vector<double>, 2>& y) {
            levelOutput (bank, 0, nc, y);
            for (int l = 1; l < layout.levels; ++l)
            {
                const Level& v = lv[(size_t) l];
                const int at = v.fill - PART;
                const auto& t = useCur ? v.tailCur : v.tailPrev;
                for (int c = 0; c < nc; ++c)
                    for (int j = 0; j < PART; ++j) y[(size_t) c][(size_t) j] += t[(size_t) c][(size_t) (at + j)];
            }
        };
        const bool fading = fadePos < FADE_LEN && prev >= 0;
        blockFor (banks[(size_t) cur], true, yNew);
        if (fading)
        {
            blockFor (banks[(size_t) prev], false, yOld);
            for (int j = 0; j < PART; ++j)
            {
                const double w = std::min (1.0, (double) (fadePos + j + 1) / FADE_LEN);
                for (int c = 0; c < nc; ++c)
                    outBlk[(size_t) c][(size_t) j] = yOld[(size_t) c][(size_t) j] * (1 - w) + yNew[(size_t) c][(size_t) j] * w;
            }
            fadePos += PART;
            if (fadePos >= FADE_LEN) { releaseSlot (prev); prev = -1; }
        }
        else
        {
            for (int c = 0; c < nc; ++c) std::memcpy (outBlk[(size_t) c].data(), yNew[(size_t) c].data(), sizeof (double) * PART);
        }

        // 3. Niveles cuyo bloque grande se ha completado: FFT y cola del bloque grande siguiente
        for (int l = 1; l < layout.levels; ++l)
        {
            Level& v = lv[(size_t) l];
            if (v.fill < v.size) continue;
            v.fill = 0;
            pushFrame (v, layout.parts[l], nc);
            levelOutput (banks[(size_t) cur], l, nc, v.tailCur);
            if (prev >= 0) levelOutput (banks[(size_t) prev], l, nc, v.tailPrev);
        }
    }

    Params p;
    Layout layout;
    double sr = 48000;
    int idx = 0;
    KernelBuilder syncBuilder;
    std::array<KernelBank, NUM_SLOTS> banks;
    std::array<std::atomic<int>, NUM_SLOTS> slotState;
    std::atomic<int> latest { -1 };
    std::atomic<int> ncForBuilder { 0 };
    std::atomic<double> srForBuilder { 48000 };
    int cur = -1, prev = -1, fadePos = FADE_LEN;
    bool async = false;
    std::array<Level, MAX_LEVELS> lv;
    std::array<std::vector<double>, 2> outBlk, yNew, yOld;
    std::atomic<int> kernelEpoch { 0 };   // sube cada vez que los filtros anteriores dejan de valer
    // Solo los usa el hilo que llama a buildPending
    Params lastBuilt;
    int lastBuiltNc = 0, lastBuiltEpoch = -1;
    double lastBuiltSr = 0;
};
} // namespace eq

#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif
