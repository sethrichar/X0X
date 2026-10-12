#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace x0x::dsp
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db)   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)    { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float semisToRatio (float s){ return std::exp2 (s / 12.0f); }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** Fast tanh approximation (Pade), accurate enough for saturation. */
inline float fastTanh (float x)
{
    if (x >  4.97f) return  1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

/** xorshift white noise, [-1, 1]. */
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    inline float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
};

/** Exponential decay envelope: value *= coef each sample. */
struct DecayEnv
{
    float v = 0.0f, coef = 0.0f;
    void start (float level, float tauSeconds, double sr)
    {
        v = level;
        coef = (tauSeconds <= 0.0f) ? 0.0f : std::exp (-1.0f / (float) (tauSeconds * sr));
    }
    inline float next() { const float out = v; v *= coef; return out; }
    bool active (float threshold = 1.0e-4f) const { return v > threshold; }
    void kill() { v = 0.0f; }
};

/** RBJ biquad, transposed direct form II. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setCoefs (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        const double n = 1.0 / A0;
        b0 = (float) (B0 * n); b1 = (float) (B1 * n); b2 = (float) (B2 * n);
        a1 = (float) (A1 * n); a2 = (float) (A2 * n);
    }

    void lowpass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        setCoefs ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        setCoefs ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double fs, double f, double q)   // constant 0 dB peak gain
    {
        f = std::min (f, fs * 0.49);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        setCoefs (al, 0.0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double gainDb)
    {
        f = std::min (f, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
        setCoefs (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double fs, double f, double slope, double gainDb)
    {
        f = std::min (f, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt (A) * al;
        setCoefs (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                  (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double fs, double f, double slope, double gainDb)
    {
        f = std::min (f, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt (A) * al;
        setCoefs (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                  (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

/** One-pole filters. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setLowpass (double fs, double fc) { a = (float) (1.0 - std::exp (-kTwoPi * fc / fs)); }
    inline float lp (float x) { z += a * (x - z); return z; }
    inline float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

/** Sine oscillator with phase accumulator. */
struct Sine
{
    float phase = 0.0f;
    inline float next (float freq, double sr)
    {
        const float out = std::sin (phase);
        phase += kTwoPi * freq / (float) sr;
        if (phase >= kTwoPi) phase -= kTwoPi;
        return out;
    }
    void reset (float p = 0.0f) { phase = p; }
};

/** PolyBLEP square oscillator (anti-aliased). */
struct Square
{
    float phase = 0.0f;   // 0..1
    inline static float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    inline float next (float freq, double sr)
    {
        const float dt = freq / (float) sr;
        float v = phase < 0.5f ? 1.0f : -1.0f;
        v += polyBlep (phase, dt);
        v -= polyBlep (std::fmod (phase + 0.5f, 1.0f), dt);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }
    void reset (float p = 0.0f) { phase = p; }
};
} // namespace x0x::dsp
