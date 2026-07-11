#pragma once

#include "../model/GraphModel.h"
#include "Theme.h"

namespace melo
{

// Fixed left pane: every connection in the graph as a row. Mod connections
// get per-connection remap controls — amount (-100%..100%, negative
// inverts) and offset (shifts the modulation centre) — writing straight to
// the connection's properties, which the processor recompiles from. Audio
// connections are listed for overview only.
//
// Collapses to a thin vertical tab so the graph gets the space back.
class MatrixPane : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::Timer
{
public:
    static constexpr int expandedWidth = 224, collapsedWidth = 20;

    explicit MatrixPane (GraphModel& m) : model (m)
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

        startTimerHz (4);   // catches replaceState() swapping the tree out
        attach();
        rebuild();
    }

    ~MatrixPane() override { observed.removeListener (this); }

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
        juce::Label title;
        juce::Slider amount, offset;

        Row()
        {
            title.setFont (juce::FontOptions (11.0f));
            title.setColour (juce::Label::textColourId, theme::text);
            title.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (title);

            for (auto* s : { &amount, &offset })
            {
                s->setSliderStyle (juce::Slider::LinearHorizontal);
                s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s->setRange (-1.0, 1.0, 0.0);
                s->setDoubleClickReturnValue (true, 0.0);
                s->setPopupDisplayEnabled (true, true, nullptr);
                addChildComponent (*s);
            }
            amount.setColour (juce::Slider::trackColourId, theme::controlSignal.withAlpha (0.4f));
            offset.setColour (juce::Slider::trackColourId, theme::textDim.withAlpha (0.4f));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (6, 1);
            title.setBounds (area.removeFromTop (15));
            if (isMod)
            {
                auto sliders = area.reduced (0, 1);
                amount.setBounds (sliders.removeFromLeft (sliders.getWidth() / 2 - 2));
                sliders.removeFromLeft (4);
                offset.setBounds (sliders);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::background.withAlpha (0.5f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 1.0f), 4.0f);
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
                row->amount.setVisible (true);
                row->offset.setVisible (true);

                row->amount.setValue ((float) child.getProperty (ids::depth, 1.0f),
                                      juce::dontSendNotification);
                row->offset.setValue ((float) child.getProperty (ids::offset, 0.0f),
                                      juce::dontSendNotification);

                row->amount.textFromValueFunction = [] (double v)
                { return "amount " + juce::String ((int) std::lround (v * 100.0)) + "%"; };
                row->offset.textFromValueFunction = [] (double v)
                { return "offset " + juce::String (v, 2); };

                row->amount.onValueChange = [r = row.get()]
                { juce::ValueTree (r->conn).setProperty (ids::depth, (float) r->amount.getValue(), nullptr); };
                row->offset.onValueChange = [r = row.get()]
                { juce::ValueTree (r->conn).setProperty (ids::offset, (float) r->offset.getValue(), nullptr); };
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

    // Connections added/removed anywhere in the tree refresh the list;
    // property edits from elsewhere (depth menu, loads) refresh sliders.
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override
    {
        if (child.hasType (ids::conn) || child.hasType (ids::node))
            rebuild();
    }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override
    {
        if (child.hasType (ids::conn) || child.hasType (ids::node))
            rebuild();
    }
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override
    {
        if (! tree.hasType (ids::conn))
            return;
        for (auto& row : rows)
            if (row->conn == tree)
            {
                if (property == ids::depth)
                    row->amount.setValue ((float) tree.getProperty (ids::depth, 1.0f),
                                          juce::dontSendNotification);
                else if (property == ids::offset)
                    row->offset.setValue ((float) tree.getProperty (ids::offset, 0.0f),
                                          juce::dontSendNotification);
            }
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
    juce::ValueTree observed;
    juce::TextButton toggle;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    bool expanded = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatrixPane)
};

} // namespace melo
