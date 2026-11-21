#include "scenemachiningpreparation.h"

#include "devices/devicerunner.h"
#include "geometry/utils.h"
#include "kinematics/kr10kinematics.h"
#include "records/sessionrecorder.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QThread>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

namespace {

// Job files in the folder that KRL mounts as /RoboCrapJobs.
const QString SharedDataPath = QStringLiteral("C:/RoboCrapJobs/shared_data.txt");
const QString CalibrationDataPath = QStringLiteral("C:/RoboCrapJobs/calibration_data.txt");

// RSI_Probe_Check (controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md): constant WORLD X increments.
constexpr double RsiCycleSeconds = 0.004; // #IPO_FAST
constexpr double TestLinStep = 0.004; // mm per cycle: 1 mm/s.
constexpr int TestLinCycles = 25000; // 100 mm.
constexpr int TestLinSettleCycles = 50; // Zero corrections before Result = 1, as for Sampling.

// The worker owns its copy of the saved motion handle.
ChamferRsiStreamResult sampleSavedMotion(std::shared_ptr<const ChamferMotion> motion)
{
  return sampleChamferRsiStream(*motion);
}

ProbeCalibrationRsiResult sampleSavedCalibration(std::shared_ptr<const ProbeTouchMotion> motion)
{
  return sampleProbeCalibrationRsi(*motion);
}

QString number(double value)
{
  return QString::number(value, 'f', 6);
}

QString frameRow(const V6d& coordinates)
{
  QStringList fields;
  for (double value : coordinates) fields.append(number(value));
  return fields.join(QLatin1Char(';'));
}

// Creates the file's folder and atomically replaces the file; returns the error text, empty on success.
QString saveFile(const QString& path, const QByteArray& contents)
{
  const QString folder = QFileInfo(path).path();
  if (!QDir().mkpath(folder)) return SceneMachiningPreparation::tr("Cannot create %1.").arg(folder);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    return SceneMachiningPreparation::tr("Cannot save %1: %2").arg(path, file.errorString());
  return {};
}

// One KRL POS row: six-decimal XYZABC and the step's S/T.
QString poseRow(const V6d& coordinates, const ProbeRsiStep& step)
{
  return frameRow(coordinates) + QStringLiteral(";%1;%2\r\n").arg(step.status).arg(step.turn);
}

// Version 1 (RSI_Probe_Calibration): ToolFrame, transfer $VEL.CP in m/s and $ACC.CP in m/s^2, the
// step count, then per step "kind;touch" and the target row (PTP, LIN), the auxiliary and target
// rows (CIRC) or no row (Search, HOME).
QByteArray calibrationData(const ProbeCalibrationRsi& job, const ChamferTimingParameters& timing)
{
  QString contents = QStringLiteral("1\r\n") + frameRow(job.toolCoordinates) + QStringLiteral("\r\n")
      + number(timing.transferFeed / 1000.0) + QStringLiteral("\r\n")
      + number(timing.transferAcceleration / 1000.0) + QStringLiteral("\r\n")
      + QString::number(job.steps.size()) + QStringLiteral("\r\n");
  for (const ProbeRsiStep& step : job.steps) {
    contents += QStringLiteral("%1;%2\r\n").arg(static_cast<int>(step.kind)).arg(step.touch);
    if (step.kind == ProbeRsiStepKind::Circ) contents += poseRow(step.auxiliary, step);
    if (step.kind != ProbeRsiStepKind::Search && step.kind != ProbeRsiStepKind::Home)
      contents += poseRow(step.target, step);
  }
  return contents.toLatin1();
}

// The text of a KRL job file, or the error that prevents it.
struct JobFile
{
  QByteArray contents;
  QString error;
};

// Version 2: PsIn/LeadStart/LeadEnd XYZABC+S/T, PsOut S/T, ToolFrame, CP rates.
// KRL constructs PsOut from PsIn XYZ and LeadEnd ABC; each point has its own S/T.
JobFile sharedData(const ChamferRsiStream& stream, const ChamferMotion& motion)
{
  const RoboCrap3D::Kr10Kinematics kinematics(motion.robot().model);
  const V3d staging = ((stream.stagingPoint * 1e6).array().round() / 1e6).matrix();
  constexpr std::array<std::pair<ChamferBoundary, const char*>, 4> targets{{
      {ChamferBoundary::StagingIn, "PsIn"}, {ChamferBoundary::LeadStart, "LeadStart"},
      {ChamferBoundary::LeadEnd, "LeadEnd"}, {ChamferBoundary::StagingOut, "PsOut"}}};
  QByteArray contents("2\r\n");
  for (const auto& [boundary, name] : targets) {
    const auto& point = motion.points()[motion.boundary(boundary)];
    V6d coordinates = boundary <= ChamferBoundary::LeadStart ? stream.initialCoordinates : stream.finalCoordinates;
    if (boundary == ChamferBoundary::StagingIn || boundary == ChamferBoundary::StagingOut)
      coordinates.head<3>() = staging;
    const auto angles = coordinates.tail<3>();
    const M3d rotation = euler2rot(angles[0], angles[1], angles[2]);
    if (!coordinates.allFinite()
        || (coordinates.head<3>() - point.tcp.block<3, 1>(0, 3)).norm() > RoboCrap3D::Kr10PosEps
        || (rotation - point.tcp.block<3, 3>(0, 0)).cwiseAbs().maxCoeff() > RoboCrap3D::Kr10RotEps)
      return {{}, QStringLiteral("Exported %1 does not match its saved animation TCP pose.")
                      .arg(QString::fromLatin1(name))};
    const auto frames = kinematics.solveFK(point.joints);
    const auto configuration = kinematics.configuration(point.joints, frames);
    const QString statusTurn = QStringLiteral("%1;%2\r\n")
        .arg(configuration.status).arg(configuration.turn);
    contents += (boundary == ChamferBoundary::StagingOut ? statusTurn
                              : frameRow(coordinates) + QLatin1Char(';') + statusTurn).toLatin1();
  }
  const auto& timing = motion.robot().timing;
  contents += (frameRow(stream.toolCoordinates) + QStringLiteral("\r\n")
               + number(timing.transferFeed / 1000.0) + QStringLiteral("\r\n")
               + number(timing.transferAcceleration / 1000.0) + QStringLiteral("\r\n")).toLatin1();
  return {contents, {}};
}

// session.json "inputs" of a chamfer session, without path_name (docs/code context/RECORDS_FORMAT.md).
QJsonObject chamferInputs(const ChamferRsiStream& stream, const ChamferMotion& motion)
{
  const ChamferPathParameters& path = motion.parameters();
  const ChamferTimingParameters& timing = motion.robot().timing;
  const std::array<double, 4>& phaseTimes = motion.centralMotion().phaseTimes();
  const std::array<double, 3> phaseEnds{phaseTimes[1], phaseTimes[2], phaseTimes[3]};
  return {{QStringLiteral("tool_xyzabc"), recordArray(stream.toolCoordinates.data(), 6)},
          {QStringLiteral("chamfer_size_mm"), recordNumber(path.chamferSize)},
          {QStringLiteral("chamfer_angle_deg"), recordNumber(path.chamferAngleDegrees)},
          {QStringLiteral("flip_axis"), path.flipAxis},
          {QStringLiteral("staging_distance_mm"), recordNumber(path.stagingDistance)},
          {QStringLiteral("lead_in_clearance_mm"), recordNumber(path.leadIn.clearance)},
          {QStringLiteral("lead_in_span_deg"), recordNumber(path.leadIn.spanDegrees)},
          {QStringLiteral("lead_out_clearance_mm"), recordNumber(path.leadOut.clearance)},
          {QStringLiteral("lead_out_span_deg"), recordNumber(path.leadOut.spanDegrees)},
          {QStringLiteral("machining_feed_mm_s"), recordNumber(timing.machiningFeed)},
          {QStringLiteral("acceleration_distance_fraction"), recordNumber(timing.accelerationDistanceFraction)},
          {QStringLiteral("transfer_feed_mm_s"), recordNumber(timing.transferFeed)},
          {QStringLiteral("transfer_acceleration_mm_s2"), recordNumber(timing.transferAcceleration)},
          {QStringLiteral("staging_joints_deg"),
           recordArray(motion.points()[motion.boundary(ChamferBoundary::StagingIn)].joints.data(), 6)},
          {QStringLiteral("joint_speed_limits_deg_s"), recordArray(timing.jointSpeed.data(), 6)},
          {QStringLiteral("phase_end_times_s"), recordArray(phaseEnds.data(), 3)},
          {QStringLiteral("cycle_s"), RsiCycleSeconds},
          {QStringLiteral("settle_zero_cycles"), static_cast<qint64>(stream.increments.size() - stream.motionCycles)}};
}

// PlcDevice state values (src/devices/plc/plcdevice.cpp): robotProgramState and robotResult.
constexpr int ProgramRunning = 1;
constexpr int ProgramFinished = 2;
constexpr int ProgramInterrupted = 3;
constexpr int StartResultStarted = 1;
constexpr int StartResultResumed = 2;
// PLC inputs X1.n: KUKA Automatic External outputs (docs/hardware context/HARDWARE_SETUP.md).
constexpr int X1AutExt = 0; // $I_O_ACTCONF
constexpr int X1PeriRdy = 2; // drives on; an EMERGENCY STOP drops it, the smartPAD STOP key does not
constexpr int X1StopMess = 3;
constexpr int X1ProAct = 4;
constexpr int X1ApplRun = 6;
constexpr int X1InHome = 7;
// robotResult names of the PLC's E_RRESULT.
const QStringList StartResultNames{QStringLiteral("-"), QStringLiteral("STARTED"), QStringLiteral("RESUMED"),
                                   QStringLiteral("TIMEOUT"), QStringLiteral("ABORTED"), QStringLiteral("AUT EXT LOST")};

// Both non-empty error texts, separated by a space.
QString joinedError(const QString& first, const QString& second)
{
  return first.isEmpty() ? second : second.isEmpty() ? first : first + QLatin1Char(' ') + second;
}

// session.json "inputs" of a calibration session (docs/code context/RECORDS_FORMAT.md).
QJsonObject calibrationInputs(const ProbeCalibrationRsi& job, const ProbeTouchMotion& motion)
{
  const ProbeTouchPlanParameters& plan = motion.plan().parameters;
  const ChamferTimingParameters& timing = motion.robot().timing;
  return {{QStringLiteral("tool_xyzabc"), recordArray(job.toolCoordinates.data(), 6)},
          {QStringLiteral("sphere_centre_b_mm"), recordArray(plan.sphereCentre.data(), 3)},
          {QStringLiteral("sphere_radius_mm"), recordNumber(plan.sphereRadius)},
          {QStringLiteral("ball_radius_mm"), recordNumber(plan.ballRadius)},
          {QStringLiteral("strut_radius_mm"), recordNumber(plan.strutRadius)},
          {QStringLiteral("strut_recess_mm"), recordNumber(plan.strutRecess)},
          {QStringLiteral("search_feed_mm_s"), recordNumber(plan.searchFeed)},
          {QStringLiteral("transfer_feed_mm_s"), recordNumber(timing.transferFeed)},
          {QStringLiteral("transfer_acceleration_mm_s2"), recordNumber(timing.transferAcceleration)},
          {QStringLiteral("touches"), job.touchCount},
          {QStringLiteral("steps"), static_cast<qint64>(job.steps.size())}};
}

// session.json "result" of a successful calibration session.
QJsonObject resultObject(const ProbeCalibrationFit& fit)
{
  return {{QStringLiteral("ball_centre_f_mm"), recordArray(fit.ballCentre.data(), 3)},
          {QStringLiteral("sphere_centre_b_mm"), recordArray(fit.sphereCentre.data(), 3)},
          {QStringLiteral("verification_sphere_centre_b_mm"), recordArray(fit.verificationSphereCentre.data(), 3)},
          {QStringLiteral("contact_vertical_mm"), recordNumber(fit.contactVertical)},
          {QStringLiteral("contact_horizontal_mm"), recordNumber(fit.contactHorizontal)},
          {QStringLiteral("quantile95_mm"), recordNumber(fit.quantile95)},
          {QStringLiteral("repeatability_mm"), recordNumber(fit.repeatability)},
          {QStringLiteral("smallest_singular_value"), recordNumber(fit.smallestSingularValue)},
          {QStringLiteral("calibration_touches"), fit.calibrationTouches},
          {QStringLiteral("verification_touches"), fit.verificationTouches}};
}

// residual_sets.csv: the step K7 sets of the fit.
QByteArray residualSetsCsv(const ProbeCalibrationFit& fit)
{
  QString contents = QStringLiteral("set;group;value_deg;mean_mm;bound_mm;count\n");
  for (const ProbeResidualSet& set : fit.residualSets)
    contents += QStringList{set.ring ? QStringLiteral("ring") : QStringLiteral("roll"), QString::number(set.group),
                            number(set.value), number(set.mean), number(set.bound), QString::number(set.count)}
                    .join(QLatin1Char(';'))
                + QLatin1Char('\n');
  return contents.toUtf8();
}

// touches.csv: AIPos of every detected touch, deg, in detection order.
QByteArray touchesCsv(const QVector<ProbeTouchDetection>& detections)
{
  QString contents = QStringLiteral("touch;a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg\n");
  for (const ProbeTouchDetection& detection : detections)
    contents += QString::number(detection.touch) + QLatin1Char(';') + frameRow(detection.joints) + QLatin1Char('\n');
  return contents.toUtf8();
}

} // namespace

