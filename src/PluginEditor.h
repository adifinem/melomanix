#pragma once

#include "PluginProcessor.h"
#include "ui/GraphCanvas.h"
#include "ui/TimelinePane.h"
#include "ui/MacroStrip.h"

// Fixed panes per the spec: macro strip (top), node graph (centre, largest),
// timeline (bottom). Nothing floats. The editor is the drag-and-drop
// container so macro grips can drop onto node parameter rows.
class MelomanixEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer
{
public:
    explicit MelomanixEditor (MelomanixProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MelomanixProcessor& processor;

    melo::SelectionModel selection;
    melo::MacroStrip macroStrip;
    melo::GraphCanvas graphCanvas;
    melo::TimelinePane timelinePane;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelomanixEditor)
};
