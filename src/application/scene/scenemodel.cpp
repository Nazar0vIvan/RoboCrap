#include "scenemodel.h"
#include "viewport/occcontroller.h"

#include <utility>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJSEngine>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

#include <array>
#include <algorithm>

namespace {

QString appendRefineJob(const QJsonObject& job)
{
  const QDir directory(QStringLiteral(ROBOCRAP_SETTINGS_DIR));
  const QString path = directory.filePath(QStringLiteral("planar_probe_scans.json"));
  if (!directory.mkpath(".")) return SceneModel::tr("Cannot create settings directory %1.").arg(directory.path());
  QLockFile lock(path + QStringLiteral(".lock"));
  if (!lock.tryLock(0)) return SceneModel::tr("Cannot lock %1 for writing.").arg(path);
  QJsonObject store{{"version", 1}};
  QJsonArray jobs;
  QFile input(path);
  if (input.exists()) {
    if (!input.open(QIODevice::ReadOnly)) return SceneModel::tr("Cannot read %1: %2").arg(path, input.errorString());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (input.error() != QFileDevice::NoError) return SceneModel::tr("Cannot read %1: %2").arg(path, input.errorString());
    input.close();
    if (error.error != QJsonParseError::NoError)
      return SceneModel::tr("Invalid JSON in %1: %2").arg(path, error.errorString());
    store = document.object();
    if (store.value("version").toInt() != 1 || !store.value("jobs").isArray())
      return SceneModel::tr("Unsupported Refine Job store in %1.").arg(path);
    jobs = store.value("jobs").toArray();
    QSet<QUuid> ids{QUuid(job.value("id").toString())};
    for (const auto& entry : jobs) {
      const QUuid id(entry.toObject().value("id").toString());
      if (id.isNull() || ids.contains(id)) return SceneModel::tr("Missing or duplicate Refine Job ID in %1.").arg(path);
      ids.insert(id);
    }
  }
  jobs.append(job);
  store.insert("jobs", jobs);
  const QByteArray bytes = QJsonDocument(store).toJson();
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    return SceneModel::tr("Cannot save %1: %2").arg(path, output.errorString());
  return {};
}

QString intersectionMessage(IntersectionStatus status)
{
  switch (status) {
    case IntersectionStatus::ParallelCylinderAxis:
      return SceneModel::tr("A plane parallel to the cylinder axis is not supported.");
    case IntersectionStatus::InvalidGeometry:
    case IntersectionStatus::Success:
      return SceneModel::tr("Cannot calculate a valid intersection for these surfaces.");
  }
  return SceneModel::tr("Cannot calculate a valid intersection for these surfaces.");
}

} // namespace

SceneModel::SceneModel(QObject* parent) : QObject(parent)
{}

void SceneModel::setViewport(RoboCrap3D::OccController* viewport)
{
  Q_ASSERT(m_objects.isEmpty()); // main attaches the document before imports.
  m_viewport = viewport;
}

QQmlListProperty<SceneObject> SceneModel::objects()
{
  // No append/clear/replace callbacks: QML cannot take over collection ownership.
  return {this, this, &SceneModel::objectCount, &SceneModel::objectAt};
}

qsizetype SceneModel::objectCount(QQmlListProperty<SceneObject>* list)
{
  return static_cast<SceneModel*>(list->data)->objectList().size();
}

SceneObject* SceneModel::objectAt(QQmlListProperty<SceneObject>* list, qsizetype index)
{
  return static_cast<SceneModel*>(list->data)->objectList().value(index);
}

SceneObject* SceneModel::findObject(quint32 objectId) const
{
  return m_objectsById.value(objectId, nullptr);
}

std::optional<std::array<const SceneObject*, 2>> SceneModel::intersectionPair(
    const QList<quint32>& ids) const
{
  if (ids.size() != 2 || ids[0] == ids[1]) return std::nullopt;
  const SceneObject* first = findObject(std::min(ids[0], ids[1]));
  const SceneObject* second = findObject(std::max(ids[0], ids[1]));
  if (!first || !second) return std::nullopt;
  // Keep the plane first regardless of selection order.
  if (!first->plane()) std::swap(first, second);
  if (!first->plane() || !second->cylinder()) return std::nullopt;
  return std::array<const SceneObject*, 2>{first, second};
}

