// Prueba de humo del plugin completo: latencia, motores Exacto y Clásico, bandas de cada tipo,
// modos L/R y M/S, límite de 12 bandas, estado guardado, mono y capturas de la interfaz.
//   ex_smoke <carpeta-salida>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/BandModel.h"

#include <iostream>

using namespace juce;

static int failures = 0;
// Los textos de las pruebas llevan tildes en literales normales: JUCE los lee byte a byte,
// así que aquí se vuelven a juntar los bytes para mostrarlos bien en la consola.
static void check (bool ok, const String& what)
{
    std::string bytes;
    for (auto c : what) bytes += c < 256 ? (char) c : '?';
    std::cout << (ok ? "  ok  " : "  FALLO ") << bytes << std::endl;
    if (! ok) ++failures;
}

// Procesa len muestras de gen(i, canal) en bloques de 480; devuelve los canales de salida.
// Sin tiempo real: los filtros del motor Exacto se calculan en el propio proceso, así el
// resultado no depende de lo rápido que vaya el hilo de filtros (eso se prueba aparte).
template <typename Gen>
static std::array<std::vector<float>, 2> run (ExcellecuProcessor& p, int len, Gen gen, int channels = 2)
{
    p.setNonRealtime (true);
    std::array<std::vector<float>, 2> out;
    for (auto& o : out) o.assign ((size_t) len, 0.0f);
    AudioBuffer<float> buf (channels, 480);
    MidiBuffer midi;
    for (int pos = 0; pos < len; pos += 480)
    {
        const int n = std::min (480, len - pos);
        buf.setSize (channels, n, false, false, true);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i) buf.setSample (c, i, gen (pos + i, c));
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i) out[(size_t) c][(size_t) (pos + i)] = buf.getSample (c, i);
    }
    return out;
}

