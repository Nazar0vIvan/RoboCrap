#include "devices/plc/plcdevice.h"
#include "common/socketconfigutils.h"
#include "devices/recordclock.h"

#include <winsock2.h>

#include <QDebug>
#include <QTcpSocket>
#include <QTimer>

namespace {

// One request is in flight at a time; its reply must arrive within this time.
constexpr int ReplyTimeoutMs = 1000;
// SNAPSHOT period while idle: keeps the PLC socket below its 10 s Keep Alive Timer.
constexpr int HeartbeatMs = 2000;

// KUKA Automatic External outputs wired to PLC module 1 inputs X1.n.
constexpr int X1ProAct = 4;
constexpr int X1PgnoReq = 5;
constexpr int X1ApplRun = 6;

bool matchesResponseField(const QVariantMap& data, const char* key, uint expected)
{
  return data.contains(key) && data.value(key).toUInt() == expected;
}

// Bit n set for a true element n of a published IO list (x1, y1, y2).
quint8 bitMask(const QVariant& bits)
{
  const QVariantList list = bits.toList();
  quint8 mask = 0;
  for (int bit = 0; bit < 8; ++bit)
    if (list.value(bit).toBool()) mask |= static_cast<quint8>(1u << bit);
  return mask;
}

} // namespace

PlcDevice::PlcDevice(const QString& name, QObject* parent) : AbstractDevice(name, parent) {}

void PlcDevice::startDevice()
{
  Q_ASSERT(!m_sock);

  m_sock = new QTcpSocket(this);

  m_replyTimer = new QTimer(this);
  m_replyTimer->setSingleShot(true);
  m_replyTimer->setInterval(ReplyTimeoutMs);
  m_heartbeat = new QTimer(this);
  m_heartbeat->setInterval(HeartbeatMs);

  attachSocket(m_sock);

  QObject::connect(m_sock, &QTcpSocket::connected, this, &PlcDevice::onConnected);
  QObject::connect(m_sock, &QTcpSocket::readyRead, this, &PlcDevice::onReadyRead);
  QObject::connect(m_sock, &QTcpSocket::stateChanged, this, &PlcDevice::onStateChanged);
  QObject::connect(m_replyTimer, &QTimer::timeout, this, &PlcDevice::onReplyTimeout);
  QObject::connect(m_heartbeat, &QTimer::timeout, this, &PlcDevice::onHeartbeat);
}

void PlcDevice::stopDevice()
{
  if (!m_sock) return;

  disconnectDevice();

  delete m_sock;
  m_sock = nullptr;

  delete m_replyTimer;
  m_replyTimer = nullptr;
  delete m_heartbeat;
  m_heartbeat = nullptr;
}

// Reached only through AbstractDevice::invoke, which refuses calls while the device is stopped.
void PlcDevice::connectDevice(const QVariantMap& config)
{
  if (!config.isEmpty()) {
    const auto parsed = parseSocketEndpoints(config);
    if (!parsed || !parsed->peerPort) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Invalid socket configuration";
      return;
    }
    m_endpoints = parsed;
  }

  if (!m_endpoints) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Socket configuration is incomplete";
    return;
  }
  const SocketEndpoints& endpoints = *m_endpoints;

  if (m_sock->state() != QAbstractSocket::UnconnectedState) disconnectDevice();

  emit stateReady({{"connectionConfig", connectionConfigMap(endpoints)}});

  // The PLC program expects this PC endpoint, including its source port.
  if (!m_sock->bind(endpoints.localAddress, endpoints.localPort)) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << QString("Bind failed: %1").arg(m_sock->errorString());
    m_sock->abort();
    return;
  }

  m_sock->connectToHost(endpoints.peerAddress, *endpoints.peerPort, QIODevice::ReadWrite);
}

// Callers run while the device is started (stopDevice, connectDevice, invoke, sendNext), so m_sock exists.
void PlcDevice::disconnectDevice()
{
  m_rx.clear();
  m_pend.clear();

  // A FIN close keeps the fixed-port connection in TIME_WAIT and the next connect fails with
  // WSAEADDRINUSE; SO_LINGER {1, 0} makes closesocket send RST, which leaves no TIME_WAIT.
  if (const qintptr fd = m_sock->socketDescriptor(); fd != -1) {
    const linger reset{1, 0};
    if (::setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_LINGER,
                     reinterpret_cast<const char*>(&reset), sizeof(reset)) != 0) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":"
          << QString("SO_LINGER failed: WSA error %1").arg(::WSAGetLastError());
    }
  }

  m_sock->abort();
}

