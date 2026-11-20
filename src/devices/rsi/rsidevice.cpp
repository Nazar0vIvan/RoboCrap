#include "devices/rsi/rsidevice.h"
#include "../common/socketconfigutils.h"

#include <QAbstractEventDispatcher>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace {

constexpr qsizetype TraceCapacity = 64 * 1024 * 1024;
constexpr int CaptureIdleMs = 250; // Save the capture after this long without accepted-peer traffic.

bool isDigits(const QString& text)
{
  if (text.isEmpty()) return false;
  for (const QChar character : text)
    if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
  return true;
}

// Attributes A1-A6 of <AIPos>, deg; empty when one is missing or not a finite number.
std::optional<V6d> decodeAxes(const QXmlStreamAttributes& attributes)
{
  V6d joints;
  for (int axis = 0; axis < 6; ++axis) {
    bool ok = false;
    joints[axis] = attributes.value(QStringLiteral("A%1").arg(axis + 1)).toDouble(&ok);
    if (!ok || !std::isfinite(joints[axis])) return std::nullopt;
  }
  return joints;
}

// Requires exactly one IPOC, Start (0/1) and Result (LONG) in a well-formed <Rob> document.
// An optional single TouchIndex is a decimal index or, from an unconnected input, "not linked:<n>" (0).
// Touch is on only for "1", so contexts without the probe input stay valid; AIPos gives the angles.
std::optional<RsiDevice::Request> decodeRequest(const QByteArray& datagram)
{
  QXmlStreamReader xml(datagram);
  if (!xml.readNextStartElement() || xml.name() != QLatin1String("Rob")) return std::nullopt;
  RsiDevice::Request request;
  bool hasIpoc = false;
  bool hasStart = false;
  bool hasResult = false;
  bool hasTouchIndex = false;
  while (xml.readNextStartElement()) {
    if (xml.name() == QLatin1String("IPOC")) {
      const QString text = xml.readElementText().trimmed();
      bool ok = false;
      request.ipoc = text.toULongLong(&ok);
      if (hasIpoc || !isDigits(text) || !ok) return std::nullopt;
      hasIpoc = true;
    } else if (xml.name() == QLatin1String("Start")) {
      const QString text = xml.readElementText().trimmed();
      if (hasStart || (text != QLatin1String("0") && text != QLatin1String("1"))) return std::nullopt;
      request.start = text == QLatin1String("1");
      hasStart = true;
    } else if (xml.name() == QLatin1String("Result")) {
      bool ok = false;
      request.result = xml.readElementText().trimmed().toLongLong(&ok);
      if (hasResult || !ok) return std::nullopt;
      hasResult = true;
    } else if (xml.name() == QLatin1String("TouchIndex")) {
      const QString text = xml.readElementText().trimmed();
      bool ok = text.startsWith(QLatin1String("not linked:"));
      if (!ok && isDigits(text)) request.touchIndex = text.toLongLong(&ok);
      if (hasTouchIndex || !ok) return std::nullopt;
      hasTouchIndex = true;
    } else if (xml.name() == QLatin1String("Touch")) {
      request.touch = xml.readElementText().trimmed() == QLatin1String("1");
    } else if (xml.name() == QLatin1String("AIPos")) {
      request.joints = decodeAxes(xml.attributes());
      xml.skipCurrentElement();
    } else {
      xml.skipCurrentElement();
    }
  }
  // Reading only through </Rob> misses truncated documents and trailing garbage.
  while (!xml.atEnd()) xml.readNext();
  if (xml.hasError() || !hasIpoc || !hasStart || !hasResult) return std::nullopt;
  return request;
}

