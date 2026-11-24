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
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::winSize, 1 }, u8 ("Tamaño ventana"),
                                                         StringArray { "512", "1024", "4096", "8192" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::overlap, 1 }, "Overlap",
                                                         StringArray { "25%", "50%", "75%" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::engine, 1 }, "Motor",
                                                         StringArray { "Exacto", u8 ("Clásico") }, 0));

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
                ParameterID { ids::set (b, k, "fc"), 1 }, kp + "frecuencia", logRange (20.0f, 20000.0f, 0.0f), 1000.0f,
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
    return { p.begin(), p.end() };
}
} // namespace

ExcellecuProcessor::ExcellecuProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Entrada", AudioChannelSet::stereo(), true)
                          .withOutput ("Salida", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int b = 0; b < eq::MAX_BANDS; ++b)
    {
        auto& bp = ptrs[(size_t) b];
        bp.used = apvts.getRawParameterValue (ids::band (b, "used"));
        bp.type = apvts.getRawParameterValue (ids::band (b, "type"));
        bp.edit = apvts.getRawParameterValue (ids::band (b, "edit"));
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
    winPtr = apvts.getRawParameterValue (ids::winSize);
    ovPtr = apvts.getRawParameterValue (ids::overlap);
    enginePtr = apvts.getRawParameterValue (ids::engine);

    engine.setParams (currentParams());
    setLatencySamples (engine.latency());
    reportedLatency = engine.latency();
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
    p.n = eq::N_OPTIONS[(size_t) idx (winPtr, 4)];
    p.overlap = eq::OV_OPTIONS[(size_t) idx (ovPtr, 3)];
    p.exact = idx (enginePtr, 2) == 0;
    return p;
}

ExcellecuProcessor::~ExcellecuProcessor()
{
    kernelThread.stopThread (2000);
}

void ExcellecuProcessor::prepareToPlay (double sampleRate, int)
{
    kernelThread.stopThread (2000);   // nada calcula filtros mientras se prepara el motor
    lastSampleRate = sampleRate;
    engine.setParams (currentParams());
    engine.prepare (sampleRate);
    wantedLatency = engine.latency();
    reportedLatency = engine.latency();
    setLatencySamples (engine.latency());
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
    engine.setParams (currentParams());
    // En tiempo real los filtros nuevos llegan del otro hilo; en un render sin tiempo real se
    // calculan aquí mismo, para que el resultado no dependa de la velocidad del render.
    engine.setAsyncKernels (! isNonRealtime() && kernelThread.isThreadRunning());
    wantedLatency = engine.latency();
    engine.process (buffer.getArrayOfWritePointers(), jmin (2, buffer.getNumChannels()), buffer.getNumSamples());
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

void ExcellecuProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ExcellecuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* ExcellecuProcessor::createEditor() { return new ExcellecuEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ExcellecuProcessor(); }
