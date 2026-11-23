#include "geometry/endgaugefit.h"
#include "geometry/plane.h"
#include "geometry/utils.h"

#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <limits>

const QStringList EndGaugeObservations::FaceNames{
    QStringLiteral("wide"), QStringLiteral("end"), QStringLiteral("top")};

EndGaugeFit EndGaugeObservations::fit() const
{
  if (!positiveFinite(ballRadius))
    return {{}, QStringLiteral("ballRadiusMm must be finite and positive.")};

  M3d normals;
  V3d offsets;
  for (int face = 0; face < 3; ++face) {
    const auto plane = Plane::fromPoints(ballCentres[face]);
    if (!plane)
      return {{}, QStringLiteral("%1 needs at least three finite, noncollinear ball centres.").arg(FaceNames[face])};
    // GE-22 material extends from x=0 toward +X, y=0 toward -Y, and z=0 toward -Z.
    const double alignment = plane->normal()[face] * (face == 0 ? -1.0 : 1.0);
    if (std::abs(alignment) < std::cos(15.0 * GeomConst::DegToRad))
      return {{}, QStringLiteral("%1 normal differs from its expected base axis by more than 15 degrees.")
                      .arg(FaceNames[face])};
    const double sign = alignment < 0.0 ? -1.0 : 1.0;
    normals.row(face) = sign * plane->normal().transpose();
    offsets[face] = -sign * plane->coeffs.w() - ballRadius;
  }

  QStringList angles;
  for (int face = 0; face < 3; ++face) {
    const int other = (face + 1) % 3;
    const double cosine = std::clamp(normals.row(face).dot(normals.row(other)), -1.0, 1.0);
    const double angle = std::acos(cosine) * GeomConst::RadToDeg;
    const QString pair = QStringLiteral("%1/%2 %3 deg").arg(FaceNames[face], FaceNames[other]).arg(angle, 0, 'f', 4);
    if (std::abs(angle - 90.0) > 5.0)
      return {{}, pair + QStringLiteral(" is outside 85 to 95 degrees.")};
    angles.append(pair);
  }

  const Eigen::JacobiSVD<M3d, Eigen::ComputeFullU | Eigen::ComputeFullV> solve(normals);
  if (solve.info() != Eigen::Success || solve.singularValues().z() < 0.1 * solve.singularValues().x())
    return {{}, QStringLiteral("The three material planes do not determine a stable corner.")};
  const V3d origin = solve.solve(offsets);
  const V3d up = normals.row(2).transpose();
  const auto width = prjUnitOnPlane(-normals.row(0).transpose(), up);
  if (!width || !origin.allFinite())
    return {{}, QStringLiteral("Cannot determine finite GE-22 axes and corner.")};
  const V3d length = up.cross(*width);
  if (!isBasis(*width, length, up) || length.dot(normals.row(1).transpose()) <= 0.0)
    return {{}, QStringLiteral("GE-22 axes disagree with the measured end face.")};
  const M4d placement = makeTransform(basis2rot({*width, length, up}), origin);
  auto result = checkContacts(placement, normals);
  result.diagnostic = angles.join(QStringLiteral(", ")) + QStringLiteral(". ") + result.diagnostic;
  return result;
}

EndGaugeFit EndGaugeObservations::checkContacts(const M4d& placement, const M3d& outwardNormals) const
{
  const V3d lower{0.0, -22.0, -35.0};
  const V3d upper{9.0, 0.0, 0.0};
  constexpr double roundingMm = 0.0001;
  const M3d baseToWorkpiece = placement.topLeftCorner<3, 3>().transpose();
  const V3d origin = placement.topRightCorner<3, 1>();
  QStringList patches;
  for (int face = 0; face < 3; ++face) {
    const V3d normal = outwardNormals.row(face).transpose();
    V3d minimum = V3d::Constant(std::numeric_limits<double>::infinity());
    V3d maximum = -minimum;
    double lowestBall = std::numeric_limits<double>::infinity();
    for (const auto& point : ballCentres[face]) {
      // Each fitted material plane passes through the recovered origin.
      const V3d relative = point - origin;
      const V3d contact = baseToWorkpiece * (relative - normal * normal.dot(relative));
      const double ballBottom = baseToWorkpiece.row(2).dot(relative) - ballRadius;
      if (!contact.allFinite() || !std::isfinite(ballBottom))
        return {{}, QStringLiteral("%1 contact projection is not finite.").arg(FaceNames[face])};
      minimum = minimum.cwiseMin(contact);
      maximum = maximum.cwiseMax(contact);
      lowestBall = std::min(lowestBall, ballBottom);
    }
    const int u = (face + 1) % 3;
    const int v = (face + 2) % 3;
    const double overEdge = std::max({0.0, lower[u] - minimum[u], maximum[u] - upper[u],
                                      lower[v] - minimum[v], maximum[v] - upper[v]});
    patches.append(QStringLiteral("%1 spread %2 x %3 mm, outside edge %4 mm")
                       .arg(FaceNames[face]).arg(maximum[u] - minimum[u]).arg(maximum[v] - minimum[v]).arg(overEdge));
    if (overEdge > roundingMm)
      return {{}, patches.join(QStringLiteral("; "))};
    const double excludedTop = lower.z() + 8.0;
    if (face != 2 && lowestBall < excludedTop - roundingMm)
      return {{}, patches.join(QStringLiteral("; ")) + QStringLiteral(". Ball enters clamp exclusion by %1 mm.")
                                                          .arg(excludedTop - lowestBall)};
  }
  return {placement, patches.join(QStringLiteral("; "))};
}
