#include "NodeComponent.h"
#include "GraphCanvas.h"
#include <juce_audio_processors/juce_audio_processors.h>

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

NodeComponent::NodeComponent (GraphModel& m, juce::ValueTree nodeTree, SelectionModel& sel,
                              std::function<double()> bpmProvider, HostedInstanceLookup hostedLookup)
    : model (m),
      tree (nodeTree),
      selection (sel),
      getBpm (std::move (bpmProvider)),
      hostedInstance (std::move (hostedLookup)),
      nodeId (nodeTree.getProperty (ids::nodeId)),
      type (nodeTypeFromString (nodeTree.getProperty (ids::type).toString()))
{
    tree.addListener (this);

    if (type == NodeType::hosted)
        buildHostedRows();
    else
        buildStaticRows();

    if (type != NodeType::audioIn && kindOf (type) != NodeKind::controller)
        addAndMakeVisible (*(audioInSocket = std::make_unique<Socket> (SocketKind::audioIn, nodeId)));
    if (type != NodeType::audioOut && kindOf (type) != NodeKind::controller)
        addAndMakeVisible (*(audioOutSocket = std::make_unique<Socket> (SocketKind::audioOut, nodeId)));
    if (kindOf (type) == NodeKind::controller)
        addAndMakeVisible (*(ctrlOutSocket = std::make_unique<Socket> (SocketKind::ctrlOut, nodeId)));

    auto bodyHeight = (int) rows.size() * rowHeight + 8;
    if (type == NodeType::hosted && rows.empty())
        bodyHeight = 44;   // room for the hint or a load-error message
    setSize (width, headerHeight + bodyHeight);
}

NodeComponent::~NodeComponent()
{
    tree.removeListener (this);
}

void NodeComponent::addRow (std::unique_ptr<ParamRow> row)
{
    row->label.setFont (juce::FontOptions (11.0f));
    row->label.setColour (juce::Label::textColourId, theme::textDim);
    row->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (row->label);

    row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    row->slider.setColour (juce::Slider::trackColourId, theme::controlSignal.withAlpha (0.4f));
    row->slider.setPopupDisplayEnabled (true, true, nullptr);
    addAndMakeVisible (row->slider);

    row->socket = std::make_unique<Socket> (SocketKind::paramIn, nodeId, juce::String (row->spec.id));
    addAndMakeVisible (*row->socket);

    rows.push_back (std::move (row));
}

void NodeComponent::buildStaticRows()
{
    for (auto& spec : paramSpecsFor (type))
    {
        auto row = std::make_unique<ParamRow>();
        row->spec = spec;
        row->label.setText (spec.name, juce::dontSendNotification);

        // Stepped params (LFO shape/sync) snap to integers.
        auto stepped = juce::String (spec.id) == "shape" || juce::String (spec.id) == "sync";
        row->slider.setNormalisableRange ({ (double) spec.min, (double) spec.max,
                                            stepped ? 1.0 : 0.0, (double) spec.skew });
        row->slider.setValue (model.getParamValue (nodeId, spec.id), juce::dontSendNotification);

        // Value bubble with real units (and note values at the host tempo).
        row->slider.textFromValueFunction = [spec, this] (double v)
        {
            return formatParamValue (spec, (float) v, getBpm != nullptr ? getBpm() : 0.0);
        };
        // Curve length is sticky on musical divisions (sixteenths up to 8
        // bars, including triple/dotted lengths); hold CTRL for free values.
        auto snapMusical = type == NodeType::curve && juce::String (spec.id) == "length";
        row->slider.onValueChange = [this, id = juce::String (spec.id), s = &row->slider, snapMusical]
        {
            if (updatingFromTree)
                return;

            auto value = s->getValue();
            if (snapMusical && ! juce::ModifierKeys::getCurrentModifiers().isCtrlDown())
            {
                static constexpr double divisions[] = { 0.25, 0.5, 1.0, 2.0, 3.0, 4.0,
                                                        6.0, 8.0, 12.0, 16.0, 24.0, 32.0 };
                auto snapped = divisions[0];
                for (auto d : divisions)
                    if (std::abs (d - value) < std::abs (snapped - value))
                        snapped = d;
                if (snapped != value)
                    s->setValue (value = snapped, juce::dontSendNotification);
            }
            model.setParamValue (nodeId, id, (float) value);
        };

        addRow (std::move (row));
    }
}

