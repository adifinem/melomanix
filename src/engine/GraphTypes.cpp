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
        case NodeType::curve:    return "curve";
        case NodeType::hosted:   return "hosted";
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
    if (s == "curve")    return NodeType::curve;
    if (s == "hosted")   return NodeType::hosted;
    return NodeType::audioIn;
}

NodeKind kindOf (NodeType t)
{
    switch (t)
    {
        case NodeType::audioIn:
        case NodeType::audioOut: return NodeKind::io;
        case NodeType::eq:
        case NodeType::delay:
        case NodeType::hosted:   return NodeKind::dsp;
        case NodeType::lfo:
        case NodeType::macro:
        case NodeType::curve:    return NodeKind::controller;
    }
    jassertfalse;
    return NodeKind::io;
}

const std::vector<ParamSpec>& paramSpecsFor (NodeType t)
{
    static const std::vector<ParamSpec> none;

    static const std::vector<ParamSpec> eqSpecs {
        { "lowFreq",  "Low Freq",   20.0f,   2000.0f,  200.0f, 0.4f, "Hz" },
        { "lowGain",  "Low Gain",  -24.0f,     24.0f,    0.0f, 1.0f, "dB" },
        { "midFreq",  "Mid Freq",  100.0f,   8000.0f, 1000.0f, 0.4f, "Hz" },
        { "midGain",  "Mid Gain",  -24.0f,     24.0f,    0.0f, 1.0f, "dB" },
        { "midQ",     "Mid Q",       0.1f,     10.0f,    0.7f, 0.5f       },
        { "highFreq", "High Freq", 1000.0f, 20000.0f, 6000.0f, 0.4f, "Hz" },
        { "highGain", "High Gain", -24.0f,     24.0f,    0.0f, 1.0f, "dB" },
    };

    static const std::vector<ParamSpec> delaySpecs {
        { "time",     "Time",     1.0f, 2000.0f, 350.0f, 0.4f, "ms" },
        { "feedback", "Feedback", 0.0f,    0.95f,  0.35f, 1.0f, "%" },
        { "mix",      "Mix",      0.0f,    1.0f,   0.35f, 1.0f, "%" },
    };

    static const std::vector<ParamSpec> lfoSpecs {
        { "rate",   "Rate",   0.02f, 20.0f, 1.0f, 0.4f, "Hz"    },
        { "sync",   "Sync",   0.0f,   5.0f, 0.0f, 1.0f, "sync"  },  // 0 free-Hz, then 1/1..1/16
        { "shape",  "Shape",  0.0f,   3.0f, 0.0f, 1.0f, "shape" },  // sine, tri, saw, square
        { "depth",  "Depth",  0.0f,   1.0f, 1.0f, 1.0f, "%"     },
        { "smooth", "Smooth", 0.0f,   1.0f, 0.0f, 1.0f, "%"     },  // output slew
    };

    static const std::vector<ParamSpec> curveSpecs {
        { "length", "Length", 0.25f, 32.0f, 4.0f, 0.5f, "beats" },
        { "depth",  "Depth",  0.0f,   1.0f, 1.0f, 1.0f, "%"     },
    };

    switch (t)
    {
        case NodeType::eq:       return eqSpecs;
        case NodeType::delay:    return delaySpecs;
        case NodeType::lfo:      return lfoSpecs;
        case NodeType::curve:    return curveSpecs;
        case NodeType::audioIn:
        case NodeType::audioOut:
        case NodeType::macro:
        case NodeType::hosted:   return none;   // hosted params exposed dynamically (v0.3b)
    }
    return none;
}

juce::String formatParamValue (const ParamSpec& spec, float value, double bpm)
{
    const juce::String unit (spec.unit);

    if (unit == "Hz")
        return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                : juce::String (value, value < 10.0f ? 2 : 1) + " Hz";

    if (unit == "dB")
        return juce::String (value, 1) + " dB";

    if (unit == "%")
        return juce::String ((int) std::lround (value * 100.0f)) + "%";

    if (unit == "shape")
    {
        static const char* names[] { "Sine", "Triangle", "Saw", "Square" };
        auto index = juce::jlimit (0, 3, (int) std::lround (value));
        return names[index];
    }

    if (unit == "sync")
    {
        static const char* names[] { "Free", "1/1", "1/2", "1/4", "1/8", "1/16" };
        auto index = juce::jlimit (0, 5, (int) std::lround (value));
        return names[index];
    }

    if (unit == "ms")
    {
        auto text = value >= 100.0f ? juce::String ((int) std::lround (value)) + " ms"
                                    : juce::String (value, 1) + " ms";

        // Annotate with the nearest note division when it's a close match.
        if (bpm > 0.0)
        {
            struct Division { double beats; const char* name; };
            static const Division divisions[] {
                { 4.0, "1/1" }, { 3.0, "1/2." }, { 2.0, "1/2" }, { 4.0 / 3.0, "1/2T" },
                { 1.5, "1/4." }, { 1.0, "1/4" }, { 2.0 / 3.0, "1/4T" },
                { 0.75, "1/8." }, { 0.5, "1/8" }, { 1.0 / 3.0, "1/8T" },
                { 0.375, "1/16." }, { 0.25, "1/16" }, { 1.0 / 6.0, "1/16T" },
            };

            auto beats = (double) value * 0.001 * bpm / 60.0;
            for (auto& division : divisions)
                if (std::abs (beats - division.beats) / division.beats < 0.03)
                    return text + " (" + division.name + ")";
        }
        return text;
    }

    if (unit == "beats")
        return juce::String (value, value < 4.0f ? 2 : 1) + " beats";

    return juce::String (value, 2);
}

} // namespace melo