SceneMachiningPreparation::SceneMachiningPreparation(SceneModel* scene, SceneMachining* machining,
                                                     SceneCalibration* calibration)
  : m_scene(scene), m_machining(machining), m_calibration(calibration)
{
  Q_ASSERT(scene && machining && calibration);
  Q_ASSERT(scene->thread() == thread() && machining->thread() == thread() && calibration->thread() == thread());
  // SceneMachining emits this after selection, removal and compatibility updates.
  connect(machining, &SceneMachining::availabilityChanged, this,
          &SceneMachiningPreparation::onAvailabilityChanged);
  // SceneCalibration emits this when its path is inserted, replaced or removed.
  connect(calibration, &SceneCalibration::availabilityChanged, this,
          &SceneMachiningPreparation::onCalibrationAvailabilityChanged);
  m_timer.setInterval(30);
  // One worker runs at a time; each completion slot ignores the other's future.
  connect(&m_timer, &QTimer::timeout, this, &SceneMachiningPreparation::finishPreparation);
  connect(&m_timer, &QTimer::timeout, this, &SceneMachiningPreparation::finishCalibration);
  // No stream exists yet, so a file left by an earlier session is stale.
  setError(clearPrepared());
}

SceneMachiningPreparation::~SceneMachiningPreparation()
{
  shutdown();
}

