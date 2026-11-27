#include "trajectories/probersistream.h"

#include "../motion/rsicoordinates.h"
#include "../motion/jointsegments.h"
#include <limits>
#include "geometry/utils.h"
#include "geometry/endgaugefit.h"
#include "kinematics/kr10kinematics.h"

#include <QDateTime>
#include <QStringList>
#include <QUuid>

#include <cmath>
#include <utility>

namespace {

using namespace RoboCrap3D;

constexpr qsizetype StepLimit = 600; // Array size in RSI_Probe_Calibration.dat.


// Constant feed along the inward normal for the planned search distance, then settle zeros. Each
// increment is the difference of consecutive rounded offsets, so the increments add up exactly to
// the rounded end offset.
QVector<V6d> searchStream(const ProbeTouch& touch, double feed)
{
  const double step = feed * RsiCyclePeriod;
  const double distance = touch.searchDistance();
  const double count = std::ceil(distance / step - 1e-9);
  if (!(count >= 1 && count <= std::numeric_limits<int>::max() - RsiSettleCycles)) return {};
  const int cycles = static_cast<int>(count);
  QVector<V6d> stream(cycles + RsiSettleCycles, V6d::Zero());
  V6d previous = V6d::Zero();
  for (int cycle = 0; cycle < cycles; ++cycle) {
    V6d offset = V6d::Zero();
    offset.head<3>() = -std::min(distance, (cycle + 1) * step) * touch.normal;
    offset = wireValues(offset);
    stream[cycle] = offset - previous;
    previous = offset;
  }
  return stream;
}

QString csvPose(const M4d& pose)
{
  QStringList fields;
  for (int axis = 0; axis < 3; ++axis) fields.append(QString::number(pose(axis, 3), 'f', 9));
  for (double angle : principalEulerDegrees(pose.block<3, 3>(0, 0))) fields.append(QString::number(angle, 'f', 9));
  return fields.join(QLatin1Char(';'));
}

class CalibrationJobSampler
{
public:
  // The saved motion outlives this worker-local sampler.
  explicit CalibrationJobSampler(const ProbeTouchMotion& motion)
    : m_motion(motion), m_kinematics(motion.robot().model) {}

