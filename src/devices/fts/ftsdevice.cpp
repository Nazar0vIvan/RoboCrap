// ftsdevice.cpp

#include "devices/fts/ftsdevice.h"
#include "common/socketconfigutils.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QSaveFile>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QtEndian>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <utility>

namespace {

constexpr double kSampleHz = 7000.0;
constexpr double kDt = 1.0 / kSampleHz;
constexpr double kCount = 1'000'000.0;
constexpr double kAxisTol = 0.05;

constexpr int kReqLen = 8;
constexpr int kRespLen = 36;
constexpr int kBatchMs = 16;
constexpr int kRxTimeout = 300;
constexpr int kLogCap = 7500;

constexpr quint16 kHeader = 0x1234;
constexpr quint16 kStartCmd = 0x0002;
constexpr quint16 kStopCmd = 0x0000;
constexpr quint16 kBiasCmd = 0x0042;
constexpr quint32 kContinuousSamples = 0; // RDT sample count 0: continuous output.

// current when it differs from the last published value by at least the tolerance.
std::optional<qint32> changedAxis(qint32 last, qint32 current)
{
	const qint64 tol = static_cast<qint64>(kAxisTol * kCount);
	if (std::llabs(qint64(current) - qint64(last)) < tol) return std::nullopt;
	return current;
}

std::optional<RDTResponse> parseResponse(const QByteArray& data)
{
	if (data.size() < kRespLen) return std::nullopt;

	const auto* raw = reinterpret_cast<const uchar*>(data.constData());

	RDTResponse sample;

	sample.rdt_sequence = qFromBigEndian<quint32>(raw + 0);
	sample.ft_sequence = qFromBigEndian<quint32>(raw + 4);
	sample.status = qFromBigEndian<quint32>(raw + 8);

	sample.Fx = qFromBigEndian<qint32>(raw + 12);
	sample.Fy = qFromBigEndian<qint32>(raw + 16);
	sample.Fz = qFromBigEndian<qint32>(raw + 20);

	sample.Tx = qFromBigEndian<qint32>(raw + 24);
	sample.Ty = qFromBigEndian<qint32>(raw + 28);
	sample.Tz = qFromBigEndian<qint32>(raw + 32);

	return sample;
}

} // namespace

FtsDevice::FtsDevice(const QString& name, QObject* parent) : AbstractDevice(name, parent) {}

void FtsDevice::startDevice()
{
	Q_ASSERT(!m_sock);
	Q_ASSERT(!m_rxTimeout);
	Q_ASSERT(!m_savePoll);

	m_sock = new QUdpSocket(this);
	m_rxTimeout = new QTimer(this);
	m_rxTimeout->setSingleShot(true);
	m_rxTimeout->setInterval(kRxTimeout);
	m_savePoll = new QTimer(this);
	m_savePoll->setInterval(kBatchMs);

	attachSocket(m_sock);

	QObject::connect(m_sock, &QUdpSocket::readyRead, this, &FtsDevice::onReadyRead);
	QObject::connect(m_rxTimeout, &QTimer::timeout, this, &FtsDevice::onRxTimeout);
	QObject::connect(m_savePoll, &QTimer::timeout, this, &FtsDevice::onLogSaveFinished);

  emit stateReady({
    {"fx", 0.0},
    {"fy", 0.0},
    {"fz", 0.0},
    {"tx", 0.0},
    {"ty", 0.0},
    {"tz", 0.0}
  });
	publishControlState();
}

void FtsDevice::stopDevice()
{
	if (!m_sock) return;

	disconnectDevice();
	if (m_saveFuture.valid()) {
		m_saveFuture.wait();
		onLogSaveFinished();
	}

	delete std::exchange(m_savePoll, nullptr);
	delete std::exchange(m_rxTimeout, nullptr);
	delete std::exchange(m_sock, nullptr);
}

