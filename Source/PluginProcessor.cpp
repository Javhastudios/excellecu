#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Format.h"

using namespace juce;

namespace
{
String u8 (const char* text) { return String::fromUTF8 (text); }

const char* keyNames[] = { "Est", "L", "R", "M", "S" };

NormalisableRange<float> logRange (float lo, float hi, float snapStep)
{
    NormalisableRange<float> r (
        lo, hi,
        [] (float s, float e, float v) { return s * std::pow (e / s, v); },
        [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
        [snapStep] (float s, float e, float v) {
            v = jlimit (s, e, v);
            return snapStep > 0 ? jlimit (s, e, std::round (v / snapStep) * snapStep) : v;
        });
    return r;
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const StringArray types { "Campana", "Low shelf", "High shelf", "High pass", "Low pass" };
    const StringArray modes { u8 ("Estéreo"), "L", "R", "M", "S" };
    const StringArray slopes { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };

    for (int b = 0; b < eq::MAX_BANDS; ++b)
    {
        const String pre = "B" + String (b + 1) + " ";
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::band (b, "used"), 1 }, pre + "existe", false));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::band (b, "type"), 1 }, pre + "tipo", types, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::band (b, "edit"), 1 }, pre + "canal", modes, 0));
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            const String kp = pre + String::fromUTF8 (keyNames[k]) + " ";
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::set (b, k, "on"), 1 }, kp + "activa", true));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::set (b, k, "fc"), 1 }, kp + "frecuencia", logRange (20.0f, 30000.0f, 0.0f), 1000.0f,
                AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return fmt::freq (v); })));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::set (b, k, "gain"), 1 }, kp + "ganancia", NormalisableRange<float> (-20.0f, 12.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return fmt::gain (v); })));
            p.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { ids::set (b, k, "q"), 1 }, kp + "Q", logRange (0.1f, 10.0f, 0.01f), 1.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::set (b, k, "slope"), 1 }, kp + "pendiente", slopes, 1));
        }
    }

    // Salida: ganancia de L y R, luego de Mid y Side. Las cuatro actúan siempre; el modo solo
    // elige qué pareja se ve y se edita, así cambiar de modo no altera lo que suena.
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "out_mode", 1 }, "Salida modo", StringArray { "L/R", "M/S" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "out_link", 1 }, "Salida enlazar", true));
    const char* outNames[] = { "Salida L", "Salida R", "Salida Mid", "Salida Side" };
    const char* outIds[] = { "out_l", "out_r", "out_m", "out_s" };
    for (int i = 0; i < 4; ++i)
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { outIds[i], 1 }, outNames[i], NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return fmt::gain (v); })));

    // Forma, resonancia y codo por banda (v0.30). Al final de la lista para no mover el orden
    // de los parámetros que ya había.
    for (int b = 0; b < eq::MAX_BANDS; ++b)
    {
        const String pre = "B" + String (b + 1) + " ";
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::band (b, "shape"), 1 }, pre + "forma",
                                                             StringArray { "TDM", "Classic" }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::band (b, "res"), 1 }, pre + "resonancia",
                                                             StringArray { "Las dos", "Base", u8 ("Cénit") }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::band (b, "knee"), 1 }, pre + "codo",
                                                             StringArray { "Recorte", "Suave", u8 ("Más suave") }, 0));
    }
    return { p.begin(), p.end() };
}
} // namespace

void ExcellecuProcessor::Meter::add (const float* x, int n)
{
    Block b;
    b.count = n;
    for (int i = 0; i < n; ++i) { b.peak = jmax (b.peak, std::abs (x[i])); b.sumSq += x[i] * x[i]; }
    const auto w = fifo.write (1);                   // si la interfaz no recoge (ventana cerrada), se descarta
    if (w.blockSize1 > 0) blocks[(size_t) w.startIndex1] = b;
    else if (w.blockSize2 > 0) blocks[(size_t) w.startIndex2] = b;
}

ExcellecuProcessor::Meter::Block ExcellecuProcessor::Meter::collect()
{
    Block total;
    const auto r = fifo.read (fifo.getNumReady());
    auto take = [&] (int start, int size) {
        for (int i = start; i < start + size; ++i)
        {
            const auto& b = blocks[(size_t) i];
            total.peak = jmax (total.peak, b.peak);
            total.sumSq += b.sumSq;
            total.count += b.count;
        }
    };
    take (r.startIndex1, r.blockSize1);
    take (r.startIndex2, r.blockSize2);
    return total;
}

