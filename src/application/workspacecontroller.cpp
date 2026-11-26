#include "workspacecontroller.h"

#include "backendqmltypes.h"
#include "scene/scenemodel.h"
#include "scene/scenemachining.h"

#include <utility>

WorkspaceController::WorkspaceController(SceneModel* scene, SceneMachining* machining,
                                         SceneEndEffectors* effectors)
  : m_scene(scene), m_machining(machining), m_effectors(effectors)
{
  Q_ASSERT(scene && machining && effectors && !s_instance);
  Q_ASSERT(scene->thread() == thread() && machining->thread() == thread() && effectors->thread() == thread());
  s_instance = this;
  connect(scene, &SceneModel::objectsChanged, this, &WorkspaceController::pruneSelection);
}

WorkspaceController* WorkspaceController::create(QQmlEngine*, QJSEngine* engine)
{
  return static_cast<WorkspaceController*>(claimQmlSingleton(s_instance.data(), engine));
}

void WorkspaceController::setMode(Mode mode)
{
  if ((mode != Measuring && mode != Machining) || mode == m_mode) return;
  m_mode = mode;
  synchronizeMode();
}

void WorkspaceController::synchronizeMode()
{
  // The mode shows the other end-effector model.
  setToolSelected(false);
  if (m_mode != Machining) m_machining->stop();
  // QML cancels the old editor before tool/input settings change.
  emit modeChanged();
  synchronizeCalibration();
  m_effectors->setActiveTool(m_mode == Machining ? SceneEndEffectors::Spindle : SceneEndEffectors::Measuring);
  synchronizeMachiningInput();
}

void WorkspaceController::setSubmode(Submode submode)
{
  if ((submode != Rough && submode != Precise && submode != Calibration) || submode == m_submode) return;
  m_submode = submode;
  emit submodeChanged();
  synchronizeCalibration();
}

void WorkspaceController::synchronizeCalibration()
{
  const auto next = m_mode == Measuring ? m_submode : Rough;
  if (next == m_activeMeasurement) return;
  const auto previous = std::exchange(m_activeMeasurement, next);
  setSelection({});
  if (previous == Precise) emit preciseActiveChanged(false);
  if (previous == Calibration) emit calibrationActiveChanged(false);
  if (next == Precise) emit preciseActiveChanged(true);
  if (next == Calibration) emit calibrationActiveChanged(true);
}

SceneObject* WorkspaceController::selectedObject() const
{
  return m_selectedIds.size() == 1 ? m_scene->findObject(m_selectedIds.front()) : nullptr;
}

bool WorkspaceController::intersectionAvailable() const
{
  return m_scene->canIntersect(m_selectedIds);
}

void WorkspaceController::selectObject(quint32 id, bool additive)
{
  const auto* object = m_scene->findObject(id);
  if (id && !object) return;
  setToolSelected(false);
  auto ids = additive ? m_selectedIds : QList<quint32>{};
  if (!id) ids.clear();
  else if (!ids.removeOne(id)) ids.append(id);
  if (object && object->kind() == SceneObject::RefineJob && ids == m_selectedIds) {
    m_scene->showSelection(ids);
    return;
  }
  setSelection(std::move(ids));
}

void WorkspaceController::pickObject(quint32 id, bool additive)
{
  if (m_pickingWorkpiece) emit workpiecePicked(id, false);
  else selectObject(id, additive);
}

void WorkspaceController::selectTool()
{
  if (m_pickingWorkpiece) {
    emit workpiecePicked(0, true);
    return;
  }
  // Objects first, so QML closes their panels before the end-effector panel opens.
  setSelection({});
  setToolSelected(true);
}

void WorkspaceController::setToolSelected(bool selected)
{
  if (m_toolSelected == selected) return;
  m_toolSelected = selected;
  emit toolSelectedChanged(selected);
}

void WorkspaceController::setSelection(QList<quint32> ids)
{
  if (ids == m_selectedIds) return;
  m_selectedIds = std::move(ids);
  // Derived selection notification lets QML cancel before the new input applies its settings.
  emit selectionDetailsChanged();
  synchronizeMachiningInput();
  emit selectionChanged(m_selectedIds);
}

void WorkspaceController::pruneSelection()
{
  auto ids = m_selectedIds;
  for (quint32 id : m_selectedIds)
    if (!m_scene->findObject(id)) ids.removeOne(id);
  setSelection(std::move(ids));
}

void WorkspaceController::synchronizeMachiningInput()
{
  const auto* object = selectedObject();
  m_machining->setInputId(m_mode == Machining && object
      && (object->kind() == SceneObject::Edge || object->kind() == SceneObject::MachiningPath)
      ? object->objectId() : 0);
}

void WorkspaceController::deleteSelected()
{
  // The calibration sphere and path cannot be deleted; other objects are hidden there.
  if (m_activeMeasurement == Calibration) return;
  const auto ids = m_selectedIds;
  setSelection({});
  m_scene->removeObjects(ids);
}

void WorkspaceController::intersectSelected()
{
  auto ids = m_scene->intersect(m_selectedIds);
  if (!ids.isEmpty()) setSelection(std::move(ids));
}

void WorkspaceController::reset()
{
  m_mode = Measuring;
  setSubmode(Rough);
  synchronizeMode();
  setSelection({});
}
