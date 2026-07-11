#include "HostedPlugin.h"

namespace melo
{

HostedPluginRegistry::HostedPluginRegistry()
{
    juce::addDefaultFormatsToManager (formatManager);   // VST3 (and AU on mac) hosting
}

HostedPluginRegistry::~HostedPluginRegistry() = default;

juce::StringArray HostedPluginRegistry::syncWithModel (juce::ValueTree& graphTree)
{
    juce::StringArray errors;
    std::set<int> liveNodeIds;

    auto fail = [&errors] (juce::ValueTree node, const juce::String& message)
    {
        node.setProperty (ids::pluginError, message, nullptr);
        errors.add (message);
    };

    for (auto child : graphTree)
    {
        if (! child.hasType (ids::node)
            || nodeTypeFromString (child.getProperty (ids::type).toString()) != NodeType::hosted)
            continue;

        int nodeId = child.getProperty (ids::nodeId);
        liveNodeIds.insert (nodeId);

        if (instances.count (nodeId) != 0)
            continue;
        if (child.hasProperty (ids::pluginError))
            continue;   // already failed; don't retry every recompile

        auto path = child.getProperty (ids::pluginPath).toString();
        juce::OwnedArray<juce::PluginDescription> found;

        for (auto* format : formatManager.getFormats())
            if (format->fileMightContainThisPluginType (path))
                format->findAllTypesForFile (found, path);

        if (found.isEmpty())
        {
            // The commonest cause is a plugin built for another OS (e.g. a
            // Windows-only .vst3 on Linux) — say so instead of a bare "no".
            fail (child, "Couldn't load — not a " + juce::String (JUCE_LINUX ? "Linux" : "this-platform")
                             + " VST3? (" + path + ")");
            continue;
        }

        juce::String error;
        auto instance = formatManager.createPluginInstance (*found[0], sampleRate, maxBlockSize, error);
        if (instance == nullptr)
        {
            fail (child, found[0]->name + ": " + error);
            continue;
        }

        child.removeProperty (ids::pluginError, nullptr);

        instance->enableAllBuses();
        instance->prepareToPlay (sampleRate, maxBlockSize);

        // Restore saved state if this node was loaded from a session.
        if (auto saved = child.getProperty (ids::pluginState).toString(); saved.isNotEmpty())
        {
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (saved) && block.getSize() > 0)
                instance->setStateInformation (block.getData(), (int) block.getSize());
        }

        instances[nodeId].instance = std::move (instance);
    }

    // Drop instances whose nodes were deleted (closes their windows too).
    for (auto it = instances.begin(); it != instances.end();)
        it = liveNodeIds.count (it->first) == 0 ? instances.erase (it) : std::next (it);

    return errors;
}

void HostedPluginRegistry::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlockSize = newMaxBlockSize;

    for (auto& [nodeId, hosted] : instances)
    {
        juce::ignoreUnused (nodeId);
        if (hosted.instance != nullptr)
        {
            hosted.instance->releaseResources();
            hosted.instance->prepareToPlay (sampleRate, maxBlockSize);
        }
    }
}

void HostedPluginRegistry::captureStates (juce::ValueTree& graphTree)
{
    for (auto child : graphTree)
    {
        if (! child.hasType (ids::node))
            continue;

        int nodeId = child.getProperty (ids::nodeId);
        auto it = instances.find (nodeId);
        if (it == instances.end() || it->second.instance == nullptr)
            continue;

        juce::MemoryBlock block;
        it->second.instance->getStateInformation (block);
        child.setProperty (ids::pluginState, block.toBase64Encoding(), nullptr);
    }
}

void scanInstalledVST3s (juce::KnownPluginList& list)
{
    juce::VST3PluginFormat format;
    auto searchPaths = format.getDefaultLocationsToSearch();

    for (auto& fileOrId : format.searchPathsForPlugins (searchPaths, true, true))
    {
        // Skip anything already known so rescans are cheap.
        if (list.getTypeForFile (fileOrId) != nullptr)
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, fileOrId);
        for (auto* description : found)
            list.addType (*description);
    }
}

juce::AudioPluginInstance* HostedPluginRegistry::instanceFor (int nodeId) const
{
    auto it = instances.find (nodeId);
    return it != instances.end() ? it->second.instance.get() : nullptr;
}

void HostedPluginRegistry::showEditorWindow (int nodeId)
{
    auto it = instances.find (nodeId);
    if (it == instances.end() || it->second.instance == nullptr)
        return;

    auto& hosted = it->second;
    if (hosted.window != nullptr)
    {
        hosted.window->toFront (true);
        return;
    }

    auto* editor = hosted.instance->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    struct PluginWindow : juce::DocumentWindow
    {
        PluginWindow (const juce::String& title, std::unique_ptr<juce::DocumentWindow>& slot)
            : DocumentWindow (title, juce::Colours::darkgrey, DocumentWindow::closeButton),
              windowSlot (slot) {}

        void closeButtonPressed() override { windowSlot.reset(); }   // deletes this
        std::unique_ptr<juce::DocumentWindow>& windowSlot;
    };

    auto window = std::make_unique<PluginWindow> (hosted.instance->getName(), hosted.window);
    window->setContentOwned (editor, true);
    window->setResizable (editor->isResizable(), false);
    window->centreWithSize (juce::jmax (200, editor->getWidth()),
                            juce::jmax (100, editor->getHeight()));
    window->setVisible (true);
    hosted.window = std::move (window);
}

} // namespace melo
