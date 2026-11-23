#include "trajectories/probecalibrationfit.h"

#include "kinematics/kr10kinematics.h"

#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

using namespace RoboCrap3D;

constexpr int MaxIterations = 50;
constexpr double StepTolerance = 1e-9; // mm, largest parameter change of a converged step.
constexpr double RankTolerance = 1e-9; // Smallest singular value relative to the largest.
constexpr double AngleTolerance = 1e-6; // deg, equal tilt or roll of planned touches.
constexpr double QuantileLevel = 0.95;

// A detected touch of the plan with its flange pose from the nominal forward kinematics.
struct FitTouch
{
  ProbeTouchPurpose purpose = ProbeTouchPurpose::Calibration;
  int group = 0;
  double tilt = 0.0; // deg
  double roll = 0.0; // deg
  V3d flangePosition = V3d::Zero(); // {B}, mm
  M3d flangeRotation = M3d::Identity(); // {F} to {B}
};

struct FitTouchesResult
{
  std::optional<QVector<FitTouch>> touches;
  QString error;
};

// Ball centre in {F}, sphere centre in {B} and the contact distances of the two classes.
struct FitModel
{
  V3d ball = V3d::Zero();
  V3d sphere = V3d::Zero();
  double contactVertical = 0.0;
  double contactHorizontal = 0.0;
};

struct SolveResult
{
  std::optional<FitModel> model;
  double smallestSingularValue = 0.0;
  QString error;
};

// Group 2 holds the stylus horizontal (class h); groups 1 and 3 hold it down (class v).
bool horizontal(const FitTouch& touch)
{
  return touch.group == 2;
}

V3d ballCentre(const FitModel& model, const FitTouch& touch)
{
  return touch.flangePosition + touch.flangeRotation * model.ball;
}

// Method (5.2.1) and (5.3.2): distance of the calculated ball centre from the sphere centre minus
// the contact distance of the touch's class, mm.
double residual(const FitModel& model, const FitTouch& touch)
{
  const double contact = horizontal(touch) ? model.contactHorizontal : model.contactVertical;
  return (ballCentre(model, touch) - model.sphere).norm() - contact;
}

// Touch k of the plan is the k-th Approach step of the motion, as numbered by the RSI job.
FitTouchesResult fitTouches(const ProbeTouchMotion& motion, const QVector<ProbeTouchDetection>& detections)
{
  const ProbeTouchPlan& plan = motion.plan();
  QVector<FitTouch> touches;
  for (const ProbeMotionStep& step : probeMotionSteps(plan)) {
    if (step.phase.segment != ProbeSegment::Approach) continue;
    const ProbeTouchPass& pass = plan.passes[step.phase.pass];
    const ProbeTouch& touch = pass.touches[step.phase.touch - 1];
    touches.append(FitTouch{pass.purpose, pass.group, touch.tilt, touch.roll});
  }
  QVector<bool> detected(touches.size(), false);
  // The saved motion owns the model for the solver's lifetime.
  const Kr10Kinematics kinematics(motion.robot().model);
  for (const ProbeTouchDetection& detection : detections) {
    if (detection.touch < 1 || detection.touch > touches.size())
      return {{}, QStringLiteral("Touch %1 is not in the calibration plan of %2 touches.")
                      .arg(detection.touch).arg(touches.size())};
    if (detected[detection.touch - 1]) return {{}, QStringLiteral("Touch %1 was detected twice.").arg(detection.touch)};
    if (!detection.joints.allFinite())
      return {{}, QStringLiteral("Touch %1 has non-finite joint angles.").arg(detection.touch)};
    const M4d flange = kinematics.solveFK(detection.joints).back();
    if (!flange.allFinite()) return {{}, QStringLiteral("Touch %1 has no finite flange pose.").arg(detection.touch)};
    FitTouch& touch = touches[detection.touch - 1];
    touch.flangePosition = flange.block<3, 1>(0, 3);
    touch.flangeRotation = flange.block<3, 3>(0, 0);
    detected[detection.touch - 1] = true;
  }
  const qsizetype found = std::count(detected.cbegin(), detected.cend(), true);
  if (found != touches.size())
    return {{}, QStringLiteral("%1 of %2 touches detected; the calibration needs all of them.")
                    .arg(found).arg(touches.size())};
  return {std::move(touches), {}};
}

