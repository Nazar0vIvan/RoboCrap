#pragma once

#include "scene/sceneobject.h"

#include <QObject>
#include <QList>
#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

class SceneModel;
class SceneMachining;
class SceneEndEffectors;

class WorkspaceController final : public QObject
{
  Q_OBJECT
  QML_NAMED_ELEMENT(WorkspaceController)
  QML_SINGLETON
  Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
  Q_PROPERTY(Submode submode READ submode WRITE setSubmode NOTIFY submodeChanged FINAL)
  Q_PROPERTY(QList<quint32> selectedObjectIds READ selectedObjectIds NOTIFY selectionChanged FINAL)
  Q_PROPERTY(SceneObject* selectedObject READ selectedObject NOTIFY selectionDetailsChanged FINAL)
  Q_PROPERTY(bool intersectionAvailable READ intersectionAvailable NOTIFY selectionDetailsChanged FINAL)
  // The displayed end-effector model is selected; it excludes selected scene objects.
  Q_PROPERTY(bool toolSelected READ toolSelected NOTIFY toolSelectedChanged FINAL)
  Q_PROPERTY(bool pickingWorkpiece MEMBER m_pickingWorkpiece NOTIFY pickingWorkpieceChanged FINAL)
public:
  enum Mode { Measuring, Machining };
  Q_ENUM(Mode)
  enum Submode { Rough, Precise, Calibration };
  Q_ENUM(Submode)

  WorkspaceController(SceneModel* scene, SceneMachining* machining, SceneEndEffectors* effectors);
  static WorkspaceController* create(QQmlEngine*, QJSEngine* engine);

  Mode mode() const { return m_mode; }
  void setMode(Mode mode);
  Submode submode() const { return m_submode; }
  void setSubmode(Submode submode);
  const QList<quint32>& selectedObjectIds() const { return m_selectedIds; }
  SceneObject* selectedObject() const;
  bool intersectionAvailable() const;
  bool toolSelected() const { return m_toolSelected; }
  // Also deselects the end-effector model.
  Q_INVOKABLE void selectObject(quint32 id, bool additive);
  void pickObject(quint32 id, bool additive);
  void replaceSelectedObject(quint32 oldId, quint32 newId);
  // Viewport pick of the end-effector model: replaces the object selection.
  void selectTool();
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void intersectSelected();
  Q_INVOKABLE void reset();

signals:
  void modeChanged();
  void submodeChanged();
  void selectionChanged(const QList<quint32>& ids);
  void selectionDetailsChanged();
  void toolSelectedChanged(bool selected);
  void pickingWorkpieceChanged();
  void workpiecePicked(quint32 id, bool tool);
  // Measuring with the Calibration submode starts or stops; false precedes the tool change.
  void calibrationActiveChanged(bool active);
  void preciseActiveChanged(bool active);

private:
  void setSelection(QList<quint32> ids);
  void setToolSelected(bool selected);
  void pruneSelection();
  void synchronizeMode();
  void synchronizeCalibration();
  void synchronizeMachiningInput();

  SceneModel* m_scene;
  SceneMachining* m_machining;
  SceneEndEffectors* m_effectors;
  Mode m_mode = Measuring;
  Submode m_submode = Rough;
  Submode m_activeMeasurement = Rough;
  QList<quint32> m_selectedIds;
  bool m_toolSelected = false;
  bool m_pickingWorkpiece = false;
  inline static QPointer<WorkspaceController> s_instance;
};
