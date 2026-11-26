#include "trajectories/probetouchmotion.h"

#include "../motion/motionbuilder.h"

#include <algorithm>
#include <limits>
#include <utility>

using namespace RoboCrap3D;

namespace {

constexpr double ClearanceTolerance = 0.001; // mm; covers the 1e-4 mm FK reconstruction of stored poses.

struct BuildResult
{
  std::optional<TimedMotion> motion;
  ProbeClearances clearances;
  QString error;
};

// Segments inside the start shell, exempt from the sphere clearance check.
bool touchSegment(ProbeSegment segment)
{
  return segment == ProbeSegment::Approach || segment == ProbeSegment::Retract;
}

// Builds the timed points of one touch plan; borrows the plan and the worker-owned robot setup.
class TouchMotionBuilder
{
public:
  TouchMotionBuilder(const ProbeTouchPlan& plan, const ChamferRobotSetup& robot)
    : m_plan(plan), m_robot(robot), m_motion(robot) {}

  BuildResult build()
  {
    m_clearances = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    for (const auto& step : probeMotionSteps(m_plan)) {
      const qsizetype first = m_motion.points().size() - 1;
      QString error = append(step);
      if (error.isEmpty() && step.kind == ProbeRsiStepKind::Ptp) {
        const auto& plane = m_plan.passes[step.phase.pass].entryClearancePlane;
        if (plane) error = entryClearanceError(*plane, first);
      }
      if (error.isEmpty()) error = clearanceError(step.phase, first);
      if (error.isEmpty()) continue;
      // MotionBuilder errors already start with the phase text.
      const QString label = step.phase.text(m_plan);
      return {std::nullopt, {}, error.startsWith(label) ? error : label + QStringLiteral(": ") + error};
    }
    return {m_motion.finish(), m_clearances, {}};
  }

private:
  QString append(const ProbeMotionStep& step)
  {
    const QString label = step.phase.text(m_plan);
    if (step.kind == ProbeRsiStepKind::Home) return m_motion.appendPtp(m_motion.home(), label);
    if (step.kind == ProbeRsiStepKind::Ptp) {
      const auto solved = m_motion.solver().solveTcp(step.target, m_motion.seed());
      if (!solved.solution) return solved.error;
      return m_motion.appendPtp(*solved.solution, label);
    }
    const auto& timing = m_robot.timing;
    // Only the Approach measures: constant search feed without ramps, interpolated joint-linearly in
    // playback. Retract and the other LIN/CIRC moves use the transfer limits.
    const bool searching = step.kind == ProbeRsiStepKind::Search;
    const LinearLimits limits = searching ? LinearLimits{m_plan.searchFeed(), 0.0}
                                          : LinearLimits{timing.transferFeed, timing.transferAcceleration};
    auto profile = linearProfile(m_motion.points().back().tcp, step.target, limits);
    // Transfers are CIRC arcs on the start shell around the sphere centre.
    if (profile && step.kind == ProbeRsiStepKind::Circ)
      profile = circularProfile(*profile, m_plan.sphere->sphereCentre);
    if (!profile) return QStringLiteral("Invalid or degenerate LIN or CIRC profile.");
    return m_motion.appendPath(LinearPath(*profile, label),
                               searching ? MotionInterpolation::JointLinear : MotionInterpolation::Spline);
  }

