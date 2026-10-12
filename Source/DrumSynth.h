#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>
#include "Params.h"
#include "DspUtils.h"

namespace x0x
{
/** A loaded sample, immutable once created. Shared between message and audio threads. */
struct SampleData
{
    juce::AudioBuffer<float> mono;      // single channel
    double sourceRate = 44100.0;
    juce::String path;
    juce::String name;
};
using SamplePtr = std::shared_ptr<const SampleData>;

/** Base class for one monophonic drum voice. tune: -1..1, decay: 0..1, vel: 0..1 */
class Voice
{
public:
    virtual ~Voice() = default;
    virtual void prepare (double sampleRate) { sr = sampleRate; }
    virtual void trigger (float vel, float tune, float decay) = 0;
    virtual float process() = 0;
    virtual bool isActive() const = 0;
    /** Fast fade-out (for hat choke). */
    void choke() { choking = true; }
protected:
    double sr = 44100.0;
    bool choking = false;
    float chokeGain = 1.0f;
    /** Applies the choke fade; returns 0 when fully choked. */
    inline float applyChoke (float x)
    {
        if (! choking) return x;
        chokeGain -= 1.0f / (0.003f * (float) sr);     // ~3 ms fade
        if (chokeGain <= 0.0f) { chokeGain = 0.0f; }
        return x * chokeGain;
    }
    void resetChoke() { choking = false; chokeGain = 1.0f; }
};

std::unique_ptr<Voice> createSynthVoice (int track);

/** Plays a user sample with tune + exponential decay. */
class SampleVoice : public Voice
{
public:
    void setSample (SamplePtr s) { sample = std::move (s); pos = -1.0; }
    bool hasSample() const { return sample != nullptr; }
    void trigger (float vel, float tune, float decay) override;
    float process() override;
    bool isActive() const override { return sample != nullptr && pos >= 0.0 && env.active(); }
private:
    SamplePtr sample;
    double pos = -1.0, rate = 1.0;
    float gain = 1.0f;
    dsp::DecayEnv env;
};

/** The whole kit: 12 tracks, each with a synth voice and an optional sample voice. */
class DrumKit
{
public:
    DrumKit();
    void prepare (double sampleRate);
    void reset();

    void setTrackParams (int track, float level, float tune, float decay);
    void setSample (int track, SamplePtr s);          // audio thread safe (called from processBlock)
    void setUseSample (int track, bool b) { useSample[(size_t) track] = b; }
    bool getUseSample (int track) const { return useSample[(size_t) track]; }
    bool hasSample (int track) const { return samples[(size_t) track].hasSample(); }

    void trigger (int track, float velocity);
    /** Renders & accumulates into a mono buffer. */
    void render (float* out, int numSamples);

    /** Per-track activity for UI lights. Set on trigger, decays via UI. */
    std::array<std::atomic<float>, kNumTracks> hitFlash {};

private:
    std::array<std::unique_ptr<Voice>, kNumTracks> synths;
    std::array<SampleVoice, kNumTracks> samples;
    std::array<bool, kNumTracks> useSample {};
    std::array<float, kNumTracks> level {}, tune {}, decay {};
};
} // namespace x0x
