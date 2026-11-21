#include "sessionfiles.h"

#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

const QByteArray RsiHeader =
    "t_s;ipoc;start;result;touch_index;touch;delay;rist_x_mm;rist_y_mm;rist_z_mm;rist_a_deg;rist_b_deg;rist_c_deg;"
    "a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg;stream_index;corr_x_mm;corr_y_mm;corr_z_mm;corr_a_deg;corr_b_deg;"
    "corr_c_deg;stop;reply_result;xml_base64\n";
const QByteArray TimingHeader = "ipoc;t_s;entry_us;sleep_us;wake_us;prev_done_us;reply_us;write_us;write_bytes\n";
const QByteArray PlcHeader =
    "t_s;event;x1_0_aut_ext;x1_2_peri_rdy;x1_3_stopmess;x1_4_pro_act;x1_5_pgno_req;x1_6_appl_run;x1_7_in_home;"
    "y1_0_ext_start;y1_1_drives_on;y1_2_conf_mess;y1_3_pgno0;y1_4_pgno1;y1_7_probe;y2_0_spindle;step;program;result\n";

// Request columns ipoc ... a6_deg of rsi.csv: element text or Element.Attribute of the request XML.
constexpr const char* RequestColumns[] = {
    "IPOC", "Start", "Result", "TouchIndex", "Touch", "Delay.D",
    "RIst.X", "RIst.Y", "RIst.Z", "RIst.A", "RIst.B", "RIst.C",
    "AIPos.A1", "AIPos.A2", "AIPos.A3", "AIPos.A4", "AIPos.A5", "AIPos.A6"};
constexpr int ReplyColumnCount = 9; // stream_index, six corrections, stop, reply_result.

// PLC terminals of plc.csv: X1.1 is unassigned; Y1.7 is the PLC's copy of the probe input X2.0 to $IN[8];
// Y1.5, Y1.6 and Y2.1 ... Y2.7 are not part of the robot run.
constexpr int X1Bits[] = {0, 2, 3, 4, 5, 6, 7};
constexpr int Y1Bits[] = {0, 1, 2, 3, 4, 7};
// PLC E_RSTEP and E_RRESULT codes.
const QByteArrayList StepNames{"IDLE", "PREP", "START", "SELECT"};
const QByteArrayList ResultNames{"-", "STARTED", "RESUMED", "TIMEOUT", "ABORTED", "AUT_EXT_LOST"};

struct RsiTables
{
  QByteArray rsi;
  QByteArray timing;
  QJsonObject summary; // session.json "rsi".
};