ExcellecuProcessor::ExcellecuProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrada", AudioChannelSet::stereo(), true)
                          .withOutput ("Salida", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      history (*this, { [this] { return lrFreshMask(); }, [this] (int m) { setLrFreshMask (m); } })
{
    history.setSelectionAccess ([this] { return selected; }, [this] (int s) { selected = s; });
    for (int i = 0; i < ANA_RING; ++i) { anaPre[(size_t) i].store (0.0f); anaPost[(size_t) i].store (0.0f); }
    const char* outIds[] = { "out_l", "out_r", "out_m", "out_s" };
    for (int i = 0; i < 4; ++i) outPtr[i] = apvts.getRawParameterValue (outIds[i]);
    bypassParam = dynamic_cast<AudioParameterBool*> (apvts.getParameter ("bypass"));

    for (int b = 0; b < eq::MAX_BANDS; ++b)
    {
        auto& bp = ptrs[(size_t) b];
        bp.used = apvts.getRawParameterValue (ids::band (b, "used"));
        bp.type = apvts.getRawParameterValue (ids::band (b, "type"));
        bp.edit = apvts.getRawParameterValue (ids::band (b, "edit"));
        bp.shape = apvts.getRawParameterValue (ids::band (b, "shape"));
        bp.res = apvts.getRawParameterValue (ids::band (b, "res"));
        bp.knee = apvts.getRawParameterValue (ids::band (b, "knee"));
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            auto& sp = bp.sets[(size_t) k];
            sp.on = apvts.getRawParameterValue (ids::set (b, k, "on"));
            sp.fc = apvts.getRawParameterValue (ids::set (b, k, "fc"));
            sp.gain = apvts.getRawParameterValue (ids::set (b, k, "gain"));
            sp.q = apvts.getRawParameterValue (ids::set (b, k, "q"));
            sp.slope = apvts.getRawParameterValue (ids::set (b, k, "slope"));
        }
    }

    engine.setParams (currentParams());
    setLatencySamples (engine.latency());
    reportedLatency = engine.latency();
    presetValues = currentValues();
    startTimerHz (10);
}

eq::Params ExcellecuProcessor::currentParams() const
{
    auto idx = [] (std::atomic<float>* a, int size) { return jlimit (0, size - 1, (int) std::lround (a->load())); };
    eq::Params p;
    for (int b = 0; b < eq::MAX_BANDS; ++b)
    {
        const auto& bp = ptrs[(size_t) b];
        auto& band = p.bands[(size_t) b];
        band.used = bp.used->load() > 0.5f;
        if (! band.used) continue; // las bandas vacías quedan con valores por defecto (no afectan)
        band.type = idx (bp.type, eq::NUM_TYPES);
        band.edit = idx (bp.edit, eq::NUM_KEYS);
        band.shape = idx (bp.shape, 2);
        band.res = idx (bp.res, 3);
        band.knee = idx (bp.knee, 3);
        for (int k = 0; k < eq::NUM_KEYS; ++k)
        {
            const auto& sp = bp.sets[(size_t) k];
            auto& s = band.sets[(size_t) k];
            s.on = sp.on->load() > 0.5f;
            s.fc = sp.fc->load();
            s.gain = sp.gain->load();
            s.q = sp.q->load();
            s.slope = eq::SLOPES[(size_t) idx (sp.slope, 4)];
        }
    }
    p.n = kWindow;
    p.soloSlot = soloSlot.load();
    p.soloKey = soloKey.load();
    return p;
}

ExcellecuProcessor::~ExcellecuProcessor()
{
    kernelThread.stopThread (2000);
}

void ExcellecuProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    kernelThread.stopThread (2000);   // nada calcula filtros mientras se prepara el motor
    lastSampleRate = sampleRate;
    engine.setParams (currentParams());
    engine.prepare (sampleRate);
    wantedLatency = engine.latency();
    reportedLatency = engine.latency();
    setLatencySamples (engine.latency());
    for (int i = 0; i < 4; ++i)
    {
        outSmooth[(size_t) i].reset (sampleRate, 0.05);   // 50 ms: sin clics al mover la ganancia
        outSmooth[(size_t) i].setCurrentAndTargetValue (Decibels::decibelsToGain (outPtr[i]->load()));
    }
    // Bypass: retraso del audio puenteado = latencia del motor; fundido de 30 ms
    dryLen = engine.latency();
    dryPos = 0;
    for (auto& r : dryRing) r.assign ((size_t) jmax (1, dryLen), 0.0f);
    for (auto& d : dryScratch) d.assign ((size_t) jmax (samplesPerBlock, 4096), 0.0f);
    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
    kernelThread.startThread();
}

void ExcellecuProcessor::releaseResources()
{
    kernelThread.stopThread (2000);
}

