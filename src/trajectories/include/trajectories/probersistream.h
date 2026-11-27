#pragma once

#include "trajectories/probetouchmotion.h"

#include <QByteArray>
#include <QVector>
#include <optional>

// Controller job of the probe sphere calibration (controller/krl/rsi/calibration): KRL executes
// every step of the saved calibration path; each Search runs under RSI with its own correction
// stream, selected by the touch number that KRL sends as TouchIndex.
struct ProbeRsiStep
{
  ProbeRsiStepKind kind = ProbeRsiStepKind::Ptp;
  int touch = 0; // Global touch number of a Search, 0 otherwise.
  V6d target = V6d::Zero(); // Six-decimal XYZ mm, principal ABC deg; PTP, LIN and CIRC only.
  V6d auxiliary = V6d::Zero(); // CIRC only, with the target's orientation and S/T.
  int status = 0;
  int turn = 0;
};

struct ProbeCalibrationRsi
{
  QString diagnosticId; // Pairs the stream table with this job; job_id in the session records.
  V6d toolCoordinates = V6d::Zero(); // Six-decimal flange-to-TCP XYZABC for KRL $TOOL.
  QVector<ProbeRsiStep> steps;
  // Index = global touch number; index 0 is empty. Increments in WORLD XYZ mm, ABC zero.
  QVector<QVector<V6d>> streams;
  int touchCount = 0;
  // plan.csv of the sessions that run this job (docs/code context/RECORDS_FORMAT.md), one row per touch.
  QByteArray plan;
};

struct ProbeCalibrationRsiResult
{
  std::optional<ProbeCalibrationRsi> job;
  QString error;
};

// Copied-input worker operation; writes no file.
ProbeCalibrationRsiResult sampleProbeCalibrationRsi(const ProbeTouchMotion& motion);
ProbeCalibrationRsiResult sampleProbeScanRsi(const ProbeTouchMotion& motion);