void PlcDevice::setOutput(const QVariantMap& args)
{
  if (m_sock->state() != QAbstractSocket::ConnectedState) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "PLC is not connected";
    return;
  }

  const PlcMessageManager::ParseResult request = m_mgr.outputRequest(args);
  if (!request.ok()) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "WRITE ERROR: " + QString::number(request.error);
    return;
  }

  writeMessage(request.data.toMap());
}

void PlcDevice::writeMessage(const QVariantMap& msg)
{
  // A request made without a connection is dropped, never sent at a later connect.
  // Callers are invoke-only methods and the heartbeat, which exists only while started.
  if (m_sock->state() != QAbstractSocket::ConnectedState) {
    publishProgramReply(msg, false);
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "PLC is not connected";
    return;
  }

  m_queue.enqueue(msg);
  sendNext();
}

void PlcDevice::runRobotProgram(const QVariantMap& args)
{
  const PlcMessageManager::ParseResult request = m_mgr.robotProgramRequest(args);
  if (!request.ok()) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "WRITE ERROR: " + QString::number(request.error);
    return;
  }

  recordIo(QString("run %1").arg(request.data.toMap().value("attr").toUInt()));
  writeMessage(request.data.toMap());
}

void PlcDevice::resumeRobot()
{
  recordIo(QStringLiteral("resume"));
  writeMessage({
    {"cmd", PlcMessageManager::SET_VAR}, {"var", PlcMessageManager::ROBOT_RESUME}, {"attr", 0}
  });
}

void PlcDevice::sendNext()
{
  // The PLC's receive buffer holds one frame, so the next request waits for the pending reply.
  while (m_pend.isEmpty() && !m_queue.isEmpty()) {
    const QVariantMap msg = m_queue.dequeue();
    const auto tid = static_cast<quint8>(m_nextTid + 1);
    const PlcMessageManager::ParseResult built = m_mgr.buildReq(msg, tid);

    if (!built.ok()) {
      publishProgramReply(msg, false);
      qInfo().noquote() << "[ERROR]" << objectName() + ":" << "WRITE ERROR: " + QString::number(built.error);
      continue;
    }

    m_nextTid = tid;
    const QByteArray data = built.data.toByteArray();
    const qint64 count = m_sock->write(swapBytes(data));
    if (count != data.size()) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":"
          << QString("Incomplete PLC write: %1 of %2 bytes queued").arg(count).arg(data.size());
      // A partially queued frame cannot safely be followed by another request. The RST close
      // clears the pending replies, and onStateChanged clears the queue, which ends this loop.
      publishProgramReply(msg, count > 0 ? QVariant{} : QVariant(false));
      if (count > 0) disconnectDevice();
      continue;
    }

    // SNAPSHOT is the 2 s heartbeat: logging it would flood the console.
    if (msg.value("cmd").toUInt() != PlcMessageManager::SNAPSHOT) {
      qInfo().noquote() << "[WRITE]" << objectName() + ":"
          << QString("TX: %1 (%2 bytes)").arg(QString(data.toHex(' ').toUpper())).arg(count);
    }
    m_pend.insert(tid, msg);
    m_replyTimer->start();
  }
}

void PlcDevice::onReplyTimeout()
{
  qInfo().noquote() << "[ERROR]" << objectName() + ":"
      << QString("PLC reply timeout for transaction %1").arg(static_cast<uint>(m_nextTid));
  recordIo(QString("reply timeout %1").arg(static_cast<uint>(m_nextTid)));
  const auto request = m_pend.value(m_nextTid);
  if (request.value("cmd").toUInt() == PlcMessageManager::SNAPSHOT) emit stateReady({{"plcIoCurrent", false}});
  publishProgramReply(request, {});
  m_pend.clear();
  sendNext();
}

void PlcDevice::onHeartbeat()
{
  if (m_queue.isEmpty() && m_pend.isEmpty()) writeMessage({{"cmd", PlcMessageManager::SNAPSHOT}});
}

