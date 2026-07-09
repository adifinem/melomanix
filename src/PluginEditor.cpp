#include "PluginEditor.h"

MelomanixEditor::MelomanixEditor (MelomanixProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      macroStrip (p),
      graphCanvas (p.graphModel, selection, [&p] { return p.getBpm(); },
                   [&p] (int nodeId) { p.hostedPlugins.showEditorWindow (nodeId); }),
      timelinePane (p, selection)
{
    addAndMakeVisible (macroStrip);
    addAndMakeVisible (graphCanvas);
    addAndMakeVisible (timelinePane);

    setResizable (true, true);
    setResizeLimits (860, 560, 3200, 2000);
    setSize (1100, 680);
}

void MelomanixEditor::paint (juce::Graphics& g)
{
    g.fillAll (melo::theme::background);
}

void MelomanixEditor::resized()
{
    auto area = getLocalBounds();
    macroStrip.setBounds (area.removeFromTop (84));
    timelinePane.setBounds (area.removeFromBottom (150));
    graphCanvas.setBounds (area);
}
