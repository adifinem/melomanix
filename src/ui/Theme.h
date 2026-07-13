#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../engine/GraphTypes.h"

namespace melo::theme
{
    // The active palette. Mutable so applyPalette() can retarget every
    // theme::x call site at once; UI (message) thread only.
    inline juce::Colour background     { 0xff1e2126 };
    inline juce::Colour panel          { 0xff262a31 };
    inline juce::Colour nodeBody       { 0xff2e333c };
    inline juce::Colour nodeSelected   { 0xff4a90d9 };
    inline juce::Colour headerDsp      { 0xff3d5a80 };
    inline juce::Colour headerCtrl     { 0xff5c8a4a };
    inline juce::Colour headerIO       { 0xff55585f };
    inline juce::Colour audioSignal    { 0xffe8a04c };   // audio cables/sockets: warm
    inline juce::Colour controlSignal  { 0xff6ecfcf };   // control cables/sockets: cool
    inline juce::Colour text           { 0xffd8dade };
    inline juce::Colour textDim        { 0xff8a8f98 };
    inline juce::Colour playhead       { 0xffe86060 };

    struct Palette
    {
        const char* name;
        juce::Colour background, panel, nodeBody, nodeSelected,
                     headerDsp, headerCtrl, headerIO,
                     audioSignal, controlSignal, text, textDim, playhead;
    };

    inline const std::array<Palette, 3>& palettes()
    {
        static const std::array<Palette, 3> all { {
            { "Slate",
              juce::Colour (0xff1e2126), juce::Colour (0xff262a31), juce::Colour (0xff2e333c),
              juce::Colour (0xff4a90d9), juce::Colour (0xff3d5a80), juce::Colour (0xff5c8a4a),
              juce::Colour (0xff55585f), juce::Colour (0xffe8a04c), juce::Colour (0xff6ecfcf),
              juce::Colour (0xffd8dade), juce::Colour (0xff8a8f98), juce::Colour (0xffe86060) },
            { "Light",
              juce::Colour (0xfff0f1f4), juce::Colour (0xffe2e5ea), juce::Colour (0xffd7dbe3),
              juce::Colour (0xff2f6fbd), juce::Colour (0xff9fbcd8), juce::Colour (0xffa9c79a),
              juce::Colour (0xffb9bdc6), juce::Colour (0xffc77f26), juce::Colour (0xff238f8f),
              juce::Colour (0xff24272c), juce::Colour (0xff5c626e), juce::Colour (0xffd04545) },
            { "Neon",
              juce::Colour (0xff0d0f14), juce::Colour (0xff14171e), juce::Colour (0xff1b1f29),
              juce::Colour (0xff00c8ff), juce::Colour (0xff243a5e), juce::Colour (0xff2e5e2e),
              juce::Colour (0xff2e3138), juce::Colour (0xffff9a1f), juce::Colour (0xff00e5d0),
              juce::Colour (0xffe8ecf2), juce::Colour (0xff77808f), juce::Colour (0xffff4060) },
        } };
        return all;
    }

    inline juce::String currentPaletteName = "Slate";

    inline void applyPalette (const juce::String& name)
    {
        for (auto& p : palettes())
            if (name == p.name)
            {
                background = p.background;  panel = p.panel;
                nodeBody = p.nodeBody;      nodeSelected = p.nodeSelected;
                headerDsp = p.headerDsp;    headerCtrl = p.headerCtrl;
                headerIO = p.headerIO;      audioSignal = p.audioSignal;
                controlSignal = p.controlSignal;
                text = p.text;              textDim = p.textDim;
                playhead = p.playhead;
                currentPaletteName = p.name;
                return;
            }
    }

    // Roles the user can recolour freely (issue #8). Pointers target the
    // live globals above; message-thread only. Stored in the project as
    // "col_<key>" properties on the graph root so a custom scheme travels
    // with the patch.
    struct ColourRole { const char* key; const char* label; juce::Colour* target; };

    inline std::vector<ColourRole> customisableRoles()
    {
        return {
            { "background",    "Background",   &background },
            { "panel",         "Panels",       &panel },
            { "nodeBody",      "Node body",    &nodeBody },
            { "headerDsp",     "DSP header",   &headerDsp },
            { "headerCtrl",    "Ctrl header",  &headerCtrl },
            { "audioSignal",   "Audio cables", &audioSignal },
            { "controlSignal", "Ctrl cables",  &controlSignal },
            { "text",          "Text",         &text },
            { "nodeSelected",  "Selection",    &nodeSelected },
            { "playhead",      "Playhead",     &playhead },
        };
    }

    inline juce::Identifier colourPropertyFor (const char* key)
    {
        return juce::Identifier ("col_" + juce::String (key));
    }

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
            case NodeType::curve:    return "Curve";
            case NodeType::xyz:      return "XYZ";
            case NodeType::hosted:   return "Plugin";
        }
        return {};
    }
}
