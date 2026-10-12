#include "DrumSynth.h"

using namespace x0x;
using namespace x0x::dsp;

namespace
{
// Helper: exponential mapping of normalized decay to a time range
inline float mapTau (float d, float minT, float maxT) { return minT * std::pow (maxT / minT, clampf (d, 0.0f, 1.0f)); }

//==============================================================================
class KickVoice final : public Voice
{
public:
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        f0 = 48.0f * semisToRatio (tune * 7.0f);
        amp.start (vel, mapTau (decay, 0.10f, 2.6f), sr);
        pitchEnv.start (1.0f, 0.016f, sr);
        clickEnv.start (vel * 0.6f, 0.0025f, sr);
        osc.reset (0.0f);
        lp.setLowpass (sr, 2500.0);
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float p = pitchEnv.next();
        const float f = f0 * (1.0f + 2.4f * p);
        const float body = fastTanh (1.6f * osc.next (f, sr)) * 0.92f;
        const float click = lp.lp (noise.next()) * clickEnv.next();
        const float a = amp.next();
        if (! amp.active (1.0e-5f)) active = false;
        return applyChoke ((body + click) * a);
    }
    bool isActive() const override { return active; }
private:
    Sine osc; Noise noise; OnePole lp;
    DecayEnv amp, pitchEnv, clickEnv;
    float f0 = 48.0f; bool active = false;
};

//==============================================================================
class SnareVoice final : public Voice
{
public:
    void prepare (double s) override { Voice::prepare (s); bp.bandpass (sr, 2200.0, 0.55); hp.highpass (sr, 700.0, 0.7); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        ratio = semisToRatio (tune * 6.0f);
        toneA.start (vel * 0.55f, 0.11f, sr);
        toneB.start (vel * 0.35f, 0.07f, sr);
        noiseEnv.start (vel * 0.9f, mapTau (decay, 0.055f, 0.42f), sr);
        pitchEnv.start (1.0f, 0.008f, sr);
        oscA.reset(); oscB.reset();
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float pe = 1.0f + 0.5f * pitchEnv.next();
        const float t = oscA.next (180.0f * ratio * pe, sr) * toneA.next()
                      + oscB.next (330.0f * ratio * pe, sr) * toneB.next();
        const float n = hp.process (bp.process (noise.next())) * noiseEnv.next();
        if (! noiseEnv.active (1.0e-5f) && ! toneA.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (1.3f * (t + n * 1.6f)));
    }
    bool isActive() const override { return active; }
private:
    Sine oscA, oscB; Noise noise; Biquad bp, hp;
    DecayEnv toneA, toneB, noiseEnv, pitchEnv;
    float ratio = 1.0f; bool active = false;
};

//==============================================================================
class TomVoice final : public Voice
{
public:
    explicit TomVoice (float baseHz) : f0base (baseHz) {}
    void prepare (double s) override { Voice::prepare (s); lp.setLowpass (sr, 1400.0); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        f0 = f0base * semisToRatio (tune * 7.0f);
        amp.start (vel, mapTau (decay, 0.14f, 0.75f), sr);
        pitchEnv.start (1.0f, 0.035f, sr);
        noiseEnv.start (vel * 0.35f, 0.06f, sr);
        osc.reset();
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float f = f0 * (1.0f + 0.7f * pitchEnv.next());
        const float body = fastTanh (1.4f * osc.next (f, sr));
        const float n = lp.lp (noise.next()) * noiseEnv.next();
        const float a = amp.next();
        if (! amp.active (1.0e-5f)) active = false;
        return applyChoke (body * a + n);
    }
    bool isActive() const override { return active; }
private:
    Sine osc; Noise noise; OnePole lp;
    DecayEnv amp, pitchEnv, noiseEnv;
    float f0base, f0 = 100.0f; bool active = false;
};

