#pragma once

#include "devices/plc/plcdevice.h"
#include "devices/rsi/rsidevice.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

// Session record folders (docs/code context/RECORDS_FORMAT.md): the values one folder needs and
// the worker operation that writes it.

enum class SessionKind { Calibration, Chamfer, TestLin };

struct RecordFile
{
  QString name;
  QByteArray contents; // An empty file is not written.
};

// What a Sampling (or the test LIN) gives every session that runs its stream table.
struct SessionJob
{
  SessionKind kind = SessionKind::Calibration;
  QString jobId; // The table's diagnosticId.
  QVector<RecordFile> files; // plan.csv and the KRL job file, when the job has them.
  QJsonObject inputs; // session.json "inputs".
};

// Everything one folder needs, copied for the writing worker.
struct SessionRecord
{
  QString folder; // Absolute path with the requested base name.
  QList<QPair<QString, QJsonValue>> session; // session.json fields in document order, without "rsi".
  qint64 startNs = 0; // recordClockNs() of t_s = 0.
  QVector<RsiCapture> captures;
  QVector<PlcIoRecord> plcRows; // The first row is the state at the start.
  QVector<RecordFile> files;
};

// The value rounded to the six decimals of the record files.
double recordNumber(double value);
// count values, each rounded by recordNumber.
QJsonArray recordArray(const double* values, qsizetype count);
struct RecordWriteResult
{
  QString folder;
  QString error;
};

RecordWriteResult writeSessionFolder(SessionRecord record);
