#pragma once

#include "../model/GraphModel.h"
#include "Selection.h"
#include "Theme.h"

namespace melo
{

class GraphCanvas;

enum class SocketKind { audioIn, audioOut, ctrlOut, paramIn };

// A connection endpoint drawn on a node. Mouse gestures are forwarded to
// the canvas, which owns the in-progress cable state.
class Socket : public juce::Component
{
public:
    Socket (SocketKind k, int nodeId, juce::String paramId = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInput() const  { return kind == SocketKind::audioIn || kind == SocketKind::paramIn; }
    bool isAudio() const  { return kind == SocketKind::audioIn || kind == SocketKind::audioOut; }

    const SocketKind kind;
    const int nodeId;
    const juce::String paramId;   // set for paramIn sockets
};

class NodeComponent : public juce::Component,
                      public juce::DragAndDropTarget,
                      private juce::ValueTree::Listener
{
public:
    NodeComponent (GraphModel&, juce::ValueTree nodeTree, SelectionModel&);
    ~NodeComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Drop target for macro-strip drag-to-assign.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    int getNodeId() const { return nodeId; }
    NodeType getType() const { return type; }

    // Centre of a socket in the parent (content) coordinate space, for cable drawing.
    juce::Point<float> socketCentreInParent (SocketKind, const juce::String& paramId = {}) const;
    Socket* socketAt (juce::Point<int> localPos);

    static constexpr int width = 172, headerHeight = 24, rowHeight = 22;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void showContextMenu();
    int paramRowAt (int y) const;

    GraphModel& model;
    juce::ValueTree tree;
    SelectionModel& selection;

    const int nodeId;
    const NodeType type;

    struct ParamRow
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<Socket> socket;
        ParamSpec spec { "", "", 0.0f, 1.0f, 0.0f };
    };

    std::vector<std::unique_ptr<ParamRow>> rows;
    std::unique_ptr<Socket> audioInSocket, audioOutSocket, ctrlOutSocket;

    juce::ComponentDragger dragger;
    int highlightedRow = -1;
    bool updatingFromTree = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
};

} // namespace melo
