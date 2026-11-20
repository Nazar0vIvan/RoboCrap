#include "geometry/plane.h"
#include "geometry/utils.h"

#include <Eigen/SVD>

#include <cmath>

std::optional<Plane> Plane::fromPoints(const QVector<V3d>& points, double eps)
{
  if (points.size() < 3 || !std::isfinite(eps) || eps < 0.0) return std::nullopt;

  const auto samples = centerPoints(points);
  if (!samples) return std::nullopt;

  const Eigen::JacobiSVD<Eigen::MatrixXd, Eigen::ComputeFullV> svd(samples->centered);
  if (svd.info() != Eigen::Success) return std::nullopt;

  const V3d singularValues = svd.singularValues();
  // Two independent directions determine a plane; its thickness may be zero.
  if (!singularValues.allFinite() || singularValues.x() <= 0.0
      || singularValues.y() <= eps * singularValues.x()) return std::nullopt;

  const V3d normal = dominantPositive(svd.matrixV().col(2));

  const double d = -normal.dot(samples->centroid);

  const V4d coeffs{normal.x(), normal.y(), normal.z(), d};

  if (!coeffs.allFinite()) {
    return std::nullopt;
  }

  return Plane{coeffs};
}

V3d Plane::normal() const noexcept
{
  return coeffs.head<3>();
}

