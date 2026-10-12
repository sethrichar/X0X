// Offline sanity test: renders the default pattern through the 1073 stage, writes a WAV,
// and checks the DSP for NaNs, level, EQ response and clipper ceiling.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <cstdio>

using namespace x0x;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

static void setParam (X0XProcessor& p, const juce::String& id, float value)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& buf, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

static juce::AudioBuffer<float> render (X0XProcessor& p, double, int block, int totalSamples)
{
    juce::AudioBuffer<float> out (2, totalSamples);
    juce::AudioBuffer<float> blk (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const int n = juce::jmin (block, totalSamples - pos);
        blk.clear();
        juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n);
        p.processBlock (view, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, view, ch, 0, n);
    }
    return out;
}

static float rmsOfSine (X0XProcessor& p, double sr, float freq, float ampDb)
{
    // Push a sine through the processor by using the sample slot: craft a sample and trigger it.
    const int len = (int) sr * 2;
    auto data = std::make_shared<SampleData>();
    data->mono.setSize (1, len);
    for (int i = 0; i < len; ++i) data->mono.setSample (0, i, juce::Decibels::decibelsToGain (ampDb) * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));
    data->sourceRate = sr;
    p.kit.setSample (BD, data);
    p.kit.setUseSample (BD, true);
    p.kit.setTrackParams (BD, 0.8f, 0.0f, 1.0f);
    p.neve.reset();
    p.kit.trigger (BD, 1.0f);

    const int block = 512;
    std::vector<float> mono ((size_t) len);
    juce::AudioBuffer<float> blk (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < len; pos += block)
    {
        const int n = juce::jmin (block, len - pos);
        blk.clear();
        juce::AudioBuffer<float> view (blk.getArrayOfWritePointers(), 2, n);
        p.processBlock (view, midi);
        std::copy (view.getReadPointer (0), view.getReadPointer (0) + n, mono.begin() + pos);
    }
    // RMS over the steady part (skip first 0.5 s)
    double acc = 0; int cnt = 0;
    for (int i = (int) sr / 2; i < len - (int) sr / 4; ++i) { acc += (double) mono[(size_t) i] * mono[(size_t) i]; ++cnt; }
    return (float) juce::Decibels::gainToDecibels (std::sqrt (acc / cnt));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 256;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    // --- 1. Render the default pattern -------------------------------------------
    {
        X0XProcessor p;
        p.setPlayHead (nullptr);
        p.prepareToPlay (sr, block);
        setParam (p, id::bpm, 124.0f);
        p.internalPlaying.store (true);
        const int bars = 4;
        const int total = (int) (sr * 60.0 / 124.0 * 4.0 * bars);
        auto buf = render (p, sr, block, total);

        bool finite = true; float peak = 0; double rms = 0;
        for (int i = 0; i < total; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = juce::jmax (peak, std::abs (v)); rms += (double) v * v;
        }
        rms = std::sqrt (rms / total);
        std::printf ("default pattern: peak %.2f dBFS, rms %.2f dBFS, latency %d samples\n",
                     juce::Decibels::gainToDecibels (peak), juce::Decibels::gainToDecibels ((float) rms), p.getLatencySamples());
        CHECK (finite, "render is finite (no NaN/inf)");
        CHECK (peak > 0.3f && peak < 1.0f, "default pattern peaks between -10 and 0 dBFS at default settings");
        CHECK (p.currentStep.load() >= 0, "sequencer is running");
        CHECK (writeWav (outDir.getChildFile ("x0x_demo_clean.wav"), buf, sr), "wrote x0x_demo_clean.wav");

        // Drive it hard: +25 dB in, LF boost, clip on, output trimmed
        setParam (p, id::inputGain, 9);     // +25 dB
        setParam (p, id::lfGain, 6.0f);
        setParam (p, id::midGain, 3.0f);
        setParam (p, id::midFreq, 1);
        setParam (p, id::clip, 1.0f);
        setParam (p, id::output, -8.0f);
        p.internalPlaying.store (false); render (p, sr, block, block);  // stop/restart to resync
        p.internalPlaying.store (true);
        auto hot = render (p, sr, block, total);
        float hotPeak = 0; bool hotFinite = true;
        for (int i = 0; i < total; ++i) { const float v = hot.getSample (0, i); if (! std::isfinite (v)) hotFinite = false; hotPeak = juce::jmax (hotPeak, std::abs (v)); }
        std::printf ("driven pattern: peak %.2f dBFS\n", juce::Decibels::gainToDecibels (hotPeak));
        CHECK (hotFinite, "driven render is finite");
        CHECK (hotPeak <= 1.0f, "clipper holds the 0 dBFS ceiling");
        CHECK (writeWav (outDir.getChildFile ("x0x_demo_driven.wav"), hot, sr), "wrote x0x_demo_driven.wav");

        // Every voice individually
        setParam (p, id::inputGain, 4); setParam (p, id::lfGain, 0); setParam (p, id::midGain, 0); setParam (p, id::clip, 0); setParam (p, id::output, 0);
        p.internalPlaying.store (false);
        p.pattern.clear();
        for (int t = 0; t < kNumTracks; ++t) p.pattern.set (t, t, 2);
        setParam (p, id::bpm, 60.0f);   // 16th steps = 250 ms apart: track t fires at t * 0.25 s
        render (p, sr, block, block);
        p.internalPlaying.store (true);
        auto solo = render (p, sr, block, (int) sr * 4);
        bool soloFinite = true; float soloPeak = 0;
        for (int i = 0; i < solo.getNumSamples(); ++i) { const float v = solo.getSample (0, i); if (! std::isfinite (v)) soloFinite = false; soloPeak = juce::jmax (soloPeak, std::abs (v)); }
        // check every hit produced energy
        bool allHit = true;
        for (int t = 0; t < kNumTracks; ++t)
        {
            const int start = (int) (sr * t * 0.25); double e = 0;
            for (int i = start; i < start + (int) (sr * 0.2) && i < solo.getNumSamples(); ++i) e += std::abs (solo.getSample (0, i));
            if (e < 1.0) { std::printf ("  voice %s silent?\n", kTrackNames[(size_t) t]); allHit = false; }
        }
        std::printf ("voices: peak %.2f dBFS\n", juce::Decibels::gainToDecibels (soloPeak));
        CHECK (soloFinite, "voice render is finite");
        CHECK (allHit, "all 12 voices produce sound");
        CHECK (writeWav (outDir.getChildFile ("x0x_demo_voices.wav"), solo, sr), "wrote x0x_demo_voices.wav");
    }

    // --- 2. 1073 EQ response ------------------------------------------------------
    {
        X0XProcessor p;
        p.prepareToPlay (sr, 512);
        p.internalPlaying.store (false);
        const float ref1k = rmsOfSine (p, sr, 1000.0f, -20.0f);
        const float ref15k = rmsOfSine (p, sr, 15000.0f, -20.0f);
        const float ref60 = rmsOfSine (p, sr, 60.0f, -20.0f);
        setParam (p, id::hfGain, 12.0f);
        const float hf15k = rmsOfSine (p, sr, 15000.0f, -20.0f);
        setParam (p, id::hfGain, 0.0f);
        setParam (p, id::lfGain, 10.0f); setParam (p, id::lfFreq, 1);
        const float lf60 = rmsOfSine (p, sr, 60.0f, -20.0f);
        setParam (p, id::lfGain, 0.0f);
        setParam (p, id::hpf, 4);   // 300 Hz
        const float hp60 = rmsOfSine (p, sr, 60.0f, -20.0f);
        setParam (p, id::hpf, 0);
        setParam (p, id::midGain, 12.0f); setParam (p, id::midFreq, 2);   // 1.6k
        const float mid1k6 = rmsOfSine (p, sr, 1600.0f, -20.0f);
        const float mid1k6ref = [&] { setParam (p, id::midGain, 0.0f); return rmsOfSine (p, sr, 1600.0f, -20.0f); }();
        std::printf ("EQ: flat 1k %.1f dB, 15k %.1f dB | HF+12 @15k: %+.1f dB | LF+10 @60: %+.1f dB | HPF300 @60: %+.1f dB | MID+12 @1.6k: %+.1f dB\n",
                     ref1k, ref15k, hf15k - ref15k, lf60 - ref60, hp60 - ref60, mid1k6 - mid1k6ref);
        CHECK (std::abs (ref1k - ref15k) < 1.5f, "flat response within 1.5 dB (1k vs 15k)");
        CHECK (hf15k - ref15k > 9.0f && hf15k - ref15k < 13.0f, "HF shelf +12 dB lifts 15 kHz by ~+9..13 dB");
        CHECK (lf60 - ref60 > 7.0f && lf60 - ref60 < 11.0f, "LF shelf +10 dB (60 Hz) lifts 60 Hz by ~+7..11 dB");
        CHECK (hp60 - ref60 < -30.0f, "HPF 300 Hz attenuates 60 Hz by > 30 dB (18 dB/oct)");
        CHECK (mid1k6 - mid1k6ref > 10.0f && mid1k6 - mid1k6ref < 13.0f, "Mid +12 dB @1.6k lifts 1.6 kHz by ~+10..13 dB");

        // Saturation grows with INPUT: measure gain compression of a -6 dBFS sine
        // (the drum bus sits 8 dB down, so a 0 dBFS sample reaches the input stage at about -8 dBFS)
        setParam (p, id::inputGain, 4);  const float lin = rmsOfSine (p, sr, 100.0f, 0.0f);
        setParam (p, id::inputGain, 8);  const float hot = rmsOfSine (p, sr, 100.0f, 0.0f);   // +20 dB in
        setParam (p, id::inputGain, 12); const float max = rmsOfSine (p, sr, 100.0f, 0.0f);   // +40 dB in
        std::printf ("drive: 0 dB -> %.1f dBFS, +20 dB -> %.1f dBFS, +40 dB -> %.1f dBFS\n", lin, hot, max);
        CHECK (hot - lin < 17.0f, "+20 dB of input gain is compressed by saturation (less than +17 dB louder)");
        CHECK (max < 9.0f, "+40 dB stays bounded by the stages (ceiling about +8 dBFS, trim with OUTPUT)");
    }

    std::printf (failures == 0 ? "\nALL CHECKS PASSED\n" : "\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
