#include "bridge_server.h"

#include <QDateTime>
#include <QTcpSocket>

#include <cmath>
#include <cstdio>
#include <mutex>

using namespace Qt::StringLiterals;

namespace scopehal_bridge {

void logLine(const QString& message)
{
    static std::mutex mutex;
    const QByteArray text = (QTime::currentTime().toString(u"HH:mm:ss.zzz"_s) + u" "_s + message).toUtf8();
    std::lock_guard lock(mutex);
    std::fprintf(stderr, "mybench-scopehal-bridge %s\n", text.constData());
    std::fflush(stderr);
}

BridgeServer::BridgeServer(Acquisition::DeviceSource device, Identity identity, Acquisition::Options options,
                           bool verbose, QObject* parent)
    : QObject(parent), m_identity(std::move(identity)), m_verbose(verbose)
{
    auto sink = [this](uint64_t session, QByteArray frame) {
        QMetaObject::invokeMethod(
            this, [this, session, frame = std::move(frame)] { deliverFrame(session, frame); }, Qt::QueuedConnection);
    };
    m_acquisition = std::make_unique<Acquisition>(std::move(device), std::move(sink), &logLine, options);
    connect(&m_controlServer, &QTcpServer::newConnection, this, &BridgeServer::onControlConnection);
    connect(&m_dataServer, &QTcpServer::newConnection, this, &BridgeServer::onDataConnection);
}

BridgeServer::~BridgeServer()
{
    m_acquisition.reset();   // joins the worker (and releases the AI task) before the sockets go
}

bool BridgeServer::listen(const QHostAddress& address, quint16 controlPort, quint16 dataPort, QString* error)
{
    if (!m_controlServer.listen(address, controlPort)) {
        *error = u"control port %1: %2"_s.arg(controlPort).arg(m_controlServer.errorString());
        return false;
    }
    if (!m_dataServer.listen(address, dataPort)) {
        *error = u"data port %1: %2"_s.arg(dataPort).arg(m_dataServer.errorString());
        m_controlServer.close();
        return false;
    }
    return true;
}

void BridgeServer::log(const QString& message) const { logLine(message); }

void BridgeServer::trace(const QString& message) const
{
    if (m_verbose)
        logLine(message);
}

void BridgeServer::onControlConnection()
{
    while (QTcpSocket* socket = m_controlServer.nextPendingConnection()) {
        const QString peer = u"%1:%2"_s.arg(socket->peerAddress().toString()).arg(socket->peerPort());
        if (m_control) {
            log(u"refused %1: a client is already connected (one at a time)"_s.arg(peer));
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_control = socket;
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(socket, &QTcpSocket::readyRead, this, &BridgeServer::onControlReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            if (socket == m_control)
                endSession(u"client disconnected"_s);
        });
        m_rx.clear();
        m_session = m_acquisition->beginSession();
        log(u"client connected from %1"_s.arg(peer));
        onDataConnection();   // its data connection may already be waiting
    }
}

void BridgeServer::onDataConnection()
{
    if (!m_control)
        return;   // stays queued until its control connection has been accepted
    QTcpSocket* newest = nullptr;
    while (QTcpSocket* socket = m_dataServer.nextPendingConnection()) {
        if (newest) {
            newest->abort();
            newest->deleteLater();
        }
        newest = socket;
    }
    if (!newest)
        return;
    if (m_data && m_data->state() == QAbstractSocket::ConnectedState) {
        log(u"refused an extra data connection"_s);
        newest->abort();
        newest->deleteLater();
        return;
    }
    attachData(newest);
}

void BridgeServer::attachData(QTcpSocket* socket)
{
    if (m_data) {
        m_data->disconnect(this);
        m_data->abort();
        m_data->deleteLater();
    }
    m_data = socket;
    m_frameInFlight = false;
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::bytesWritten, this, &BridgeServer::onDataBytesWritten);
    connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });   // nothing expected
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (socket != m_data)
            return;
        log(u"data connection closed"_s);
        m_data->deleteLater();
        m_data = nullptr;
        m_frameInFlight = false;
        m_acquisition->setDataConnected(false);
    });
    m_acquisition->setDataConnected(true);
    trace(u"data connection from %1:%2"_s.arg(socket->peerAddress().toString()).arg(socket->peerPort()));
}

