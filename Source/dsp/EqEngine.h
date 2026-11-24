// Motor del EQ: traducción del prototipo web v0.18 (web/eq-dft-proto.html) a C++ sin JUCE.
//
// Dos motores, como en la web:
//  · Clásico: ventana Hann (periódica), FFT, respuesta por bin, IFFT y suma solapada. Aquí en
//    tiempo real: se guardan las últimas N muestras y cada "hop" muestras se procesa una ventana.
//    Latencia N.
//  · Exacto: las mismas curvas como un filtro fijo de fase cero de N+1 muestras (idéntico al de
//    la web). En tiempo real se aplica por convolución particionada uniforme (trozos de 256
//    muestras): mismo resultado, coste repartido por igual y latencia N/2 + 256.
// La latencia se declara al DAW.
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
inline const std::array<double, 3> OV_OPTIONS { 0.25, 0.5, 0.75 };
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
    int n = 4096;
    double overlap = 0.5;
    bool exact = true;                // motor Exacto (true) o Clásico (false)
    bool sameBands (const Params& o) const { return bands == o.bands; }
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

// Ganancia total de un rol en una lista de frecuencias (responseAt de la web)
inline void responseAt (const Params& p, int role, const double* freqs, int count, double* g)
{
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
    explicit Fft (int size) : n (size), rev ((size_t) size), cosT ((size_t) size / 2), sinT ((size_t) size / 2)
    {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int k = 0; k < n / 2; ++k)
        {
            const double angle = -2 * PI * k / n;
            cosT[(size_t) k] = std::cos (angle);
            sinT[(size_t) k] = std::sin (angle);
        }
    }

    void forward (double* re, double* im) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int s = 0; s < n; s += len)
                for (int k = 0; k < half; ++k)
                {
                    const double wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                    double& oR = re[s + k + half]; double& oI = im[s + k + half];
                    const double tr = wr * oR - wi * oI;
                    const double ti = wr * oI + wi * oR;
                    const double eR = re[s + k], eI = im[s + k];
                    re[s + k] = eR + tr; im[s + k] = eI + ti;
                    oR = eR - tr; oI = eI - ti;
                }
        }
    }

private:
    int n;
    std::vector<int> rev;
    std::vector<double> cosT, sinT;
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
// tiempo real se aplica por convolución particionada uniforme: el filtro, ya causal (retrasado
// N/2), se parte en trozos de PART muestras y cada bloque de PART muestras de audio se
// convoluciona con todos ellos en el dominio de la frecuencia (overlap-save, FFT de 2·PART).
// El resultado es la misma convolución lineal que la web, con un coste repartido por igual en
// cada bloque y latencia N/2 + PART en lugar de N.
constexpr int PART = 256;                              // tamaño de partición (bloque de audio)
constexpr int PART_FFT = 2 * PART;                     // FFT de cada partición
constexpr int PART_BINS = PART + 1;                    // bins útiles (señal real)
constexpr int MAX_PARTS = (MAX_N + 1 + PART - 1) / PART;
constexpr int FADE_LEN = 2048;                         // fundido al cambiar de filtro (muestras)

inline int numParts (int n) { return (n + 1 + PART - 1) / PART; }
// Latencia que declara el plugin para unos ajustes
inline int latencyFor (int n, bool exact) { return exact ? n / 2 + PART : n; }

// Filtros de un juego de ajustes, ya partidos y en el dominio de la frecuencia.
// Estéreo: Lout = A·L + B·R, Rout = C·L + D·R (etapas L/R y M/S combinadas). Mono: H.
struct KernelBank
{
    enum { KA = 0, KB, KC, KD, KH, NUM = 5 };
    int n = 0, nc = 0, parts = 0;
    double sr = 0;
    Params params;
    std::array<std::vector<double>, NUM> re, im;       // parts × PART_BINS cada uno
    std::array<bool, NUM> null {};

