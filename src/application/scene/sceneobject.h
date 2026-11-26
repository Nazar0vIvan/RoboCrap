#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <variant>
#include "scenegeometry.h"

namespace RoboCrap3D {
struct DrawingObject;
struct DrawingDiagnostics;
}

class SceneModel;

class SceneObject final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(quint32 objectId READ objectId CONSTANT FINAL)
  Q_PROPERTY(QString name READ name NOTIFY nameChanged FINAL)
  Q_PROPERTY(Kind kind READ kind CONSTANT FINAL)
  Q_PROPERTY(Classification classification READ classification CONSTANT FINAL)
  Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged FINAL)
  Q_PROPERTY(bool showPoints READ showPoints NOTIFY diagnosticOverlaysChanged FINAL)
  Q_PROPERTY(bool showNormals READ showNormals NOTIFY diagnosticOverlaysChanged FINAL)
  Q_PROPERTY(bool showBallCenterSphere READ showBallCenterSphere NOTIFY sphereDisplayChanged FINAL)
  Q_PROPERTY(bool showCompensatedSphere READ showCompensatedSphere NOTIFY sphereDisplayChanged FINAL)
  Q_PROPERTY(QUrl sourceUrl READ sourceUrl CONSTANT FINAL)
  Q_PROPERTY(QJsonObject refineJob READ refineJob NOTIFY refineJobChanged FINAL)
  // The saved machining path's QObject; null for every other kind.
  Q_PROPERTY(QObject* geometry READ geometry CONSTANT FINAL)
  // Formatted {label, value} rows for the Properties panel.
  Q_PROPERTY(QVariantList properties READ properties NOTIFY propertiesChanged FINAL)

public:
  enum Kind { Plane, Cylinder, Cone, Edge, ScanPath, MachiningPath, Sphere, CalibrationPath, EndGauge, RefineJob };
  Q_ENUM(Kind)
  enum Classification { Unclassified, Rough, Precise };
  Q_ENUM(Classification)
  // Refine jobs own their saved definition without a drawing.
  using Shape = std::variant<std::monostate, BoundedPlane, BoundedCylinder, SceneEdge, SceneSphere, SceneTouchPath, SceneEndGauge, QJsonObject>;

  std::optional<RoboCrap3D::DrawingObject> drawing() const;
  RoboCrap3D::DrawingDiagnostics drawingDiagnostics() const;
  quint32 objectId() const { return m_objectId; }
  struct Description {
    QString name;
    Kind kind = Plane;
    Classification classification = Unclassified;
    QUrl sourceUrl;
  };

  const BoundedPlane* plane() const { return std::get_if<BoundedPlane>(&m_shape); }
  const BoundedCylinder* cylinder() const { return std::get_if<BoundedCylinder>(&m_shape); }
  const SceneEdge* edge() const { return std::get_if<SceneEdge>(&m_shape); }
  const SceneSphere* sphere() const { return std::get_if<SceneSphere>(&m_shape); }
  const SceneEndGauge* endGauge() const { return std::get_if<SceneEndGauge>(&m_shape); }
  const SceneTouchPath* touchPath() const { return std::get_if<SceneTouchPath>(&m_shape); }
  bool isRefineScan() const { return touchPath() && !touchPath()->refineJobId.isEmpty(); }
  void setScanSamplingInput(const QJsonObject& input);
  const ChamferMotion* machiningPath() const { return m_machiningPath ? &m_machiningPath->motion() : nullptr; }
  QVariantList properties() const;
  QJsonObject refineJob() const;
  quint32 refineTarget() const { return m_refineTarget; }
  QString name() const { return m_name; }
  Kind kind() const { return m_kind; }
  Classification classification() const { return m_classification; }
  bool visible() const { return m_visible; }
  bool showPoints() const { return m_showPoints; }
  bool showNormals() const { return m_showNormals; }
  bool showBallCenterSphere() const { return m_showBallCenterSphere; }
  bool showCompensatedSphere() const { return m_showCompensatedSphere; }
  QUrl sourceUrl() const { return m_sourceUrl; }
  QObject* geometry() const { return m_machiningPath; }

signals:
  void propertiesChanged();
  void refineJobChanged();
  void nameChanged();
  void visibleChanged();
  void diagnosticOverlaysChanged();
  void sphereDisplayChanged();

private:
  friend class SceneModel;
  SceneObject(quint32 objectId, const Description& description, QObject* parent);
  void setRefineJob(const QJsonObject& job, quint32 target);
  void setRefineTarget(quint32 target) { m_refineTarget = target; }
  void setShape(Shape shape) { m_shape = std::move(shape); }
  void setMachiningPath(SceneMachiningData data);
  void setName(const QString& name);
  void setVisible(bool visible);
  void setDiagnosticOverlays(bool showPoints, bool showNormals);
  void setSphereDisplay(bool ballCenter, bool compensated);

  const quint32 m_objectId;
  QString m_name;
  const Kind m_kind;
  const Classification m_classification;
  bool m_visible = true;
  bool m_showPoints = false;
  bool m_showNormals = false;
  bool m_showBallCenterSphere = true;
  bool m_showCompensatedSphere = true;
  const QUrl m_sourceUrl;
  Shape m_shape;
  quint32 m_refineTarget = 0; // Matched scene instance in this process only.
  SceneMachiningPath* m_machiningPath = nullptr;
};