// Fixed notation: no exponent text such as 1e-05 reaches the controller.
QByteArray encodeReply(const RsiDevice::Reply& reply)
{
  static const char* const axes[6] = {"X", "Y", "Z", "A", "B", "C"};
  QByteArray datagram;
  QXmlStreamWriter xml(&datagram);
  xml.writeStartElement(QStringLiteral("Sen"));
  xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("ImFree"));
  xml.writeEmptyElement(QStringLiteral("RKorr"));
  for (int axis = 0; axis < 6; ++axis)
    xml.writeAttribute(QLatin1String(axes[axis]), QString::number(reply.correction[axis], 'f', 6));
  xml.writeTextElement(QStringLiteral("Stop"), reply.stop ? QStringLiteral("1") : QStringLiteral("0"));
  xml.writeTextElement(QStringLiteral("Result"), QString::number(reply.result));
  xml.writeTextElement(QStringLiteral("IPOC"), QString::number(reply.ipoc));
  xml.writeEndElement();
  return datagram;
}

constexpr char CaptureHeader[] =
    "# pc_ns: monotonic nanoseconds since device start; rx: controller request, tx: PC reply\n"
    "# tx only: stream_index (next increment before the reply), writeDatagram result and duration,\n"
    "# handler entry, last event-loop sleep and wake, previous replying handler end (pc_ns)\n"
    "# XML values as sent: rx RIst, AIPos (deg), Delay; tx RKorr; xml_base64 only for unreadable XML\n"
    "pc_ns,direction,stream_index,write_bytes,write_ns,entry_ns,sleep_ns,wake_ns,prev_done_ns,"
    "ipoc,start,result,touch_index,touch,delay,rist_x,rist_y,rist_z,rist_a,rist_b,rist_c,"
    "a1,a2,a3,a4,a5,a6,corr_x,corr_y,corr_z,corr_a,corr_b,corr_c,stop,xml_base64\n";

// XML values of the header columns ipoc ... stop: element text or Element.Attribute.
constexpr const char* XmlColumns[] = {
    "IPOC", "Start", "Result", "TouchIndex", "Touch", "Delay.D",
    "RIst.X", "RIst.Y", "RIst.Z", "RIst.A", "RIst.B", "RIst.C",
    "AIPos.A1", "AIPos.A2", "AIPos.A3", "AIPos.A4", "AIPos.A5", "AIPos.A6",
    "RKorr.X", "RKorr.Y", "RKorr.Z", "RKorr.A", "RKorr.B", "RKorr.C", "Stop"};

// Buffered row "pc_ns,rx,-1,-1,-1,<Base64 XML>" or "pc_ns,tx,<index>,<bytes>,<write ns>,<Base64 XML>,
// <entry>,<sleep>,<wake>,<done>" as one CaptureHeader line.
QByteArray captureRow(const QByteArray& row)
{
  const QList<QByteArray> fields = row.split(',');
  const bool reply = fields.value(1) == "tx";
  QByteArray line = fields.value(0) + ',' + fields.value(1);
  for (const int column : {2, 3, 4, 6, 7, 8, 9}) line += ',' + (reply ? fields.value(column) : QByteArray());
  QXmlStreamReader xml(QByteArray::fromBase64(fields.value(5)));
  QHash<QString, QString> values;
  if (xml.readNextStartElement()) {
    while (xml.readNextStartElement()) {
      const QString name = xml.name().toString();
      for (const QXmlStreamAttribute& attribute : xml.attributes())
        values.insert(name + QLatin1Char('.') + attribute.name().toString(), attribute.value().toString());
      values.insert(name, xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
    }
  }
  while (!xml.atEnd()) xml.readNext();
  const bool readable = !xml.hasError() && !values.isEmpty();
  for (const char* key : XmlColumns)
    line += ',' + (readable ? values.value(QString::fromLatin1(key)).toUtf8() : QByteArray());
  return line + ',' + (readable ? QByteArray() : fields.value(5)) + '\n';
}

} // namespace

RsiDevice::RsiDevice(const QString& name, QObject* parent) : AbstractDevice(name, parent) {}

