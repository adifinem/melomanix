#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"

namespace melo
{

// Fixed top strip of macro dials. Each dial is a host parameter; the grip
// beneath a dial drags onto any parameter row in the graph to wire that
// macro in (the fast-path gesture — the same mod connection as manual cabling).
class MacroStrip : public juce::Component
{
public:
    explicit MacroStrip (MelomanixProcessor& p)
    {
        for (int i = 0; i < MelomanixProcessor::numMacros; ++i)
        {
            auto& macro = macros[(size_t) i];

            macro.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            macro.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            macro.slider.setColour (juce::Slider::rotarySliderFillColourId, theme::controlSignal);
            addAndMakeVisible (macro.slider);

            macro.grip.index = i;
            addAndMakeVisible (macro.grip);

            macro.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                p.apvts, "macro" + juce::String (i + 1), macro.slider);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::textDim);
        g.setFont (11.0f);

        auto cellWidth = getWidth() / MelomanixProcessor::numMacros;
        for (int i = 0; i < MelomanixProcessor::numMacros; ++i)
            g.drawText ("M" + juce::String (i + 1),
                        i * cellWidth, getHeight() - 14, cellWidth, 12,
                        juce::Justification::centred);
    }

    void resized() override
    {
        auto strip = getLocalBounds().reduced (8, 4);
        strip.removeFromBottom (12);
        auto cellWidth = strip.getWidth() / MelomanixProcessor::numMacros;

        for (auto& macro : macros)
        {
            auto cell = strip.removeFromLeft (cellWidth);
            auto grip = cell.removeFromRight (14).withSizeKeepingCentre (12, 24);
            macro.grip.setBounds (grip);
            macro.slider.setBounds (cell.reduced (2));
        }
    }

private:
    // The draggable handle: dragging it onto a node's parameter row assigns
    // this macro to that parameter.
    struct DragGrip : public juce::Component
    {
        int index = 0;

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::controlSignal.withAlpha (0.9f));
            for (int y = 4; y < getHeight() - 3; y += 5)
                g.fillEllipse (3.0f, (float) y, 4.0f, 4.0f);
        }

        void mouseDrag (const juce::MouseEvent&) override
        {
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! container->isDragAndDropActive())
                    container->startDragging ("macro:" + juce::String (index), this);
        }

        void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    };

    struct MacroDial
    {
        juce::Slider slider;
        DragGrip grip;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<MacroDial, MelomanixProcessor::numMacros> macros;
};

} // namespace melo