bool ExcellecuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void ExcellecuProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int nc = jmin (2, buffer.getNumChannels()), ns = buffer.getNumSamples();
    // Por trozos del tamaño del búfer de retraso (un DAW puede mandar bloques más grandes que los anunciados)
    const int chunk = dryScratch[0].empty() ? ns : (int) dryScratch[0].size();   // sin preparar: de una vez
    for (int start = 0; start < ns; start += chunk)
    {
        float* chunkPtrs[2] = { buffer.getWritePointer (0, start), nc > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (chunkPtrs, nc, jmin (chunk, ns - start), false);
    }
}

// Algunos formatos piden el bypass del DAW en lugar del parámetro: mismo camino, puenteado
void ExcellecuProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    const int nc = jmin (2, buffer.getNumChannels()), ns = buffer.getNumSamples();
    const int chunk = dryScratch[0].empty() ? ns : (int) dryScratch[0].size();
    for (int start = 0; start < ns; start += chunk)
    {
        float* chunkPtrs[2] = { buffer.getWritePointer (0, start), nc > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (chunkPtrs, nc, jmin (chunk, ns - start), true);
    }
}

void ExcellecuProcessor::processChunk (float* const* ch, int nc, int ns, bool forceBypass)
{
    engine.setParams (currentParams());
    // En tiempo real los filtros nuevos llegan del otro hilo; en un render sin tiempo real se
    // calculan aquí mismo, para que el resultado no dependa de la velocidad del render.
    engine.setAsyncKernels (! isNonRealtime() && kernelThread.isThreadRunning());
    wantedLatency = engine.latency();
    meterChannels.store (nc, std::memory_order_relaxed);
    for (int c = 0; c < nc; ++c) inMeter[(size_t) c].add (ch[c], ns);

    // Copia retrasada de la entrada (para el bypass), antes de que el motor la sobrescriba
    const bool haveDry = dryLen > 0 && (int) dryScratch[0].size() >= ns;   // falso solo sin preparar
    if (haveDry)
    {
        for (int c = 0; c < nc; ++c)
        {
            float* ring = dryRing[(size_t) c].data();
            float* out = dryScratch[(size_t) c].data();
            int pos = dryPos;
            for (int i = 0; i < ns; ++i)
            {
                out[i] = ring[pos];
                ring[pos] = ch[c][i];
                if (++pos == dryLen) pos = 0;
            }
        }
        dryPos = (dryPos + ns) % dryLen;
    }

    engine.process (ch, nc, ns);
    applyOutputGain (ch, nc, ns);

    // Fundido entre procesado y puenteado
    bypassMix.setTargetValue (forceBypass || bypassParam->get() ? 1.0f : 0.0f);
    if (! haveDry) {}
    else if (bypassMix.isSmoothing())
    {
        for (int i = 0; i < ns; ++i)
        {
            const float g = bypassMix.getNextValue();
            for (int c = 0; c < nc; ++c) ch[c][i] += g * (dryScratch[(size_t) c][(size_t) i] - ch[c][i]);
        }
    }
    else if (bypassMix.getTargetValue() > 0.5f)
    {
        for (int c = 0; c < nc; ++c) std::copy (dryScratch[(size_t) c].data(), dryScratch[(size_t) c].data() + ns, ch[c]);
    }
    for (int c = 0; c < nc; ++c) outMeter[(size_t) c].add (ch[c], ns);

    // Analizador: Pre = la entrada retrasada (la copia del bypass), Post = lo que sale
    int pos = anaPos.load (std::memory_order_relaxed);
    for (int i = 0; i < ns; ++i)
    {
        float pre = 0.0f;
        if (haveDry) pre = nc > 1 ? 0.5f * (dryScratch[0][(size_t) i] + dryScratch[1][(size_t) i]) : dryScratch[0][(size_t) i];
        const float post = nc > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
        anaPre[(size_t) pos].store (pre, std::memory_order_relaxed);
        anaPost[(size_t) pos].store (post, std::memory_order_relaxed);
        pos = (pos + 1) & (ANA_RING - 1);
    }
    anaPos.store (pos, std::memory_order_release);
}