QVector<FitTouch> selectTouches(const QVector<FitTouch>& touches, ProbeTouchPurpose purpose)
{
  QVector<FitTouch> selected;
  for (const FitTouch& touch : touches)
    if (touch.purpose == purpose) selected.append(touch);
  return selected;
}

// Gauss-Newton over the given touches. Calibration estimates all eight parameters of method (5.2.2);
// verification only the sphere centre of (5.3.1), with the ball centre and contact distances fixed.
SolveResult solve(const QVector<FitTouch>& touches, FitModel model, ProbeTouchPurpose purpose)
{
  const bool calibration = purpose == ProbeTouchPurpose::Calibration;
  const QString stage = calibration ? QStringLiteral("calibration") : QStringLiteral("verification");
  const int columns = calibration ? 8 : 3;
  if (touches.size() < columns)
    return {{}, 0.0, QStringLiteral("The %1 needs at least %2 touches, the plan has %3.")
                         .arg(stage).arg(columns).arg(touches.size())};
  for (int iteration = 0; iteration < MaxIterations; ++iteration) {
    Eigen::MatrixXd jacobian(touches.size(), columns);
    Eigen::VectorXd residuals(touches.size());
    for (int row = 0; row < touches.size(); ++row) {
      const FitTouch& touch = touches[row];
      const V3d direction = (ballCentre(model, touch) - model.sphere).normalized();
      residuals[row] = residual(model, touch);
      if (!calibration) {
        jacobian.row(row) = -direction.transpose();
        continue;
      }
      jacobian.block<1, 3>(row, 0) = direction.transpose() * touch.flangeRotation;
      jacobian.block<1, 3>(row, 3) = -direction.transpose();
      jacobian(row, 6) = horizontal(touch) ? 0.0 : -1.0;
      jacobian(row, 7) = horizontal(touch) ? -1.0 : 0.0;
    }
    if (!jacobian.allFinite() || !residuals.allFinite())
      return {{}, 0.0, QStringLiteral("The %1 fit reached non-finite values.").arg(stage)};
    const Eigen::JacobiSVD<Eigen::MatrixXd, Eigen::ComputeThinU | Eigen::ComputeThinV> svd(jacobian);
    if (svd.info() != Eigen::Success) return {{}, 0.0, QStringLiteral("The %1 fit failed.").arg(stage)};
    const Eigen::VectorXd& singularValues = svd.singularValues();
    const double smallest = singularValues[columns - 1];
    if (!(smallest >= RankTolerance * singularValues[0]))
      return {{}, smallest, QStringLiteral("The %1 parameters are not identifiable from these touches.").arg(stage)};
    const Eigen::VectorXd step = svd.solve(-residuals);
    if (calibration) {
      model.ball += step.segment<3>(0);
      model.sphere += step.segment<3>(3);
      model.contactVertical += step[6];
      model.contactHorizontal += step[7];
    } else {
      model.sphere += step.head<3>();
    }
    if (step.cwiseAbs().maxCoeff() < StepTolerance) return {model, smallest, {}};
  }
  return {{}, 0.0, QStringLiteral("The %1 fit did not converge in %2 iterations.").arg(stage).arg(MaxIterations)};
}

bool sameDirection(const FitTouch& first, const FitTouch& second)
{
  return first.group == second.group && std::abs(first.tilt - second.tilt) < AngleTolerance
      && std::abs(first.roll - second.roll) < AngleTolerance;
}

// Method (5.2.3): residual differences of the pairs of twice-touched directions of one group.
std::optional<double> repeatability(const QVector<FitTouch>& calibration, const FitModel& model)
{
  double sum = 0.0;
  int pairs = 0;
  for (qsizetype first = 0; first < calibration.size(); ++first) {
    for (qsizetype second = first + 1; second < calibration.size(); ++second) {
      if (!sameDirection(calibration[first], calibration[second])) continue;
      const double difference = residual(model, calibration[first]) - residual(model, calibration[second]);
      sum += difference * difference;
      ++pairs;
    }
  }
  if (pairs == 0) return std::nullopt;
  return std::sqrt(sum / (2.0 * pairs));
}

