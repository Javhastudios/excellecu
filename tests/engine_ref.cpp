// Salida del motor en un caso exigente, para comparar dos versiones del motor muestra a muestra.
//   ./engine_ref salida.bin        (escribe)        ./engine_ref salida.bin ref.bin   (compara)
// Estéreo y mono, tamaños de filtro 512/1024/4096/8192, cambios de ajustes a mitad (fundidos),
// solo en una banda M/S, bloques de tamaño aleatorio de 1 a 1500 muestras.
#include "dsp/EqEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

static eq::Params params (int n, int variant)
{
    eq::Params p;
    p.n = n;
    auto set = [&] (int slot, int type, int edit, double fc, double gain, double q, int slope = 12) {
        auto& b = p.bands[(size_t) slot];
        b.used = true; b.type = type; b.edit = edit;
        for (auto& s : b.sets) { s.fc = fc; s.gain = 0; s.q = q; s.slope = slope; }
        b.sets[(size_t) edit].gain = gain;
    };
    set (0, eq::Bell, eq::St, 1550 + 300 * variant, 1.0 + variant, 0.41);
    set (1, eq::HighShelf, eq::St, 30000, 1.7, 0.49);
    set (2, eq::HighPass, eq::St, 40 + 20 * variant, 0, 1, 48);
    set (3, eq::HighShelf, eq::S, 388, 0.5 + variant, 2.0);
    set (4, eq::Bell, eq::M, 60, 1.0 - variant, 0.62);
    set (5, eq::Bell, eq::L, 3000, -3, 2);
    if (variant == 2) { p.soloSlot = 4; p.soloKey = eq::M; }
    return p;
}

int main (int argc, char** argv)
{
    std::vector<float> all;
    unsigned seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int n : { 512, 1024, 4096, 8192 })
        for (int nc : { 2, 1 })
        {
            eq::Engine e;
            e.setParams (params (n, 0));
            e.prepare (44100);
            e.setAsyncKernels (false);
            const int total = 44100 * 3;
            std::vector<float> buf[2] = { std::vector<float> (1500), std::vector<float> (1500) };
            int pos = 0, change = 0;
            while (pos < total)
            {
                const int len = std::min (1 + (int) (rnd() % 1500), total - pos);
                if (pos > 30000 && change == 0) { e.setParams (params (n, 1)); change = 1; }
                if (pos > 70000 && change == 1) { e.setParams (params (n, 2)); change = 2; }
                if (pos > 100000 && change == 2) { e.setParams (params (n, 0)); change = 3; }
                for (int i = 0; i < len; ++i)
                {
                    const double t = (pos + i) / 44100.0;
                    buf[0][(size_t) i] = (float) (0.3 * std::sin (2 * eq::PI * 55 * t) + 0.2 * ((rnd() % 20001) / 10000.0 - 1));
                    buf[1][(size_t) i] = (float) (0.3 * std::sin (2 * eq::PI * 4400 * t) + 0.2 * ((rnd() % 20001) / 10000.0 - 1));
                }
                float* ch[2] = { buf[0].data(), buf[1].data() };
                e.process (ch, nc, len);
                for (int i = 0; i < len; ++i) for (int c = 0; c < nc; ++c) all.push_back (buf[(size_t) c][(size_t) i]);
                pos += len;
            }
        }
    if (argc == 2)
    {
        FILE* f = std::fopen (argv[1], "wb");
        std::fwrite (all.data(), sizeof (float), all.size(), f);
        std::fclose (f);
        std::printf ("%zu muestras escritas\n", all.size());
        return 0;
    }
    FILE* f = std::fopen (argv[2], "rb");
    std::vector<float> ref (all.size());
    const size_t got = std::fread (ref.data(), sizeof (float), ref.size(), f);
    std::fclose (f);
    double worst = 0, peak = 0;
    for (size_t i = 0; i < all.size(); ++i) { worst = std::max (worst, (double) std::abs (all[i] - ref[i])); peak = std::max (peak, (double) std::abs (ref[i])); }
    std::printf ("%zu/%zu muestras · diferencia máxima %.3g (%.1f dB bajo el pico)\n", got, all.size(), worst, 20 * std::log10 (peak / std::max (worst, 1e-30)));
    return (got == all.size() && worst < 1e-6) ? 0 : 1;
}
