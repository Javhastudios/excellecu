// Analizador de espectro (v0.31; web v0.24, anaFft/anaToPixels/anaTick). Pre = la entrada
// (retrasada lo mismo que el motor, así va alineada con lo que suena), Post = lo que sale. Suma
// mono, FFT de 8192 con Hann terminando en la última muestra, cada píxel suma 1/12 de octava a su
// alrededor (un seno marca su nivel real y el ruido rosa sale plano), subida rápida y caída según
// la velocidad. Escala propia: 0 a −90 dBFS de arriba abajo, con inclinación alrededor de 1 kHz.
#pragma once

#include "../PluginProcessor.h"

#include <cmath>
#include <functional>
#include <vector>

namespace ui
{
class Analyzer
{
public:
    static constexpr int N = 8192;
    static constexpr double TOP = 0, RANGE = 90;
    static constexpr double SPEEDS[] = { 12, 30, 80 };           // caída en dB/s
    static constexpr const char* SPEED_NAMES[] = { "Lenta", "Media", "Rápida" };
    static constexpr double TILTS[] = { 0, 1.5, 3, 4.5 };         // dB/oct alrededor de 1 kHz

    Analyzer() : fft (N), win ((size_t) N), re ((size_t) N), im ((size_t) N), pow ((size_t) N / 2 + 1),
                 pre ((size_t) N), post ((size_t) N)
    {
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5 * (1 - std::cos (2 * eq::PI * i / N));
    }

    // Ajustes (se guardan con la sesión, en el estado del procesador)
    bool showPre = true, showPost = true;
    int speed = 1, tilt = 0;

    // Un fotograma: lee las últimas muestras, calcula y mezcla con lo anterior. fromX da la
    // frecuencia de cada píxel (width píxeles). Devuelve true si hay algo que dibujar.
    bool tick (const ExcellecuProcessor& proc, double dt, int width, const std::function<double (int)>& freqAtPixel)
    {
        if (! showPre && ! showPost) { preDb.clear(); postDb.clear(); active = false; return false; }
        proc.copyAnalyzer (pre.data(), post.data(), N);
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const double fall = SPEEDS[speed] * dt;
        bool visible = false;
        auto run = [&] (bool on, const std::vector<float>& x, std::vector<float>& db) {
            if (! on) { db.clear(); return; }
            spectrum (x);
            toPixels (sr, width, freqAtPixel, fresh);
            if ((int) db.size() != width) db.assign ((size_t) width, -200.0f);
            for (int i = 0; i < width; ++i)
            {
                const float v = fresh[(size_t) i];
                float& o = db[(size_t) i];
                o = v > o ? o + (v - o) * 0.6f : std::max (v, o - (float) fall);   // subida rápida, caída suave
                if (o > TOP - RANGE) visible = true;
            }
        };
        run (showPre, pre, preDb);
        run (showPost, post, postDb);
        active = visible;
        return visible;
    }

    std::vector<float> preDb, postDb;   // dB por píxel
    bool active = false;

private:
    void spectrum (const std::vector<float>& x)
    {
        for (int i = 0; i < N; ++i) { re[(size_t) i] = x[(size_t) i] * win[(size_t) i]; im[(size_t) i] = 0; }
        fft.forward (re.data(), im.data());
        const double norm = 4.0 / N;   // 2/Σventana (Σ Hann = N/2), en amplitud: un seno a 0 dBFS = 1
        for (int k = 0; k <= N / 2; ++k)
            pow[(size_t) k] = (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm * norm;
    }

    // Potencia por bin → dB por píxel: suma de 1/12 de octava dividida entre el ancho de banda
    // equivalente de la Hann (1,5 bins), que es también el ancho mínimo en graves
    void toPixels (double sr, int width, const std::function<double (int)>& freqAtPixel, std::vector<float>& out) const
    {
        constexpr double SMOOTH_OCT = 1.0 / 12, ENBW = 1.5;
        out.assign ((size_t) width, -200.0f);
        const double binHz = sr / N, half = std::pow (2.0, SMOOTH_OCT / 2), t = TILTS[tilt];
        const int kMax = N / 2;
        auto pAt = [&] (double k) {
            if (k <= 0) return pow[0];
            if (k >= kMax) return pow[(size_t) kMax];
            const int i = (int) std::floor (k);
            const double f = k - i;
            return pow[(size_t) i] * (1 - f) + pow[(size_t) i + 1] * f;
        };
        for (int x = 0; x < width; ++x)
        {
            const double fc = freqAtPixel (x);
            if (fc > sr / 2) continue;
            const double kc = fc / binHz;
            const double w = std::max (kc * (half - 1 / half), ENBW);
            const double a = kc - w / 2, b = kc + w / 2;
            double e = 0;
            if (w <= 6)
            {
                const int steps = 12;
                for (int j = 0; j < steps; ++j) e += pAt (a + (j + 0.5) * w / steps);
                e *= w / steps;
            }
            else
            {
                for (int k = (int) std::ceil (a); k <= std::min (kMax, (int) std::floor (b)); ++k) e += pow[(size_t) k];
            }
            out[(size_t) x] = (float) (10 * std::log10 (e / ENBW + 1e-20) + t * std::log2 (fc / 1000));
        }
    }

    eq::Fft fft;
    std::vector<double> win, re, im, pow;
    std::vector<float> pre, post, fresh;
};
}
