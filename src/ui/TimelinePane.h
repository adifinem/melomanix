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
        addChildComponent (gridButton);
        gridButton.onClick = [this] { showGridMenu(); };
        updateGridButton();
        startTimerHz (30);
    }

    void resized() override
    {
        gridButton.setBounds (getWidth() - 86, 4, 78, 18);
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
            snapToGrid (draggingPoint.getParent(), t, v);
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
        auto t = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        auto v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
        snapToGrid (node, t, v);

        juce::ValueTree point (ids::point);
        point.setProperty (ids::pointT, t, nullptr);
        point.setProperty (ids::pointV, v, nullptr);
        point.setProperty (ids::tension, 0.0f, nullptr);
        node.addChild (point, -1, nullptr);
    }

private:
    static constexpr double lfoWindowSeconds = 4.0;

    // --- point-editing grid (per curve node, 0 = free axis) ---------------

    static constexpr std::pair<int, int> gridPresets[] = {
        { 0, 0 }, { 2, 2 }, { 4, 4 }, { 6, 4 }, { 4, 6 }, { 8, 8 }, { 16, 8 }
    };

    static juce::String gridLabel (int gx, int gy)
    {
        return gx <= 0 ? juce::String ("grid: off")
                       : "grid: " + juce::String (gx)
                             + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (gy);
    }

    // Snaps t/v to the node's grid; CTRL bypasses (matches the length slider).
    static void snapToGrid (const juce::ValueTree& node, float& t, float& v)
    {
        if (! node.isValid() || juce::ModifierKeys::getCurrentModifiers().isCtrlDown())
            return;
        int gx = node.getProperty (ids::gridX, 0), gy = node.getProperty (ids::gridY, 0);
        if (gx > 0) t = std::round (t * (float) gx) / (float) gx;
        if (gy > 0) v = std::round (v * (float) gy) / (float) gy;
    }

    void showGridMenu()
    {
        auto node = selectedCurveNode();
        if (! node.isValid())
            return;

        int curX = node.getProperty (ids::gridX, 0), curY = node.getProperty (ids::gridY, 0);
        juce::PopupMenu menu;
        for (int i = 0; i < (int) std::size (gridPresets); ++i)
        {
            auto [gx, gy] = gridPresets[i];
            menu.addItem (i + 1, gridLabel (gx, gy), true, gx == curX && gy == curY);
        }
        menu.addSeparator();
        // Any division count, not just the presets (issue #4 follow-up).
        menu.addItem (customGridItemId, "Custom...", true, ! isPreset (curX, curY));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridButton),
                            [this, node] (int result) mutable
                            {
                                if (result <= 0)
                                    return;
                                if (result == customGridItemId)
                                {
                                    promptCustomGrid (node);
                                    return;
                                }
                                auto [gx, gy] = gridPresets[result - 1];
                                node.setProperty (ids::gridX, gx, nullptr);
                                node.setProperty (ids::gridY, gy, nullptr);
                                updateGridButton();
                                repaint();
                            });
    }

    static constexpr int customGridItemId = 1000;

    static bool isPreset (int gx, int gy)
    {
        for (auto& [px, py] : gridPresets)
            if (px == gx && py == gy)
                return true;
        return false;
    }

    // Free-form grid entry: any non-negative column/row count (0 = free axis).
    void promptCustomGrid (juce::ValueTree node)
    {
        auto window = std::make_shared<juce::AlertWindow> (
            "Custom grid", "Columns and rows to snap to (0 = free on that axis).",
            juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("cols", juce::String ((int) node.getProperty (ids::gridX, 0)), "Columns (X)");
        window->addTextEditor ("rows", juce::String ((int) node.getProperty (ids::gridY, 0)), "Rows (Y)");
        window->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, node, window] (int result) mutable
            {
                if (result == 1)
                {
                    auto gx = juce::jlimit (0, 256, window->getTextEditorContents ("cols").getIntValue());
                    auto gy = juce::jlimit (0, 256, window->getTextEditorContents ("rows").getIntValue());
                    node.setProperty (ids::gridX, gx, nullptr);
                    node.setProperty (ids::gridY, gy, nullptr);
                    updateGridButton();
                    repaint();
                }
            }), false);
    }

    void updateGridButton()
    {
        auto node = selectedCurveNode();
        gridButton.setVisible (node.isValid());
        if (node.isValid())
            gridButton.setButtonText (gridLabel (node.getProperty (ids::gridX, 0),
                                                 node.getProperty (ids::gridY, 0)));
    }

    juce::Rectangle<int> laneBounds() const
    {
        auto area = getLocalBounds().reduced (8);
        area.removeFromTop (18);
        return area;
    }

    juce::String titleTextFor (NodeType type, const juce::String& title) const
    {
        if (type == NodeType::curve)
            return title + "   —   drag points · double-click to add · right-click to delete · drag between points to bend · CTRL = no snap";
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

        // Editing grid on top of the beat frame, slightly brighter so the
        // snap targets read against it.
        int gx = node.getProperty (ids::gridX, 0), gy = node.getProperty (ids::gridY, 0);
        if (gx > 0)
        {
            g.setColour (theme::controlSignal.withAlpha (0.18f));
            for (int i = 0; i <= gx; ++i)
                g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / (float) gx),
                                    area.getY(), area.getBottom());
            for (int j = 0; j <= gy; ++j)
                g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) j / (float) gy),
                                      area.getX(), area.getRight());
        }

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

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateGridButton();
        repaint();
    }

    MelomanixProcessor& processor;
    SelectionModel& selection;
    juce::TextButton gridButton;

    juce::ValueTree draggingPoint;
    juce::ValueTree tensionSegmentStart;
    float tensionDragStartValue = 0.0f;
};

} // namespace melo
