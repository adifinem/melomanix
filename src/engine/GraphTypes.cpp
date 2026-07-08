#include "GraphTypes.h"

namespace melo
{

juce::String nodeTypeToString (NodeType t)
{
    switch (t)
    {
        case NodeType::audioIn:  return "audioIn";
        case NodeType::audioOut: return "audioOut";
        case NodeType::eq:       return "eq";
        case NodeType::delay:    return "delay";
        case NodeType::lfo:      return "lfo";
        case NodeType::macro:    return "macro";
    }
    jassertfalse;
    return "audioIn";
}

NodeType nodeTypeFromString (const juce::String& s)
{
    if (s == "audioOut") return NodeType::audioOut;
    if (s == "eq")       return NodeType::eq;
    if (s == "delay")    return NodeType::delay;
    if (s == "lfo")      return NodeType::lfo;
    if (s == "macro")    return NodeType::macro;
    return NodeType::audioIn;
}

NodeKind kindOf (NodeType t)
{
    switch (t)
    {
        case NodeType::audioIn:
        case NodeType::audioOut: return NodeKind::io;
        case NodeType::eq:
        case NodeType::delay:    return NodeKind::dsp;
        case NodeType::lfo:
        case NodeType::macro:    return NodeKind::controller;
    }
    jassertfalse;
    return NodeKind::io;
}

const std::vector<ParamSpec>& paramSpecsFor (NodeType t)
{
    static const std::vector<ParamSpec> none;

    static const std::vector<ParamSpec> eqSpecs {
        { "lowFreq",  "Low Freq",   20.0f,   2000.0f,  200.0f, 0.4f },
        { "lowGain",  "Low Gain",  -24.0f,     24.0f,    0.0f       },
        { "midFreq",  "Mid Freq",  100.0f,   8000.0f, 1000.0f, 0.4f },
        { "midGain",  "Mid Gain",  -24.0f,     24.0f,    0.0f       },
        { "midQ",     "Mid Q",       0.1f,     10.0f,    0.7f, 0.5f },
        { "highFreq", "High Freq", 1000.0f, 20000.0f, 6000.0f, 0.4f },
        { "highGain", "High Gain", -24.0f,     24.0f,    0.0f       },
    };

    static const std::vector<ParamSpec> delaySpecs {
        { "time",     "Time",     1.0f, 2000.0f, 350.0f, 0.4f },
        { "feedback", "Feedback", 0.0f,    0.95f,  0.35f      },
        { "mix",      "Mix",      0.0f,    1.0f,   0.35f      },
    };

    static const std::vector<ParamSpec> lfoSpecs {
        { "rate",  "Rate",  0.02f, 20.0f, 1.0f, 0.4f },
        { "shape", "Shape", 0.0f,   3.0f, 0.0f       },  // 0 sine, 1 tri, 2 saw, 3 square
        { "depth", "Depth", 0.0f,   1.0f, 1.0f       },
    };

    switch (t)
    {
        case NodeType::eq:       return eqSpecs;
        case NodeType::delay:    return delaySpecs;
        case NodeType::lfo:      return lfoSpecs;
        case NodeType::audioIn:
        case NodeType::audioOut:
        case NodeType::macro:    return none;
    }
    return none;
}

} // namespace melo
