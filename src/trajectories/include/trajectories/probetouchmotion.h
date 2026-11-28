#pragma once

#include "trajectories/probetouchplan.h"
#include "trajectories/timedmotion.h"
#include "geometry/refinecontacts.h"

#include <QString>
#include <optional>
#include <utility>
#include <variant>

enum class ProbeSegment { ToEntry, Descend, Transfer, Approach, Retract, Retreat, ReturnHome };

struct ProbeMotionPhase
{
  int pass = -1; // Index into ProbeTouchPlan::passes; -1 for the return to HOME.
  int touch = 0; // 1-based touch of Transfer, Approach and Retract; 0 otherwise.
  ProbeSegment segment = ProbeSegment::ToEntry;

  // "Calibration G1 pass 1: descend", "Calibration G2 pass 1, touch 5/19: approach", "Exit: return HOME".
  QString text(const ProbeTouchPlan& plan) const;
};

// Movement of the nominal path and its controller job encoding.
enum class ProbeRsiStepKind { Ptp = 1, Lin = 2, Circ = 3, Search = 4, Home = 5 };

// One motion step: its phase and the TCP pose at its end (unused by ReturnHome).
struct ProbeMotionStep
{
  ProbeMotionPhase phase;
  ProbeRsiStepKind kind;
  M4d target = M4d::Identity();
};

// Steps of the plan in execution order; ProbeTouchMotion stores one TimedMotion phase per step.
// A pass in another probe orientation is reached through the previous exit, a PTP to its entry and a
// descent; a pass in the same orientation by a transfer, omitted when it starts where the previous
// pass ended (a zero-length CIRC is degenerate).
QVector<ProbeMotionStep> probeMotionSteps(const ProbeTouchPlan& plan);

// Smallest sampled ball clearances during the sphere passes, mm.
struct ProbeClearances
{
  double transfer = 0.0; // To the sphere, outside Approach and Retract.
  double strut = 0.0; // To the strut, at every point.
};

struct ProbeTouchMotionResult;

// Timed animation of the probe touch plan, from HOME back to HOME.
class ProbeTouchMotion
{
public:
  static ProbeTouchMotionResult create(const ProbeTouchPlan& plan, const ChamferRobotSetup& robot);
  // Nominal touch poses of the calibration and verification passes that the motion executes.
  const ProbeTouchPlan& plan() const { return m_plan; }
  const QVector<ChamferJointPoint>& points() const { return m_timed.points(); }
  const ChamferRobotSetup& robot() const { return m_timed.robot(); }
  std::variant<M4d, QString> measuredTcp(const V6d& joints) const;
  double duration() const { return m_timed.duration(); }
  double minimumTransferClearance() const { return m_clearances.transfer; }
  double minimumStrutClearance() const { return m_clearances.strut; }
  // One playback segment and one phase per motion step, labelled with ProbeMotionPhase::text().
  const TimedMotion& timed() const { return m_timed; }

private:
  ProbeTouchMotion(TimedMotion timed, ProbeTouchPlan plan, const ProbeClearances& clearances)
    : m_timed(std::move(timed)), m_plan(std::move(plan)), m_clearances(clearances) {}
  TimedMotion m_timed;
  ProbeTouchPlan m_plan;
  ProbeClearances m_clearances;
};

struct ProbeTouchMotionResult
{
  std::optional<ProbeTouchMotion> motion;
  QString error;
};

// Numerical input owned by the worker during a standalone three-plane solve.
struct RefineScan
{
  std::array<RoboCrap3D::RefinePlaneContacts, 3> planes;
  ProbeGaugeMount mounting;
  double ballRadius = 0.0;
  std::optional<ProbeTouchPlanParameters> sphere;
  std::optional<Eigen::AlignedBox3d> tableTop;
  ProbeTouchMotionResult solve(const ChamferRobotSetup& robot, int group) const;
};
