// Headless engine tests. Exits nonzero on first failure — run by CI and
// local builds; no GUI or audio device needed.

#include "../src/model/GraphModel.h"
#include "../src/engine/GraphEngine.h"
#include "../src/engine/HostedPlugin.h"
#include "../src/engine/Transport.h"

#include <iostream>

static int failures = 0;

static void expect (bool condition, const char* description)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << description << "\n";
        ++failures;
    }
    else
    {
        std::cout << "ok:   " << description << "\n";
    }
}

int main()
{
    using namespace melo;

    // --- Model rules ---------------------------------------------------
    GraphModel model;

    auto findIO = [&model] (NodeType t)
    {
        for (auto child : model.state())
            if (child.hasType (ids::node)
                && nodeTypeFromString (child.getProperty (ids::type).toString()) == t)
                return (int) child.getProperty (ids::nodeId);
        return -1;
    };

    auto audioIn  = findIO (NodeType::audioIn);
    auto audioOut = findIO (NodeType::audioOut);
    expect (audioIn > 0 && audioOut > 0, "default graph has audio in/out");

    auto delay = model.addNode (NodeType::delay, 0.0f, 0.0f);
    auto lfo1  = model.addNode (NodeType::lfo, 0.0f, 100.0f);
    auto lfo2  = model.addNode (NodeType::lfo, 0.0f, 200.0f);
    auto macro = model.addMacroNode (0, 0.0f, 300.0f);

    // Rewire audio path through the delay. The default in->out edge must be
    // removed first because audio inputs accept one source.
    for (int i = model.state().getNumChildren(); --i >= 0;)
        if (model.state().getChild (i).hasType (ids::conn))
            model.state().removeChild (i, nullptr);

    expect (model.addAudioConnection (audioIn, delay),  "audioIn -> delay");
    expect (model.addAudioConnection (delay, audioOut), "delay -> audioOut");
    expect (! model.addAudioConnection (audioIn, delay), "second source into one audio input rejected");
    expect (! model.addAudioConnection (lfo1, delay),    "controller cannot carry audio");

    expect (model.addModConnection (lfo1, delay, "time"),  "controller -> DSP param");
    expect (model.addModConnection (lfo1, delay, "mix"),   "fan-out: same controller -> second param");
    expect (! model.addModConnection (lfo2, delay, "time"), "fan-in rejected: second source on same param");
    expect (model.addModConnection (lfo2, lfo1, "rate"),   "controller -> controller param (the core bet)");
    expect (model.addModConnection (macro, lfo2, "rate"),  "macro -> controller param");
    expect (! model.addModConnection (lfo1, lfo2, "depth"), "cycle rejected (lfo2 already feeds lfo1)");
    expect (! model.addModConnection (lfo1, delay, "nope"), "unknown param rejected");

    // --- Engine behaviour ----------------------------------------------
    std::atomic<float> macroValue { 0.0f };
    std::vector<std::atomic<float>*> macroValues { &macroValue };

    auto compiled = compileGraph (model.state(), macroValues);
    expect (compiled != nullptr && compiled->nodes.size() == 6, "graph compiles with all nodes");

    GraphEngine engine;
    engine.prepare (48000.0, 256);
    engine.setGraph (compiled);

    ProcessContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.maxBlockSize = 256;
    ctx.numSamples = 256;

    juce::AudioBuffer<float> buffer (2, 256);

    // Feed an impulse, then silence; with a wet mix the delayed impulse
    // must appear in the output after the delay time has elapsed. The LFO
    // mods on time/mix are detached first so the echo lands at a known time.
    for (int i = model.state().getNumChildren(); --i >= 0;)
    {
        auto child = model.state().getChild (i);
        if (child.hasType (ids::conn) && (int) child.getProperty (ids::dstNode) == delay
            && child.hasProperty (ids::dstParam))
            model.removeConnection (child);
    }
    model.setParamValue (delay, "mix", 0.5f);
    model.setParamValue (delay, "time", 100.0f);
    compiled = compileGraph (model.state(), macroValues);
    engine.setGraph (compiled);

    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    double seconds = 0.0;
    float energyAfterDelay = 0.0f;

    for (int block = 0; block < 40; ++block)
    {
        ctx.playheadSeconds = seconds;
        engine.process (buffer, ctx);

        if (block > 18)   // 100ms at 48k = ~4800 samples = ~19 blocks
            energyAfterDelay += buffer.getMagnitude (0, 256);

        seconds += 256.0 / 48000.0;
        buffer.clear();
    }

    expect (energyAfterDelay > 0.001f, "delayed signal appears at output");

    // LFO output must move over time and respond to its modulated rate.
    auto* lfoNode = [&compiled, lfo1]() -> EngineNode*
    {
        for (auto& n : compiled->nodes)
            if (n->modelNodeId == lfo1)
                return n.get();
        return nullptr;
    }();

    expect (lfoNode != nullptr, "compiled graph contains lfo1");

    float minOut = 1.0f, maxOut = 0.0f;
    for (int block = 0; block < 100; ++block)
    {
        ctx.playheadSeconds = seconds;
        engine.process (buffer, ctx);
        minOut = std::min (minOut, lfoNode->lastOutput);
        maxOut = std::max (maxOut, lfoNode->lastOutput);
        seconds += 256.0 / 48000.0;
        buffer.clear();
    }
    expect (maxOut - minOut > 0.5f, "LFO output sweeps over time");

    // Macro drives lfo2.rate which drives lfo1.rate: changing the macro must
    // change lfo1's effective rate parameter (controller chain works).
    auto rateBefore = lfoNode->params[0].current();
    macroValue.store (1.0f);
    for (int block = 0; block < 200; ++block)   // let smoothing settle
    {
        ctx.playheadSeconds = seconds;
        engine.process (buffer, ctx);
        seconds += 256.0 / 48000.0;
        buffer.clear();
    }
    auto rateAfter = lfoNode->params[0].current();
    expect (std::abs (rateAfter - rateBefore) > 0.01f,
            "macro -> lfo2 -> lfo1.rate chain changes effective rate");

    // --- Tempo sync -------------------------------------------------------
    expect (juce::approximatelyEqual (LFONode::effectiveRate (3.3f, 0, 120.0), (double) 3.3f),
            "sync off passes the Hz knob through");
    expect (juce::approximatelyEqual (LFONode::effectiveRate (3.3f, 3, 120.0), 2.0),
            "1/4 note at 120bpm = 2Hz");
    expect (juce::approximatelyEqual (LFONode::effectiveRate (3.3f, 1, 60.0), 0.25),
            "1/1 note at 60bpm = 0.25Hz");

    // --- Curve node -------------------------------------------------------
    {
        std::vector<CurvePoint> ramp { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        expect (juce::approximatelyEqual (CurveNode::valueAt (ramp, 0.5f), 0.5f),
                "linear segment interpolates");
        expect (juce::approximatelyEqual (CurveNode::valueAt (ramp, -0.2f), 0.0f)
                && juce::approximatelyEqual (CurveNode::valueAt (ramp, 1.2f), 1.0f),
                "curve clamps outside its points");

        ramp[0].tension = 1.0f;
        expect (CurveNode::valueAt (ramp, 0.5f) < 0.3f, "positive tension eases in");
        ramp[0].tension = -1.0f;
        expect (CurveNode::valueAt (ramp, 0.5f) > 0.7f, "negative tension eases out");

        auto curveId = model.addNode (NodeType::curve, 0.0f, 400.0f);
        auto curveTree = model.getNode (curveId);
        int pointCount = 0;
        for (auto child : curveTree)
            if (child.hasType (ids::point))
                ++pointCount;
        expect (pointCount == 3, "new curve node has default points");

        expect (model.addModConnection (curveId, delay, "feedback"),
                "curve output patches into a DSP param");

        auto withCurve = compileGraph (model.state(), macroValues);
        CurveNode* compiledCurve = nullptr;
        for (auto& n : withCurve->nodes)
            if (n->modelNodeId == curveId)
                compiledCurve = dynamic_cast<CurveNode*> (n.get());
        expect (compiledCurve != nullptr && compiledCurve->points.size() == 3,
                "compiled curve carries its sorted points");

        // --- Node duplication -------------------------------------------
        model.setParamValue (curveId, "length", 8.0f);
        auto dupId = model.duplicateNode (curveId);
        expect (dupId > curveId, "duplicate gets a fresh node id");

        auto dup = model.getNode (dupId);
        int dupPoints = 0;
        for (auto child : dup)
            if (child.hasType (ids::point))
                ++dupPoints;
        expect (dupPoints == 3, "duplicate copies curve points");
        expect (juce::approximatelyEqual (model.getParamValue (dupId, "length"), 8.0f),
                "duplicate copies parameter values");
        expect (juce::approximatelyEqual ((float) dup.getProperty (ids::posX),
                                          (float) curveTree.getProperty (ids::posX) + 40.0f),
                "duplicate offsets its position");

        int dupConns = 0;
        for (auto child : model.state())
            if (child.hasType (ids::conn)
                && ((int) child.getProperty (ids::srcNode) == dupId
                    || (int) child.getProperty (ids::dstNode) == dupId))
                ++dupConns;
        expect (dupConns == 0, "duplicate carries no connections");

        expect (model.duplicateNode (findIO (NodeType::audioIn)) == -1,
                "IO nodes cannot be duplicated");
    }

    // --- Per-connection remap: depth inversion and offset ----------------
    {
        GraphModel remapModel;
        auto rIn = -1, rOut = -1;
        for (auto c : remapModel.state())
            if (c.hasType (ids::node))
            {
                auto t = c.getProperty (ids::type).toString();
                if (t == "audioIn")  rIn  = c.getProperty (ids::nodeId);
                if (t == "audioOut") rOut = c.getProperty (ids::nodeId);
            }
        juce::ignoreUnused (rIn, rOut);

        auto rDelay = remapModel.addNode (NodeType::delay, 0.0f, 0.0f);
        auto rMacro = remapModel.addMacroNode (0, 0.0f, 100.0f);
        expect (remapModel.addModConnection (rMacro, rDelay, "mix"), "remap: macro -> delay.mix");

        // Point the connection at full inversion with a centre offset.
        for (auto c : remapModel.state())
            if (c.hasType (ids::conn) && c.hasProperty (ids::dstParam))
            {
                c.setProperty (ids::depth, -0.5f, nullptr);
                c.setProperty (ids::offset, 0.25f, nullptr);
            }

        std::atomic<float> remapMacro { 1.0f };   // bipolar(1) = +1
        std::vector<std::atomic<float>*> remapMacros { &remapMacro };
        remapModel.setParamValue (rDelay, "mix", 0.5f);   // mix range is 0..1

        auto remapCompiled = compileGraph (remapModel.state(), remapMacros);
        GraphEngine remapEngine;
        remapEngine.prepare (48000.0, 256);
        remapEngine.setGraph (remapCompiled);

        juce::AudioBuffer<float> remapBuffer (2, 256);
        ProcessContext remapCtx;
        remapCtx.sampleRate = 48000.0;
        remapCtx.maxBlockSize = 256;
        remapCtx.numSamples = 256;
        for (int block = 0; block < 400; ++block)   // let smoothing settle
        {
            remapEngine.process (remapBuffer, remapCtx);
            remapBuffer.clear();
        }

        EngineNode* compiledDelay = nullptr;
        for (auto& n : remapCompiled->nodes)
            if (n->modelNodeId == rDelay)
                compiledDelay = n.get();

        // base 0.5 + offset 0.25 + depth -0.5 * (+1) = 0.25
        auto mixIndex = compiledDelay->indexOfParam ("mix");
        auto effective = compiledDelay->params[(size_t) mixIndex].current();
        expect (std::abs (effective - 0.25f) < 0.02f,
                "connection depth inverts and offset shifts the modulation");
    }

    // --- Per-connection morph curve (issue #7) ---------------------------
    {
        GraphModel morphModel;
        auto mDelay = morphModel.addNode (NodeType::delay, 0.0f, 0.0f);
        auto mMacro = morphModel.addMacroNode (0, 0.0f, 100.0f);
        expect (morphModel.addModConnection (mMacro, mDelay, "mix"), "morph: macro -> delay.mix");

        // Give the connection an inverting transfer curve: source 0 -> 1,
        // source 1 -> 0. This must override the default linear map.
        juce::ValueTree morphConn;
        for (auto c : morphModel.state())
            if (c.hasType (ids::conn) && c.hasProperty (ids::dstParam))
                morphConn = c;
        for (auto [t, v] : { std::pair<float, float> { 0.0f, 1.0f }, { 1.0f, 0.0f } })
        {
            juce::ValueTree p (ids::point);
            p.setProperty (ids::pointT, t, nullptr);
            p.setProperty (ids::pointV, v, nullptr);
            morphConn.addChild (p, -1, nullptr);
        }

        std::atomic<float> morphMacro { 1.0f };
        std::vector<std::atomic<float>*> morphMacros { &morphMacro };

        auto settle = [&] (float macroVal)
        {
            morphMacro.store (macroVal);
            auto compiledM = compileGraph (morphModel.state(), morphMacros);
            GraphEngine eng;
            eng.prepare (48000.0, 256);
            eng.setGraph (compiledM);
            juce::AudioBuffer<float> buf (2, 256);
            ProcessContext c; c.sampleRate = 48000.0; c.maxBlockSize = 256; c.numSamples = 256;
            for (int b = 0; b < 400; ++b) { eng.process (buf, c); buf.clear(); }
            EngineNode* d = nullptr;
            for (auto& n : compiledM->nodes) if (n->modelNodeId == mDelay) d = n.get();
            return d->params[(size_t) d->indexOfParam ("mix")].current();
        };

        expect (settle (1.0f) < 0.05f, "morph curve maps source=1 to output~0 (inverted)");
        expect (settle (0.0f) > 0.95f, "morph curve maps source=0 to output~1 (inverted)");

        // The compiled connection must carry the sorted morph points.
        auto compiledMorph = compileGraph (morphModel.state(), morphMacros);
        EngineNode* dNode = nullptr;
        for (auto& n : compiledMorph->nodes) if (n->modelNodeId == mDelay) dNode = n.get();
        auto& mixParam = dNode->params[(size_t) dNode->indexOfParam ("mix")];
        expect (mixParam.morphPoints.size() == 2, "compiled connection carries its morph points");
    }

    // --- XYZ controller: independent multi-output routing (issue #9) ------
    {
        GraphModel xyzModel;
        auto delayA = xyzModel.addNode (NodeType::delay, 0.0f, 0.0f);
        auto delayB = xyzModel.addNode (NodeType::delay, 0.0f, 100.0f);
        auto xyz    = xyzModel.addNode (NodeType::xyz,   0.0f, 200.0f);
        xyzModel.setParamValue (xyz, "x", 0.2f);
        xyzModel.setParamValue (xyz, "y", 0.6f);
        xyzModel.setParamValue (xyz, "z", 0.9f);

        // Y output (srcOut 1) -> delayA.mix, Z output (srcOut 2) -> delayB.mix,
        // each through an identity morph so the param settles to that axis.
        expect (xyzModel.addModConnection (xyz, delayA, "mix", 1.0f, 1), "xyz: Y -> delayA.mix");
        expect (xyzModel.addModConnection (xyz, delayB, "mix", 1.0f, 2), "xyz: Z -> delayB.mix");
        for (auto c : xyzModel.state())
            if (c.hasType (ids::conn) && c.hasProperty (ids::dstParam))
                for (auto [t, v] : { std::pair<float, float> { 0.0f, 0.0f }, { 1.0f, 1.0f } })
                {
                    juce::ValueTree p (ids::point);
                    p.setProperty (ids::pointT, t, nullptr);
                    p.setProperty (ids::pointV, v, nullptr);
                    c.addChild (p, -1, nullptr);
                }

        std::vector<std::atomic<float>*> noMacros;
        auto xyzCompiled = compileGraph (xyzModel.state(), noMacros);
        GraphEngine xyzEngine;
        xyzEngine.prepare (48000.0, 256);
        xyzEngine.setGraph (xyzCompiled);
        juce::AudioBuffer<float> xyzBuf (2, 256);
        ProcessContext xyzCtx; xyzCtx.sampleRate = 48000.0; xyzCtx.maxBlockSize = 256; xyzCtx.numSamples = 256;
        for (int b = 0; b < 400; ++b) { xyzEngine.process (xyzBuf, xyzCtx); xyzBuf.clear(); }

        EngineNode* dA = nullptr; EngineNode* dB = nullptr;
        for (auto& n : xyzCompiled->nodes)
        {
            if (n->modelNodeId == delayA) dA = n.get();
            if (n->modelNodeId == delayB) dB = n.get();
        }
        auto mixA = dA->params[(size_t) dA->indexOfParam ("mix")].current();
        auto mixB = dB->params[(size_t) dB->indexOfParam ("mix")].current();
        expect (std::abs (mixA - 0.6f) < 0.03f, "xyz Y output (0.6) routes independently to delayA.mix");
        expect (std::abs (mixB - 0.9f) < 0.03f, "xyz Z output (0.9) routes independently to delayB.mix");
    }

    // --- Transport clock: LFO determinism across stop/play ---------------
    {
        // A host that reports musical position (ppq) but no seconds — the
        // case that made LFO-modulated params random every stop/play.
        auto t0 = resolveTransportTime (true, 0.0, false, 999.0, 120.0);   // "seconds" here is a bogus free-run
        auto t1 = resolveTransportTime (true, 0.0, false, 40.0,  120.0);   // second play: free-run has advanced
        expect (juce::approximatelyEqual (t0.seconds, 0.0) && juce::approximatelyEqual (t1.seconds, 0.0),
                "beat 0 always resolves to seconds 0 regardless of any free-running clock");

        auto tb = resolveTransportTime (true, 4.0, false, 0.0, 120.0);
        expect (juce::approximatelyEqual (tb.seconds, 2.0),
                "4 beats at 120bpm resolves to 2.0s");

        // Standalone / no host position: seconds passes through, beats derived.
        auto tf = resolveTransportTime (false, 0.0, false, 3.0, 120.0);
        expect (juce::approximatelyEqual (tf.seconds, 3.0) && juce::approximatelyEqual (tf.beats, 6.0),
                "no host position: free-running seconds pass through");

        // End to end: an LFO fed transport-resolved time gives the SAME output
        // at the same musical position on two separate "plays".
        GraphModel lfoModel;
        int lIn = -1, lOut = -1;
        for (auto c : lfoModel.state())
            if (c.hasType (ids::node))
            {
                auto tt = c.getProperty (ids::type).toString();
                if (tt == "audioIn")  lIn  = c.getProperty (ids::nodeId);
                if (tt == "audioOut") lOut = c.getProperty (ids::nodeId);
            }
        juce::ignoreUnused (lIn, lOut);
        auto lDelay = lfoModel.addNode (NodeType::delay, 0.0f, 0.0f);
        auto lfo    = lfoModel.addNode (NodeType::lfo, 0.0f, 100.0f);
        lfoModel.setParamValue (lfo, "rate", 3.0f);   // free-Hz
        lfoModel.setParamValue (lfo, "sync", 0.0f);
        expect (lfoModel.addModConnection (lfo, lDelay, "mix"), "transport: lfo -> delay.mix");

        std::vector<std::atomic<float>*> noMacros;
        auto play = [&] (double startFreeRun)
        {
            auto compiled = compileGraph (lfoModel.state(), noMacros);
            GraphEngine eng; eng.prepare (48000.0, 256); eng.setGraph (compiled);
            juce::AudioBuffer<float> buf (2, 256);
            EngineNode* d = nullptr;
            for (auto& n : compiled->nodes) if (n->modelNodeId == lDelay) d = n.get();
            // Walk beats 0..~1, resolving time as the processor now does. The
            // "free-run" offset stands in for a clock that used to leak in.
            double lastMix = 0.0;
            for (int b = 0; b < 64; ++b)
            {
                double beats = b * (1.0 / 64.0);
                auto tt = resolveTransportTime (true, beats, false, startFreeRun + b, 120.0);
                ProcessContext c; c.sampleRate = 48000.0; c.maxBlockSize = 256; c.numSamples = 256;
                c.playheadSeconds = tt.seconds; c.playheadBeats = tt.beats; c.bpm = 120.0;
                eng.process (buf, c); buf.clear();
                lastMix = d->params[(size_t) d->indexOfParam ("mix")].current();
            }
            return lastMix;
        };
        auto playA = play (0.0);
        auto playB = play (1000.0);   // a very different free-running clock
        expect (std::abs (playA - playB) < 1.0e-4f,
                "LFO-modulated param is identical across plays (transport-locked, was random)");
    }

    // --- Serialisation round-trip ---------------------------------------
    auto xml = model.state().toXmlString();
    GraphModel restored;
    restored.replaceState (juce::ValueTree::fromXml (xml));
    auto recompiled = compileGraph (restored.state(), macroValues);
    auto reference = compileGraph (model.state(), macroValues);
    expect (recompiled != nullptr && recompiled->nodes.size() == reference->nodes.size(),
            "graph survives XML round-trip");

    // --- Hosted plugin (optional: needs MELOMANIX_TEST_VST3=<path>) --------
    if (auto vst3Path = juce::SystemStats::getEnvironmentVariable ("MELOMANIX_TEST_VST3", {});
        vst3Path.isNotEmpty())
    {
        juce::ScopedJuceInitialiser_GUI juceInit;   // hosting needs a message manager

        GraphModel hostModel;
        auto hIn  = [&hostModel] { for (auto c : hostModel.state())
                                       if (c.hasType (ids::node) && c.getProperty (ids::type).toString() == "audioIn")
                                           return (int) c.getProperty (ids::nodeId);
                                   return -1; }();
        auto hOut = [&hostModel] { for (auto c : hostModel.state())
                                       if (c.hasType (ids::node) && c.getProperty (ids::type).toString() == "audioOut")
                                           return (int) c.getProperty (ids::nodeId);
                                   return -1; }();

        auto hostedId = hostModel.addHostedNode (vst3Path, "TestPlugin", 0.0f, 0.0f);

        for (int i = hostModel.state().getNumChildren(); --i >= 0;)
            if (hostModel.state().getChild (i).hasType (ids::conn))
                hostModel.state().removeChild (i, nullptr);
        expect (hostModel.addAudioConnection (hIn, hostedId), "audio connects into hosted node");
        expect (hostModel.addAudioConnection (hostedId, hOut), "hosted node connects to output");

        HostedPluginRegistry registry;
        registry.prepare (48000.0, 256);
        auto loadErrors = registry.syncWithModel (hostModel.state());
        expect (loadErrors.isEmpty(), ("hosted plugin loads: " + loadErrors.joinIntoString ("; ")).toRawUTF8());
        expect (registry.instanceFor (hostedId) != nullptr, "registry holds the instance");

        auto hostedGraph = compileGraph (hostModel.state(), macroValues,
                                         [&registry] (int id) { return registry.instanceFor (id); });
        GraphEngine hostedEngine;
        hostedEngine.prepare (48000.0, 256);
        hostedEngine.setGraph (hostedGraph);

        juce::AudioBuffer<float> hostBuffer (2, 256);
        hostBuffer.clear();
        ProcessContext hostCtx;
        hostCtx.sampleRate = 48000.0;
        hostCtx.maxBlockSize = 256;
        hostCtx.numSamples = 256;
        for (int block = 0; block < 8; ++block)
            hostedEngine.process (hostBuffer, hostCtx);
        expect (true, "audio processes through hosted plugin without crashing");

        // State capture round-trip.
        auto graphRef = hostModel.state();
        registry.captureStates (graphRef);
        expect (hostModel.getNode (hostedId).getProperty (ids::pluginState).toString().isNotEmpty(),
                "hosted plugin state captured into the tree");

        // Exposed-parameter modulation: LFO -> hosted param 0.
        auto* inst = registry.instanceFor (hostedId);
        if (inst != nullptr && ! inst->getParameters().isEmpty())
        {
            expect (! hostModel.addModConnection (
                        hostModel.addNode (NodeType::lfo, 0.0f, 0.0f), hostedId, "p0"),
                    "mod into unexposed hosted param rejected");

            hostModel.setHostedParamExposed (hostedId, 0,
                                             inst->getParameters()[0]->getName (40), true);
            auto modLfo = hostModel.addNode (NodeType::lfo, 0.0f, 100.0f);
            hostModel.setParamValue (modLfo, "rate", 5.0f);
            expect (hostModel.addModConnection (modLfo, hostedId, "p0"),
                    "mod into exposed hosted param accepted");

            auto modGraph = compileGraph (hostModel.state(), macroValues,
                                          [&registry] (int id) { return registry.instanceFor (id); });
            hostedEngine.setGraph (modGraph);

            auto before = inst->getParameters()[0]->getValue();
            float minSeen = 1.0f, maxSeen = 0.0f;
            double beats = 0.0;
            for (int block = 0; block < 60; ++block)
            {
                hostCtx.playheadSeconds = beats / 2.0;
                hostCtx.playheadBeats = beats;
                hostedEngine.process (hostBuffer, hostCtx);
                auto v = inst->getParameters()[0]->getValue();
                minSeen = std::min (minSeen, v);
                maxSeen = std::max (maxSeen, v);
                beats += 256.0 / 48000.0 * 2.0;
                hostBuffer.clear();
            }
            juce::ignoreUnused (before);
            expect (maxSeen - minSeen > 0.2f,
                    "LFO visibly sweeps the hosted plugin's parameter");
        }
    }
    else
    {
        std::cout << "(hosted plugin test skipped: set MELOMANIX_TEST_VST3=<path.vst3>)\n";
    }

    std::cout << (failures == 0 ? "\nALL ENGINE TESTS PASSED\n"
                                : "\nENGINE TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
