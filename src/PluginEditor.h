#pragma once

#include "PluginProcessor.h"

class MelomanixEditor : public juce::AudioProcessorEditor
{
public:
    explicit MelomanixEditor (MelomanixProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MelomanixProcessor& processor;

    juce::Slider gainSlider;
    juce::Label gainLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelomanixEditor)
};
