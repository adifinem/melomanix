#include "PluginEditor.h"

MelomanixEditor::MelomanixEditor (MelomanixProcessor& p)
    : AudioProcessorEditor (p),
      processor (p)
{
    for (int i = 0; i < MelomanixProcessor::numMacros; ++i)
    {
        auto& macro = macros[(size_t) i];

        macro.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        macro.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (macro.slider);

        macro.label.setText ("M" + juce::String (i + 1), juce::dontSendNotification);
        macro.label.setJustificationType (juce::Justification::centred);
        macro.label.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (macro.label);

        macro.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, "macro" + juce::String (i + 1), macro.slider);
    }

    setSize (760, 480);
}

void MelomanixEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));

    auto area = getLocalBounds();
    auto macroStrip = area.removeFromTop (86);

    g.setColour (juce::Colour (0xff262a31));
    g.fillRect (macroStrip);

    auto timeline = area.removeFromBottom (120);
    g.fillRect (timeline);

    g.setColour (juce::Colours::grey);
    g.setFont (13.0f);
    g.drawText ("node graph (next milestone)", area, juce::Justification::centred);
    g.drawText ("timeline (next milestone)", timeline, juce::Justification::centred);
}

void MelomanixEditor::resized()
{
    auto strip = getLocalBounds().removeFromTop (86).reduced (12, 6);
    auto dialWidth = strip.getWidth() / MelomanixProcessor::numMacros;

    for (auto& macro : macros)
    {
        auto cell = strip.removeFromLeft (dialWidth);
        macro.label.setBounds (cell.removeFromBottom (16));
        macro.slider.setBounds (cell.reduced (4, 0));
    }
}
