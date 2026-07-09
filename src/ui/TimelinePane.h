#pragma once

#include "../PluginProcessor.h"
#include "Selection.h"
#include "Theme.h"

namespace melo
{

// Fixed bottom pane. Retargets to the last-clicked node and, for
// controllers, renders their time-domain lane with an animated playhead.
// Renders via the same LFONode::shapeValue the engine uses, so what you see
// is exactly what modulates.
class TimelinePane : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    TimelinePane (MelomanixProcessor& p, SelectionModel& sel)
        : processor (p), selection (sel)
    {
        selection.addChangeListener (this);
        startTimerHz (30);
    }

    ~TimelinePane() override
    {
        selection.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        auto area = getLocalBounds().reduced (8);

        auto nodeId = selection.getSelectedNodeId();
        auto node = processor.graphModel.getNode (nodeId);

        if (! node.isValid())
        {
            g.setColour (theme::textDim);
            g.setFont (13.0f);
            g.drawText ("click a node to see its lane", area, juce::Justification::centred);
            return;
        }

        auto type = processor.graphModel.getNodeType (node);
        auto title = theme::titleFor (type, (int) node.getProperty (ids::macroIndex, -1));

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (title, area.removeFromTop (16), juce::Justification::centredLeft);
        area.removeFromTop (2);

        if (type == NodeType::lfo)
            paintLfoLane (g, area.toFloat(), nodeId);
        else if (type == NodeType::macro)
            paintMacroLane (g, area.toFloat(), node);
        else
        {
            g.setFont (12.0f);
            g.drawText ("audio node — parameter lanes live on its mod sockets",
                        area, juce::Justification::centred);
        }
    }

private:
    static constexpr double windowSeconds = 4.0;

    void paintLfoLane (juce::Graphics& g, juce::Rectangle<float> area, int nodeId)
    {
        auto& model = processor.graphModel;
        auto rate  = LFONode::effectiveRate (model.getParamValue (nodeId, "rate"),
                                             (int) std::lround (model.getParamValue (nodeId, "sync")),
                                             processor.getBpm());
        auto shape = (int) std::lround (model.getParamValue (nodeId, "shape"));
        auto depth = model.getParamValue (nodeId, "depth");

        drawLaneFrame (g, area);

        juce::Path path;
        for (float x = 0.0f; x <= area.getWidth(); x += 1.0f)
        {
            auto t = (double) (x / area.getWidth()) * windowSeconds;
            auto phase = (float) std::fmod (t * rate, 1.0);
            auto value = 0.5f + (LFONode::shapeValue (shape, phase) - 0.5f) * depth;
            auto point = juce::Point<float> (area.getX() + x,
                                             area.getBottom() - value * area.getHeight());
            if (x == 0.0f)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }
        g.setColour (theme::controlSignal);
        g.strokePath (path, juce::PathStrokeType (1.8f));

        drawPlayhead (g, area);
    }

    void paintMacroLane (juce::Graphics& g, juce::Rectangle<float> area, const juce::ValueTree& node)
    {
        drawLaneFrame (g, area);

        auto index = (int) node.getProperty (ids::macroIndex, 0);
        auto* raw = processor.apvts.getRawParameterValue ("macro" + juce::String (index + 1));
        auto value = raw != nullptr ? raw->load() : 0.0f;

        auto y = area.getBottom() - value * area.getHeight();
        g.setColour (theme::controlSignal);
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setFont (11.0f);
        g.drawText (juce::String (value, 2), area.reduced (4.0f, 2.0f),
                    juce::Justification::topRight);

        drawPlayhead (g, area);
    }

    void drawLaneFrame (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (theme::background);
        g.fillRect (area);
        g.setColour (theme::textDim.withAlpha (0.3f));
        for (int i = 1; i < 4; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 4.0f),
                                area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    }

    void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto position = std::fmod (processor.getPlayheadSeconds(), windowSeconds) / windowSeconds;
        auto x = area.getX() + (float) position * area.getWidth();
        g.setColour (theme::playhead);
        g.drawLine (x, area.getY(), x, area.getBottom(), 1.5f);
    }

    void timerCallback() override { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    MelomanixProcessor& processor;
    SelectionModel& selection;
};

} // namespace melo