void BridgeServer::endSession(const QString& reason)
{
    m_acquisition->endSession();
    for (QTcpSocket** socket : {&m_data, &m_control}) {
        if (*socket) {
            (*socket)->disconnect(this);
            (*socket)->abort();
            (*socket)->deleteLater();
            *socket = nullptr;
        }
    }
    m_frameInFlight = false;
    m_rx.clear();
    log(reason + u"; acquisition stopped, waiting for a client"_s);
}

void BridgeServer::onControlReadyRead()
{
    if (!m_control)
        return;
    m_rx += m_control->readAll();
    for (;;) {
        qsizetype end = -1;
        for (qsizetype i = 0; i < m_rx.size(); ++i) {
            if (m_rx[i] == '\n' || m_rx[i] == ';') {
                end = i;
                break;
            }
        }
        if (end < 0)
            break;
        const QByteArray line = m_rx.left(end);
        m_rx.remove(0, end + 1);
        handleLine(line);
        if (!m_control)
            return;   // the session ended (EXIT)
    }
    if (m_rx.size() > 65536) {
        log(u"dropped an over-long control line"_s);
        m_rx.clear();
    }
}

void BridgeServer::handleLine(const QByteArray& line)
{
    if (line.trimmed().isEmpty())
        return;
    trace(u"<- %1"_s.arg(QString::fromUtf8(line.trimmed())));
    const Command c = parseCommand(line);
    if (c.query)
        handleQuery(c, line);   // exactly one reply line per query
    else
        handleCommand(c, line);   // never a reply: the client does not read one
}

void BridgeServer::reply(const QByteArray& text)
{
    if (!m_control)
        return;
    trace(u"-> %1"_s.arg(QString::fromUtf8(text)));
    m_control->write(text + '\n');
}

void BridgeServer::handleQuery(const Command& c, const QByteArray& line)
{
    if (!c.subject.isEmpty()) {
        log(u"unknown query \"%1\": replied with an empty line"_s.arg(QString::fromUtf8(line.trimmed())));
        reply({});
    } else if (c.name == "*IDN") {
        reply(QStringList{m_identity.vendor, m_identity.model, m_identity.serial, m_identity.version}.join(u',').toUtf8());
    } else if (c.name == "CHANS") {
        reply(QByteArray::number(kChannelCount));
    } else if (c.name == "RATES") {
        reply(ratesReply());
    } else if (c.name == "DEPTHS") {
        reply(depthsReply());
    } else if (c.name == "ARMED") {
        reply(m_acquisition->isArmed() ? "1" : "0");
    } else if (c.name == "RATE") {
        reply(QByteArray::number(quint64(m_acquisition->settings().rate)));
    } else if (c.name == "DEPTH") {
        reply(QByteArray::number(quint64(m_acquisition->settings().depth)));
    } else {
        log(u"unknown query \"%1\": replied with an empty line"_s.arg(QString::fromUtf8(line.trimmed())));
        reply({});
    }
}

