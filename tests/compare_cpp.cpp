// Ejecuta el motor C++ (en tiempo real, en bloques de 256) con los guiones que genera
// run_js.mjs (motor Exacto de la web) y guarda la salida alineada con la de la web: la muestra
// q del original sale latency() muestras después.
//   g++ -std=c++17 -O2 -I Source -o compare_cpp tests/compare_cpp.cpp && ./compare_cpp <carpeta>
#include "dsp/EqEngine.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "tests/out";
    const double SR = 48000;

    std::ifstream fin (dir + "/in.f32", std::ios::binary);
    int frames = 0;
    fin.read ((char*) &frames, 4);
    std::vector<float> inL ((size_t) frames), inR ((size_t) frames);
    fin.read ((char*) inL.data(), frames * 4);
    fin.read ((char*) inR.data(), frames * 4);

    std::ifstream namesFile (dir + "/scenarios.json");
    std::string names ((std::istreambuf_iterator<char> (namesFile)), {});
    for (char& c : names) if (c == '[' || c == ']' || c == '"' || c == ',') c = ' ';
    std::istringstream nameStream (names);
    std::string name;
    while (nameStream >> name)
    {
        eq::Params p;
        int channels = 2;
        std::ifstream sc (dir + "/" + name + ".txt");
        std::string line;
        while (std::getline (sc, line))
        {
            std::istringstream ls (line);
            std::string key;
            ls >> key;
            if (key == "channels") ls >> channels;
            else if (key == "N") ls >> p.n;
            else if (key == "overlap" || key == "engine") {}   // solo los usa la web
            else if (key == "band")
            {
                int slot, type, edit;
                ls >> slot >> type >> edit;
                auto& b = p.bands[(size_t) slot];
                b.used = true; b.type = type; b.edit = edit;
            }
            else if (key == "shape")
            {
                int slot, shape, res, knee;
                ls >> slot >> shape >> res >> knee;
                auto& b = p.bands[(size_t) slot];
                b.shape = shape; b.res = res; b.knee = knee;
            }
            else if (key == "set")
            {
                int slot, k, on, slope;
                double fc, gain, q;
                ls >> slot >> k >> on >> fc >> gain >> q >> slope;
                auto& s = p.bands[(size_t) slot].sets[(size_t) k];
                s.on = on != 0; s.fc = fc; s.gain = gain; s.q = q; s.slope = slope;
            }
            else if (! key.empty()) { std::cerr << "clave desconocida " << key << "\n"; return 1; }
        }

        eq::Engine eng;
        eng.setParams (p);
        eng.prepare (SR);
        const int N = eng.latency(), hop = eng.hopSize();
        // Ceros delante para que las ventanas caigan en la misma rejilla que en la web
        const int D = ((-N % hop) + hop) % hop;
        const int total = D + frames + N;
        std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
        for (int i = 0; i < frames; ++i) { L[(size_t) (D + i)] = inL[(size_t) i]; R[(size_t) (D + i)] = channels == 2 ? inR[(size_t) i] : 0.0f; }

        for (int pos = 0; pos < total; pos += 256)
        {
            const int len = std::min (256, total - pos);
            float* ch[2] = { L.data() + pos, R.data() + pos };
            eng.process (ch, channels, len);
        }

        // La muestra q del original sale en D + q + N
        std::vector<float> out ((size_t) (frames * channels));
        for (int c = 0; c < channels; ++c)
            for (int q = 0; q < frames; ++q)
                out[(size_t) (c * frames + q)] = (c == 0 ? L : R)[(size_t) (D + q + N)];
        std::ofstream fo (dir + "/" + name + ".cpp.f32", std::ios::binary);
        fo.write ((const char*) out.data(), (std::streamsize) (out.size() * 4));
        std::cout << "C++ " << name << ": bloque " << hop << ", latencia " << N << "\n";
    }
    return 0;
}
