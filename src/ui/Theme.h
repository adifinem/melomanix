#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/GraphTypes.h"

namespace melo::theme
{
    inline const juce::Colour background     { 0xff1e2126 };
    inline const juce::Colour panel          { 0xff262a31 };
    inline const juce::Colour nodeBody       { 0xff2e333c };
    inline const juce::Colour nodeSelected   { 0xff4a90d9 };
    inline const juce::Colour headerDsp      { 0xff3d5a80 };
    inline const juce::Colour headerCtrl     { 0xff5c8a4a };
    inline const juce::Colour headerIO       { 0xff55585f };
    inline const juce::Colour audioSignal    { 0xffe8a04c };   // audio cables/sockets: warm
    inline const juce::Colour controlSignal  { 0xff6ecfcf };   // control cables/sockets: cool
    inline const juce::Colour text           { 0xffd8dade };
    inline const juce::Colour textDim        { 0xff8a8f98 };
    inline const juce::Colour playhead       { 0xffe86060 };

    inline juce::Colour headerFor (NodeKind kind)
    {
        switch (kind)
        {
            case NodeKind::dsp:        return headerDsp;
            case NodeKind::controller: return headerCtrl;
            case NodeKind::io:         return headerIO;
        }
        return headerIO;
    }

    inline juce::String titleFor (NodeType type, int macroIndex = -1)
    {
        switch (type)
        {
            case NodeType::audioIn:  return "Audio In";
            case NodeType::audioOut: return "Audio Out";
            case NodeType::eq:       return "EQ";
            case NodeType::delay:    return "Delay";
            case NodeType::lfo:      return "LFO";
            case NodeType::macro:    return "Macro " + juce::String (macroIndex + 1);
        }
        return {};
    }
}