//==============================================================================
class RimVoice final : public Voice
{
public:
    void prepare (double s) override { Voice::prepare (s); hp.highpass (sr, 2500.0, 0.7); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        ratio = semisToRatio (tune * 6.0f);
        const float dscale = mapTau (decay, 0.5f, 2.2f);
        a.start (vel * 0.9f, 0.006f * dscale, sr);
        b.start (vel * 0.7f, 0.013f * dscale, sr);
        n.start (vel * 0.5f, 0.004f, sr);
        oscA.reset(); oscB.reset();
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float x = oscA.next (1720.0f * ratio, sr) * a.next()
                      + oscB.next (480.0f * ratio, sr) * b.next()
                      + hp.process (noise.next()) * n.next();
        if (! b.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (2.8f * x) * 0.8f);
    }
    bool isActive() const override { return active; }
private:
    Sine oscA, oscB; Noise noise; Biquad hp;
    DecayEnv a, b, n; float ratio = 1.0f; bool active = false;
};

//==============================================================================
class ClapVoice final : public Voice
{
public:
    void prepare (double s) override { Voice::prepare (s); bp.bandpass (sr, 1100.0, 1.1); hp.highpass (sr, 500.0, 0.7); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        bp.bandpass (sr, 1100.0 * semisToRatio (tune * 5.0f), 1.1);
        velocity = vel;
        tailTau = mapTau (decay, 0.07f, 0.38f);
        samplesIntoHit = 0; burst = 0;
        burstEnv.start (vel, 0.0065f, sr);
        tail.v = 0.0f;
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        // Re-fire the burst every ~10 ms, 4 times, then release the tail.
        const int spacing = (int) (0.0105 * sr);
        if (burst < 3 && samplesIntoHit >= spacing * (burst + 1))
        {
            ++burst;
            burstEnv.start (velocity * (1.0f - 0.1f * (float) burst), 0.0065f, sr);
            if (burst == 3) tail.start (velocity * 0.75f, tailTau, sr);
        }
        ++samplesIntoHit;
        const float env = burstEnv.next() + tail.next();
        const float x = hp.process (bp.process (noise.next())) * env * 2.2f;
        if (burst >= 3 && ! tail.active (1.0e-5f) && ! burstEnv.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (x));
    }
    bool isActive() const override { return active; }
private:
    Noise noise; Biquad bp, hp;
    DecayEnv burstEnv, tail;
    float velocity = 1.0f, tailTau = 0.1f; int samplesIntoHit = 0, burst = 0; bool active = false;
};

//==============================================================================
class CowbellVoice final : public Voice
{
public:
    void prepare (double s) override { Voice::prepare (s); bp.bandpass (sr, 2640.0, 1.0); lp.setLowpass (sr, 1800.0); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        ratio = semisToRatio (tune * 5.0f);
        pluck.start (vel * 0.9f, 0.015f, sr);
        tail.start (vel * 0.45f, mapTau (decay, 0.12f, 0.6f), sr);
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float m = 0.5f * (a.next (540.0f * ratio, sr) + b.next (800.0f * ratio, sr));
        const float env = pluck.next() + tail.next();
        const float x = (bp.process (m) * 1.4f + lp.lp (m) * 0.5f) * env;
        if (! tail.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (1.5f * x));
    }
    bool isActive() const override { return active; }
private:
    Square a, b; Biquad bp; OnePole lp; DecayEnv pluck, tail; float ratio = 1.0f; bool active = false;
};

//==============================================================================
/** Shared 808 "metal" source: six square waves. */
struct MetalSource
{
    static constexpr float freqs[6] = { 205.3f, 369.6f, 304.4f, 522.7f, 800.0f, 540.0f };
    Square osc[6];
    inline float next (float ratio, double sr)
    {
        float s = 0.0f;
        for (int i = 0; i < 6; ++i) s += osc[i].next (freqs[i] * ratio, sr);
        return s * (1.0f / 6.0f);
    }
};

