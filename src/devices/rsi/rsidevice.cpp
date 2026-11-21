#include "devices/rsi/rsidevice.h"
#include "../common/socketconfigutils.h"
#include "devices/recordclock.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>
#include <optional>
#include <utility>

namespace {

// Reserved at connect: a full calibration run stores about 133,000 requests of about 430 bytes.
constexpr qsizetype CaptureRecords = 262144; // About 17 min of continuous RSI.
constexpr qsizetype CaptureXmlBytes = 128 * 1024 * 1024;
constexpr qint64 CaptureSilenceNs = 1'000'000'000; // A capture ends after this long without a request.
constexpr int CaptureCheckMs = 250;

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

} // namespace

RsiDevice::RsiDevice(const QString& name, QObject* parent) : AbstractDevice(name, parent) {}

void RsiDevice::startDevice()
{
  Q_ASSERT(!m_sock);
  m_captureTimer = new QTimer(this);
  m_captureTimer->setInterval(CaptureCheckMs);
  QObject::connect(m_captureTimer, &QTimer::timeout, this, &RsiDevice::onCaptureTimer);
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
  delete std::exchange(m_captureTimer, nullptr);
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
  // Allocate before peer traffic can start; emitCapture keeps the capacity.
  m_capture.records.reserve(CaptureRecords);
  m_capture.requestXml.reserve(CaptureXmlBytes);
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
  emitCapture(false);
  qInfo().noquote() << "[OK]" << objectName() + ":" << "Socket disconnected";
}

void RsiDevice::setCorrections(const QVector<QVector<V6d>>& streams, const QString& diagnosticId)
{
  // Buffered requests belong to the outgoing stream, not the new array.
  while (m_sock && m_sock->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_sock->receiveDatagram();
    if (acceptsSender(datagram)) storeRequest(datagram.data(), recordClockNs());
  }
  emitCapture(false);
  m_streams = streams;
  m_capture.diagnosticId = diagnosticId;
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
  // No console output, file work or allocation on this path: it can delay the reply past the
  // controller cycle. Malformed and unanswered requests remain in the capture.
  const qint64 entryNs = recordClockNs();
  std::optional<Request> newest;
  qsizetype newestRecord = -1; // Capture record of newest.
  std::optional<Request> detection; // Oldest pending packet that reports a new touch.
  while (m_sock && m_sock->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_sock->receiveDatagram();
    if (!acceptsSender(datagram)) continue;
    const qsizetype record = storeRequest(datagram.data(), recordClockNs());
    const auto request = decodeRequest(datagram.data());
    if (!request) continue;
    m_pp = datagram.senderPort();
    newest = request;
    newestRecord = record;
    if (!request->start) m_reportedTouch = 0; // KRL sends Start = 0 before every search.
    if (!detection && reportsTouch(*request)) detection = request;
  }
  if (!newest) return;
  const qsizetype index = m_index;
  Reply reply = advance(*newest);
  reply.ipoc = newest->ipoc;
  const QByteArray encoded = encodeReply(reply);
  const qint64 sentNs = recordClockNs();
  // A reply follows an accepted request, so the endpoints exist.
  const qint64 written = m_sock->writeDatagram(encoded, m_endpoints->peerAddress, m_pp);
  const qint64 writeNs = recordClockNs() - sentNs;
  if (newestRecord >= 0) {
    RsiCaptureRecord& stored = m_capture.records[newestRecord]; // Unshared: no detach.
    stored.entryNs = entryNs;
    stored.sleepNs = m_sleepNs;
    stored.wakeNs = m_wakeNs;
    stored.previousDoneNs = m_handlerDoneNs;
    stored.replyNs = sentNs;
    stored.writeNs = writeNs;
    stored.written = written;
    stored.streamIndex = index;
    stored.correction = reply.correction;
    stored.stop = reply.stop;
    stored.result = reply.result;
  }
  m_handlerDoneNs = recordClockNs();
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
  m_sleepNs = recordClockNs();
}

void RsiDevice::onDispatcherAwake()
{
  m_wakeNs = recordClockNs();
}

qsizetype RsiDevice::storeRequest(const QByteArray& xml, qint64 receivedNs)
{
  if (m_streams.isEmpty()) return -1; // Requests without a table belong to no run.
  QVector<RsiCaptureRecord>& records = m_capture.records;
  if (records.size() == CaptureRecords || xml.size() > CaptureXmlBytes - m_capture.requestXml.size()) {
    ++m_capture.dropped;
    return -1;
  }
  if (records.isEmpty()) m_captureTimer->start(); // Once per capture, never per request.
  RsiCaptureRecord record;
  record.receivedNs = receivedNs;
  record.xmlBegin = m_capture.requestXml.size();
  record.xmlSize = xml.size();
  m_capture.requestXml.append(xml);
  records.append(record);
  return records.size() - 1;
}

void RsiDevice::emitCapture(bool requested)
{
  if (!m_captureTimer || (!requested && m_capture.records.isEmpty())) return;
  m_captureTimer->stop();
  // Deep copies: the reserved members stay unshared, so clearing them keeps their capacity.
  const RsiCapture capture{m_capture.diagnosticId,
                           QVector<RsiCaptureRecord>(m_capture.records.cbegin(), m_capture.records.cend()),
                           QByteArray(m_capture.requestXml.constData(), m_capture.requestXml.size()),
                           m_capture.dropped, m_capture.completed, requested};
  m_capture.records.resize(0);
  m_capture.requestXml.truncate(0);
  m_capture.dropped = 0;
  m_capture.completed = false;
  emit captureReady(capture);
}

void RsiDevice::flushCapture()
{
  emitCapture(true);
}

void RsiDevice::onCaptureTimer()
{
  if (!m_capture.records.isEmpty() && recordClockNs() - m_capture.records.constLast().receivedNs >= CaptureSilenceNs)
    emitCapture(false);
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
  if (m_state == RunState::Finished && request.result == 1) {
    setState(RunState::Stopping);
    m_capture.completed = true;
  }
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
