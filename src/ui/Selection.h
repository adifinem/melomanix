#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace melo
{

// Which node was last clicked. The timeline pane retargets from this —
// single-select only in v0 (spec open question #3 deferred).
class SelectionModel : public juce::ChangeBroadcaster
{
public:
    void select (int nodeId)
    {
        if (selectedNodeId != nodeId)
        {
            selectedNodeId = nodeId;
            sendChangeMessage();
        }
    }

    void clearIfSelected (int nodeId)
    {
        if (selectedNodeId == nodeId)
            select (-1);
    }

    int getSelectedNodeId() const { return selectedNodeId; }

private:
    int selectedNodeId = -1;
};

} // namespace melo
