#include "scenemachiningpreparation.h"

#include "devices/devicerunner.h"
#include "geometry/endgaugefit.h"
#include "geometry/utils.h"
#include "kinematics/kr10kinematics.h"
#include "records/sessionrecorder.h"

#include <QByteArrayList>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>
#include <QThread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <utility>

namespace {

// Job files in the folder that KRL mounts as /RoboCrapJobs.
const QString ScanDataPath = QStringLiteral("C:/RoboCrapJobs/scan_data.txt");
const QString SharedDataPath = QStringLiteral("C:/RoboCrapJobs/shared_data.txt");

const QString ScanOwnerPath = QStringLiteral(ROBOCRAP_SETTINGS_DIR "/scan_in_progress");
// Enable only after the installed CELL, output, RSI context and physical measurement bounds are commissioned.
constexpr bool ScanCommissioned = false;
const QJsonObject CommissionedScanPoseBounds{}; // Empty until measured for this robot, tool and search setup.

bool hasScanPoseBounds(const QJsonObject& bounds)
{
  for (const auto* key : {"axial_mm", "lateral_mm", "angular_deg"}) {
    const double limit = bounds.value(key).toDouble(-1.0);
    if (!std::isfinite(limit) || limit < 0.0) return false;
  }
  return !bounds.value("basis").toString().trimmed().isEmpty();
}

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

QJsonObject scanPlcState(const QQmlPropertyMap& data)
{
  return {{"x1", QJsonArray::fromVariantList(data.value("x1").toList())},
          {"robot_step", data.value("robotStep").toInt()}, {"robot_program", data.value("robotProgram").toInt()},
          {"robot_result", data.value("robotResult").toInt()}, {"program_state", data.value("robotProgramState").toInt()}};
}

QJsonArray frameRows(const M4d& frame)
{
  QJsonArray rows;
  for (int r = 0; r < 4; ++r) rows.append(QJsonArray{frame(r, 0), frame(r, 1), frame(r, 2), frame(r, 3)});
  return rows;
}

// session.json "inputs" of a calibration session (docs/code context/RECORDS_FORMAT.md).
QJsonObject calibrationInputs(const ProbeCalibrationRsi& job, const ProbeTouchMotion& motion)
{
  const ChamferTimingParameters& timing = motion.robot().timing;
  const auto& mount = *motion.plan().gauge;
  QJsonArray placement;
  for (int row = 0; row < 4; ++row) {
    const V4d values = mount.workpieceInBase.row(row).transpose();
    placement.append(recordArray(values.data(), 4));
  }
  QJsonObject inputs{
          {QStringLiteral("gauge_source"), mount.source},
          {QStringLiteral("gauge_observations_sha256"), mount.observationsSha256},
          {QStringLiteral("gauge_workpiece_in_base"), placement},
          {QStringLiteral("tool_xyzabc"), recordArray(job.toolCoordinates.data(), 6)},
          {QStringLiteral("search_feed_mm_s"), recordNumber(motion.plan().searchFeed())},
          {QStringLiteral("transfer_feed_mm_s"), recordNumber(timing.transferFeed)},
          {QStringLiteral("transfer_acceleration_mm_s2"), recordNumber(timing.transferAcceleration)},
          {QStringLiteral("touches"), job.touchCount},
          {QStringLiteral("steps"), static_cast<qint64>(job.steps.size())}};
  if (const auto& sphere = motion.plan().sphere) {
    inputs.insert("sphere_centre_b_mm", recordArray(sphere->sphereCentre.data(), 3));
    inputs.insert("sphere_radius_mm", recordNumber(sphere->sphereRadius));
    inputs.insert("ball_radius_mm", recordNumber(sphere->ballRadius));
    inputs.insert("strut_radius_mm", recordNumber(sphere->strutRadius));
    inputs.insert("strut_recess_mm", recordNumber(sphere->strutRecess));
  }
  return inputs;
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
  connect(calibration, &SceneCalibration::settingsErrorChanged, this, &SceneMachiningPreparation::onCalibrationAvailabilityChanged);
  connect(calibration, &SceneCalibration::scanSamplingRequested, this, &SceneMachiningPreparation::prepareProbe);
  connect(scene, &SceneModel::objectsChanged, this, &SceneMachiningPreparation::onCalibrationAvailabilityChanged);
  connect(&m_scanCad, &QFileSystemWatcher::fileChanged, this, &SceneMachiningPreparation::onCalibrationAvailabilityChanged);
  m_timer.setInterval(30);
  // One worker runs at a time; each completion slot ignores the other's future.
  connect(&m_timer, &QTimer::timeout, this, &SceneMachiningPreparation::finishPreparation);
  connect(&m_timer, &QTimer::timeout, this, &SceneMachiningPreparation::finishCalibration);
  m_scanRequestTimer.setSingleShot(true);
  // PLC PREP/START/SELECT timeouts total 37 s; allow two additional 2 s snapshots.
  m_scanRequestTimer.setInterval(41000);
  connect(&m_scanRequestTimer, &QTimer::timeout, this, &SceneMachiningPreparation::onScanRequestTimeout);
  if (QFile::exists(ScanOwnerPath)) {
    m_scan.emplace();
    m_scan->flushed = true;
    m_scan->failure = tr("Previous scan requires controller recovery and confirmed return to CELL.");
    setError(m_scan->failure);
  } else {
    setError(clearPrepared());
  }
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

bool SceneMachiningPreparation::controllerBusy() const
{
  if (m_scan || m_run != CalibrationRun::Idle) return true;
  if (m_plc && m_plc->isConnected()) {
    const auto& data = *m_plc->data();
    const int program = data.value("robotProgramState").toInt();
    const auto x1 = data.value("x1").toList();
    if (data.value("robotStep").toInt() != 0 || x1.value(X1ApplRun).toBool()
        || (!x1.value(X1AutExt).toBool() && x1.value(X1ProAct).toBool())
        || program == ProgramRunning || program == ProgramInterrupted) return true;
  }
  if (!m_rsi || !m_rsi->isConnected()) return false;
  const auto state = m_rsi->data()->value("rsiState").toString();
  return !state.isEmpty() && state != QStringLiteral("idle");
}

QString SceneMachiningPreparation::busyReason() const
{
  if (m_shuttingDown) return tr("Application is shutting down.");
  if (m_candidate) return tr("Scan result awaiting archive or operator acceptance.");
  if (controllerBusy()) return tr("Controller program in progress.");
  if (m_preparing) return tr("RSI Sampling…");
  return {};
}

QString SceneMachiningPreparation::calibrationUnavailableReason() const
{
  if (const QString reason = busyReason(); !reason.isEmpty()) return reason;
  if (!m_recorder) return tr("Calibration recorder unavailable.");
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
    return (m_preparedCalibration->scanPathId ? tr("RSI Sampling: %1 touches · %2 steps")
                                              : tr("RSI calibration: %1 touches · %2 steps"))
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

QString SceneMachiningPreparation::probeJobError(const PreparedProbe& probe) const
{
  if (const auto error = m_calibration->gaugeUseError(); !error.isEmpty()) return error;
  if (!probe.scanPathId)
    return isCurrentCalibration(probe.motion) ? QString{} : tr("The calibration path changed. Run RSI Sampling again.");
  const auto* object = m_scene->findObject(probe.scanPathId);
  const auto* path = object ? object->touchPath() : nullptr;
  if (!path || path->motion != probe.motion || probe.probeInputs != m_calibration->scanProbeInputs())
    return tr("The scan path or measuring setup changed. Run RSI Sampling again.");
  if (m_scanCad.files().isEmpty()) return tr("The EG22 CAD file was removed or replaced. Run RSI Sampling again.");
  return m_scene->refineScanError(path->source, probe.motion->plan());
}

void SceneMachiningPreparation::onCalibrationAvailabilityChanged()
{
  if (m_candidate && m_candidate->stale.isEmpty()) m_candidate->stale = probeJobError(m_candidate->probe);
  if (m_preparing && m_pendingProbe.scanPathId && m_pendingScanError.isEmpty())
    m_pendingScanError = probeJobError(m_pendingProbe);
  if (m_preparedCalibration) {
    const QString reason = probeJobError(*m_preparedCalibration);
    if (!reason.isEmpty()) {
      if (m_scan) { failScan(reason); return; }
      const QString removeError = clearPrepared();
      setError(removeError.isEmpty() ? reason : reason + QLatin1Char(' ') + removeError);
    }
  }
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
  prepareProbe(0);
}

void SceneMachiningPreparation::prepareProbe(quint32 scanPathId)
{
  Q_ASSERT(QThread::currentThread() == thread());
  const QString reason = scanPathId ? busyReason() : calibrationUnavailableReason();
  if (!reason.isEmpty()) { setError(reason); return; }
  if (scanPathId && (!m_calibration->canPrepareScan() || !m_recorder)) {
    setError(tr("Select a generated Scan Path with the measuring tool active."));
    return;
  }
  if (const QString error = clearPrepared(); !error.isEmpty()) { setError(error); return; }
  m_pendingProbe = {};
  m_pendingScanError.clear();
  if (scanPathId) {
    const auto* object = m_scene->findObject(scanPathId);
    if (!object || !object->isRefineScan()) { setError(tr("The Scan Path was removed.")); return; }
    const auto& path = *object->touchPath();
    const auto inputs = m_calibration->scanProbeInputs();
    if (inputs.isEmpty() || m_scene->resolveRefineTarget(path.source.job) != path.source.target) {
      setError(tr("The measuring tool or mounted Refine Job target changed."));
      return;
    }
    const auto contacts = m_scene->refineContacts(path.source.job, inputs.value("ball_radius_mm").toDouble());
    if (const auto* error = std::get_if<QString>(&contacts)) { setError(*error); return; }
    if (!m_scanCad.addPath(SceneEndGauge::cadPath())) {
      setError(tr("Cannot monitor the EG22 CAD revision at %1.").arg(SceneEndGauge::cadPath()));
      return;
    }
    m_pendingProbe = {path.motion, scanPathId, inputs};
  } else {
    m_lastCalibrationRecord.clear();
    m_pendingProbe.motion = m_calibration->path()->motion;
  }
  setError({});
  m_preparing = true;
  const auto sample = scanPathId ? &sampleProbeScanRsi : &sampleProbeCalibrationRsi;
  m_calibrationFuture = std::async(std::launch::async, sample, std::cref(*m_pendingProbe.motion));
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
  if (controllerBusy()) {
    setError(tr("Controller program in progress."));
  } else if (!isCurrent(m_pending)) {
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
  QString error = m_pendingScanError;
  if (error.isEmpty()) error = probeJobError(m_pendingProbe);
  if (error.isEmpty() && controllerBusy()) error = tr("Controller program in progress.");
  if (error.isEmpty()) error = result.job ? saveProbePlan(*result.job) : result.error;
  if (!error.isEmpty() && !m_scanCad.files().isEmpty()) m_scanCad.removePaths(m_scanCad.files());
  setError(error);
  m_pendingProbe = {};
  m_preparing = false;
  emit preparingChanged();
  emit availabilityChanged();
}

std::variant<double, QString> SceneMachiningPreparation::PreparedProbe::scanCompensationRadius() const
{
  const auto& plan = motion->plan();
  const auto primary = probeInputs.value("primary").toObject();
  const auto source = primary.value("source").toString();
  const auto ball = primary.value("ballCentreF").toArray();
  const auto sphere = primary.value("sphereCentreB").toArray();
  if (const QString error = probeInputs.value("settings_error").toString(); !error.isEmpty()) return error;
  if (!plan.sphere || source.trimmed().isEmpty() || source == QStringLiteral("manual")
      || ball.size() != 3 || sphere.size() != 3)
    return tr("Scan requires a reference sphere and applied primary probe calibration.");
  const auto& tool = motion->robot().flangeToTcp;
  for (int axis = 0; axis < 3; ++axis)
    if (ball.at(axis).toDouble() != tool(axis, 3) || sphere.at(axis).toDouble() != plan.sphere->sphereCentre[axis])
      return tr("Sampled TCP or reference sphere centre differs from the applied primary.");
  const double physical = probeInputs.value("ball_radius_mm").toDouble();
  if (!positiveFinite(physical) || physical != plan.sphere->ballRadius || !positiveFinite(plan.sphere->sphereRadius))
    return tr("Invalid sampled reference sphere or physical probe ball radius.");
  const double vertical = primary.value("contactVertical").toDouble() - plan.sphere->sphereRadius;
  const double horizontal = primary.value("contactHorizontal").toDouble() - plan.sphere->sphereRadius;
  if (!positiveFinite(vertical) || !positiveFinite(horizontal))
    return tr("Both applied contact distances must exceed the reference sphere radius.");
  // The sampler accepts one group across all three faces. The pinned straight stylus follows flange +Z.
  const int group = plan.passes.front().group;
  const V3d expected = group == 1 ? V3d(0, 0, -1) : V3d(1, 0, 0);
  const V3d stylusInTcp = tool.topLeftCorner<3, 3>().row(2).transpose();
  for (const auto& pass : plan.passes)
    for (const auto& touch : pass.touches) {
      const V3d stylus = touch.start.topLeftCorner<3, 3>() * stylusInTcp;
      if ((stylus - expected).cwiseAbs().maxCoeff() > RoboCrap3D::Kr10RotEps)
        return tr("Sampled probe mounting disagrees with the G%1 stylus orientation.").arg(group);
    }
  return group == 1 ? vertical : horizontal;
}

QString SceneMachiningPreparation::saveProbePlan(const ProbeCalibrationRsi& job)
{
  const auto& motion = *m_pendingProbe.motion;
  const bool scan = m_pendingProbe.scanPathId != 0;
  const QByteArray bytes = calibrationData(job, motion.robot().timing);
  SessionJob record{scan ? SessionKind::Scan : SessionKind::Calibration, job.diagnosticId,
                    {{"plan.csv", job.plan}, {scan ? QStringLiteral("scan_data.txt") : QStringLiteral("calibration_data.txt"), bytes}},
                    calibrationInputs(job, motion)};
  auto* object = m_scene->findObject(m_pendingProbe.scanPathId);
  if (scan) {
    const auto& source = object->touchPath()->source;
    const auto definition = m_scene->findObject(source.job)->refineJob();
    record.inputs.insert("refine_job_id", source.uuid);
    record.inputs.insert("refine_job_revision", source.revision);
    record.inputs.insert("workpiece_id", m_scene->findObject(source.target)->endGauge()->workpieceId);
    record.inputs.insert("model_sha256", definition.value("modelSha256"));
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    record.inputs.insert("scan_data_sha256", QString::fromLatin1(digest));
    record.inputs.insert("measuring_setup", m_pendingProbe.probeInputs);
    record.inputs.insert("robot_model", QStringLiteral("KUKA KR10 R1420"));
    record.inputs.insert("home_joints_deg", recordArray(motion.robot().model.qHome.data(), 6));
    record.inputs.insert("joint_speed_limits_deg_s", recordArray(motion.robot().timing.jointSpeed.data(), 6));
    record.inputs.insert("cycle_s", RsiCycleSeconds);
    record.inputs.insert("group", motion.plan().passes.front().group);
    record.inputs.insert("pose_bounds", CommissionedScanPoseBounds);
    const auto radius = m_pendingProbe.scanCompensationRadius();
    if (const auto* error = std::get_if<QString>(&radius)) record.inputs.insert("scan_setup_error", *error);
    else record.inputs.insert("compensation_radius_mm", std::get<double>(radius));
  } else {
    record.inputs.insert("gauge_length_mm", 22.0);
    record.inputs.insert("gauge_reference", QStringLiteral("nominal"));
  }
  const auto saved = m_recorder->saveCalibrationPlan(record);
  if (!scan) m_lastCalibrationRecord = QDir::toNativeSeparators(saved.folder);
  if (!saved.error.isEmpty() || !scan) return saved.error;
  if (const auto error = saveFile(ScanDataPath, bytes); !error.isEmpty()) return error;
  m_pendingProbe.touchCount = job.touchCount;
  m_pendingProbe.stepCount = job.steps.size();
  m_preparedCalibration = m_pendingProbe;
  m_recorder->arm(record);
  emit correctionsReady(job.streams, job.diagnosticId);
  object->setScanSamplingInput({{"job_id", record.jobId}, {"scan_data_sha256", record.inputs.value("scan_data_sha256")}});
  emit preparedChanged();
  return {};
}

QString SceneMachiningPreparation::clearPrepared()
{
  if (m_scan) return tr("The previous scan still owns the controller job.");
  // The detections belong to the dropped table.
  if (m_run != CalibrationRun::Idle) endRun(tr("The calibration job was replaced."));
  // Without a stream KRL must not find frames from another or older path.
  QStringList errors;
  for (const auto& path : {SharedDataPath, ScanDataPath}) {
    if (QFile::exists(path) && !QFile::remove(path))
      errors.append(tr("Cannot remove %1.").arg(QDir::toNativeSeparators(path)));
  }
  const QString error = errors.join(QLatin1Char(' '));
  if (!error.isEmpty()) qInfo().noquote() << "[ERROR] RSI Sampling:" << error;
  if (!m_candidate && !m_scanCad.files().isEmpty()) m_scanCad.removePaths(m_scanCad.files());
  auto* scan = m_preparedCalibration ? m_scene->findObject(m_preparedCalibration->scanPathId) : nullptr;
  const bool wasPrepared = m_prepared.has_value() || m_testLin || m_preparedCalibration.has_value();
  m_prepared.reset();
  m_testLin = false;
  m_preparedCalibration.reset();
  // At shutdown the recorder still needs the job to write the RSI device's final capture.
  if (m_recorder && !m_shuttingDown) m_recorder->disarm();
  emit correctionsReady({}, {});
  if (scan) scan->setScanSamplingInput({});
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
  m_pendingProbe = {};
  m_preparing = false;
  if (m_scan && !m_scan->flushed) {
    failScan(tr("Application closed during the scan."));
    closeScan();
  } else if (!m_scan && m_run != CalibrationRun::Idle) {
    endRun(tr("Application closed during the calibration run."));
  }
  clearPrepared(); // A removal failure is already logged; nothing shows the text after shutdown.
  emit preparingChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::attachPlc(DeviceRunner* plc, DeviceRunner* rsi)
{
  Q_ASSERT(plc && !m_plc && plc->thread() == thread());
  m_plc = plc;
  m_rsi = rsi;
  if (rsi) {
    connect(rsi, &DeviceRunner::dataUpdated, this, &SceneMachiningPreparation::onRsiState);
    connect(rsi, &DeviceRunner::socketStateChanged, this, &SceneMachiningPreparation::onRsiState);
  }
  connect(plc, &DeviceRunner::dataUpdated, this, &SceneMachiningPreparation::onPlcData);
  connect(plc, &DeviceRunner::socketStateChanged, this, &SceneMachiningPreparation::onPlcSocket);
  emit availabilityChanged();
}

void SceneMachiningPreparation::attachRecorder(SessionRecorder* recorder)
{
  Q_ASSERT(recorder && !m_recorder && recorder->thread() == thread());
  m_recorder = recorder;
  connect(recorder, &SessionRecorder::probeTouch, this, &SceneMachiningPreparation::recordTouch);
  connect(recorder, &SessionRecorder::scanSearchFailed, this, &SceneMachiningPreparation::onSearchFailure);
  connect(recorder, &SessionRecorder::scanClosing, this, &SceneMachiningPreparation::finishScanRecord);
  connect(recorder, &SessionRecorder::scanArchiveWritten, this, &SceneMachiningPreparation::onScanArchiveWritten);
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
  if (m_candidate || m_run != CalibrationRun::Started || !m_plc || !m_plc->isConnected()) return false;
  if (m_scan && (!m_scan->paused || !m_scan->failure.isEmpty() || m_scan->closing || !m_rsi || !m_rsi->isConnected()
                 || m_rsi->data()->value("rsiDiagnosticId").toString() != m_scan->id)) return false;
  const QQmlPropertyMap& data = *m_plc->data();
  const QVariantList x1 = data.value(QStringLiteral("x1")).toList();
  return data.value(QStringLiteral("robotProgramState")).toInt() == ProgramInterrupted
      && data.value(QStringLiteral("robotStep")).toInt() == 0 && x1.value(X1AutExt).toBool()
      && x1.value(X1ApplRun).toBool() && !x1.value(X1ProAct).toBool() && !x1.value(X1StopMess).toBool()
      && m_calibration->gaugeUseError().isEmpty();
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
  if (m_scan) {
    const QString stale = probeJobError(*m_preparedCalibration);
    if (!stale.isEmpty()) { failScan(stale); return; }
  }
  m_runError.clear();
  m_run = CalibrationRun::Resuming;
  if (m_scan) { m_scan->requestPending = true; m_scanRequestTimer.start(); }
  m_plc->invoke(QStringLiteral("resumeRobot"));
  emit calibrationRunChanged();
  emit availabilityChanged();
}

bool SceneMachiningPreparation::canStartScan() const
{
  if (!ScanCommissioned || !canPrepareScan() || !m_preparedCalibration || !plcReady() || !m_plc->data()->value("plcIoCurrent").toBool()
      || !m_rsi || !m_rsi->isConnected()) return false;
  const auto& probe = *m_preparedCalibration;
  const auto* job = m_recorder->armedJob();
  if (!job || job->kind != SessionKind::Scan || m_calibration->selectedScanId() != probe.scanPathId
      || m_rsi->data()->value("rsiDiagnosticId").toString() != job->jobId) return false;
  const auto* object = m_scene->findObject(probe.scanPathId);
  const auto* path = object ? object->touchPath() : nullptr;
  if (!path || path->samplingInput.value("job_id").toString() != job->jobId
      || path->samplingInput.value("scan_data_sha256") != job->inputs.value("scan_data_sha256")) return false;
  return probe.motion->plan().gauge->source == QStringLiteral("measured")
      && job->inputs.contains("compensation_radius_mm") && hasScanPoseBounds(job->inputs.value("pose_bounds").toObject());
}

void SceneMachiningPreparation::startScan()
{
  if (!canStartScan()) return;
  const QString stale = probeJobError(*m_preparedCalibration);
  if (!stale.isEmpty()) {
    const QString cleanup = clearPrepared();
    setError(stale + QLatin1Char(' ') + cleanup);
    return;
  }
  const SessionJob& job = *m_recorder->armedJob();
  m_recorder->openCalibration();
  m_scan.emplace();
  m_scan->id = job.jobId;
  m_scan->terminal = scanPlcState(*m_plc->data());
  m_detections.clear();
  m_runError.clear();
  setError({});
  QString error = saveFile(ScanOwnerPath, job.jobId.toUtf8());
  if (error.isEmpty()) {
    QFile file(ScanDataPath);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !digest.addData(&file) || file.error() != QFileDevice::NoError)
      error = tr("Cannot read %1: %2").arg(ScanDataPath, file.errorString());
    else if (digest.result().toHex() != job.inputs.value("scan_data_sha256").toString().toLatin1())
      error = tr("scan_data.txt differs from the sampled job. Run RSI Sampling again.");
  }
  if (!error.isEmpty()) {
    m_scan->terminated = true; // No PLC request was sent.
    failScan(error);
    closeScan();
    return;
  }
  m_run = CalibrationRun::Requested;
  m_scan->requestPending = true;
  m_scanRequestTimer.start();
  m_plc->invoke(QStringLiteral("runRobotProgram"), {{QStringLiteral("program"), 2}});
  emit calibrationRunChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::onRsiState()
{
  if (m_scan && (!m_rsi->isConnected() || m_rsi->data()->value("rsiDiagnosticId").toString() != m_scan->id))
    failScan(tr("Scan RSI connection or installed job was lost."));
  emit availabilityChanged();
}

void SceneMachiningPreparation::onSearchFailure(const QString& id)
{
  if (m_scan && m_scan->id == id) failScan(tr("Invalid or restarted scan SEARCH. Inspect the recorded RSI requests."));
}

void SceneMachiningPreparation::onScanRequestTimeout()
{
  if (m_scan) failScan(tr("The PLC did not confirm the scan command."));
}

void SceneMachiningPreparation::failScan(const QString& error)
{
  if (!m_scan || !m_scan->failure.isEmpty()) return;
  m_scan->failure = error;
  if (!m_scan->closing) emit scanPermission({});
  m_scanRequestTimer.stop();
  m_runError = error;
  setError(error);
  emit calibrationRunChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::closeScan()
{
  if (m_scan->closing || m_scan->flushed) return;
  m_scan->closing = true;
  emit scanPermission({});
  m_scanRequestTimer.stop();
  m_recorder->closeCalibration(m_scan->failure);
}

void SceneMachiningPreparation::finishScanRecord(const QString& id, const QString& error)
{
  if (!m_scan || m_scan->id != id) return;
  if (!error.isEmpty()) failScan(error);
  if (m_scan->failure.isEmpty()) {
    const QString stale = probeJobError(*m_preparedCalibration);
    if (!stale.isEmpty()) failScan(stale);
  }
  const auto fit = recordScanMeasurements();
  QJsonObject evidence{{"acknowledged", m_scan->acknowledged}, {"observed_low", m_scan->low},
                       {"observed_high_after_low", m_scan->high}, {"terminated", m_scan->terminated},
                       {"terminal_plc", m_scan->terminal}, {"error", m_scan->failure}};
  if (!fit.isEmpty()) {
    if (m_scan->failure.isEmpty()) m_recorder->setResult(fit);
    else evidence.insert("fit", fit);
  }
  m_recorder->addFile({"scan_completion.json", QJsonDocument(evidence).toJson()});
  m_recorder->failScan(m_scan->failure);
  m_scan->flushed = true;
  releaseScan();
}

QJsonObject SceneMachiningPreparation::recordScanMeasurements()
{
  const auto& job = *m_recorder->armedJob();
  const auto& motion = *m_preparedCalibration->motion;
  const int expected = m_preparedCalibration->touchCount;
  if (m_detections.size() != expected)
    failScan(tr("Recorded %1 of %2 scan detections.").arg(m_detections.size()).arg(expected));

  // plan.csv is the immutable sampled acquisition table; touch numbers are one-based row numbers.
  QList<QByteArray> plan;
  for (const auto& file : job.files)
    if (file.name == QStringLiteral("plan.csv")) plan = file.contents.split('\n');
  const auto header = plan.value(0).trimmed().split(';');
  const QList<QByteArray> columns{"search_step", "pass", "role", "source_face", "u", "v"};
  auto rows = touchesCsv(m_detections).split('\n');
  rows[0] += ';' + columns.join(';');
  const auto bounds = job.inputs.value("pose_bounds").toObject();
  const bool bounded = hasScanPoseBounds(bounds);
  if (!bounded) failScan(tr("The sampled scan has no commissioned pose bounds and basis."));
  if (const auto error = job.inputs.value("scan_setup_error").toString(); !error.isEmpty()) failScan(error);
  EndGaugeObservations observations;
  observations.compensationRadiusMm = job.inputs.value("compensation_radius_mm").toDouble();
  observations.physicalBallRadiusMm = m_preparedCalibration->probeInputs.value("ball_radius_mm").toDouble();
  std::array<QList<QByteArray>, 3> centreRows;
  QJsonArray measured;
  for (const auto& detection : std::as_const(m_detections)) {
    const auto fields = plan.value(detection.touch).trimmed().split(';');
    const bool matches = fields.value(0) == QByteArray::number(detection.touch);
    QList<QByteArray> identity;
    for (const auto& column : columns) {
      const QByteArray field = matches ? fields.value(header.indexOf(column)) : QByteArray{};
      if (field.isEmpty()) failScan(tr("Missing scan plan field for touch %1.").arg(detection.touch));
      identity.append(field);
    }
    const QByteArray labels = identity.join(';');
    rows[detection.touch] += ';' + labels;
    const int pass = fields.value(header.indexOf("pass")).toInt();
    const int local = fields.value(header.indexOf("pass_touch")).toInt();
    if (!matches || pass < 1 || pass > motion.plan().passes.size() || local < 1
        || local > motion.plan().passes[pass - 1].touches.size()) {
      failScan(tr("No sampled Approach for touch %1.").arg(detection.touch));
      continue;
    }
    const auto& touch = motion.plan().passes[pass - 1].touches[local - 1];
    const auto pose = motion.measuredTcp(detection.joints);
    if (const auto* error = std::get_if<QString>(&pose)) {
      failScan(tr("Touch %1: %2").arg(detection.touch).arg(*error));
      continue;
    }
    const auto& tcp = std::get<M4d>(pose);
    const V3d centre = tcp.topRightCorner<3, 1>();
    const V3d delta = centre - touch.start.topRightCorner<3, 1>();
    const double axial = -touch.normal.dot(delta);
    const double lateral = (delta + axial * touch.normal).norm();
    const M3d relative = touch.start.topLeftCorner<3, 3>().transpose() * tcp.topLeftCorner<3, 3>();
    const double angle = Eigen::AngleAxisd(relative).angle() * GeomConst::RadToDeg;
    if (!std::isfinite(axial) || !std::isfinite(lateral) || !std::isfinite(angle))
      failScan(tr("Non-finite measured pose deviation at touch %1.").arg(detection.touch));
    else if (bounded && (axial < -bounds.value("axial_mm").toDouble()
             || axial > touch.searchDistance() + bounds.value("axial_mm").toDouble()
             || lateral > bounds.value("lateral_mm").toDouble() || angle > bounds.value("angular_deg").toDouble()))
      failScan(tr("Touch %1 exceeds the sampled pose bounds (axial %2 mm, lateral %3 mm, angle %4 deg).")
                   .arg(detection.touch).arg(axial).arg(lateral).arg(angle));

    const int face = pass - 1;
    observations.ballCentres[face].append(centre);
    measured.append(QJsonObject{{"touch", detection.touch}, {"face", EndGaugeObservations::FaceNames[face]},
                                {"sourceFace", motion.plan().passes[face].sourceFace},
                                {"u", touch.cell.x()}, {"v", touch.cell.y()},
                                {"X", centre.x()}, {"Y", centre.y()}, {"Z", centre.z()}});
    QByteArray row = QByteArray::number(detection.touch) + ';' + labels + ';' + EndGaugeObservations::FaceNames[face].toUtf8();
    for (double value : {centre.x(), centre.y(), centre.z(), axial, lateral, angle})
      row += ';' + QByteArray::number(value, 'g', 17);
    centreRows[face].append(row);
  }
  m_recorder->addFile({"touches.csv", rows.join('\n')});

  EndGaugeFit fit;
  QJsonObject result;
  if (m_scan->failure.isEmpty()) {
    fit = observations.fit();
    result.insert("diagnostic", fit.diagnostic);
    if (fit.workpieceInBase) {
      result.insert("workpiece_in_base", frameRows(*fit.workpieceInBase));
      QJsonArray counts;
      for (const auto& pass : motion.plan().passes) counts.append(pass.touches.size());
      QJsonObject inputs = job.inputs;
      const auto& sphere = *motion.plan().sphere;
      inputs.insert("sphere_radius_mm", sphere.sphereRadius);
      inputs.insert("sphere_centre_b_mm", QJsonArray{sphere.sphereCentre.x(), sphere.sphereCentre.y(), sphere.sphereCentre.z()});
      inputs.insert("gauge_workpiece_in_base", frameRows(motion.plan().gauge->workpieceInBase));
      const auto& tool = motion.robot().flangeToTcp;
      const auto abc = rot2euler(tool.topLeftCorner<3, 3>());
      inputs.insert("tool_xyzabc", QJsonArray{tool(0, 3), tool(1, 3), tool(2, 3), abc.A1, abc.B1, abc.C1});
      QJsonObject document{{"version", 1}, {"model", "GE-22"}, {"source", "measured"},
                           {"workpieceId", inputs.value("workpiece_id")}, {"job_id", job.jobId},
                           {"acquired_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                           {"inputs", inputs}, {"expected_counts", counts}, {"observations", measured},
                           {"compensationRadiusMm", observations.compensationRadiusMm},
                           {"physicalBallRadiusMm", observations.physicalBallRadiusMm}};
      m_candidate = ScanCandidate{*m_preparedCalibration, std::move(document), *fit.workpieceInBase};
    } else failScan(fit.diagnostic);
  }
  QByteArray csv = "touch;" + columns.join(';') + ";face;x_mm;y_mm;z_mm;axial_mm;lateral_mm;angular_deg;plane_residual_mm\n";
  QJsonObject summaries;
  for (int face = 0; face < 3; ++face) {
    const auto& residuals = fit.planeResidualsMm[face];
    double squares = 0.0;
    double maximum = 0.0;
    for (qsizetype i = 0; i < centreRows[face].size(); ++i) {
      csv += centreRows[face][i] + ';';
      if (!residuals.isEmpty()) {
        const double residual = residuals[i];
        csv += QByteArray::number(residual, 'g', 17);
        squares += residual * residual;
        maximum = std::max(maximum, std::abs(residual));
      }
      csv += '\n';
    }
    if (!residuals.isEmpty())
      summaries.insert(EndGaugeObservations::FaceNames[face], QJsonObject{{"count", residuals.size()},
                       {"rms_mm", std::sqrt(squares / residuals.size())}, {"max_abs_mm", maximum}});
  }
  if (!summaries.isEmpty()) result.insert("plane_residuals", summaries);
  m_recorder->addFile({"observations.csv", csv});
  return result;
}

void SceneMachiningPreparation::releaseScan()
{
  if (!m_scan->terminated || !m_scan->flushed) return;
  if (QFile::exists(ScanOwnerPath) && !QFile::remove(ScanOwnerPath)) {
    setError(tr("Cannot release %1.").arg(ScanOwnerPath));
    return;
  }
  const QString outcome = m_scan->id.isEmpty() ? QString{} : m_scan->failure;
  m_scan.reset();
  m_run = CalibrationRun::Idle;
  const QString cleanup = clearPrepared();
  setError(cleanup.isEmpty() ? outcome : outcome + QLatin1Char(' ') + cleanup);
  if (m_candidate && m_candidate->document.contains("session_folder")) emit scanResultChanged();
  emit calibrationRunChanged();
  emit availabilityChanged();
}

QVariantList SceneMachiningPreparation::scanDisplacement() const
{
  if (m_scan || !m_candidate || !m_candidate->document.contains("session_folder")) return {};
  const M4d relative = inverseRigidTransform(m_candidate->probe.motion->plan().gauge->workpieceInBase)
      * m_candidate->fittedFrame;
  const auto angles = rot2euler(relative.topLeftCorner<3, 3>());
  return {relative(0, 3), relative(1, 3), relative(2, 3), angles.A1, angles.B1, angles.C1};
}

void SceneMachiningPreparation::onScanArchiveWritten(const QString& jobId, const QString& folder, const QString& error)
{
  if (m_shuttingDown || !m_candidate || m_candidate->document.value("job_id").toString() != jobId) return;
  m_candidate->archiveFailed = !error.isEmpty();
  if (error.isEmpty()) m_candidate->document.insert("session_folder", folder);
  else setError(error);
  emit scanResultChanged();
}

void SceneMachiningPreparation::retryScanArchive()
{
  if (!scanArchiveFailed() || m_shuttingDown) return;
  m_candidate->archiveFailed = false;
  setError({});
  m_recorder->retryScanArchive();
  emit scanResultChanged();
}

bool SceneMachiningPreparation::reassignScan()
{
  if (m_scan || m_shuttingDown || !m_candidate || !m_candidate->document.contains("session_folder")) return false;
  if (m_candidate->stale.isEmpty()) m_candidate->stale = probeJobError(m_candidate->probe);
  if (!m_candidate->stale.isEmpty()) { setError(m_candidate->stale); return false; }
  const auto* path = m_scene->findObject(m_candidate->probe.scanPathId)->touchPath();
  if (const auto error = m_scene->savedRefineJobError(path->source.job); !error.isEmpty()) { setError(error); return false; }
  const bool committed = m_calibration->applyScanObservations(m_candidate->document);
  setError(m_calibration->error());
  if (committed) discardScanResult();
  return committed;
}

void SceneMachiningPreparation::discardScanResult()
{
  if (!m_candidate) return;
  m_candidate.reset();
  m_recorder->discardScanArchive();
  if (!m_scan && !m_scanCad.files().isEmpty()) m_scanCad.removePaths(m_scanCad.files());
  emit scanResultChanged();
  emit availabilityChanged();
}

void SceneMachiningPreparation::observeScan(const QVariantHash& values)
{
  auto& scan = *m_scan;
  if (scan.terminated) { releaseScan(); return; }
  if (values.contains("plcIoCurrent") && !values.value("plcIoCurrent").toBool()) {
    failScan(tr("Scan PLC feedback was lost."));
    return;
  }
  for (const auto& key : {QStringLiteral("scanStartReply"), QStringLiteral("resumeReply")}) {
    if (!values.contains(key) || !scan.requestPending) continue;
    const bool resume = key == QStringLiteral("resumeReply");
    if (resume != (m_run == CalibrationRun::Resuming || m_run == CalibrationRun::ResumeStarting)) return;
    scan.requestPending = false;
    if (!values.value(key).isValid()) {
      failScan(tr("PLC command outcome is unknown. Controller recovery is required."));
    } else if (!values.value(key).toBool()) {
      m_scanRequestTimer.stop();
      if (resume && scan.failure.isEmpty()) {
        m_run = CalibrationRun::Started;
        m_runError = tr("PLC refused Resume. Check the stop acknowledgement.");
        emit calibrationRunChanged();
      } else if (!resume) {
        scan.recoverySeen = true; // Matched negative PLC reply confirms non-start.
        failScan(tr("PLC refused scan program 2."));
      }
    } else if (!resume) {
      scan.accepted = true;
    }
    return;
  }
  const auto x1 = values.value("x1").toList();
  if (x1.size() != 8 || !m_plc->isConnected() || !values.contains("robotStep")
      || values.value("robotProgramState", -1).toInt() == 0) return;
  const int step = values.value("robotStep").toInt();
  const int program = values.value("robotProgram").toInt();
  const int result = values.value("robotResult").toInt();
  const int state = m_plc->data()->value("robotProgramState").toInt();
  const bool automatic = x1.at(X1AutExt).toBool();
  const bool active = x1.at(X1ApplRun).toBool();
  const bool proAct = x1.at(X1ProAct).toBool();
  const bool cell = automatic && !active && proAct && x1.at(5).toBool() && step == 0;
  scan.terminal = scanPlcState(*m_plc->data());
  if (scan.accepted && program == 2 && (result == 4 || result == 5) && m_run == CalibrationRun::Starting)
    failScan(tr("PLC aborted the scan start sequence."));
  if (!automatic) {
    failScan(tr("AUT EXT was left during the scan."));
    if (!active && !proAct && step == 0 && !scan.requestPending) scan.recoverySeen = true;
  }
  if (!scan.acknowledged && program == 2 && step != 0 && m_run == CalibrationRun::Requested)
    m_run = CalibrationRun::Starting;
  if (scan.accepted && m_run == CalibrationRun::Starting && program == 2) {
    if (active && !x1.at(5).toBool() && (proAct || state == ProgramInterrupted)) {
      scan.acknowledged = true;
      m_run = CalibrationRun::Started;
      m_scanRequestTimer.stop();
      scan.paused = state == ProgramInterrupted;
      if (scan.failure.isEmpty()) emit scanPermission(scan.id, scan.paused);
    } else if (step == 0 && !active) {
      failScan(tr("Scan program 2 did not start. PLC result %1.").arg(result));
    }
  }
  if (scan.acknowledged && active && !x1.at(1).toBool()) scan.low = true;
  if (scan.low && x1.at(1).toBool()) scan.high = true;
  if (scan.acknowledged && scan.failure.isEmpty()) {
    if (state == ProgramInterrupted && !scan.paused) {
      scan.paused = true;
      emit scanPermission(scan.id, true);
    }
    const bool resuming = m_run == CalibrationRun::Resuming || m_run == CalibrationRun::ResumeStarting;
    if (resuming && program == 0 && step != 0) m_run = CalibrationRun::ResumeStarting;
    if (resuming && program == 0 && !scan.requestPending) observeResume(state, step, result);
    if (scan.paused && active && proAct && state == ProgramRunning && !scan.requestPending) {
      if (resuming && program == 0) {
        scan.paused = false;
        emit scanPermission(scan.id, false);
      } else {
        failScan(tr("Scan continued without its acknowledged Resume."));
      }
    }
  }
  if (cell && !scan.requestPending && (scan.acknowledged || scan.recoverySeen)) {
    scan.terminated = true;
    if (!scan.acknowledged || scan.paused || !scan.low || !scan.high || !x1.at(1).toBool()
        || !x1.at(X1InHome).toBool() || state != ProgramFinished)
      failScan(tr("CELL returned without complete scan success and HOME evidence."));
    closeScan();
  }
  releaseScan();
}

bool SceneMachiningPreparation::canStartCalibration() const
{
  return busyReason().isEmpty() && m_preparedCalibration && !m_preparedCalibration->scanPathId
      && isCurrentCalibration(m_preparedCalibration->motion) && m_calibration->gaugeUseError().isEmpty()
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

void SceneMachiningPreparation::recordTouch(const QString& jobId, int touch, const V6d& joints)
{
  if (m_scan) {
    if (jobId != m_scan->id || m_scan->flushed || !m_preparedCalibration) return;
    if (touch != m_detections.size() + 1 || touch > m_preparedCalibration->touchCount || !joints.allFinite()) {
      failScan(tr("Unexpected scan detection %1 after %2 accepted rows.").arg(touch).arg(m_detections.size()));
      return;
    }
    m_detections.append(ProbeTouchDetection{touch, joints});
    emit calibrationRunChanged();
    return;
  }
  if (!m_recorder->armedJob() || m_recorder->armedJob()->jobId != jobId) return;
  if (m_run == CalibrationRun::Idle) return;
  for (const ProbeTouchDetection& detection : std::as_const(m_detections))
    if (detection.touch == touch) return;
  m_detections.append(ProbeTouchDetection{touch, joints});
  emit calibrationRunChanged();
}

void SceneMachiningPreparation::observeResume(int state, int step, int result)
{
  if (m_run != CalibrationRun::Resuming && m_run != CalibrationRun::ResumeStarting) return;
  const bool resumed = m_run == CalibrationRun::ResumeStarting && step == 0 && result == StartResultResumed;
  if (m_scan && resumed && state != ProgramRunning) return; // Await the resumed application's PRO_ACT.
  if (state == ProgramRunning || resumed) {
    m_run = CalibrationRun::Started;
  } else if (step != 0) {
    m_run = CalibrationRun::ResumeStarting;
  } else if (m_run == CalibrationRun::ResumeStarting) {
    m_run = CalibrationRun::Started;
    m_runError = tr("Resume failed: %1.").arg(StartResultNames.value(result, QString::number(result)));
  }
  if (m_scan && m_run == CalibrationRun::Started) m_scanRequestTimer.stop();
  emit calibrationRunChanged();
}

// The PLC sends robotResult with every 2 s heartbeat, so a start or resume failure is taken only when
// the start sequence of this run (robotStep above IDLE) returns to IDLE without program 1 running.
void SceneMachiningPreparation::onPlcData(const QVariantHash& values)
{
  if (!m_scan && (values.contains("resumeReply") || values.contains("scanStartReply")
                  || (values.contains("plcIoCurrent") && !values.contains("x1")))) return;
  if (m_scan) {
    observeScan(values);
    emit availabilityChanged();
    return;
  }
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
  observeResume(state, step, result);
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
  if (m_scan && !m_plc->isConnected()) {
    m_scan->requestPending = false;
    m_scan->recoverySeen = false;
    failScan(tr("PLC disconnected during the scan."));
    return;
  }
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
