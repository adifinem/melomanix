# Melomanix

A VST3 plugin where modulation controllers (LFOs, macros, curves) and DSP
effects are peer nodes in a Blender-style node graph, with a fixed
always-visible timeline pane. Early development.

**New here? Read [GUIDE.md](GUIDE.md)** — a hands-on walkthrough of every
feature, organized by release so you can see what's new and how to use it.

Current state (v0): pan/zoom node graph with drag-to-connect cables; LFO,
3-band EQ and delay nodes; controller outputs patch into DSP params *and*
other controllers' params; 8 host-automatable macros with drag-to-assign;
timeline pane showing the selected controller's lane with a transport-synced
playhead; tempo-synced LFO rates; full graph state save/load. Headless
engine tests + pluginval strictness 10 pass.

## Building (Linux)

Requires CMake ≥ 3.22, a C++20 compiler, and the JUCE Linux dependencies
(ALSA, JACK, X11, freetype/fontconfig, Mesa dev packages).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Produces a VST3 (copied to `~/.vst3` by default) and a Standalone app under
`build/Melomanix_artefacts/`. Windows VST3 builds are produced by the GitHub
Actions workflows.

## Installing a release build (Windows / FL Studio)

Grab `Melomanix-*-windows-VST3.zip` from the
[releases page](../../releases), extract it, and copy the `Melomanix.vst3`
folder into `C:\Program Files\Common Files\VST3\`. In FL Studio: Options →
Manage plugins → Find more plugins (scan), then add "Melomanix" to a mixer
insert. On Linux, use the `-linux-` zip and extract into `~/.vst3/`.

## License

AGPL-3.0 (matching the JUCE 8 open-source license). See [LICENSE](LICENSE).
