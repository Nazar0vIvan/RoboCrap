#pragma once

#include "sceneobject.h"
#include "viewport/refinecontacts.h"
#include <QHash>
#include <QList>
#include <QJsonArray>
#include <QQmlListProperty>
#include <QPointer>

namespace RoboCrap3D { class OccController; }

class SceneModel : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QQmlListProperty<SceneObject> objects READ objects NOTIFY objectsChanged FINAL)
  Q_PROPERTY(QString refineSelectionError READ refineSelectionError NOTIFY refineSelectionErrorChanged FINAL)

public:
  explicit SceneModel(QObject* parent = nullptr);

  void setViewport(RoboCrap3D::OccController* viewport);
  void showSelection(const QList<quint32>& ids);
  QString refineSelectionError() const { return m_refineSelectionError; }
  QQmlListProperty<SceneObject> objects();
  const QList<SceneObject*>& objectList() const { return m_objects; }
  Q_INVOKABLE SceneObject* findObject(quint32 objectId) const;
  Q_INVOKABLE void removeObjects(const QList<quint32>& ids);
  Q_INVOKABLE bool canIntersect(const QList<quint32>& selectedIds) const;
  Q_INVOKABLE QList<quint32> intersect(const QList<quint32>& selectedIds);
  Q_INVOKABLE QVariantMap createRefineJob(quint32 target, const QJsonArray& planes);
  Q_INVOKABLE void loadRefineJobs();
  Q_INVOKABLE quint32 resolveRefineTarget(quint32 jobId);
  RoboCrap3D::RefineContacts refineContacts(quint32 jobId, std::optional<double> eg22BallRadius = {}) const;
  QString refineScanError(const RefineScanSource& source, const ProbeTouchPlan& plan);
  QList<quint32> refinePathIds(const QString& jobId = {}, quint32 target = 0) const;
  std::pair<quint32, QString> publishRefineScan(const RefineScanSource& source, ProbeTouchMotion motion);
  Q_INVOKABLE QVariantMap saveRefineJob(quint32 jobId, quint32 target, const QJsonArray& planes);
  SceneObject* addMachiningPath(SceneMachiningData data, const QString& name);
	SceneObject* addBoundedPlane(const QUrl& sourceUrl, const QString& name, BoundedPlane plane);
	SceneObject* addBoundedCylinder(const QUrl& sourceUrl, const QString& name, BoundedCylinder cylinder);
  // A calibration sphere or calibration path; the shape decides the kind.
  SceneObject* addCalibrationObject(SceneObject::Shape shape, const QString& name);
  Q_INVOKABLE bool renameObject(SceneObject* object, const QString& name);
  Q_INVOKABLE bool setObjectVisible(SceneObject* object, bool visible);
  Q_INVOKABLE bool setObjectDiagnosticOverlays(SceneObject* object, bool showPoints, bool showNormals);
  // Reinstalls the sphere drawing with the requested spheres; keeps the old state on failure.
  Q_INVOKABLE bool setSphereDisplay(SceneObject* object, bool ballCenter, bool compensated);

signals:
  void refineSelectionErrorChanged();
  void refineJobError(const QString& error);
  void intersectionFailed(const QString& message);
  void objectsChanged();
  void objectAdded(SceneObject* object);
  void objectRemoved(quint32 objectId);
  void objectVisibilityChanged(SceneObject* object);
  void objectDiagnosticOverlaysChanged(SceneObject* object);

private:
  QString previewRefineJob(SceneObject& object);
  void refreshRefineSelection();
  std::optional<std::array<const SceneObject*, 2>> intersectionPair(const QList<quint32>& ids) const;
  static qsizetype objectCount(QQmlListProperty<SceneObject>* list);
  static SceneObject* objectAt(QQmlListProperty<SceneObject>* list, qsizetype index);
  SceneObject* appendRefineObject(const QJsonObject& job, quint32 target);
  SceneObject* appendObject(SceneObject* object);
  SceneObject* createObject(SceneObject::Description description);
  void onObjectVisibilityChanged();
  void onObjectDiagnosticOverlaysChanged();

  QPointer<RoboCrap3D::OccController> m_viewport;
  quint32 m_selectedRefineJob = 0;
  QString m_refineSelectionError;
  QList<SceneObject*> m_objects;
  QHash<quint32, SceneObject*> m_objectsById;
  quint32 m_nextId = 1; // Zero is never allocated, including after exhaustion.
  quint64 m_nextEdgeNumber = 1;
  quint64 m_nextMachiningPathNumber = 1;
};