void RsiDevice::startDevice()
{
  Q_ASSERT(!m_sock);
  m_traceClock.start();
  m_traceTimer = new QTimer(this);
  m_traceTimer->setSingleShot(true);
  m_traceTimer->setInterval(CaptureIdleMs);
  QObject::connect(m_traceTimer, &QTimer::timeout, this, &RsiDevice::saveTrace);
  m_sock = new QUdpSocket(this);
  attachSocket(m_sock);
  QObject::connect(m_sock, &QUdpSocket::readyRead, this, &RsiDevice::onReadyRead);
  // Sleep/wake times of this thread's event loop show why a request was read late.
  // Unique: a restarted device must not record twice per event.
  if (auto* dispatcher = QAbstractEventDispatcher::instance()) {
    QObject::connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock,
                     this, &RsiDevice::onDispatcherAboutToBlock, Qt::UniqueConnection);
    QObject::connect(dispatcher, &QAbstractEventDispatcher::awake,
                     this, &RsiDevice::onDispatcherAwake, Qt::UniqueConnection);
  }
  publishState();
}

void RsiDevice::stopDevice()
{
  if (!m_sock) return;
  disconnectDevice();
  for (auto& save : m_traceSaves) save.wait();
  saveTrace();
  delete std::exchange(m_traceTimer, nullptr);
  delete std::exchange(m_sock, nullptr);
}

// Reached only through AbstractDevice::invoke, which refuses calls while the device is stopped.
void RsiDevice::connectDevice(const QVariantMap& config)
{
  if (!config.isEmpty()) {
    auto parsed = parseSocketEndpoints(config);
    if (!parsed) {
      qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Invalid socket configuration";
      return;
    }
    parsed->peerPort.reset(); // Learned from the controller (m_pp), never configured.
    m_endpoints = parsed;
  }
  if (!m_endpoints) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Socket configuration is incomplete";
    return;
  }
  const SocketEndpoints& endpoints = *m_endpoints;
  if (m_sock->state() != QAbstractSocket::UnconnectedState) disconnectDevice();
  emit stateReady({{"connectionConfig", connectionConfigMap(endpoints)}});
  m_trace.reserve(TraceCapacity); // Allocate before peer traffic can start.
  if (!m_sock->bind(endpoints.localAddress, endpoints.localPort)) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << QString("Bind failed: %1").arg(m_sock->errorString());
    return;
  }
  qInfo().noquote() << "[OK]" << objectName() + ":"
      << QString("Socket connected: %1:%2").arg(endpoints.localAddress.toString()).arg(endpoints.localPort);
}

// Callers run while the device is started (stopDevice, connectDevice, invoke), so m_sock exists.
void RsiDevice::disconnectDevice()
{
  m_sock->close();
  // The controller peer port is learned again from the next connection.
  m_pp = 0;
  resetRun();
  saveTrace();
  m_traceFile.clear(); // Unassigned rows of a new connection go to a new file.
  qInfo().noquote() << "[OK]" << objectName() + ":" << "Socket disconnected";
}

void RsiDevice::setCorrections(const QVector<QVector<V6d>>& streams, const QString& diagnosticId)
{
  // Buffered requests belong to the outgoing stream, not the new array.
  while (m_sock && m_sock->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_sock->receiveDatagram();
    if (!acceptsSender(datagram)) continue;
    appendTrace(QByteArray::number(m_traceClock.nsecsElapsed()) + ",rx,-1,-1,-1,"
                + datagram.data().toBase64() + '\n');
  }
  saveTrace();
  if (m_diagnosticId != diagnosticId) m_traceFile.clear();
  m_streams = streams;
  m_diagnosticId = diagnosticId;
  m_activeStream = 0;
  m_index = 0;
  m_reportedTouch = 0;
  m_state = RunState::Idle;
  m_pp = 0;
  qInfo().noquote() << "[INFO]" << objectName() + ": RSI correction streams installed:"
                    << streams.size();
  publishState();
}

bool RsiDevice::acceptsSender(const QNetworkDatagram& datagram) const
{
  return m_endpoints && datagram.senderAddress() == m_endpoints->peerAddress && datagram.senderPort() > 0
      && (m_pp == 0 || datagram.senderPort() == m_pp);
}