// Element text and Element.Attribute values of one request, as sent; empty for unreadable XML.
QHash<QString, QString> xmlValues(const QByteArray& xml)
{
  QXmlStreamReader reader(xml);
  QHash<QString, QString> values;
  if (reader.readNextStartElement()) {
    while (reader.readNextStartElement()) {
      const QString name = reader.name().toString();
      for (const QXmlStreamAttribute& attribute : reader.attributes())
        values.insert(name + QLatin1Char('.') + attribute.name().toString(), attribute.value().toString());
      values.insert(name, reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
    }
  }
  // Reading only through </Rob> misses truncated documents and trailing garbage.
  while (!reader.atEnd()) reader.readNext();
  return reader.hasError() ? QHash<QString, QString>{} : values;
}

QByteArray seconds(qint64 ns, qint64 startNs)
{
  return QByteArray::number((ns - startNs) / 1e9, 'f', 6);
}

// Microseconds from origin to time, one decimal; empty for an unrecorded time (-1).
QByteArray offsetUs(qint64 time, qint64 origin)
{
  return time < 0 ? QByteArray() : QByteArray::number((time - origin) / 1e3, 'f', 1);
}

// stream_index ... reply_result of rsi.csv, empty for an unanswered request; the text that was sent.
QByteArrayList replyColumns(const RsiCaptureRecord& record)
{
  if (record.replyNs < 0) return QByteArrayList(ReplyColumnCount);
  QByteArrayList columns{QByteArray::number(record.streamIndex)};
  for (double value : record.correction) columns.append(QByteArray::number(value, 'f', 6));
  columns << QByteArray(record.stop ? "1" : "0") << QByteArray::number(record.result);
  return columns;
}

// One rsi_timing.csv row; the handler columns only for an answered request.
QByteArray timingRow(const RsiCaptureRecord& record, const QByteArray& ipoc, const QByteArray& time)
{
  const bool answered = record.replyNs >= 0;
  QByteArrayList row{ipoc, time};
  for (qint64 value : {record.entryNs, record.sleepNs, record.wakeNs, record.previousDoneNs, record.replyNs})
    row.append(answered ? offsetUs(value, record.receivedNs) : QByteArray());
  row.append(answered ? QByteArray::number(record.writeNs / 1e3, 'f', 1) : QByteArray());
  row.append(answered ? QByteArray::number(record.written) : QByteArray());
  return row.join(';') + '\n';
}

// One decoding pass over all stored requests: both CSV files and their summary.
RsiTables rsiTables(const QVector<RsiCapture>& captures, qint64 startNs)
{
  RsiTables tables{RsiHeader, TimingHeader, {}};
  qint64 requests = 0;
  qint64 unanswered = 0;
  qint64 delayed = 0;
  qint64 unreadable = 0;
  qint64 dropped = 0;
  int maximumDelay = 0;
  for (const RsiCapture& capture : captures) {
    dropped += capture.dropped;
    for (const RsiCaptureRecord& record : capture.records) {
      const QByteArray xml = capture.requestXml.mid(record.xmlBegin, record.xmlSize);
      const QHash<QString, QString> values = xmlValues(xml);
      const QByteArray time = seconds(record.receivedNs, startNs);
      QByteArrayList row{time};
      for (const char* key : RequestColumns) row.append(values.value(QString::fromLatin1(key)).toUtf8());
      row += replyColumns(record);
      row.append(values.isEmpty() ? xml.toBase64() : QByteArray());
      tables.rsi += row.join(';') + '\n';
      tables.timing += timingRow(record, row.at(1), time);
      const int delay = values.value(QStringLiteral("Delay.D")).toInt();
      ++requests;
      unanswered += record.replyNs < 0;
      delayed += delay > 0;
      unreadable += values.isEmpty();
      maximumDelay = std::max(maximumDelay, delay);
    }
  }
  tables.summary = {{QStringLiteral("requests"), requests}, {QStringLiteral("unanswered"), unanswered},
                    {QStringLiteral("delayed"), delayed}, {QStringLiteral("max_delay"), maximumDelay},
                    {QStringLiteral("unreadable"), unreadable}, {QStringLiteral("dropped"), dropped}};
  return tables;
}

QByteArray bitCell(quint8 mask, int bit)
{
  return (mask >> bit) & 1u ? QByteArray("1") : QByteArray("0");
}

// A PLC code as its name; an unknown code as its number.
QByteArray codeName(int code, const QByteArrayList& names)
{
  return code >= 0 && code < names.size() ? names.at(code) : QByteArray::number(code);
}

QByteArray plcCsv(const QVector<PlcIoRecord>& rows, qint64 startNs)
{
  QByteArray contents = PlcHeader;
  for (const PlcIoRecord& row : rows) {
    QByteArrayList cells{seconds(row.timeNs, startNs), row.event.toUtf8()};
    for (const int bit : X1Bits) cells.append(bitCell(row.x1, bit));
    for (const int bit : Y1Bits) cells.append(bitCell(row.y1, bit));
    cells << bitCell(row.y2, 0) << codeName(row.step, StepNames) << QByteArray::number(row.program)
          << codeName(row.result, ResultNames);
    contents += cells.join(';') + '\n';
  }
  return contents;
}

// JSON text of one value; an object indented one level deeper than the session.json keys.
QByteArray jsonText(const QJsonValue& value)
{
  if (value.isObject()) return QJsonDocument(value.toObject()).toJson().trimmed().replace("\n", "\n    ");
  const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return array.mid(1, array.size() - 2);
}

// QJsonObject sorts its keys; session.json keeps the order of RECORDS_FORMAT.md.
QByteArray sessionJson(const QList<QPair<QString, QJsonValue>>& fields)
{
  QByteArray text = "{\n";
  for (const auto& [key, value] : fields) text += "    \"" + key.toUtf8() + "\": " + jsonText(value) + ",\n";
  text.chop(2);
  return text + "\n}\n";
}

} // namespace

double recordNumber(double value)
{
  return std::round(value * 1e6) / 1e6;
}

QJsonArray recordArray(const double* values, qsizetype count)
{
  QJsonArray array;
  for (qsizetype index = 0; index < count; ++index) array.append(recordNumber(values[index]));
  return array;
}

QString writeSessionFolder(SessionRecord record)
{
  if (!QDir().mkpath(record.folder)) return QStringLiteral("cannot create %1").arg(record.folder);
  if (!record.captures.isEmpty()) {
    const RsiTables tables = rsiTables(record.captures, record.startNs);
    record.files.append(RecordFile{QStringLiteral("rsi.csv"), tables.rsi});
    record.files.append(RecordFile{QStringLiteral("rsi_timing.csv"), tables.timing});
    record.session.append(qMakePair(QStringLiteral("rsi"), QJsonValue(tables.summary)));
  }
  if (!record.plcRows.isEmpty())
    record.files.append(RecordFile{QStringLiteral("plc.csv"), plcCsv(record.plcRows, record.startNs)});
  record.files.append(RecordFile{QStringLiteral("session.json"), sessionJson(record.session)});
  const QDir folder(record.folder);
  for (const RecordFile& file : std::as_const(record.files)) {
    if (file.contents.isEmpty()) continue;
    QSaveFile output(folder.filePath(file.name));
    if (!output.open(QIODevice::WriteOnly) || output.write(file.contents) != file.contents.size() || !output.commit())
      return QStringLiteral("cannot write %1: %2").arg(output.fileName(), output.errorString());
  }
  return {};
}