void PlcDevice::onConnected()
{
  qInfo().noquote() << "[OK]" << objectName() + ":" << "Connection has been successfully established";

  // No request right at connect: the PLC never answered it (a timeout at every connect, 2026-11-13).
  // Its IO change frame on a new connection gives the state; the heartbeat follows in 2 s.
  m_heartbeat->start();
}

void PlcDevice::recordIo(const QString& event)
{
  emit ioRecorded(PlcIoRecord{recordClockNs(), event, bitMask(m_logged.value("x1")), bitMask(m_logged.value("y1")),
                              bitMask(m_logged.value("y2")), m_logged.value("robotStep").toInt(),
                              m_logged.value("robotProgram").toInt(), m_logged.value("robotResult").toInt()});
}

// An invalid value denotes a transmitted command without a matched reply.
void PlcDevice::publishProgramReply(const QVariantMap& request, const QVariant& accepted)
{
  if (request.value("cmd").toUInt() != PlcMessageManager::SET_VAR) return;
  const uint var = request.value("var").toUInt();
  if (var == PlcMessageManager::RUN_PROG && request.value("attr").toInt() == 2)
    emit stateReady({{"scanStartReply", accepted}});
  else if (var == PlcMessageManager::ROBOT_RESUME)
    emit stateReady({{"resumeReply", accepted}});
}

bool PlcDevice::matchResponse(const QVariantMap& data)
{
  const auto tid = static_cast<quint8>(data.value("tid").toUInt());
  const auto pending = m_pend.constFind(tid);
  if (pending == m_pend.cend()) return false;
  const QVariantMap& request = *pending;
  const uint cmd = request.value("cmd").toUInt();
  if (data.value("cmd").toUInt() != cmd) return false;

  // Error replies contain only command/error/code, so no endpoint fields can be matched.
  if (data.value("type").toUInt() == PlcMessageManager::RESP_OK) {
    switch (cmd) {
      case PlcMessageManager::WRITE_IO:
        if (!matchesResponseField(data, "dev", PlcMessageManager::Y)
            || !matchesResponseField(data, "module", request.value("module").toUInt())) return false;
        break;
      case PlcMessageManager::SET_VAR:
        if (!matchesResponseField(data, "var", request.value("var").toUInt())
            || !matchesResponseField(data, "attr", request.value("attr").toUInt())) return false;
        break;
      default: break;
    }
  }
  const bool accepted = data.value("type").toUInt() == PlcMessageManager::RESP_OK;
  if (cmd == PlcMessageManager::SNAPSHOT && !accepted) emit stateReady({{"plcIoCurrent", false}});
  publishProgramReply(request, accepted);
  m_pend.remove(tid);
  m_replyTimer->stop();
  return true;
}

void PlcDevice::onReadyRead()
{
  processIncoming(m_sock->readAll());
}

void PlcDevice::processIncoming(const QByteArray& bytes)
{
  m_rx.append(bytes);

  while (m_rx.size() >= PlcMessageManager::RESP_SIZE) {
    const QByteArray frame = swapBytes(m_rx.left(PlcMessageManager::RESP_SIZE));

    m_rx.remove(0, PlcMessageManager::RESP_SIZE);

    const PlcMessageManager::ParseResult parsed = m_mgr.parseMessage(frame);

    if (!parsed.ok()) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":" << "READ ERROR: " + QString::number(parsed.error);
      recordIo(QString("read error %1 frame %2")
                   .arg(static_cast<int>(parsed.error)).arg(QString(frame.toHex(' ').toUpper())));
      continue;
    }

    const QVariantMap data = parsed.data.toMap();

    if (data.contains("tid")) {
      if (!matchResponse(data)) {
        qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Unmatched PLC response for transaction " + data.value("tid").toString();
        recordIo("unmatched reply " + data.value("tid").toString());
        continue;
      }
    }

    if (data.value("type").toUInt() == PlcMessageManager::RESP_ERR) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":"
          << QString("PLC response error: cmd=%1, err=%2, code=%3")
                 .arg(data.value("cmd").toUInt())
                 .arg(data.value("err").toUInt())
                 .arg(data.value("code").toUInt());
      recordIo(QString("error cmd=%1 code=%2").arg(data.value("cmd").toUInt()).arg(data.value("code").toUInt()));

      continue;
    }

    publishData(data);
  }

  sendNext();
}

