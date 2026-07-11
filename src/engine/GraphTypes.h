#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace melo
{

enum class NodeType
{
    audioIn,
    audioOut,
    eq,
    delay,
    lfo,
    macro,
    curve,
    hosted   // a third-party plugin loaded as a DSP node
};

// Controllers emit a block-rate control value in [0, 1]; DSP nodes process audio.
// IO nodes bridge the host's buffers into the graph.
enum class NodeKind { io, dsp, controller };

struct ParamSpec
{
    const char* id;
    const char* name;
    float min, max, def;
    float skew = 1.0f;   // <1 gives finer resolution at the low end (freq/time params)
    const char* unit = "";   // "Hz", "dB", "ms", "%", "shape", "sync" — drives value display

    float normalise (float value) const
    {
        auto proportion = juce::jlimit (0.0f, 1.0f, (value - min) / (max - min));
        return juce::exactlyEqual (skew, 1.0f) ? proportion : std::pow (proportion, skew);
    }

    float denormalise (float norm) const
    {
        norm = juce::jlimit (0.0f, 1.0f, norm);
        if (! juce::exactlyEqual (skew, 1.0f))
            norm = std::pow (norm, 1.0f / skew);
        return min + norm * (max - min);
    }
};

juce::String nodeTypeToString (NodeType);
NodeType nodeTypeFromString (const juce::String&);
NodeKind kindOf (NodeType);
const std::vector<ParamSpec>& paramSpecsFor (NodeType);

// Human-readable value with units: "804 Hz", "6.2 kHz", "-3.5 dB",
// "250 ms (1/8)" (note match at the given BPM), "35%", "Saw", "1/4".
juce::String formatParamValue (const ParamSpec&, float value, double bpm);

// ValueTree schema identifiers. The graph is stored as flat nodes + edge list,
// nodes referenced by integer id only — a constraint from the spec so that
// subgraph collapsing can be retrofitted without a data-model rewrite.
namespace ids
{
    inline const juce::Identifier graph      { "GRAPH" };
    inline const juce::Identifier node       { "NODE" };
    inline const juce::Identifier conn       { "CONN" };
    inline const juce::Identifier nodeId     { "id" };
    inline const juce::Identifier type       { "type" };
    inline const juce::Identifier posX       { "x" };
    inline const juce::Identifier posY       { "y" };
    inline const juce::Identifier param      { "PARAM" };
    inline const juce::Identifier paramId    { "pid" };
    inline const juce::Identifier value      { "value" };
    inline const juce::Identifier srcNode    { "src" };
    inline const juce::Identifier dstNode    { "dst" };
    inline const juce::Identifier dstParam   { "dstParam" };  // set => mod connection, absent => audio
    inline const juce::Identifier depth      { "depth" };     // -1..1, negative inverts
    inline const juce::Identifier offset     { "offset" };    // -1..1 shift of the mod centre
    inline const juce::Identifier nextNodeId { "nextNodeId" };
    inline const juce::Identifier macroIndex { "macroIndex" };

    // Curve node breakpoints: children of the NODE, position/value in [0,1],
    // tension in [-1,1] shaping the segment to the NEXT point.
    inline const juce::Identifier point     { "POINT" };
    inline const juce::Identifier pointT    { "t" };
    inline const juce::Identifier pointV    { "v" };
    inline const juce::Identifier tension   { "tension" };

    // Editing grid for curve points (per curve node); 0 = no snap on that axis.
    inline const juce::Identifier gridX     { "gridX" };
    inline const juce::Identifier gridY     { "gridY" };

    // Hosted third-party plugin nodes.
    inline const juce::Identifier pluginPath  { "pluginPath" };
    inline const juce::Identifier pluginName  { "pluginName" };
    inline const juce::Identifier pluginState { "pluginState" };   // base64
    inline const juce::Identifier pluginError { "pluginError" };   // set when load failed

    // A hosted parameter exposed as a modulation socket. Children of the
    // hosted NODE; the socket's paramId is "p<index>".
    inline const juce::Identifier exposed   { "EXPOSED" };
    inline const juce::Identifier hostParam { "hostParam" };   // parameter index in the plugin
    inline const juce::Identifier paramName { "paramName" };
}

} // namespace melo
