#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "GraphTypes.h"

namespace melo
{

struct ProcessContext
{
    double sampleRate = 44100.0;
    int    maxBlockSize = 512;
    double playheadSeconds = 0.0;   // for time-synced controllers (LFO phase, timeline display)
    double playheadBeats = 0.0;     // musical position (host ppq, or derived from seconds*bpm)
    double bpm = 120.0;             // host tempo, for tempo-synced LFO rates
    int    numSamples = 0;
};

// A parameter slot on a compiled node. `baseNorm` is the knob position;
// a mod connection offsets it bipolarly in the normalised domain:
//     effective = clamp01(base + offset + depth * (control*2 - 1))
// then denormalised and smoothed before use. Controllers emit in [0,1].
// Negative depth inverts the modulation; offset shifts its centre.
struct ParamState
{
    ParamSpec spec { "", "", 0.0f, 1.0f, 0.0f };
    float baseNorm = 0.0f;
    int   modSourceIndex = -1;   // index into CompiledGraph::nodes, -1 = unmodulated
    float modDepth = 0.0f;
    float modOffset = 0.0f;
    juce::SmoothedValue<float> smoothed;

    float current() const { return smoothed.getCurrentValue(); }
};

class EngineNode
{
public:
    explicit EngineNode (NodeType t) : type (t)
    {
        for (auto& spec : paramSpecsFor (t))
        {
            ParamState p;
            p.spec = spec;
            p.baseNorm = spec.normalise (spec.def);
            params.push_back (p);
        }
    }

    virtual ~EngineNode() = default;

    virtual void prepare (const ProcessContext& ctx)
    {
        for (auto& p : params)
        {
            p.smoothed.reset (ctx.sampleRate / juce::jmax (1, ctx.maxBlockSize), 0.02);
            p.smoothed.setCurrentAndTargetValue (p.spec.denormalise (p.baseNorm));
        }
    }

    // Controllers: produce this block's control value in [0,1].
    virtual float evaluate (const ProcessContext&) { return 0.0f; }

    // DSP nodes: process the buffer in place.
    virtual void process (juce::AudioBuffer<float>&, const ProcessContext&) {}

    // Called by the engine each block, in topological order, before
    // evaluate()/process(). Advances smoothing one step per block.
    void updateParams (const std::vector<std::unique_ptr<EngineNode>>& nodes)
    {
        for (auto& p : params)
        {
            auto norm = p.baseNorm;
            if (p.modSourceIndex >= 0)
                norm = juce::jlimit (0.0f, 1.0f,
                                     norm + p.modOffset
                                         + p.modDepth * (nodes[(size_t) p.modSourceIndex]->lastOutput * 2.0f - 1.0f));
            p.smoothed.setTargetValue (p.spec.denormalise (norm));
            p.smoothed.getNextValue();
        }
    }

    int indexOfParam (const juce::String& paramId) const
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (paramId == params[i].spec.id)
                return (int) i;
        return -1;
    }

    const NodeType type;
    int modelNodeId = 0;
    std::vector<ParamState> params;
    float lastOutput = 0.0f;   // controllers only; read by downstream updateParams
};

class AudioIONode : public EngineNode
{
public:
    explicit AudioIONode (NodeType t) : EngineNode (t) {}
};

class LFONode : public EngineNode
{
public:
    LFONode() : EngineNode (NodeType::lfo) {}

    float evaluate (const ProcessContext& ctx) override;

    // Shape at a given phase [0,1), output [0,1] before depth — shared with
    // the timeline pane so display and audio always match.
    static float shapeValue (int shape, float phase);

    // Effective cycles-per-second: the Hz knob when sync is off, otherwise
    // derived from host tempo (sync 1..5 = 1/1, 1/2, 1/4, 1/8, 1/16 notes).
    static double effectiveRate (float rateHz, int sync, double bpm);

private:
    float slewed = 0.5f;   // one-pole smoothed output state for the smooth param
};

class EQNode : public EngineNode
{
public:
    EQNode() : EngineNode (NodeType::eq) {}

    void prepare (const ProcessContext& ctx) override;
    void process (juce::AudioBuffer<float>&, const ProcessContext& ctx) override;

private:
    using Band = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                juce::dsp::IIR::Coefficients<float>>;
    Band low, mid, high;
    double sampleRate = 44100.0;
};

class DelayNode : public EngineNode
{
public:
    DelayNode() : EngineNode (NodeType::delay) {}

    void prepare (const ProcessContext& ctx) override;
    void process (juce::AudioBuffer<float>&, const ProcessContext& ctx) override;

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 1 };
    double sampleRate = 44100.0;
};

struct CurvePoint
{
    float t = 0.0f;         // position within the loop, [0,1]
    float v = 0.5f;         // output value, [0,1]
    float tension = 0.0f;   // shapes the segment to the next point, [-1,1]
};

// A drawn breakpoint controller: loops over `length` beats of the transport,
// interpolating between user-placed points with per-segment tension.
class CurveNode : public EngineNode
{
public:
    CurveNode() : EngineNode (NodeType::curve) {}

    float evaluate (const ProcessContext&) override;

    // Static so the timeline editor renders the exact same curve it edits.
    // Points must be sorted by t. Tension >0 holds toward the start value
    // longer (ease-in), <0 rushes toward the end value (ease-out).
    static float valueAt (const std::vector<CurvePoint>& sortedPoints, float phase);
    static float shapeSegment (float x, float tension);

    std::vector<CurvePoint> points;   // set by the compiler, sorted by t
};

// A macro is a controller whose value is a host-automatable parameter,
// owned by the processor's APVTS and read here lock-free.
class MacroNode : public EngineNode
{
public:
    MacroNode (int index, std::atomic<float>* hostValue)
        : EngineNode (NodeType::macro), macroIndex (index), value (hostValue) {}

    float evaluate (const ProcessContext&) override
    {
        return value != nullptr ? value->load() : 0.0f;
    }

    const int macroIndex;

private:
    std::atomic<float>* value;
};

} // namespace melo