void SceneModel::removeObjects(const QList<quint32>& ids)
{
  bool changed = false;
  for (quint32 id : ids) {
    SceneObject* object = m_objectsById.take(id);
    if (!object) continue;
    m_objects.removeOne(object);
    disconnect(object, nullptr, this, nullptr);
    if (m_viewport) m_viewport->removeObject(id);
    emit objectRemoved(id);
    object->deleteLater();
    changed = true;
  }
  if (changed) emit objectsChanged();
}

bool SceneModel::canIntersect(const QList<quint32>& selectedIds) const
{
  return intersectionPair(selectedIds).has_value();
}

QList<quint32> SceneModel::intersect(const QList<quint32>& selectedIds)
{
  const auto pair = intersectionPair(selectedIds);
  if (!pair) {
    emit intersectionFailed(tr("Select one fitted plane and one fitted cylinder."));
    return {};
  }
  const SceneObject* first = (*pair)[0];
  const SceneObject* second = (*pair)[1];
  auto result = intersectSurfaces(*first->plane(), *second->cylinder());
  if (result.status != IntersectionStatus::Success || !result.edge) {
    emit intersectionFailed(intersectionMessage(result.status));
    return {};
  }
  const auto classification = first->classification() == second->classification()
      ? first->classification() : SceneObject::Unclassified;
  auto* object = createObject({tr("Edge %1").arg(m_nextEdgeNumber), SceneObject::Edge, classification, {}});
  if (!object) {
    emit intersectionFailed(tr("Cannot create an edge: scene object IDs are exhausted."));
    return {};
  }
  const std::array<quint32, 2> sources{std::min(first->objectId(), second->objectId()),
                                       std::max(first->objectId(), second->objectId())};
  object->setShape(SceneEdge{std::move(*result.edge), sources});
  ++m_nextEdgeNumber;
  if (!appendObject(object)) {
    emit intersectionFailed(tr("Cannot insert the intersection drawing."));
    return {};
  }
  return {object->objectId()};
}

SceneObject* SceneModel::createObject(SceneObject::Description description)
{
  if (m_nextId == 0) return nullptr;
  description.name = description.name.trimmed();
  return new SceneObject(m_nextId++, description, this);
}

QVariantMap SceneModel::createRefineJob(quint32 target, const QJsonArray& planes)
{
  const auto* workpiece = findObject(target);
  if (!workpiece || !m_viewport) return {{"error", tr("Select a placed workpiece.")}};
  if (!m_nextId) return {{"error", tr("Scene object IDs are exhausted.")}};
  auto [job, error] = m_viewport->refineJobDefinition(target, planes);
  if (!error.isEmpty()) return {{"error", error}};
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString name = tr("Refine Job %1").arg(id.left(8));
  job.insert("id", id);
  job.insert("revision", 1);
  job.insert("name", name);
  job.insert("workpieceName", workpiece->name());
  if (const QString failure = appendRefineJob(job); !failure.isEmpty()) return {{"error", failure}};
  auto* object = createObject({name, SceneObject::RefineJob, SceneObject::Unclassified, {}});
  object->setShape(std::move(job));
  appendObject(object);
  return {{"objectId", object->objectId()}};
}

SceneObject* SceneModel::addMachiningPath(SceneMachiningData data, const QString& name)
{
  if (!data.motion) return nullptr;
  const bool automaticName = name.trimmed().isEmpty();
  auto* object = createObject({automaticName ? tr("Machining Path %1").arg(m_nextMachiningPathNumber) : name,
                              SceneObject::MachiningPath, SceneObject::Unclassified, {}});
  if (!object) return nullptr;
  object->setMachiningPath(std::move(data));
  if (automaticName) ++m_nextMachiningPathNumber;
  return appendObject(object);
}