void BridgeServer::handleCommand(const Command& c, const QByteArray& line)
{
    const QByteArray arg = c.args.value(0);
    const auto ignored = [&](const QString& why) {
        log(u"ignored \"%1\": %2"_s.arg(QString::fromUtf8(line.trimmed()), why));
    };
    const auto number = [&](double* out) {
        bool ok = false;
        *out = arg.toDouble(&ok);
        return ok && std::isfinite(*out);
    };
    double value = 0;

    if (c.subject.isEmpty()) {
        if (c.name == "START") {
            m_acquisition->start();
        } else if (c.name == "SINGLE") {
            m_acquisition->single();
        } else if (c.name == "FORCE") {
            m_acquisition->force();
        } else if (c.name == "STOP") {
            m_acquisition->stop();
        } else if (c.name == "RATE" || c.name == "DEPTH") {
            if (!number(&value) || value < 1)
                return ignored(u"expected a positive number"_s);
            const bool rate = c.name == "RATE";
            const uint64_t requested = uint64_t(std::llround(value));
            m_acquisition->configure([&](Settings& s) { (rate ? s.rate : s.depth) = requested; });
            const Settings s = m_acquisition->settings();
            const uint64_t used = rate ? s.rate : s.depth;
            if (used != requested)
                log(u"%1 %2 not offered, using %3"_s.arg(QString::fromLatin1(c.name)).arg(requested).arg(used));
        } else if (c.name == "EXIT") {
            endSession(u"client sent EXIT"_s);
        } else {
            ignored(u"unknown command"_s);
        }
        return;
    }

    if (c.subject == "TRIG") {
        if (c.name == "DELAY") {
            bool ok = false;
            qint64 delay = arg.toLongLong(&ok);
            if (!ok && number(&value)) {
                delay = qint64(value);
                ok = true;
            }
            if (!ok)
                return ignored(u"expected a delay in fs"_s);
            m_acquisition->configure([&](Settings& s) { s.triggerDelayFs = delay; });
        } else if (c.name == "SOU") {
            const int channel = channelIndex(arg);
            if (channel < 0)
                return ignored(u"the trigger source must be C1 or C2"_s);
            m_acquisition->configure([&](Settings& s) { s.triggerChannel = channel; });
        } else if (c.name == "LEV") {
            if (!number(&value))
                return ignored(u"expected a level in V"_s);
            m_acquisition->configure([&](Settings& s) { s.triggerLevel = value; });
        } else if (c.name == "EDGE:DIR") {
            EdgeSlope slope;
            if (arg == "RISING")
                slope = EdgeSlope::Rising;
            else if (arg == "FALLING")
                slope = EdgeSlope::Falling;
            else if (arg == "ANY")
                slope = EdgeSlope::Any;
            else
                return ignored(u"expected RISING, FALLING or ANY"_s);
            m_acquisition->configure([&](Settings& s) { s.slope = slope; });
        } else if (c.name == "MODE") {
            if (arg != "EDGE")
                ignored(u"only edge triggers are supported"_s);
        } else {
            ignored(u"unknown trigger command"_s);
        }
        return;
    }

    const int channel = channelIndex(c.subject);
    if (channel < 0)
        return ignored(u"unknown command or channel"_s);
    const size_t ch = size_t(channel);
    if (c.name == "ON" || c.name == "OFF") {
        const bool on = c.name == "ON";
        m_acquisition->configure([&](Settings& s) { s.channels[ch].enabled = on; });
    } else if (c.name == "COUP") {
        if (arg != "DC1M")
            ignored(u"the myDAQ inputs are DC coupled only"_s);
    } else if (c.name == "RANGE") {
        if (!number(&value) || value <= 0)
            return ignored(u"expected a positive span in V"_s);
        m_acquisition->configure([&](Settings& s) { s.channels[ch].span = value; });
    } else if (c.name == "OFFS") {
        if (!number(&value))
            return ignored(u"expected an offset in V"_s);
        m_acquisition->configure([&](Settings& s) { s.channels[ch].center = value; });
    } else if (c.name == "ATTEN") {
        if (!number(&value) || value <= 0)
            return ignored(u"expected a positive attenuation"_s);
        m_acquisition->configure([&](Settings& s) { s.channels[ch].attenuation = value; });
    } else {
        ignored(u"unknown channel command"_s);
    }
}

void BridgeServer::deliverFrame(uint64_t session, const QByteArray& frame)
{
    if (session != m_session || !m_data || m_data->state() != QAbstractSocket::ConnectedState) {
        m_acquisition->frameDelivered(session);   // dropped: let a normal-mode run go on
        return;
    }
    m_data->write(frame);
    m_frameInFlight = true;
}

void BridgeServer::onDataBytesWritten()
{
    if (m_frameInFlight && m_data && m_data->bytesToWrite() == 0) {
        m_frameInFlight = false;
        m_acquisition->frameDelivered(m_session);
    }
}

}  // namespace scopehal_bridge
