#pragma once

#include "trajectories/probetouchmotion.h"

#include <QString>
#include <QVector>
#include <optional>

// Ball-centre calibration of the measuring probe from the touches of one RSI_Probe_Calibration run:
// base model of docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md, section 5.

// One detected touch: its global touch number (1-based, the order of the Approach steps of
// probeMotionSteps) and the controller's actual joint angles AIPos at detection, degrees.
struct ProbeTouchDetection
{
  int touch = 0;
  V6d joints = V6d::Zero();
};

// Mean calibration residual of the touches of one group with one tilt or one roll (method step K7).
struct ProbeResidualSet
{
  int group = 0;
  bool ring = true; // true: value is the tilt of a tilt ring; false: value is the roll.
  double value = 0.0; // deg
  double mean = 0.0; // mm
  double bound = 0.0; // 3 * repeatability / sqrt(count), mm
  int count = 0;
};

struct ProbeCalibrationFit
{
  V3d ballCentre = V3d::Zero(); // {F}, mm
  V3d sphereCentre = V3d::Zero(); // {B}, mm
  double contactVertical = 0.0; // Contact distance of groups 1 and 3 (stylus down), mm.
  double contactHorizontal = 0.0; // Contact distance of group 2 (stylus horizontal), mm.
  double repeatability = 0.0; // s_rep from the twice-touched calibration directions, mm.
  double smallestSingularValue = 0.0; // Of the calibration Jacobian at the estimate.
  V3d verificationSphereCentre = V3d::Zero(); // {B}, fitted from the verification touches, mm.
  double quantile95 = 0.0; // chi95 of the absolute verification residuals, mm.
  int calibrationTouches = 0;
  int verificationTouches = 0;
  QVector<ProbeResidualSet> residualSets;
};

struct ProbeCalibrationFitResult
{
  std::optional<ProbeCalibrationFit> fit;
  QString error;
};

// Requires exactly one detection of every touch of the motion's plan.
ProbeCalibrationFitResult fitProbeCalibration(const ProbeTouchMotion& motion,
                                              const QVector<ProbeTouchDetection>& detections);