    KernelBank()
    {
        for (auto& v : re) v.assign ((size_t) (MAX_PARTS * PART_BINS), 0.0);
        for (auto& v : im) v.assign ((size_t) (MAX_PARTS * PART_BINS), 0.0);
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
    KernelBuilder() : fftPart (PART_FFT)
    {
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) ffts2.emplace_back (2 * N_OPTIONS[(size_t) s]);
        for (auto& v : dense) v.assign (MAX_NF / 2 + 1, 1.0);
        freqs.assign (MAX_NF / 2 + 1, 0.0);
        mixG.assign (MAX_NF / 2 + 1, 0.0);
        tr.assign (MAX_NF, 0.0); ti.assign (MAX_NF, 0.0);
        hw.assign (MAX_NF, 0.0);
        pr.assign (PART_FFT, 0.0); pi.assign (PART_FFT, 0.0);
    }

    void build (const Params& p, double sr, int nc, KernelBank& out)
    {
        int sizeIdx = 0;
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) if (N_OPTIONS[(size_t) s] == p.n) sizeIdx = s;
        const int n = N_OPTIONS[(size_t) sizeIdx], nf = 2 * n, len = nf / 2 + 1;
        out.n = n; out.nc = nc; out.sr = sr; out.parts = numParts (n); out.params = p;

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
    // parte en trozos de PART muestras, cada uno con su FFT de 2·PART.
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

        const int parts = numParts (n);
        double* dr = out.re[(size_t) kid].data(); double* di = out.im[(size_t) kid].data();
        for (int q = 0; q < parts; ++q)
        {
            std::fill (pr.begin(), pr.end(), 0.0); std::fill (pi.begin(), pi.end(), 0.0);
            for (int j = 0; j < PART; ++j)
            {
                const int idx = q * PART + j;
                if (idx <= n) pr[(size_t) j] = h[idx];
            }
            fftPart.forward (pr.data(), pi.data());
            for (int b = 0; b < PART_BINS; ++b)
            {
                dr[q * PART_BINS + b] = pr[(size_t) b];
                di[q * PART_BINS + b] = pi[(size_t) b];
            }
        }
    }

    Fft fftPart;
    std::vector<Fft> ffts2;
    std::array<std::vector<double>, 4> dense;
    std::vector<double> freqs, mixG, tr, ti, hw, pr, pi;
};

// ── Motor en tiempo real ──────────────────────────────────────────────────
class Engine
{
public:
    static constexpr int NUM_SLOTS = 4;   // filtro actual, anterior (fundido), publicado y en cálculo