// Reached only through AbstractDevice::invoke, which refuses calls while the device is stopped.
void FtsDevice::connectDevice(const QVariantMap& config)
{
	std::optional<SocketEndpoints> parsed;
	if (!config.isEmpty()) {
		parsed = parseSocketEndpoints(config);
		if (!parsed || !parsed->peerPort) {
			qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Invalid socket configuration";
			return;
		}
	}
	if (!parsed && !m_endpoints) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Socket configuration is incomplete";
		return;
	}

	// Before the endpoints are replaced, so Stop still reaches the old peer.
	if (m_sock->state() != QAbstractSocket::UnconnectedState) disconnectDevice();
	if (parsed) m_endpoints = parsed;
	const SocketEndpoints& endpoints = *m_endpoints;

	emit stateReady({{"connectionConfig", connectionConfigMap(endpoints)}});

	if (!m_sock->bind(endpoints.localAddress, endpoints.localPort,
									 QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << QString("Bind failed: %1").arg(m_sock->errorString());
		return;
	}
	publishControlState();

	qInfo().noquote() << "[OK]" << objectName() + ":"
			<< QString("Socket connected: local [%1]:%2, peer [%3]:%4")
					 .arg(endpoints.localAddress.toString())
					 .arg(endpoints.localPort)
					 .arg(endpoints.peerAddress.toString())
					 .arg(*endpoints.peerPort);
}

// Callers run while the device is started (stopDevice, connectDevice, invoke), so m_sock exists.
void FtsDevice::disconnectDevice()
{
	if (isSocketReady()) stopStreaming();

	m_sock->close();
	m_rxTimeout->stop();

	m_batch.clear();
	m_needBase = true;
	m_hasPub = false;
	m_streaming = false;
	m_streamRequested = false;
	m_startPending = false;
	m_stopRequested = false;

	stopLogRecording();

	publishControlState();
	qInfo().noquote() << "[OK]" << objectName() + ":" << "Socket disconnected";
}

void FtsDevice::startStreaming()
{
	if (m_sock->state() != QAbstractSocket::BoundState) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "FTS socket is not bound";
		return;
	}

	if (m_saveFuture.valid()) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Wait for the FTS recording to finish saving";
		return;
	}
	if (m_startPending || (m_streamRequested && m_streaming)) return;
	if (!sendRequest(kStartCmd)) return;

	m_needBase = true;
	m_batch.clear();
	m_hasPub = false;
	m_streamRequested = true;
	m_startPending = true;
	m_stopRequested = false;
	m_rxTimeout->start();
	publishControlState();

	emit streamReset();
}

void FtsDevice::stopStreaming()
{
	if (!sendRequest(kStopCmd)) return;
	if (m_logEnabled) processBatch();
	m_streamRequested = false;
	m_startPending = false;
	m_stopRequested = true;
	stopLogRecording();
	m_rxTimeout->start();
	publishControlState();

	emit streamReset();
}

void FtsDevice::bias()
{
	if (!isSocketReady() || !m_streaming || m_startPending || m_stopRequested) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Start the connected FTS stream before biasing";
		return;
	}
	if (!sendRequest(kBiasCmd)) return;
	m_batch.clear();
	m_hasPub = false;

	if (m_batchClock.isValid()) m_batchClock.restart();

	emit streamReset();
}

void FtsDevice::onReadyRead()
{
	bool receivedSample = false;
	while (m_sock && m_sock->hasPendingDatagrams()) {
		const QNetworkDatagram datagram = m_sock->receiveDatagram(m_sock->pendingDatagramSize());
		// Datagrams arrive only while bound, so the endpoints exist.
		if (datagram.senderAddress() != m_endpoints->peerAddress
				|| datagram.senderPort() != *m_endpoints->peerPort) continue;

		const auto parsed = parseResponse(datagram.data());
		if (!parsed) continue;
		receivedSample = true;

		RDTResponse sample = *parsed;

		if (m_needBase) {
			m_baseSeq = sample.rdt_sequence;
			m_batch.clear();
			m_batchClock.start();
			m_needBase = false;
		}
		if (!m_streaming || m_startPending) {
			m_streaming = true;
			m_startPending = false;
			publishControlState();
		}

		sample.timestamp = double(quint32(sample.rdt_sequence - m_baseSeq)) * kDt;

		emit dataSampleHFReady(sample);

		m_batch.push_back(sample);

		if (m_batchClock.elapsed() >= kBatchMs)
			processBatch();
	}
	if (receivedSample) m_rxTimeout->start();
}

