#pragma once

#include "scenecalibration.h"
#include "scenemachining.h"
#include "trajectories/chamferrsistream.h"
#include "trajectories/probecalibrationfit.h"
#include "trajectories/probersistream.h"

#include <QTimer>
#include <QVariantHash>
#include <future>
#include <memory>
#include <optional>

class DeviceRunner;
class SessionRecorder;

// Machining sampling installs the selected path's correction stream and controller job.
// Combined calibration sampling archives the planned touches and controller text in records.
class SceneMachiningPreparation final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(bool canPrepareCalibration READ canPrepareCalibration NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(QString calibrationUnavailableReason READ calibrationUnavailableReason NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(bool preparing READ preparing NOTIFY preparingChanged FINAL)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
  Q_PROPERTY(QString lastCalibrationRecord READ lastCalibrationRecord NOTIFY preparingChanged FINAL)
  Q_PROPERTY(QString summary READ summary NOTIFY preparedChanged FINAL)
  Q_PROPERTY(bool canStartCalibration READ canStartCalibration NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(bool canResumeCalibration READ canResumeCalibration NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(QString calibrationInterruption READ calibrationInterruption NOTIFY availabilityChanged FINAL)
  Q_PROPERTY(bool calibrationRunning READ calibrationRunning NOTIFY calibrationRunChanged FINAL)
  Q_PROPERTY(int calibrationDetected READ calibrationDetected NOTIFY calibrationRunChanged FINAL)
  Q_PROPERTY(int calibrationTouchCount READ calibrationTouchCount NOTIFY preparedChanged FINAL)
  Q_PROPERTY(QString calibrationRunError READ calibrationRunError NOTIFY calibrationRunChanged FINAL)

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
  QString lastCalibrationRecord() const { return m_lastCalibrationRecord; }
  // Hard-coded RSI_Probe_Check stream: WORLD +X at 1 mm/s for 100 mm; replaces a held stream and file.
  Q_INVOKABLE void prepareTestLin();
  void shutdown();
  // The PLC runner (GUI thread); called once after DeviceHub exists.
  void attachPlc(DeviceRunner* plc);
  // The recorder for sampled plans and executed sessions, attached once after construction.
  void attachRecorder(SessionRecorder* recorder);
  // Held, current calibration job, no Sampling or run active, PLC ready to start a program.
  bool canStartCalibration() const;
  bool calibrationRunning() const { return m_run != CalibrationRun::Idle; }
  int calibrationDetected() const { return static_cast<int>(m_detections.size()); }
  int calibrationTouchCount() const { return m_preparedCalibration ? m_preparedCalibration->touchCount : 0; }
  QString calibrationRunError() const { return m_runError; }
  // Starts program 1 (RSI_Probe_Calibration) through the PLC.
  Q_INVOKABLE void startCalibration();
  // This run's program is interrupted by the smartPAD STOP key and its message acknowledged.
  bool canResumeCalibration() const;
  // Continues the interrupted program 1 through the PLC.
  Q_INVOKABLE void resumeCalibration();
  // Status-bar line while this run's program is interrupted or resuming; empty otherwise.
  QString calibrationInterruption() const;

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
  // A run ended; a successful fit is SceneCalibration's last result, errors are in calibrationRunError.
  void calibrationRunFinished();

private:
  // Requested: Run sent; Starting: PLC start sequence seen; Started: program 1 seen running (it may be
  // interrupted); Resuming: Resume sent; ResumeStarting: PLC sequence of that Resume seen.
  enum class CalibrationRun { Idle, Requested, Starting, Started, Resuming, ResumeStarting };

  // Program 1 of this run was seen running (it may be interrupted or resuming).
  bool programStarted() const
  {
    return m_run == CalibrationRun::Started || m_run == CalibrationRun::Resuming
        || m_run == CalibrationRun::ResumeStarting;
  }

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
  };

  bool isCurrent(const PreparedRsiStream& prepared) const;
  bool isCurrentCalibration(const std::shared_ptr<const ProbeTouchMotion>& motion) const;
  // Shutdown, a calibration run in any non-Idle state or a running Sampling; empty otherwise.
  QString busyReason() const;
  void onAvailabilityChanged();
  void onCalibrationAvailabilityChanged();
  void finishPreparation();
  void finishCalibration();
  // Drops the held table and job files; returns the text of failed removals, empty when none remain.
  QString clearPrepared();
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
  QString m_lastCalibrationRecord;
  std::shared_ptr<const ProbeTouchMotion> m_pendingCalibration; // Owns the borrowed worker input until get() or shutdown wait().
  std::optional<PreparedCalibration> m_preparedCalibration;
  QString m_error;
  bool m_preparing = false;
  bool m_testLin = false; // The installed stream is the test LIN, not a sampled path.
  bool m_shuttingDown = false;
  DeviceRunner* m_plc = nullptr;
  SessionRecorder* m_recorder = nullptr;
  CalibrationRun m_run = CalibrationRun::Idle;
  QVector<ProbeTouchDetection> m_detections; // Of the current or last run.
  QString m_runError;
};
