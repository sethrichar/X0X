#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Params.h"
#include "DrumSynth.h"
#include "Neve1073.h"

namespace x0x
{
/** 12 x 16 step grid. 0 = off, 1 = on, 2 = accent. Lock-free, shared with the UI. */
struct Pattern
{
    std::array<std::atomic<uint8_t>, kNumTracks * kNumSteps> steps {};
    uint8_t get (int track, int step) const { return steps[(size_t) (track * kNumSteps + step)].load (std::memory_order_relaxed); }
    void set (int track, int step, uint8_t v) { steps[(size_t) (track * kNumSteps + step)].store (v, std::memory_order_relaxed); }
    void clear() { for (auto& s : steps) s.store (0); }
    juce::String toString() const;
    void fromString (const juce::String&);
};

class X0XProcessor final : public juce::AudioProcessor
{
public:
    X0XProcessor();
    ~X0XProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "X0X"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    Pattern pattern;
    DrumKit kit;
    Neve1073 neve;

    // Transport / UI shared state
    std::atomic<bool> internalPlaying { false };
    std::atomic<int>  currentStep { -1 };
    std::atomic<bool> hostTempoInUse { false };
    std::atomic<float> displayBpm { 120.0f };
    void togglePlay();
    void stop();

    /** Audition a drum from the UI (thread-safe). */
    void triggerFromUI (int track) { uiTriggerMask.fetch_or (1u << (unsigned) track); }

    // Samples (message thread)
    bool loadSampleForTrack (int track, const juce::File& file);
    void clearSampleForTrack (int track);
    int  loadKitFolder (const juce::File& folder);     // returns number of tracks matched
    juce::String getSampleName (int track) const;
    bool trackHasSample (int track) const { return staged[(size_t) track] != nullptr; }
    bool getUseSample (int track) const { return useSampleUI[(size_t) track].load(); }
    void setUseSample (int track, bool b);
    static bool isAudioFile (const juce::File& f);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Event { int offset; int track; float velocity; };
    void addStepEvents (double ppqStart, double ppqEnd, double ppqPerSample, int numSamples, std::vector<Event>& events);
    void publishSamples();

    // Clock
    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;
    double lastPpqEnd = -1.0;
    long long nextStepN = 0;
    bool wasRunning = false;

    std::atomic<uint32_t> uiTriggerMask { 0 };
    std::vector<float> mono;
    std::vector<Event> events;

    // Sample hand-off
    juce::SpinLock sampleLock;
    std::array<SamplePtr, kNumTracks> staged {};
    std::array<std::atomic<bool>, kNumTracks> useSampleUI {};
    std::vector<SamplePtr> retired;
    std::atomic<bool> samplesDirty { false };
    juce::AudioFormatManager formatManager;

    // cached params
    std::array<std::atomic<float>*, kNumTracks> pLevel {}, pTune {}, pDecay {};
    std::atomic<float>* pBpm = nullptr; std::atomic<float>* pSwing = nullptr; std::atomic<float>* pLength = nullptr; std::atomic<float>* pAccent = nullptr;
    std::atomic<float>* pInput = nullptr; std::atomic<float>* pHpf = nullptr; std::atomic<float>* pLfF = nullptr; std::atomic<float>* pLfG = nullptr;
    std::atomic<float>* pMidF = nullptr; std::atomic<float>* pMidG = nullptr; std::atomic<float>* pHfG = nullptr;
    std::atomic<float>* pEqIn = nullptr; std::atomic<float>* pClip = nullptr; std::atomic<float>* pOut = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (X0XProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (X0XProcessor)
};
} // namespace x0x
