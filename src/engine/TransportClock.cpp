#include "TransportClock.h"

#include <algorithm>
#include <cmath>

namespace fxtend
{

void TransportClock::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void TransportClock::reset() noexcept
{
    nextPpq = 0.0;
    lastBlockQn = 0.0;
    wasSynced = false;
    wasPlaying = false;
    firstBlock = true;
}

void TransportClock::setManualBpm (double bpm) noexcept
{
    manualBpm = std::clamp (bpm, 20.0, 999.0);
}

BlockTiming TransportClock::beginBlock (const TransportState& state, int numSamples) noexcept
{
    BlockTiming t;

    t.tempoFromHost = state.hasBpm && state.bpm > 0.0;
    t.bpm = t.tempoFromHost ? state.bpm : manualBpm;

    t.timeSigFromHost = state.hasTimeSig
                        && state.timeSig.numerator > 0
                        && state.timeSig.denominator > 0;
    t.timeSig = t.timeSigFromHost ? state.timeSig : TimeSignature {};

    t.qnPerSample = t.bpm / (60.0 * sampleRate);
    t.hostSynced = state.isPlaying && state.hasPpq;

    const double blockQn = numSamples * t.qnPerSample;

    if (t.hostSynced)
    {
        t.ppqStart = state.ppq;

        // The host only reports one tempo per block, so a tempo ramp makes the
        // real position drift a little from our prediction. Anything beyond
        // this tolerance is a seek or a loop wrap rather than drift. The
        // prediction was made with the previous block's tempo, so that block
        // counts as well: after a tempo drop the new block alone is too small
        // a measure, and the step would be taken for a seek.
        const double tolerance = std::max (0.05, 0.5 * std::max (blockQn, lastBlockQn));
        t.discontinuity = ! wasSynced || std::abs (state.ppq - nextPpq) > tolerance;
    }
    else
    {
        t.ppqStart = nextPpq;
        t.discontinuity = wasSynced;
    }

    if (state.isPlaying != wasPlaying)
        t.discontinuity = true;

    if (firstBlock)
        t.discontinuity = false;

    nextPpq = t.ppqStart + blockQn;
    lastBlockQn = blockQn;
    wasSynced = t.hostSynced;
    wasPlaying = state.isPlaying;
    firstBlock = false;

    return t;
}

} // namespace fxtend
