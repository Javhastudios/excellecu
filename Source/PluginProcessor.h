#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/EqEngine.h"
#include "state/UndoHistory.h"

namespace ids
{
inline const juce::StringArray keyIds { "st", "l", "r", "m", "s" };
inline juce::String band (int slot, const char* field) { return "b" + juce::String (slot + 1) + "_" + field; }
inline juce::String set (int slot, int key, const char* field) { return "b" + juce::String (slot + 1) + "_" + keyIds[key] + "_" + field; }
} // namespace ids

class ExcellecuProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    // Tamaño del filtro del motor (N): filtro de N+1 muestras, latencia N/2 + 256
    static constexpr int kWindow = 8192;

    ExcellecuProcessor();
    ~ExcellecuProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    // Bypass propio: el DAW lo usa en lugar del suyo, así el audio puenteado sale con el mismo
    // retraso que el procesado (sin saltos de tiempo al activarlo) y con un fundido corto.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Excellecu"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Ajustes actuales tal como los usa el motor
    eq::Params currentParams() const;
    double sampleRateForUi() const { return lastSampleRate.load(); }

    // Estado de la interfaz (no es audio). selected: hueco de la banda seleccionada (-1 ninguna)
    int selected = -1;
    int newBandType = eq::Bell;

    // Escuchar banda (solo): lo activa el botón de la banda y sigue a la banda seleccionada.
    // No es un parámetro ni se guarda con la sesión; al cerrar la ventana se apaga.
    bool soloOn = false;
    void setSolo (int slot, int key) { soloSlot.store (slot); soloKey.store (key); }
    int soloSlotNow() const { return soloSlot.load(); }

    // "L y R parten de Estéreo mientras no se hayan tocado" (lrFresh de la web), por hueco
    bool isLrFresh (int slot) const;
    void setLrFresh (int slot, bool fresh);
    int lrFreshMask() const;
    void setLrFreshMask (int mask);

    // Deshacer / rehacer (solo cambios hechos desde la interfaz)
    UndoHistory history;

    // Medidores: por cada bloque, el audio deja en una cola sin bloqueos su pico, su suma de
    // cuadrados y su número de muestras, juntos; la interfaz los recoge todos en cada refresco.
    // (Antes se acumulaba en atómicos sueltos y, si la interfaz recogía a mitad de una suma, un
    // bloque podía contarse dos veces y el RMS daba un salto.)
    struct Meter
    {
        struct Block { float peak = 0, sumSq = 0; int count = 0; };
        void add (const float* x, int n);                 // hilo de audio
        Block collect();                                  // hilo de la interfaz: todo lo pendiente
        juce::AbstractFifo fifo { 512 };
        std::array<Block, 512> blocks;
    };
    std::array<Meter, 2> inMeter, outMeter;
    std::atomic<int> meterChannels { 2 };

    // Analizador (v0.31, web v0.24): suma mono de la entrada retrasada lo mismo que el motor (así
    // Pre y Post van alineados) y de lo que sale. El audio escribe; la ventana copia las últimas
    // muestras. Sin bloqueos: cada muestra es atómica y la posición se publica al final del bloque.
    static constexpr int ANA_RING = 16384;
    std::array<std::atomic<float>, ANA_RING> anaPre, anaPost;
    std::atomic<int> anaPos { 0 };
    // Copia las últimas n muestras (n ≤ ANA_RING), de la más antigua a la más reciente
    void copyAnalyzer (float* pre, float* post, int n) const
    {
        const int end = anaPos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int k = (end - n + i) & (ANA_RING - 1);
            pre[i] = anaPre[(size_t) k].load (std::memory_order_relaxed);
            post[i] = anaPost[(size_t) k].load (std::memory_order_relaxed);
        }
    }

    // Presets: aplica valores normalizados (todos los parámetros) desde la interfaz, como un solo
    // paso de deshacer. El nombre se guarda con la sesión.
    void applyPreset (const std::vector<float>& values, int lrFresh, const juce::String& name);
    juce::String presetName() const { return apvts.state.getProperty ("presetName", "Plano").toString(); }
    bool presetModified() const;
    void setPresetSaved (const juce::String& name);   // tras guardar: ese es ahora el preset actual   // ¿difiere el estado del último preset cargado?

private:
    void timerCallback() override;

    // Calcula los filtros del motor Exacto fuera del hilo de audio: cuando cambian los ajustes,
    // los calcula y los publica; el audio los recoge y funde del filtro anterior al nuevo.
    class KernelThread : public juce::Thread
    {
    public:
        explicit KernelThread (ExcellecuProcessor& p) : juce::Thread ("Excellecu filtros"), proc (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                proc.engine.buildPending (proc.currentParams(), builder);
                wait (5);
            }
        }
    private:
        ExcellecuProcessor& proc;
        eq::KernelBuilder builder;
    };

    void applyOutputGain (float* const* ch, int numCh, int numSamples);
    void processChunk (float* const* ch, int nc, int ns, bool forceBypass);

    // Bypass: el audio de entrada pasa por un retraso igual a la latencia del motor y se funde
    // con el procesado. El motor sigue trabajando puenteado para que la vuelta sea limpia.
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::LinearSmoothedValue<float> bypassMix;            // 0 = procesado, 1 = puenteado
    std::array<std::vector<float>, 2> dryRing, dryScratch;
    int dryPos = 0, dryLen = 0;
    std::vector<float> currentValues() const;

    // Ganancia de salida: L/R y luego M/S, con suavizado (en ganancia lineal)
    std::atomic<float>* outPtr[4] {};
    std::array<juce::LinearSmoothedValue<float>, 4> outSmooth;
    std::vector<float> presetValues;
    std::atomic<int> soloSlot { -1 }, soloKey { eq::St };   // valores del último preset cargado (para "modificado")

    struct SetPtrs { std::atomic<float>* on; std::atomic<float>* fc; std::atomic<float>* gain; std::atomic<float>* q; std::atomic<float>* slope; };
    struct BandPtrs
    {
        std::atomic<float>* used; std::atomic<float>* type; std::atomic<float>* edit;
        std::atomic<float>* shape; std::atomic<float>* res; std::atomic<float>* knee;
        std::array<SetPtrs, eq::NUM_KEYS> sets;
    };
    std::array<BandPtrs, eq::MAX_BANDS> ptrs {};

    eq::Engine engine;
    KernelThread kernelThread { *this };
    std::atomic<double> lastSampleRate { 48000.0 };
    std::atomic<int> wantedLatency { eq::latencyFor (kWindow) };
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExcellecuProcessor)
};
