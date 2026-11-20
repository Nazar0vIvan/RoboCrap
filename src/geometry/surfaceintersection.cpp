#include "geometry/surfaceintersection.h"
#include "geometry/utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

V3d normal(const BoundedPlane& plane)
{
  const auto& coefficients = plane.coefficients();
  return {coefficients[0], coefficients[1], coefficients[2]};
}

// Local size sets the geometric tolerance; coordinate magnitude accounts for
// subtraction roundoff without changing the project's global tolerances.
double distanceTolerance(double size, double magnitude)
{
  return GeomConst::Eps * std::max(1.0, size)
      + 32.0 * std::numeric_limits<double>::epsilon() * magnitude;
}

bool unit(const V3d& direction)
{
  return direction.allFinite() && std::abs(direction.squaredNorm() - 1.0) <= GeomConst::Eps;
}

IntersectionResult result(std::optional<EdgeGeometry> edge)
{
  if (!edge) return {};
  return {IntersectionStatus::Success, std::move(edge)};
}

} // namespace

EdgeGeometry::EdgeGeometry(Ellipse ellipse) : m_ellipse(std::move(ellipse)) {}

std::optional<EdgeGeometry> EdgeGeometry::fromEllipse(Ellipse ellipse)
{
  if (!ellipse.center.allFinite() || !unit(ellipse.majorAxis) || !unit(ellipse.minorAxis)
      || std::abs(ellipse.majorAxis.dot(ellipse.minorAxis)) > GeomConst::Eps
      || !std::isfinite(ellipse.majorRadius) || !std::isfinite(ellipse.minorRadius)
      || ellipse.minorRadius <= GeomConst::Eps || ellipse.majorRadius < ellipse.minorRadius)
    return std::nullopt;
  return EdgeGeometry(std::move(ellipse));
}

IntersectionResult intersectSurfaces(const BoundedPlane& plane, const BoundedCylinder& cylinder)
{
  const V3d n = normal(plane);
  const V3d axis = toV3d(cylinder.axis());
  const V3d origin = toV3d(cylinder.origin());
  const V3d planeOrigin = toV3d(plane.origin());
  const double cosine = n.dot(axis);
  if (!std::isfinite(cosine)) return {};
  if (std::abs(cosine) <= GeomConst::Eps) return {IntersectionStatus::ParallelCylinderAxis, {}};
  const double size = std::max({plane.width(), plane.height(), cylinder.length(), cylinder.radius()});
  // Solve locally before translating the center to world coordinates.
  const V3d centerOffset = axis * (n.dot(planeOrigin - origin) / cosine);
  EdgeGeometry::Ellipse ellipse;
  ellipse.center = centerOffset;
  ellipse.minorRadius = cylinder.radius();
  ellipse.majorRadius = cylinder.radius() / std::min(1.0, std::abs(cosine));
  const V3d projected = axis - cosine * n;
  const double sine = projected.stableNorm();
  if (sine <= GeomConst::Eps) {
    ellipse.majorAxis = toV3d(plane.axisU());
    ellipse.majorRadius = ellipse.minorRadius;
  } else {
    ellipse.majorAxis = dominantPositive(projected / sine);
  }
  ellipse.minorAxis = n.cross(ellipse.majorAxis).normalized();

  ellipse.center += origin;
  const double residualTolerance = distanceTolerance(size,
      std::max({origin.cwiseAbs().maxCoeff(), planeOrigin.cwiseAbs().maxCoeff(),
                ellipse.center.cwiseAbs().maxCoeff()}));
  if (!ellipse.center.allFinite() || !std::isfinite(residualTolerance)
      || std::abs(n.dot(ellipse.center - planeOrigin)) > residualTolerance) return {};
  return result(EdgeGeometry::fromEllipse(std::move(ellipse)));
}
