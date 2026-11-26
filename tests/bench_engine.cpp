// Mide el coste del motor en tiempo real (sin JUCE): segundos de CPU por segundo de audio.
//   g++ -std=c++17 -O3 -I Source tests/bench_engine.cpp -o bench && ./bench
// Escenarios: TDM (bandas en Estéreo y en M/S: 4 filtros) y solo bandas en Estéreo (2 filtros).
#include "dsp/EqEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

static eq::Params tdm (bool withMs)
{
    eq::Params p;
    p.n = 8192;
    auto set = [&] (int slot, int type, int edit, double fc, double gain, double q) {
        auto& b = p.bands[(size_t) slot];
        b.used = true; b.type = type; b.edit = edit;
        for (auto& s : b.sets) { s.fc = fc; s.gain = 0; s.q = q; }
        b.sets[(size_t) edit].gain = gain;
        b.sets[eq::St].gain = edit == eq::St ? gain : 0;
    };
    set (0, eq::Bell, eq::St, 1550, 1.0, 0.41);
    set (1, eq::HighShelf, eq::St, 30000, 1.7, 0.49);
    set (2, eq::Bell, eq::St, 214, -0.8, 4.28);
    if (withMs) { set (3, eq::HighShelf, eq::S, 388, 0.5, 2.0); set (4, eq::Bell, eq::M, 60, 1.0, 0.62); }
    return p;
}

int main()
{
    const double sr = 44100;
    const int block = 512, seconds = 60;
    for (int scenario = 0; scenario < 2; ++scenario)
    {
        eq::Engine e;
        e.setParams (tdm (scenario == 0));
        e.prepare (sr);
        e.setAsyncKernels (false);
        std::vector<float> l (block), r (block);
        float* ch[2] = { l.data(), r.data() };
        unsigned seed = 1;
        auto noise = [&] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 9) * (1.0 / 8388608.0) - 1.0) * 0.3f; };
        // Calentar (calcula el filtro)
        for (int i = 0; i < 20; ++i) { for (int j = 0; j < block; ++j) { l[(size_t) j] = noise(); r[(size_t) j] = noise(); } e.process (ch, 2, block); }
        const int blocks = (int) (seconds * sr / block);
        double sum = 0, t = 1e9;
        for (int rep = 0; rep < 5; ++rep)   // el mejor de 5 (la máquina compartida mete ruido)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int j = 0; j < block; ++j) { l[(size_t) j] = noise(); r[(size_t) j] = noise(); }
                e.process (ch, 2, block);
                sum += l[0];
            }
            t = std::min (t, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        std::printf ("%-28s %6.2f ms por segundo de audio (%.3f %% de un núcleo)  [%g]\n",
                     scenario == 0 ? "TDM (Estéreo + M/S)" : "Solo bandas en Estéreo", 1000 * t / seconds, 100 * t / seconds, sum);
    }
}
