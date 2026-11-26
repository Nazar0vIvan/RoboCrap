#pragma once

#include "geometry/mathtypes.h"

#include <QString>
#include <QPoint>
#include <QVector>
#include <optional>

// Distance from search start to nominal contact, along the accessible surface normal.
constexpr double ProbeApproachGap = 3.0; // mm from the start shell to the contact shell.

constexpr double ProbeSearchFeed = 1.0; // mm/s

enum class ProbeTouchPurpose { Calibration, Verification, Gauge, Scan };
enum class ProbeFace { Sphere, A, B, Refine };

// Sphere and strut of the calibration fixture, mm in BASE.
struct ProbeTouchPlanParameters
{
  V3d sphereCentre = V3d::Zero();
  double sphereRadius = 12.5;
  double ballRadius = 1.5; // Ruby ball of the measuring TCP.
  double strutRadius = 8.9; // Vertical strut on the axis through the sphere centre.
  double strutRecess = 2.0; // Strut top above the lowest sphere point.
  double searchFeed = ProbeSearchFeed; // mm/s, constant during every Approach; Retract uses the transfer limits.

  // Ball-centre distance from the sphere centre at contact.
  double contactDistance() const { return sphereRadius + ballRadius; }
  // Distance from the ball to the strut solid, which extends downward from its top face.
  double strutClearance(const V3d& ball) const;
};

// Nominal search endpoints in robot base coordinates.
struct ProbeTouch
{
  M4d start = M4d::Identity();
  M4d contact = M4d::Identity();
  V3d normal = V3d::Zero(); // Outward surface normal at the contact.
  double tilt = 0.0; // deg, between the stylus and the approach direction.
  double roll = 0.0; // deg, about the stylus.
  int facePoint = 0; // Original 1-based point on a planar face, independent of traversal order.
  double overtravel = 2.0; // Maximum inward travel beyond predicted contact, mm.
  QPoint cell; // Original 1-based grid cell of a Refine Job.
  V3d localMaterialPoint = V3d::Zero(); // Refine Job material point in workpiece coordinates, mm.
  double searchDistance() const { return (start.col(3) - contact.col(3)).norm() + overtravel; }
};

// One execution of a group's direction list in the group's fixed probe orientation.
struct ProbeTouchPass
{
  ProbeTouchPurpose purpose = ProbeTouchPurpose::Calibration;
  int group = 1; // G1 stylus down, G2 stylus along +X, G3 stylus down turned 180 deg about it.
  int repetition = 1;
  M4d entry = M4d::Identity(); // Outward and above the first touch.
  QVector<ProbeTouch> touches;
  M4d exit = M4d::Identity(); // Outward along the last normal.

  ProbeFace face = ProbeFace::Sphere;
  // Optional plane in BASE for a joint move to entry. The unit normal points to the allowed
  // side; the offset already includes the ruby radius and required surface clearance (mm).
  std::optional<V4d> entryClearancePlane = std::nullopt;
  QString role;
  int sourceFace = 0;

  // "Calibration G2 pass 1" or "GE-22 face A G2 pass 1".
  QString label() const;
};

struct ProbeGaugeMount
{
  M4d workpieceInBase = M4d::Identity();
  QString source;
  QString observationsSha256;
};

struct ProbeTouchPlan
{
  std::optional<ProbeTouchPlanParameters> sphere; // Physical fixture when present in the scene.
  QVector<ProbeTouchPass> passes; // Calibration passes first, then verification passes.
  std::optional<ProbeGaugeMount> gauge = std::nullopt;
  std::optional<Eigen::AlignedBox3d> tableTop; // Finite rectangle in BASE, mm.

  double searchFeed() const { return sphere ? sphere->searchFeed : ProbeSearchFeed; }
  int touchCount(ProbeTouchPurpose purpose) const;
};

struct ProbeTouchPlanResult
{
  std::optional<ProbeTouchPlan> plan;
  QString error;
};

ProbeTouchPlanResult createProbeTouchPlan(const ProbeTouchPlanParameters& parameters);
ProbeTouchPlanResult createGaugeTouchPlan(const M4d& workpieceInBase, double ballRadius);
