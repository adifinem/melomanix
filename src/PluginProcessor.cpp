#include "PluginProcessor.h"
#include "PluginEditor.h"

MelomanixProcessor::MelomanixProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < numMacros; ++i)
        macroValues.push_back (apvts.getRawParameterValue ("macro" + juce::String (i + 1)));

    attachToModel();
    rebuildGraph();
}

MelomanixProcessor::~MelomanixProcessor()
{
    graphModel.state().removeListener (this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout MelomanixProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 1; i <= numMacros; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "macro" + juce::String (i), 1 },
            "Macro " + juce::String (i),
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.0f));

    return layout;
}

void MelomanixProcessor::attachToModel()
{
    graphModel.state().addListener (this);
}

void MelomanixProcessor::rebuildGraph()
{
    auto errors = hostedPlugins.syncWithModel (graphModel.state());
    for (auto& error : errors)
        juce::Logger::writeToLog ("Melomanix hosted plugin: " + error);

    engine.setGraph (melo::compileGraph (graphModel.state(), macroValues,
        [this] (int nodeId) { return hostedPlugins.instanceFor (nodeId); }));
}

void MelomanixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    hostedPlugins.prepare (sampleRate, samplesPerBlock);
    engine.prepare (sampleRate, samplesPerBlock);
}

bool MelomanixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainOut == layouts.getMainInputChannelSet();
}

void MelomanixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Resolve the transport clock. The host's musical position is canonical
    // when present, so LFO phase (seconds-based) resets with the transport
    // just like curve phase (beats-based) — see resolveTransportTime. Only a
    // host with no position at all falls back to the free-running clock.
    bool haveHostSeconds = false, haveHostBeats = false;
    double hostSeconds = 0.0, hostBeats = 0.0;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                lastBpm.store (*bpm);
            if (auto time = position->getTimeInSeconds())
            {
                hostSeconds = *time;
                haveHostSeconds = true;
            }
            if (auto ppq = position->getPpqPosition())
            {
                hostBeats = *ppq;
                haveHostBeats = true;
            }
        }

    if (! haveHostBeats && ! haveHostSeconds)
    {
        internalClockSeconds += buffer.getNumSamples() / currentSampleRate;
        hostSeconds = internalClockSeconds;
    }

    auto transport = melo::resolveTransportTime (haveHostBeats, hostBeats,
                                                 haveHostSeconds, hostSeconds,
                                                 lastBpm.load());
    auto seconds = transport.seconds;
    auto beats = transport.beats;
    lastPlayheadSeconds.store (seconds);
    lastPlayheadBeats.store (beats);

    melo::ProcessContext ctx;
    ctx.sampleRate = currentSampleRate;
    ctx.maxBlockSize = buffer.getNumSamples();
    ctx.numSamples = buffer.getNumSamples();
    ctx.playheadSeconds = seconds;
    ctx.playheadBeats = beats;
    ctx.bpm = lastBpm.load();

    engine.process (buffer, ctx);
}

void MelomanixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    hostedPlugins.captureStates (graphModel.state());

    juce::ValueTree root ("MELOMANIX");
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (graphModel.state().createCopy(), -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void MelomanixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (juce::Identifier ("MELOMANIX")))
        return;

    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    graphModel.state().removeListener (this);
    graphModel.replaceState (root.getChildWithName (melo::ids::graph).createCopy());
    attachToModel();

    // Instantiating hosted plugins must happen on the message thread; some
    // hosts call setStateInformation from a background thread.
    triggerAsyncUpdate();
}

juce::AudioProcessorEditor* MelomanixProcessor::createEditor()
{
    return new MelomanixEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelomanixProcessor();
}