  // PTP traverses a straight line in joint coordinates. Over one stored interval, using joint
  // fraction 0..1, each rotated link's second derivative is bounded by its length times the
  // square of the sum of absolute joint rotations (radians). The chord error is at most M/8
  // for a second derivative bounded by M. This also bounds the error in signed plane height.
  QString entryClearanceError(const V4d& plane, qsizetype first) const
  {
    double length = m_robot.flangeToTcp.topRightCorner<3, 1>().norm();
    for (const auto& joint : m_robot.model.joints)
      length += joint.localTransform.topRightCorner<3, 1>().norm();
    const auto& points = m_motion.points();
    double previous = plane.dot(points[first].tcp.col(3));
    for (qsizetype i = first + 1; i < points.size(); ++i) {
      const double height = plane.dot(points[i].tcp.col(3));
      const double angle = (points[i].joints - points[i - 1].joints).cwiseAbs().sum() * GeomConst::DegToRad;
      const double margin = std::min(previous, height) - length * angle * angle / 8.0;
      if (!(margin >= 0.0))
        return QStringLiteral("Cannot maintain the required ball clearance above the gauge top "
                              "during reorientation at %1 to %2 s (clearance margin %3 mm).")
            .arg(points[i - 1].time).arg(points[i].time).arg(margin, 0, 'f', 3);
      previous = height;
    }
    return {};
  }

  // The final HOME move belongs to the last fixture. The move to a new fixture belongs to its entry pass.
  QString clearanceError(const ProbeMotionPhase& phase, qsizetype first)
  {
    const auto& pass = phase.pass < 0 ? m_plan.passes.back() : m_plan.passes[phase.pass];
    const bool scan = pass.purpose == ProbeTouchPurpose::Scan;
    if (!m_plan.sphere || (!scan && pass.face != ProbeFace::Sphere)) return {};
    const auto& parameters = *m_plan.sphere;
    const auto& points = m_motion.points();
    const double contact = parameters.contactDistance();
    const double required = ProbeApproachGap - ClearanceTolerance;
    for (qsizetype i = first + (scan ? 0 : 1); i < points.size(); ++i) {
      const auto& point = points[i];
      const V3d ball = point.tcp.block<3, 1>(0, 3);
      if (scan && m_plan.tableTop) {
        const double gap = m_plan.tableTop->exteriorDistance(ball) - parameters.ballRadius;
        if (gap < required)
          return QStringLiteral("ball clearance to the table top %1 mm is below %2 mm at %3 s.")
              .arg(gap, 0, 'f', 4).arg(required).arg(point.time);
      }
      const double strut = parameters.strutClearance(ball);
      if (strut < required)
        return QStringLiteral("strut clearance %1 mm is below %2 mm at %3 s.")
            .arg(strut, 0, 'f', 4).arg(required, 0, 'f', 4).arg(point.time, 0, 'g', 12);
      m_clearances.strut = std::min(m_clearances.strut, strut);
      if (!scan && touchSegment(phase.segment)) continue;
      const double gap = (ball - parameters.sphereCentre).norm() - contact;
      if (gap < required)
        return QStringLiteral("ball clearance to the sphere %1 mm is below %2 mm at %3 s.")
            .arg(gap, 0, 'f', 4).arg(required, 0, 'f', 4).arg(point.time, 0, 'g', 12);
      m_clearances.transfer = std::min(m_clearances.transfer, gap);
    }
    return {};
  }

  const ProbeTouchPlan& m_plan;
  const ChamferRobotSetup& m_robot;
  MotionBuilder m_motion;
  ProbeClearances m_clearances;
};

} // namespace