void PlcDevice::onStateChanged(QAbstractSocket::SocketState state)
{
  if (state != QAbstractSocket::UnconnectedState) return;

  m_rx.clear();
  m_queue.clear();
  m_pend.clear();
  m_replyTimer->stop();
  m_heartbeat->stop();
  m_programState = ProgramState::Unknown;
  m_logged.clear();

  // Clear the persistent PLC state seen by QML.
  emit stateReady({
    {"plcIoCurrent", false},
    {"x1", QVariantList(8, false)},
    {"y1", QVariantList(8, false)},
    {"x2", QVariantList(8, false)},
    {"y2", QVariantList(8, false)},
    {"robotStep", 0},
    {"robotProgram", 0},
    {"robotResult", 0},
    {"robotProgramState", static_cast<int>(ProgramState::Unknown)}
  });
}

void PlcDevice::publishData(const QVariantMap& data)
{
  const quint8 type = static_cast<quint8>(data.value("type").toUInt());

  if (type == PlcMessageManager::RESP_OK) {
    const quint8 cmd = static_cast<quint8>(data.value("cmd").toUInt());

    if (cmd == PlcMessageManager::SNAPSHOT) {
      publishIo(data);
      return;
    }

    if (cmd == PlcMessageManager::WRITE_IO) {
      const int mod = data.value("module").toInt();

      if (mod == 1) {
        emit stateReady({{"y1", data.value("state")}});
      } else if (mod == 2) {
        emit stateReady({{"y2", data.value("state")}
        });
      }
      return;
    }
    return;
  }

  if (type == PlcMessageManager::CHG) {
    const quint8 chg = static_cast<quint8>(data.value("chg").toUInt());
    if (chg == PlcMessageManager::IOs) publishIo(data);
  }
}

void PlcDevice::publishIo(const QVariantMap& data)
{
  // The IO change frame and the SNAPSHOT reply carry the same IO bytes and start-sequence state.
  QVariantHash values{{"plcIoCurrent", true}};
  for (const char* key : {"x1", "y1", "x2", "y2", "robotStep", "robotProgram", "robotResult"})
    values.insert(key, data.value(key));

  // The heartbeat SNAPSHOT repeats unchanged values every 2 s; the records keep only changes.
  bool changed = false;
  for (const char* key : {"x1", "y1", "y2", "robotStep", "robotProgram", "robotResult"}) {
    if (values.value(key) == m_logged.value(key)) continue;
    m_logged.insert(key, values.value(key));
    changed = true;
  }
  if (changed) recordIo(QStringLiteral("io"));

  const QVariantList x1 = data.value("x1").toList();
  const ProgramState next = nextProgramState(m_programState, x1);
  if (next != m_programState) {
    m_programState = next;
    values.insert("robotProgramState", static_cast<int>(next));
  }

  emit stateReady(values);
}

PlcDevice::ProgramState PlcDevice::nextProgramState(ProgramState current, const QVariantList& x1)
{
  // P00 sets APPL_RUN at the program-number acknowledgement and clears it only when CELL asks for
  // the next number, so APPL_RUN without $PRO_ACT is a stopped (interrupted) application program.
  const bool proAct = x1.value(X1ProAct).toBool();
  const bool pgnoReq = x1.value(X1PgnoReq).toBool();
  const bool applRun = x1.value(X1ApplRun).toBool();

  if (applRun) return proAct ? ProgramState::Running : ProgramState::Interrupted;

  const bool started = current == ProgramState::Running || current == ProgramState::Interrupted;
  if (started && proAct && pgnoReq) return ProgramState::Finished;
  // KSS Fig. 6-25 ... 6-28 draw APPL_RUN falling on a stop; this covers that behaviour too.
  if (current == ProgramState::Running && !proAct) return ProgramState::Interrupted;
  return current;
}

QByteArray PlcDevice::swapBytes(const QByteArray& data)
{
  QByteArray out = data;

  for (int i = 0; i + 1 < out.size(); i += 2)
    qSwap(out[i], out[i + 1]);

  return out;
}
