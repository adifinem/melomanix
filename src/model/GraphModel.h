#pragma once

#include "../engine/GraphTypes.h"

namespace melo
{

// Message-thread-only wrapper around the graph ValueTree. All structural
// rules live here: unique node ids, one mod source per (node, param) socket
// (v0 fan-in restriction), no cycles across audio+mod edges combined.
class GraphModel
{
public:
    GraphModel();

    juce::ValueTree& state() { return tree; }
    void replaceState (const juce::ValueTree& newGraph);

    int  addNode (NodeType type, float x, float y);
    // Macro nodes carry the index of the host macro parameter they mirror.
    int  addMacroNode (int macroIndex, float x, float y);
    void removeNode (int nodeId);

    // Audio edge (src's audio out -> dst's audio in). Returns false if rejected.
    bool addAudioConnection (int srcNodeId, int dstNodeId);
    // Mod edge (controller output -> a parameter socket on dst). Returns false if rejected.
    bool addModConnection (int srcNodeId, int dstNodeId, const juce::String& paramId, float depth = 1.0f);
    void removeConnection (const juce::ValueTree& conn);

    void setParamValue (int nodeId, const juce::String& paramId, float denormalisedValue);
    float getParamValue (int nodeId, const juce::String& paramId) const;

    juce::ValueTree getNode (int nodeId) const;
    NodeType getNodeType (const juce::ValueTree& node) const;
    void setNodePosition (int nodeId, float x, float y);

    // Existing macro node for a strip index, or invalid tree if none yet.
    juce::ValueTree findMacroNode (int macroIndex) const;

    // Fresh default patch: audioIn -> audioOut.
    void resetToDefault();

private:
    bool wouldCreateCycle (int srcNodeId, int dstNodeId) const;
    bool connectionExistsTo (int dstNodeId, const juce::String& paramId) const;
    int  takeNextNodeId();

    juce::ValueTree tree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphModel)
};

} // namespace melo