  ProbeCalibrationRsiResult run()
  {
    const auto steps = probeMotionSteps(m_motion.plan());
    const auto& phases = m_motion.timed().phases();
    if (steps.size() != phases.size())
      return {{}, QStringLiteral("The probe path has %1 phases for %2 motion steps. Generate it again.")
                      .arg(phases.size()).arg(steps.size())};
    if (steps.size() > StepLimit)
      return {{}, QStringLiteral("%1 steps exceed the controller DAT limit of %2.")
                      .arg(steps.size()).arg(StepLimit)};
    const auto tool = wirePose(m_motion.robot().flangeToTcp);
    if (!tool) return {{}, QStringLiteral("Cannot represent the measuring flange-to-TCP transform at six decimal places.")};
    m_job.toolCoordinates = *tool;
    // Pairs the stream table with its job; the job_id of the sessions that run it.
    const bool scan = m_motion.plan().passes.front().purpose == ProbeTouchPurpose::Scan;
    m_job.diagnosticId = scan ? QStringLiteral("scan_") + QUuid::createUuid().toString(QUuid::WithoutBraces)
        : QStringLiteral("calibration_")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    m_job.streams.append(QVector<V6d>{}); // Touch numbers start at 1.
    for (qsizetype index = 0; index < steps.size(); ++index) {
      const QString error = appendStep(steps[index], phases[index]);
      if (!error.isEmpty()) return {{}, steps[index].phase.text(m_motion.plan()) + QStringLiteral(": ") + error};
    }
    m_job.touchCount = static_cast<int>(m_job.streams.size() - 1);
    m_job.plan = (QStringLiteral("touch;search_step;purpose;group;repetition;pass_touch;tilt_deg;roll_deg;normal_x;"
                                 "normal_y;normal_z;start_x_mm;start_y_mm;start_z_mm;start_a_deg;start_b_deg;start_c_deg;"
                                 "contact_x_mm;contact_y_mm;contact_z_mm;contact_a_deg;contact_b_deg;contact_c_deg;"
                                 "start_s;start_t;face;face_point")
                  + (scan ? QStringLiteral(";pass;role;source_face;u;v;local_x_mm;local_y_mm;local_z_mm;"
                                           "direction_x;direction_y;direction_z;search_mm;overtravel_mm") : QString{})
                  + QLatin1Char('\n')
                  + m_planRows).toUtf8();
    return {std::move(m_job), {}};
  }

private:
  QString appendStep(const ProbeMotionStep& step, const MotionPhaseRange& range)
  {
    const auto& points = m_motion.points();
    const auto kind = step.kind;
    if (range.first < 0 || range.last < range.first || range.last >= points.size()
        || range.label != step.phase.text(m_motion.plan())) return QStringLiteral("saved phase mismatch.");
    if (kind != ProbeRsiStepKind::Home && !samePose(points[range.last].tcp, step.target))
      return QStringLiteral("saved animation endpoint differs from the step target.");
    if (kind == ProbeRsiStepKind::Home) {
      m_job.steps.append(ProbeRsiStep{ProbeRsiStepKind::Home});
      return {};
    }
    if (kind == ProbeRsiStepKind::Search) {
      return appendSearch(step.phase, points[range.first]);
    }
    auto exported = poseStep(kind, points[range.last]);
    if (!exported) return QStringLiteral("the end pose cannot be represented at six decimal places.");
    if (kind == ProbeRsiStepKind::Circ) {
      const auto auxiliary = arcMidpoint(points[range.first], points[range.last]);
      if (!auxiliary) return QStringLiteral("no CIRC auxiliary point (arc of 180 degrees or rounding).");
      exported->auxiliary = *auxiliary;
    }
    m_job.steps.append(*exported);
    return {};
  }

  // The touch's stream, its Search step and its row of plan.csv.
  QString appendSearch(const ProbeMotionPhase& phase, const ChamferJointPoint& start)
  {
    const ProbeTouchPlan& plan = m_motion.plan();
    const ProbeTouchPass& pass = plan.passes[phase.pass];
    const ProbeTouch& touch = pass.touches[phase.touch - 1];
    const int number = static_cast<int>(m_job.streams.size());
    const V3d gap = (touch.start - touch.contact).topRightCorner<3, 1>();
    if ((gap - gap.norm() * touch.normal).norm() > Kr10PosEps || touch.overtravel < 0.0
        || !samePose(start.tcp, touch.start))
      return QStringLiteral("search start or normal does not match the saved approach.");
    auto stream = searchStream(touch, plan.searchFeed());
    if (stream.isEmpty()) return QStringLiteral("invalid search distance or feed.");
    m_job.streams.append(std::move(stream));
    m_job.steps.append(ProbeRsiStep{ProbeRsiStepKind::Search, number});
    const auto configuration = m_kinematics.configuration(start.joints, m_kinematics.solveFK(start.joints));
    const bool scan = pass.purpose == ProbeTouchPurpose::Scan;
    static const std::array<const char*, 4> purposes{"calibration", "verification", "gauge", "scan"};
    const QString purpose = QString::fromLatin1(purposes.at(static_cast<std::size_t>(pass.purpose)));
    m_planRows += QStringList{QString::number(number), QString::number(m_job.steps.size()), purpose,
                              QString::number(pass.group), QString::number(pass.repetition),
                              QString::number(phase.touch), QString::number(touch.tilt, 'f', 6),
                              QString::number(touch.roll, 'f', 6), QString::number(touch.normal.x(), 'f', 9),
                              QString::number(touch.normal.y(), 'f', 9), QString::number(touch.normal.z(), 'f', 9),
                              csvPose(touch.start), csvPose(touch.contact), QString::number(configuration.status),
                              QString::number(configuration.turn)}.join(QLatin1Char(';'))
                  + QLatin1Char(';');
    if (scan) {
      QStringList fields{EndGaugeObservations::FaceNames.at(phase.pass), QString{}, QString::number(phase.pass + 1), pass.role,
                         QString::number(pass.sourceFace), QString::number(touch.cell.x()), QString::number(touch.cell.y())};
      for (double value : touch.localMaterialPoint) fields.append(QString::number(value, 'g', 17));
      for (double value : touch.normal) fields.append(QString::number(-value, 'f', 9));
      fields << QString::number(touch.searchDistance(), 'g', 17) << QString::number(touch.overtravel, 'g', 17);
      m_planRows += fields.join(QLatin1Char(';'));
    } else if (pass.face == ProbeFace::Sphere) {
      m_planRows += QLatin1Char(';');
    } else {
      m_planRows += QStringLiteral("%1;%2").arg(pass.face == ProbeFace::A ? "A" : "B").arg(touch.facePoint);
    }
    m_planRows += QLatin1Char('\n');
    return {};
  }