void RsiDevice::onReadyRead()
{
  // Replies to older queued packets would arrive late and be rejected, losing
  // their corrections; answer only the newest so the path is delayed, not cut.
  // No console output before the reply: it can delay it past the controller cycle.
  // Malformed and unanswered requests remain in the packet capture.
  const qint64 entryNs = m_traceClock.nsecsElapsed();
  std::optional<Request> newest;
  std::optional<Request> detection; // Oldest pending packet that reports a new touch.
  QVector<QByteArray> receivedRows;
  while (m_sock && m_sock->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_sock->receiveDatagram();
    if (!acceptsSender(datagram)) continue;
    const qint64 receivedNs = m_traceClock.nsecsElapsed();
    receivedRows.append(QByteArray::number(receivedNs) + ",rx,-1,-1,-1,"
                        + datagram.data().toBase64() + '\n');
    const auto request = decodeRequest(datagram.data());
    if (!request) continue;
    m_pp = datagram.senderPort();
    newest = request;
    if (!request->start) m_reportedTouch = 0; // KRL sends Start = 0 before every search.
    if (!detection && reportsTouch(*request)) detection = request;
  }
  if (!newest) {
    for (const auto& row : std::as_const(receivedRows)) appendTrace(row);
    return;
  }
  const qsizetype index = m_index;
  Reply reply = advance(*newest);
  reply.ipoc = newest->ipoc;
  const QByteArray encoded = encodeReply(reply);
  const qint64 sentNs = m_traceClock.nsecsElapsed();
  // A reply follows an accepted request, so the endpoints exist.
  const qint64 written = m_sock->writeDatagram(encoded, m_endpoints->peerAddress, m_pp);
  const qint64 writeNs = m_traceClock.nsecsElapsed() - sentNs;
  // Capture the whole exchange after replying, outside the reply's measured latency.
  for (const auto& row : std::as_const(receivedRows)) appendTrace(row);
  appendTrace(QByteArray::number(sentNs) + ",tx," + QByteArray::number(index)
              + ',' + QByteArray::number(written) + ',' + QByteArray::number(writeNs)
              + ',' + encoded.toBase64() + ',' + QByteArray::number(entryNs)
              + ',' + QByteArray::number(m_sleepNs) + ',' + QByteArray::number(m_wakeNs)
              + ',' + QByteArray::number(m_handlerDoneNs) + '\n');
  m_handlerDoneNs = m_traceClock.nsecsElapsed();
  // After the reply: the queued signal must not delay it.
  if (detection) {
    m_reportedTouch = detection->touchIndex;
    emit touchDetected(static_cast<int>(detection->touchIndex), *detection->joints);
  }
}

bool RsiDevice::reportsTouch(const Request& request) const
{
  return request.start && request.touch && request.joints && request.touchIndex > 0
      && request.touchIndex != m_reportedTouch;
}

void RsiDevice::onDispatcherAboutToBlock()
{
  m_sleepNs = m_traceClock.nsecsElapsed();
}

void RsiDevice::onDispatcherAwake()
{
  m_wakeNs = m_traceClock.nsecsElapsed();
}

void RsiDevice::appendTrace(const QByteArray& row)
{
  if (row.size() <= TraceCapacity - m_trace.size()) m_trace.append(row);
  else ++m_traceDropped;
  m_traceTimer->start();
}

