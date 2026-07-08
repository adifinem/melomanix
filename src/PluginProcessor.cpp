#include "PluginProcessor.h"
#include "PluginEditor.h"

MelomanixProcessor::MelomanixProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    gainParam = apvts.getRawParameterValue ("gain");
}

juce::AudioProcessorValueTreeState::ParameterLayout MelomanixProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 },
        "Gain",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

void MelomanixProcessor::prepareToPlay (double sampleRate, int)
{
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (gainParam->load()));
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

    smoothedGain.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    smoothedGain.applyGain (buffer, buffer.getNumSamples());
}

void MelomanixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MelomanixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MelomanixProcessor::createEditor()
{
    return new MelomanixEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelomanixProcessor();
}