bool SceneMachiningPreparation::isCurrent(const PreparedRsiStream& prepared) const
{
  const auto* path = m_machining->machiningPath(prepared.pathId);
  return path && path->compatible() && path->data().motion == prepared.motion;
}

bool SceneMachiningPreparation::isCurrentCalibration(const std::shared_ptr<const ProbeTouchMotion>& motion) const
{
  const auto* path = m_calibration->path();
  return motion && path && path->motion == motion;
}

QString SceneMachiningPreparation::busyReason() const
{
  if (m_shuttingDown) return tr("Application is shutting down.");
  if (m_run != CalibrationRun::Idle) return tr("Calibration run in progress.");
  if (m_preparing) return tr("RSI Sampling…");
  return {};
}

QString SceneMachiningPreparation::calibrationUnavailableReason() const
{
  if (const QString reason = busyReason(); !reason.isEmpty()) return reason;
  if (m_calibration->calculating()) return tr("Calculating trajectory…");
  if (!m_calibration->path()) return tr("Generate the calibration path first.");
  return {};
}

QString SceneMachiningPreparation::unavailableReason() const
{
  if (const QString reason = busyReason(); !reason.isEmpty()) return reason;
  if (m_machining->calculating()) return tr("Calculating trajectory…");
  const auto* path = m_machining->machiningPath(m_machining->inputId());
  if (!path) return tr("Select a generated machining path.");
  return path->incompatibility();
}