SceneObject* SceneModel::appendObject(SceneObject* object)
{
  if (m_viewport) {
    const auto drawing = object->drawing();
    if (drawing && !m_viewport->insertObject(*drawing).accepted) {
      delete object;
      return nullptr;
    }
  }
  QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
  if (object->geometry()) QJSEngine::setObjectOwnership(object->geometry(), QJSEngine::CppOwnership);
  m_objects.append(object);
  m_objectsById.insert(object->objectId(), object);
  connect(object, &SceneObject::visibleChanged, this, &SceneModel::onObjectVisibilityChanged);
  connect(object, &SceneObject::diagnosticOverlaysChanged, this, &SceneModel::onObjectDiagnosticOverlaysChanged);
  emit objectAdded(object);
  emit objectsChanged();
  return object;
}

SceneObject* SceneModel::addBoundedPlane(const QUrl& sourceUrl, const QString& name,
                                        BoundedPlane plane)
{
  auto* object = createObject({name.trimmed().isEmpty() ? tr("Plane") : name,
                               SceneObject::Plane, SceneObject::Rough, sourceUrl});
  if (!object) return nullptr;
  object->setShape(std::move(plane));
  return appendObject(object);
}

SceneObject* SceneModel::addBoundedCylinder(const QUrl& sourceUrl, const QString& name,
                                           BoundedCylinder cylinder)
{
  auto* object = createObject({name.trimmed().isEmpty() ? tr("Cylinder") : name,
                               SceneObject::Cylinder, SceneObject::Rough, sourceUrl});
  if (!object) return nullptr;
  object->setShape(std::move(cylinder));
  return appendObject(object);
}

SceneObject* SceneModel::addCalibrationObject(SceneObject::Shape shape, const QString& name)
{
  SceneObject::Kind kind;
  if (std::holds_alternative<SceneSphere>(shape)) kind = SceneObject::Sphere;
  else if (const auto* path = std::get_if<SceneTouchPath>(&shape)) {
    if (!path->motion) return nullptr;
    kind = path->motion->plan().passes.front().purpose == ProbeTouchPurpose::Gauge
        ? SceneObject::ScanPath : SceneObject::CalibrationPath;
  } else if (std::holds_alternative<SceneEndGauge>(shape)) kind = SceneObject::EndGauge;
  else return nullptr;
  auto* object = createObject({name, kind, SceneObject::Unclassified, {}});
  if (!object) return nullptr;
  object->setShape(std::move(shape));
  return appendObject(object);
}

void SceneModel::onObjectVisibilityChanged()
{
  auto* object = qobject_cast<SceneObject*>(sender());
  if (m_viewport && object) m_viewport->setObjectVisible(object->objectId(), object->visible());
  emit objectVisibilityChanged(object);
}

void SceneModel::onObjectDiagnosticOverlaysChanged()
{
  auto* object = qobject_cast<SceneObject*>(sender());
  if (m_viewport && object)
    m_viewport->setObjectDiagnostics(object->objectId(), object->drawingDiagnostics());
  emit objectDiagnosticOverlaysChanged(object);
}

bool SceneModel::setObjectDiagnosticOverlays(SceneObject* object, bool showPoints, bool showNormals)
{
  if (!object || m_objectsById.value(object->objectId()) != object) return false;
  object->setDiagnosticOverlays(showPoints, showNormals);
  return true;
}

bool SceneModel::setSphereDisplay(SceneObject* object, bool ballCenter, bool compensated)
{
  if (!object || m_objectsById.value(object->objectId()) != object || !object->sphere()) return false;
  const bool ballCenterBefore = object->showBallCenterSphere();
  const bool compensatedBefore = object->showCompensatedSphere();
  object->setSphereDisplay(ballCenter, compensated);
  if (!m_viewport) return true;
  // Inserting an installed id replaces its drawing only after the new one is accepted.
  const auto drawing = object->drawing();
  if (drawing && m_viewport->insertObject(*drawing).accepted) return true;
  object->setSphereDisplay(ballCenterBefore, compensatedBefore);
  return false;
}

bool SceneModel::renameObject(SceneObject* object, const QString& name)
{
  const QString trimmedName = name.trimmed();
  if (!object || m_objectsById.value(object->objectId()) != object || trimmedName.isEmpty()) return false;
  object->setName(trimmedName);
  return true;
}

bool SceneModel::setObjectVisible(SceneObject* object, bool visible)
{
  if (!object || m_objectsById.value(object->objectId()) != object) return false;
  object->setVisible(visible);
  return true;
}