// Method section 5.3: value number ceil(0.95 m) of the m absolute verification residuals, ascending.
double quantile95(const QVector<FitTouch>& verification, const FitModel& model)
{
  std::vector<double> magnitudes;
  magnitudes.reserve(verification.size());
  for (const FitTouch& touch : verification) magnitudes.push_back(std::abs(residual(model, touch)));
  std::sort(magnitudes.begin(), magnitudes.end());
  return magnitudes[static_cast<std::size_t>(std::ceil(QuantileLevel * magnitudes.size())) - 1];
}

// Index of the set with the key's group, kind and value; -1 if there is none.
qsizetype findSet(const QVector<ProbeResidualSet>& sets, const ProbeResidualSet& key)
{
  for (qsizetype index = 0; index < sets.size(); ++index) {
    const ProbeResidualSet& set = sets[index];
    if (set.group == key.group && set.ring == key.ring && std::abs(set.value - key.value) < AngleTolerance)
      return index;
  }
  return -1;
}

// Method step K7: the mean residual of every tilt ring and every roll of each group, with its bound.
QVector<ProbeResidualSet> residualSets(const QVector<FitTouch>& calibration, const FitModel& model,
                                       double repeatability)
{
  QVector<ProbeResidualSet> sets;
  for (const bool ring : {true, false}) {
    for (const FitTouch& touch : calibration) {
      const ProbeResidualSet key{touch.group, ring, ring ? touch.tilt : touch.roll};
      qsizetype index = findSet(sets, key);
      if (index < 0) {
        sets.append(key);
        index = sets.size() - 1;
      }
      sets[index].mean += residual(model, touch); // Sum until divided below.
      ++sets[index].count;
    }
  }
  for (ProbeResidualSet& set : sets) {
    set.mean /= set.count;
    set.bound = 3.0 * repeatability / std::sqrt(static_cast<double>(set.count));
  }
  return sets;
}

} // namespace

ProbeCalibrationFitResult fitProbeCalibration(const ProbeTouchMotion& motion,
                                              const QVector<ProbeTouchDetection>& detections)
{
  if (!motion.plan().sphere) return {{}, QStringLiteral("The calibration fit requires the sphere fixture.")};
  const FitTouchesResult touches = fitTouches(motion, detections);
  if (!touches.touches) return {{}, touches.error};
  const QVector<FitTouch> calibration = selectTouches(*touches.touches, ProbeTouchPurpose::Calibration);
  const QVector<FitTouch> verification = selectTouches(*touches.touches, ProbeTouchPurpose::Verification);

  // Start values of method section 5.2: the measuring TCP, the sphere of the manual touches, L.
  const ProbeTouchPlanParameters& parameters = *motion.plan().sphere;
  const FitModel start{V3d(motion.robot().flangeToTcp.block<3, 1>(0, 3)), parameters.sphereCentre,
                       parameters.contactDistance(), parameters.contactDistance()};
  const SolveResult estimate = solve(calibration, start, ProbeTouchPurpose::Calibration);
  if (!estimate.model) return {{}, estimate.error};
  const auto spread = repeatability(calibration, *estimate.model);
  if (!spread) return {{}, QStringLiteral("The plan has no twice-touched calibration direction.")};
  const SolveResult check = solve(verification, *estimate.model, ProbeTouchPurpose::Verification);
  if (!check.model) return {{}, check.error};

  ProbeCalibrationFit fit;
  fit.ballCentre = estimate.model->ball;
  fit.sphereCentre = estimate.model->sphere;
  fit.contactVertical = estimate.model->contactVertical;
  fit.contactHorizontal = estimate.model->contactHorizontal;
  fit.repeatability = *spread;
  fit.smallestSingularValue = estimate.smallestSingularValue;
  fit.verificationSphereCentre = check.model->sphere;
  fit.quantile95 = quantile95(verification, *check.model);
  fit.calibrationTouches = static_cast<int>(calibration.size());
  fit.verificationTouches = static_cast<int>(verification.size());
  fit.residualSets = residualSets(calibration, *estimate.model, *spread);
  return {std::move(fit), {}};
}
