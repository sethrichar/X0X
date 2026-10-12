#include "PluginEditor.h"

using namespace x0x;

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& cap, int numSteps)
    : caption (cap), steps (numSteps)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (160);
    slider.setDoubleClickReturnValue (true, param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
    slider.addListener (this);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (showCaption) r.removeFromBottom (15);
    const int m = steps > 0 ? 5 : 1;
    slider.setBounds (r.reduced (m));
}

void Knob::sliderValueChanged (juce::Slider*) { repaint(); }

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto capArea = showCaption ? r.removeFromBottom (15) : juce::Rectangle<int>();

    if (steps > 1)
    {
        auto sb = slider.getBounds().toFloat();
        const float d = juce::jmin (sb.getWidth(), sb.getHeight());
        const auto c = sb.getCentre();
        const float rad = d * 0.5f + 3.0f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        for (int i = 0; i < steps; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / (float) (steps - 1);
            g.setColour (colours::textDim);
            g.fillEllipse (c.x + std::sin (a) * rad - 1.0f, c.y - std::cos (a) * rad - 1.0f, 2.0f, 2.0f);
        }
    }

    if (showCaption)
    {
        juce::String txt = caption;
        if ((dragging || slider.isMouseOverOrDragging()) && param != nullptr) txt = param->getCurrentValueAsText();
        g.setColour (dragging ? colours::textHi : colours::text);
        g.setFont (uiFont (10.0f, true));
        g.drawText (txt.toUpperCase(), capArea, juce::Justification::centred);
    }
}

//==============================================================================
juce::Rectangle<float> StepGrid::cellBounds (int track, int step) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float cw = (w - 3.0f * kGroupGap) / (float) kNumSteps;
    const float ch = h / (float) kNumTracks;
    const float x = (float) step * cw + (float) (step / 4) * kGroupGap;
    return { x, (float) track * ch, cw, ch };
}

int StepGrid::trackAtY (int y) const
{
    if (y < 0 || y >= getHeight()) return -1;
    return juce::jlimit (0, kNumTracks - 1, (int) (y * kNumTracks / getHeight()));
}

std::pair<int, int> StepGrid::cellAt (juce::Point<int> p) const
{
    const int t = trackAtY (p.y);
    if (t < 0 || p.x < 0 || p.x >= getWidth()) return { -1, -1 };
    for (int s = 0; s < kNumSteps; ++s)
        if (cellBounds (t, s).expanded (kGroupGap * 0.5f, 0.0f).contains (p.toFloat())) return { t, s };
    return { -1, -1 };
}

void StepGrid::paint (juce::Graphics& g)
{
    const int cur = proc.currentStep.load();
    const int length = juce::jlimit (1, kNumSteps, (int) *proc.apvts.getRawParameterValue (id::length));

    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kNumSteps; ++s)
        {
            auto cell = cellBounds (t, s);
            const float d = juce::jmin (cell.getWidth(), cell.getHeight()) * 0.5f;
            auto light = juce::Rectangle<float> (d, d).withCentre (cell.getCentre());
            const uint8_t v = proc.pattern.get (t, s);
            const bool outside = s >= length;
            float intensity = v == 2 ? 1.0f : v == 1 ? 0.62f : 0.0f;
            if (s == cur && v > 0) intensity = 1.0f;
            if (outside) { g.setOpacity (0.35f); }
            if (v == 0 && s == cur) { drawLight (g, light, 0.0f); g.setColour (colours::lightOn.withAlpha (0.35f)); g.drawEllipse (light.reduced (0.5f), 1.0f); }
            else drawLight (g, light, intensity);
            g.setOpacity (1.0f);
        }

    // playhead column hairline
    if (cur >= 0)
    {
        auto c = cellBounds (0, cur);
        g.setColour (colours::lightOn.withAlpha (0.06f));
        g.fillRect (c.getX(), 0.0f, c.getWidth(), (float) getHeight());
    }
}

void StepGrid::applyPaint (int track, int step)
{
    if (track < 0 || step < 0 || (track == lastTrack && step == lastStep)) return;
    proc.pattern.set (track, step, paintValue);
    lastTrack = track; lastStep = step;
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    auto [t, s] = cellAt (e.getPosition());
    if (t < 0) return;
    const uint8_t v = proc.pattern.get (t, s);
    if (e.mods.isPopupMenu())        paintValue = 0;
    else if (e.mods.isShiftDown())   paintValue = v == 2 ? 1 : 2;
    else                             paintValue = (uint8_t) ((v + 1) % 3);
    lastTrack = lastStep = -1;
    applyPaint (t, s);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto [t, s] = cellAt (e.getPosition());
    applyPaint (t, s);
}

