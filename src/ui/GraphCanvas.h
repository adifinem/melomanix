#pragma once

#include "NodeComponent.h"

namespace melo
{

// The centre pane: a pannable/zoomable surface holding node components,
// with cables painted between socket centres. All structural edits go
// through the GraphModel; the canvas rebuilds from the tree.
class GraphCanvas : public juce::Component,
                    private juce::ValueTree::Listener,
                    private juce::Timer
{
public:
    GraphCanvas (GraphModel&, SelectionModel&, std::function<double()> bpmProvider,
                 std::function<void (int)> openHostedEditorFn);

    // Double-clicking a hosted node's header opens its plugin GUI.
    void openHostedEditor (int nodeId) { if (openHostedEditorFn != nullptr) openHostedEditorFn (nodeId); }
    ~GraphCanvas() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Cable gesture, driven by Socket mouse events.
    void beginCableDrag (Socket&);
    void updateCableDrag (const juce::MouseEvent&);
    void endCableDrag (const juce::MouseEvent&);
    void showDisconnectMenu (Socket&);

    // Node drag bookkeeping.
    void nodeMoved (NodeComponent&);
    void nodeDragFinished (NodeComponent&);
    void nodePositionChangedInModel (NodeComponent&);

private:
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void timerCallback() override;

    void rebuild();
    void showAddNodeMenu (juce::Point<int> canvasPos);
    void chooseAndLoadPlugin (juce::Point<float> contentPos);
    NodeComponent* findNodeComponent (int nodeId) const;
    juce::Point<float> contentPosFor (const juce::ValueTree& node) const;
    void applyViewTransform();

    // Painting helpers — canvas coords.
    void drawCables (juce::Graphics&);
    juce::Point<float> toCanvas (juce::Point<float> contentPos) const;

    GraphModel& model;
    SelectionModel& selection;
    std::function<double()> getBpm;
    std::function<void (int)> openHostedEditorFn;
    std::unique_ptr<juce::FileChooser> pluginChooser;
    juce::ValueTree observedTree;

    std::vector<std::unique_ptr<NodeComponent>> nodeComps;

    // View state: content coords -> canvas coords is scale then translate.
    float zoom = 1.0f;
    juce::Point<float> panOffset { 0.0f, 0.0f };
    juce::Point<float> panDragStart;
    bool centredOnce = false;

    // In-progress cable.
    Socket* dragSourceSocket = nullptr;
    juce::Point<float> dragCableEnd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphCanvas)
};

} // namespace melo
