#include "NodeComponent.h"
#include "GraphCanvas.h"

namespace melo
{

// --- Socket -----------------------------------------------------------

Socket::Socket (SocketKind k, int node, juce::String param)
    : kind (k), nodeId (node), paramId (std::move (param))
{
    setSize (14, 14);
}

void Socket::paint (juce::Graphics& g)
{
    auto colour = isAudio() ? theme::audioSignal : theme::controlSignal;
    auto circle = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (colour);
    g.fillEllipse (circle);
    g.setColour (theme::background);
    g.drawEllipse (circle, 1.0f);
}

void Socket::mouseDown (const juce::MouseEvent& e)
{
    if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
    {
        if (e.mods.isPopupMenu())
            canvas->showDisconnectMenu (*this);
        else
            canvas->beginCableDrag (*this);
    }
}

void Socket::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
        canvas->updateCableDrag (e);
}

void Socket::mouseUp (const juce::MouseEvent& e)
{
    if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
        canvas->endCableDrag (e);
}

// --- NodeComponent ------------------------------------------------------

NodeComponent::NodeComponent (GraphModel& m, juce::ValueTree nodeTree, SelectionModel& sel)
    : model (m),
      tree (nodeTree),
      selection (sel),
      nodeId (nodeTree.getProperty (ids::nodeId)),
      type (nodeTypeFromString (nodeTree.getProperty (ids::type).toString()))
{
    tree.addListener (this);

    auto& specs = paramSpecsFor (type);
    for (auto& spec : specs)
    {
        auto row = std::make_unique<ParamRow>();
        row->spec = spec;

        row->label.setText (spec.name, juce::dontSendNotification);
        row->label.setFont (juce::FontOptions (11.0f));
        row->label.setColour (juce::Label::textColourId, theme::textDim);
        row->label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (row->label);

        row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        // Stepped params (like LFO shape) snap to integers.
        auto interval = juce::String (spec.id) == "shape" ? 1.0 : 0.0;
        row->slider.setNormalisableRange ({ (double) spec.min, (double) spec.max,
                                            interval, (double) spec.skew });
        row->slider.setValue (model.getParamValue (nodeId, spec.id), juce::dontSendNotification);
        row->slider.setColour (juce::Slider::trackColourId, theme::controlSignal.withAlpha (0.4f));
        row->slider.onValueChange = [this, id = juce::String (spec.id), s = &row->slider]
        {
            if (! updatingFromTree)
                model.setParamValue (nodeId, id, (float) s->getValue());
        };
        addAndMakeVisible (row->slider);

        row->socket = std::make_unique<Socket> (SocketKind::paramIn, nodeId, spec.id);
        addAndMakeVisible (*row->socket);

        rows.push_back (std::move (row));
    }

    if (type != NodeType::audioIn && kindOf (type) != NodeKind::controller)
        addAndMakeVisible (*(audioInSocket = std::make_unique<Socket> (SocketKind::audioIn, nodeId)));
    if (type != NodeType::audioOut && kindOf (type) != NodeKind::controller)
        addAndMakeVisible (*(audioOutSocket = std::make_unique<Socket> (SocketKind::audioOut, nodeId)));
    if (kindOf (type) == NodeKind::controller)
        addAndMakeVisible (*(ctrlOutSocket = std::make_unique<Socket> (SocketKind::ctrlOut, nodeId)));

    setSize (width, headerHeight + (int) rows.size() * rowHeight + 8);
}

NodeComponent::~NodeComponent()
{
    tree.removeListener (this);
}

void NodeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    auto isSelected = selection.getSelectedNodeId() == nodeId;

    g.setColour (theme::nodeBody);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto header = bounds.removeFromTop ((float) headerHeight);
    g.setColour (theme::headerFor (kindOf (type)));
    g.fillRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight() + 6.0f, 6.0f);
    g.setColour (theme::nodeBody);
    g.fillRect (header.getX(), header.getBottom(), header.getWidth(), 6.0f);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (theme::titleFor (type, (int) tree.getProperty (ids::macroIndex, -1)),
                header.reduced (18.0f, 0.0f), juce::Justification::centred);

    if (highlightedRow >= 0)
    {
        g.setColour (theme::controlSignal.withAlpha (0.25f));
        g.fillRect (2, headerHeight + highlightedRow * rowHeight, getWidth() - 4, rowHeight);
    }

    g.setColour (isSelected ? theme::nodeSelected : juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, isSelected ? 2.0f : 1.0f);
}