//==============================================================================
void TrackLabel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto light = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 6.0f, r.getCentreY() });
    drawLight (g, light, flash, false);

    const bool hover = isMouseOver();
    const bool sample = proc.getUseSample (track) && proc.trackHasSample (track);
    g.setColour (hover ? colours::textHi : colours::text);
    g.setFont (uiFont (12.0f, true));
    g.drawText (kTrackNames[(size_t) track], r.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    if (sample)
    {
        g.setColour (colours::textHi);
        g.fillRect (r.getX() + 40.0f, r.getCentreY() - 1.5f, 3.0f, 3.0f);
    }
}

void TrackLabel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) { proc.triggerFromUI (track); flash = 1.0f; repaint(); return; }

    juce::PopupMenu m;
    const bool has = proc.trackHasSample (track);
    m.addSectionHeader (juce::String (kTrackLong[(size_t) track]) + (has ? "  -  " + proc.getSampleName (track) : juce::String()));
    m.addItem (1, "Load sample...");
    m.addItem (2, "Use sample", has, has && proc.getUseSample (track));
    m.addItem (3, "Use synth", true, ! (has && proc.getUseSample (track)));
    m.addSeparator();
    m.addItem (4, "Clear sample", has);
    juce::Component::SafePointer<TrackLabel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& p = safe->proc; const int t = safe->track;
        switch (r)
        {
            case 1: if (safe->onLoadSample) safe->onLoadSample (t); break;
            case 2: p.setUseSample (t, true); break;
            case 3: p.setUseSample (t, false); break;
            case 4: p.clearSampleForTrack (t); break;
            default: break;
        }
        safe->repaint();
    });
}

//==============================================================================
void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::lightOff);
    g.fillRoundedRectangle (r, 1.5f);
    const float db = juce::Decibels::gainToDecibels (level, -48.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    hold = juce::jmax (frac, hold * 0.96f);
    auto fill = r.withTop (r.getBottom() - r.getHeight() * frac);
    g.setColour (colours::lightOn.withAlpha (0.85f));
    g.fillRoundedRectangle (fill, 1.5f);
    if (hold > 0.01f)
    {
        g.setColour (colours::lightOn);
        g.fillRect (r.getX(), r.getBottom() - r.getHeight() * hold - 1.0f, r.getWidth(), 1.5f);
    }
}

//==============================================================================
X0XEditor::X0XEditor (X0XProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      tempoKnob (p.apvts, id::bpm, "Tempo"), swingKnob (p.apvts, id::swing, "Swing"),
      accentKnob (p.apvts, id::accent, "Accent"), lengthKnob (p.apvts, id::length, "Length", 16),
      grid (p),
      inputKnob (p.apvts, id::inputGain, "Input", 13), hpfKnob (p.apvts, id::hpf, "HPF", 5),
      lfFreqKnob (p.apvts, id::lfFreq, "LF Hz", 4), lfGainKnob (p.apvts, id::lfGain, "LF dB"),
      midFreqKnob (p.apvts, id::midFreq, "Mid kHz", 6), midGainKnob (p.apvts, id::midGain, "Mid dB"),
      hfGainKnob (p.apvts, id::hfGain, "12k dB"), outputKnob (p.apvts, id::output, "Output")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    // header
    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { proc.togglePlay(); };
    content.addAndMakeVisible (playButton);
    for (auto* k : { &tempoKnob, &swingKnob, &accentKnob, &lengthKnob }) content.addAndMakeVisible (*k);
    kitButton.onClick = [this] { openKitChooser(); };
    kitButton.setTooltip ("Load a folder of samples; tracks are matched by file name (kick, snare, clap...)");
    content.addAndMakeVisible (kitButton);
    clearButton.onClick = [this] { proc.pattern.clear(); grid.repaint(); };
    content.addAndMakeVisible (clearButton);

    // grid
    content.addAndMakeVisible (grid);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& lab = labels[(size_t) t];
        lab = std::make_unique<TrackLabel> (proc, t);
        lab->onLoadSample = [this] (int track) { openSampleChooser (track); };
        content.addAndMakeVisible (*lab);

        levelKnobs[(size_t) t] = std::make_unique<Knob> (proc.apvts, id::level (t), "Level");
        tuneKnobs[(size_t) t]  = std::make_unique<Knob> (proc.apvts, id::tune (t), "Tune");
        decayKnobs[(size_t) t] = std::make_unique<Knob> (proc.apvts, id::decay (t), "Decay");
        for (auto* k : { levelKnobs[(size_t) t].get(), tuneKnobs[(size_t) t].get(), decayKnobs[(size_t) t].get() })
        {
            k->setCaptionVisible (false);
            k->getSlider().setMouseDragSensitivity (120);
            content.addAndMakeVisible (*k);
        }
    }

    // 1073 strip
    for (auto* k : { &inputKnob, &hpfKnob, &lfFreqKnob, &lfGainKnob, &midFreqKnob, &midGainKnob, &hfGainKnob, &outputKnob })
        content.addAndMakeVisible (*k);
    eqAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::eqIn, eqButton);
    clipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::clip, clipButton);
    content.addAndMakeVisible (eqButton);
    content.addAndMakeVisible (clipButton);
    content.addAndMakeVisible (meter);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (600, 384, 2400, 1536);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setSize (kDesignW, kDesignH);
    layoutContent();
    startTimerHz (30);
}

