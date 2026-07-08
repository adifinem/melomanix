# Melomanix

A VST3 plugin where modulation controllers (LFOs, macros, curves) and DSP
effects are peer nodes in a Blender-style node graph, with a fixed
always-visible timeline pane. Early development.

## Building (Linux)

Requires CMake ≥ 3.22, a C++20 compiler, and the JUCE Linux dependencies
(ALSA, JACK, X11, freetype/fontconfig, Mesa dev packages).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Produces a VST3 (copied to `~/.vst3` by default) and a Standalone app under
`build/Melomanix_artefacts/`. Windows VST3 builds are produced by the GitHub
Actions workflow.