void FtsDevice::onRxTimeout()
{
	if (m_logEnabled) processBatch();
	if (m_startPending)
		qInfo().noquote() << "[ERROR]" << objectName() + ":"
				<< "No FTS data received after Start; check the sensor connection";
	m_batch.clear();
	m_needBase = true;
	m_hasPub = false;
	m_streaming = false;
	m_streamRequested = false;
	m_startPending = false;

	stopLogRecording();

	publishControlState();
}

// Bound only after connectDevice stored the endpoints, so a bound socket has a peer.
bool FtsDevice::isSocketReady() const
{
	return m_sock && m_sock->state() == QAbstractSocket::BoundState;
}

bool FtsDevice::canRecord() const
{
	return isSocketReady() && m_streaming && m_streamRequested
			&& !m_startPending && !m_stopRequested && !m_logEnabled && !m_saveFuture.valid();
}

bool FtsDevice::canSave() const
{
	return isSocketReady() && m_stopRequested && !m_streaming && !m_startPending
			&& m_rxTimeout && !m_rxTimeout->isActive() && !m_logEnabled
			&& !m_log.isEmpty() && !m_saveFuture.valid();
}

void FtsDevice::publishControlState()
{
	emit stateReady({
		{"streaming", m_streaming},
		{"startPending", m_startPending},
		{"stopRequested", m_stopRequested},
		{"recording", m_logEnabled},
		{"saving", m_saveFuture.valid()},
		{"canRecord", canRecord()},
		{"canSave", canSave()}
	});
}

void FtsDevice::publishState(const RDTResponse& sample)
{
	if (!m_hasPub) {
		m_lastPub = sample;
		m_hasPub = true;

		emit stateReady({
			{"fx", sample.Fx / kCount},
			{"fy", sample.Fy / kCount},
			{"fz", sample.Fz / kCount},
			{"tx", sample.Tx / kCount},
			{"ty", sample.Ty / kCount},
			{"tz", sample.Tz / kCount}
		});

		return;
	}

	QVariantHash vals;

	if (const auto fx = changedAxis(m_lastPub.Fx, sample.Fx)) { m_lastPub.Fx = *fx; vals.insert("fx", *fx / kCount); }
	if (const auto fy = changedAxis(m_lastPub.Fy, sample.Fy)) { m_lastPub.Fy = *fy; vals.insert("fy", *fy / kCount); }
	if (const auto fz = changedAxis(m_lastPub.Fz, sample.Fz)) { m_lastPub.Fz = *fz; vals.insert("fz", *fz / kCount); }
	if (const auto tx = changedAxis(m_lastPub.Tx, sample.Tx)) { m_lastPub.Tx = *tx; vals.insert("tx", *tx / kCount); }
	if (const auto ty = changedAxis(m_lastPub.Ty, sample.Ty)) { m_lastPub.Ty = *ty; vals.insert("ty", *ty / kCount); }
	if (const auto tz = changedAxis(m_lastPub.Tz, sample.Tz)) { m_lastPub.Tz = *tz; vals.insert("tz", *tz / kCount); }

	if (!vals.isEmpty()) emit stateReady(vals);
}

bool FtsDevice::sendRequest(quint16 cmd)
{
	if (!isSocketReady()) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Connect the FTS socket before sending a command";
		return false;
	}

	QByteArray data(kReqLen, '\0');
	auto* raw = reinterpret_cast<uchar*>(data.data());

	qToBigEndian<quint16>(kHeader, raw + 0);
	qToBigEndian<quint16>(cmd, raw + 2);
	qToBigEndian<quint32>(kContinuousSamples, raw + 4);

	if (m_sock->writeDatagram(data, m_endpoints->peerAddress, *m_endpoints->peerPort) != data.size()) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << QString("Failed to send FTS command: %1").arg(m_sock->errorString());
		return false;
	}
	return true;
}

void FtsDevice::processBatch()
{
	if (m_batch.isEmpty()) return;

	const RDTResponse sample = m_batch.back();

  if (m_logEnabled)
    appendLogSample(sample);

	emit dataBatchReady(m_batch);
	publishState(sample);

	m_batch.clear();
	m_batchClock.restart();
}

void FtsDevice::startLogRecording()
{
  if (m_logEnabled) return;

  if (!canRecord() || !m_rxTimeout || !m_rxTimeout->isActive()) {
    qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Cannot record FTS log: no data is being received";
    return;
  }

  m_log.clear();
  m_log.reserve(kLogCap);
  m_batch.clear();
  m_batchClock.start();
  m_logEnabled = true;
  publishControlState();

  qInfo().noquote() << "[OK]" << objectName() + ":"
      << QString("LF log recording started (capacity=%1 samples)").arg(kLogCap);
}