X0XEditor::~X0XEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void X0XEditor::layoutContent()
{
    content.setBounds (0, 0, kDesignW, kDesignH);
    const int M = 20;

    // --- header ----------------------------------------------------------------
    headerBounds = { 0, 0, kDesignW, 72 };
    int x = 150;
    playButton.setBounds (x, 20, 70, 32); x += 90;
    tempoKnob.setBounds (x, 8, 60, 60);   x += 70;
    swingKnob.setBounds (x, 8, 60, 60);   x += 70;
    accentKnob.setBounds (x, 8, 60, 60);  x += 70;
    lengthKnob.setBounds (x, 8, 60, 60);
    clearButton.setBounds (kDesignW - M - 70, 22, 70, 28);
    kitButton.setBounds (kDesignW - M - 70 - 10 - 90, 22, 90, 28);

    // --- grid ------------------------------------------------------------------
    const int labelW = 60;
    const int knobColW = 44;
    const int gridTop = 100, rowH = 28;
    const int gridH = rowH * kNumTracks;
    const int gridLeft = M + labelW + 4;
    const int gridRight = kDesignW - M - knobColW * 3 - 14;
    gridHeaderBounds = { gridLeft, gridTop - 20, kDesignW - M - gridLeft, 18 };
    grid.setBounds (gridLeft, gridTop, gridRight - gridLeft, gridH);
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int y = gridTop + t * rowH;
        labels[(size_t) t]->setBounds (M, y, labelW, rowH);
        int kx = gridRight + 14;
        levelKnobs[(size_t) t]->setBounds (kx, y + 1, knobColW, rowH - 2); kx += knobColW;
        tuneKnobs[(size_t) t]->setBounds (kx, y + 1, knobColW, rowH - 2);  kx += knobColW;
        decayKnobs[(size_t) t]->setBounds (kx, y + 1, knobColW, rowH - 2);
    }

    // --- 1073 strip --------------------------------------------------------------
    stripBounds = { 0, gridTop + gridH + 26, kDesignW, kDesignH - (gridTop + gridH + 26) };
    const int ky = stripBounds.getY() + 36, kd = 92;
    x = 118;
    inputKnob.setBounds (x, ky, kd, kd);   x += 104;
    hpfKnob.setBounds (x, ky + 10, kd - 20, kd - 20);  x += 84;
    lfFreqKnob.setBounds (x, ky + 10, kd - 20, kd - 20); x += 76;
    lfGainKnob.setBounds (x, ky, kd, kd);  x += 96;
    midFreqKnob.setBounds (x, ky + 10, kd - 20, kd - 20); x += 76;
    midGainKnob.setBounds (x, ky, kd, kd); x += 96;
    hfGainKnob.setBounds (x, ky, kd, kd);  x += 108;
    outputKnob.setBounds (x, ky, kd, kd);  x += 108;
    eqButton.setBounds (x, ky + 12, 70, 24);
    clipButton.setBounds (x, ky + 44, 70, 24);
    clipLedBounds = { x + 70, ky + 44, 24, 24 };
    meter.setBounds (kDesignW - M - 6, ky, 6, kd);
}

