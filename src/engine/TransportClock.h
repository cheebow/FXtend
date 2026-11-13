#pragma once

namespace fxtend
{

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
};

/** What the host told us about the transport at the first sample of a block.
    Every field except isPlaying may be missing, depending on the host. */
struct TransportState
{
    bool isPlaying = false;

    bool hasPpq = false;
    double ppq = 0.0;

    bool hasBpm = false;
    double bpm = 120.0;

    bool hasTimeSig = false;
    TimeSignature timeSig;
};

/** Musical timing resolved for one processing block. */
struct BlockTiming
{
    double ppqStart = 0.0;          // quarter-note position of sample 0
    double qnPerSample = 0.0;       // quarter notes advanced per sample
    double bpm = 120.0;
    TimeSignature timeSig;

    bool hostSynced = false;        // ppqStart comes from a playing host
    bool tempoFromHost = false;     // false -> manual BPM fallback in use
    bool timeSigFromHost = false;   // false -> 4/4 fallback in use
    bool discontinuity = false;     // seek / loop / start / stop since the last block

    double ppqAt (int sample) const noexcept { return ppqStart + sample * qnPerSample; }
};

/** Turns per-block host transport info into a continuous quarter-note timeline.

    While the host plays, the host PPQ is the source of truth and is re-read
    every block. While it is stopped (or reports no PPQ) the clock free-runs so
    that previews and live key presses still have a timeline to play against. */
class TransportClock
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setManualBpm (double bpm) noexcept;

    BlockTiming beginBlock (const TransportState& state, int numSamples) noexcept;

private:
    double sampleRate = 44100.0;
    double manualBpm = 120.0;
    double nextPpq = 0.0;       // predicted position of the next block's first sample
    double lastBlockQn = 0.0;   // length of the previous block, at the tempo it was given
    bool wasSynced = false;
    bool wasPlaying = false;
    bool firstBlock = true;
};

} // namespace fxtend
