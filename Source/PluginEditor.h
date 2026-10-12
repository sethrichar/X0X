#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

namespace x0x
{
/** Rotary knob with a caption that shows the value while dragging. */
class Knob : public juce::Component, private juce::Slider::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& caption, int numSteps = 0);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider& getSlider() { return slider; }
    void setCaptionVisible (bool b) { showCaption = b; }
private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override { dragging = true; repaint(); }
    void sliderDragEnded (juce::Slider*) override { dragging = false; repaint(); }
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String caption;
    int steps; bool dragging = false, showCaption = true;
};

/** The 12 x 16 grid of lights. */
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (X0XProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::Rectangle<float> cellBounds (int track, int step) const;
    int trackAtY (int y) const;
    static constexpr int kGroupGap = 8;
private:
    std::pair<int, int> cellAt (juce::Point<int>) const;
    void applyPaint (int track, int step);
    X0XProcessor& proc;
    uint8_t paintValue = 0;
    int lastTrack = -1, lastStep = -1;
};

/** Track name: click to audition, right-click for sample options, with a hit light. */
class TrackLabel : public juce::Component
{
public:
    TrackLabel (X0XProcessor& p, int t) : proc (p), track (t) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void (int)> onLoadSample;
    float flash = 0.0f;
private:
    X0XProcessor& proc; int track;
};

/** Small vertical output meter. */
class Meter : public juce::Component
{
public:
    void setLevel (float linear) { level = linear; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float level = 0.0f, hold = 0.0f;
};

class X0XEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        public juce::FileDragAndDropTarget
{
public:
    explicit X0XEditor (X0XProcessor&);
    ~X0XEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    static constexpr int kDesignW = 1000, kDesignH = 640;

private:
    void timerCallback() override;
    void openSampleChooser (int track);
    void openKitChooser();
    void layoutContent();

    X0XProcessor& proc;
    X0XLookAndFeel lnf;
    juce::Component content;          // fixed-size, scaled to fit

    // header
    juce::ToggleButton playButton { "Play" };
    Knob tempoKnob, swingKnob, accentKnob, lengthKnob;
    juce::TextButton kitButton { "Load Kit" }, clearButton { "Clear" };

    // grid
    StepGrid grid;
    std::array<std::unique_ptr<TrackLabel>, kNumTracks> labels;
    std::array<std::unique_ptr<Knob>, kNumTracks> levelKnobs, tuneKnobs, decayKnobs;

    // 1073 strip
    Knob inputKnob, hpfKnob, lfFreqKnob, lfGainKnob, midFreqKnob, midGainKnob, hfGainKnob, outputKnob;
    juce::ToggleButton eqButton { "EQ" }, clipButton { "Clip" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqAttachment, clipAttachment;
    Meter meter;
    float clipLedLevel = 0.0f;
    juce::Rectangle<int> clipLedBounds, stripBounds, headerBounds, gridHeaderBounds;

    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (X0XEditor)
};
} // namespace x0x
