#pragma once

#include "sessionfiles.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <future>
#include <optional>
#include <vector>

// Session records (docs/code context/RECORDS_FORMAT.md) on the GUI thread: the job of the installed
// stream table, the open calibration session, the PLC rows since the job was armed and the workers
// that write finished sessions. A chamfer or test LIN session is one RSI capture of the armed job.
class SessionRecorder final : public QObject
{
  Q_OBJECT

public:
  SessionRecorder();
  // The job of the stream table just installed.
  void arm(const SessionJob& job);
  RecordWriteResult saveCalibrationPlan(const SessionJob& job) const;
  // The table was dropped; an open calibration session keeps its own copy of the job.
  void disarm();
  // Start Calibration: opens the calibration session of the armed job.
  void openCalibration();
  QString sessionName() const; // Folder name of the open session without suffix; empty when none.
  void addFile(const RecordFile& file);
  void setResult(const QJsonObject& result);
  // Ends the calibration session; it is written when RsiDevice has returned its remaining requests.
  void closeCalibration(const QString& error);
  // After DeviceHub::stopAll: takes the devices' last signals, writes a closing session, waits for every write.
  void shutdown();

public slots:
  void addRsiCapture(const RsiCapture& capture);
  void addPlcRecord(const PlcIoRecord& record);

signals:
  // RsiDevice::flushCapture: the remaining requests of the closing calibration session.
  void captureRequested();

private:
  struct OpenSession
  {
    SessionJob job;
    qint64 startNs = 0; // recordClockNs()
    qint64 endNs = 0;
    QDateTime start; // Local time of startNs.
    QString name; // yyyy-MM-dd_HH-mm-ss_<kind>
    QVector<RsiCapture> captures;
    QVector<RecordFile> files;
    QJsonObject result;
    QString error;
    bool closing = false; // Waiting for the requested capture.
  };

  static OpenSession createSession(const SessionJob& job, qint64 startNs);
  // The PLC state at the session start (event "state") and every row until its end.
  QVector<PlcIoRecord> plcRows(const OpenSession& session) const;
  SessionRecord recordFor(const OpenSession& session, const QString& outcome) const;
  void writeSession(const OpenSession& session);
  // Writes the calibration session; note (a missing capture) is added to its error.
  void writeCalibration(const QString& note);
  void onTimer();
  // Prints the errors of finished writes, of all writes when wait is true, and drops them.
  void reportWrites(bool wait);

  std::optional<SessionJob> m_job;
  std::optional<OpenSession> m_session;
  QVector<PlcIoRecord> m_plcRows; // Since the last arm, starting with the latest row before it.
  std::vector<std::future<RecordWriteResult>> m_writes;
  QTimer m_timer; // Capture wait of a closing session and finished writes.
};
