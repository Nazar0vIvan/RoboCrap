#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <QJsonValue>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVector>

#include "mathtypes.h"


constexpr qsizetype MaximumProbeSamples = 10000; // Points of one imported probe file.

struct ProbeSamples {
  QVector<V3d> points;
  double radius = 0.0;
  int dir = 1;

  // Finite nonnegative probe radius and dir equal to 1 or -1.
  bool hasValidCompensation() const
  {
    return std::isfinite(radius) && radius >= 0.0 && (dir == 1 || dir == -1);
  }
};

// Legacy arrays have zero compensation; objects require points, radius and dir.
// Empty for more than MaximumProbeSamples points.
std::optional<ProbeSamples> decodeProbeSamples(const QJsonDocument& document);

bool nearlyEqual(double lhs, double rhs, double eps = GeomConst::Eps);
bool positiveFinite(double value);

// The direction with its largest-magnitude component positive; ties go to the lower index.
V3d dominantPositive(V3d direction);
// UnitX, or UnitY when direction is close to X: the vector projected to build a perpendicular tangent.
V3d referenceAxis(const V3d& direction);

struct CenteredPoints {
  Eigen::MatrixXd centered; // One row per point, minus the centroid.
  V3d centroid = V3d::Zero();
};

// Rows relative to the first point to avoid summing large world-coordinate offsets, then
// minus their mean; empty for no points or any nonfinite point, centroid or row.
std::optional<CenteredPoints> centerPoints(const QVector<V3d>& points);

V3d toV3d(const std::array<double, 3>& point);
std::array<double, 3> toArray(const V3d& value);
std::optional<V3d> normalize(const V3d& v, double eps = GeomConst::Eps);

M4d makeTranslation(const V3d& delta);
M4d makeRotation(const double angleDeg, const Axis axis);
// Finite angle and finite nonzero axis; numerical callers validate their inputs.
// Unlike the principal-axis overload, this retains small rotation components.
M4d makeRotation(double angleDeg, const V3d& axis);
M4d makeTransform(const M3d& rot, const V3d& origin);
// Inverse of a rigid transform: rotation must be orthonormal, with no scale/shear.
M4d inverseRigidTransform(const M4d& transform);

bool isBasis(const V3d& v1, const V3d& v2, const V3d& v3, double eps = GeomConst::Eps);
std::optional<OrthoBasis> vecs2basis(const V3d& v1, const V3d& v2, const V3d& v3, double eps = GeomConst::Eps);
M3d basis2rot(const OrthoBasis& orthobasis);
EulerSolution rot2euler(const M3d& rot);
M3d euler2rot(const double A, const double B, const double C);

std::optional<V3d> prjUnitOnPlane(const V3d& vec, const V3d& normal);
std::optional<V3d> prjPointToPlane(const V3d& point, const V4d& planeCoeffs);
