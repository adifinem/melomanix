#pragma once

#include "../PluginProcessor.h"
#include "Selection.h"
#include "Theme.h"

namespace melo
{

// Fixed bottom pane. Retargets to the last-clicked node; controller lanes
// render through the same functions the engine evaluates, so what you see
// is exactly what modulates.
//
// Curve nodes are edited here directly: drag points, double-click to add,
// right-click a point to delete, drag the space between two points
// vertically to bend that segment (tension).
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

    // --- painting -----------------------------------------------------

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);

        auto nodeId = selection.getSelectedNodeId();
        auto node = processor.graphModel.getNode (nodeId);

        if (! node.isValid())
        {
            g.setColour (theme::textDim);
            g.setFont (13.0f);
            g.drawText ("click a node to see its lane", getLocalBounds(), juce::Justification::centred);
            return;
        }

        auto type = processor.graphModel.getNodeType (node);
        auto title = theme::titleFor (type, (int) node.getProperty (ids::macroIndex, -1));

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (titleTextFor (type, title), getLocalBounds().reduced (8).removeFromTop (16),
                    juce::Justification::centredLeft);

        auto area = laneBounds().toFloat();

        if (type == NodeType::lfo)
            paintLfoLane (g, area, nodeId);
        else if (type == NodeType::curve)
            paintCurveLane (g, area, node);
        else if (type == NodeType::macro)
            paintMacroLane (g, area, node);
        else
        {
            g.setFont (12.0f);
            g.drawText ("audio node — parameter lanes live on its mod sockets",
                        area, juce::Justification::centred);
        }
    }

    // --- curve editing ---------------------------------------------------

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto node = selectedCurveNode();
        if (! node.isValid())
            return;

        draggingPoint = juce::ValueTree();
        tensionSegmentStart = juce::ValueTree();

        if (auto point = pointAt (node, e.getPosition()); point.isValid())
        {
            if (e.mods.isPopupMenu())
            {
                // Keep at least two points so the curve stays defined.
                if (countPoints (node) > 2)
                    node.removeChild (point, nullptr);
                return;
            }
            draggingPoint = point;
            return;
        }

        if (! e.mods.isPopupMenu())
        {
            // Not on a point: a vertical drag bends the segment under the cursor.
            if (auto segment = segmentStartAt (node, e.position.x); segment.isValid())
            {
                tensionSegmentStart = segment;
                tensionDragStartValue = segment.getProperty (ids::tension, 0.0f);
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto area = laneBounds().toFloat();

        if (draggingPoint.isValid())
        {
            auto t = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
            auto v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
            draggingPoint.setProperty (ids::pointT, t, nullptr);
            draggingPoint.setProperty (ids::pointV, v, nullptr);
        }
        else if (tensionSegmentStart.isValid())
        {
            auto tension = juce::jlimit (-1.0f, 1.0f,
                tensionDragStartValue + (float) e.getDistanceFromDragStartY() / 60.0f);
            tensionSegmentStart.setProperty (ids::tension, tension, nullptr);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingPoint = juce::ValueTree();
        tensionSegmentStart = juce::ValueTree();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto node = selectedCurveNode();
        if (! node.isValid())
            return;

        auto area = laneBounds().toFloat();
        juce::ValueTree point (ids::point);
        point.setProperty (ids::pointT,
                           juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth()), nullptr);
        point.setProperty (ids::pointV,
                           juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight()), nullptr);
        point.setProperty (ids::tension, 0.0f, nullptr);
        node.addChild (point, -1, nullptr);
    }

