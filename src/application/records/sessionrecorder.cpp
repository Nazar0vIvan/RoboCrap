#include "sessionrecorder.h"

#include "devices/recordclock.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStringList>

#include <chrono>
#include <cmath>
#include <utility>

namespace {

constexpr qint64 CaptureWaitNs = 2'000'000'000; // A closing session is written without its capture after this.
constexpr int PollMs = 250;

// Folder kind and KRL program of each SessionKind.
const QStringList KindNames{QStringLiteral("calibration"), QStringLiteral("chamfer"), QStringLiteral("test-lin")};
const QStringList KrlPrograms{QStringLiteral("RSI_Probe_Calibration"), QStringLiteral("RSI_Chamfer_Hole"),
                              QStringLiteral("RSI_Probe_Check")};

} // namespace

SessionRecorder::SessionRecorder()
{
  m_timer.setInterval(PollMs);
  connect(&m_timer, &QTimer::timeout, this, &SessionRecorder::onTimer);
}

void SessionRecorder::arm(const SessionJob& job)
{
  m_job = job;
  // A session still closing needs its rows; otherwise only the latest row is kept as the state source.
  if (!m_session && m_plcRows.size() > 1) m_plcRows = {m_plcRows.constLast()};
}

void SessionRecorder::disarm()
{
  m_job.reset();
}

void SessionRecorder::openCalibration()
{
  if (!m_job || m_job->kind != SessionKind::Calibration) return;
  if (m_session) writeCalibration(tr("RSI capture not received."));
  m_session = createSession(*m_job, recordClockNs());
}

QString SessionRecorder::sessionName() const
{
  return m_session ? m_session->name : QString();
}

void SessionRecorder::addFile(const RecordFile& file)
{
  if (m_session) m_session->files.append(file);
}

void SessionRecorder::setResult(const QJsonObject& result)
{
  if (m_session) m_session->result = result;
}

void SessionRecorder::closeCalibration(const QString& error)
{
  if (!m_session) return;
  m_session->error = error;
  m_session->endNs = recordClockNs();
  m_session->closing = true;
  m_timer.start();
  emit captureRequested();
}

void SessionRecorder::shutdown()
{
  // The event loop has ended: deliver the captures and PLC rows that DeviceHub::stopAll produced.
  QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
  if (m_session) {
    if (!m_session->closing) m_session->endNs = recordClockNs();
    writeCalibration(tr("RSI capture not received."));
  }
  reportWrites(true);
  m_timer.stop();
}

void SessionRecorder::addRsiCapture(const RsiCapture& capture)
{
  if (m_session && capture.diagnosticId == m_session->job.jobId) {
    if (!capture.records.isEmpty() || capture.dropped > 0) m_session->captures.append(capture);
    if (m_session->closing && capture.requested) writeCalibration({});
    return;
  }
  if (capture.records.isEmpty() || !m_job || capture.diagnosticId != m_job->jobId) return;
  if (m_job->kind == SessionKind::Calibration) {
    qInfo().noquote() << "[ERROR] Records: RSI exchange of" << capture.diagnosticId
                      << "outside Start Calibration was not recorded.";
    return;
  }
  // Chamfer and test LIN: the programs start on the smartPAD, so the capture is the session.
  OpenSession session = createSession(*m_job, capture.records.constFirst().receivedNs);
  session.endNs = capture.records.constLast().receivedNs;
  session.captures = {capture};
  if (!capture.completed) session.error = tr("RSI exchange ended before the stream finished.");
  writeSession(session);
}

void SessionRecorder::addPlcRecord(const PlcIoRecord& record)
{
  m_plcRows.append(record);
}

SessionRecorder::OpenSession SessionRecorder::createSession(const SessionJob& job, qint64 startNs)
{
  OpenSession session;
  session.job = job;
  session.startNs = startNs;
  session.endNs = startNs;
  session.start = QDateTime::currentDateTime().addMSecs(-(recordClockNs() - startNs) / 1'000'000);
  session.name = session.start.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss_")) + KindNames[static_cast<int>(job.kind)];
  return session;
}

QVector<PlcIoRecord> SessionRecorder::plcRows(const OpenSession& session) const
{
  QVector<PlcIoRecord> rows;
  for (const PlcIoRecord& row : m_plcRows) {
    if (row.timeNs <= session.startNs) {
      rows = {row};
      rows.first().event = QStringLiteral("state");
      rows.first().timeNs = session.startNs;
    } else if (row.timeNs <= session.endNs) {
      rows.append(row);
    }
  }
  return rows;
}

RecordWriteResult SessionRecorder::saveCalibrationPlan(const SessionJob& job) const
{
  return writeSessionFolder(recordFor(createSession(job, recordClockNs()), QStringLiteral("sampled")));
}

SessionRecord SessionRecorder::recordFor(const OpenSession& session, const QString& outcome) const
{
  const bool failed = !session.error.isEmpty();
  const QString name = session.name + (failed ? QStringLiteral("_failed") : QString());
  const QDir records(QStringLiteral(ROBOCRAP_RECORDS_DIR));
  const int kind = static_cast<int>(session.job.kind);
  SessionRecord record{
      records.filePath(name),
      {{QStringLiteral("format"), 1},
       {QStringLiteral("kind"), KindNames[kind]},
       {QStringLiteral("krl_program"), KrlPrograms[kind]},
       {QStringLiteral("start"), session.start.toOffsetFromUtc(session.start.offsetFromUtc()).toString(Qt::ISODateWithMs)},
       {QStringLiteral("duration_s"), std::round((session.endNs - session.startNs) / 1e6) / 1e3},
       {QStringLiteral("outcome"), outcome},
       {QStringLiteral("error"), session.error},
       {QStringLiteral("job_id"), session.job.jobId},
       {QStringLiteral("inputs"), session.job.inputs}},
      session.startNs,
      session.captures,
      outcome == QStringLiteral("sampled") ? QVector<PlcIoRecord>{} : plcRows(session),
      session.job.files + session.files};
  if (!failed && !session.result.isEmpty())
    record.session.append(qMakePair(QStringLiteral("result"), QJsonValue(session.result)));
  return record;
}

void SessionRecorder::writeSession(const OpenSession& session)
{
  const QString outcome = session.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("failed");
  m_writes.push_back(std::async(std::launch::async, &writeSessionFolder, recordFor(session, outcome)));
  m_timer.start();
}

void SessionRecorder::writeCalibration(const QString& note)
{
  OpenSession session = std::move(*m_session);
  m_session.reset();
  if (!note.isEmpty()) session.error = session.error.isEmpty() ? note : session.error + QLatin1Char(' ') + note;
  writeSession(session);
}

void SessionRecorder::onTimer()
{
  if (m_session && m_session->closing && recordClockNs() - m_session->endNs > CaptureWaitNs)
    writeCalibration(tr("RSI capture not received."));
  reportWrites(false);
  if (m_writes.empty() && !(m_session && m_session->closing)) m_timer.stop();
}

void SessionRecorder::reportWrites(bool wait)
{
  for (auto write = m_writes.begin(); write != m_writes.end();) {
    if (!wait && write->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++write;
      continue;
    }
    const QString error = write->get().error;
    if (!error.isEmpty()) qInfo().noquote() << "[ERROR] Records:" << error;
    write = m_writes.erase(write);
  }
}
