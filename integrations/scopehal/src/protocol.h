// Wire protocol of scopehal's "digilent" driver (RemoteBridgeOscilloscope over the "twinlan"
// transport), as the bridge speaks it: text commands on the control socket, binary waveform
// frames on the data socket. See README.md for the command list and the frame layout.
#pragma once

#include <QByteArray>
#include <QList>

#include <cstdint>
#include <vector>

namespace scopehal_bridge {

inline constexpr int kChannelCount = 2;                         // C1 = AI0, C2 = AI1
inline constexpr int64_t kFsPerSecond = 1'000'000'000'000'000;  // scopehal time unit: femtoseconds

// Sample rates offered (S/s per channel), fastest first: 1-2-5 steps from the myDAQ maximum
// (200 kS/s with both channels on) down to 10 S/s. Each divides the 100 MHz sample clock and
// 1e15, so the device runs exactly at that rate and the period in fs is an integer.
// The client starts at the first entry.
const std::vector<uint64_t>& sampleRates();
// Record lengths (samples per channel), smallest first; the client starts at the first entry.
const std::vector<uint64_t>& sampleDepths();

// Replies to RATES? (sample periods in fs) and DEPTHS?. Every entry is followed by a comma: the
// client only takes entries that end with one.
QByteArray ratesReply();
QByteArray depthsReply();

uint64_t nearestRate(uint64_t hz);       // closest offered rate
uint64_t nearestDepth(uint64_t depth);   // closest offered depth

// One control line, e.g. ":C1:RANGE 5.000000" -> subject "C1", name "RANGE", args {"5.000000"};
// "TRIG:EDGE:DIR RISING" -> "TRIG", "EDGE:DIR", {"RISING"}; "*IDN?" -> "", "*IDN", query.
// Subject, name and arguments are upper-cased; a leading ':' is ignored.
struct Command {
    QByteArray subject;
    QByteArray name;
    QList<QByteArray> args;
    bool query = false;   // the line contains '?': exactly one reply line is expected
};
Command parseCommand(const QByteArray& line);

// "C1" -> 0, "C2" -> 1; -1 for anything else.
int channelIndex(const QByteArray& subject);

// One channel of a waveform frame.
struct FrameChannel {
    int index = 0;               // 0 = C1
    float triggerPhaseFs = 0;    // time of the first sample relative to the frame's time origin
    const double* samples = nullptr;
    double scale = 1.0;          // probe attenuation: the client shows values at the probe tip
};

// Data-socket frame, little endian:
//   uint16 channel count, int64 fs per sample,
//   per channel: uint64 channel index, uint64 depth, float trigger phase (fs), double[depth] volts.
// Every channel carries `depth` samples (the client reads one depth for all channels).
QByteArray encodeFrame(int64_t fsPerSample, uint64_t depth, const std::vector<FrameChannel>& channels);

}  // namespace scopehal_bridge