void NodeComponent::resized()
{
    if (audioInSocket)  audioInSocket->setCentrePosition (7, headerHeight / 2);
    if (audioOutSocket) audioOutSocket->setCentrePosition (getWidth() - 7, headerHeight / 2);
    if (ctrlOutSocket)  ctrlOutSocket->setCentrePosition (getWidth() - 7, headerHeight / 2);

    auto y = headerHeight;
    for (auto& row : rows)
    {
        row->socket->setCentrePosition (7, y + rowHeight / 2);
        row->label.setBounds (16, y, 64, rowHeight);
        row->slider.setBounds (80, y, getWidth() - 88, rowHeight);
        y += rowHeight;
    }
}

void NodeComponent::mouseDown (const juce::MouseEvent& e)
{
    selection.select (nodeId);
    getParentComponent()->repaint();

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }
    dragger.startDraggingComponent (this, e);
}

void NodeComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
    {
        dragger.dragComponent (this, e, nullptr);
        if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
            canvas->nodeMoved (*this);
    }
}

void NodeComponent::mouseUp (const juce::MouseEvent&)
{
    if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
        canvas->nodeDragFinished (*this);
}

void NodeComponent::showContextMenu()
{
    // IO nodes are fixed plumbing; everything else can be removed.
    if (kindOf (type) == NodeKind::io)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Delete node");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                selection.clearIfSelected (nodeId);
                                model.removeNode (nodeId);
                            }
                        });
}

juce::Point<float> NodeComponent::socketCentreInParent (SocketKind k, const juce::String& param) const
{
    const Socket* socket = nullptr;
    switch (k)
    {
        case SocketKind::audioIn:  socket = audioInSocket.get(); break;
        case SocketKind::audioOut: socket = audioOutSocket.get(); break;
        case SocketKind::ctrlOut:  socket = ctrlOutSocket.get(); break;
        case SocketKind::paramIn:
            for (auto& row : rows)
                if (row->socket->paramId == param)
                    socket = row->socket.get();
            break;
    }

    if (socket == nullptr)
        return getBounds().getCentre().toFloat();

    return getPosition().toFloat() + socket->getBounds().getCentre().toFloat();
}

Socket* NodeComponent::socketAt (juce::Point<int> localPos)
{
    for (auto* candidate : { audioInSocket.get(), audioOutSocket.get(), ctrlOutSocket.get() })
        if (candidate != nullptr && candidate->getBounds().expanded (4).contains (localPos))
            return candidate;

    for (auto& row : rows)
        if (row->socket->getBounds().expanded (4).contains (localPos))
            return row->socket.get();

    return nullptr;
}

int NodeComponent::paramRowAt (int y) const
{
    auto row = (y - headerHeight) / rowHeight;
    return juce::isPositiveAndBelow (row, (int) rows.size()) ? row : -1;
}

void NodeComponent::valueTreePropertyChanged (juce::ValueTree& changed, const juce::Identifier& property)
{
    if (changed == tree && (property == ids::posX || property == ids::posY))
    {
        if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
            canvas->nodePositionChangedInModel (*this);
        return;
    }

    if (changed.hasType (ids::param) && property == ids::value)
    {
        auto paramId = changed.getProperty (ids::paramId).toString();
        for (auto& row : rows)
            if (paramId == row->spec.id)
            {
                juce::ScopedValueSetter<bool> guard (updatingFromTree, true);
                row->slider.setValue (changed.getProperty (ids::value), juce::dontSendNotification);
            }
    }
}

// --- macro drag-to-assign -------------------------------------------------

bool NodeComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("macro:") && ! rows.empty();
}

void NodeComponent::itemDragMove (const SourceDetails& details)
{
    auto row = paramRowAt (details.localPosition.y);
    if (row != highlightedRow)
    {
        highlightedRow = row;
        repaint();
    }
}

void NodeComponent::itemDragExit (const SourceDetails&)
{
    highlightedRow = -1;
    repaint();
}

void NodeComponent::itemDropped (const SourceDetails& details)
{
    auto row = paramRowAt (details.localPosition.y);
    highlightedRow = -1;
    repaint();

    if (row < 0)
        return;

    auto macroIndex = details.description.toString().fromFirstOccurrenceOf ("macro:", false, false).getIntValue();

    auto macroNode = model.findMacroNode (macroIndex);
    int macroNodeId;
    if (macroNode.isValid())
    {
        macroNodeId = macroNode.getProperty (ids::nodeId);
    }
    else
    {
        macroNodeId = model.addMacroNode (macroIndex,
                                          (float) getX() - width - 60.0f,
                                          (float) getY() + (float) row * rowHeight);
    }

    model.addModConnection (macroNodeId, nodeId, rows[(size_t) row]->spec.id);
}

} // namespace melo
