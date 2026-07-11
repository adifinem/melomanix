#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace melo
{

// The primary (last-clicked) node drives the timeline pane; the multi set
// holds everything inside the latest rubber-band (or just the primary).
// Group operations (move/duplicate/delete) act on the multi set.
class SelectionModel : public juce::ChangeBroadcaster
{
public:
    // Single click: node becomes primary and the only member of the set.
    void select (int nodeId)
    {
        auto wanted = nodeId >= 0 ? std::set<int> { nodeId } : std::set<int> {};
        if (selectedNodeId != nodeId || multi != wanted)
        {
            selectedNodeId = nodeId;
            multi = std::move (wanted);
            sendChangeMessage();
        }
    }

    // Clicking a node that's already in a group keeps the group and only
    // retargets the primary (so the timeline follows without breaking it).
    void setPrimaryKeepingGroup (int nodeId)
    {
        if (! isSelected (nodeId))
        {
            select (nodeId);
            return;
        }
        if (selectedNodeId != nodeId)
        {
            selectedNodeId = nodeId;
            sendChangeMessage();
        }
    }

    // Rubber-band result. The primary survives only if it's in the band.
    void setMultiple (std::set<int> nodeIds)
    {
        if (multi == nodeIds)
            return;
        multi = std::move (nodeIds);
        if (multi.count (selectedNodeId) == 0)
            selectedNodeId = -1;
        sendChangeMessage();
    }

    void clearIfSelected (int nodeId)
    {
        auto erased = multi.erase (nodeId) > 0;
        if (selectedNodeId == nodeId)
            selectedNodeId = -1;
        if (erased || selectedNodeId == -1)
            sendChangeMessage();
    }

    bool isSelected (int nodeId) const { return nodeId == selectedNodeId || multi.count (nodeId) > 0; }
    int getSelectedNodeId() const      { return selectedNodeId; }
    const std::set<int>& getGroup() const { return multi; }
    bool isGroup() const               { return multi.size() > 1; }

private:
    int selectedNodeId = -1;
    std::set<int> multi;
};

} // namespace melo