void X0XEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) kDesignW, (float) getHeight() / (float) kDesignH);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void X0XEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    // Everything below is drawn in design coordinates.
    const float scale = juce::jmin ((float) getWidth() / (float) kDesignW, (float) getHeight() / (float) kDesignH);
    g.addTransform (juce::AffineTransform::scale (scale));

    // header
    g.setColour (colours::textHi);
    g.setFont (uiFont (26.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("X0X", 20, 18, 90, 36, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (uiFont (9.0f, true));
    g.drawText ("808 / 1073", 22, 50, 100, 12, juce::Justification::centredLeft);
    if (proc.hostTempoInUse.load())
        g.drawText ("HOST", tempoKnob.getX(), tempoKnob.getBottom() + 2, tempoKnob.getWidth(), 10, juce::Justification::centred);

    g.setColour (colours::line);
    g.fillRect (20, headerBounds.getBottom(), kDesignW - 40, 1);

    // grid column headers
    g.setFont (uiFont (9.0f, true));
    g.setColour (colours::textDim);
    for (int s = 0; s < kNumSteps; s += 4)
    {
        auto c = grid.cellBounds (0, s);
        g.drawText (juce::String (s + 1), (int) (grid.getX() + c.getX()), gridHeaderBounds.getY(), (int) c.getWidth(), 18, juce::Justification::centred);
    }
    const char* heads[] = { "LVL", "TUNE", "DEC" };
    for (int i = 0; i < 3; ++i)
        g.drawText (heads[i], levelKnobs[0]->getX() + i * 44, gridHeaderBounds.getY(), 44, 18, juce::Justification::centred);

    // strip
    g.setColour (colours::line);
    g.fillRect (20, stripBounds.getY(), kDesignW - 40, 1);
    g.setColour (colours::textHi);
    g.setFont (uiFont (22.0f, true).withExtraKerningFactor (0.1f));
    g.drawText ("1073", 20, stripBounds.getY() + 46, 90, 30, juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (uiFont (9.0f, true));
    g.drawText ("OUTPUT STAGE", 22, stripBounds.getY() + 76, 100, 12, juce::Justification::centredLeft);

    // section separators in the strip
    g.setColour (colours::line);
    auto vsep = [&] (int sx) { g.fillRect (sx, stripBounds.getY() + 30, 1, 108); };
    vsep (inputKnob.getRight() + 6);
    vsep (hfGainKnob.getX() - 8);
    vsep (outputKnob.getX() - 8);

    // clip LED
    drawLight (g, clipLedBounds.toFloat().reduced (7.0f), clipLedLevel);

    if (dragOver)
    {
        g.setColour (colours::lightOn.withAlpha (0.08f));
        g.fillRoundedRectangle (grid.getBounds().toFloat().expanded (6.0f), 6.0f);
    }
}

//==============================================================================
void X0XEditor::timerCallback()
{
    bool any = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& lab = *labels[(size_t) t];
        const float hit = proc.kit.hitFlash[(size_t) t].exchange (0.0f);
        const float before = lab.flash;
        lab.flash = juce::jmax (hit, lab.flash * 0.78f);
        if (lab.flash < 0.02f) lab.flash = 0.0f;
        if (! juce::exactlyEqual (lab.flash, before)) { lab.repaint(); any = true; }
    }
    grid.repaint();

    const bool playing = proc.internalPlaying.load() || proc.currentStep.load() >= 0;
    if (playButton.getToggleState() != playing) playButton.setToggleState (playing, juce::dontSendNotification);
    const bool hostTempo = proc.hostTempoInUse.load();
    if (tempoKnob.isEnabled() == hostTempo) { tempoKnob.setEnabled (! hostTempo); repaint (headerBounds); }

    const float before = clipLedLevel;
    if (proc.neve.clipLed.exchange (false)) clipLedLevel = 1.0f;
    else clipLedLevel = clipLedLevel < 0.02f ? 0.0f : clipLedLevel * 0.82f;
    if (! juce::exactlyEqual (clipLedLevel, before)) repaint (clipLedBounds.expanded (12));
    meter.setLevel (proc.neve.outputPeak.load());
    juce::ignoreUnused (any);
}

bool X0XEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { proc.togglePlay(); return true; }
    return false;
}

//==============================================================================
bool X0XEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).isDirectory() || X0XProcessor::isAudioFile (juce::File (f))) return true;
    return false;
}

void X0XEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOver = false;
    const auto local = content.getLocalPoint (this, juce::Point<int> (x, y));
    int track = grid.trackAtY (local.y - grid.getY());
    if (track < 0) track = 0;

    for (const auto& path : files)
    {
        juce::File f (path);
        if (f.isDirectory()) { proc.loadKitFolder (f); continue; }
        if (X0XProcessor::isAudioFile (f) && track < kNumTracks)
        {
            proc.loadSampleForTrack (track, f);
            ++track;
        }
    }
    repaint();
}

void X0XEditor::openSampleChooser (int track)
{
    chooser = std::make_unique<juce::FileChooser> ("Load sample for " + juce::String (kTrackLong[(size_t) track]),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, track] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { proc.loadSampleForTrack (track, f); labels[(size_t) track]->repaint(); }
    });
}

void X0XEditor::openKitChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of 808 samples",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.isDirectory()) { proc.loadKitFolder (f); repaint(); }
    });
}