private:
    static constexpr double lfoWindowSeconds = 4.0;

    juce::Rectangle<int> laneBounds() const
    {
        auto area = getLocalBounds().reduced (8);
        area.removeFromTop (18);
        return area;
    }

    juce::String titleTextFor (NodeType type, const juce::String& title) const
    {
        if (type == NodeType::curve)
            return title + "   —   drag points · double-click to add · right-click to delete · drag between points to bend";
        return title;
    }

    juce::ValueTree selectedCurveNode() const
    {
        auto node = processor.graphModel.getNode (selection.getSelectedNodeId());
        if (node.isValid() && processor.graphModel.getNodeType (node) == NodeType::curve)
            return node;
        return {};
    }

    static int countPoints (const juce::ValueTree& node)
    {
        int count = 0;
        for (auto child : node)
            if (child.hasType (ids::point))
                ++count;
        return count;
    }

    std::vector<CurvePoint> sortedPoints (const juce::ValueTree& node) const
    {
        std::vector<CurvePoint> points;
        for (auto child : node)
            if (child.hasType (ids::point))
                points.push_back ({ child.getProperty (ids::pointT, 0.0f),
                                    child.getProperty (ids::pointV, 0.5f),
                                    child.getProperty (ids::tension, 0.0f) });
        std::sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.t < b.t; });
        return points;
    }

    juce::Point<float> pointToScreen (const CurvePoint& point, juce::Rectangle<float> area) const
    {
        return { area.getX() + point.t * area.getWidth(),
                 area.getBottom() - point.v * area.getHeight() };
    }

    juce::ValueTree pointAt (const juce::ValueTree& node, juce::Point<int> position) const
    {
        auto area = laneBounds().toFloat();
        for (auto child : node)
        {
            if (! child.hasType (ids::point))
                continue;
            CurvePoint p { child.getProperty (ids::pointT, 0.0f),
                           child.getProperty (ids::pointV, 0.5f), 0.0f };
            if (pointToScreen (p, area).getDistanceFrom (position.toFloat()) < 9.0f)
                return child;
        }
        return {};
    }

    // The model tree of the point that STARTS the segment under screen-x.
    juce::ValueTree segmentStartAt (const juce::ValueTree& node, float screenX) const
    {
        auto area = laneBounds().toFloat();
        auto phase = (screenX - area.getX()) / area.getWidth();

        juce::ValueTree best;
        float bestT = -1.0f;
        for (auto child : node)
        {
            if (! child.hasType (ids::point))
                continue;
            float t = child.getProperty (ids::pointT, 0.0f);
            if (t <= phase && t > bestT)
            {
                bestT = t;
                best = child;
            }
        }
        return best;
    }

    // --- lane painters ---------------------------------------------------

    void paintLfoLane (juce::Graphics& g, juce::Rectangle<float> area, int nodeId)
    {
        auto& model = processor.graphModel;
        auto rate  = LFONode::effectiveRate (model.getParamValue (nodeId, "rate"),
                                             (int) std::lround (model.getParamValue (nodeId, "sync")),
                                             processor.getBpm());
        auto shape = (int) std::lround (model.getParamValue (nodeId, "shape"));
        auto depth = model.getParamValue (nodeId, "depth");

        drawLaneFrame (g, area, 4);

        juce::Path path;
        for (float x = 0.0f; x <= area.getWidth(); x += 1.0f)
        {
            auto t = (double) (x / area.getWidth()) * lfoWindowSeconds;
            auto phase = (float) std::fmod (t * rate, 1.0);
            auto value = 0.5f + (LFONode::shapeValue (shape, phase) - 0.5f) * depth;
            auto point = juce::Point<float> (area.getX() + x,
                                             area.getBottom() - value * area.getHeight());
            x == 0.0f ? path.startNewSubPath (point) : path.lineTo (point);
        }
        g.setColour (theme::controlSignal);
        g.strokePath (path, juce::PathStrokeType (1.8f));

        drawPlayheadAt (g, area,
            (float) (std::fmod (processor.getPlayheadSeconds(), lfoWindowSeconds) / lfoWindowSeconds));
    }

    void paintCurveLane (juce::Graphics& g, juce::Rectangle<float> area, const juce::ValueTree& node)
    {
        auto nodeId = (int) node.getProperty (ids::nodeId);
        auto lengthBeats = juce::jmax (0.25f, processor.graphModel.getParamValue (nodeId, "length"));
        auto points = sortedPoints (node);

        drawLaneFrame (g, area, juce::jlimit (1, 32, (int) std::lround (lengthBeats)));

        juce::Path path;
        for (float x = 0.0f; x <= area.getWidth(); x += 1.0f)
        {
            auto value = CurveNode::valueAt (points, x / area.getWidth());
            auto point = juce::Point<float> (area.getX() + x,
                                             area.getBottom() - value * area.getHeight());
            x == 0.0f ? path.startNewSubPath (point) : path.lineTo (point);
        }

        // Soft fill under the curve, then the line, then the handles.
        auto fill = path;
        fill.lineTo (area.getBottomRight());
        fill.lineTo (area.getBottomLeft());
        fill.closeSubPath();
        g.setColour (theme::controlSignal.withAlpha (0.12f));
        g.fillPath (fill);

        g.setColour (theme::controlSignal);
        g.strokePath (path, juce::PathStrokeType (1.8f));

        for (auto& point : points)
        {
            auto centre = pointToScreen (point, area);
            g.setColour (theme::panel);
            g.fillEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::controlSignal);
            g.drawEllipse (centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f, 1.6f);
        }

        auto phase = (float) std::fmod (processor.getPlayheadBeats() / (double) lengthBeats, 1.0);
        drawPlayheadAt (g, area, phase < 0.0f ? phase + 1.0f : phase);
    }

    void paintMacroLane (juce::Graphics& g, juce::Rectangle<float> area, const juce::ValueTree& node)
    {
        drawLaneFrame (g, area, 4);

        auto index = (int) node.getProperty (ids::macroIndex, 0);
        auto* raw = processor.apvts.getRawParameterValue ("macro" + juce::String (index + 1));
        auto value = raw != nullptr ? raw->load() : 0.0f;

        auto y = area.getBottom() - value * area.getHeight();
        g.setColour (theme::controlSignal);
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setFont (11.0f);
        g.drawText (juce::String (value, 2), area.reduced (4.0f, 2.0f),
                    juce::Justification::topRight);
    }

    void drawLaneFrame (juce::Graphics& g, juce::Rectangle<float> area, int divisions)
    {
        g.setColour (theme::background);
        g.fillRect (area);
        g.setColour (theme::textDim.withAlpha (0.3f));
        for (int i = 1; i < divisions; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / (float) divisions),
                                area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    }

    void drawPlayheadAt (juce::Graphics& g, juce::Rectangle<float> area, float proportion)
    {
        auto x = area.getX() + proportion * area.getWidth();
        g.setColour (theme::playhead);
        g.drawLine (x, area.getY(), x, area.getBottom(), 1.5f);
    }

    void timerCallback() override { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    MelomanixProcessor& processor;
    SelectionModel& selection;

    juce::ValueTree draggingPoint;
    juce::ValueTree tensionSegmentStart;
    float tensionDragStartValue = 0.0f;
};

} // namespace melo
