#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace x0x;

//==============================================================================
juce::String Pattern::toString() const
{
    juce::String s;
    s.preallocateBytes ((size_t) steps.size());
    for (auto& st : steps) s << (char) ('0' + st.load());
    return s;
}

void Pattern::fromString (const juce::String& s)
{
    const int n = juce::jmin ((int) steps.size(), s.length());
    for (int i = 0; i < (int) steps.size(); ++i)
    {
        const int c = i < n ? (s[i] - '0') : 0;
        steps[(size_t) i].store ((uint8_t) juce::jlimit (0, 2, c));
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout X0XProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)); };

    static const std::array<float, kNumTracks> defaultDecay { 0.45f, 0.4f, 0.4f, 0.4f, 0.4f, 0.5f, 0.4f, 0.4f, 0.5f, 0.4f, 0.3f, 0.4f };
    for (int t = 0; t < kNumTracks; ++t)
    {
        const String name (kTrackNames[(size_t) t]);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::level (t), 1 }, name + " Level",
                     NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::tune (t), 1 }, name + " Tune",
                     NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::decay (t), 1 }, name + " Decay",
                     NormalisableRange<float> (0.0f, 1.0f, 0.001f), defaultDecay[(size_t) t], AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    }

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::bpm, 1 }, "Tempo",
                 NormalisableRange<float> (40.0f, 300.0f, 0.1f), 120.0f,
                 AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::swing, 1 }, "Swing",
                 NormalisableRange<float> (50.0f, 75.0f, 0.1f), 50.0f,
                 AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 0) + "%"; })));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id::length, 1 }, "Length", 1, 16, 16));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::accent, 1 }, "Accent",
                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::inputGain, 1 }, "Input", kInputGainLabels, 4));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::hpf, 1 }, "HPF", kHpfLabels, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::lfFreq, 1 }, "LF Freq", kLfLabels, 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::lfGain, 1 }, "LF Gain",
                 NormalisableRange<float> (-16.0f, 16.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::midFreq, 1 }, "Mid Freq", kMidLabels, 2));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::midGain, 1 }, "Mid Gain",
                 NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::hfGain, 1 }, "HF Gain",
                 NormalisableRange<float> (-16.0f, 16.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::eqIn, 1 }, "EQ In", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::clip, 1 }, "Clip", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::output, 1 }, "Output",
                 NormalisableRange<float> (-60.0f, 10.0f, 0.1f, 2.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    return { p.begin(), p.end() };
}

//==============================================================================
X0XProcessor::X0XProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "X0X", createLayout())
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        pLevel[(size_t) t] = apvts.getRawParameterValue (id::level (t));
        pTune[(size_t) t]  = apvts.getRawParameterValue (id::tune (t));
        pDecay[(size_t) t] = apvts.getRawParameterValue (id::decay (t));
        useSampleUI[(size_t) t].store (false);
    }
    pBpm = apvts.getRawParameterValue (id::bpm);       pSwing = apvts.getRawParameterValue (id::swing);
    pLength = apvts.getRawParameterValue (id::length); pAccent = apvts.getRawParameterValue (id::accent);
    pInput = apvts.getRawParameterValue (id::inputGain); pHpf = apvts.getRawParameterValue (id::hpf);
    pLfF = apvts.getRawParameterValue (id::lfFreq);    pLfG = apvts.getRawParameterValue (id::lfGain);
    pMidF = apvts.getRawParameterValue (id::midFreq);  pMidG = apvts.getRawParameterValue (id::midGain);
    pHfG = apvts.getRawParameterValue (id::hfGain);    pEqIn = apvts.getRawParameterValue (id::eqIn);
    pClip = apvts.getRawParameterValue (id::clip);     pOut = apvts.getRawParameterValue (id::output);

    formatManager.registerBasicFormats();
    events.reserve (256);

    // A classic starting pattern so the thing makes noise immediately.
    static const char* defaultPattern =
        "1000100010001000"   // BD
        "0000100000001000"   // SD
        "0000000000000000"   // LT
        "0000000000000000"   // MT
        "0000000000000000"   // HT
        "0000000000000000"   // RS
        "0000100000001000"   // CP
        "0000000000000000"   // CB
        "0000000000000000"   // CY
        "0010001000100010"   // OH
        "2000200020002000"   // CH
        "0000000000000000";  // MA
    pattern.fromString (defaultPattern);
}

