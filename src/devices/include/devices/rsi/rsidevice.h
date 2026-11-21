#pragma once

#include "devices/abstractdevice.h"
#include "geometry/mathtypes.h"

#include <QByteArray>
#include <QVector>

#include <optional>

class QNetworkDatagram;
class QTimer;
class QUdpSocket;

// One accepted controller request of a capture, stamped with recordClockNs() (devices/recordclock.h).
struct RsiCaptureRecord
{
  qint64 receivedNs = 0; // Dequeued from the socket.
  qsizetype xmlBegin = 0; // Request bytes in RsiCapture::requestXml.
  qsizetype xmlSize = 0;
  // Only the newest request of one read is answered; replyNs is -1 for the others.
  qint64 entryNs = -1; // Receive handler entry.
  qint64 sleepNs = -1; // ControlIO event loop last went to sleep before the handler.
  qint64 wakeNs = -1; // ControlIO event loop last woke up before the handler.
  qint64 previousDoneNs = -1; // Previous replying handler finished.
  qint64 replyNs = -1; // writeDatagram called.
  qint64 writeNs = -1; // writeDatagram duration.
  qint64 written = -1; // writeDatagram result.
  qsizetype streamIndex = -1; // Index of the correction sent in the reply.
  V6d correction = V6d::Zero();
  bool stop = false;
  qint64 result = 0;
};

// Requests stored during the exchange with one stream table, handed over without file work.
struct RsiCapture
{
  QString diagnosticId; // The table's Sampling identifier.
  QVector<RsiCaptureRecord> records;
  QByteArray requestXml; // Accepted request datagrams, concatenated as received.
  qsizetype dropped = 0; // Requests not stored because the reserved memory was full.
  bool completed = false; // The stream reached Stopping (Result 1 echoed).
  bool requested = false; // Emitted by flushCapture.
};

// KUKA RSI UDP partner: answers every controller cycle and, while the controller reports
// Start = 1, plays the correction stream selected by the request's TouchIndex.
class RsiDevice : public AbstractDevice
{
  Q_OBJECT

public:
  // Fields of a controller <Rob> packet; other elements are ignored.
  struct Request
  {
    quint64 ipoc = 0;
    bool start = false;
    qint64 result = 0;
    qsizetype touchIndex = 0; // Stream index; 0 when the context leaves TouchIndex unconnected.
    bool touch = false; // Touch 1: the probe input $IN[8] of RSI_Probe_Calibration is on.
    std::optional<V6d> joints; // AIPos A1-A6, deg; empty when missing or not numeric.
  };

  // One PC reply. Stop ends RSI_MOVECORR; Result = 1 reports normal completion.
  struct Reply
  {
    quint64 ipoc = 0;
    V6d correction = V6d::Zero(); // Relative X/Y/Z mm, A/B/C deg.
    bool stop = false;
    qint64 result = 0;
  };

  explicit RsiDevice(const QString& name, QObject* parent = nullptr);

  Q_INVOKABLE void connectDevice(const QVariantMap& config);
  Q_INVOKABLE void disconnectDevice();

public slots:
  // Discard buffered requests and reset run/peer state; install the table in Idle.
  // Stream i plays for TouchIndex i; RSI_Chamfer_Hole and RSI_Probe_Check use stream 0.
  void setCorrections(const QVector<QVector<V6d>>& streams, const QString& diagnosticId);
  // Emits the stored requests now (requested = true), also when there are none.
  void flushCapture();

signals:
  // Once per search: the joint angles of the first packet with Touch 1 of touch number touch.
  void touchDetected(int touch, const V6d& joints);
  // Stored requests: after 1 s without a request, on flushCapture, before a new table, on disconnect and stop.
  void captureReady(const RsiCapture& capture);

protected:
  void startDevice() override;
  void stopDevice() override;

private:
  enum class RunState { Idle, Streaming, Finished, Stopping };

  void onReadyRead();
  void onDispatcherAboutToBlock();
  void onDispatcherAwake();
  bool acceptsSender(const QNetworkDatagram& datagram) const;
  // State-machine step for the newest accepted request.
  Reply advance(const Request& request);
  bool reportsTouch(const Request& request) const; // A detection not yet reported for its touch.
  qsizetype activeStreamSize() const; // 0 when the active index has no stream.
  void resetRun();
  void setState(RunState state);
  void publishState();
  // Copies an accepted request into the reserved capture memory while a table is installed;
  // returns its record index, or -1 when it was not stored.
  qsizetype storeRequest(const QByteArray& xml, qint64 receivedNs);
  // Emits a copy of the stored requests and clears them, keeping the reserved capacity; without
  // requested, only when requests are stored.
  void emitCapture(bool requested);
  void onCaptureTimer();

  QUdpSocket* m_sock = nullptr;
  std::optional<SocketEndpoints> m_endpoints; // Without a peer port; never cleared after the first valid set.
  quint16 m_pp = 0; // Learned from the first accepted controller packet.

  QVector<QVector<V6d>> m_streams;
  qsizetype m_activeStream = 0; // TouchIndex of the current run.
  qsizetype m_index = 0;
  // TouchIndex reported in the current search; 0 after a Start = 0 packet or a new table.
  qsizetype m_reportedTouch = 0;
  RunState m_state = RunState::Idle;
  // Capture timing, recordClockNs(); -1 until first recorded.
  qint64 m_sleepNs = -1; // ControlIO event loop last went to sleep.
  qint64 m_wakeNs = -1; // ControlIO event loop last woke up.
  qint64 m_handlerDoneNs = -1; // Previous replying onReadyRead finished.
  QTimer* m_captureTimer = nullptr; // Silence check while requests are stored.
  // Requests of the installed table since the last emission; its diagnosticId identifies the table.
  RsiCapture m_capture;
};
