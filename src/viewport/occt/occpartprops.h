#pragma once

#include "geometry/mathtypes.h"

#include <gp_Trsf.hxx>
#include <Quantity_Color.hxx>

#include <algorithm>

namespace RoboCrap3D {

inline gp_Trsf toOccTransform(const M4d& transform)
{
  gp_Trsf result;
  result.SetValues(
      transform(0, 0), transform(0, 1), transform(0, 2), transform(0, 3),
      transform(1, 0), transform(1, 1), transform(1, 2), transform(1, 3),
      transform(2, 0), transform(2, 1), transform(2, 2), transform(2, 3));
  return result;
}

inline Quantity_Color rgb(int r, int g, int b)
{
  return Quantity_Color(
      static_cast<double>(std::clamp(r, 0, 255)) / 255.0,
      static_cast<double>(std::clamp(g, 0, 255)) / 255.0,
      static_cast<double>(std::clamp(b, 0, 255)) / 255.0,
      Quantity_TOC_RGB);
}

inline const Quantity_Color kDefaultPartColor{0.72, 0.76, 0.80, Quantity_TOC_RGB};
// Face boundaries of selected parts, in the viewer's selection style and in OccScene's own outlines.
inline const Quantity_Color kSelectionOutlineColor{48.0 / 255.0, 48.0 / 255.0, 48.0 / 255.0, Quantity_TOC_sRGB};
constexpr double kSelectionOutlineWidth = 2.0;
// Hover highlight of scene parts and of the view cube.
inline const Quantity_Color kHoverColor = rgb(102, 179, 204);
// Axis length (mm) while the camera scale is unknown.
constexpr double kDefaultAxisLength = 10.0;

enum class OccSelectionMode {
  PartOnly,
  All,
  None,
};

struct OccPartProps
{
  gp_Trsf transform;
  Quantity_Color color = kDefaultPartColor;
  OccSelectionMode selectionMode = OccSelectionMode::None;
  bool wireframe = false;
  bool topmost = false;
  bool fitExcluded = false; // Decoration: ignored by camera fitting, drawn as usual.
  // Face edges drawn in black, and as the selection outline; off for faces bounded only by seams.
  bool faceBoundary = true;
  double markerSize = 0.0;
  double lineWidth = 0.0;
  double transparency = 0.0;
};

} // namespace RoboCrap3D