void RsiDevice::saveTrace()
{
  if (!m_traceTimer) return; // Correction updates may arrive before start or after stop.
  for (auto save = m_traceSaves.begin(); save != m_traceSaves.end();) {
    if (save->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++save;
      continue;
    }
    const QString error = save->get();
    if (!error.isEmpty()) qInfo().noquote() << "[ERROR]" << objectName() + ":" << error;
    save = m_traceSaves.erase(save);
  }
  m_traceTimer->stop();
  if (!m_trace.isEmpty()) {
    // One file per table in its record folder records/<id>; unassigned rows in records.
    if (m_traceFile.isEmpty()) {
      m_traceFile = m_diagnosticId.isEmpty()
          ? QStringLiteral("rsi_packets_unassigned_%1.csv")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")))
          : QStringLiteral("%1/rsi_packets_%1.csv").arg(m_diagnosticId);
    }
    if (m_traceDropped > 0) m_trace.append("# dropped_rows," + QByteArray::number(m_traceDropped) + '\n');
    const std::shared_future<QString> previous = m_traceSaves.empty() ? std::shared_future<QString>{}
                                                                       : m_traceSaves.back();
    m_traceSaves.push_back(std::async(std::launch::async, &RsiDevice::writeTrace, previous,
                                      std::exchange(m_trace, {}), m_traceFile).share());
    m_traceDropped = 0;
    if (m_sock->state() == QAbstractSocket::BoundState) m_trace.reserve(TraceCapacity);
  }
  if (!m_traceSaves.empty()) m_traceTimer->start();
}

QString RsiDevice::writeTrace(std::shared_future<QString> previous, QByteArray rows, QString fileName)
{
  if (previous.valid()) previous.wait(); // Rows of one file stay in capture order.
  QFile file(QDir(QStringLiteral(ROBOCRAP_RECORDS_DIR)).filePath(fileName));
  if (!QDir().mkpath(QFileInfo(file.fileName()).absolutePath())
      || !file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    return QStringLiteral("cannot open RSI diagnostic %1 (%2)").arg(file.fileName(), file.errorString());
  }
  QByteArray contents = file.size() == 0 ? QByteArray(CaptureHeader) : QByteArray();
  for (const QByteArray& row : rows.split('\n')) {
    if (row.startsWith('#')) contents += row + '\n';
    else if (!row.isEmpty()) contents += captureRow(row);
  }
  if (file.write(contents) != contents.size() || !file.flush()) {
    return QStringLiteral("cannot save RSI diagnostic %1 (%2)").arg(file.fileName(), file.errorString());
  }
  return {};
}

RsiDevice::Reply RsiDevice::advance(const Request& request)
{
  if (!request.start) {
    resetRun();
    return {};
  }
  // Consecutive searches may send no Start = 0 packet between them; another touch's
  // request must not continue this run's stream.
  if (m_state != RunState::Idle && request.touchIndex != m_activeStream) resetRun();
  if (m_state == RunState::Idle) {
    m_activeStream = request.touchIndex;
    setState(activeStreamSize() == 0 ? RunState::Stopping : RunState::Streaming);
  }
  if (m_state == RunState::Streaming) {
    Reply reply;
    // const access: no detach of the implicitly shared table inside the reply path.
    reply.correction = m_streams.at(m_activeStream).at(m_index++);
    if (m_index == activeStreamSize()) setState(RunState::Finished);
    return reply;
  }
  // Stop only after the controller echoes Result = 1, so KRL sees completion.
  if (m_state == RunState::Finished && request.result == 1) setState(RunState::Stopping);
  Reply reply;
  reply.stop = m_state == RunState::Stopping;
  // Stopping is reached only from Finished or from Start without a stream.
  reply.result = activeStreamSize() == 0 ? 0 : 1;
  return reply;
}

qsizetype RsiDevice::activeStreamSize() const
{
  return m_activeStream < m_streams.size() ? m_streams[m_activeStream].size() : 0;
}

// Runs on every Start = 0 packet; publishes only an actual change.
void RsiDevice::resetRun()
{
  m_index = 0;
  setState(RunState::Idle);
}

void RsiDevice::setState(RunState state)
{
  if (m_state == state) return;
  m_state = state;
  publishState();
}

void RsiDevice::publishState()
{
  static const char* const names[4] = {"idle", "streaming", "finished", "stopping"};
  const char* name = names[static_cast<int>(m_state)];
  emit stateReady({{"rsiState", QString::fromLatin1(name)},
                   {"rsiCorrections", activeStreamSize()}});
}