  std::optional<ProbeRsiStep> poseStep(ProbeRsiStepKind kind, const ChamferJointPoint& point) const
  {
    const auto coordinates = wirePose(point.tcp);
    if (!coordinates) return std::nullopt;
    const auto configuration = m_kinematics.configuration(point.joints, m_kinematics.solveFK(point.joints));
    return ProbeRsiStep{kind, 0, *coordinates, V6d::Zero(), configuration.status, configuration.turn};
  }

  // Start-shell point halfway along the transfer arc about the sphere centre, with the target's
  // orientation; the plan's arcs are shorter than 180 degrees.
  std::optional<V6d> arcMidpoint(const ChamferJointPoint& start, const ChamferJointPoint& end) const
  {
    const auto& timing = m_motion.robot().timing;
    const auto chord = linearProfile(start.tcp, end.tcp, {timing.transferFeed, timing.transferAcceleration});
    if (!chord) return {};
    const auto arc = circularProfile(*chord, m_motion.plan().sphere->sphereCentre);
    if (!arc) return {};
    M4d pose = end.tcp;
    pose.col(3) = arc->target(arc->duration() / 2.0).col(3);
    return wirePose(pose);
  }

  const ProbeTouchMotion& m_motion;
  Kr10Kinematics m_kinematics;
  ProbeCalibrationRsi m_job;
  QString m_planRows;
};

} // namespace

ProbeCalibrationRsiResult sampleProbeCalibrationRsi(const ProbeTouchMotion& motion)
{
  const auto& plan = motion.plan();
  if (!plan.sphere || !plan.gauge || !plan.touchCount(ProbeTouchPurpose::Calibration)
      || !plan.touchCount(ProbeTouchPurpose::Verification) || !plan.touchCount(ProbeTouchPurpose::Gauge))
    return {{}, QStringLiteral("Generate the combined sphere and GE-22 calibration path.")};
  return CalibrationJobSampler(motion).run();
}

ProbeCalibrationRsiResult sampleProbeScanRsi(const ProbeTouchMotion& motion)
{
  const auto& plan = motion.plan();
  if (!plan.gauge || plan.passes.size() != 3)
    return {{}, QStringLiteral("Generate the three-plane EG22 Scan Path.")};
  const int group = plan.passes.front().group;
  for (const auto& pass : plan.passes)
    if (pass.purpose != ProbeTouchPurpose::Scan || pass.face != ProbeFace::Refine || pass.touches.isEmpty()
        || pass.group != group || (group != 1 && group != 2))
      return {{}, QStringLiteral("The three scan passes must use the same G1 or G2 orientation.")};
  return CalibrationJobSampler(motion).run();
}
