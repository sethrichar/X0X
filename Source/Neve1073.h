#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "DspUtils.h"

namespace x0x
{
/**
    A 1073-flavoured channel: stepped input gain into an asymmetric class-A style
    input stage, the classic 3-band + HPF EQ, a class-A output stage feeding a
    transformer model whose core saturates harder at low frequencies, and an
    optional soft-knee clipper. Non-linear stages run 4x oversampled.

    Signal path (oversampled):
        in -> INPUT gain -> input stage sat -> [HPF -> LF -> MID -> HF] -> output stage sat
           -> transformer (LF sat + LF roll-off) -> OUTPUT trim -> [CLIP] -> down-sample
*/
class Neve1073
{
public:
    struct Params
    {
        float inputDb   = 0.0f;   // -20 .. +40, 5 dB steps
        int   hpfIndex  = 0;      // 0 = off
        int   lfIndex   = 1;      // 35/60/110/220
        float lfGainDb  = 0.0f;   // ±16
        int   midIndex  = 2;      // 0.36/0.7/1.6/3.2/4.8/7.2 k
        float midGainDb = 0.0f;   // ±18
        float hfGainDb  = 0.0f;   // ±16 @ 12 kHz
        bool  eqIn      = true;
        bool  clip      = false;
        float outputDb  = 0.0f;   // -60 .. +10
    };

    Neve1073();
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const Params& p);
    /** Processes a mono buffer in place. */
    void process (float* data, int numSamples);
    int getLatencySamples() const { return latency; }

    /** Meter info for the UI. */
    std::atomic<float> outputPeak { 0.0f };   // linear peak of the last block
    std::atomic<bool>  clipLed    { false };  // set when the clipper/ceiling engages
    std::atomic<float> driveMeter { 0.0f };   // how hard the input stage is being hit (0..1+)

private:
    void updateEq (bool force);

    static constexpr int kOversampleOrder = 2;   // 4x
    double baseRate = 44100.0, osRate = 176400.0;
    int latency = 0, maxBlock = 512;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    Params params, applied;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0f }, outGain { 1.0f };
    float lfGainSm = 0.0f, midGainSm = 0.0f, hfGainSm = 0.0f;   // block-rate smoothed EQ gains

    // EQ (at oversampled rate)
    struct FirstOrderHP
    {
        float b0 = 1, b1 = -1, a1 = 0, x1 = 0, y1 = 0;
        void set (double fs, double fc)
        {
            const double k = std::tan (dsp::kPi * fc / fs);
            b0 = (float) (1.0 / (1.0 + k)); b1 = -b0; a1 = (float) ((k - 1.0) / (k + 1.0));
        }
        inline float process (float x) { const float y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
        void reset() { x1 = y1 = 0; }
    };
    FirstOrderHP hpf1;
    dsp::Biquad hpf2, lfShelf, midBell, hfShelf;

    // Transformer model
    dsp::OnePole fluxLp;      // LF split (core flux proxy)
    FirstOrderHP xfmrHp;      // transformer LF roll-off / DC block
    FirstOrderHP dcBlock;     // after asymmetric input stage
};
} // namespace x0x
