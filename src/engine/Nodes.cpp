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

double LFONode::effectiveRate (float rateHz, int sync, double bpm)
{
    if (sync <= 0)
        return rateHz;

    // sync index -> note length in beats (4/4): 1/1=4, 1/2=2, 1/4=1, 1/8=.5, 1/16=.25
    auto beatsPerCycle = 4.0 / std::pow (2.0, sync - 1);
    return (bpm / 60.0) / beatsPerCycle;
}

float LFONode::evaluate (const ProcessContext& ctx)
{
    auto rate   = effectiveRate (params[0].current(),
                                 (int) std::lround (params[1].current()), ctx.bpm);
    auto shape  = (int) std::lround (params[2].current());
    auto depth  = params[3].current();
    auto smooth = params[4].current();

    // Phase derived from transport time, so LFOs stay deterministic against
    // the playhead (and the timeline pane can render the identical curve).
    auto phase = (float) std::fmod (ctx.playheadSeconds * rate, 1.0);
    if (phase < 0.0f)
        phase += 1.0f;

    auto target = 0.5f + (shapeValue (shape, phase) - 0.5f) * depth;

    // Output slew: smooth in (0,1] maps to a time constant up to ~300ms,
    // rounding off steps in square/saw shapes.
    if (smooth <= 0.001f)
        return slewed = target;

    auto dt = ctx.numSamples > 0 ? ctx.numSamples / ctx.sampleRate
                                 : ctx.maxBlockSize / ctx.sampleRate;
    auto alpha = (float) (1.0 - std::exp (-dt / (0.3 * (double) smooth)));
    slewed += (target - slewed) * alpha;
    return slewed;
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