QString SceneMachiningPreparation::summary() const
{
  if (m_testLin)
    return tr("RSI test LIN: WORLD +X %1 mm at %2 mm/s · %3 cycles")
        .arg(TestLinCycles * TestLinStep).arg(TestLinStep / RsiCycleSeconds).arg(TestLinCycles + TestLinSettleCycles);
  if (m_preparedCalibration)
    return tr("RSI calibration: %1 touches · %2 steps")
        .arg(m_preparedCalibration->touchCount).arg(m_preparedCalibration->stepCount);
  if (!m_prepared) return {};
  const auto& stream = m_prepared->stream;
  // One line for the footer status bar. One multi-argument substitution keeps a
  // '%' in the user-editable path name literal.
  return tr("RSI: %1 · %2 cycles · %3 s · offset up to %4 mm, %5°")
      .arg(m_prepared->pathName, QString::number(stream.increments.size()),
           QString::number(stream.duration, 'f', 3),
           QString::number(stream.maximumTranslationOffset, 'f', 3),
           QString::number(stream.maximumRotationOffset, 'f', 3));
}

void SceneMachiningPreparation::onAvailabilityChanged()
{
  if (m_prepared && !isCurrent(*m_prepared))
    if (const QString removeError = clearPrepared(); !removeError.isEmpty()) setError(removeError);
  emit availabilityChanged();
}

