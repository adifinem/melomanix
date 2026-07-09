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
    engine.setGraph (melo::compileGraph (graphModel.state(), macroValues));
}

void MelomanixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
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

    // Prefer the host's transport time so LFO phase follows the project
    // playhead; fall back to a free-running clock (standalone, or hosts
    // that don't report position).
    double seconds = internalClockSeconds;
    bool haveHostTime = false;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto time = position->getTimeInSeconds())
            {
                seconds = *time;
                haveHostTime = true;
            }
            if (auto bpm = position->getBpm())
                lastBpm.store (*bpm);
        }

    if (! haveHostTime)
        internalClockSeconds += buffer.getNumSamples() / currentSampleRate;

    lastPlayheadSeconds.store (seconds);

    melo::ProcessContext ctx;
    ctx.sampleRate = currentSampleRate;
    ctx.maxBlockSize = buffer.getNumSamples();
    ctx.numSamples = buffer.getNumSamples();
    ctx.playheadSeconds = seconds;
    ctx.bpm = lastBpm.load();

    engine.process (buffer, ctx);
}

void MelomanixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
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
    rebuildGraph();
}

juce::AudioProcessorEditor* MelomanixProcessor::createEditor()
{
    return new MelomanixEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelomanixProcessor();
}