X0XProcessor::~X0XProcessor() = default;

bool X0XProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void X0XProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    kit.prepare (sampleRate);
    neve.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (neve.getLatencySamples());
    mono.assign ((size_t) samplesPerBlock, 0.0f);
    lastPpqEnd = -1.0;
    wasRunning = false;
    currentStep.store (-1);
}

void X0XProcessor::togglePlay()
{
    const bool now = ! internalPlaying.load();
    internalPlaying.store (now);
}

void X0XProcessor::stop() { internalPlaying.store (false); }

//==============================================================================
void X0XProcessor::addStepEvents (double ppqStart, double ppqEnd, double ppqPerSample, int numSamples, std::vector<Event>& evs)
{
    const int length = juce::jlimit (1, kNumSteps, (int) pLength->load());
    const double swingShift = ((double) pSwing->load() - 50.0) / 25.0 * (0.25 / 3.0);
    const float accentAmt = pAccent->load();
    const float normalVel = 1.0f - 0.45f * accentAmt;

    auto stepTime = [swingShift] (long long n) { return (double) n * 0.25 + ((n & 1) ? swingShift : 0.0); };

    // Re-sync when starting or when the host jumped (loop / relocate).
    if (lastPpqEnd < 0.0 || ppqStart < lastPpqEnd - 1.0e-6 || ppqStart > lastPpqEnd + 0.05)
    {
        nextStepN = (long long) std::floor (ppqStart / 0.25) - 1;
        if (nextStepN < 0) nextStepN = 0;
        while (stepTime (nextStepN) < ppqStart - 1.0e-9) ++nextStepN;
    }

    while (stepTime (nextStepN) < ppqEnd)
    {
        const double t = stepTime (nextStepN);
        int offset = (int) std::floor ((t - ppqStart) / ppqPerSample);
        offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), offset);
        const int step = (int) (nextStepN % length);
        for (int tr = 0; tr < kNumTracks; ++tr)
        {
            const uint8_t s = pattern.get (tr, step);
            if (s == 0) continue;
            evs.push_back ({ offset, tr, s == 2 ? 1.0f : normalVel });
        }
        ++nextStepN;
    }
    lastPpqEnd = ppqEnd;
}

