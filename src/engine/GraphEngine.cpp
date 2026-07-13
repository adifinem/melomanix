#include "GraphEngine.h"
#include "HostedPlugin.h"
#include <algorithm>
#include <map>

namespace melo
{

void CompiledGraph::prepare (double sampleRate, int maxBlockSize)
{
    ProcessContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.maxBlockSize = maxBlockSize;

    buffers.clear();
    for (auto& node : nodes)
    {
        node->prepare (ctx);
        buffers.emplace_back (2, maxBlockSize);
    }
}

void CompiledGraph::process (juce::AudioBuffer<float>& hostBuffer, ProcessContext& ctx)
{
    auto numSamples  = ctx.numSamples;
    auto numChannels = juce::jmin (hostBuffer.getNumChannels(), 2);

    if (audioInIndex >= 0)
        for (int ch = 0; ch < numChannels; ++ch)
            buffers[(size_t) audioInIndex].copyFrom (ch, 0, hostBuffer, ch, 0, numSamples);

    for (auto index : topoOrder)
    {
        auto& node = *nodes[(size_t) index];
        node.updateParams (nodes);

        if (kindOf (node.type) == NodeKind::controller)
        {
            node.lastOutput = node.evaluate (ctx);
            continue;
        }

        auto& buffer = buffers[(size_t) index];
        if (auto src = audioSource[(size_t) index]; src >= 0)
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.copyFrom (ch, 0, buffers[(size_t) src], ch, 0, numSamples);
        else if (node.type != NodeType::audioIn)
            buffer.clear();

        if (kindOf (node.type) == NodeKind::dsp)
        {
            // Process only the live region without reallocating.
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), numChannels, numSamples);
            node.process (view, ctx);
        }
    }

    if (audioOutIndex >= 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            hostBuffer.copyFrom (ch, 0, buffers[(size_t) audioOutIndex], ch, 0, numSamples);
    }
    else
    {
        hostBuffer.clear();
    }
}

void GraphEngine::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    if (auto graph = active.load())
        graph->prepare (sampleRate, maxBlockSize);
}

void GraphEngine::setGraph (std::shared_ptr<CompiledGraph> newGraph)
{
    if (newGraph != nullptr)
        newGraph->prepare (sampleRate, maxBlockSize);

    retired = active.exchange (std::move (newGraph));
}

void GraphEngine::process (juce::AudioBuffer<float>& buffer, ProcessContext& ctx)
{
    if (auto graph = active.load())
        graph->process (buffer, ctx);
}

