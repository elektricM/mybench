#include "protocol.h"

#include <QtEndian>

#include <bit>
#include <cctype>
#include <cstring>

namespace scopehal_bridge {

const std::vector<uint64_t>& sampleRates()
{
    static const std::vector<uint64_t> rates = {200000, 100000, 50000, 20000, 10000, 5000, 2000,
                                                1000,   500,    200,   100,   50,    20,   10};
    return rates;
}

const std::vector<uint64_t>& sampleDepths()
{
    static const std::vector<uint64_t> depths = {1000,   2000,   5000,   10000,  20000,
                                                 50000,  100000, 200000, 500000, 1000000};
    return depths;
}

QByteArray ratesReply()
{
    QByteArray out;
    for (uint64_t rate : sampleRates()) {
        out += QByteArray::number(qint64(kFsPerSecond / int64_t(rate)));
        out += ',';
    }
    return out;
}

QByteArray depthsReply()
{
    QByteArray out;
    for (uint64_t depth : sampleDepths()) {
        out += QByteArray::number(quint64(depth));
        out += ',';
    }
    return out;
}

namespace {

uint64_t nearest(const std::vector<uint64_t>& list, uint64_t value)
{
    uint64_t best = list.front();
    for (uint64_t v : list) {
        const uint64_t d = v > value ? v - value : value - v;
        const uint64_t bestD = best > value ? best - value : value - best;
        if (d < bestD)
            best = v;
    }
    return best;
}

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

uint64_t nearestRate(uint64_t hz) { return nearest(sampleRates(), hz); }
uint64_t nearestDepth(uint64_t depth) { return nearest(sampleDepths(), depth); }

Command parseCommand(const QByteArray& raw)
{
    Command c;
    const QByteArray line = raw.trimmed();
    c.query = line.contains('?');

    qsizetype split = 0;
    while (split < line.size() && !isSpace(line[split]))
        ++split;
    QByteArray header = line.left(split).toUpper();
    header.replace("?", "");
    if (header.startsWith(':'))
        header.remove(0, 1);
    const qsizetype colon = header.indexOf(':');
    if (colon >= 0) {
        c.subject = header.left(colon);
        c.name = header.mid(colon + 1);
    } else {
        c.name = header;
    }

    // Arguments: separated by commas and/or white space.
    QByteArray token;
    const QByteArray rest = line.mid(split).toUpper();
    for (char ch : rest) {
        if (ch == ',' || isSpace(ch)) {
            if (!token.isEmpty())
                c.args << token;
            token.clear();
        } else if (ch != '?') {
            token += ch;
        }
    }
    if (!token.isEmpty())
        c.args << token;
    return c;
}

int channelIndex(const QByteArray& subject)
{
    if (subject.size() < 2 || subject[0] != 'C')
        return -1;
    bool ok = false;
    const int n = subject.mid(1).toInt(&ok);
    if (!ok || n < 1 || n > kChannelCount)
        return -1;
    return n - 1;
}

namespace {

template <typename T>
char* put(char* out, T value)
{
    const T le = qToLittleEndian(value);
    std::memcpy(out, &le, sizeof le);
    return out + sizeof le;
}

}  // namespace

QByteArray encodeFrame(int64_t fsPerSample, uint64_t depth, const std::vector<FrameChannel>& channels)
{
    constexpr size_t kHeader = sizeof(uint16_t) + sizeof(int64_t);
    constexpr size_t kChannelHeader = 2 * sizeof(uint64_t) + sizeof(float);
    const size_t size = kHeader + channels.size() * (kChannelHeader + size_t(depth) * sizeof(double));
    QByteArray frame(qsizetype(size), Qt::Uninitialized);
    char* out = frame.data();
    out = put<uint16_t>(out, uint16_t(channels.size()));
    out = put<int64_t>(out, fsPerSample);
    for (const FrameChannel& ch : channels) {
        out = put<uint64_t>(out, uint64_t(ch.index));
        out = put<uint64_t>(out, depth);
        out = put<uint32_t>(out, std::bit_cast<uint32_t>(ch.triggerPhaseFs));
        for (uint64_t i = 0; i < depth; ++i)
            out = put<uint64_t>(out, std::bit_cast<uint64_t>(ch.samples[i] * ch.scale));
    }
    return frame;
}

}  // namespace scopehal_bridge
