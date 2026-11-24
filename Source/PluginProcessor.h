#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/EqEngine.h"

namespace ids
{
inline const juce::StringArray keyIds { "st", "l", "r", "m", "s" };
inline juce::String band (int slot, const char* field) { return "b" + juce::String (slot + 1) + "_" + field; }
inline juce::String set (int slot, int key, const char* field) { return "b" + juce::String (slot + 1) + "_" + keyIds[key] + "_" + field; }
inline constexpr const char* winSize = "winSize";
inline constexpr const char* overlap = "overlap";
inline constexpr const char* engine = "engine";   // 0 = Exacto, 1 = Clásico
} // namespace ids

class ExcellecuProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ExcellecuProcessor();
    ~ExcellecuProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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

    // "L y R parten de Estéreo mientras no se hayan tocado" (lrFresh de la web), por hueco
    bool isLrFresh (int slot) const;
    void setLrFresh (int slot, bool fresh);

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

    struct SetPtrs { std::atomic<float>* on; std::atomic<float>* fc; std::atomic<float>* gain; std::atomic<float>* q; std::atomic<float>* slope; };
    struct BandPtrs { std::atomic<float>* used; std::atomic<float>* type; std::atomic<float>* edit; std::array<SetPtrs, eq::NUM_KEYS> sets; };
    std::array<BandPtrs, eq::MAX_BANDS> ptrs {};
    std::atomic<float>* winPtr = nullptr;
    std::atomic<float>* ovPtr = nullptr;
    std::atomic<float>* enginePtr = nullptr;

    eq::Engine engine;
    KernelThread kernelThread { *this };
    std::atomic<double> lastSampleRate { 48000.0 };
    std::atomic<int> wantedLatency { 4096 };
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExcellecuProcessor)
};