class HatVoice final : public Voice
{
public:
    HatVoice (float minT, float maxT) : tMin (minT), tMax (maxT) {}
    void prepare (double s) override
    {
        Voice::prepare (s);
        bp.bandpass (sr, 3400.0, 1.1);
        hp.highpass (sr, 7000.0, 0.7);
        hp2.highpass (sr, 7000.0, 0.7);
    }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        ratio = semisToRatio (tune * 5.0f);
        amp.start (vel, mapTau (decay, tMin, tMax), sr);
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float m = metal.next (ratio, sr);
        const float x = hp2.process (hp.process (m)) * 1.6f + bp.process (m) * 0.4f;
        const float a = amp.next();
        if (! amp.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (2.0f * x) * a);
    }
    bool isActive() const override { return active; }
private:
    MetalSource metal; Biquad bp, hp, hp2; DecayEnv amp; float ratio = 1.0f, tMin, tMax; bool active = false;
};

class CymbalVoice final : public Voice
{
public:
    void prepare (double s) override
    {
        Voice::prepare (s);
        bpLow.bandpass (sr, 3400.0, 1.4);
        hp.highpass (sr, 7000.0, 0.7);
        bpHi.bandpass (sr, 9000.0, 0.8);
    }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        ratio = semisToRatio (tune * 4.0f);
        ampLong.start (vel * 0.7f, mapTau (decay, 0.5f, 3.2f), sr);
        ampShort.start (vel, mapTau (decay, 0.15f, 1.0f), sr);
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        const float m = metal.next (ratio, sr);
        const float x = fastTanh (2.5f * bpLow.process (m)) * ampLong.next()
                      + fastTanh (2.0f * bpHi.process (hp.process (m))) * ampShort.next();
        if (! ampLong.active (1.0e-5f)) active = false;
        return applyChoke (x);
    }
    bool isActive() const override { return active; }
private:
    MetalSource metal; Biquad bpLow, hp, bpHi; DecayEnv ampLong, ampShort; float ratio = 1.0f; bool active = false;
};

//==============================================================================
class MaracaVoice final : public Voice
{
public:
    void prepare (double s) override { Voice::prepare (s); hp.highpass (sr, 4000.0, 0.7); bp.bandpass (sr, 7000.0, 0.7); }
    void trigger (float vel, float tune, float decay) override
    {
        resetChoke();
        bp.bandpass (sr, 7000.0 * semisToRatio (tune * 4.0f), 0.7);
        amp.start (vel, mapTau (decay, 0.018f, 0.07f), sr);
        attack = 0.0f;
        attackInc = 1.0f / (0.004f * (float) sr);
        active = true;
    }
    float process() override
    {
        if (! active) return 0.0f;
        attack = std::min (1.0f, attack + attackInc);
        const float x = bp.process (hp.process (noise.next())) * amp.next() * attack * 2.0f;
        if (! amp.active (1.0e-5f)) active = false;
        return applyChoke (fastTanh (x));
    }
    bool isActive() const override { return active; }
private:
    Noise noise; Biquad hp, bp; DecayEnv amp; float attack = 0.0f, attackInc = 1.0f; bool active = false;
};
} // namespace

//==============================================================================
std::unique_ptr<Voice> x0x::createSynthVoice (int track)
{
    switch (track)
    {
        case BD: return std::make_unique<KickVoice>();
        case SD: return std::make_unique<SnareVoice>();
        case LT: return std::make_unique<TomVoice> (80.0f);
        case MT: return std::make_unique<TomVoice> (118.0f);
        case HT: return std::make_unique<TomVoice> (165.0f);
        case RS: return std::make_unique<RimVoice>();
        case CP: return std::make_unique<ClapVoice>();
        case CB: return std::make_unique<CowbellVoice>();
        case CY: return std::make_unique<CymbalVoice>();
        case OH: return std::make_unique<HatVoice> (0.14f, 0.8f);
        case CH: return std::make_unique<HatVoice> (0.018f, 0.09f);
        case MA: return std::make_unique<MaracaVoice>();
        default: return std::make_unique<KickVoice>();
    }
}

