#pragma once
#include <JuceHeader.h>
#include <array>

namespace x0x
{
constexpr int kNumTracks = 12;
constexpr int kNumSteps  = 16;

enum Track : int { BD = 0, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, MA };

inline const std::array<const char*, kNumTracks> kTrackIds    { "bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb", "cy", "oh", "ch", "ma" };
inline const std::array<const char*, kNumTracks> kTrackNames  { "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH", "MA" };
inline const std::array<const char*, kNumTracks> kTrackLong   { "Bass Drum", "Snare Drum", "Low Tom", "Mid Tom", "Hi Tom", "Rim Shot",
                                                                "Hand Clap", "Cowbell", "Cymbal", "Open Hat", "Closed Hat", "Maracas" };
// General-MIDI-ish default note mapping
inline const std::array<int, kNumTracks> kTrackNotes { 36, 38, 41, 45, 50, 37, 39, 56, 49, 46, 42, 70 };

namespace id
{
    inline juce::String level (int t) { return juce::String (kTrackIds[(size_t) t]) + "_level"; }
    inline juce::String tune  (int t) { return juce::String (kTrackIds[(size_t) t]) + "_tune"; }
    inline juce::String decay (int t) { return juce::String (kTrackIds[(size_t) t]) + "_decay"; }

    constexpr const char* bpm       = "bpm";
    constexpr const char* swing     = "swing";
    constexpr const char* length    = "length";
    constexpr const char* accent    = "accent";

    constexpr const char* inputGain = "n_input";     // stepped, 5 dB
    constexpr const char* hpf       = "n_hpf";       // off/50/80/160/300
    constexpr const char* lfFreq    = "n_lf_freq";   // 35/60/110/220
    constexpr const char* lfGain    = "n_lf_gain";   // ±16 dB
    constexpr const char* midFreq   = "n_mid_freq";  // 360/700/1k6/3k2/4k8/7k2
    constexpr const char* midGain   = "n_mid_gain";  // ±18 dB
    constexpr const char* hfGain    = "n_hf_gain";   // ±16 dB @ 12k
    constexpr const char* eqIn      = "n_eq_in";
    constexpr const char* clip      = "n_clip";
    constexpr const char* output    = "n_output";    // -inf..+10 dB
}

// 1073 stepped values -------------------------------------------------------
inline const juce::StringArray kInputGainLabels { "-20", "-15", "-10", "-5", "0", "+5", "+10", "+15", "+20", "+25", "+30", "+35", "+40" };
inline float inputGainDbForIndex (int i) { return -20.0f + 5.0f * (float) i; }

inline const juce::StringArray kHpfLabels    { "OFF", "50", "80", "160", "300" };
inline const std::array<float, 5> kHpfHz     { 0.0f, 50.0f, 80.0f, 160.0f, 300.0f };

inline const juce::StringArray kLfLabels     { "35", "60", "110", "220" };
inline const std::array<float, 4> kLfHz      { 35.0f, 60.0f, 110.0f, 220.0f };

inline const juce::StringArray kMidLabels    { "0.36", "0.7", "1.6", "3.2", "4.8", "7.2" };
inline const std::array<float, 6> kMidHz     { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
} // namespace x0x
