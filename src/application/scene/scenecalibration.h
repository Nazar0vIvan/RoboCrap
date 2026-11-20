#pragma once

#include "scenemodel.h"
#include "sceneendeffectors.h"
#include "sceneplayback.h"
#include "trajectories/probecalibrationfit.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <future>
#include <optional>

// Centres that path generation uses; an absent value falls back to TOOL_DATA[5] or the jog fit.
struct PrimaryCentres
{
  QString source; // "manual" or the calibration id that Reassign took them from.
  std::optional<V3d> ballCentre; // {F}, mm.
  std::optional<V3d> sphereCentre; // {B}, mm.
  std::optional<double> contactVertical; // mm, stylus down.
  std::optional<double> contactHorizontal; // mm, stylus horizontal.
};

// The most recent successful calibration fit (ProbeCalibrationFit without its residual sets).
struct CalibrationSummary
{
  QString id;
  V3d ballCentre = V3d::Zero(); // {F}, mm.
  V3d sphereCentre = V3d::Zero(); // {B}, mm.
  V3d verificationSphereCentre = V3d::Zero(); // {B}, mm.
  double contactVertical = 0.0; // mm.
  double contactHorizontal = 0.0; // mm.
  double quantile95 = 0.0; // mm.
  double repeatability = 0.0; // mm.
  double smallestSingularValue = 0.0;
  int calibrationTouches = 0;
  int verificationTouches = 0;
  int setsOutsideBound = 0;
  int setCount = 0;
};

// Contents of ROBOCRAP_SETTINGS_DIR/probe_calibration.json.
struct ProbeSettings
{
  PrimaryCentres primary;
  std::optional<CalibrationSummary> last;
};

// Probe sphere calibration in the Measuring/Calibration submode: the sphere from the deployed
// sphere file, the generated calibration path and the visibility of the other scene objects
// while the submode is active. Owns the probe settings file: reads it once at construction, saves
// every changed ball centre of the measuring TCP, the last calibration result and Reassign.
class SceneCalibration : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  // Read or write error of the probe settings file; empty after a successful write.
  Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)
  // Rows {label, value} of the last successful calibration fit; empty without one.
  Q_PROPERTY(QVariantList lastCalibrationRows READ lastCalibrationRows NOTIFY lastCalibrationChanged)
  Q_PROPERTY(bool canGenerate READ canGenerate NOTIFY availabilityChanged)
  Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
  Q_PROPERTY(bool calculating READ calculating NOTIFY availabilityChanged)
  Q_PROPERTY(bool canPlay READ canPlay NOTIFY availabilityChanged)
public:
  SceneCalibration(SceneModel* scene, SceneEndEffectors* effectors, ScenePlayback* playback);
  QString error() const { return m_error; }
  QString settingsError() const { return m_settingsError; }
  QVariantList lastCalibrationRows() const;
  // Saves a successful fit of run id as the last result; returns the settings write error.
  QString recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id);
  // Makes the ball and sphere centres and contact distances of the last result primary, applies
  // the ball centre (A, B, C kept) and, in the submode, moves the sphere. False without a last
  // result or when the file cannot be written (settingsError).
  Q_INVOKABLE bool reassignPrimaryCentres();
  // Entering re-reads the sphere file and hides every other object; leaving restores them.
  void setActive(bool active);
  bool canGenerate() const { return unavailableReason().isEmpty(); }
  QString unavailableReason() const;
  bool calculating() const { return m_calculation.valid(); }
  Q_INVOKABLE void generate();
  bool canPlay() const;
  // Resumes the paused calibration playback or starts the calibration path from HOME.
  Q_INVOKABLE void dryRun();
  // The generated calibration path, or null.
  const SceneTouchPath* path() const;

signals:
  void errorChanged();
  // Also emitted for a repeated failure with the same text, so every failed write is reported.
  void settingsErrorChanged();
  // Emitted only by recordCalibrationResult, that is after a successful fit.
  void lastCalibrationChanged();
  void availabilityChanged();

private:
  void activate();
  void deactivate();
  const SceneSphere* sphere() const;
  bool ownsPlayback() const;
  void onPlaybackFailed(quint32 ownerId, const QString& error);
  void replaceSphere(const SceneSphere& sphere);
  // A changed sphere, measuring TCP or robot model makes the calibration path invalid.
  void invalidatePath();
  void finishCalculation();
  void hideOther(SceneObject* object);
  void onObjectAdded(SceneObject* object);
  void setError(const QString& error);
  // Saves X, Y, Z of a changed measuring TCP as the manual primary ball centre.
  void onMeasuringTcpChanged();
  // Replaces X, Y, Z of the measuring TCP by the ball centre; A, B, C stay.
  void applyBallCentre(const V3d& ball);
  void setSettingsError(const QString& error);

  SceneModel* m_scene;
  SceneEndEffectors* m_effectors;
  ScenePlayback* m_playback;
  bool m_active = false;
  quint32 m_sphereId = 0;
  quint32 m_pathId = 0;
  QList<quint32> m_hiddenIds; // Objects hidden on entry, shown again on exit.
  QString m_error;
  ProbeSettings m_settings; // Empty when the file is missing or invalid.
  QString m_settingsError;
  quint64 m_revision = 0; // Increased by invalidatePath.
  quint64 m_calculationRevision = 0;
  QTimer m_calculationTimer;
  std::future<ProbeTouchMotionResult> m_calculation;
};