void X0XProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const double sr = currentSampleRate;
    if (numSamples <= 0 || sr <= 0.0) return;
    if ((int) mono.size() < numSamples) mono.resize ((size_t) numSamples);
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);

    // --- sample hand-off -----------------------------------------------------
    if (samplesDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked())
        {
            for (int t = 0; t < kNumTracks; ++t)
            {
                kit.setSample (t, staged[(size_t) t]);
                kit.setUseSample (t, useSampleUI[(size_t) t].load());
            }
            samplesDirty.store (false);
        }
    }
    for (int t = 0; t < kNumTracks; ++t)
    {
        kit.setUseSample (t, useSampleUI[(size_t) t].load());
        kit.setTrackParams (t, pLevel[(size_t) t]->load(), pTune[(size_t) t]->load(), pDecay[(size_t) t]->load());
    }

    // --- 1073 params ----------------------------------------------------------
    Neve1073::Params np;
    np.inputDb   = inputGainDbForIndex ((int) pInput->load());
    np.hpfIndex  = (int) pHpf->load();
    np.lfIndex   = (int) pLfF->load();
    np.lfGainDb  = pLfG->load();
    np.midIndex  = (int) pMidF->load();
    np.midGainDb = pMidG->load();
    np.hfGainDb  = pHfG->load();
    np.eqIn      = pEqIn->load() > 0.5f;
    np.clip      = pClip->load() > 0.5f;
    np.outputDb  = pOut->load();
    neve.setParams (np);

    // --- transport -------------------------------------------------------------
    events.clear();
    double bpm = pBpm->load();
    bool hostPlaying = false;
    double hostPpq = 0.0;
    bool haveHostPpq = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { if (*b > 0.0) { bpm = *b; hostTempoInUse.store (true); } }
            else hostTempoInUse.store (false);
            hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; haveHostPpq = true; }
            else if (auto ts = pos->getTimeInSamples()) { hostPpq = (double) *ts / sr * bpm / 60.0; haveHostPpq = true; }
        }
        else hostTempoInUse.store (false);
    }
    displayBpm.store ((float) bpm);

    const double ppqPerSample = bpm / 60.0 / sr;
    const bool running = (hostPlaying && haveHostPpq) || internalPlaying.load();

    if (running)
    {
        double ppqStart;
        if (hostPlaying && haveHostPpq) { ppqStart = hostPpq; internalPpq = hostPpq; }
        else
        {
            if (! wasRunning) { internalPpq = 0.0; lastPpqEnd = -1.0; }
            ppqStart = internalPpq;
        }
        const double ppqEnd = ppqStart + ppqPerSample * numSamples;
        addStepEvents (ppqStart, ppqEnd, ppqPerSample, numSamples, events);
        internalPpq = ppqEnd;

        const int length = juce::jlimit (1, kNumSteps, (int) pLength->load());
        const long long n = (long long) std::floor (std::max (0.0, ppqEnd - 1.0e-9) / 0.25);
        currentStep.store ((int) (n % length));
    }
    else
    {
        lastPpqEnd = -1.0;
        currentStep.store (-1);
    }
    wasRunning = running;

    // --- MIDI + UI triggers -------------------------------------------------------
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn()) continue;
        const int note = m.getNoteNumber();
        for (int t = 0; t < kNumTracks; ++t)
            if (kTrackNotes[(size_t) t] == note || (note >= 60 && note < 60 + kNumTracks && note - 60 == t))
                events.push_back ({ juce::jlimit (0, numSamples - 1, meta.samplePosition), t, m.getFloatVelocity() });
    }
    if (const uint32_t mask = uiTriggerMask.exchange (0))
        for (int t = 0; t < kNumTracks; ++t)
            if (mask & (1u << (unsigned) t)) events.push_back ({ 0, t, 1.0f });

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });

    // --- render drums in segments ------------------------------------------------
    int pos = 0;
    for (const auto& e : events)
    {
        if (e.offset > pos) { kit.render (mono.data() + pos, e.offset - pos); pos = e.offset; }
        kit.trigger (e.track, e.velocity);
    }
    if (pos < numSamples) kit.render (mono.data() + pos, numSamples - pos);

    // --- 1073 output stage ----------------------------------------------------------
    neve.process (mono.data(), numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, mono.data(), numSamples);

    midi.clear();
}

//==============================================================================
bool X0XProcessor::isAudioFile (const juce::File& f)
{
    static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3", ".caf" };
    return f.existsAsFile() && exts.contains (f.getFileExtension().toLowerCase());
}

bool X0XProcessor::loadSampleForTrack (int track, const juce::File& file)
{
    if (track < 0 || track >= kNumTracks) return false;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;

    const juce::int64 cap = (juce::int64) (reader->sampleRate * 30.0);  // cap at 30 s
    const int len = (int) (reader->lengthInSamples < cap ? reader->lengthInSamples : cap);
    juce::AudioBuffer<float> tmp ((int) reader->numChannels, len);
    reader->read (&tmp, 0, len, 0, true, true);

    auto data = std::make_shared<SampleData>();
    data->mono.setSize (1, len);
    data->mono.clear();
    const float scale = 1.0f / (float) juce::jmax (1u, reader->numChannels);
    for (int ch = 0; ch < (int) reader->numChannels; ++ch) data->mono.addFrom (0, 0, tmp, ch, 0, len, scale);
    data->sourceRate = reader->sampleRate;
    data->path = file.getFullPathName();
    data->name = file.getFileNameWithoutExtension();

    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        if (! samplesDirty.load()) retired.clear();
        if (staged[(size_t) track]) retired.push_back (staged[(size_t) track]);
        staged[(size_t) track] = std::move (data);
        useSampleUI[(size_t) track].store (true);
        samplesDirty.store (true);
    }
    return true;
}

void X0XProcessor::clearSampleForTrack (int track)
{
    if (track < 0 || track >= kNumTracks) return;
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    if (! samplesDirty.load()) retired.clear();
    if (staged[(size_t) track]) retired.push_back (staged[(size_t) track]);
    staged[(size_t) track] = nullptr;
    useSampleUI[(size_t) track].store (false);
    samplesDirty.store (true);
}

