#pragma once
#include <JuceHeader.h>

namespace x0x
{
namespace colours
{
    const juce::Colour bg        { 0xff0a0a0b };
    const juce::Colour panel     { 0xff111113 };
    const juce::Colour line      { 0xff1e1e21 };
    const juce::Colour lightOff  { 0xff1c1c1f };
    const juce::Colour lightRing { 0xff2e2e32 };
    const juce::Colour lightOn   { 0xfff4f4f4 };
    const juce::Colour text      { 0xff8d8d92 };
    const juce::Colour textDim   { 0xff4a4a4f };
    const juce::Colour textHi    { 0xffececec };
    const juce::Colour knobBody  { 0xff151517 };
    const juce::Colour knobTrack { 0xff2a2a2e };
    const juce::Colour knobArc   { 0xffe6e6e6 };
}

inline juce::Font uiFont (float height, bool bold = false)
{
    auto opts = juce::FontOptions().withHeight (height);
    if (bold) opts = opts.withStyle ("Bold");
    return juce::Font (opts).withExtraKerningFactor (0.06f);
}

/** Draws a soft white light with glow. intensity 0..1 */
inline void drawLight (juce::Graphics& g, juce::Rectangle<float> r, float intensity, bool ringWhenOff = true)
{
    auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;
    if (intensity <= 0.001f)
    {
        g.setColour (colours::lightOff);
        g.fillEllipse (r);
        if (ringWhenOff) { g.setColour (colours::lightRing); g.drawEllipse (r.reduced (0.5f), 1.0f); }
        return;
    }
    const float a = juce::jlimit (0.0f, 1.0f, intensity);
    // glow
    for (int i = 3; i >= 1; --i)
    {
        const float gr = rad * (1.0f + 0.55f * (float) i * a);
        g.setColour (colours::lightOn.withAlpha (0.045f * a * (4 - (float) i)));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2, gr * 2);
    }
    g.setColour (colours::lightOff.interpolatedWith (colours::lightOn, 0.25f + 0.75f * a));
    g.fillEllipse (r);
    if (a > 0.6f)
    {
        g.setColour (juce::Colours::white.withAlpha ((a - 0.6f) * 2.0f));
        g.fillEllipse (r.reduced (rad * 0.35f));
    }
}

class X0XLookAndFeel : public juce::LookAndFeel_V4
{
public:
    X0XLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141416));
        setColour (juce::PopupMenu::textColourId, colours::textHi);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a2e));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxTextColourId, colours::textHi);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff141416));
        setColour (juce::TooltipWindow::textColourId, colours::textHi);
        setColour (juce::TooltipWindow::outlineColourId, colours::line);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff141416));
        setColour (juce::AlertWindow::textColourId, colours::textHi);
    }

    juce::Font getPopupMenuFont() override { return uiFont (13.0f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (d, d);
        const auto c = r.getCentre();
        const float rad = d * 0.5f;
        const float lineW = juce::jmax (1.5f, d * 0.045f);
        const float arcR = rad - lineW * 0.5f;
        const bool enabled = s.isEnabled();

        // body
        g.setColour (juce::Colour (0xff151517));
        g.fillEllipse (r.reduced (lineW * 1.6f));
        g.setColour (juce::Colour (0xff232327));
        g.drawEllipse (r.reduced (lineW * 1.6f), 1.0f);

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::knobTrack);
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc (bipolar sliders arc from centre)
        const float angle = startAngle + pos * (endAngle - startAngle);
        // Symmetric ranges (EQ gain, tune) arc from the centre; everything else from the start.
        const bool bipolar = s.getMinimum() < 0.0 && juce::approximatelyEqual (-s.getMinimum(), s.getMaximum());
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (enabled ? colours::knobArc : colours::textDim);
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        const float pr = rad - lineW * 2.6f;
        juce::Point<float> p1 (c.x + std::sin (angle) * pr * 0.45f, c.y - std::cos (angle) * pr * 0.45f);
        juce::Point<float> p2 (c.x + std::sin (angle) * pr, c.y - std::cos (angle) * pr);
        g.setColour (enabled ? colours::textHi : colours::textDim);
        g.drawLine ({ p1, p2 }, juce::jmax (1.5f, d * 0.04f));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float lightD = juce::jmin (10.0f, r.getHeight() * 0.5f);
        auto light = juce::Rectangle<float> (lightD, lightD).withCentre ({ r.getX() + lightD * 0.5f + 2.0f, r.getCentreY() });
        drawLight (g, light, b.getToggleState() ? 1.0f : (highlighted ? 0.12f : 0.0f));
        g.setColour (b.getToggleState() ? colours::textHi : colours::text);
        g.setFont (uiFont (11.0f, true));
        g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (lightD + 8.0f), juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? juce::Colour (0xff2a2a2e) : highlighted ? juce::Colour (0xff1c1c1f) : juce::Colour (0xff131315));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff2a2a2e));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour (b.isEnabled() ? colours::textHi : colours::textDim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (11.0f, true); }
};
} // namespace x0x