void NodeComponent::buildHostedRows()
{
    auto* instance = hostedInstance != nullptr ? hostedInstance (nodeId) : nullptr;

    for (auto child : tree)
    {
        if (! child.hasType (ids::exposed))
            continue;

        auto index = (int) child.getProperty (ids::hostParam);
        auto row = std::make_unique<ParamRow>();
        row->hostParamIndex = index;
        row->idStorage = ("p" + juce::String (index)).toStdString();
        row->spec = ParamSpec { row->idStorage.c_str(), "", 0.0f, 1.0f, 0.5f };
        row->label.setText (child.getProperty (ids::paramName).toString(), juce::dontSendNotification);

        juce::AudioProcessorParameter* hostedParam =
            instance != nullptr && juce::isPositiveAndBelow (index, instance->getParameters().size())
                ? instance->getParameters()[index] : nullptr;

        row->slider.setRange (0.0, 1.0);
        if (hostedParam != nullptr)
        {
            row->slider.setValue (hostedParam->getValue(), juce::dontSendNotification);
            row->slider.textFromValueFunction = [hostedParam] (double v)
            {
                return hostedParam->getText ((float) v, 24);
            };
            row->slider.onValueChange = [hostedParam, s = &row->slider]
            {
                hostedParam->setValue ((float) s->getValue());
            };
        }

        addRow (std::move (row));
    }
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
    auto title = type == NodeType::hosted
                     ? tree.getProperty (ids::pluginName, "Plugin").toString()
                     : theme::titleFor (type, (int) tree.getProperty (ids::macroIndex, -1));
    g.drawText (title, header.reduced (18.0f, 0.0f), juce::Justification::centred);

    if (type == NodeType::hosted && rows.empty())
    {
        auto loadError = tree.getProperty (ids::pluginError).toString();
        auto body = getLocalBounds().withTrimmedTop (headerHeight).reduced (6, 2);
        if (loadError.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffd06060));
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (loadError, body, juce::Justification::centred, 3);
        }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("double-click: GUI · right-click: params", body,
                        juce::Justification::centred);
        }
    }

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

void NodeComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (type == NodeType::hosted)
        if (auto* canvas = findParentComponentOfClass<GraphCanvas>())
            canvas->openHostedEditor (nodeId);
}

void NodeComponent::showContextMenu()
{
    // IO nodes are fixed plumbing; everything else can be removed.
    if (kindOf (type) == NodeKind::io)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Delete node");

    // Hosted nodes: expose plugin parameters as modulation sockets.
    juce::AudioPluginInstance* instance = nullptr;
    if (type == NodeType::hosted && hostedInstance != nullptr)
        instance = hostedInstance (nodeId);

    if (instance != nullptr)
    {
        juce::PopupMenu paramsMenu;
        auto& parameters = instance->getParameters();
        auto count = juce::jmin (parameters.size(), 200);
        for (int i = 0; i < count; ++i)
            paramsMenu.addItem (100 + i, parameters[i]->getName (40), true,
                                model.isHostedParamExposed (nodeId, i));
        menu.addSubMenu ("Expose parameter", paramsMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, instance] (int result)
                        {
                            if (result == 1)
                            {
                                selection.clearIfSelected (nodeId);
                                model.removeNode (nodeId);
                            }
                            else if (result >= 100 && instance != nullptr)
                            {
                                auto index = result - 100;
                                model.setHostedParamExposed (
                                    nodeId, index,
                                    instance->getParameters()[index]->getName (40),
                                    ! model.isHostedParamExposed (nodeId, index));
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
    if (changed == tree && property == ids::pluginError)
    {
        repaint();
        return;
    }

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
