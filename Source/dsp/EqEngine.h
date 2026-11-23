// Motor del EQ: traducción del prototipo web v0.17 (web/eq-dft-proto.html) a C++ sin JUCE.
//
// La web procesa el archivo entero (ventana Hann, FFT, respuesta por bin, IFFT y suma
// solapada). Aquí se hace lo mismo en tiempo real: se guardan las últimas N muestras y cada
// "hop" muestras se procesa una ventana. Latencia = N muestras (se declara al DAW).
//
// Bandas: hasta 12, de cinco tipos, cada una con cinco juegos de ajustes (Estéreo, L, R,
// Mid, Side). El juego que se edita decide el modo de la banda: Estéreo (etapa L/R, igual en
// los dos canales), L/R (etapa L/R, un juego por canal) o M/S (etapa M/S). Copia literal de
// las funciones JS, en double y con el mismo orden de operaciones.
#pragma once

#include <algorithm>
#include <array>
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

inline void hannWindow (int n, double* w)
{
    for (int i = 0; i < n; ++i) w[i] = 0.5 * (1 - std::cos (2 * PI * i / (n - 1)));
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

// ── Motor en tiempo real ──────────────────────────────────────────────────
class Engine
{
public:
    Engine()
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
        freqs.assign (MAX_N / 2, 0.0);
        norm.assign (MAX_N, 0.0);
        applyLayout();
        rebuild();
    }

    void prepare (double sampleRate) { sr = sampleRate; rebuild(); reset(); }

    void reset()
    {
        for (auto& b : inBuf) std::fill (b.begin(), b.end(), 0.0);
        for (auto& b : outBuf) std::fill (b.begin(), b.end(), 0.0);
        idx = 0;
    }

    // Solo recalcula lo que cambia
    void setParams (const Params& np)
    {
        const bool layout = np.n != p.n || np.overlap != p.overlap;
        const bool bandsChanged = ! np.sameBands (p);
        p = np;
        if (layout) { applyLayout(); reset(); }
        if (layout || bandsChanged) rebuild();
    }

    const Params& params() const { return p; }
    int latency() const { return p.n; }
    int hopSize() const { return hop; }

    void process (float* const* ch, int numCh, int numSamples)
    {
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

private:
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
        for (int role = 0; role < 4; ++role) computeResponse (p, role, p.n, sr, resp[(size_t) role], freqs);
        useMS = p.anyGroup (GroupMS);
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
    std::vector<Fft> ffts;
    std::vector<std::vector<double>> windows;
    std::array<std::vector<double>, 2> inBuf, outBuf, re, im;
    std::array<Response, 4> resp;   // L, R, Mid, Side
    std::vector<double> freqs, norm;
};
} // namespace eq

#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif
