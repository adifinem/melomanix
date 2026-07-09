#include "GraphCanvas.h"

namespace melo
{

GraphCanvas::GraphCanvas (GraphModel& m, SelectionModel& sel)
    : model (m), selection (sel)
{
    observedTree = model.state();
    observedTree.addListener (this);
    startTimer (500);   // watches for wholesale tree replacement (state load)
    rebuild();
}

GraphCanvas::~GraphCanvas()
{
    observedTree.removeListener (this);
}

// Mapping: logical (L) space holds nodes at contentPos + panOffset/zoom with a
// scale(zoom) transform per node, so canvas coords = contentPos*zoom + panOffset.
juce::Point<float> GraphCanvas::toCanvas (juce::Point<float> contentPos) const
{
    return contentPos * zoom + panOffset;
}

juce::Point<float> GraphCanvas::contentPosFor (const juce::ValueTree& node) const
{
    return { node.getProperty (ids::posX, 0.0f), node.getProperty (ids::posY, 0.0f) };
}

void GraphCanvas::applyViewTransform()
{
    for (auto& comp : nodeComps)
    {
        auto node = model.getNode (comp->getNodeId());
        auto logical = contentPosFor (node) + panOffset / zoom;
        comp->setTransform (juce::AffineTransform::scale (zoom));
        comp->setTopLeftPosition (logical.roundToInt());
    }
    repaint();
}

void GraphCanvas::resized()
{
    if (! centredOnce && getWidth() > 0)
    {
        centredOnce = true;
        panOffset = { getWidth() * 0.5f, getHeight() * 0.45f };
    }
    applyViewTransform();
}

void GraphCanvas::rebuild()
{
    nodeComps.clear();

    for (auto child : model.state())
    {
        if (! child.hasType (ids::node))
            continue;

        auto comp = std::make_unique<NodeComponent> (model, child, selection);
        addAndMakeVisible (*comp);
        nodeComps.push_back (std::move (comp));
    }

    applyViewTransform();
}

NodeComponent* GraphCanvas::findNodeComponent (int nodeId) const
{
    for (auto& comp : nodeComps)
        if (comp->getNodeId() == nodeId)
            return comp.get();
    return nullptr;
}

// --- painting -------------------------------------------------------------

void GraphCanvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Dot grid, spaced in content units so it moves with pan/zoom.
    g.setColour (theme::panel.brighter (0.06f));
    const float spacing = 28.0f * zoom;
    if (spacing > 8.0f)
    {
        auto offsetX = std::fmod (panOffset.x, spacing);
        auto offsetY = std::fmod (panOffset.y, spacing);
        for (float x = offsetX; x < (float) getWidth(); x += spacing)
            for (float y = offsetY; y < (float) getHeight(); y += spacing)
                g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
    }

    drawCables (g);
}

static void drawCablePath (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to,
                           juce::Colour colour, float thickness)
{
    juce::Path path;
    auto bend = juce::jmax (30.0f, std::abs (to.x - from.x) * 0.5f);
    path.startNewSubPath (from);
    path.cubicTo (from.translated (bend, 0.0f), to.translated (-bend, 0.0f), to);
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved));
}

void GraphCanvas::drawCables (juce::Graphics& g)
{
    for (auto child : model.state())
    {
        if (! child.hasType (ids::conn))
            continue;

        auto* srcComp = findNodeComponent (child.getProperty (ids::srcNode));
        auto* dstComp = findNodeComponent (child.getProperty (ids::dstNode));
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        auto isMod = child.hasProperty (ids::dstParam);
        auto from = srcComp->socketCentreInParent (isMod ? SocketKind::ctrlOut : SocketKind::audioOut);
        auto to = isMod
                      ? dstComp->socketCentreInParent (SocketKind::paramIn,
                                                       child.getProperty (ids::dstParam).toString())
                      : dstComp->socketCentreInParent (SocketKind::audioIn);

        drawCablePath (g, from * zoom, to * zoom,
                       (isMod ? theme::controlSignal : theme::audioSignal).withAlpha (0.85f),
                       (isMod ? 1.6f : 2.4f) * zoom);
    }

    if (dragSourceSocket != nullptr)
    {
        auto* nodeComp = findNodeComponent (dragSourceSocket->nodeId);
        if (nodeComp != nullptr)
        {
            auto from = nodeComp->socketCentreInParent (dragSourceSocket->kind, dragSourceSocket->paramId);
            drawCablePath (g, from * zoom, dragCableEnd * zoom,
                           (dragSourceSocket->isAudio() ? theme::audioSignal : theme::controlSignal)
                               .withAlpha (0.6f),
                           1.8f * zoom);
        }
    }
}

// --- background interaction -------------------------------------------------

void GraphCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showAddNodeMenu (e.getPosition());
        return;
    }
    panDragStart = panOffset;
}

void GraphCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
    {
        panOffset = panDragStart + e.getOffsetFromDragStart().toFloat();
        applyViewTransform();
    }
}

void GraphCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto oldZoom = zoom;
    zoom = juce::jlimit (0.4f, 1.8f, zoom * (wheel.deltaY > 0 ? 1.1f : 1.0f / 1.1f));

    // Keep the content point under the cursor stationary.
    auto mouse = e.getPosition().toFloat();
    auto content = (mouse - panOffset) / oldZoom;
    panOffset = mouse - content * zoom;

    applyViewTransform();
}

void GraphCanvas::showAddNodeMenu (juce::Point<int> canvasPos)
{
    auto contentPos = (canvasPos.toFloat() - panOffset) / zoom;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Add node");
    menu.addItem (1, "LFO");
    menu.addItem (2, "EQ");
    menu.addItem (3, "Delay");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, contentPos] (int result)
                        {
                            if (result == 1) model.addNode (NodeType::lfo,   contentPos.x, contentPos.y);
                            if (result == 2) model.addNode (NodeType::eq,    contentPos.x, contentPos.y);
                            if (result == 3) model.addNode (NodeType::delay, contentPos.x, contentPos.y);
                        });
}

// --- cable gesture ----------------------------------------------------------

void GraphCanvas::beginCableDrag (Socket& socket)
{
    dragSourceSocket = &socket;
    auto* nodeComp = findNodeComponent (socket.nodeId);
    dragCableEnd = nodeComp != nullptr
                       ? nodeComp->socketCentreInParent (socket.kind, socket.paramId)
                       : juce::Point<float>();
    repaint();
}

void GraphCanvas::updateCableDrag (const juce::MouseEvent& e)
{
    if (dragSourceSocket == nullptr)
        return;

    auto canvasPos = e.getEventRelativeTo (this).position;
    dragCableEnd = (canvasPos - panOffset) / zoom;
    repaint();
}

void GraphCanvas::endCableDrag (const juce::MouseEvent& e)
{
    if (dragSourceSocket == nullptr)
        return;

    auto* source = dragSourceSocket;
    dragSourceSocket = nullptr;
    repaint();

    // Find a socket under the pointer.
    Socket* target = nullptr;
    for (auto& comp : nodeComps)
    {
        auto local = e.getEventRelativeTo (comp.get()).getPosition();
        if (comp->getLocalBounds().expanded (6).contains (local))
            if (auto* found = comp->socketAt (local))
            {
                target = found;
                break;
            }
    }

    if (target == nullptr || target == source)
        return;

    // Accept either drag direction; canonicalise to output -> input.
    auto* out = source->isInput() ? target : source;
    auto* in  = source->isInput() ? source : target;
    if (out->isInput() || ! in->isInput())
        return;

    if (out->kind == SocketKind::audioOut && in->kind == SocketKind::audioIn)
        model.addAudioConnection (out->nodeId, in->nodeId);
    else if (out->kind == SocketKind::ctrlOut && in->kind == SocketKind::paramIn)
        model.addModConnection (out->nodeId, in->nodeId, in->paramId);
}

void GraphCanvas::showDisconnectMenu (Socket& socket)
{
    juce::Array<juce::ValueTree> matches;

    for (auto child : model.state())
    {
        if (! child.hasType (ids::conn))
            continue;

        auto isMod = child.hasProperty (ids::dstParam);
        auto src = (int) child.getProperty (ids::srcNode);
        auto dst = (int) child.getProperty (ids::dstNode);

        bool involved = false;
        switch (socket.kind)
        {
            case SocketKind::audioIn:  involved = ! isMod && dst == socket.nodeId; break;
            case SocketKind::audioOut: involved = ! isMod && src == socket.nodeId; break;
            case SocketKind::ctrlOut:  involved = isMod && src == socket.nodeId; break;
            case SocketKind::paramIn:  involved = isMod && dst == socket.nodeId
                                           && child.getProperty (ids::dstParam).toString() == socket.paramId;
                                       break;
        }
        if (involved)
            matches.add (child);
    }

    if (matches.isEmpty())
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < matches.size(); ++i)
    {
        auto other = socket.isInput() ? matches[i].getProperty (ids::srcNode)
                                      : matches[i].getProperty (ids::dstNode);
        auto otherNode = model.getNode (other);
        auto title = theme::titleFor (nodeTypeFromString (otherNode.getProperty (ids::type).toString()),
                                      (int) otherNode.getProperty (ids::macroIndex, -1));
        auto param = matches[i].getProperty (ids::dstParam).toString();
        menu.addItem (i + 1, "Disconnect " + title + (param.isNotEmpty() ? " (" + param + ")" : ""));
    }

    // Depth choices for a modulated param socket — a stopgap until the
    // matrix pane brings proper per-connection amount/transfer curves.
    static constexpr float depths[] { 1.0f, 0.5f, 0.25f, 0.1f, -0.25f, -0.5f, -1.0f };
    if (socket.kind == SocketKind::paramIn && matches.size() == 1)
    {
        juce::PopupMenu depthMenu;
        auto current = (float) matches[0].getProperty (ids::depth, 1.0f);
        for (int i = 0; i < (int) std::size (depths); ++i)
            depthMenu.addItem (100 + i, juce::String ((int) (depths[i] * 100)) + "%",
                               true, juce::approximatelyEqual (current, depths[i]));
        menu.addSubMenu ("Mod depth", depthMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [this, matches] (int result)
                        {
                            if (result >= 100)
                                juce::ValueTree (matches[0]).setProperty (ids::depth,
                                                                          depths[result - 100], nullptr);
                            else if (result > 0)
                                model.removeConnection (matches[result - 1]);
                        });
}

// --- node drag bookkeeping ---------------------------------------------------

void GraphCanvas::nodeMoved (NodeComponent&)
{
    repaint();   // cables track the moving node
}

void GraphCanvas::nodeDragFinished (NodeComponent& comp)
{
    auto content = comp.getPosition().toFloat() - panOffset / zoom;
    model.setNodePosition (comp.getNodeId(), content.x, content.y);
}

void GraphCanvas::nodePositionChangedInModel (NodeComponent& comp)
{
    auto node = model.getNode (comp.getNodeId());
    comp.setTopLeftPosition ((contentPosFor (node) + panOffset / zoom).roundToInt());
    repaint();
}

// --- model observation --------------------------------------------------------

void GraphCanvas::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (ids::node) && parent == observedTree)
        rebuild();
    else
        repaint();
}

void GraphCanvas::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (ids::node) && parent == observedTree)
        rebuild();
    else
        repaint();
}

void GraphCanvas::timerCallback()
{
    if (model.state() != observedTree)
    {
        observedTree.removeListener (this);
        observedTree = model.state();
        observedTree.addListener (this);
        rebuild();
    }
}

} // namespace melo
