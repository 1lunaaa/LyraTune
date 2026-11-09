#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/TunerChain.h"
#include "DSP/Pitch/Scale.h"

class LyraTuneProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LyraTuneProcessor();
    ~LyraTuneProcessor() override;

    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Allowed-note mask from root/scale/custom notes. Lock-free; callable from any thread.
    lyra::PitchClassMask currentMask() const noexcept;
    double latencyMsFor (int rangeIndex, bool hq) const noexcept;

    // Editing workflow (message thread only).
    void resetAllParameters();
    int getABSlot() const noexcept { return abSlot; }
    void switchAB();      // store the current settings in this slot, recall the other one
    void copyToOtherAB(); // overwrite the other slot with the current settings

    juce::UndoManager undoManager { 400, 20 }; // >= 20 user-visible transactions kept
    juce::AudioProcessorValueTreeState apvts;
    lyra::TelemetryQueue telemetry;            // audio -> editor, single consumer

    // Display preferences: not parameters (not automatable, not part of A/B), saved with the project.
    struct UiSettings { bool heatmaps = true, detectedNotes = true, tooltips = true; float scale = 0.7f; } ui;

private:
    void process (juce::AudioBuffer<float>&, bool forceBypass) noexcept;
    void timerCallback() override; // message thread: keeps reported latency in step with Range

    lyra::TunerChain chain; // audio-thread owned after prepareToPlay()
    std::atomic<double> preparedRate { 0.0 };
    juce::ValueTree abSnapshot[2];
    int abSlot = 0;

    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pRoot = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pRef = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pStabilizer = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pHq = nullptr;
    std::atomic<float>* pDoubler = nullptr;
    std::atomic<float>* pDblWidth = nullptr;
    std::atomic<float>* pDblBlend = nullptr;
    std::atomic<float>* pStrike = nullptr;
    std::atomic<float>* pStrikeAmount = nullptr;
    std::atomic<float>* pStrikeLength = nullptr;
    std::atomic<float>* pNotes[12] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LyraTuneProcessor)
};
