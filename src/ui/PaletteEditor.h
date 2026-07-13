#pragma once

#include "../model/GraphModel.h"
#include "Theme.h"

namespace melo
{

// Free-form colour customisation (issue #8). Pick a role, drag the colour
// wheel; the change applies live and is written to the project so it loads
// back. Selecting the "Custom" palette in the Appearance menu is implied by
// editing here — the graph root's palette name becomes "Custom".
class PaletteEditor : public juce::Component,
                      private juce::ChangeListener
{
public:
    PaletteEditor (GraphModel& m, std::function<void()> onChanged)
        : model (m), notify (std::move (onChanged))
    {
        roleBox.setColour (juce::ComboBox::backgroundColourId, theme::nodeBody);
        for (int i = 0; i < (int) roles.size(); ++i)
            roleBox.addItem (roles[(size_t) i].label, i + 1);
        roleBox.setSelectedId (1, juce::dontSendNotification);
        roleBox.onChange = [this] { loadSelectedRole(); };
        addAndMakeVisible (roleBox);

        selector.setColour (juce::ColourSelector::backgroundColourId, theme::panel);
        selector.addChangeListener (this);
        addAndMakeVisible (selector);

        reset.setButtonText ("Reset all to Slate");
        reset.onClick = [this] { resetToSlate(); };
        addAndMakeVisible (reset);

        setSize (300, 340);
        loadSelectedRole();
    }

    ~PaletteEditor() override { selector.removeChangeListener (this); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        roleBox.setBounds (area.removeFromTop (24));
        reset.setBounds (area.removeFromBottom (26).reduced (0, 2));
        selector.setBounds (area.reduced (0, 6));
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }

private:
    const theme::ColourRole& current() const { return roles[(size_t) roleBox.getSelectedId() - 1]; }

    void loadSelectedRole()
    {
        guard = true;
        selector.setCurrentColour (*current().target, juce::dontSendNotification);
        guard = false;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (guard)
            return;

        auto& role = current();
        auto colour = selector.getCurrentColour();
        *role.target = colour;

        // Persist onto the project and flip the active palette to Custom.
        model.state().setProperty (theme::colourPropertyFor (role.key), colour.toString(), nullptr);
        model.state().setProperty (ids::palette, "Custom", nullptr);
        theme::currentPaletteName = "Custom";

        if (notify != nullptr)
            notify();
    }

    void resetToSlate()
    {
        theme::applyPalette ("Slate");
        for (auto& role : roles)
            model.state().removeProperty (theme::colourPropertyFor (role.key), nullptr);
        model.state().setProperty (ids::palette, "Slate", nullptr);
        loadSelectedRole();
        if (notify != nullptr)
            notify();
    }

    GraphModel& model;
    std::function<void()> notify;
    std::vector<theme::ColourRole> roles { theme::customisableRoles() };
    juce::ComboBox roleBox;
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop
                                    | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace };
    juce::TextButton reset;
    bool guard = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaletteEditor)
};

} // namespace melo
