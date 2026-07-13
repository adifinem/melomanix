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

## New in v0.5 — second feedback round: morph curves, free colours, XYZ

Follow-ups from Will's testing of v0.4, plus a new request.

- **Connection morph curves ([#7]).** A modulation cable is no longer just an
  amount slider — it carries a full **transfer curve** that maps the source's
  value (input, left→right) to the value it applies (output, bottom→top).
  **Click a cable** in the graph (or its row in the Matrix pane) and shape the
  curve in the timeline pane with the same points-and-tension editing the
  Curve node uses. A red marker shows where the source currently sits and what
  it maps to. Draw a downward line to invert, a hump for a there-and-back
  sweep, a staircase for stepped motion — whatever you want. Until you shape
  it, the cable keeps the old linear behaviour, so existing patches are
  unchanged. Each Matrix row shows a mini preview of its curve.
- **Curve grid: any number ([#4]).** The point-snap grid menu gains
  **Custom…**, so you can type any column/row counts, not just the presets.
- **Free colours ([#8]).** Appearance → **Custom colours…** opens a colour
  wheel for each part of the UI (background, cables, headers, text, …).
  Changes apply live and are saved with the project. *Reset* returns to Slate.
- **XYZ controller ([#9]).** Right-click the canvas → **XYZ controller**. One
  node, three axes (X/Y/Z); each is a value you set (and can itself modulate),
  emitted from its own output socket — patch X, Y and Z to different
  parameters for vector-style control. *Note:* importing VST2 or FL
  `.fst`/`.dll` controllers isn't possible in an open-source JUCE 8 build
  (there's no redistributable VST2 SDK, and `.fst` is FL-only), so this is the
  build-our-own alternative you suggested.

[#4]: ../../issues/4
[#7]: ../../issues/7
[#8]: ../../issues/8
[#9]: ../../issues/9

---

## New in v0.4 — editing quality-of-life: Will's seven requests

All seven requests from the first user feedback round ([#1](../../issues/1)).

- **Cable drags follow the mouse.** The dragged cable used to drift away
  from the cursor once you'd panned the canvas; it now sticks to the
  pointer at any pan and zoom.
- **Curve length snaps to musical values.** The Length slider lands on
  1/16-note through 8-bar values (including triplet-friendly 3, 6, 12, 24
  beats). Hold **CTRL** while dragging for free, unquantised lengths.
- **Snap grid for curve points.** With a curve selected, a **grid** button
  appears in the timeline pane's top-right corner: off, 2×2, 4×4, 6×4,
  4×6, 8×8 or 16×8. Dragged and double-click-added points stick to grid
  intersections; **CTRL** bypasses. Each curve remembers its own grid.
- **Duplicate nodes.** Right-click a node → **Duplicate node**. The copy
  keeps all parameter values, curve points and (for hosted plugins) the
  plugin and its state, and lands slightly offset. Cables aren't copied.
- **Multi-select.** Left-drag on empty canvas draws a selection box; every
  node inside becomes a group you can drag as one, or right-click for
  **Duplicate/Delete selection**. Clicking empty canvas clears it.
  **Canvas panning is now middle-mouse drag** (wheel still zooms) —
  left-drag is reserved for selection, Blender-style.
- **Matrix pane.** New collapsible pane on the left listing every
  connection in the patch. Modulation connections get two sliders:
  **amount** (−100%..+100% — negative *inverts* the modulation, e.g. LFO 1
  into a VST parameter upside-down) and **offset** (shifts the modulation
  centre). Changes apply live. Collapse it with the `<` button to give the
  graph the space back.
- **Appearance.** Right-click the canvas → **Appearance**: pick the
  **Slate** (classic), **Light** or **Neon** palette — saved with your
  project — and toggle **Cable glow**, which makes control cables brighten
  and thicken with the live value flowing through them.

**Try it:** add a Curve, set grid 4×4, double-click to add snapped points;
band-select a few nodes and drag them as a block; then open the matrix,
drag **amount** negative on a connection and watch the modulation flip.

---

## New in v0.3.1 — plugin loading that actually finds your plugins

v0.3.0's file dialog was broken on Windows (it showed only folders — you
couldn't see any `.vst3` files). Fixed, and improved:

- Right-click the canvas → **Add installed plugin...** now *scans your
  standard VST3 folders* and lists every plugin it finds **by name** — just
  click one. No file dialog at all. (First open takes a few seconds while
  it scans; there's a Rescan item at the top.)
- **Load VST3 from file...** still exists for plugins in unusual places,
  and now starts in your VST3 folder and correctly shows files.
- If a plugin fails to load, the node now **shows the error on itself in
  red** instead of failing silently — including the common case of picking
  a plugin built for the wrong operating system (a Windows-only `.vst3`
  can't load on Linux, and vice versa).

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