static double rmsDb (const std::vector<float>& v, int from, int to, double ref)
{
    double s = 0;
    for (int i = from; i < to; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return 20 * std::log10 (std::sqrt (s / std::max (1, to - from)) / ref);
}

static bool finite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

static auto sine (double f, double amp = 0.25)
{
    return [f, amp] (int i, int) { return (float) (amp * std::sin (2 * eq::PI * f * i / 48000.0)); };
}
static const double sineRms = 0.25 / std::sqrt (2.0);

// Residuo tras quitar un seno de frecuencia f (ajuste por mínimos cuadrados), en dBFS
static double residualDbfs (const std::vector<float>& v, int from, int to, double f)
{
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (int i = from; i < to; ++i)
    {
        const double s = std::sin (2 * eq::PI * f * i / 48000.0), c = std::cos (2 * eq::PI * f * i / 48000.0);
        ss += s * s; sc += s * c; cc += c * c; ys += v[(size_t) i] * s; yc += v[(size_t) i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double e = 0;
    for (int i = from; i < to; ++i)
    {
        const double r = v[(size_t) i] - a * std::sin (2 * eq::PI * f * i / 48000.0) - b * std::cos (2 * eq::PI * f * i / 48000.0);
        e += r * r;
    }
    return 20 * std::log10 (std::sqrt (e / (to - from)) + 1e-30);
}

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  captura " << f.getFileName() << std::endl;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir = File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "smoke-out");
    outDir.createDirectory();
    const double SR = 48000;

    {
        std::cout << "Latencia y paso directo" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        check (p.getLatencySamples() == 2304, "latencia declarada 2304 con ventana 4096 en modo Exacto (N/2 + 256)");
        auto out = run (p, 20000, [] (int i, int) { return i == 1000 ? 1.0f : 0.0f; });
        int peakAt = 0;
        for (int i = 0; i < 20000; ++i) if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) peakAt])) peakAt = i;
        check (peakAt == 1000 + 2304, "impulso retrasado exactamente la latencia (" + String (peakAt - 1000) + ")");
        check (std::abs (out[0][(size_t) peakAt] - 1.0f) < 1e-5f, "sin bandas la salida es idéntica a la entrada");
    }

    {
        std::cout << "Motores Exacto y Clásico" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        check (p.currentParams().exact, "arranca en motor Exacto");
        for (int e = 0; e < 2; ++e)
        {
            const String name = e == 0 ? "Exacto" : "Clásico";
            m.set (ids::engine, (float) e);
            m.set (ids::winSize, 2.0f);
            auto out = run (p, 20000, [] (int i, int) { return i == 1000 ? 1.0f : 0.0f; });
            int peakAt = 0;
            for (int i = 0; i < 20000; ++i) if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) peakAt])) peakAt = i;
            const int want = e == 0 ? 2304 : 4096;
            check (peakAt == 1000 + want && std::abs (out[0][(size_t) peakAt] - 1.0f) < 1e-5f,
                   name + ": sin bandas, impulso intacto y retrasado " + String (want) + " (" + String (peakAt - 1000) + ")");
        }

        // Caso medido en Plugin Doctor: seno de 29,6 Hz a −6 dBFS con HPF 50 Hz 48 dB/oct, ventana 8192
        const int hp = m.create (eq::HighPass, 50.3, 0);
        m.set (ids::set (hp, eq::St, "slope"), 3.0f);
        m.set (ids::winSize, 3.0f);
        double res[2];
        for (int e = 0; e < 2; ++e)
        {
            m.set (ids::engine, (float) e);
            auto o = run (p, 120000, sine (29.6, 0.5));
            res[e] = residualDbfs (o[0], 50000, 110000, 29.6);
            check (finite (o[0]), String (e == 0 ? "Exacto" : "Clásico") + ": HPF en graves sin valores raros");
        }
        check (res[0] < -120, "Exacto: residuo del HPF 48 dB/oct en graves " + String (res[0], 1) + " dBFS (objetivo < −120)");
        std::cout << "  info  Clásico: residuo " << res[1] << " dBFS" << std::endl;
        m.set (ids::engine, 0.0f);
        run (p, 960, [] (int, int) { return 0.0f; });   // la latencia se actualiza al procesar
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (p.getLatencySamples() == 4352, "latencia 4352 con ventana 8192 en modo Exacto (" + String (p.getLatencySamples()) + ")");
        m.set (ids::engine, 1.0f);
        run (p, 960, [] (int, int) { return 0.0f; });
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (p.getLatencySamples() == 8192, "latencia 8192 con ventana 8192 en modo Clásico (" + String (p.getLatencySamples()) + ")");
        m.set (ids::engine, 0.0f);
    }

    {
        std::cout << "Tiempo real: filtros en otro hilo y fundido" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 128);
        p.setNonRealtime (false);
        ui::BandModel m (p);
        m.set (ids::winSize, 3.0f);
        const int bell = m.create (eq::Bell, 1000, 6);
        // Se procesa a velocidad real (bloques de 128) para que el hilo de filtros vaya a su ritmo
        const int blk = 128, len = 48000 * 2;
        AudioBuffer<float> buf (2, blk);
        MidiBuffer midi;
        std::vector<float> outL ((size_t) len);
        const auto start = Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < len; pos += blk)
        {
            if (pos == 48000) m.set (ids::set (bell, eq::St, "gain"), -6.0f);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blk; ++i) buf.setSample (c, i, (float) (0.25 * std::sin (2 * eq::PI * 1000 * (pos + i) / SR)));
            p.processBlock (buf, midi);
            for (int i = 0; i < blk; ++i) outL[(size_t) (pos + i)] = buf.getSample (0, i);
            const double due = start + 1000.0 * (pos + blk) / SR;
            while (Time::getMillisecondCounterHiRes() < due) Thread::sleep (1);
        }
        const double before = rmsDb (outL, 30000, 46000, sineRms), after = rmsDb (outL, 80000, 96000, sineRms);
        check (std::abs (before - 6) < 0.2 && std::abs (after + 6) < 0.2,
               "el cambio de ganancia llega desde el hilo de filtros (" + String (before, 2) + " / " + String (after, 2) + " dB)");
        double maxStep = 0;
        for (int i = 40000; i < len; ++i) maxStep = std::max (maxStep, (double) std::abs (outL[(size_t) i] - outL[(size_t) i - 1]));
        const double sineStep = 0.5 * 2 * eq::PI * 1000 / SR;   // pendiente máxima del seno a +6 dB
        check (maxStep <= sineStep * 1.01, "fundido sin saltos (paso máximo " + String (maxStep, 4) + ", el seno llega a " + String (sineStep, 4) + ")");
        p.releaseResources();
    }

    {
        std::cout << "Bandas" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);

        const int bell = m.create (eq::Bell, 1000, 6);
        check (bell == 0 && m.count() == 1, "crear campana en el primer hueco");
        auto o = run (p, 40000, sine (1000));
        check (std::abs (rmsDb (o[0], 20000, 40000, sineRms) - 6) < 0.2, "campana +6 dB a 1 kHz: " + String (rmsDb (o[0], 20000, 40000, sineRms), 2) + " dB");

        // L/R: L y R parten de Estéreo; se cambia solo R a 0 dB
        m.setMode (bell, eq::R);
        check (std::abs (m.band (bell).sets[eq::L].gain - 6) < 1e-6, "al pasar a L/R, L y R copian Estéreo");
        m.set (ids::set (bell, eq::R, "gain"), 0.0f);
        o = run (p, 40000, sine (1000));
        const double dl = rmsDb (o[0], 20000, 40000, sineRms), dr = rmsDb (o[1], 20000, 40000, sineRms);
        check (std::abs (dl - 6) < 0.2 && std::abs (dr) < 0.2, "modo L/R: L +6 dB, R 0 dB (" + String (dl, 2) + " / " + String (dr, 2) + ")");

        // M/S: con la banda en Mid, una señal en fase (L=R) sube y una en contrafase (L=-R) no
        m.setMode (bell, eq::M);
        m.set (ids::set (bell, eq::M, "gain"), 6.0f);
        o = run (p, 40000, sine (1000));
        const double mid = rmsDb (o[0], 20000, 40000, sineRms);
        o = run (p, 40000, [] (int i, int c) { return (float) ((c == 0 ? 0.25 : -0.25) * std::sin (2 * eq::PI * 1000 * i / 48000.0)); });
        const double side = rmsDb (o[0], 20000, 40000, sineRms);
        check (std::abs (mid - 6) < 0.2 && std::abs (side) < 0.2, "modo M/S: Mid +6 dB, Side 0 dB (" + String (mid, 2) + " / " + String (side, 2) + ")");
        m.remove (bell);
        check (m.count() == 0, "eliminar banda");

        // Filtros
        const int hp = m.create (eq::HighPass, 1000, 0);
        m.set (ids::set (hp, eq::St, "slope"), 2.0f); // 24 dB/oct
        o = run (p, 40000, sine (250));
        const double hpDb = rmsDb (o[0], 20000, 40000, sineRms);
        check (std::abs (hpDb + 48) < 1.0, "high pass 24 dB/oct a 1 kHz: 250 Hz baja " + String (hpDb, 1) + " dB");
        m.setType (hp, eq::LowPass);
        o = run (p, 40000, sine (4000));
        const double lpDb = rmsDb (o[0], 20000, 40000, sineRms);
        check (std::abs (lpDb + 48) < 1.0, "cambiado a low pass: 4 kHz baja " + String (lpDb, 1) + " dB");
        m.setType (hp, eq::HighShelf);
        check (m.band (hp).sets[eq::St].gain == 0 && m.band (hp).sets[eq::M].on, "de filtro a shelf: ganancia 0 y M/S encendidos");

        int created = 1;
        while (m.create (eq::Bell, 100.0 * (created + 1), 3) >= 0) ++created;
        check (created == 12 && m.create (eq::Bell, 500, 0) == -1, "máximo 12 bandas");

        // Ventanas y solapes en marcha con muchas bandas
        bool allOk = true;
        for (int w = 0; w < 4; ++w)
            for (int ov = 0; ov < 3; ++ov)
            {
                m.set (ids::winSize, (float) w);
                m.set (ids::overlap, (float) ov);
                o = run (p, 20000, [] (int i, int c) { return 0.2f * (float) std::sin (0.013 * i * (c + 1)) + (i % 97 == 0 ? 0.3f : 0.0f); });
                allOk = allOk && finite (o[0]) && finite (o[1]);
            }
        check (allOk, "las 12 combinaciones de ventana y overlap sin valores raros");
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (p.getLatencySamples() == 4352, "latencia actualizada a 4352 (ventana 8192, Exacto) (" + String (p.getLatencySamples()) + ")");

        MemoryBlock state;
        p.getStateInformation (state);
        ExcellecuProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        const auto a = p.currentParams(), b = q.currentParams();
        bool same = a.n == b.n && a.overlap == b.overlap && a.exact == b.exact;
        for (int s = 0; s < eq::MAX_BANDS; ++s)
        {
            const auto &x = a.bands[(size_t) s], &y = b.bands[(size_t) s];
            same = same && x.used == y.used && x.type == y.type && x.edit == y.edit;
            for (int k = 0; k < eq::NUM_KEYS; ++k)
            {
                const auto &u = x.sets[(size_t) k], &v = y.sets[(size_t) k];
                same = same && u.on == v.on && u.slope == v.slope && std::abs (u.fc - v.fc) < u.fc * 1e-5
                       && std::abs (u.gain - v.gain) < 1e-4 && std::abs (u.q - v.q) < 1e-4;
            }
        }
        check (same, "estado restaurado en otra instancia (con redondeo de guardado)");
    }

    {
        std::cout << "Mono" << std::endl;
        ExcellecuProcessor p;
        AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (AudioChannelSet::mono());
        mono.outputBuses.add (AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "acepta mono");
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        const int b = m.create (eq::Bell, 1000, -6);
        m.setMode (b, eq::M);
        m.set (ids::set (b, eq::M, "gain"), -6.0f);
        auto o = run (p, 40000, sine (1000), 1);
        check (std::abs (rmsDb (o[0], 20000, 40000, sineRms) + 6) < 0.2, "en mono se aplican las bandas Mid (como la web)");
    }

    {
        std::cout << "Interfaz" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1100, 780);
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        snapshot (*ed, outDir.getChildFile ("01_vacio.png"));

        m.create (eq::Bell, 1000, 6);
        m.set (ids::set (0, eq::St, "q"), 2.0f);
        m.create (eq::LowShelf, 120, -4);
        m.create (eq::HighShelf, 7000, 3);
        m.create (eq::HighPass, 40, 0);
        m.create (eq::LowPass, 15000, 0);
        p.selected = 0;
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        snapshot (*ed, outDir.getChildFile ("02_estereo.png"));

        m.setMode (0, eq::L);
        m.set (ids::set (0, eq::R, "gain"), -5.0f);
        m.set (ids::set (0, eq::R, "fc"), 2500.0f);
        m.setMode (2, eq::S);
        m.set (ids::set (2, eq::S, "gain"), 6.0f);
        p.selected = 2;
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        snapshot (*ed, outDir.getChildFile ("03_lr_ms.png"));

        p.selected = 3;
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        snapshot (*ed, outDir.getChildFile ("04_filtro.png"));
        ed.reset();
    }

    std::cout << (failures == 0 ? "TODO BIEN" : String (failures) + " fallo(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
