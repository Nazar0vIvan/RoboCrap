#include "geometry/spherefit.h"
#include "geometry/utils.h"

#include <Eigen/SVD>

#include <cmath>

std::optional<SphereFit> fitSphere(const QVector<V3d>& points)
{
  if (points.size() < 4) return std::nullopt;

  const auto samples = centerPoints(points);
  if (!samples) return std::nullopt;
  const Eigen::MatrixXd& centered = samples->centered;

  // ||q||^2 = 2 q.c + k is linear in the centered center c and k = r^2 - ||c||^2.
  Eigen::MatrixXd system(points.size(), 4);
  system.leftCols(3) = 2.0 * centered;
  system.col(3).setOnes();
  const Eigen::VectorXd squares = centered.rowwise().squaredNorm();
  const Eigen::JacobiSVD<Eigen::MatrixXd, Eigen::ComputeThinU | Eigen::ComputeThinV> svd(system);
  if (svd.info() != Eigen::Success) return std::nullopt;

  // Coplanar points leave a family of spheres: the system loses rank.
  const Eigen::VectorXd& singularValues = svd.singularValues();
  if (!singularValues.allFinite() || singularValues[0] <= 0.0
      || singularValues[3] < GeomConst::Eps * singularValues[0]) return std::nullopt;

  const Eigen::VectorXd solution = svd.solve(squares);
  const V3d offset = solution.head<3>();
  const double squaredRadius = solution[3] + offset.squaredNorm();
  if (!std::isfinite(squaredRadius) || squaredRadius <= 0.0) return std::nullopt;

  SphereFit result{samples->centroid + offset, std::sqrt(squaredRadius), 0.0};
  double sum = 0.0;
  for (const V3d& point : points) {
    const double residual = (point - result.center).norm() - result.radius;
    sum += residual * residual;
  }
  result.rms = std::sqrt(sum / points.size());
  if (!result.center.allFinite() || !std::isfinite(result.rms)) return std::nullopt;
  return result;
}
