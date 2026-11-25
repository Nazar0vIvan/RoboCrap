#pragma once

#include "geometry/boundedplane.h"
#include "geometry/boundedcylinder.h"
#include "geometry/surfaceintersection.h"
#include "trajectories/chamfermotion.h"
#include "trajectories/probetouchmotion.h"
#include <memory>
#include <QList>
#include <QObject>
#include <optional>
#include <utility>

// Value-type draft settings; geometry, robot model and tool transform are supplied by C++.
struct SceneMachiningSettings
{
  Q_GADGET
  Q_PROPERTY(double chamferSize MEMBER chamferSize FINAL)
  Q_PROPERTY(double chamferAngle MEMBER chamferAngle FINAL)
  Q_PROPERTY(double stagingDistance MEMBER stagingDistance FINAL)
  Q_PROPERTY(bool flipAxis MEMBER flipAxis FINAL)
  Q_PROPERTY(double leadInClearance MEMBER leadInClearance FINAL)
  Q_PROPERTY(double leadOutClearance MEMBER leadOutClearance FINAL)
  Q_PROPERTY(double leadInSpan MEMBER leadInSpan FINAL)
  Q_PROPERTY(double leadOutSpan MEMBER leadOutSpan FINAL)
  Q_PROPERTY(double machiningFeed MEMBER machiningFeed FINAL)
  Q_PROPERTY(double accelerationDistanceFraction MEMBER accelerationDistanceFraction FINAL)
  Q_PROPERTY(double transferFeed MEMBER transferFeed FINAL)
  Q_PROPERTY(double transferAcceleration MEMBER transferAcceleration FINAL)
  Q_PROPERTY(double acceleration MEMBER acceleration FINAL)
  Q_PROPERTY(double auxiliaryScale MEMBER auxiliaryScale FINAL)
  Q_PROPERTY(QList<double> jointSpeed MEMBER jointSpeed FINAL)
  Q_PROPERTY(QList<double> jointAcceleration MEMBER jointAcceleration FINAL)
public:
  double chamferSize = 0.5;
  double chamferAngle = 45.0;
  double stagingDistance = 10.0;
  bool flipAxis = false;
  double leadInClearance = 5.0; // mm
  double leadOutClearance = 5.0; // mm
  double leadInSpan = 30.0; // degrees
  double leadOutSpan = 30.0; // degrees
  double machiningFeed = 5.0;
  double accelerationDistanceFraction = 0.30;
  double transferFeed = 10.0; // mm/s, shared staging-transfer setting.
  double transferAcceleration = 20.0; // mm/s^2, requested transfer setting.
  double acceleration = 10000.0; // mm/s^2, maximum programmable CP path acceleration $ACC_MA.CP.
  double auxiliaryScale = 1.0;
  QList<double> jointSpeed{0, 0, 0, 0, 0, 0}; // Model-derived snapshot; unresolved before model loading.
  QList<double> jointAcceleration{60, 60, 60, 60, 60, 60};

  static SceneMachiningSettings fromMotion(const ChamferMotion& motion);
  ChamferPathParameters applyTo(ChamferPathParameters parameters) const;
  std::optional<ChamferTimingParameters> timing() const;
};

struct SceneMachiningData
{
  std::shared_ptr<const ChamferMotion> motion;
  quint32 sourceEdgeId = 0;
  quint64 robotSetupRevision = 0;
};

class SceneMachiningPath final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(double duration READ duration CONSTANT FINAL)
  Q_PROPERTY(double centralDuration READ centralDuration CONSTANT FINAL)
  Q_PROPERTY(quint32 sourceEdgeId READ sourceEdgeId CONSTANT FINAL)
  Q_PROPERTY(SceneMachiningSettings settings READ settings CONSTANT FINAL)
  Q_PROPERTY(QString incompatibility READ incompatibility NOTIFY compatibilityChanged FINAL)
  Q_PROPERTY(bool compatible READ compatible NOTIFY compatibilityChanged FINAL)
public:
  SceneMachiningPath(SceneMachiningData data, QObject* parent)
    : QObject(parent), m_data(std::move(data)) {}
  const ChamferMotion& motion() const { return *m_data.motion; }
  const SceneMachiningData& data() const { return m_data; }
  double duration() const { return motion().duration(); }
  double centralDuration() const { return motion().centralMotion().duration(); }
  quint32 sourceEdgeId() const { return m_data.sourceEdgeId; }
  SceneMachiningSettings settings() const { return SceneMachiningSettings::fromMotion(motion()); }
  QString incompatibility() const { return m_incompatibility; }
  bool compatible() const { return m_incompatibility.isEmpty(); }
  void setIncompatibility(const QString& reason)
  {
    if (reason == m_incompatibility) return;
    m_incompatibility = reason;
    emit compatibilityChanged();
  }
signals:
  void compatibilityChanged();
private:
  const SceneMachiningData m_data;
  QString m_incompatibility;
};

// Intersection curve with the two source object IDs, smaller ID first.
struct SceneEdge
{
  EdgeGeometry edge;
  std::array<quint32, 2> sources{};
};

// Table top under the calibration sphere, measured from the sphere center (mm); its sides are
// parallel to the x- and y-axes of {B}.
struct CalibrationTable
{
  double lengthX = 0.0; // Side along the x-axis.
  double widthY = 0.0; // Side along the y-axis.
  double sphereFromMinusX = 0.0; // Sphere center to the edge at the smaller x.
  double sphereFromMinusY = 0.0; // Sphere center to the edge at the smaller y.
  double sphereAboveTop = 0.0; // Sphere center above the table top.

  friend bool operator==(const CalibrationTable& a, const CalibrationTable& b)
  {
    return a.lengthX == b.lengthX && a.widthY == b.widthY && a.sphereFromMinusX == b.sphereFromMinusX
        && a.sphereFromMinusY == b.sphereFromMinusY && a.sphereAboveTop == b.sphereAboveTop;
  }
};

// Calibration sphere fitted to the ruby ball centers of the jogged touches.
struct SceneSphere
{
  V3d center = V3d::Zero(); // Used center: the saved primary sphere centre, otherwise roughCenter.
  V3d roughCenter = V3d::Zero(); // Center fitted to the jogged touches; the radii and rms belong to it.
  double radius = 0.0; // mm, nominal: half the sphere file's diameter.
  double ballCenterRadius = 0.0; // mm, sphere through the ball centers.
  double compensatedRadius = 0.0; // mm, ballCenterRadius minus the ruby ball radius.
  double rms = 0.0; // mm, fit residual.
  QList<V3d> roughPoints; // Ball centers reported by the controller at the jogged touches.
  double strutRadius = 0.0; // mm, vertical strut under the sphere.
  double strutRecess = 0.0; // mm, strut top above the lowest sphere point.
  CalibrationTable table;
  QString standModel; // Absolute BREP path of the stand, saved with its origin at the sphere center.

  friend bool operator==(const SceneSphere& a, const SceneSphere& b)
  {
    return a.center == b.center && a.roughCenter == b.roughCenter && a.radius == b.radius && a.ballCenterRadius == b.ballCenterRadius
        && a.compensatedRadius == b.compensatedRadius && a.rms == b.rms && a.roughPoints == b.roughPoints
        && a.strutRadius == b.strutRadius && a.strutRecess == b.strutRecess && a.table == b.table
        && a.standModel == b.standModel;
  }
};

using SceneEndGauge = ProbeGaugeMount;

// Generated probe calibration motion, shared with playback.
struct SceneTouchPath
{
  std::shared_ptr<const ProbeTouchMotion> motion;
  QString refineJobId; // Empty for calibration and standalone preset paths.
};
