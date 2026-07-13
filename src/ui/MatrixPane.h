#pragma once

#include "../model/GraphModel.h"
#include "../engine/Nodes.h"
#include "Selection.h"
#include "Theme.h"
#include <algorithm>

namespace melo
{

// Fixed left pane: every connection in the graph as a row. Clicking a mod
// connection selects it, opening its morph curve in the timeline pane
// (issue #7); each row shows a small preview of that curve. Audio
// connections are listed for overview only.
//
// Collapses to a thin vertical tab so the graph gets the space back.
class MatrixPane : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    static constexpr int expandedWidth = 224, collapsedWidth = 20;

    MatrixPane (GraphModel& m, SelectionModel& sel) : model (m), selection (sel)
    {
        toggle.setButtonText ("<");
        toggle.onClick = [this]
        {
            expanded = ! expanded;
            toggle.setButtonText (expanded ? "<" : ">");
            if (onWidthChanged != nullptr)
                onWidthChanged();
        };
        addAndMakeVisible (toggle);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        selection.addChangeListener (this);
        startTimerHz (4);   // catches replaceState() swapping the tree out
        attach();
        rebuild();
    }

    ~MatrixPane() override
    {
        selection.removeChangeListener (this);
        observed.removeListener (this);
    }

    int preferredWidth() const { return expanded ? expandedWidth : collapsedWidth; }
    std::function<void()> onWidthChanged;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);

        if (! expanded)
        {
            // Vertical label on the collapsed tab.
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi, 10.0f, (float) getHeight() * 0.5f));
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("MATRIX", juce::Rectangle<int> (-80, (int) (getHeight() * 0.5f) - 8, 160, 16),
                        juce::Justification::centred);
            return;
        }

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Matrix", 8, 4, getWidth() - 40, 18, juce::Justification::centredLeft);

        if (rows.empty())
        {
            g.setFont (11.0f);
            g.drawText ("no connections yet", viewport.getBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (! expanded)
        {
            toggle.setBounds (0, 2, getWidth(), 18);
            viewport.setBounds ({});
            return;
        }
        toggle.setBounds (getWidth() - 22, 2, 18, 18);
        viewport.setBounds (0, 24, getWidth(), getHeight() - 24);
        layoutRows();
    }

private:
    struct Row : juce::Component
    {
        juce::ValueTree conn;
        bool isMod = false;
        bool selected = false;
        juce::Label title;
        std::function<void()> onSelect;

        Row()
        {
            title.setFont (juce::FontOptions (11.0f));
            title.setColour (juce::Label::textColourId, theme::text);
            title.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (title);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (isMod && onSelect != nullptr)
                onSelect();
        }

        void resized() override
        {
            title.setBounds (getLocalBounds().reduced (6, 1).removeFromTop (15));
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
            g.setColour ((selected ? theme::nodeSelected.withAlpha (0.25f)
                                   : theme::background.withAlpha (0.5f)));
            g.fillRoundedRectangle (bounds, 4.0f);
            if (selected)
            {
                g.setColour (theme::nodeSelected);
                g.drawRoundedRectangle (bounds, 4.0f, 1.2f);
            }

            if (! isMod)
                return;

            // Miniature of the connection's transfer curve: a diagonal while
            // it's on the default linear map, the drawn shape once shaped.
            auto preview = getLocalBounds().reduced (8, 2);
            preview.removeFromTop (16);
            auto area = preview.toFloat();
            g.setColour (theme::background);
            g.fillRect (area);

            std::vector<CurvePoint> pts;
            for (auto child : conn)
                if (child.hasType (ids::point))
                    pts.push_back ({ child.getProperty (ids::pointT, 0.0f),
                                     child.getProperty (ids::pointV, 0.5f),
                                     child.getProperty (ids::tension, 0.0f) });
            std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.t < b.t; });

            g.setColour (theme::controlSignal.withAlpha (pts.size() >= 2 ? 0.95f : 0.4f));
            if (pts.size() < 2)
            {
                g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);
            }
            else
            {
                juce::Path path;
                for (float x = 0.0f; x <= area.getWidth(); x += 2.0f)
                {
                    auto v = CurveNode::valueAt (pts, x / area.getWidth());
                    auto p = juce::Point<float> (area.getX() + x, area.getBottom() - v * area.getHeight());
                    x == 0.0f ? path.startNewSubPath (p) : path.lineTo (p);
                }
                g.strokePath (path, juce::PathStrokeType (1.2f));
            }
        }
    };

    void attach()
    {
        observed.removeListener (this);
        observed = model.state();
        observed.addListener (this);
    }

    juce::String nodeTitle (int nodeId) const
    {
        auto node = model.getNode (nodeId);
        if (! node.isValid())
            return "?";
        auto type = nodeTypeFromString (node.getProperty (ids::type).toString());
        if (type == NodeType::hosted)
            return node.getProperty (ids::pluginName, "Plugin").toString();
        return theme::titleFor (type, (int) node.getProperty (ids::macroIndex, -1));
    }

    void rebuild()
    {
        rows.clear();

        for (auto child : model.state())
        {
            if (! child.hasType (ids::conn))
                continue;

            auto row = std::make_unique<Row>();
            row->conn = child;
            row->isMod = child.hasProperty (ids::dstParam);

            auto text = nodeTitle (child.getProperty (ids::srcNode))
                        + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xa3 "))
                        + nodeTitle (child.getProperty (ids::dstNode));
            if (row->isMod)
                text += "." + child.getProperty (ids::dstParam).toString();
            row->title.setText (text, juce::dontSendNotification);

            if (row->isMod)
            {
                row->onSelect = [this, c = child] { selection.selectConnection (c); };
                row->selected = (child == selection.getSelectedConnection());
            }

            content.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }

        layoutRows();
        repaint();
    }

    void layoutRows()
    {
        auto rowHeight = 38;
        content.setSize (juce::jmax (1, viewport.getWidth() - 8),
                         juce::jmax (1, (int) rows.size() * rowHeight));
        auto y = 0;
        for (auto& row : rows)
        {
            row->setBounds (0, y, content.getWidth(),
                            row->isMod ? rowHeight : 20);
            y += row->isMod ? rowHeight : 20;
        }
        // Recompute: mixed row heights need a second pass for total size.
        content.setSize (content.getWidth(), juce::jmax (1, y));
    }

    // Connections/nodes added or removed rebuild the list; morph POINT edits
    // just repaint the row previews.
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override
    {
        if (child.hasType (ids::conn) || child.hasType (ids::node))
            rebuild();
        else if (child.hasType (ids::point))
            content.repaint();
    }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override
    {
        if (child.hasType (ids::conn) || child.hasType (ids::node))
            rebuild();
        else if (child.hasType (ids::point))
            content.repaint();
    }
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree.hasType (ids::point))
            content.repaint();
    }

    // Selection changed elsewhere (a cable clicked on the canvas): refresh
    // which row is highlighted.
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        auto sel = selection.getSelectedConnection();
        for (auto& row : rows)
            row->selected = row->conn == sel;
        content.repaint();
    }

    void timerCallback() override
    {
        if (model.state() != observed)
        {
            attach();
            rebuild();
        }
    }

    GraphModel& model;
    SelectionModel& selection;
    juce::ValueTree observed;
    juce::TextButton toggle;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    bool expanded = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixPane)
};

} // namespace melo
