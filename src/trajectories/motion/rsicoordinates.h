#pragma once

#include "geometry/mathtypes.h"
#include "geometry/utils.h"
#include "kinematics/kr10kinematicmodel.h"

#include <cmath>
#include <optional>

// Coordinates written to the KR C4 (RSI corrections and KRL job files).

constexpr double RsiCyclePeriod = 0.004; // s, RSI IPO_FAST controller cycle.
constexpr int RsiSettleCycles = 50; // Zero corrections after a stream, before Result = 1.

// Principal KUKA A, B, C (Z-Y-X, degrees) of a rotation: B in [-90, 90]; C = 0 at B = +/-90.
inline V3d principalEulerDegrees(const M3d& rotation)
{
  // atan2 retains pitch accuracy near +/-90 degrees, where asin can lose it.
  const double cosinePitch = std::hypot(rotation(0, 0), rotation(1, 0));
  if (cosinePitch <= GeomConst::Eps)
    return {std::atan2(-rotation(0, 1), rotation(1, 1)) * GeomConst::RadToDeg,
            std::copysign(90.0, -rotation(2, 0)), 0.0};
  return {std::atan2(rotation(1, 0), rotation(0, 0)) * GeomConst::RadToDeg,
          std::atan2(-rotation(2, 0), cosinePitch) * GeomConst::RadToDeg,
          std::atan2(rotation(2, 1), rotation(2, 2)) * GeomConst::RadToDeg};
}

// The value the controller parses from six fixed decimal places.
inline V6d wireValues(const V6d& values)
{
  constexpr double WireScale = 1e6;
  return ((values * WireScale).array().round() / WireScale).matrix();
}

// Six-decimal XYZ and principal ABC of a pose; empty when the rounded values miss the pose by more
// than the KR10 position or rotation-entry tolerance.
inline std::optional<V6d> wirePose(const M4d& pose)
{
  V6d coordinates = V6d::Zero();
  coordinates << pose.block<3, 1>(0, 3), principalEulerDegrees(pose.block<3, 3>(0, 0));
  coordinates = wireValues(coordinates);
  const M3d rotation = euler2rot(coordinates[3], coordinates[4], coordinates[5]);
  if (!coordinates.allFinite() || (coordinates.head<3>() - pose.block<3, 1>(0, 3)).norm() > RoboCrap3D::Kr10PosEps
      || (rotation - pose.block<3, 3>(0, 0)).cwiseAbs().maxCoeff() > RoboCrap3D::Kr10RotEps)
    return std::nullopt;
  return coordinates;
}
