#pragma once

#include "devices/abstractdevice.h"
#include "geometry/mathtypes.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>

#include <future>
#include <optional>
#include <vector>

class QNetworkDatagram;
class QTimer;
class QUdpSocket;

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

signals:
  // Once per search: the joint angles of the first packet with Touch 1 of touch number touch.
  void touchDetected(int touch, const V6d& joints);

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
  void appendTrace(const QByteArray& row);
  void saveTrace();
  // Appends rows to fileName under the records folder after the previous save finished;
  // returns an empty string or the error.
  static QString writeTrace(std::shared_future<QString> previous, QByteArray rows, QString fileName);

  QUdpSocket* m_sock = nullptr;
  std::optional<SocketEndpoints> m_endpoints; // Without a peer port; never cleared after the first valid set.
  quint16 m_pp = 0; // Learned from the first accepted controller packet.

  QVector<QVector<V6d>> m_streams;
  QString m_diagnosticId; // Identifies the table in packet-capture file names.
  qsizetype m_activeStream = 0; // TouchIndex of the current run.
  qsizetype m_index = 0;
  // TouchIndex reported in the current search; 0 after a Start = 0 packet or a new table.
  qsizetype m_reportedTouch = 0;
  RunState m_state = RunState::Idle;
  QElapsedTimer m_traceClock;
  // Capture timing, m_traceClock ns; -1 until first recorded.
  qint64 m_sleepNs = -1; // ControlIO event loop last went to sleep.
  qint64 m_wakeNs = -1; // ControlIO event loop last woke up.
  qint64 m_handlerDoneNs = -1; // Previous replying onReadyRead finished.
  QTimer* m_traceTimer = nullptr;
  QByteArray m_trace;
  qsizetype m_traceDropped = 0;
  // Capture file of the current table, relative to the records folder; empty until its first save.
  QString m_traceFile;
  std::vector<std::shared_future<QString>> m_traceSaves; // In start order; each waits for the previous.
};
