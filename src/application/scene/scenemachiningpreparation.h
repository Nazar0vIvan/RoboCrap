#pragma once

#include "scenecalibration.h"
#include "scenemachining.h"
#include "trajectories/chamferrsistream.h"
#include "trajectories/probecalibrationfit.h"
#include "trajectories/probersistream.h"

#include <QTimer>
#include <QVariantHash>
#include <QVariantList>
#include <future>
#include <memory>
#include <optional>

class DeviceRunner;

// RSI Sampling: turns a selected saved machining path into the controller correction stream and
// C:/RoboCrapJobs/shared_data.txt, or the calibration path into one search stream per touch and
// C:/RoboCrapJobs/calibration_data.txt. At most one stream table and its job file are held; they
// belong to the sampled path, not the selection, and are dropped when that path is removed or
// incompatible. With a calibration job held, Start Calibration runs program 1 through the PLC,
// collects the touch detections and fits the ball centre when the program finishes.
class SceneMachiningPreparation final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY availabilityChanged)
  Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
  Q_PROPERTY(bool canPrepareCalibration READ canPrepareCalibration NOTIFY availabilityChanged)
  Q_PROPERTY(QString calibrationUnavailableReason READ calibrationUnavailableReason NOTIFY availabilityChanged)
  Q_PROPERTY(bool preparing READ preparing NOTIFY preparingChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QString summary READ summary NOTIFY preparedChanged)
  Q_PROPERTY(bool canStartCalibration READ canStartCalibration NOTIFY availabilityChanged)
  Q_PROPERTY(bool calibrationRunning READ calibrationRunning NOTIFY calibrationRunChanged)
  Q_PROPERTY(int calibrationDetected READ calibrationDetected NOTIFY calibrationRunChanged)
  Q_PROPERTY(int calibrationTouchCount READ calibrationTouchCount NOTIFY preparedChanged)
  Q_PROPERTY(QString calibrationRunError READ calibrationRunError NOTIFY calibrationRunChanged)
  // Rows {label, value} of the last successful fit.
  Q_PROPERTY(QVariantList calibrationResult READ calibrationResult NOTIFY calibrationRunChanged)

public:
  SceneMachiningPreparation(SceneModel* scene, SceneMachining* machining, SceneCalibration* calibration);
  ~SceneMachiningPreparation() override;
  bool canPrepare() const { return unavailableReason().isEmpty(); }
  QString unavailableReason() const;
  bool canPrepareCalibration() const { return calibrationUnavailableReason().isEmpty(); }
  QString calibrationUnavailableReason() const;
  bool preparing() const { return m_preparing; }
  QString error() const { return m_error; }
  QString summary() const; // Empty while no stream is held.
  Q_INVOKABLE void prepare();
  // Samples the generated calibration path for RSI_Probe_Calibration.
  Q_INVOKABLE void prepareCalibration();
  // Hard-coded RSI_Probe_Check stream: WORLD +X at 1 mm/s for 100 mm; replaces a held stream and file.
  Q_INVOKABLE void prepareTestLin();
  void shutdown();
  // The PLC runner (GUI thread); called once after DeviceHub exists.
  void attachPlc(DeviceRunner* plc);
  // Held, current calibration job, no Sampling or run active, PLC ready to start a program.
  bool canStartCalibration() const;
  bool calibrationRunning() const { return m_run != CalibrationRun::Idle; }
  int calibrationDetected() const { return static_cast<int>(m_detections.size()); }
  int calibrationTouchCount() const { return m_preparedCalibration ? m_preparedCalibration->touchCount : 0; }
  QString calibrationRunError() const { return m_runError; }
  QVariantList calibrationResult() const { return m_runResult; }
  // Starts program 1 (RSI_Probe_Calibration) through the PLC.
  Q_INVOKABLE void startCalibration();

public slots:
  // RsiDevice::touchDetected; kept only during a run, the first detection of each touch.
  void recordTouch(int touch, const V6d& joints);

signals:
  void availabilityChanged();
  void preparingChanged();
  void preparedChanged();
  void errorChanged();
  // Empty values clear the streams; the table (stream i for TouchIndex i) and its log ID travel together.
  void correctionsReady(const QVector<QVector<V6d>>& streams, const QString& diagnosticId);
  void calibrationRunChanged();
  // A run ended; calibrationResult or calibrationRunError holds the outcome.
  void calibrationRunFinished();

private:
  // Requested: Run sent; Starting: PLC start sequence seen; Started: program 1 seen running.
  enum class CalibrationRun { Idle, Requested, Starting, Started };

  struct PreparedRsiStream
  {
    quint32 pathId = 0;
    QString pathName;
    std::shared_ptr<const ChamferMotion> motion; // Identity of the sampled saved motion.
    ChamferRsiStream stream;
  };

  struct PreparedCalibration
  {
    std::shared_ptr<const ProbeTouchMotion> motion; // Identity of the sampled calibration path.
    int touchCount = 0;
    qsizetype stepCount = 0;
    QString diagnosticId; // Names the procedure's record folder records/<id>.
  };

  SceneMachiningPath* pathById(quint32 objectId) const;
  bool isCurrent(const PreparedRsiStream& prepared) const;
  bool isCurrentCalibration(const std::shared_ptr<const ProbeTouchMotion>& motion) const;
  void onAvailabilityChanged();
  void onCalibrationAvailabilityChanged();
  void finishPreparation();
  void finishCalibration();
  void clearPrepared();
  void setError(const QString& error);
  bool plcReady() const;
  void onPlcData(const QVariantHash& values);
  void onPlcSocket();
  // An empty error: program 1 finished, so the detections are fitted.
  void endRun(const QString& error);

  SceneModel* m_scene;
  SceneMachining* m_machining;
  SceneCalibration* m_calibration;
  QTimer m_timer;
  std::future<ChamferRsiStreamResult> m_future;
  PreparedRsiStream m_pending; // Input of the running worker; its stream is unused.
  std::optional<PreparedRsiStream> m_prepared;
  std::future<ProbeCalibrationRsiResult> m_calibrationFuture;
  std::shared_ptr<const ProbeTouchMotion> m_pendingCalibration; // Input of the running calibration worker.
  std::optional<PreparedCalibration> m_preparedCalibration;
  QString m_error;
  bool m_preparing = false;
  bool m_testLin = false; // The installed stream is the test LIN, not a sampled path.
  bool m_shuttingDown = false;
  DeviceRunner* m_plc = nullptr;
  CalibrationRun m_run = CalibrationRun::Idle;
  QVector<ProbeTouchDetection> m_detections; // Of the current or last run.
  QString m_runError;
  QVariantList m_runResult;
};
