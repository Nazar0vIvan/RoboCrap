#pragma once

#include "mathtypes.h"
#include <QList>
#include <QPoint>
#include <QString>
#include <array>
#include <variant>

namespace RoboCrap3D {

struct RefineContact {
  QPoint index; // Original 1-based (U,V).
  V3d position = V3d::Zero(); // Material point in workpiece coordinates, mm.
};

struct RefinePlaneContacts {
  QString role;
  int faceIndex = 0;
  V3d normal = V3d::Zero(); // Unit approach-side normal in workpiece coordinates.
  QList<RefineContact> points;
};

using RefineContacts = std::variant<std::array<RefinePlaneContacts, 3>, QString>;

} // namespace RoboCrap3D
