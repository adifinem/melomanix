#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "model/GraphModel.h"
#include "engine/GraphEngine.h"

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

    // Editor reads this to animate the timeline playhead.
    double getPlayheadSeconds() const { return lastPlayheadSeconds.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuildGraph(); }

    void rebuildGraph();
    void attachToModel();

    melo::GraphEngine engine;
    std::vector<std::atomic<float>*> macroValues;

    std::atomic<double> lastPlayheadSeconds { 0.0 };
    double internalClockSeconds = 0.0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelomanixProcessor)
};
