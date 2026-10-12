#include "Neve1073.h"
#include "Params.h"

using namespace x0x;
using namespace x0x::dsp;

namespace
{
// Calibration: with INPUT at 0 dB a 0 dBFS peak drives the input stage to this level.
constexpr float kInputCal   = 0.35f;
constexpr float kInputBias  = 0.14f;   // asymmetry -> even harmonics
constexpr float kOutputCal  = 0.55f;   // output stage headroom relative to the input stage
constexpr float kXfmrDrive  = 1.6f;    // LF core saturation
constexpr float kClipKnee   = 0.84f;   // soft-knee start, ceiling at 1.0

inline float inputStage (float v)
{
    // Asymmetric soft saturation (class-A single-ended behaviour)
    return fastTanh (v + kInputBias) - fastTanh (kInputBias);
}

inline float outputStage (float v)
{
    const float x = v * kOutputCal;
    return (fastTanh (x + 0.05f) - fastTanh (0.05f)) / kOutputCal;
}

inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= kClipKnee) return x;
    const float over = (a - kClipKnee) / (1.0f - kClipKnee);
    const float y = kClipKnee + (1.0f - kClipKnee) * fastTanh (over);
    return x < 0.0f ? -y : y;
}
} // namespace

Neve1073::Neve1073() = default;

void Neve1073::prepare (double sampleRate, int maxBlockSize)
{
    baseRate = sampleRate;
    osRate = sampleRate * (1 << kOversampleOrder);
    maxBlock = juce::jmax (1, maxBlockSize);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, kOversampleOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxBlockSize);
    latency = (int) std::ceil (oversampler->getLatencyInSamples());

    inGain.reset (osRate, 0.02);
    outGain.reset (osRate, 0.02);

    fluxLp.setLowpass (osRate, 55.0);
    xfmrHp.set (osRate, 16.0);
    dcBlock.set (osRate, 5.0);

    reset();
    updateEq (true);
}

void Neve1073::reset()
{
    if (oversampler) oversampler->reset();
    hpf1.reset(); hpf2.reset(); lfShelf.reset(); midBell.reset(); hfShelf.reset();
    fluxLp.reset(); xfmrHp.reset(); dcBlock.reset();
    inGain.setCurrentAndTargetValue (dbToGain (params.inputDb));
    outGain.setCurrentAndTargetValue (dbToGain (params.outputDb));
    lfGainSm = params.lfGainDb; midGainSm = params.midGainDb; hfGainSm = params.hfGainDb;
    outputPeak.store (0.0f);
    clipLed.store (false);
}

void Neve1073::setParams (const Params& p)
{
    params = p;
    inGain.setTargetValue (dbToGain (p.inputDb));
    outGain.setTargetValue (dbToGain (p.outputDb));
}

void Neve1073::updateEq (bool force)
{
    // Smooth the continuous gains at block rate so sweeps don't zipper.
    auto smooth = [] (float& cur, float target) { cur += 0.35f * (target - cur); if (std::abs (target - cur) < 0.01f) cur = target; };
    smooth (lfGainSm, params.lfGainDb);
    smooth (midGainSm, params.midGainDb);
    smooth (hfGainSm, params.hfGainDb);

    const bool freqChanged = force || params.hpfIndex != applied.hpfIndex || params.lfIndex != applied.lfIndex
                           || params.midIndex != applied.midIndex;
    const bool gainChanged = force || ! juce::exactlyEqual (lfGainSm, applied.lfGainDb)
                                   || ! juce::exactlyEqual (midGainSm, applied.midGainDb)
                                   || ! juce::exactlyEqual (hfGainSm, applied.hfGainDb);
    if (! freqChanged && ! gainChanged) return;

    if (params.hpfIndex > 0)
    {
        const double fc = kHpfHz[(size_t) juce::jlimit (0, 4, params.hpfIndex)];
        hpf1.set (osRate, fc);
        hpf2.highpass (osRate, fc, 1.0);     // 1st + 2nd order (Q=1) = 3rd-order Butterworth, 18 dB/oct
    }

    // LF shelf: broad and gentle. The marked frequency is where the boost is ~fully in, so the
    // RBJ midpoint sits about an octave above it.
    lfShelf.lowShelf (osRate, kLfHz[(size_t) juce::jlimit (0, 3, params.lfIndex)] * 2.0, 0.8, lfGainSm);

    // MID: proportional Q (broad when gentle, focused when pushed)
    const double q = 0.55 + 1.05 * std::min (1.0, std::abs ((double) midGainSm) / 18.0);
    midBell.peak (osRate, kMidHz[(size_t) juce::jlimit (0, 5, params.midIndex)], q, midGainSm);

    // HF shelf "12 kHz": a wide shelf whose midpoint sits lower so the top octave is fully lifted.
    hfShelf.highShelf (osRate, 8500.0, 0.8, hfGainSm);

    applied = params;
    applied.lfGainDb = lfGainSm; applied.midGainDb = midGainSm; applied.hfGainDb = hfGainSm;
}

void Neve1073::process (float* data, int numSamples)
{
    if (oversampler == nullptr) return;
    // Hosts should never exceed the prepared block size, but be safe and chunk if they do.
    while (numSamples > maxBlock)
    {
        process (data, maxBlock);
        data += maxBlock;
        numSamples -= maxBlock;
    }
    if (numSamples <= 0) return;
    updateEq (false);

    juce::dsp::AudioBlock<float> block (&data, 1, (size_t) numSamples);
    auto os = oversampler->processSamplesUp (block);
    float* d = os.getChannelPointer (0);
    const int n = (int) os.getNumSamples();

    const bool eq = params.eqIn, hp = params.hpfIndex > 0, clip = params.clip;
    float peak = 0.0f, drive = 0.0f;
    bool led = false;

    for (int i = 0; i < n; ++i)
    {
        // --- input gain + input stage ---------------------------------------
        float v = d[i] * inGain.getNextValue() * kInputCal;
        drive = std::max (drive, std::abs (v));
        v = dcBlock.process (inputStage (v));

        // --- EQ ----------------------------------------------------------------
        if (eq)
        {
            if (hp) v = hpf2.process (hpf1.process (v));
            v = lfShelf.process (v);
            v = midBell.process (v);
            v = hfShelf.process (v);
        }

        // --- output stage ------------------------------------------------------
        v = outputStage (v);

        // --- output transformer: LF core saturation + LF roll-off --------------
        const float lf = fluxLp.lp (v);
        v = (v - lf) + fastTanh (lf * kXfmrDrive) / kXfmrDrive;
        v = xfmrHp.process (v);

        // back to "volts -> dBFS" and output trim
        v = (v / kInputCal) * outGain.getNextValue();

        if (clip)
        {
            if (std::abs (v) > kClipKnee) led = true;
            v = softClip (v);
        }
        else if (std::abs (v) > 1.0f) led = true;

        peak = std::max (peak, std::abs (v));
        d[i] = v;
    }

    oversampler->processSamplesDown (block);

    // Band-limiting a hard-clipped signal re-grows inter-sample peaks (a bandlimited square's
    // fundamental is 4/pi taller than its flat top), so hold the ceiling once more at base rate.
    if (clip)
        for (int i = 0; i < numSamples; ++i) data[i] = softClip (data[i]);

    outputPeak.store (peak);
    driveMeter.store (drive);
    if (led) clipLed.store (true);
}