std::shared_ptr<CompiledGraph> compileGraph (const juce::ValueTree& graphTree,
                                             const std::vector<std::atomic<float>*>& macroValues,
                                             const std::function<juce::AudioPluginInstance* (int)>& hostedInstanceLookup)
{
    auto compiled = std::make_shared<CompiledGraph>();
    std::map<int, int> modelIdToIndex;

    for (auto child : graphTree)
    {
        if (! child.hasType (ids::node))
            continue;

        auto type = nodeTypeFromString (child.getProperty (ids::type).toString());
        std::unique_ptr<EngineNode> node;

        switch (type)
        {
            case NodeType::audioIn:
            case NodeType::audioOut: node = std::make_unique<AudioIONode> (type); break;
            case NodeType::eq:       node = std::make_unique<EQNode>();  break;
            case NodeType::delay:    node = std::make_unique<DelayNode>(); break;
            case NodeType::lfo:      node = std::make_unique<LFONode>(); break;
            case NodeType::curve:
            {
                auto curve = std::make_unique<CurveNode>();
                for (auto pointTree : child)
                    if (pointTree.hasType (ids::point))
                        curve->points.push_back ({ pointTree.getProperty (ids::pointT, 0.0f),
                                                   pointTree.getProperty (ids::pointV, 0.5f),
                                                   pointTree.getProperty (ids::tension, 0.0f) });
                std::sort (curve->points.begin(), curve->points.end(),
                           [] (auto& a, auto& b) { return a.t < b.t; });
                node = std::move (curve);
                break;
            }
            case NodeType::hosted:
            {
                int hostedNodeId = child.getProperty (ids::nodeId);
                std::vector<int> exposedIndices;
                for (auto exposedTree : child)
                    if (exposedTree.hasType (ids::exposed))
                        exposedIndices.push_back (exposedTree.getProperty (ids::hostParam));
                node = std::make_unique<HostedNode> (
                    hostedInstanceLookup != nullptr ? hostedInstanceLookup (hostedNodeId) : nullptr,
                    exposedIndices);
                break;
            }
            case NodeType::macro:
            {
                int macroIndex = child.getProperty (ids::macroIndex, 0);
                auto* value = juce::isPositiveAndBelow (macroIndex, (int) macroValues.size())
                                  ? macroValues[(size_t) macroIndex] : nullptr;
                node = std::make_unique<MacroNode> (macroIndex, value);
                break;
            }
        }

        node->modelNodeId = child.getProperty (ids::nodeId);

        for (auto paramTree : child)
        {
            if (! paramTree.hasType (ids::param))
                continue;
            auto index = node->indexOfParam (paramTree.getProperty (ids::paramId).toString());
            if (index >= 0)
                node->params[(size_t) index].baseNorm =
                    node->params[(size_t) index].spec.normalise (paramTree.getProperty (ids::value));
        }

        auto nodeIndex = (int) compiled->nodes.size();
        modelIdToIndex[node->modelNodeId] = nodeIndex;
        if (type == NodeType::audioIn)  compiled->audioInIndex = nodeIndex;
        if (type == NodeType::audioOut) compiled->audioOutIndex = nodeIndex;
        compiled->nodes.push_back (std::move (node));
    }

    auto numNodes = (int) compiled->nodes.size();
    compiled->audioSource.assign ((size_t) numNodes, -1);
    std::vector<std::vector<int>> outEdges ((size_t) numNodes);
    std::vector<int> inDegree ((size_t) numNodes, 0);

    for (auto child : graphTree)
    {
        if (! child.hasType (ids::conn))
            continue;

        auto srcIt = modelIdToIndex.find (child.getProperty (ids::srcNode));
        auto dstIt = modelIdToIndex.find (child.getProperty (ids::dstNode));
        if (srcIt == modelIdToIndex.end() || dstIt == modelIdToIndex.end())
            continue;

        auto src = srcIt->second, dst = dstIt->second;

        if (child.hasProperty (ids::dstParam))
        {
            auto paramIndex = compiled->nodes[(size_t) dst]->indexOfParam (
                child.getProperty (ids::dstParam).toString());
            if (paramIndex < 0)
                continue;
            auto& param = compiled->nodes[(size_t) dst]->params[(size_t) paramIndex];
            param.modSourceIndex = src;
            param.modDepth = child.getProperty (ids::depth, 1.0f);
            param.modOffset = child.getProperty (ids::offset, 0.0f);

            // A drawn morph curve (POINT children) overrides the linear map.
            for (auto pointTree : child)
                if (pointTree.hasType (ids::point))
                    param.morphPoints.push_back ({ pointTree.getProperty (ids::pointT, 0.0f),
                                                   pointTree.getProperty (ids::pointV, 0.5f),
                                                   pointTree.getProperty (ids::tension, 0.0f) });
            std::sort (param.morphPoints.begin(), param.morphPoints.end(),
                       [] (auto& a, auto& b) { return a.t < b.t; });
        }
        else
        {
            compiled->audioSource[(size_t) dst] = src;
        }

        outEdges[(size_t) src].push_back (dst);
        ++inDegree[(size_t) dst];
    }

    // Kahn topological sort. The model rejects cycles, so this always
    // completes; if it somehow doesn't, fall back to an empty (silent) graph.
    std::vector<int> queue;
    for (int i = 0; i < numNodes; ++i)
        if (inDegree[(size_t) i] == 0)
            queue.push_back (i);

    for (size_t head = 0; head < queue.size(); ++head)
    {
        auto current = queue[head];
        compiled->topoOrder.push_back (current);
        for (auto next : outEdges[(size_t) current])
            if (--inDegree[(size_t) next] == 0)
                queue.push_back (next);
    }

    if ((int) compiled->topoOrder.size() != numNodes)
    {
        jassertfalse;
        return std::make_shared<CompiledGraph>();
    }

    return compiled;
}

} // namespace melo