void SceneMachiningPreparation::onCalibrationAvailabilityChanged()
{
  if (m_preparedCalibration && !isCurrentCalibration(m_preparedCalibration->motion))
    if (const QString removeError = clearPrepared(); !removeError.isEmpty()) setError(removeError);
  emit availabilityChanged();
}

void SceneMachiningPreparation::prepare()
{
  Q_ASSERT(QThread::currentThread() == thread());
  const QString reason = unavailableReason();
  if (!reason.isEmpty()) { setError(reason); return; }
  // A new request replaces the previous stream and file, even if it fails; a file that remains stops it.
  if (const QString removeError = clearPrepared(); !removeError.isEmpty()) { setError(removeError); return; }
  const quint32 pathId = m_machining->inputId();
  m_pending = {pathId, m_scene->findObject(pathId)->name(), m_machining->machiningPath(pathId)->data().motion, {}};
  setError({});
  m_preparing = true;
  m_future = std::async(std::launch::async, &sampleSavedMotion, m_pending.motion);
  m_timer.start();
  emit preparingChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::prepareCalibration()
{
  Q_ASSERT(QThread::currentThread() == thread());
  const QString reason = calibrationUnavailableReason();
  if (!reason.isEmpty()) { setError(reason); return; }
  // A new request replaces the previous stream and job file, even if it fails; a file that remains stops it.
  if (const QString removeError = clearPrepared(); !removeError.isEmpty()) { setError(removeError); return; }
  m_pendingCalibration = m_calibration->path()->motion;
  setError({});
  m_preparing = true;
  m_calibrationFuture = std::async(std::launch::async, &sampleSavedCalibration, m_pendingCalibration);
  m_timer.start();
  emit preparingChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::prepareTestLin()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (const QString reason = busyReason(); !reason.isEmpty()) { setError(reason); return; }
  // Like a new Sampling request: RSI_Chamfer_Hole must not find a file for another stream.
  if (const QString removeError = clearPrepared(); !removeError.isEmpty()) { setError(removeError); return; }
  QVector<V6d> increments(TestLinCycles + TestLinSettleCycles, V6d::Zero());
  for (int cycle = 0; cycle < TestLinCycles; ++cycle) increments[cycle][0] = TestLinStep;
  setError({});
  m_testLin = true;
  const QString id = QStringLiteral("test_lin_")
      + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
  m_recorder->arm(SessionJob{SessionKind::TestLin, id, {},
                             {{QStringLiteral("direction"), QStringLiteral("WORLD +X")},
                              {QStringLiteral("step_mm"), TestLinStep},
                              {QStringLiteral("cycles"), TestLinCycles},
                              {QStringLiteral("settle_zero_cycles"), TestLinSettleCycles}}});
  emit correctionsReady({increments}, id);
  emit preparedChanged();
}

void SceneMachiningPreparation::finishPreparation()
{
  if (!m_future.valid()
      || m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_timer.stop();
  auto result = m_future.get();
  if (result.stream)
    qInfo().noquote() << QStringLiteral("RSI Sampling: %1 cycles, %2 s movement")
                             .arg(result.stream->increments.size()).arg(result.stream->duration);
  else
    qInfo().noquote() << "RSI Sampling failed:" << result.error;
  if (!isCurrent(m_pending)) {
    setError(tr("The sampled path changed. Run RSI Sampling again."));
  } else if (!result.stream) {
    setError(result.error);
  } else {
    const JobFile jobFile = sharedData(*result.stream, *m_pending.motion);
    const QString writeError = jobFile.error.isEmpty() ? saveFile(SharedDataPath, jobFile.contents) : jobFile.error;
    if (writeError.isEmpty()) {
      QJsonObject inputs = chamferInputs(*result.stream, *m_pending.motion);
      inputs.insert(QStringLiteral("path_name"), m_pending.pathName);
      // The plan text moves to the recorder; the held stream keeps only what the run needs.
      m_recorder->arm(SessionJob{SessionKind::Chamfer, result.stream->diagnosticId,
                                 {{QStringLiteral("plan.csv"), std::exchange(result.stream->plan, {})},
                                  {QStringLiteral("shared_data.txt"), jobFile.contents}},
                                 inputs});
      m_pending.stream = std::move(*result.stream);
      m_prepared = std::move(m_pending);
      emit correctionsReady({m_prepared->stream.increments}, m_prepared->stream.diagnosticId);
      emit preparedChanged();
    }
    setError(writeError);
  }
  m_pending = {};
  m_preparing = false;
  emit preparingChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::finishCalibration()
{
  if (!m_calibrationFuture.valid()
      || m_calibrationFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_timer.stop();
  auto result = m_calibrationFuture.get();
  if (result.job)
    qInfo().noquote() << QStringLiteral("RSI calibration sampling: %1 touches, %2 steps")
                             .arg(result.job->touchCount).arg(result.job->steps.size());
  else
    qInfo().noquote() << "RSI calibration sampling failed:" << result.error;
  if (!isCurrentCalibration(m_pendingCalibration)) {
    setError(tr("The calibration path changed. Run RSI Sampling again."));
  } else if (!result.job) {
    setError(result.error);
  } else {
    const QByteArray jobFile = calibrationData(*result.job, m_pendingCalibration->robot().timing);
    const QString writeError = saveFile(CalibrationDataPath, jobFile);
    if (writeError.isEmpty()) {
      m_preparedCalibration = PreparedCalibration{m_pendingCalibration, result.job->touchCount,
                                                  result.job->steps.size()};
      m_recorder->arm(SessionJob{SessionKind::Calibration, result.job->diagnosticId,
                                 {{QStringLiteral("plan.csv"), result.job->plan},
                                  {QStringLiteral("calibration_data.txt"), jobFile}},
                                 calibrationInputs(*result.job, *m_pendingCalibration)});
      emit correctionsReady(result.job->streams, result.job->diagnosticId);
      emit preparedChanged();
    }
    setError(writeError);
  }
  m_pendingCalibration.reset();
  m_preparing = false;
  emit preparingChanged();
  emit availabilityChanged();
}

QString SceneMachiningPreparation::clearPrepared()
{
  // The detections belong to the dropped table.
  if (m_run != CalibrationRun::Idle) endRun(tr("The calibration job was replaced."));
  // Without a stream KRL must not find frames from another or older path.
  QString error;
  for (const QString& path : {SharedDataPath, CalibrationDataPath})
    if (QFile::exists(path) && !QFile::remove(path)) {
      qInfo().noquote() << "[ERROR] RSI Sampling: cannot remove" << path;
      error = joinedError(error, tr("Cannot remove %1.").arg(QDir::toNativeSeparators(path)));
    }
  const bool wasPrepared = m_prepared.has_value() || m_testLin || m_preparedCalibration.has_value();
  m_prepared.reset();
  m_testLin = false;
  m_preparedCalibration.reset();
  // At shutdown the recorder still needs the job to write the RSI device's final capture.
  if (m_recorder && !m_shuttingDown) m_recorder->disarm();
  emit correctionsReady({}, {});
  if (wasPrepared) emit preparedChanged();
  return error;
}

void SceneMachiningPreparation::setError(const QString& error)
{
  if (m_error == error) return;
  m_error = error;
  emit errorChanged();
}

void SceneMachiningPreparation::shutdown()
{
  if (m_shuttingDown) return;
  m_shuttingDown = true;
  m_timer.stop();
  if (m_future.valid()) m_future.wait();
  if (m_calibrationFuture.valid()) m_calibrationFuture.wait();
  m_future = {};
  m_calibrationFuture = {};
  m_pending = {};
  m_pendingCalibration.reset();
  m_preparing = false;
  if (m_run != CalibrationRun::Idle) endRun(tr("Application closed during the calibration run."));
  clearPrepared(); // A removal failure is already logged; nothing shows the text after shutdown.
  emit preparingChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::attachPlc(DeviceRunner* plc)
{
  Q_ASSERT(plc && !m_plc && plc->thread() == thread());
  m_plc = plc;
  connect(plc, &DeviceRunner::dataUpdated, this, &SceneMachiningPreparation::onPlcData);
  connect(plc, &DeviceRunner::socketStateChanged, this, &SceneMachiningPreparation::onPlcSocket);
  emit availabilityChanged();
}

void SceneMachiningPreparation::attachRecorder(SessionRecorder* recorder)
{
  Q_ASSERT(recorder && !m_recorder && recorder->thread() == thread());
  m_recorder = recorder;
}

// The PLC's own Run checks (FB_RunSetVar): step IDLE, AUT EXT, $IN_HOME, APPL_RUN off.
bool SceneMachiningPreparation::plcReady() const
{
  if (!m_plc || !m_plc->isConnected()) return false;
  const QVariantList x1 = m_plc->data()->value(QStringLiteral("x1")).toList();
  return m_plc->data()->value(QStringLiteral("robotStep")).toInt() == 0 && x1.value(X1AutExt).toBool()
      && x1.value(X1InHome).toBool() && !x1.value(X1ApplRun).toBool();
}

// The PLC's own Resume checks (FB_RunSetVar) for this run's interrupted program: step IDLE, AUT EXT,
// APPL_RUN without $PRO_ACT and no stop message. After an EMERGENCY STOP APPL_RUN falls and the
// message stays (only the PLC's $CONF_MESS acknowledges it in AUT EXT), so the PLC refuses Resume.
bool SceneMachiningPreparation::canResumeCalibration() const
{
  if (m_run != CalibrationRun::Started || !m_plc || !m_plc->isConnected()) return false;
  const QQmlPropertyMap& data = *m_plc->data();
  const QVariantList x1 = data.value(QStringLiteral("x1")).toList();
  return data.value(QStringLiteral("robotProgramState")).toInt() == ProgramInterrupted
      && data.value(QStringLiteral("robotStep")).toInt() == 0 && x1.value(X1AutExt).toBool()
      && x1.value(X1ApplRun).toBool() && !x1.value(X1ProAct).toBool() && !x1.value(X1StopMess).toBool();
}

QString SceneMachiningPreparation::calibrationInterruption() const
{
  if (!programStarted() || !m_plc) return {};
  if (m_run != CalibrationRun::Started) return tr("Resuming…");
  const QQmlPropertyMap& data = *m_plc->data();
  if (data.value(QStringLiteral("robotProgramState")).toInt() != ProgramInterrupted) return {};
  const QVariantList x1 = data.value(QStringLiteral("x1")).toList();
  // Short texts: the status line elides its end. Without Resume, leaving AUT EXT ends the run.
  const QString state = !x1.value(X1PeriRdy).toBool() ? tr("E-STOP · switch to T1 to end the run")
      : !x1.value(X1ApplRun).toBool() ? tr("Interrupted · switch to T1 to end the run")
      : x1.value(X1StopMess).toBool() ? tr("Interrupted · acknowledge the message on the smartPAD")
      : tr("Interrupted · press Resume");
  return m_runError.isEmpty() ? state : m_runError + QStringLiteral(" · ") + state;
}

void SceneMachiningPreparation::resumeCalibration()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!canResumeCalibration()) return;
  m_runError.clear();
  m_run = CalibrationRun::Resuming;
  m_plc->invoke(QStringLiteral("resumeRobot"));
  emit calibrationRunChanged();
  emit availabilityChanged();
}

bool SceneMachiningPreparation::canStartCalibration() const
{
  return busyReason().isEmpty() && m_preparedCalibration && isCurrentCalibration(m_preparedCalibration->motion)
      && plcReady();
}

void SceneMachiningPreparation::startCalibration()
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!canStartCalibration()) return;
  m_detections.clear();
  m_runError.clear();
  m_run = CalibrationRun::Requested;
  m_recorder->openCalibration(); // Before Run, so the PLC start sequence is inside the session.
  m_plc->invoke(QStringLiteral("runRobotProgram"), {{QStringLiteral("program"), 1}});
  emit calibrationRunChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::recordTouch(int touch, const V6d& joints)
{
  if (m_run == CalibrationRun::Idle) return;
  for (const ProbeTouchDetection& detection : std::as_const(m_detections))
    if (detection.touch == touch) return;
  m_detections.append(ProbeTouchDetection{touch, joints});
  emit calibrationRunChanged();
}

// The PLC sends robotResult with every 2 s heartbeat, so a start or resume failure is taken only when
// the start sequence of this run (robotStep above IDLE) returns to IDLE without program 1 running.
void SceneMachiningPreparation::onPlcData(const QVariantHash& values)
{
  const int state = values.value(QStringLiteral("robotProgramState"), -1).toInt(); // -1: unchanged.
  const QQmlPropertyMap& data = *m_plc->data();
  const int step = data.value(QStringLiteral("robotStep")).toInt();
  const int result = data.value(QStringLiteral("robotResult")).toInt();
  // Leaving AUT EXT is the operator's abort: a program interrupted in another mode is restarted from
  // CELL, never resumed (the PLC's ResumeBlocked).
  if (m_run != CalibrationRun::Idle && !data.value(QStringLiteral("x1")).toList().value(X1AutExt).toBool()) {
    endRun(tr("AUT EXT was left during the calibration run."));
    return;
  }
  if (programStarted() && state == ProgramFinished) {
    endRun({});
    return;
  }
  if (m_run == CalibrationRun::Resuming || m_run == CalibrationRun::ResumeStarting) {
    // A failed resume keeps the run interrupted; its reason is shown with the interruption.
    if (state == ProgramRunning || (m_run == CalibrationRun::ResumeStarting && step == 0 && result == StartResultResumed)) {
      m_run = CalibrationRun::Started;
    } else if (step != 0) {
      m_run = CalibrationRun::ResumeStarting;
    } else if (m_run == CalibrationRun::ResumeStarting) {
      m_run = CalibrationRun::Started;
      m_runError = tr("Resume failed: %1.").arg(StartResultNames.value(result, QString::number(result)));
    }
    emit calibrationRunChanged();
  }
  if (m_run == CalibrationRun::Requested || m_run == CalibrationRun::Starting) {
    // A start sequence ending with STARTED also counts: the running state can be lost with a PLC frame.
    if (state == ProgramRunning || (m_run == CalibrationRun::Starting && step == 0 && result == StartResultStarted)) {
      m_run = CalibrationRun::Started;
    } else if (step != 0) {
      m_run = CalibrationRun::Starting;
    } else if (m_run == CalibrationRun::Starting) {
      endRun(tr("Program 1 did not start: %1.").arg(StartResultNames.value(result, QString::number(result))));
      return;
    }
  }
  emit availabilityChanged();
}

void SceneMachiningPreparation::onPlcSocket()
{
  emit availabilityChanged();
  if (m_run != CalibrationRun::Idle && !m_plc->isConnected()) endRun(tr("PLC disconnected during the calibration run."));
}

void SceneMachiningPreparation::endRun(const QString& error)
{
  Q_ASSERT(m_preparedCalibration); // clearPrepared ends a run before it drops the job.
  m_run = CalibrationRun::Idle;
  m_runError = error;
  // The touches are kept also when the run or the fit fails.
  if (!m_detections.isEmpty()) m_recorder->addFile({QStringLiteral("touches.csv"), touchesCsv(m_detections)});
  if (error.isEmpty()) {
    const ProbeCalibrationFitResult result = fitProbeCalibration(*m_preparedCalibration->motion, m_detections);
    if (result.fit) {
      m_recorder->addFile({QStringLiteral("residual_sets.csv"), residualSetsCsv(*result.fit)});
      m_recorder->setResult(resultObject(*result.fit));
      // Emits lastCalibrationChanged, which opens the result window; it binds calibrationRunError.
      m_runError = m_calibration->recordCalibrationResult(*result.fit, m_recorder->sessionName());
    } else {
      m_runError = result.error;
    }
  }
  // The record folder is written after RsiDevice returns the remaining requests; failed with an error.
  m_recorder->closeCalibration(m_runError);
  if (!m_runError.isEmpty()) qInfo().noquote() << "Probe calibration run:" << m_runError;
  emit calibrationRunChanged();
  emit availabilityChanged();
  if (!m_shuttingDown) emit calibrationRunFinished();
}
