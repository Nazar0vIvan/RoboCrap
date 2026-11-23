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
};

struct EndGaugeObservations
{
  // Indexed by the CAD coordinate held constant on the face: X, Y, Z.
  static const QStringList FaceNames;
  std::array<QVector<V3d>, 3> ballCentres;
  double ballRadius = 0.0; // mm, as used when collecting these observations.

  EndGaugeFit fit() const;

private:
  EndGaugeFit checkContacts(const M4d& workpieceInBase, const M3d& outwardNormals) const;
};
