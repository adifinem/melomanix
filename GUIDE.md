# Melomanix — Feature Guide & Changelog

A hands-on tour of every feature, newest first. Each version section shows
what was added and walks you through actually using it. New releases add a
new section at the top, so skim down to where you left off.

## Getting started (any version)

1. Grab the zip for your OS from the [releases page](../../releases) and
   extract it.
   - **Windows:** copy the `Melomanix.vst3` *folder* into
     `C:\Program Files\Common Files\VST3\`
   - **Linux:** copy it into `~/.vst3/`
2. Rescan plugins in your DAW (FL Studio: Options → Manage plugins → Find
   plugins) and add **Melomanix** as an effect on a channel or mixer insert.
3. Play something through it — a drum loop is ideal — and open the plugin
   window. You'll see three fixed panes:
   - **Macro strip** (top): eight dials your DAW can automate
   - **Node graph** (middle): where everything gets patched
   - **Timeline** (bottom): shows the "performance" of whatever you click

Color code everywhere: **orange = audio**, **teal = control signals**.

---

## New in v0.3 — host other plugins inside the graph

Melomanix can now load third-party VST3 plugins as nodes and modulate
their knobs, Patcher-style.

**Try it:**

1. Right-click empty graph space → **Load VST3 plugin...** and pick any
   `.vst3` from your plugin folder (on Windows that's usually
   `C:\Program Files\Common Files\VST3\`).
2. It appears as a node. Wire it into the audio path: drag from Audio In's
   orange dot to the plugin node's orange input, and from its orange output
   to Audio Out. (If Audio In is still wired straight to Audio Out,
   right-click one of those orange dots → Disconnect first.)
3. **Double-click the node's title** — the plugin's own interface opens in
   its own window. Tweak it like normal; everything you set is saved with
   your project.
4. Now the fun part. **Right-click the node → Expose parameter** and tick
   any parameter — say a filter cutoff or reverb size. It appears as a row
   on the node with a teal socket.
5. Add an **LFO** (right-click canvas → Add node → LFO) and drag from its
   teal output onto that new row's teal socket. You are now modulating a
   third-party plugin's knob from the graph. Try a **Curve** node there
   instead for drawn, tempo-locked shapes.

Notes and honest caveats:
- Parameters you *don't* expose (or expose but leave unconnected) stay
  fully under the plugin's own control — Melomanix only drives a parameter
  while a cable is attached to it.
- VST3 only (the old VST2 format can't be licensed for new hosts).
- A badly-behaved plugin can crash the host — FL Patcher shares this
  weakness. A crash-shield is on the roadmap.
- Synth/instrument plugins load but don't receive MIDI notes yet, so
  **effects** are the interesting thing to host for now.

---

## New in v0.2 — drawn curves, real value readouts, tempo sync

**Hover value bubbles.** Hover (or drag) any slider on any node: a bubble
shows the real value — "804 Hz", "-3.5 dB", "35%", and delay times get a
note-value annotation at your project tempo, like "250 ms (1/8)".

**Curve node — draw your own modulation.** Right-click the canvas → Add
node → **Curve**, then click it. The bottom pane becomes an editor:

- **Drag points** to move them (up/down = value, left/right = timing)
- **Double-click** empty space to add a point
- **Right-click a point** to delete it
- **Drag the space between two points** up or down to bend that segment —
  this is the smoothing/tension control
- The **Length** slider on the node sets how many beats the curve spans
  before looping; it stays locked to your project's tempo and playhead.

Patch the Curve's teal output anywhere an LFO can go — a delay's Mix, an
EQ band's gain, another LFO's rate.

**LFO upgrades.**
- **Sync** slider: locks the rate to note divisions (1/1 down to 1/16) of
  the project tempo instead of free Hz.
- **Smooth** slider: rounds off the hard steps of square and saw shapes.

**Modulation depth.** Right-click the teal socket *next to a modulated
parameter* → **Mod depth** to scale the modulation down (or invert it with
the negative values). 100% swings the full range; ±10–50% is usually more
musical.

---

## v0.1 — the core (first release)

The founding idea: **controllers and effects are equal citizens of one
graph**, and whatever you click shows its motion in a fixed timeline pane.

**Patching basics:**

1. Right-click empty canvas → **Add node** → LFO, EQ, or Delay.
2. Move nodes by dragging their title bars. Mouse wheel zooms, dragging
   the background pans.
3. Connect audio: drag from an orange output dot to an orange input dot.
   Route Audio In → Delay → Audio Out and you'll hear echoes.
4. Connect modulation: drag from a controller's teal output onto the teal
   socket beside any parameter — including **another controller's**
   parameter. An LFO modulating another LFO's rate is the signature move.
5. One controller can fan out to many targets. Right-click any socket to
   disconnect; right-click a node's title to delete it.

**Timeline pane:** click an LFO and watch its actual waveform scroll under
a red playhead, synced to your DAW's transport. What you see is exactly
what modulates — same math, same clock.

**Macros:** the eight top-strip dials are real plugin parameters, so your
DAW can automate them (FL Studio: right-click the dial in the plugin
wrapper → link or automate as usual). Drag the small dotted grip beside a
dial onto any parameter row to wire that macro in instantly.

**Everything persists:** the whole patch — nodes, cables, curves, hosted
plugins and their internal settings — saves and reloads with your project.
