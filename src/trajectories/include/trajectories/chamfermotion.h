#pragma once

#include "trajectories/chamfercentralmotion.h"
#include "trajectories/timedmotion.h"

struct ChamferMotionResult;

// The eight phase boundaries (point indices) of a chamfer motion, in order.
enum class ChamferBoundary : qsizetype {
  HomeStart,
  StagingIn,
  LeadStart,
  MachiningStart,
  MachiningEnd,
  LeadEnd,
  StagingOut,
  HomeEnd,
};
constexpr qsizetype ChamferBoundaryCount = 8;
constexpr qsizetype ChamferPhaseCount = 7; // The ranges between consecutive boundaries.

// Timed simulation motion; does not emulate controller-specific KRL interpolation.
class ChamferMotion
{
public:
  static ChamferMotionResult create(const ChamferPath& path, const ChamferRobotSetup& robot);
  const QVector<ChamferJointPoint>& points() const { return m_timed.points(); }
  // Inclusive ranges of the seven phases: HOME to P_s, P_s to lead-in, Lead-in, Machining, Lead-out,
  // Lead-out to P_s, P_s to HOME. Junction points are shared.
  qsizetype boundary(ChamferBoundary which) const { return m_boundaries[static_cast<std::size_t>(which)]; }
  const ChamferRobotSetup& robot() const { return m_timed.robot(); }
  const ChamferPathParameters& parameters() const { return m_centralMotion.path().parameters(); }
  double duration() const { return m_timed.duration(); }
  const ChamferCentralMotion& centralMotion() const { return m_centralMotion; }
  // The points with their playback segments (PTP smoothstep, LIN/central splines) and phase labels.
  const TimedMotion& timed() const { return m_timed; }

private:
  ChamferMotion(TimedMotion timed, const std::array<qsizetype, ChamferBoundaryCount>& boundaries,
                ChamferCentralMotion central);
  TimedMotion m_timed;
  std::array<qsizetype, ChamferBoundaryCount> m_boundaries{};
  ChamferCentralMotion m_centralMotion; // Also holds the path, so its parameters.
};

struct ChamferMotionResult
{
  std::optional<ChamferMotion> motion;
  QString error;
};
