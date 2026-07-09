// Headless engine tests. Exits nonzero on first failure — run by CI and
// local builds; no GUI or audio device needed.

#include "../src/model/GraphModel.h"
#include "../src/engine/GraphEngine.h"
#include "../src/engine/HostedPlugin.h"

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