//==============================================================================
void SampleVoice::trigger (float vel, float tune, float decay)
{
    if (sample == nullptr) return;
    resetChoke();
    pos = 0.0;
    rate = (sample->sourceRate / sr) * (double) semisToRatio (tune * 12.0f);
    gain = vel;
    if (decay >= 0.995f) env.start (1.0f, 0.0f, sr), env.coef = 1.0f;     // natural length
    else                 env.start (1.0f, mapTau (decay, 0.04f, 4.0f), sr);
}

float SampleVoice::process()
{
    if (sample == nullptr || pos < 0.0) return 0.0f;
    const auto& buf = sample->mono;
    const int n = buf.getNumSamples();
    const int i = (int) pos;
    if (i >= n - 1) { pos = -1.0; return 0.0f; }
    const float* d = buf.getReadPointer (0);
    // 4-point Hermite interpolation
    const float frac = (float) (pos - (double) i);
    const float xm1 = i > 0 ? d[i - 1] : d[i];
    const float x0 = d[i], x1 = d[i + 1], x2 = (i + 2 < n) ? d[i + 2] : x1;
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float bN = w + a;
    const float out = (((a * frac) - bN) * frac + c) * frac + x0;
    pos += rate;
    const float e = env.next();
    if (! env.active (1.0e-5f)) pos = -1.0;
    return applyChoke (out * e * gain);
}

//==============================================================================
DrumKit::DrumKit()
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        synths[(size_t) t] = createSynthVoice (t);
        level[(size_t) t] = 0.8f; tune[(size_t) t] = 0.0f; decay[(size_t) t] = 0.5f;
        hitFlash[(size_t) t].store (0.0f);
    }
}

void DrumKit::prepare (double sampleRate)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        synths[(size_t) t]->prepare (sampleRate);
        samples[(size_t) t].prepare (sampleRate);
    }
}

void DrumKit::reset()
{
    for (int t = 0; t < kNumTracks; ++t) { synths[(size_t) t]->choke(); samples[(size_t) t].choke(); }
}

void DrumKit::setTrackParams (int track, float lv, float tn, float dc)
{
    level[(size_t) track] = lv; tune[(size_t) track] = tn; decay[(size_t) track] = dc;
}

void DrumKit::setSample (int track, SamplePtr s)
{
    samples[(size_t) track].setSample (std::move (s));
}

void DrumKit::trigger (int track, float velocity)
{
    if (track < 0 || track >= kNumTracks) return;
    const auto t = (size_t) track;
    hitFlash[t].store (velocity);

    // 808 behaviour: closed hat chokes the open hat.
    if (track == CH) { synths[OH]->choke(); samples[OH].choke(); }

    if (useSample[t] && samples[t].hasSample()) samples[t].trigger (velocity, tune[t], decay[t]);
    else                                        synths[t]->trigger (velocity, tune[t], decay[t]);
}

void DrumKit::render (float* out, int numSamples)
{
    // Bus headroom so a full default pattern peaks around -4 dBFS before the 1073 stage.
    constexpr float kBusGain = 0.4f;
    for (size_t t = 0; t < (size_t) kNumTracks; ++t)
    {
        // Level: 0.8 = unity, exponential-ish taper so the bottom of the knob is quiet.
        const float g = level[t] <= 0.001f ? 0.0f : std::pow (level[t] / 0.8f, 1.8f) * kBusGain;
        if (g <= 0.0f) continue;

        if (synths[t]->isActive())
        {
            Voice& v = *synths[t];
            for (int i = 0; i < numSamples; ++i) out[i] += v.process() * g;
        }
        if (samples[t].isActive())
        {
            SampleVoice& v = samples[t];
            for (int i = 0; i < numSamples; ++i) out[i] += v.process() * g;
        }
    }
}
