#pragma once

#include "trajectories/chamferarclength.h"

#include <array>

struct ChamferTimingParameters;

struct ChamferCentralProfile
{
  double length = 0.0; // TCP-origin path length, mm.
  double speed = 0.0; // Machining/cruise speed, mm/s.
  double rampDistance = 0.0; // First lead-in or last lead-out distance, mm.
  double rampSeconds = 0.0;
  double cruiseSeconds = 0.0;
  double acceleration = 0.0; // Tangential ramp magnitude, mm/s^2; zero for machining.

  double duration() const { return rampSeconds + cruiseSeconds; }
  // Phase-local time of the ramp/cruise switch; none for machining or a ramp-only lead.
  std::optional<double> switchSeconds(ChamferPhase phase) const
  {
    if (phase == ChamferPhase::Machining || cruiseSeconds <= 0.0) return std::nullopt;
    return phase == ChamferPhase::LeadIn ? rampSeconds : cruiseSeconds;
  }
};

struct ChamferCentralPose
{
  M4d tcp = M4d::Identity(); // BASE-relative analytical pose.
  double progress = 0.0; // Geometric phase parameter in [0, 1].
};

struct ChamferCentralMotionResult;

// Prescribed Cartesian motion only. IK validation and controller sampling are separate.
class ChamferCentralMotion
{
public:
  // Only machiningFeed and accelerationDistanceFraction define central timing.
  static ChamferCentralMotionResult create(const ChamferPath& path,
                                          const ChamferTimingParameters& timing);

  const ChamferPath& path() const { return m_path; }
  const std::array<double, 4>& phaseTimes() const { return m_phaseTimes; }
  double duration() const { return m_phaseTimes.back(); }
  std::optional<ChamferCentralProfile> profile(ChamferPhase phase) const;
  // Seconds from lead-in start. Reject times outside [0, duration()]. Shared
  // junctions belong to the following phase; the final endpoint belongs to lead-out.
  std::optional<ChamferCentralPose> evaluate(double seconds) const;
  // Phase-local seconds; retains the explicit full-turn machining endpoint.
  // Acceleration is right-sided at switches, and zero at the final stop.
  std::optional<ChamferCentralPose> evaluatePhase(ChamferPhase phase, double seconds) const;

private:
  ChamferCentralMotion(const ChamferPath& path, std::array<ChamferArcLength, 3> lengths,
                       const ChamferTimingParameters& timing);
  QString initializeTiming(double fraction);

  ChamferPath m_path;
  std::array<ChamferArcLength, 3> m_lengths;
  std::array<ChamferCentralProfile, 3> m_profiles{};
  std::array<double, 4> m_phaseTimes{};
};

struct ChamferCentralMotionResult
{
  std::optional<ChamferCentralMotion> motion;
  QString error;
};
