#pragma once

#include "mathtypes.h"

#include <QStringList>
#include <QVector>

#include <array>
#include <optional>

struct EndGaugeFit
{
  std::optional<M4d> workpieceInBase;
  QString diagnostic; // Fit statistics on success, reason on failure.
  std::array<QVector<double>, 3> planeResidualsMm; // Outward signed distances in input order.
};

struct EndGaugeObservations
{
  // Indexed by the CAD coordinate held constant on the face: X, Y, Z.
  static const QStringList FaceNames;
  static constexpr double ClampTopMm = -35.0 + 8.0; // Bottom 8 mm of the native side faces.
  std::array<QVector<V3d>, 3> ballCentres;
  double compensationRadiusMm = 0.0;
  double physicalBallRadiusMm = 0.0;

  EndGaugeFit fit() const;

private:
  EndGaugeFit checkContacts(const M4d& workpieceInBase, const M3d& outwardNormals) const;
};
