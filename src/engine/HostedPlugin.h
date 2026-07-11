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
    // Message thread only. Failures are written onto the node's pluginError
    // property so the UI can show them; also returned for logging.
    juce::StringArray syncWithModel (juce::ValueTree& graphTree);

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

// Scans the platform's standard VST3 locations (and ~/.vst3 etc) into the
// list. Blocking; first run can take a few seconds with many plugins.
void scanInstalledVST3s (juce::KnownPluginList&);

// Engine node that hands its buffer to a hosted plugin instance. The raw
// pointer is valid for the compiled graph's lifetime because the registry
// only frees instances after the model (and thus any compiled graph
// referencing the node) has dropped them.
//
// Exposed plugin parameters become ParamState slots (id "p<index>", 0..1
// normalised — the VST3 parameter domain). A slot only writes to the plugin
// while a modulation source is connected, so unmodulated params stay under
// the plugin GUI's control.
class HostedNode : public EngineNode
{
public:
    HostedNode (juce::AudioPluginInstance* hosted, const std::vector<int>& exposedIndices)
        : EngineNode (NodeType::hosted), instance (hosted)
    {
        paramIds.reserve (exposedIndices.size());

        for (auto index : exposedIndices)
        {
            juce::AudioProcessorParameter* hostedParam = nullptr;
            if (instance != nullptr && juce::isPositiveAndBelow (index, instance->getParameters().size()))
                hostedParam = instance->getParameters()[index];

            paramIds.push_back ("p" + std::to_string (index));

            ParamState slot;
            slot.spec = ParamSpec { paramIds.back().c_str(), "", 0.0f, 1.0f, 0.5f };
            slot.baseNorm = hostedParam != nullptr ? hostedParam->getValue() : 0.5f;
            params.push_back (slot);
            hostedParams.push_back (hostedParam);
        }
    }

    void process (juce::AudioBuffer<float>& buffer, const ProcessContext&) override
    {
        if (instance == nullptr)
            return;

        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].modSourceIndex >= 0 && hostedParams[i] != nullptr)
                hostedParams[i]->setValue (params[i].current());

        midiScratch.clear();
        instance->processBlock (buffer, midiScratch);
    }

private:
    juce::AudioPluginInstance* instance;
    std::vector<std::string> paramIds;   // stable storage backing ParamSpec::id
    std::vector<juce::AudioProcessorParameter*> hostedParams;
    juce::MidiBuffer midiScratch;
};

} // namespace melo
