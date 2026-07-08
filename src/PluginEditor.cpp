#include "PluginEditor.h"

MelomanixEditor::MelomanixEditor (MelomanixProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      gainAttachment (p.apvts, "gain", gainSlider)
{
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible (gainSlider);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (gainLabel);

    setSize (240, 200);
}

void MelomanixEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::grey);
    g.setFont (13.0f);
    g.drawText ("Melomanix v0 skeleton", getLocalBounds().removeFromBottom (24),
                juce::Justification::centred);
}

void MelomanixEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    gainLabel.setBounds (area.removeFromTop (24));
    gainSlider.setBounds (area.removeFromTop (120));
}