void X0XProcessor::setUseSample (int track, bool b)
{
    if (track < 0 || track >= kNumTracks) return;
    useSampleUI[(size_t) track].store (b && staged[(size_t) track] != nullptr);
}

juce::String X0XProcessor::getSampleName (int track) const
{
    if (track < 0 || track >= kNumTracks || staged[(size_t) track] == nullptr) return {};
    return staged[(size_t) track]->name;
}

int X0XProcessor::loadKitFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return 0;
    // Keyword lists: longer / more specific first. Two-letter codes must match as whole tokens.
    static const std::array<juce::StringArray, kNumTracks> keywords {
        juce::StringArray { "kick", "bassdrum", "bass drum", "bd" },
        juce::StringArray { "snare", "sd", "sn" },
        juce::StringArray { "low tom", "lowtom", "ltom", "tom low", "tom1", "tom 1", "lt" },
        juce::StringArray { "mid tom", "midtom", "mtom", "tom mid", "tom2", "tom 2", "mt" },
        juce::StringArray { "hi tom", "high tom", "hitom", "hightom", "htom", "tom hi", "tom3", "tom 3", "ht" },
        juce::StringArray { "rimshot", "rim shot", "rim", "rs" },
        juce::StringArray { "clap", "cp" },
        juce::StringArray { "cowbell", "bell", "cb" },
        juce::StringArray { "cymbal", "crash", "ride", "cy" },
        juce::StringArray { "open hat", "openhat", "open hihat", "open hi hat", "ohh", "oh", "open" },
        juce::StringArray { "closed hat", "closedhat", "closed hihat", "closed hi hat", "chh", "ch", "closed", "hihat", "hi hat", "hat" },
        juce::StringArray { "maraca", "maracas", "shaker", "ma" } };

    juce::Array<juce::File> files;
    for (const auto& f : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
        if (isAudioFile (f.getFile())) files.add (f.getFile());
    files.sort();

    auto tokens = [] (const juce::String& name)
    {
        juce::StringArray t;
        t.addTokens (name.toLowerCase().replaceCharacters ("_-.()[]", "       "), " ", "");
        t.removeEmptyStrings();
        return t;
    };

    int matched = 0;
    std::array<bool, kNumTracks> done {};
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (const auto& kw : keywords[(size_t) t])
        {
            if (done[(size_t) t]) break;
            for (const auto& f : files)
            {
                const auto lower = f.getFileNameWithoutExtension().toLowerCase();
                bool hit = false;
                if (kw.length() <= 2) hit = tokens (lower).contains (kw);
                else                  hit = lower.contains (kw);
                if (hit && loadSampleForTrack (t, f)) { done[(size_t) t] = true; ++matched; break; }
            }
        }
    }
    return matched;
}

//==============================================================================
void X0XProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", pattern.toString(), nullptr);
    juce::ValueTree samples ("samples");
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        for (int t = 0; t < kNumTracks; ++t)
            if (staged[(size_t) t] != nullptr)
            {
                juce::ValueTree s ("s");
                s.setProperty ("track", t, nullptr);
                s.setProperty ("path", staged[(size_t) t]->path, nullptr);
                s.setProperty ("use", useSampleUI[(size_t) t].load(), nullptr);
                samples.appendChild (s, nullptr);
            }
    }
    state.appendChild (samples, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void X0XProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (tree.hasProperty ("pattern")) pattern.fromString (tree.getProperty ("pattern").toString());

    auto samples = tree.getChildWithName ("samples");
    tree.removeChild (samples, nullptr);
    apvts.replaceState (tree);

    if (samples.isValid())
    {
        std::vector<std::tuple<int, juce::String, bool>> toLoad;
        for (const auto& s : samples)
            toLoad.emplace_back ((int) s.getProperty ("track"), s.getProperty ("path").toString(), (bool) s.getProperty ("use", true));
        juce::WeakReference<X0XProcessor> weak (this);
        juce::MessageManager::callAsync ([weak, toLoad]
        {
            if (auto* self = weak.get())
                for (const auto& [track, path, use] : toLoad)
                    if (self->loadSampleForTrack (track, juce::File (path))) self->setUseSample (track, use);
        });
    }
}

juce::AudioProcessorEditor* X0XProcessor::createEditor() { return new X0XEditor (*this); }

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new x0x::X0XProcessor(); }
