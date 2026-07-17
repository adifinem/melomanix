#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "model/GraphModel.h"
#include "engine/GraphEngine.h"
#include "engine/HostedPlugin.h"
#include "engine/Transport.h"

class MelomanixProcessor : public juce::AudioProcessor,
                           private juce::ValueTree::Listener,
                           private juce::AsyncUpdater
{
public:
    static constexpr int numMacros = 8;

    MelomanixProcessor();
    ~MelomanixProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    melo::GraphModel graphModel;
    melo::HostedPluginRegistry hostedPlugins;

    // Editor reads these to animate the timeline playhead and render
    // tempo-synced lanes identically to the engine.
    double getPlayheadSeconds() const { return lastPlayheadSeconds.load(); }
    double getPlayheadBeats() const { return lastPlayheadBeats.load(); }
    double getBpm() const { return lastBpm.load(); }

    // Display only: a controller's latest block output for cable glow.
    float getControllerValue (int nodeId) const { return engine.controllerValueFor (nodeId); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override
    {
        // Positions and appearance are cosmetic; don't recompile for them.
        if (property != melo::ids::posX && property != melo::ids::posY
            && property != melo::ids::palette && property != melo::ids::cableGlow
            && property != melo::ids::gridX && property != melo::ids::gridY)
            triggerAsyncUpdate();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildGraph(); }

    void rebuildGraph();
    void attachToModel();

    melo::GraphEngine engine;
    std::vector<std::atomic<float>*> macroValues;

    std::atomic<double> lastPlayheadSeconds { 0.0 };
    std::atomic<double> lastPlayheadBeats { 0.0 };
    std::atomic<double> lastBpm { 120.0 };
    double internalClockSeconds = 0.0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelomanixProcessor)
};