void FtsDevice::stopLogRecording()
{
  if (!m_logEnabled) return;

  m_logEnabled = false;
  publishControlState();

  qInfo().noquote() << "[OK]" << objectName() + ":" << "LF log recording stopped";
}

void FtsDevice::appendLogSample(const RDTResponse& sample)
{
  m_log.push_back(sample);

  if (m_log.size() < kLogCap) return;

  qInfo().noquote() << "[INFO]" << objectName() + ":"
      << QString("LF log reached capacity (%1 samples), auto-stopping").arg(kLogCap);

  stopLogRecording();
}

void FtsDevice::saveLogToDefaultFile()
{
	if (!canSave()) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << "Connect FTS, record samples and Stop before saving";
		return;
	}

	const QString fileName = QStringLiteral("fts_%1_%2.json")
			.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
				 QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QString filePath = QDir(QStringLiteral(ROBOCRAP_RECORDS_DIR)).filePath(fileName);
	m_saveFuture = std::async(std::launch::async, &FtsDevice::writeLogFile, m_log, filePath);
	m_savePoll->start();
	publishControlState();
}

FtsDevice::LogSaveResult FtsDevice::writeLogFile(QVector<RDTResponse> samples, QString filePath)
{
	LogSaveResult result{std::move(filePath), {}, samples.size(), 0};
	if (!QDir().mkpath(QFileInfo(result.filePath).absolutePath())) {
		result.error = QString("Cannot create the FTS records directory for '%1'").arg(result.filePath);
		return result;
	}
	QJsonArray jsonSamples;

	for (const RDTResponse& sample : std::as_const(samples)) {
		const QJsonObject item{
			{"rdt_sequence", static_cast<qint64>(sample.rdt_sequence)},
			{"ft_sequence", static_cast<qint64>(sample.ft_sequence)},
			{"status", static_cast<qint64>(sample.status)},
			{"Fx", static_cast<qint64>(sample.Fx)},
			{"Fy", static_cast<qint64>(sample.Fy)},
			{"Fz", static_cast<qint64>(sample.Fz)},
			{"Tx", static_cast<qint64>(sample.Tx)},
			{"Ty", static_cast<qint64>(sample.Ty)},
			{"Tz", static_cast<qint64>(sample.Tz)},
			{"timestamp", sample.timestamp}
		};

		jsonSamples.append(item);
	}

	const QJsonObject meta{
		{"capacity", kLogCap},
		{"count", samples.size()},
		{"emit_interval_ms", kBatchMs},
		{"counts_per_unit", kCount},
		{"note", QStringLiteral("Low-frequency FTS samples.")}
	};

	const QJsonObject root{
		{"meta", meta},
		{"samples", jsonSamples}
	};

	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);

	QSaveFile file(result.filePath);

	if (!file.open(QIODevice::WriteOnly)) {
		result.error = QString("Failed to write log file '%1': %2").arg(result.filePath, file.errorString());
		return result;
	}

	const qint64 written = file.write(json);

	if (written != json.size()) {
		result.error = QString("Partial write to '%1': wrote %2 of %3 bytes")
				.arg(result.filePath).arg(written).arg(json.size());
		return result;
	}
	if (!file.commit()) {
		result.error = QString("Failed to finish log file '%1': %2").arg(result.filePath, file.errorString());
		return result;
	}
	result.byteCount = written;
	return result;
}

void FtsDevice::onLogSaveFinished()
{
	if (!m_saveFuture.valid()) {
		m_savePoll->stop();
		return;
	}
	if (m_saveFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
	const LogSaveResult result = m_saveFuture.get();
	m_savePoll->stop();
	publishControlState();
	if (!result.error.isEmpty()) {
		qInfo().noquote() << "[ERROR]" << objectName() + ":" << result.error;
		return;
	}
	qInfo().noquote() << "[OK]" << objectName() + ":"
			<< QString("Saved LF log to '%1' (%2 samples, %3 bytes)")
					 .arg(result.filePath)
					 .arg(result.sampleCount)
					 .arg(result.byteCount);
}