void ExcellecuProcessor::applyOutputGain (float* const* ch, int numCh, int numSamples)
{
    bool moving = false, unity = true;
    for (int i = 0; i < 4; ++i)
    {
        auto& sm = outSmooth[(size_t) i];
        sm.setTargetValue (Decibels::decibelsToGain (outPtr[i]->load()));
        moving = moving || sm.isSmoothing();
        unity = unity && sm.getTargetValue() == 1.0f;
    }
    if (! moving && unity) return;

    if (numCh < 2)
    {
        // Mono: la señal es solo Mid (Side = 0), así que actúan L y Mid
        float* x = ch[0];
        for (int i = 0; i < numSamples; ++i)
        {
            const float gl = outSmooth[0].getNextValue();
            outSmooth[1].skip (1);
            const float gm = outSmooth[2].getNextValue();
            outSmooth[3].skip (1);
            x[i] *= gl * gm;
        }
        return;
    }
    float* l = ch[0];
    float* r = ch[1];
    if (! moving)
    {
        const float gl = outSmooth[0].getTargetValue(), gr = outSmooth[1].getTargetValue();
        const float gm = outSmooth[2].getTargetValue(), gs = outSmooth[3].getTargetValue();
        if (gm == gs)
        {
            for (int i = 0; i < numSamples; ++i) { l[i] *= gl * gm; r[i] *= gr * gm; }
            return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = l[i] * gl, b = r[i] * gr;
            const float m = 0.5f * (a + b) * gm, s = 0.5f * (a - b) * gs;
            l[i] = m + s; r[i] = m - s;
        }
        return;
    }
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = l[i] * outSmooth[0].getNextValue(), b = r[i] * outSmooth[1].getNextValue();
        const float m = 0.5f * (a + b) * outSmooth[2].getNextValue(), s = 0.5f * (a - b) * outSmooth[3].getNextValue();
        l[i] = m + s; r[i] = m - s;
    }
}

void ExcellecuProcessor::timerCallback()
{
    // La latencia cambia con el tamaño de ventana: se avisa al DAW desde el hilo de mensajes
    const int lat = wantedLatency.load();
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples (lat); }
}

bool ExcellecuProcessor::isLrFresh (int slot) const
{
    const int mask = apvts.state.getProperty ("lrFresh", 0xfff);
    return (mask >> slot) & 1;
}

void ExcellecuProcessor::setLrFresh (int slot, bool fresh)
{
    int mask = apvts.state.getProperty ("lrFresh", 0xfff);
    mask = fresh ? (mask | (1 << slot)) : (mask & ~(1 << slot));
    apvts.state.setProperty ("lrFresh", mask, nullptr);
}

int ExcellecuProcessor::lrFreshMask() const { return apvts.state.getProperty ("lrFresh", 0xfff); }
void ExcellecuProcessor::setLrFreshMask (int mask) { apvts.state.setProperty ("lrFresh", mask & 0xfff, nullptr); }

std::vector<float> ExcellecuProcessor::currentValues() const
{
    std::vector<float> v;
    for (auto* prm : getParameters()) v.push_back (prm->getValue());
    return v;
}

void ExcellecuProcessor::applyPreset (const std::vector<float>& values, int lrFresh, const String& name)
{
    const auto& params = getParameters();
    for (int i = 0; i < params.size() && i < (int) values.size(); ++i)
    {
        auto* prm = params[i];
        if (prm == bypassParam || prm->getValue() == values[(size_t) i]) continue;   // el bypass no es del preset
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (values[(size_t) i]);
        prm->endChangeGesture();
    }
    // lrFresh va en el mismo paso de deshacer: el historial lo lee al cerrar el paso
    setLrFreshMask (lrFresh);
    apvts.state.setProperty ("presetName", name, nullptr);
    presetValues = currentValues();
    if (selected >= 0 && apvts.getRawParameterValue (ids::band (selected, "used"))->load() < 0.5f) selected = -1;
}

void ExcellecuProcessor::setPresetSaved (const String& name)
{
    apvts.state.setProperty ("presetName", name, nullptr);
    presetValues = currentValues();
}

bool ExcellecuProcessor::presetModified() const
{
    const auto& params = getParameters();
    if (presetValues.size() != (size_t) params.size()) return false;
    for (int i = 0; i < params.size(); ++i)
    {
        if (params[i] == bypassParam) continue;
        // Las bandas que no existen no cuentan (sus valores no suenan)
        if (std::abs (params[i]->getValue() - presetValues[(size_t) i]) > 1e-5f)
        {
            if (auto* r = dynamic_cast<RangedAudioParameter*> (params[i]))
            {
                const auto id = r->getParameterID();
                if (id.startsWithChar ('b') && id.containsChar ('_') && ! id.endsWith ("_used"))
                {
                    const int b = id.substring (1).getIntValue() - 1;
                    if (apvts.getRawParameterValue (ids::band (b, "used"))->load() < 0.5f) continue;
                }
            }
            return true;
        }
    }
    return false;
}

void ExcellecuProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ExcellecuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            // Sesión nueva: el historial anterior no vale y el preset cuenta como sin modificar
            history.clear();
            presetValues = currentValues();
        }
}

AudioProcessorEditor* ExcellecuProcessor::createEditor() { return new ExcellecuEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ExcellecuProcessor(); }
