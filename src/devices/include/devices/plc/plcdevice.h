#pragma once

#include "devices/abstractdevice.h"
#include "devices/plc/plcmessagemanager.h"

#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QVariantList>

class QTcpSocket;
class QTimer;

// One change of the robot interface or one PLC event, stamped with recordClockNs() (devices/recordclock.h).
struct PlcIoRecord
{
  qint64 timeNs = 0;
  QString event; // "io", "run <n>", "resume", "abort", "reply timeout <tid>", ...
  quint8 x1 = 0; // Bit n = terminal X1.n.
  quint8 y1 = 0; // Bit n = terminal Y1.n.
  quint8 y2 = 0; // Bit n = terminal Y2.n.
  int step = 0; // PLC E_RSTEP code.
  int program = 0;
  int result = 0; // PLC E_RRESULT code.
};

class PlcDevice : public AbstractDevice
{
  Q_OBJECT

public:
  explicit PlcDevice(const QString& name, QObject* parent = nullptr);
  ~PlcDevice() override = default;

  Q_INVOKABLE void connectDevice(const QVariantMap& config);
  Q_INVOKABLE void disconnectDevice();
  Q_INVOKABLE void setOutput(const QVariantMap& args);
  Q_INVOKABLE void runRobotProgram(const QVariantMap& args);
  Q_INVOKABLE void resumeRobot();

signals:
  // Every logged change and event, for the session records; no file is written here.
  void ioRecorded(const PlcIoRecord& record);

protected:
  void startDevice() override;
  void stopDevice() override;

private slots:
  void onConnected();
  void onReadyRead();
  void onStateChanged(QAbstractSocket::SocketState state);
  void onReplyTimeout();
  void onHeartbeat();

private:
  // KUKA application program state derived from the X1 status bits, published as robotProgramState.
  enum class ProgramState : quint8 { Unknown = 0, Running = 1, Finished = 2, Interrupted = 3 };

  static QByteArray swapBytes(const QByteArray& data);
  static ProgramState nextProgramState(ProgramState current, const QVariantList& x1);
  // Queues one request map (WRITE_IO, SET_VAR or SNAPSHOT); not part of the invoke API.
  void writeMessage(const QVariantMap& msg);
  void sendNext();
  void publishProgramReply(const QVariantMap& request, const QVariant& accepted);
  bool matchResponse(const QVariantMap& data);
  void processIncoming(const QByteArray& bytes);

  void publishData(const QVariantMap& data);
  void publishIo(const QVariantMap& data);
  // Emits ioRecorded with the full logged state and event.
  void recordIo(const QString& event);

  QTcpSocket* m_sock = nullptr;
  PlcMessageManager m_mgr;
  QTimer* m_replyTimer = nullptr;
  QTimer* m_heartbeat = nullptr;
  // Last logged x1/y1/y2 bits and robot step/program/result: every record shows the full state.
  QVariantMap m_logged;

  std::optional<SocketEndpoints> m_endpoints; // With a peer port; never cleared after the first valid set.

  QByteArray m_rx;
  QQueue<QVariantMap> m_queue;
  QHash<quint8, QVariantMap> m_pend;
  quint8 m_nextTid = 1;
  ProgramState m_programState = ProgramState::Unknown;
};
