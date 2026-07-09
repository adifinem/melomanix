#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Nodes.h"

namespace melo
{

// Owns third-party plugin instances, keyed by graph node id. Lives on the
// message thread and OUTLIVES compiled-graph swaps: recompiling the graph
// must never tear down a hosted plugin (state, GUI, and realtime processing
// all persist across patch edits). Instances die only when their node is
// deleted from the model.
class HostedPluginRegistry
{
public:
    HostedPluginRegistry();
    ~HostedPluginRegistry();

    // Scans the model tree: instantiates plugins for hosted nodes that have
    // none yet (restoring saved state), drops instances whose nodes are gone.
    // Message thread only. Returns error text for any node that failed.
    juce::StringArray syncWithModel (const juce::ValueTree& graphTree);

    // Called from prepareToPlay (audio not running).
    void prepare (double sampleRate, int maxBlockSize);

    // Snapshot every live instance's state into its node tree (for host save).
    void captureStates (juce::ValueTree& graphTree);

    juce::AudioPluginInstance* instanceFor (int nodeId) const;

    // Opens (or refocuses) the plugin's own editor in a floating window.
    void showEditorWindow (int nodeId);

private:
    struct Hosted
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::unique_ptr<juce::DocumentWindow> window;
    };

    juce::AudioPluginFormatManager formatManager;
    std::map<int, Hosted> instances;
    double sampleRate = 44100.0;
    int maxBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedPluginRegistry)
};

// Engine node that hands its buffer to a hosted plugin instance. The raw
// pointer is valid for the compiled graph's lifetime because the registry
// only frees instances after the model (and thus any compiled graph
// referencing the node) has dropped them.
class HostedNode : public EngineNode
{
public:
    explicit HostedNode (juce::AudioPluginInstance* hosted)
        : EngineNode (NodeType::hosted), instance (hosted) {}

    void process (juce::AudioBuffer<float>& buffer, const ProcessContext&) override
    {
        if (instance != nullptr)
        {
            midiScratch.clear();
            instance->processBlock (buffer, midiScratch);
        }
    }

private:
    juce::AudioPluginInstance* instance;
    juce::MidiBuffer midiScratch;
};

} // namespace melo