QVector<ProbeMotionStep> probeMotionSteps(const ProbeTouchPlan& plan)
{
  QVector<ProbeMotionStep> steps;
  for (int index = 0; index < plan.passes.size(); ++index) {
    const ProbeTouchPass& pass = plan.passes[index];
    const M4d& first = pass.touches.front().start;
    const auto transfer = pass.face == ProbeFace::Sphere ? ProbeRsiStepKind::Circ : ProbeRsiStepKind::Lin;
    if (index == 0 || pass.group != plan.passes[index - 1].group || pass.face != plan.passes[index - 1].face
        || pass.sourceFace != plan.passes[index - 1].sourceFace) {
      if (index > 0)
        steps.append(ProbeMotionStep{{index - 1, 0, ProbeSegment::Retreat}, ProbeRsiStepKind::Lin,
                                     plan.passes[index - 1].exit});
      const auto toEntry = index > 0 && pass.purpose == ProbeTouchPurpose::Scan
          ? ProbeRsiStepKind::Lin : ProbeRsiStepKind::Ptp;
      steps.append(ProbeMotionStep{{index, 0, ProbeSegment::ToEntry}, toEntry, pass.entry});
      steps.append(ProbeMotionStep{{index, 0, ProbeSegment::Descend}, ProbeRsiStepKind::Lin, first});
    } else if (first != plan.passes[index - 1].touches.back().start) {
      steps.append(ProbeMotionStep{{index, 1, ProbeSegment::Transfer}, transfer, first});
    }
    int number = 0;
    for (const ProbeTouch& touch : pass.touches) {
      ++number;
      if (number > 1) steps.append(ProbeMotionStep{{index, number, ProbeSegment::Transfer}, transfer, touch.start});
      steps.append(ProbeMotionStep{{index, number, ProbeSegment::Approach}, ProbeRsiStepKind::Search, touch.contact});
      steps.append(ProbeMotionStep{{index, number, ProbeSegment::Retract}, ProbeRsiStepKind::Lin, touch.start});
    }
  }
  steps.append(ProbeMotionStep{{static_cast<int>(plan.passes.size()) - 1, 0, ProbeSegment::Retreat}, ProbeRsiStepKind::Lin,
                               plan.passes.back().exit});
  steps.append(ProbeMotionStep{{-1, 0, ProbeSegment::ReturnHome}, ProbeRsiStepKind::Home, M4d::Identity()});
  return steps;
}

QString ProbeMotionPhase::text(const ProbeTouchPlan& plan) const
{
  static const std::array<const char*, 7> names{"to entry", "descend", "transfer", "approach", "retract",
                                                "retreat", "return HOME"};
  const QString name = QString::fromLatin1(names[static_cast<std::size_t>(segment)]);
  if (pass < 0 && (plan.passes.isEmpty() || plan.passes.back().purpose != ProbeTouchPurpose::Scan))
    return QStringLiteral("Exit: ") + name;
  const ProbeTouchPass& owner = pass < 0 ? plan.passes.back() : plan.passes[pass];
  if (owner.purpose == ProbeTouchPurpose::Scan) {
    const bool leaving = segment == ProbeSegment::Retreat || segment == ProbeSegment::ReturnHome;
    const auto cell = touch > 0 ? owner.touches[touch - 1].cell : (leaving ? owner.touches.back() : owner.touches.front()).cell;
    return QStringLiteral("%1 (%2,%3): %4").arg(owner.label()).arg(cell.x()).arg(cell.y()).arg(name);
  }
  if (touch > 0 && owner.purpose == ProbeTouchPurpose::Gauge)
    return QStringLiteral("%1, point %2: %3").arg(owner.label()).arg(owner.touches[touch - 1].facePoint).arg(name);
  if (touch > 0)
    return QStringLiteral("%1, touch %2/%3: %4").arg(owner.label()).arg(touch).arg(owner.touches.size()).arg(name);
  return owner.label() + QStringLiteral(": ") + name;
}

ProbeTouchMotionResult ProbeTouchMotion::create(const ProbeTouchPlan& plan, const ChamferRobotSetup& robot)
{
  const auto rated = ratedRobotSetup(robot);
  if (!rated.robot) return {{}, rated.error};
  if (plan.passes.isEmpty()) return {{}, QStringLiteral("The touch plan has no passes.")};
  for (const ProbeTouchPass& pass : plan.passes) {
    if (pass.touches.isEmpty()) return {{}, pass.label() + QStringLiteral(": the pass has no touches.")};
    if (pass.face == ProbeFace::Sphere && !plan.sphere) return {{}, QStringLiteral("Sphere pass without a sphere fixture.")};
  }
  auto built = TouchMotionBuilder(plan, *rated.robot).build();
  if (!built.error.isEmpty()) return {{}, built.error};
  return {ProbeTouchMotion(std::move(*built.motion), plan, built.clearances), {}};
}
