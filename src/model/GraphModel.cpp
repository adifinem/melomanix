#include "GraphModel.h"
#include <set>

namespace melo
{

GraphModel::GraphModel()
{
    resetToDefault();
}

void GraphModel::resetToDefault()
{
    tree = juce::ValueTree (ids::graph);
    tree.setProperty (ids::nextNodeId, 1, nullptr);

    auto in  = addNode (NodeType::audioIn,  -300.0f, 0.0f);
    auto out = addNode (NodeType::audioOut,  300.0f, 0.0f);
    addAudioConnection (in, out);
}

void GraphModel::replaceState (const juce::ValueTree& newGraph)
{
    if (newGraph.hasType (ids::graph))
        tree = newGraph;
    else
        resetToDefault();
}

int GraphModel::takeNextNodeId()
{
    int id = tree.getProperty (ids::nextNodeId, 1);
    tree.setProperty (ids::nextNodeId, id + 1, nullptr);
    return id;
}

int GraphModel::addNode (NodeType type, float x, float y)
{
    auto id = takeNextNodeId();

    juce::ValueTree node (ids::node);
    node.setProperty (ids::nodeId, id, nullptr);
    node.setProperty (ids::type, nodeTypeToString (type), nullptr);
    node.setProperty (ids::posX, x, nullptr);
    node.setProperty (ids::posY, y, nullptr);

    for (auto& spec : paramSpecsFor (type))
    {
        juce::ValueTree p (ids::param);
        p.setProperty (ids::paramId, juce::String (spec.id), nullptr);
        p.setProperty (ids::value, spec.def, nullptr);
        node.addChild (p, -1, nullptr);
    }

    // Curves start as a gentle rise-and-fall so patching one is instantly audible.
    if (type == NodeType::curve)
    {
        for (auto [t, v] : { std::pair { 0.0f, 0.25f }, { 0.5f, 0.85f }, { 1.0f, 0.25f } })
        {
            juce::ValueTree p (ids::point);
            p.setProperty (ids::pointT, t, nullptr);
            p.setProperty (ids::pointV, v, nullptr);
            p.setProperty (ids::tension, 0.0f, nullptr);
            node.addChild (p, -1, nullptr);
        }
    }

    tree.addChild (node, -1, nullptr);
    return id;
}

int GraphModel::addMacroNode (int macroIndex, float x, float y)
{
    auto id = addNode (NodeType::macro, x, y);
    getNode (id).setProperty (ids::macroIndex, macroIndex, nullptr);
    return id;
}

int GraphModel::addHostedNode (const juce::String& pluginPath, const juce::String& pluginName, float x, float y)
{
    auto id = addNode (NodeType::hosted, x, y);
    auto node = getNode (id);
    node.setProperty (ids::pluginPath, pluginPath, nullptr);
    node.setProperty (ids::pluginName, pluginName, nullptr);
    return id;
}

void GraphModel::setHostedParamExposed (int nodeId, int paramIndex, const juce::String& name, bool shouldExpose)
{
    auto node = getNode (nodeId);
    if (! node.isValid())
        return;

    for (int i = node.getNumChildren(); --i >= 0;)
    {
        auto child = node.getChild (i);
        if (child.hasType (ids::exposed) && (int) child.getProperty (ids::hostParam) == paramIndex)
        {
            if (shouldExpose)
                return;   // already exposed

            node.removeChild (i, nullptr);
            auto paramId = "p" + juce::String (paramIndex);
            for (int c = tree.getNumChildren(); --c >= 0;)
            {
                auto conn = tree.getChild (c);
                if (conn.hasType (ids::conn) && (int) conn.getProperty (ids::dstNode) == nodeId
                    && conn.getProperty (ids::dstParam).toString() == paramId)
                    tree.removeChild (c, nullptr);
            }
            return;
        }
    }

    if (shouldExpose)
    {
        juce::ValueTree exposedTree (ids::exposed);
        exposedTree.setProperty (ids::hostParam, paramIndex, nullptr);
        exposedTree.setProperty (ids::paramName, name, nullptr);
        node.addChild (exposedTree, -1, nullptr);
    }
}

bool GraphModel::isHostedParamExposed (int nodeId, int paramIndex) const
{
    auto node = getNode (nodeId);
    for (auto child : node)
        if (child.hasType (ids::exposed) && (int) child.getProperty (ids::hostParam) == paramIndex)
            return true;
    return false;
}

int GraphModel::duplicateNode (int nodeId)
{
    auto source = getNode (nodeId);
    if (! source.isValid() || kindOf (getNodeType (source)) == NodeKind::io)
        return -1;

    // Copies params, curve points, exposed hosted params and plugin state;
    // connections are intentionally not copied (fan-in rules would reject
    // most of them — duplicating is about reusing a node's settings).
    auto copy = source.createCopy();
    auto id = takeNextNodeId();
    copy.setProperty (ids::nodeId, id, nullptr);
    copy.setProperty (ids::posX, (float) source.getProperty (ids::posX) + 40.0f, nullptr);
    copy.setProperty (ids::posY, (float) source.getProperty (ids::posY) + 40.0f, nullptr);
    copy.removeProperty (ids::pluginError, nullptr);
    tree.addChild (copy, -1, nullptr);
    return id;
}

void GraphModel::removeNode (int nodeId)
{
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        auto child = tree.getChild (i);
        if (child.hasType (ids::conn)
            && ((int) child.getProperty (ids::srcNode) == nodeId
                || (int) child.getProperty (ids::dstNode) == nodeId))
            tree.removeChild (i, nullptr);
        else if (child.hasType (ids::node) && (int) child.getProperty (ids::nodeId) == nodeId)
            tree.removeChild (i, nullptr);
    }
}

