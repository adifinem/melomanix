#include "Nodes.h"

namespace melo
{

float LFONode::shapeValue (int shape, float phase)
{
    switch (shape)
    {
        default:
        case 0: return 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
        case 1: return phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
        case 2: return phase;
        case 3: return phase < 0.5f ? 1.0f : 0.0f;
    }
}

float LFONode::evaluate (const ProcessContext& ctx)
{
    auto rate  = params[0].current();
    auto shape = (int) std::lround (params[1].current());
    auto depth = params[2].current();

    // Phase derived from transport time, so LFOs stay deterministic against
    // the playhead (and the timeline pane can render the identical curve).
    auto phase = (float) std::fmod (ctx.playheadSeconds * rate, 1.0);
    if (phase < 0.0f)
        phase += 1.0f;

    return 0.5f + (shapeValue (shape, phase) - 0.5f) * depth;
}

void EQNode::prepare (const ProcessContext& ctx)
{
    EngineNode::prepare (ctx);
    sampleRate = ctx.sampleRate;

    juce::dsp::ProcessSpec spec { ctx.sampleRate, (juce::uint32) ctx.maxBlockSize, 2 };
    for (auto* band : { &low, &mid, &high })
    {
        band->prepare (spec);
        band->reset();
    }
}

void EQNode::process (juce::AudioBuffer<float>& buffer, const ProcessContext&)
{
    auto sr = sampleRate;
    *low.state  = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, params[0].current(), 0.7f,
                      juce::Decibels::decibelsToGain (params[1].current()));
    *mid.state  = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, params[2].current(), params[4].current(),
                      juce::Decibels::decibelsToGain (params[3].current()));
    *high.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, params[5].current(), 0.7f,
                      juce::Decibels::decibelsToGain (params[6].current()));

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    low.process (context);
    mid.process (context);
    high.process (context);
}

void DelayNode::prepare (const ProcessContext& ctx)
{
    EngineNode::prepare (ctx);
    sampleRate = ctx.sampleRate;

    delayLine = decltype (delayLine) ((int) std::ceil (ctx.sampleRate * 2.1));
    juce::dsp::ProcessSpec spec { ctx.sampleRate, (juce::uint32) ctx.maxBlockSize, 2 };
    delayLine.prepare (spec);
}

void DelayNode::process (juce::AudioBuffer<float>& buffer, const ProcessContext& ctx)
{
    auto delaySamples = (float) (params[0].current() * 0.001 * sampleRate);
    auto feedback     = params[1].current();
    auto mix          = params[2].current();

    delayLine.setDelay (juce::jlimit (1.0f, (float) delayLine.getMaximumDelayInSamples() - 1.0f, delaySamples));

    auto numChannels = juce::jmin (buffer.getNumChannels(), 2);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < ctx.numSamples; ++i)
        {
            auto delayed = delayLine.popSample (ch);
            delayLine.pushSample (ch, data[i] + delayed * feedback);
            data[i] = data[i] * (1.0f - mix) + delayed * mix;
        }
    }
}

} // namespace melo
