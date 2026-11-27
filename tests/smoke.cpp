// Prueba de humo del plugin completo: latencia, HPF en graves, filtros en tiempo real, bandas de cada tipo,
// ganancia de salida L/R–M/S, medidores, deshacer, presets,
// modos L/R y M/S, límite de 12 bandas, estado guardado, mono y capturas de la interfaz.
//   ex_smoke <carpeta-salida>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/BandModel.h"
#include "ui/Meters.h"
#include "state/Presets.h"
#include "ui/Format.h"

#include <map>

#include <iostream>
#include <thread>

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

static void setPlain (ExcellecuProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    prm->endChangeGesture();
}
static float plain (ExcellecuProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

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
        check (p.getLatencySamples() == 4352, "latencia declarada 4352 (filtro de 8193 muestras: N/2 + 256)");
        auto out = run (p, 20000, [] (int i, int) { return i == 1000 ? 1.0f : 0.0f; });
        int peakAt = 0;
        for (int i = 0; i < 20000; ++i) if (std::abs (out[0][(size_t) i]) > std::abs (out[0][(size_t) peakAt])) peakAt = i;
        check (peakAt == 1000 + 4352, "impulso retrasado exactamente la latencia (" + String (peakAt - 1000) + ")");
        check (std::abs (out[0][(size_t) peakAt] - 1.0f) < 1e-5f, "sin bandas la salida es idéntica a la entrada");
        check (p.apvts.getParameter ("engine") == nullptr && p.apvts.getParameter ("winSize") == nullptr
                   && p.apvts.getParameter ("overlap") == nullptr,
               "sin parámetros de motor, ventana ni overlap (quitados en la v0.20)");
    }

    {
        std::cout << "HPF pronunciado en graves" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        // Caso medido en Plugin Doctor: seno de 29,6 Hz a −6 dBFS con HPF 50 Hz 48 dB/oct
        const int hp = m.create (eq::HighPass, 50.3, 0);
        m.set (ids::set (hp, eq::St, "slope"), 3.0f);
        auto o = run (p, 120000, sine (29.6, 0.5));
        const double res = residualDbfs (o[0], 50000, 110000, 29.6);
        check (finite (o[0]), "HPF en graves sin valores raros");
        check (res < -120, "residuo del HPF 48 dB/oct en graves " + String (res, 1) + " dBFS (objetivo < −120)");
    }

    {
        std::cout << "Tiempo real: filtros en otro hilo y fundido" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 128);
        p.setNonRealtime (false);
        ui::BandModel m (p);
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

        // Las 12 bandas en marcha
        o = run (p, 40000, [] (int i, int c) { return 0.2f * (float) std::sin (0.013 * i * (c + 1)) + (i % 97 == 0 ? 0.3f : 0.0f); });
        check (finite (o[0]) && finite (o[1]), "12 bandas sin valores raros");

        MemoryBlock state;
        p.getStateInformation (state);
        ExcellecuProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        const auto a = p.currentParams(), b = q.currentParams();
        bool same = a.n == b.n;
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
        std::cout << "Ganancia de salida" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        setPlain (p, "out_l", 6.0f);
        auto o = run (p, 40000, sine (1000));
        const double l = rmsDb (o[0], 20000, 40000, sineRms), r = rmsDb (o[1], 20000, 40000, sineRms);
        check (std::abs (l - 6) < 0.05 && std::abs (r) < 0.05, "L/R: L +6 dB, R 0 dB (" + String (l, 2) + " / " + String (r, 2) + ")");

        setPlain (p, "out_l", 0.0f);
        setPlain (p, "out_s", 6.0f);
        setPlain (p, "out_mode", 1.0f);
        auto mid = run (p, 40000, sine (1000));                                                       // L = R: solo Mid
        auto side = run (p, 40000, [] (int i, int c) { return (c == 0 ? 1.0f : -1.0f) * sine (1000) (i, c); }); // L = −R: solo Side
        const double dm = rmsDb (mid[0], 20000, 40000, sineRms), ds = rmsDb (side[0], 20000, 40000, sineRms);
        check (std::abs (dm) < 0.05 && std::abs (ds - 6) < 0.05, "M/S: Mid 0 dB, Side +6 dB (" + String (dm, 2) + " / " + String (ds, 2) + ")");

        // Cambiar el modo solo cambia qué pareja se edita: lo que suena es igual
        setPlain (p, "out_mode", 0.0f);
        auto side2 = run (p, 40000, [] (int i, int c) { return (c == 0 ? 1.0f : -1.0f) * sine (1000) (i, c); });
        check (std::abs (rmsDb (side2[0], 20000, 40000, sineRms) - 6) < 0.05, "volver a L/R no quita la ganancia de Side");

        // Suavizado: un salto de −24 dB a mitad de bloque no deja escalones
        setPlain (p, "out_s", 0.0f);
        p.setNonRealtime (false);
        AudioBuffer<float> buf (2, 480);
        MidiBuffer midi;
        float last = 0, maxStep = 0;
        const float sineStep = (float) (0.25 * 2 * eq::PI * 1000 / SR);
        for (int blk = 0; blk < 200; ++blk)
        {
            if (blk == 120) setPlain (p, "out_l", -24.0f);
            for (int i = 0; i < 480; ++i) for (int c = 0; c < 2; ++c) buf.setSample (c, i, sine (1000) (blk * 480 + i, c));
            p.processBlock (buf, midi);
            for (int i = 0; i < 480; ++i)
            {
                if (blk > 100) maxStep = std::max (maxStep, std::abs (buf.getSample (0, i) - last));
                last = buf.getSample (0, i);
            }
        }
        check (maxStep <= sineStep * 1.01f, "ganancia suavizada, sin saltos (paso máximo " + String (maxStep, 4) + ")");
        check (std::abs (rmsDb (std::vector<float> (buf.getReadPointer (0), buf.getReadPointer (0) + 480), 0, 480, sineRms) + 24) < 0.1,
               "tras el suavizado, L a −24 dB");

        // Mono: actúan L y Mid
        ExcellecuProcessor q;
        AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (AudioChannelSet::mono());
        mono.outputBuses.add (AudioChannelSet::mono());
        q.setBusesLayout (mono);
        q.prepareToPlay (SR, 480);
        setPlain (q, "out_l", -3.0f);
        setPlain (q, "out_m", -3.0f);
        setPlain (q, "out_s", 12.0f);
        auto om = run (q, 40000, sine (1000), 1);
        check (std::abs (rmsDb (om[0], 20000, 40000, sineRms) + 6) < 0.05, "mono: L −3 y Mid −3 dan −6 dB (Side no actúa)");
    }

    {
        std::cout << "Medidores" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        setPlain (p, "out_r", -6.0f);
        MeterView mv (p);   // al abrirse descarta lo que hubiera pendiente
        run (p, 48000, sine (1000, 0.5));
        mv.tick();
        const auto& ch = mv.channel (1, 1);
        check (std::abs (ch.peakDb + 12.04f) < 0.1f, "medidor de salida R: pico " + String (ch.peakDb, 2) + " dBFS");
        // RMS de todo lo recogido: incluye las 4352 muestras de silencio de la latencia
        const float wantRms = (float) (20 * std::log10 (0.25 / std::sqrt (2.0)) + 10 * std::log10 ((48000.0 - 4352) / 48000.0));
        check (std::abs (ch.rmsDb - wantRms) < 0.05f, "medidor de salida R: RMS " + String (ch.rmsDb, 2) + " dBFS (esperado " + String (wantRms, 2) + ")");
        // La cifra es la del canal más alto: L (0 dB), 6,02 dB por encima de R
        check (std::abs (mv.rmsReadout (1) - (wantRms + 6.0206f + 3.0103f)) < 0.05f, "cifra de RMS de la salida (AES-17, canal más alto): " + String (mv.rmsReadout (1), 2));
        mv.setAes17 (true);
        check (std::abs (mv.shownRms (ch) - (wantRms + 3.0103f)) < 0.05f, "RMS AES-17 por defecto: +3,01 dB (" + String (mv.shownRms (ch), 2) + ")");
        mv.setAes17 (false);
        check (std::abs (mv.shownRms (ch) - wantRms) < 0.05f && ! (bool) p.apvts.state.getProperty ("rmsAes17"), "RMS sin ponderar (se guarda en el estado)");
        check (std::abs (mv.channel (0, 0).peakDb + 6.02f) < 0.1f && std::abs (mv.channel (1, 0).peakDb + 6.02f) < 0.1f,
               "pico de entrada y salida L 0,5 (−6 dBFS)");
        check (p.inMeter[0].fifo.getNumReady() == 0, "el medidor recoge todo lo pendiente");

        // RMS con la interfaz recogiendo a la vez que el audio (antes podía contar un bloque dos veces)
        {
            ExcellecuProcessor q;
            q.prepareToPlay (SR, 64);
            q.setNonRealtime (true);
            MeterView mq (q);
            std::atomic<bool> stop { false };
            std::thread audio ([&] {
                AudioBuffer<float> b (2, 64);
                MidiBuffer mid;
                for (int blk = 0; ! stop; ++blk)
                {
                    for (int i = 0; i < 64; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, sine (1000) (blk * 64 + i, c));
                    q.processBlock (b, mid);
                }
            });
            Thread::sleep (300);   // pasa la latencia: ya sale seno
            float worst = 0;
            for (int t = 0; t < 300; ++t)
            {
                mq.tick();
                if (t > 20) worst = std::max (worst, std::abs (mq.channel (0, 0).rmsDb - (float) (20 * std::log10 (sineRms))));
                Thread::sleep (1);
            }
            stop = true;
            audio.join();
            check (worst < 0.05f, "RMS estable recogiendo a la vez que el audio (desvío máx. " + String (worst, 3) + " dB)");
        }
        run (p, 4800, sine (1000, 1.2));
        mv.tick();
        check (mv.channel (0, 0).clip && mv.channel (1, 0).clip, "saturación encendida con picos por encima de 0 dBFS");
        {
            ExcellecuProcessor fresh;
            MeterView mf (fresh);
            check (mf.aes17(), "una instancia nueva arranca en AES-17");
        }
    }

    {
        std::cout << "Deshacer y rehacer" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        auto& h = p.history;
        const int b = m.create (eq::Bell, 1000, 6);
        h.flush();
        check (h.undoCount() == 1, "crear una banda es un solo paso (toca ~30 parámetros)");

        // Arrastre: muchos movimientos dentro de un gesto = un paso
        const auto fcId = ids::set (b, eq::St, "fc");
        m.begin (fcId);
        for (int i = 1; i <= 20; ++i) m.setLive (fcId, 1000.0f + 50.0f * i);
        m.end (fcId);
        h.flush();
        check (h.undoCount() == 2 && std::abs (plain (p, fcId) - 2000) < 1, "un arrastre entero es un solo paso");

        // Automatización (sin gesto): no entra en el historial
        auto* g = p.apvts.getParameter (ids::set (b, eq::St, "gain"));
        g->setValueNotifyingHost (g->convertTo0to1 (3.0f));
        h.flush();
        check (h.undoCount() == 2, "la automatización del DAW no se apunta");

        // Rueda: cambios seguidos del mismo control se juntan
        m.set (ids::set (b, eq::St, "q"), 1.2f);
        h.flush();
        m.set (ids::set (b, eq::St, "q"), 1.4f);
        h.flush();
        check (h.undoCount() == 3, "dos toques seguidos de la misma Q se juntan en un paso");

        h.undo();
        check (std::abs (plain (p, ids::set (b, eq::St, "q")) - 1) < 1e-4, "deshacer la Q vuelve a 1,00");
        check (std::abs (plain (p, ids::set (b, eq::St, "gain")) - 3) < 1e-4, "deshacer no pisa lo que movió la automatización");
        h.undo();
        check (std::abs (plain (p, fcId) - 1000) < 1, "deshacer el arrastre vuelve a 1 kHz");
        p.selected = -1;
        h.undo();
        check (! m.used (b) && m.count() == 0, "deshacer la creación quita la banda");
        check (! h.undo(), "no hay más pasos que deshacer");
        h.redo();
        check (m.used (b) && std::abs (m.band (b).sets[eq::St].gain - 6) < 1e-4 && p.selected == b,
               "rehacer devuelve la banda (+6 dB) y la selecciona");
        h.redo();
        h.redo();
        check (std::abs (plain (p, ids::set (b, eq::St, "q")) - 1.4) < 1e-4, "rehacer hasta el final");

        m.remove (b);
        h.flush();
        h.undo();
        check (m.used (b) && p.selected == b, "deshacer un borrado devuelve la banda seleccionada");
        m.setMode (b, eq::L);
        m.set (ids::set (b, eq::L, "gain"), -2.0f);
        m.markEdited (b);
        h.flush();
        check (! p.isLrFresh (b), "editar L marca la banda como editada (lrFresh)");
        h.undo();
        check (p.isLrFresh (b), "deshacer devuelve también lrFresh");
        check (h.canRedo(), "tras deshacer se puede rehacer");
        m.set (ids::set (b, eq::St, "gain"), 1.0f);
        h.flush();
        check (! h.canRedo(), "un cambio nuevo borra lo que había para rehacer");

        for (int i = 0; i < 130; ++i) { m.set (ids::set (b, eq::St, "fc"), 100.0f + i); h.flush(); Thread::sleep (i < 2 ? 450 : 0); }
        check (h.undoCount() <= UndoHistory::MAX_STEPS, "como mucho " + String (UndoHistory::MAX_STEPS) + " pasos (" + String (h.undoCount()) + ")");

        MemoryBlock st;
        p.getStateInformation (st);
        p.setStateInformation (st.getData(), (int) st.getSize());
        check (! h.canUndo() && ! h.canRedo(), "cargar una sesión vacía el historial");
    }

    {
        std::cout << "Presets" << std::endl;
        const File tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("excellecu-smoke.excellecu");
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        const int a = m.create (eq::Bell, 64, 5);
        m.set (ids::set (a, eq::St, "q"), 0.7f);
        const int c = m.create (eq::HighShelf, 9000, 2);
        m.setMode (c, eq::S);
        m.set (ids::set (c, eq::S, "gain"), 1.5f);
        m.create (eq::HighPass, 30, 0);
        setPlain (p, "out_m", -1.5f);
        check (presets::save (p, tmp, "Prueba"), "guardar preset en archivo");
        auto xml = parseXML (tmp);
        check (xml != nullptr && xml->getIntAttribute ("version") == presets::FORMAT_VERSION, "el archivo lleva número de versión");
        check (xml != nullptr && xml->getChildByAttribute ("id", "b5_used") == nullptr, "no guarda las bandas vacías");

        ExcellecuProcessor q;
        q.prepareToPlay (SR, 480);
        ui::BandModel mq (q);
        mq.create (eq::LowPass, 5000, 0);          // algo distinto que el preset debe quitar
        setPlain (q, "out_l", 3.0f);
        q.history.flush();
        const int stepsBefore = q.history.undoCount();
        std::vector<float> vals;
        int fresh = 0;
        check (presets::decode (q, *xml, vals, fresh), "leer el preset");
        q.applyPreset (vals, fresh, "Prueba");
        q.history.flush();
        bool same = true;
        for (auto* prm : p.getParameters())
        {
            const auto id = dynamic_cast<RangedAudioParameter*> (prm)->getParameterID();
            if (presets::isBandParam (id) && plain (p, ids::band (presets::bandOf (id), "used")) < 0.5f) continue;
            same = same && std::abs (plain (p, id) - plain (q, id)) < 1e-3f * std::max (1.0f, std::abs (plain (p, id)));
        }
        check (same, "el preset carga igual en otra instancia (bandas, M/S y salida)");
        check (plain (q, "out_l") == 0.0f, "lo que no está en el preset vuelve a su valor por defecto");
        check (q.presetName() == "Prueba" && ! q.presetModified(), "nombre del preset y sin modificar");
        check (q.history.undoCount() == stepsBefore + 1, "cargar un preset es un solo paso de deshacer");
        auto o1 = run (p, 30000, sine (64)), o2 = run (q, 30000, sine (64));
        check (std::abs (rmsDb (o1[0], 15000, 30000, 1) - rmsDb (o2[0], 15000, 30000, 1)) < 0.01, "suena igual que el original");

        setPlain (q, "out_s", 1.0f);
        check (q.presetModified(), "al cambiar algo aparece como modificado");
        q.history.flush();
        q.history.undo();
        check (! q.presetModified(), "deshacer el cambio vuelve a «sin modificar»");
        q.history.undo();
        check (plain (q, "out_l") == 3.0f && mq.count() == 1, "deshacer la carga del preset recupera lo anterior");

        // Preset antiguo sin la sección de salida: carga y la salida queda a 0 dB
        xml->deleteAllChildElementsWithTagName ("P");
        auto* e = xml->createNewChildElement ("P"); e->setAttribute ("id", "b1_used"); e->setAttribute ("v", 1);
        auto* e2 = xml->createNewChildElement ("P"); e2->setAttribute ("id", "b1_st_gain"); e2->setAttribute ("v", 4);
        auto* e3 = xml->createNewChildElement ("P"); e3->setAttribute ("id", "parametro_futuro"); e3->setAttribute ("v", 9);
        presets::decode (q, *xml, vals, fresh);
        q.applyPreset (vals, fresh, "Viejo");
        check (mq.count() == 1 && std::abs (mq.band (0).sets[eq::St].gain - 4) < 1e-4 && plain (q, "out_m") == 0.0f,
               "preset incompleto o con parámetros desconocidos: carga lo que entiende");

        // De fábrica: "Plano" deja el EQ vacío
        auto flat = presets::factoryXml (presets::factoryList()[0]);
        presets::decode (q, *flat, vals, fresh);
        q.applyPreset (vals, fresh, "Plano");
        check (mq.count() == 0 && plain (q, "out_m") == 0.0f, "preset de fábrica «Plano»: sin bandas, salida a 0 dB");

        // Fábrica con una banda: igual que crearla a mano
        presets::Factory f { "Test", { { eq::Bell, eq::St, { { eq::St, 200, -3, 2 } } } } };
        auto fx = presets::factoryXml (f);
        presets::decode (q, *fx, vals, fresh);
        q.applyPreset (vals, fresh, "Test");
        ExcellecuProcessor r;
        ui::BandModel mr (r);
        mr.create (eq::Bell, 200, -3);
        mr.set (ids::set (0, eq::St, "q"), 2.0f);
        check (q.currentParams().sameBands (r.currentParams()) && q.lrFreshMask() == r.lrFreshMask(),
               "un preset de fábrica da lo mismo que crear las bandas a mano");

        // El nombre del preset se guarda con la sesión
        MemoryBlock st;
        q.getStateInformation (st);
        ExcellecuProcessor s2;
        s2.setStateInformation (st.getData(), (int) st.getSize());
        check (s2.presetName() == "Test" && ! s2.presetModified(), "el nombre del preset se guarda con la sesión");

        // TDM: cinco bandas (dos en M/S) y la curva en los puntos clave
        const auto& fl = presets::factoryList();
        check (fl.size() >= 2 && String (fl[1].name) == "TDM", "preset de fábrica TDM");
        auto tdm = presets::factoryXml (fl[1]);
        tdm->writeTo (outDir.getChildFile ("TDM.excellecu"));   // para comprobar que la web lo abre igual
        presets::decode (q, *tdm, vals, fresh);
        q.applyPreset (vals, fresh, "TDM");
        const auto tp = q.currentParams();
        check (mq.count() == 5 && tp.bands[2].edit == eq::S && tp.bands[3].edit == eq::M, "TDM: 5 bandas, la 3 en Side y la 4 en Mid");
        auto ot = run (q, 48000, sine (60));
        check (std::abs (rmsDb (ot[0], 20000, 48000, sineRms) - 1.0) < 0.3, "TDM a 60 Hz ≈ +1 dB (" + String (rmsDb (ot[0], 20000, 48000, sineRms), 2) + ")");
        tmp.deleteFile();
    }


    {
        std::cout << "Frecuencia hasta 30 kHz" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        const int b = m.create (eq::HighShelf, 30000, 6);
        check (std::abs (m.band (b).sets[eq::St].fc - 30000) < 1, "un punto se puede crear a 30 kHz");
        m.set (ids::set (b, eq::St, "q"), 0.5f);
        auto o = run (p, 40000, sine (12000));
        const double db12 = rmsDb (o[0], 20000, 40000, sineRms);
        const double want = 20 * std::log10 (eq::highShelfAt (12000, 30000, 6, 0.5));
        check (db12 > 0.3 && db12 < 5.9 && std::abs (db12 - want) < 0.05,
               "shelf a 30 kHz: a 12 kHz sube " + String (db12, 2) + " dB (curva " + String (want, 2) + "), sin llegar al tope");
        MemoryBlock st;
        p.getStateInformation (st);
        ExcellecuProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (q.currentParams().bands[(size_t) b].sets[eq::St].fc - 30000) < 1, "los 30 kHz se guardan con la sesión");
    }

    {
        std::cout << "Escuchar banda" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        const int bell = m.create (eq::Bell, 1000, 6);
        m.set (ids::set (bell, eq::St, "q"), 2.0f);
        m.create (eq::LowShelf, 100, -4);
        p.setSolo (bell, eq::St);
        auto at1k = run (p, 40000, sine (1000)), at4k = run (p, 40000, sine (4000));
        const double d1 = rmsDb (at1k[0], 20000, 40000, sineRms), d4 = rmsDb (at4k[0], 20000, 40000, sineRms);
        check (std::abs (d1) < 0.1, "campana en solo: su frecuencia pasa a 0 dB (" + String (d1, 2) + ")");
        check (d4 < -40, "campana en solo: 4 kHz queda fuera (" + String (d4, 1) + " dB)");

        // Campana a 0 dB: se oye igualmente su forma
        m.set (ids::set (bell, eq::St, "gain"), 0.0f);
        auto z = run (p, 40000, sine (1000));
        check (std::abs (rmsDb (z[0], 20000, 40000, sineRms)) < 0.1, "una banda a 0 dB también se oye en solo");

        // High pass en solo: se oye lo que recorta
        const int hp = m.create (eq::HighPass, 500, 0);
        m.set (ids::set (hp, eq::St, "slope"), 3.0f);
        p.setSolo (hp, eq::St);
        auto lo = run (p, 40000, sine (100)), hi = run (p, 40000, sine (4000));
        check (rmsDb (lo[0], 20000, 40000, sineRms) > -0.5 && rmsDb (hi[0], 20000, 40000, sineRms) < -40,
               "HPF en solo: suena lo de debajo del corte, no lo de arriba");

        // Banda de Side en solo: con señal solo Mid no suena nada
        const int sb = m.create (eq::Bell, 1000, 3);
        m.setMode (sb, eq::S);
        p.setSolo (sb, eq::S);
        auto mid = run (p, 40000, sine (1000));
        auto side = run (p, 40000, [] (int i, int c) { return (c == 0 ? 1.0f : -1.0f) * sine (1000) (i, c); });
        check (rmsDb (mid[0], 20000, 40000, sineRms) < -100, "banda de Side en solo: el Mid queda en silencio");
        check (std::abs (rmsDb (side[0], 20000, 40000, sineRms)) < 0.1 && std::abs (rmsDb (side[1], 20000, 40000, sineRms)) < 0.1,
               "banda de Side en solo: el Side a su frecuencia pasa entero");

        // Banda de L en solo: R en silencio
        const int lb = m.create (eq::Bell, 2000, 4);
        m.setMode (lb, eq::L);
        p.setSolo (lb, eq::L);
        auto lr = run (p, 40000, sine (2000));
        check (std::abs (rmsDb (lr[0], 20000, 40000, sineRms)) < 0.1 && rmsDb (lr[1], 20000, 40000, sineRms) < -100,
               "banda de L en solo: suena L, R en silencio");

        // Quitar el solo: vuelve la curva normal
        p.setSolo (-1, eq::St);
        auto back = run (p, 40000, sine (1000));
        check (std::abs (rmsDb (back[0], 20000, 40000, sineRms) - rmsDb (run (p, 40000, sine (1000))[0], 20000, 40000, sineRms)) < 0.01
                   && rmsDb (back[0], 20000, 40000, sineRms) > -1,
               "sin solo vuelve la curva del EQ");
        p.history.flush();
        const int before = p.history.undoCount();
        p.setSolo (bell, eq::St);
        p.setSolo (-1, eq::St);
        p.history.flush();
        check (p.history.undoCount() == before, "escuchar no entra en el historial");
        MemoryBlock st;
        p.setSolo (bell, eq::St);
        p.getStateInformation (st);
        ExcellecuProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.currentParams().soloSlot == -1, "el solo no se guarda con la sesión");

        // En tiempo real, con fundido: activar el solo no deja saltos
        p.setSolo (-1, eq::St);
        p.setNonRealtime (false);
        AudioBuffer<float> buf (2, 480);
        MidiBuffer midi;
        float last = 0, maxStep = 0;
        for (int blk = 0; blk < 300; ++blk)
        {
            if (blk == 150) p.setSolo (bell, eq::St);
            for (int i = 0; i < 480; ++i) for (int c = 0; c < 2; ++c) buf.setSample (c, i, sine (1000) (blk * 480 + i, c));
            p.processBlock (buf, midi);
            Thread::sleep (2);
            for (int i = 0; i < 480; ++i) { if (blk > 120) maxStep = std::max (maxStep, std::abs (buf.getSample (0, i) - last)); last = buf.getSample (0, i); }
        }
        const float sineStep = (float) (0.25 * 2 * eq::PI * 1000 / SR) * 2.0f;   // la curva a 1 kHz llega a +6 dB
        check (maxStep <= sineStep * 1.05f, "activar el solo en tiempo real sin saltos (paso máx. " + String (maxStep, 4) + ")");
    }


    {
        std::cout << "Bypass" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        m.create (eq::Bell, 1000, 6);
        m.create (eq::HighPass, 80, 0);
        check (p.getBypassParameter() != nullptr && p.getBypassParameter() == p.apvts.getParameter ("bypass"),
               "el plugin ofrece su propio bypass al DAW");
        const int L = p.getLatencySamples();
        auto in = [] (int i, int c) { return (float) (0.3 * std::sin (2 * eq::PI * 997 * i / 48000.0 + c) + 0.2 * std::sin (2 * eq::PI * 60 * i / 48000.0)); };
        setPlain (p, "bypass", 1.0f);
        auto o = run (p, 30000, in);
        double worst = 0;
        for (int i = L; i < 30000; ++i) for (int c = 0; c < 2; ++c) worst = std::max (worst, (double) std::abs (o[(size_t) c][(size_t) i] - in (i - L, c)));
        check (worst < 1e-6, "puenteado: sale la entrada con el mismo retraso que el procesado (dif. " + String (worst, 9) + ")");
        // processBlockBypassed (lo usan algunos formatos): igual
        {
            ExcellecuProcessor q;
            q.prepareToPlay (SR, 480);
            AudioBuffer<float> b (2, 480);
            MidiBuffer mid;
            std::vector<float> outL;
            for (int blk = 0; blk < 40; ++blk)
            {
                for (int i = 0; i < 480; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, in (blk * 480 + i, c));
                q.processBlockBypassed (b, mid);
                for (int i = 0; i < 480; ++i) outL.push_back (b.getSample (0, i));
            }
            double w2 = 0;
            for (int i = L + 2000; i < (int) outL.size(); ++i) w2 = std::max (w2, (double) std::abs (outL[(size_t) i] - in (i - L, 0)));
            check (w2 < 1e-6, "processBlockBypassed: entrada con el retraso del procesado");
        }
        // Bloques mayores que los anunciados
        {
            ExcellecuProcessor q;
            q.prepareToPlay (SR, 256);
            setPlain (q, "bypass", 1.0f);
            AudioBuffer<float> b (2, 10000);
            MidiBuffer mid;
            for (int i = 0; i < 10000; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, in (i, c));
            q.processBlock (b, mid);
            double w3 = 0;
            for (int i = L; i < 10000; ++i) w3 = std::max (w3, (double) std::abs (b.getSample (0, i) - in (i - L, 0)));
            check (w3 < 1e-6, "bloque de 10000 muestras con búfer anunciado de 256: retraso correcto");
        }
        // Sin bandas, procesado y puenteado son la misma señal: alternar no cambia nada
        {
            ExcellecuProcessor q;
            q.prepareToPlay (SR, 480);
            q.setNonRealtime (false);
            AudioBuffer<float> b (2, 480);
            MidiBuffer mid;
            double w4 = 0;
            for (int blk = 0; blk < 200; ++blk)
            {
                if (blk % 20 == 10) setPlain (q, "bypass", (blk / 20) % 2 == 0 ? 1.0f : 0.0f);
                for (int i = 0; i < 480; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, in (blk * 480 + i, c));
                q.processBlock (b, mid);
                for (int i = 0; i < 480; ++i) { const int t = blk * 480 + i; if (t >= L) w4 = std::max (w4, (double) std::abs (b.getSample (0, i) - in (t - L, 0))); }
            }
            check (w4 < 1e-5, "EQ plano: activar y quitar el bypass no cambia la señal (dif. " + String (w4, 7) + ")");
        }
        // Con EQ, el cambio es un fundido, sin saltos
        setPlain (p, "bypass", 0.0f);
        p.setNonRealtime (false);
        {
            AudioBuffer<float> b (2, 480);
            MidiBuffer mid;
            float last = 0, maxStep = 0;
            const float sineStep = (float) (0.25 * 2 * eq::PI * 1000 / SR) * 2.0f;
            for (int blk = 0; blk < 200; ++blk)
            {
                if (blk == 100) setPlain (p, "bypass", 1.0f);
                if (blk == 150) setPlain (p, "bypass", 0.0f);
                for (int i = 0; i < 480; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, sine (1000) (blk * 480 + i, c));
                p.processBlock (b, mid);
                for (int i = 0; i < 480; ++i) { if (blk > 80) maxStep = std::max (maxStep, std::abs (b.getSample (0, i) - last)); last = b.getSample (0, i); }
            }
            check (maxStep <= sineStep * 1.05f, "con EQ, entrar y salir del bypass sin saltos (paso máx. " + String (maxStep, 4) + ")");
        }
        // El bypass no es parte de un preset ni lo cambia cargarlo
        setPlain (p, "bypass", 1.0f);
        auto xml = presets::capture (p, "X");
        check (xml->getChildByAttribute ("id", "bypass") == nullptr, "el preset no guarda el bypass");
        std::vector<float> vals; int fresh = 0;
        auto flat = presets::factoryXml (presets::factoryList()[0]);
        presets::decode (p, *flat, vals, fresh);
        p.applyPreset (vals, fresh, "Plano");
        check (p.getBypassParameter()->getValue() > 0.5f && ! p.presetModified(), "cargar un preset no quita el bypass");
        setPlain (p, "bypass", 0.0f);
        check (! p.presetModified(), "el bypass no marca el preset como modificado");
    }

    {
        std::cout << "Formas Classic, resonancia y codo (v0.30)" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        p.setNonRealtime (true);
        ui::BandModel m (p);
        // Gana lo que dice la curva: seno a f, medido en dB
        auto measure = [&] (double f) {
            auto o = run (p, 48000, sine (f));
            return rmsDb (o[0], 20000, 40000, sineRms);
        };
        const int b = m.create (eq::Bell, 1000, 6);
        check (plain (p, ids::band (b, "shape")) == 0, "los puntos nuevos son TDM");
        m.set (ids::band (b, "shape"), 1.0f);
        const double atFc = measure (1000), oct = measure (2000);
        const double want = 20 * std::log10 (eq::classicBellAt (2000, 1000, 6, 1));
        check (std::abs (atFc - 6) < 0.1 && std::abs (oct - want) < 0.1,
               "campana Classic +6 dB: " + String (atFc, 2) + " dB en fc, " + String (oct, 2) + " una octava arriba (curva " + String (want, 2) + ")");
        m.setType (b, eq::LowShelf);
        m.set (ids::set (b, eq::St, "q"), 3.0f);
        const double half = measure (1000);
        check (std::abs (half - 3) < 0.1, "low shelf Classic +6 dB: mitad de la ganancia en fc (" + String (half, 2) + " dB)");
        const double dip = measure (1300);
        m.set (ids::band (b, "res"), 2.0f);   // Cénit: el valle de la base se recorta
        const double clipped = measure (1300);
        check (dip < -1 && std::abs (clipped) < 0.1, "Cénit recorta el valle de la base (" + String (dip, 2) + " → " + String (clipped, 2) + " dB)");
        double viaParams[3] {};
        for (int k = 0; k < 3; ++k)
        {
            m.set (ids::band (b, "knee"), (float) k);
            viaParams[k] = measure (1100);
            const double curve = 20 * std::log10 (eq::classicShelfWithRes (eq::LowShelf, 1100, 1000, 6, 3, eq::ResTop, k));
            check (std::abs (viaParams[k] - curve) < 0.1, String ("codo ") + (k == 0 ? "Recorte" : k == 1 ? "Suave" : "Más suave")
                   + ": " + String (viaParams[k], 2) + " dB a 1,1 kHz (curva " + String (curve, 2) + ")");
        }
        check (viaParams[0] < viaParams[1] && viaParams[1] < viaParams[2], "Recorte < Suave < Más suave junto a la línea");
        // Preset: se guardan y se leen; el formato de la web abre igual
        auto xml = presets::capture (p, "C");
        check (xml->getChildByAttribute ("id", ids::band (b, "knee")) != nullptr, "el preset guarda forma, resonancia y codo");
        {
            auto web = parseXML (String ("<ExcellecuPreset version=\"1\" name=\"W\" lrFresh=\"4095\">")
                                 + "<P id=\"b1_used\" v=\"1\"/><P id=\"b1_type\" v=\"2\"/><P id=\"b1_shape\" v=\"1\"/>"
                                 + "<P id=\"b1_res\" v=\"1\"/><P id=\"b1_knee\" v=\"2\"/></ExcellecuPreset>");
            std::vector<float> vals; int fresh = 0;
            presets::decode (p, *web, vals, fresh);
            p.applyPreset (vals, fresh, "W");
            const auto bb = m.band (0);
            check (bb.shape == eq::ShapeClassic && bb.res == eq::ResBase && bb.knee == eq::KneeSofter,
                   "un preset de la web con forma, resonancia y codo abre igual");
            std::vector<float> v2; presets::decode (p, *presets::factoryXml (presets::factoryList()[1]), v2, fresh);
            p.applyPreset (v2, fresh, "TDM");
            check (m.band (0).shape == eq::ShapeTdm, "los presets antiguos (sin forma) abren en TDM");
        }
        // Al borrar y crear otra vez, el punto nuevo vuelve a TDM
        m.set (ids::band (0, "shape"), 1.0f);
        m.remove (0);
        const int again = m.create (eq::Bell, 500, 3);
        check (again == 0 && m.band (0).shape == eq::ShapeTdm && m.band (0).res == eq::ResBoth && m.band (0).knee == eq::KneeClip,
               "un punto nuevo en un hueco usado vuelve a TDM, Las dos y Recorte");
        // Escribir valores
        auto near = [] (double a, double b) { return std::abs (a - b) < 1e-9; };
        check (near (fmt::parseNumber ("1,55k"), 1550) && near (fmt::parseNumber ("1500 Hz"), 1500) && near (fmt::parseNumber ("-2,5 dB"), -2.5)
               && near (fmt::parseNumber ("24 dB/oct"), 24) && near (fmt::parseNumber ("2.5 kHz"), 2500) && near (fmt::parseNumber (" 0,71 "), 0.71)
               && std::isnan (fmt::parseNumber ("abc")) && std::isnan (fmt::parseNumber ("")),
               "leer lo escrito: 1,55k · 1500 Hz · -2,5 dB · 24 dB/oct · 2.5 kHz · 0,71; sin número, nada");
    }

    {
        std::cout << "Analizador (v0.31)" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        const int W = 1000;
        auto fAt = [] (int x) { return 20.0 * std::pow (1000.0, x / (double) W); };   // 20 Hz … 20 kHz
        auto pixelOf = [] (double f) { return (int) std::round (W * std::log (f / 20.0) / std::log (1000.0)); };
        // Seno de 1 kHz a −12 dBFS (amplitud 0,25), EQ plana: Pre y Post marcan −12 dB
        run (p, 48000, sine (1000));
        // La subida recorre el 60 % de lo que falta en cada fotograma (como la web): se deja asentar
        auto settle = [&] (ui::Analyzer& an) { for (int k = 0; k < 40; ++k) an.tick (p, 0, W, fAt); };
        ui::Analyzer a;
        settle (a);
        const float preK = a.preDb[(size_t) pixelOf (1000)], postK = a.postDb[(size_t) pixelOf (1000)];
        check (std::abs (preK + 12.04f) < 0.3f && std::abs (postK + 12.04f) < 0.3f,
               "seno de 1 kHz a −12 dBFS: Pre " + String (preK, 2) + " dB, Post " + String (postK, 2) + " dB");
        const float far = a.postDb[(size_t) pixelOf (5000)];
        check (far < -80, "lejos del seno no hay nada (" + String (far, 1) + " dB a 5 kHz)");
        // Pre va retrasada lo mismo que el motor: con la EQ plana, Pre y Post son la misma señal
        {
            std::vector<float> pre ((size_t) ui::Analyzer::N), post ((size_t) ui::Analyzer::N);
            run (p, 48000, [] (int i, int c) { return (float) (0.3 * std::sin (i * 0.0123 + c) * std::sin (i * 0.00071)); });
            p.copyAnalyzer (pre.data(), post.data(), ui::Analyzer::N);
            float maxd = 0, maxv = 0;
            for (int i = 0; i < ui::Analyzer::N; ++i) { maxd = std::max (maxd, std::abs (pre[(size_t) i] - post[(size_t) i])); maxv = std::max (maxv, std::abs (pre[(size_t) i])); }
            check (maxv > 0.1f && maxd < 1e-4f, "Pre y Post alineados muestra a muestra con la EQ plana (diferencia máx. " + String (maxd, 7) + ")");
        }
        // Campana +6 dB en 1 kHz: Post − Pre = +6 dB en 1 kHz
        m.create (eq::Bell, 1000, 6);
        run (p, 48000, sine (1000));
        ui::Analyzer b;
        settle (b);
        const float diff = b.postDb[(size_t) pixelOf (1000)] - b.preDb[(size_t) pixelOf (1000)];
        check (std::abs (diff - 6) < 0.2f, "campana +6 dB: Post − Pre = " + String (diff, 2) + " dB en 1 kHz");
        // Caída: sin señal nueva baja según la velocidad (Media = 30 dB/s)
        run (p, 48000, [] (int, int) { return 0.0f; });
        b.tick (p, 0.5, W, fAt);
        const float fell = b.postDb[(size_t) pixelOf (1000)];
        check (std::abs (fell - (-6.04f - 15)) < 0.5f, "con silencio cae 15 dB en medio segundo a velocidad Media (" + String (fell, 1) + " dB)");
        // Inclinación: 3 dB/oct alrededor de 1 kHz suma 3 dB por octava por encima
        run (p, 48000, sine (2000));
        ui::Analyzer c; c.tilt = 2;
        settle (c);
        const float tilted = c.preDb[(size_t) pixelOf (2000)];
        check (std::abs (tilted - (-12.04f + 3)) < 0.3f, "inclinación 3 dB/oct: seno de 2 kHz a −12 dBFS marca " + String (tilted, 2) + " dB");
    }

    {
        std::cout << "Interfaz" << std::endl;
        ExcellecuProcessor p;
        p.prepareToPlay (SR, 480);
        ui::BandModel m (p);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1120, 1000);
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

        // Salida en M/S con L/R también activa, y medidores con señal
        setPlain (p, "out_l", -2.0f);
        setPlain (p, "out_mode", 1.0f);
        setPlain (p, "out_s", 1.5f);
        p.applyPreset (std::vector<float> (), p.lrFreshMask(), "Mi master");   // solo el nombre
        p.setPresetSaved ("Mi master");
        setPlain (p, "out_m", -0.5f);
        run (p, 24000, [] (int i, int c) { return (float) ((c == 0 ? 0.9 : 0.6) * std::sin (2 * eq::PI * 1000 * i / 48000.0)); });
        MessageManager::getInstance()->runDispatchLoopUntil (60);
        snapshot (*ed, outDir.getChildFile ("05_salida_medidores.png"));

        // Preset de fábrica TDM (para comparar con la curva original)
        {
            std::vector<float> vals;
            int fresh = 0;
            auto tdm = presets::factoryXml (presets::factoryList()[1]);
            presets::decode (p, *tdm, vals, fresh);
            p.applyPreset (vals, fresh, "TDM");
            p.selected = 3;
            MessageManager::getInstance()->runDispatchLoopUntil (300);
            snapshot (*ed, outDir.getChildFile ("06_tdm.png"));
            // Escala ±3 dB (se guarda con la sesión)
            p.apvts.state.setProperty ("zoomDb", 3, nullptr);
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            snapshot (*ed, outDir.getChildFile ("07_tdm_escala3.png"));
            MemoryBlock st;
            p.getStateInformation (st);
            ExcellecuProcessor q;
            q.setStateInformation (st.getData(), (int) st.getSize());
            check ((int) q.apvts.state.getProperty ("zoomDb") == 3, "la escala vertical se guarda con la sesión");
            p.apvts.state.setProperty ("zoomDb", 12, nullptr);
        }
        {
            // Tamaño mínimo: todo cabe sin solaparse
            ed->setSize (960, 760);
            p.selected = 3;
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            snapshot (*ed, outDir.getChildFile ("08_minimo.png"));
            // Salida enlazada: al arrastrar un deslizador, el otro se mueve en tiempo real (antes de soltar)
            {
                std::vector<Slider*> outs;
                std::function<void (Component&)> find = [&] (Component& c) {
                    for (auto* ch : c.getChildren())
                    {
                        if (auto* sl = dynamic_cast<Slider*> (ch))
                            if (sl->getMinimum() == -24.0 && sl->getMaximum() == 12.0) outs.push_back (sl);
                        find (*ch);
                    }
                };
                find (*ed);
                setPlain (p, "out_mode", 0.0f);
                setPlain (p, "out_link", 1.0f);
                setPlain (p, "out_l", 0.0f);
                setPlain (p, "out_r", 0.0f);
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                if (outs.size() == 2)
                {
                    Slider& a = *outs[0];
                    Slider& b = *outs[1];
                    auto src = Desktop::getInstance().getMainMouseSource();
                    const auto t0 = Time::getCurrentTime();
                    auto ev = [&] (float x, bool dragged) {
                        return MouseEvent (src, { x, (float) a.getHeight() / 2 }, ModifierKeys (ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                           &a, &a, t0, { (float) a.getWidth() * 0.6667f, (float) a.getHeight() / 2 }, t0, 1, dragged);
                    };
                    const float x0 = (float) a.getWidth() * 0.6667f;   // 0 dB está a 2/3 del recorrido (−24 … +12)
                    a.mouseDown (ev (x0, false));
                    a.mouseDrag (ev (x0 + (float) a.getWidth() * 0.15f, true));
                    const double during = b.getValue(), moved = a.getValue();
                    a.mouseUp (ev (x0 + (float) a.getWidth() * 0.15f, true));
                    check (moved > 2.0 && std::abs (during - moved) < 0.11,
                           "salida enlazada: el otro deslizador sigue al arrastrado antes de soltar (" + String (moved, 1) + " / " + String (during, 1) + " dB)");
                    setPlain (p, "out_l", 0.0f);
                    setPlain (p, "out_r", 0.0f);
                }
                else check (false, "encontrar los dos deslizadores de salida (" + String ((int) outs.size()) + ")");
            }
            // Escuchar la banda seleccionada (el botón se ve activo; la gráfica sigue enseñando la curva)
            p.soloOn = true;
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            check (p.soloSlotNow() == 3, "el botón Escuchar sigue a la banda seleccionada");
            snapshot (*ed, outDir.getChildFile ("09_escuchar.png"));
            p.selected = 1;
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            check (p.soloSlotNow() == 1, "al seleccionar otra banda, se escucha esa");
            ed->setSize (1120, 1000);
        }
        // Forma Classic en la interfaz: filas Forma / Resonancia / Codo y escribir valores con doble clic
        {
            for (int s2 = 0; s2 < eq::MAX_BANDS; ++s2) m.remove (s2);
            const int ls = m.create (eq::LowShelf, 1000, 6);
            m.set (ids::set (ls, eq::St, "q"), 3.0f);
            m.set (ids::band (ls, "shape"), 1.0f);
            m.set (ids::band (ls, "res"), 2.0f);
            m.set (ids::band (ls, "knee"), 1.0f);
            p.selected = ls;
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            snapshot (*ed, outDir.getChildFile ("10_classic.png"));
            BandPanel* panel = nullptr;
            ValueEntry* entry = nullptr;
            std::map<String, bool> visible;
            std::function<void (Component&)> find = [&] (Component& c) {
                for (auto* ch : c.getChildren())
                {
                    if (auto* bp = dynamic_cast<BandPanel*> (ch)) panel = bp;
                    if (auto* ve = dynamic_cast<ValueEntry*> (ch)) if (ch->getParentComponent() == panel) entry = ve;
                    if (auto* bt = dynamic_cast<MsButton*> (ch)) if (bt->isVisible()) visible[bt->getButtonText()] = true;
                    find (*ch);
                }
            };
            find (*ed);
            find (*ed);   // segunda pasada: el campo del panel, ya conocido el panel
            check (visible[String::fromUTF8 ("Classic")] && visible[String::fromUTF8 ("Cénit")] && visible[String::fromUTF8 ("Más suave")],
                   "panel: Forma, Resonancia y Codo a la vista en un shelf Classic con Cénit");
            ed->setSize (960, 760);
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            snapshot (*ed, outDir.getChildFile ("11_classic_minimo.png"));
            bool overlap = false;
            {
                std::vector<Rectangle<int>> rs;
                for (auto* ch : panel->getChildren())
                    if (auto* bt = dynamic_cast<MsButton*> (ch)) if (bt->isVisible()) rs.push_back (bt->getBounds());
                for (size_t i = 0; i < rs.size(); ++i)
                    for (size_t j = i + 1; j < rs.size(); ++j) overlap = overlap || rs[i].intersects (rs[j]);
                for (auto& r : rs) overlap = overlap || ! panel->getLocalBounds().contains (r);
            }
            check (! overlap, "tamaño mínimo: los botones del panel no se pisan ni se salen");
            ed->setSize (1120, 1000);
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            // Doble clic en la cifra de la frecuencia → escribir «2,5k» → Intro
            bool typed = false;
            if (panel != nullptr && entry != nullptr)
            {
                auto src = Desktop::getInstance().getMainMouseSource();
                const auto t0 = Time::getCurrentTime();
                for (int y = 0; y < panel->getHeight() && ! typed; y += 3)
                    for (int x = panel->getWidth() - 1; x >= 0 && ! typed; x -= 6)
                    {
                        const MouseEvent ev (src, { (float) x, (float) y }, ModifierKeys(), 1.0f, 0, 0, 0, 0, panel, panel, t0, { (float) x, (float) y }, t0, 2, false);
                        panel->mouseDoubleClick (ev);
                        if (! entry->isOpen()) continue;
                        if (entry->getText() == "1k")
                        {
                            entry->setText ("2,5k");
                            entry->keyPressed (KeyPress (KeyPress::returnKey));
                            typed = true;
                        }
                        else entry->keyPressed (KeyPress (KeyPress::escapeKey));
                    }
            }
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            check (typed && std::abs (plain (p, ids::set (ls, eq::St, "fc")) - 2500) < 1,
                   "escribir «2,5k» en la frecuencia la pone a " + fmt::freq (plain (p, ids::set (ls, eq::St, "fc"))));
            check (entry != nullptr && ! entry->isOpen(), "el campo se cierra al aplicar");
        }
        // Analizador en la ventana: con ruido sonando se dibuja; ajustes guardados con la sesión
        {
            for (int s2 = 0; s2 < eq::MAX_BANDS; ++s2) m.remove (s2);
            p.soloOn = false;                 // una prueba anterior deja «Escuchar» encendido
            p.setSolo (-1, eq::St);
            m.create (eq::Bell, 2500, -6);
            m.create (eq::HighPass, 80, 0);
            uint32_t rng = 1;
            run (p, 96000, [&rng] (int, int) { rng = rng * 1664525u + 1013904223u; return (float) (((rng >> 8) / 8388608.0 - 1.0) * 0.3); });
            MagChart* chart = nullptr;
            std::function<void (Component&)> findChart = [&] (Component& c) {
                for (auto* ch : c.getChildren()) { if (auto* mc = dynamic_cast<MagChart*> (ch)) chart = mc; findChart (*ch); }
            };
            findChart (*ed);
            MessageManager::getInstance()->runDispatchLoopUntil (2500);   // que caiga lo de pruebas anteriores
            check (chart != nullptr && chart->analyzer.active && ! chart->analyzer.postDb.empty(), "con audio, el analizador se dibuja en la gráfica");
            {
                const auto G = chart->geom();
                auto at = [&] (const std::vector<float>& v, double f) { return v[(size_t) std::round (G.toX (f) - G.l)]; };
                const float d25 = at (chart->analyzer.postDb, 2500) - at (chart->analyzer.preDb, 2500);
                const float d40 = at (chart->analyzer.postDb, 40) - at (chart->analyzer.preDb, 40);
                check (std::abs (d25 + 6) < 1.5f && std::abs (d40 + 12) < 2.0f,
                       "con ruido, Post − Pre: " + String (d25, 1) + " dB a 2,5 kHz (campana −6) y " + String (d40, 1) + " dB a 40 Hz (HPF de 12 dB/oct en 80 Hz)");
            }
            snapshot (*ed, outDir.getChildFile ("12_analizador.png"));
            chart->setAnalyzer (true, false, 2, 3);
            check (! (bool) p.apvts.state.getProperty ("anaPost") && (int) p.apvts.state.getProperty ("anaSpeed") == 2
                   && (int) p.apvts.state.getProperty ("anaTilt") == 3, "los ajustes del analizador se guardan con la sesión");
            // Botones del analizador sin pisar a los demás, ancho y mínimo
            for (int w : { 1120, 960 })
            {
                ed->setSize (w, w == 960 ? 760 : 1000);
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                std::vector<Rectangle<int>> rs;
                for (auto* ch : ed->getChildren())
                    if (auto* bt = dynamic_cast<MsButton*> (ch)) if (bt->isVisible()) rs.push_back (bt->getBounds());
                bool overlap = false;
                for (size_t i = 0; i < rs.size(); ++i)
                    for (size_t j = i + 1; j < rs.size(); ++j) overlap = overlap || rs[i].intersects (rs[j]);
                check (! overlap, "ventana de " + String (w) + " px: los botones de la cabecera no se pisan");
                if (w == 960) snapshot (*ed, outDir.getChildFile ("13_analizador_minimo.png"));
            }
            chart->setAnalyzer (true, true, 1, 0);
            ed->setSize (1120, 1000);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        ed.reset();

        // Reabrir la ventana con una banda seleccionada (antes se colgaba: el botón "Activa"
        // refrescaba la ventana a medio construir)
        for (int sel : { 0, 2, 3, -1 })
        {
            p.selected = sel;
            ed.reset (p.createEditor());
            ed->setSize (1120, 1000);
            MessageManager::getInstance()->runDispatchLoopUntil (100);
            ed.reset();
        }
        check (true, "reabrir la ventana con banda seleccionada (0, 2, 3) y sin selección");
        check (! p.soloOn && p.soloSlotNow() == -1, "al cerrar la ventana se deja de escuchar la banda");
    }

    std::cout << (failures == 0 ? "TODO BIEN" : String (failures) + " fallo(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
