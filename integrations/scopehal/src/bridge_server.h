// TCP side of the bridge: the control socket (text commands, one reply line per query) and the
// data socket (waveform frames) of the twinlan transport. One client at a time; a client that
// goes away stops the acquisition and releases the AI task.
#pragma once

#include "acquisition.h"

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

#include <memory>

class QTcpSocket;

namespace scopehal_bridge {

struct Identity {
    QString vendor;    // *IDN? fields; none may contain ',' or ';', the version no white space
    QString model;
    QString serial;
    QString version;
};

class BridgeServer : public QObject {
    Q_OBJECT
public:
    BridgeServer(Acquisition::DeviceSource device, Identity identity, Acquisition::Options options,
                 bool verbose, QObject* parent = nullptr);
    ~BridgeServer() override;

    bool listen(const QHostAddress& address, quint16 controlPort, quint16 dataPort, QString* error);
    quint16 controlPort() const { return m_controlServer.serverPort(); }
    quint16 dataPort() const { return m_dataServer.serverPort(); }

private:
    void onControlConnection();
    void onDataConnection();
    void attachData(QTcpSocket* socket);
    void endSession(const QString& reason);
    void onControlReadyRead();
    void handleLine(const QByteArray& line);
    void handleQuery(const Command& c, const QByteArray& line);
    void handleCommand(const Command& c, const QByteArray& line);
    void reply(const QByteArray& text);
    void deliverFrame(uint64_t session, const QByteArray& frame);
    void onDataBytesWritten();
    void log(const QString& message) const;
    void trace(const QString& message) const;

    Identity m_identity;
    bool m_verbose = false;
    QTcpServer m_controlServer;
    QTcpServer m_dataServer;
    QTcpSocket* m_control = nullptr;
    QTcpSocket* m_data = nullptr;
    QByteArray m_rx;
    uint64_t m_session = 0;
    bool m_frameInFlight = false;   // a frame is still in m_data's write buffer
    std::unique_ptr<Acquisition> m_acquisition;
};

// stderr logging shared by the bridge ("mybench-scopehal-bridge: <message>").
void logLine(const QString& message);

}  // namespace scopehal_bridge
