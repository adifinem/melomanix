#pragma once

#include "Nodes.h"

namespace juce { class AudioPluginInstance; }

namespace melo
{

// Immutable, audio-thread-ready form of the graph. Built on the message
// thread by compile(), then swapped in atomically. No allocation, locks or
// ValueTree access happens on the audio thread.
struct CompiledGraph
{
    std::vector<std::unique_ptr<EngineNode>> nodes;
    std::vector<int> topoOrder;                    // indices into nodes
    std::vector<int> audioSource;                  // per node: index feeding its audio in, -1 = none
    std::vector<juce::AudioBuffer<float>> buffers; // per node scratch, sized in prepare()
    int audioInIndex = -1, audioOutIndex = -1;

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& hostBuffer, ProcessContext& ctx);
};

class GraphEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    // Message thread. Prepares the new graph, then publishes it.
    void setGraph (std::shared_ptr<CompiledGraph> newGraph);

    // Audio thread.
    void process (juce::AudioBuffer<float>& buffer, ProcessContext& ctx);

    // Message thread, display only (cable glow): a controller's most recent
    // block value in [0,1], 0 if unknown. Reads a float the audio thread
    // writes without synchronisation — fine for pixels.
    float controllerValueFor (int modelNodeId) const
    {
        if (auto graph = active.load())
            for (auto& node : graph->nodes)
                if (node->modelNodeId == modelNodeId)
                    return node->lastOutput;
        return 0.0f;
    }

private:
    std::atomic<std::shared_ptr<CompiledGraph>> active { nullptr };

    // Retains the previously active graph so its destruction happens here
    // (message thread) rather than on the audio thread when refcounts drop.
    // Assumes swaps are far slower than one audio block, fine for v0.
    std::shared_ptr<CompiledGraph> retired;

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
};

// Builds a CompiledGraph from the model tree. `macroValues` supplies the
// host-parameter storage that macro nodes read (indexed by macro number).
// `hostedInstanceLookup` resolves a hosted node's plugin instance by node id
// (nullable — hosted nodes then compile as silent passthroughs, e.g. in tests).
std::shared_ptr<CompiledGraph> compileGraph (const juce::ValueTree& graphTree,
                                             const std::vector<std::atomic<float>*>& macroValues,
                                             const std::function<juce::AudioPluginInstance* (int)>& hostedInstanceLookup = nullptr);

} // namespace melo
