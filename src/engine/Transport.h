#pragma once

namespace melo
{

// A resolved (seconds, beats) pair for one processing block.
struct TransportTime
{
    double seconds = 0.0;
    double beats = 0.0;
};

// Resolve the transport clock so *both* the seconds- and beats-based
// controllers reset with the host transport.
//
// The bug this fixes: LFO phase is derived from `seconds` and curve phase
// from `beats`. Some hosts (FL Studio among them) report a musical position
// (ppq) reliably but leave getTimeInSeconds() empty. The old code then let
// `seconds` free-run on an internal clock that never reset on stop/play, so
// LFO-modulated parameters landed on a *different* value every time the
// transport restarted — while curve-modulated ones (using ppq) behaved.
// That is why only some parameters looked "random".
//
// The musical position is therefore canonical whenever the host provides it:
// seconds = beats * 60 / bpm is the identical real time, but correctly
// anchored to the transport, so it resets on stop/play. Only when the host
// offers no position at all (standalone) do we fall back to the caller's
// free-running seconds.
inline TransportTime resolveTransportTime (bool haveBeats, double beats,
                                           bool haveSeconds, double seconds,
                                           double bpm)
{
    bpm = bpm > 0.0 ? bpm : 120.0;

    if (haveBeats)
        return { beats * 60.0 / bpm, beats };

    // No musical position: use whatever seconds we have (host or free-running)
    // and derive the musical position from it.
    (void) haveSeconds;
    return { seconds, seconds * bpm / 60.0 };
}

} // namespace melo
