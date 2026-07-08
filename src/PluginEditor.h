#pragma once

#include "PluginProcessor.h"

// Interim editor for the engine-core milestone: the macro strip is real
// (these are the actual host parameters), the graph and timeline panes are
// placeholders to be built next.
class MelomanixEditor : public juce::AudioProcessorEditor
{
public:
    explicit MelomanixEditor (MelomanixProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MelomanixProcessor& processor;

    struct MacroDial
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<MacroDial, MelomanixProcessor::numMacros> macros;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelomanixEditor)
};