juce::ValueTree GraphModel::getNode (int nodeId) const
{
    for (auto child : tree)
        if (child.hasType (ids::node) && (int) child.getProperty (ids::nodeId) == nodeId)
            return child;
    return {};
}

NodeType GraphModel::getNodeType (const juce::ValueTree& node) const
{
    return nodeTypeFromString (node.getProperty (ids::type).toString());
}

void GraphModel::setNodePosition (int nodeId, float x, float y)
{
    auto node = getNode (nodeId);
    if (node.isValid())
    {
        node.setProperty (ids::posX, x, nullptr);
        node.setProperty (ids::posY, y, nullptr);
    }
}

juce::ValueTree GraphModel::findMacroNode (int macroIndex) const
{
    for (auto child : tree)
        if (child.hasType (ids::node)
            && nodeTypeFromString (child.getProperty (ids::type).toString()) == NodeType::macro
            && (int) child.getProperty (ids::macroIndex, -1) == macroIndex)
            return child;
    return {};
}

bool GraphModel::addAudioConnection (int srcNodeId, int dstNodeId)
{
    auto src = getNode (srcNodeId);
    auto dst = getNode (dstNodeId);
    if (! src.isValid() || ! dst.isValid() || srcNodeId == dstNodeId)
        return false;

    // Only DSP/IO nodes carry audio, and v0 restricts each audio input to one source.
    if (kindOf (getNodeType (src)) == NodeKind::controller
        || kindOf (getNodeType (dst)) == NodeKind::controller
        || getNodeType (src) == NodeType::audioOut
        || getNodeType (dst) == NodeType::audioIn)
        return false;

    if (connectionExistsTo (dstNodeId, {}))
        return false;

    if (wouldCreateCycle (srcNodeId, dstNodeId))
        return false;

    juce::ValueTree conn (ids::conn);
    conn.setProperty (ids::srcNode, srcNodeId, nullptr);
    conn.setProperty (ids::dstNode, dstNodeId, nullptr);
    tree.addChild (conn, -1, nullptr);
    return true;
}

bool GraphModel::addModConnection (int srcNodeId, int dstNodeId, const juce::String& paramId,
                                   float depth, int srcOut)
{
    auto src = getNode (srcNodeId);
    auto dst = getNode (dstNodeId);
    if (! src.isValid() || ! dst.isValid() || srcNodeId == dstNodeId)
        return false;

    if (kindOf (getNodeType (src)) != NodeKind::controller)
        return false;

    // The target param must exist on the destination node. Hosted nodes
    // validate against their exposed parameters instead of static specs.
    bool paramExists = false;
    if (getNodeType (dst) == NodeType::hosted)
    {
        for (auto child : dst)
            if (child.hasType (ids::exposed)
                && "p" + child.getProperty (ids::hostParam).toString() == paramId)
                paramExists = true;
    }
    else
    {
        for (auto& spec : paramSpecsFor (getNodeType (dst)))
            if (paramId == spec.id)
                paramExists = true;
    }
    if (! paramExists)
        return false;

    // v0 fan-in restriction: one modulation source per parameter socket.
    if (connectionExistsTo (dstNodeId, paramId))
        return false;

    if (wouldCreateCycle (srcNodeId, dstNodeId))
        return false;

    juce::ValueTree conn (ids::conn);
    conn.setProperty (ids::srcNode, srcNodeId, nullptr);
    conn.setProperty (ids::dstNode, dstNodeId, nullptr);
    conn.setProperty (ids::dstParam, paramId, nullptr);
    conn.setProperty (ids::depth, depth, nullptr);
    if (srcOut != 0)
        conn.setProperty (ids::srcOut, srcOut, nullptr);
    tree.addChild (conn, -1, nullptr);
    return true;
}

void GraphModel::removeConnection (const juce::ValueTree& conn)
{
    tree.removeChild (conn, nullptr);
}

bool GraphModel::connectionExistsTo (int dstNodeId, const juce::String& paramId) const
{
    for (auto child : tree)
    {
        if (! child.hasType (ids::conn) || (int) child.getProperty (ids::dstNode) != dstNodeId)
            continue;
        auto existing = child.getProperty (ids::dstParam).toString();
        if (existing == paramId)
            return true;
    }
    return false;
}

bool GraphModel::wouldCreateCycle (int srcNodeId, int dstNodeId) const
{
    // Adding src->dst creates a cycle iff src is reachable from dst
    // following existing edges of either kind.
    std::vector<int> stack { dstNodeId };
    std::set<int> seen;

    while (! stack.empty())
    {
        auto current = stack.back();
        stack.pop_back();
        if (current == srcNodeId)
            return true;
        if (! seen.insert (current).second)
            continue;

        for (auto child : tree)
            if (child.hasType (ids::conn) && (int) child.getProperty (ids::srcNode) == current)
                stack.push_back (child.getProperty (ids::dstNode));
    }
    return false;
}

void GraphModel::setParamValue (int nodeId, const juce::String& paramId, float value)
{
    auto node = getNode (nodeId);
    for (auto p : node)
        if (p.hasType (ids::param) && p.getProperty (ids::paramId).toString() == paramId)
            p.setProperty (ids::value, value, nullptr);
}

float GraphModel::getParamValue (int nodeId, const juce::String& paramId) const
{
    auto node = getNode (nodeId);
    for (auto p : node)
        if (p.hasType (ids::param) && p.getProperty (ids::paramId).toString() == paramId)
            return p.getProperty (ids::value);
    return 0.0f;
}

} // namespace melo