    Engine() : fftPart (PART_FFT)
    {
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s)
        {
            ffts.emplace_back (N_OPTIONS[(size_t) s]);
            windows.emplace_back ((size_t) N_OPTIONS[(size_t) s]);
            hannWindow (N_OPTIONS[(size_t) s], windows.back().data());
        }
        for (auto& b : inBuf) b.assign (MAX_N, 0.0);
        for (auto& b : outBuf) b.assign (MAX_N, 0.0);
        for (auto& r : resp) r.g.assign (MAX_N / 2, 1.0);
        for (auto& v : re) v.assign (MAX_N, 0.0);
        for (auto& v : im) v.assign (MAX_N, 0.0);
        freqs.assign (MAX_N / 2 + 1, 0.0);
        norm.assign (MAX_N, 0.0);
        // Motor exacto
        for (auto& v : fdlRe) v.assign ((size_t) (MAX_PARTS * PART_BINS), 0.0);
        for (auto& v : fdlIm) v.assign ((size_t) (MAX_PARTS * PART_BINS), 0.0);
        for (auto& v : frame) v.assign (PART_FFT, 0.0);
        for (auto& v : outBlk) v.assign (PART, 0.0);
        for (auto& v : yNew) v.assign (PART, 0.0);
        for (auto& v : yOld) v.assign (PART, 0.0);
        accRe.assign (PART_FFT, 0.0); accIm.assign (PART_FFT, 0.0);
        for (auto& s : slotState) s.store (SlotFree);
        applyLayout();
        rebuild();
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        srForBuilder.store (sampleRate);
        dropKernels();
        rebuild();
        reset();
    }

    void reset()
    {
        for (auto& b : inBuf) std::fill (b.begin(), b.end(), 0.0);
        for (auto& b : outBuf) std::fill (b.begin(), b.end(), 0.0);
        for (auto& v : fdlRe) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : fdlIm) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : frame) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : outBlk) std::fill (v.begin(), v.end(), 0.0);
        idx = 0; fdlHead = 0;
    }

    // Solo recalcula lo que cambia
    void setParams (const Params& np)
    {
        const bool layout = np.n != p.n || np.overlap != p.overlap || np.exact != p.exact;
        const bool bandsChanged = ! np.sameBands (p);
        p = np;
        if (layout) { applyLayout(); reset(); dropKernels(); }
        if (layout || bandsChanged) rebuild();
    }

    // true: los filtros nuevos los calcula otro hilo (buildPending) y aquí solo se recogen.
    // false: se calculan en el propio process() (pruebas, render sin tiempo real).
    void setAsyncKernels (bool a) { async = a; }

    const Params& params() const { return p; }
    int latency() const { return latencyFor (p.n, p.exact); }
    int hopSize() const { return p.exact ? PART : hop; }

    void process (float* const* ch, int numCh, int numSamples)
    {
        if (p.exact) { processExact (ch, numCh, numSamples); return; }
        const int n = p.n;
        const bool stereo = numCh >= 2;
        const int nc = stereo ? 2 : 1;
        for (int i = 0; i < numSamples; ++i)
        {
            const double nm = norm[(size_t) idx];
            for (int c = 0; c < nc; ++c)
            {
                inBuf[(size_t) c][(size_t) (n - hop + idx)] = ch[c][i];
                ch[c][i] = (float) (nm > 1e-6 ? outBuf[(size_t) c][(size_t) idx] / nm : 0.0);
            }
            if (++idx == hop)
            {
                idx = 0;
                processFrame (nc);
            }
        }
        for (int c = nc; c < numCh; ++c) std::memset (ch[c], 0, sizeof (float) * (size_t) numSamples);
    }

    // ── Cálculo de filtros desde otro hilo ───────────────────────────────────
    // Llamar periódicamente con los ajustes actuales (los mismos que recibe setParams). Si hace
    // falta un filtro nuevo, lo calcula y lo publica; el hilo de audio lo recoge en el siguiente
    // bloque y funde del filtro anterior al nuevo. Devuelve true si ha publicado algo.
    bool buildPending (const Params& want, KernelBuilder& builder)
    {
        if (! want.exact) return false;
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

    void applyLayout()
    {
        sizeIdx = 0;
        for (int s = 0; s < (int) N_OPTIONS.size(); ++s) if (N_OPTIONS[(size_t) s] == p.n) sizeIdx = s;
        p.n = N_OPTIONS[(size_t) sizeIdx];
        hop = std::max (1, (int) std::floor (p.n * (1 - p.overlap)));
        const double* w = windows[(size_t) sizeIdx].data();
        for (int r = 0; r < hop; ++r)
        {
            double s = 0;
            for (int j = r; j < p.n; j += hop) s += w[j] * w[j];
            norm[(size_t) r] = s;
        }
    }

    void rebuild()
    {
        if (! p.exact) for (int role = 0; role < 4; ++role) computeResponse (p, role, p.n, sr, resp[(size_t) role], freqs);
        useMS = p.anyGroup (GroupMS);
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

    // Al cambiar de tamaño de ventana, de motor o de frecuencia de muestreo: los filtros
    // anteriores ya no sirven (solo desde el hilo de audio).
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
        startFade (s);
    }

    void startFade (int s)
    {
        if (cur < 0) { cur = s; return; }                     // primer filtro: sin fundido
        releaseSlot (prev);
        prev = cur; cur = s; fadePos = 0;
    }

    // Cálculo en el propio hilo de audio
    void buildNow (int nc)
    {
        const int s = acquireSlot();
        if (s < 0) return;
        syncBuilder.build (p, sr, nc, banks[(size_t) s]);
        slotState[(size_t) s].store (SlotInUse);
        startFade (s);
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

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < nc; ++c)
            {
                frame[(size_t) c][(size_t) (PART + idx)] = ch[c][i];
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

    // Y = Σ_q K[q]·X[bloque − q] (+ el mismo con la segunda entrada), en bins 0 … PART
    void accumulate (const KernelBank& bank, int k1, int src1, int k2, int src2)
    {
        std::fill (accRe.begin(), accRe.begin() + PART_BINS, 0.0);
        std::fill (accIm.begin(), accIm.begin() + PART_BINS, 0.0);
        const int parts = bank.parts;
        const int kids[2] = { k1, k2 }, srcs[2] = { src1, src2 };
        for (int t = 0; t < 2; ++t)
        {
            const int kid = kids[t], src = srcs[t];
            if (kid < 0 || src < 0 || bank.null[(size_t) kid]) continue;
            const double* kr = bank.re[(size_t) kid].data();
            const double* ki = bank.im[(size_t) kid].data();
            const double* xr = fdlRe[(size_t) src].data();
            const double* xi = fdlIm[(size_t) src].data();
            for (int q = 0; q < parts; ++q)
            {
                int slot = fdlHead - q;
                if (slot < 0) slot += parts;
                const double* a = kr + q * PART_BINS; const double* b = ki + q * PART_BINS;
                const double* c = xr + slot * PART_BINS; const double* d = xi + slot * PART_BINS;
                for (int k = 0; k < PART_BINS; ++k)
                {
                    accRe[(size_t) k] += a[k] * c[k] - b[k] * d[k];
                    accIm[(size_t) k] += a[k] * d[k] + b[k] * c[k];
                }
            }
        }
    }

    // Espectro acumulado (bins 0 … PART) → últimas PART muestras de la IFFT (overlap-save)
    void synth (std::vector<double>& y)
    {
        for (int k = 1; k < PART; ++k) { accRe[(size_t) (PART_FFT - k)] = accRe[(size_t) k]; accIm[(size_t) (PART_FFT - k)] = -accIm[(size_t) k]; }
        for (int k = 0; k < PART_FFT; ++k) accIm[(size_t) k] = -accIm[(size_t) k];   // ifft = conj(fft(conj(Y))) / n
        fftPart.forward (accRe.data(), accIm.data());
        const double inv = 1.0 / PART_FFT;
        for (int j = 0; j < PART; ++j) y[(size_t) j] = accRe[(size_t) (PART + j)] * inv;
    }

    void outputFor (const KernelBank& bank, int nc, std::array<std::vector<double>, 2>& y)
    {
        if (nc == 2)
        {
            accumulate (bank, KernelBank::KA, 0, KernelBank::KB, 1); synth (y[0]);
            accumulate (bank, KernelBank::KC, 0, KernelBank::KD, 1); synth (y[1]);
        }
        else
        {
            accumulate (bank, KernelBank::KH, 0, -1, -1); synth (y[0]);
        }
    }

    void processPartBlock (int nc)
    {
        const KernelBank& bank = banks[(size_t) cur];
        const int parts = bank.parts;
        fdlHead = (fdlHead + 1) % parts;

        // Trama de 2·PART (bloque anterior + actual) → FFT → línea de retardo en frecuencia
        for (int c = 0; c < nc; ++c)
        {
            double* f = frame[(size_t) c].data();
            for (int j = 0; j < PART_FFT; ++j) { accRe[(size_t) j] = f[j]; accIm[(size_t) j] = 0.0; }
            fftPart.forward (accRe.data(), accIm.data());
            double* xr = fdlRe[(size_t) c].data() + fdlHead * PART_BINS;
            double* xi = fdlIm[(size_t) c].data() + fdlHead * PART_BINS;
            for (int k = 0; k < PART_BINS; ++k) { xr[k] = accRe[(size_t) k]; xi[k] = accIm[(size_t) k]; }
            std::memmove (f, f + PART, sizeof (double) * PART);   // el bloque actual pasa a ser el anterior
        }

        outputFor (bank, nc, yNew);
        if (fadePos < FADE_LEN && prev >= 0)
        {
            outputFor (banks[(size_t) prev], nc, yOld);
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
    }

    // processStereoBlock / processMonoBlock de la web
    void processFrame (int nc)
    {
        const int n = p.n;
        const double* win = windows[(size_t) sizeIdx].data();
        const auto& fft = ffts[(size_t) sizeIdx];
        for (int c = 0; c < nc; ++c)
        {
            double* xr = re[(size_t) c].data();
            double* xi = im[(size_t) c].data();
            for (int i = 0; i < n; ++i) { xr[i] = inBuf[(size_t) c][(size_t) i] * win[i]; xi[i] = 0.0; }
            fft.forward (xr, xi);
        }

        double *lr = re[0].data(), *li = im[0].data(), *rr = re[1].data(), *ri = im[1].data();
        if (nc == 2)
        {
            applyResponse (lr, li, n, resp[RoleL]);
            applyResponse (rr, ri, n, resp[RoleR]);
            if (useMS)
            {
                for (int k = 0; k < n; ++k)
                {
                    const double a = lr[k], b = li[k], c = rr[k], d = ri[k];
                    lr[k] = (a + c) * 0.5; li[k] = (b + d) * 0.5;
                    rr[k] = (a - c) * 0.5; ri[k] = (b - d) * 0.5;
                }
                applyResponse (lr, li, n, resp[RoleM]);
                applyResponse (rr, ri, n, resp[RoleS]);
                for (int k = 0; k < n; ++k)
                {
                    const double a = lr[k], b = li[k], c = rr[k], d = ri[k];
                    lr[k] = a + c; li[k] = b + d;
                    rr[k] = a - c; ri[k] = b - d;
                }
            }
        }
        else
        {
            applyResponse (lr, li, n, resp[RoleL]);
            applyResponse (lr, li, n, resp[RoleM]);
        }

        const double invN = 1.0 / n;
        for (int c = 0; c < nc; ++c)
        {
            double* xr = re[(size_t) c].data();
            double* xi = im[(size_t) c].data();
            for (int i = 0; i < n; ++i) xi[i] = -xi[i];          // ifft = conj(fft(conj(X))) / n
            fft.forward (xr, xi);
            auto& out = outBuf[(size_t) c];
            std::memmove (out.data(), out.data() + hop, sizeof (double) * (size_t) (n - hop));
            std::fill (out.begin() + (n - hop), out.begin() + n, 0.0);
            for (int i = 0; i < n; ++i) out[(size_t) i] += xr[i] * invN * win[i];
            auto& in = inBuf[(size_t) c];
            std::memmove (in.data(), in.data() + hop, sizeof (double) * (size_t) (n - hop));
        }
    }

    Params p;
    double sr = 48000;
    int sizeIdx = 2, hop = 2048, idx = 0;
    bool useMS = false;
    std::vector<Fft> ffts;                 // FFT de tamaño N (clásico)
    std::vector<std::vector<double>> windows;
    std::array<std::vector<double>, 2> inBuf, outBuf, re, im;
    std::array<Response, 4> resp;          // L, R, Mid, Side
    std::vector<double> freqs, norm;

    // Motor exacto
    Fft fftPart;
    KernelBuilder syncBuilder;
    std::array<KernelBank, NUM_SLOTS> banks;
    std::array<std::atomic<int>, NUM_SLOTS> slotState;
    std::atomic<int> latest { -1 };
    std::atomic<int> ncForBuilder { 0 };
    std::atomic<double> srForBuilder { 48000 };
    int cur = -1, prev = -1, fadePos = FADE_LEN, fdlHead = 0;
    bool async = false;
    std::array<std::vector<double>, 2> fdlRe, fdlIm, frame, outBlk, yNew, yOld;
    std::vector<double> accRe, accIm;
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
