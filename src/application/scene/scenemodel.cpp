#include "scenemodel.h"
#include "viewport/occcontroller.h"

#include <utility>
#include <limits>

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

QString refineStorePath()
{
  return QDir(QStringLiteral(ROBOCRAP_SETTINGS_DIR)).filePath(QStringLiteral("planar_probe_scans.json"));
}

bool validStoredPlane(const QJsonObject& plane, int role)
{
  const auto counts = plane.value("countsUV").toArray();
  const auto spacing = plane.value("spacingMmUV").toArray();
  const auto automatic = plane.value("autoSpacingUV").toArray();
  const auto margins = plane.value("marginsMmLeftTopBottomRight").toArray();
  if (plane.value("role").toString() != QStringLiteral("%1Plane").arg(QStringLiteral("XYZ").at(role))
      || plane.value("faceIndex").toInt() < 1 || !plane.value("reversed").isBool()
      || counts.size() != 2 || spacing.size() != 2 || automatic.size() != 2 || margins.size() != 4
      || !plane.value("skippedUV").isArray()) return false;
  for (int i = 0; i < 2; ++i)
    if (counts.at(i).toInt() < 2 || spacing.at(i).toDouble() <= 0 || !automatic.at(i).isBool()) return false;
  for (const auto& margin : margins)
    if (margin.toDouble(-1) < 0) return false;
  for (const auto& value : plane.value("skippedUV").toArray()) {
    const auto pair = value.toArray();
    if (pair.size() != 2 || pair.at(0).toInt() < 1 || pair.at(1).toInt() < 1) return false;
  }
  return true;
}

std::pair<QJsonObject, QString> readRefineStore(const QString& path)
{
  QFile input(path);
  if (!input.exists()) return {QJsonObject{{"version", 1}, {"jobs", QJsonArray{}}}, {}};
  if (!input.open(QIODevice::ReadOnly)) return {{}, SceneModel::tr("Cannot read %1: %2").arg(path, input.errorString())};
  QJsonParseError error;
  const auto store = QJsonDocument::fromJson(input.readAll(), &error).object();
  if (input.error() != QFileDevice::NoError) return {{}, SceneModel::tr("Cannot read %1: %2").arg(path, input.errorString())};
  if (error.error != QJsonParseError::NoError)
    return {{}, SceneModel::tr("Invalid JSON in %1: %2").arg(path, error.errorString())};
  if (store.value("version").toInt() != 1 || !store.value("jobs").isArray())
    return {{}, SceneModel::tr("Unsupported Refine Job store in %1.").arg(path)};
  QSet<QUuid> ids;
  for (const auto& value : store.value("jobs").toArray()) {
    const auto job = value.toObject();
    const QUuid id(job.value("id").toString());
    if (id.isNull() || ids.contains(id)) return {{}, SceneModel::tr("Missing or duplicate Refine Job ID in %1.").arg(path)};
    ids.insert(id);
    const auto planes = job.value("planes").toArray();
    const auto placement = job.value("modelInBase").toArray();
    if (job.value("revision").toInt() < 1 || job.value("method") != "byThreePlanes"
        || job.value("coordinateConvention") != "workpiece-metric-grid-v1" || planes.size() != 3 || placement.size() != 4
        || job.value("name").toString().trimmed().isEmpty() || job.value("mountingId").toString().isEmpty()
        || (job.contains("workpieceId") && job.value("workpieceId").toString().trimmed().isEmpty())
        || job.value("modelSha256").toString().size() != 64 || job.value("modelPath").toString().isEmpty())
      return {{}, SceneModel::tr("Invalid or unsupported Refine Job %1 in %2.").arg(id.toString(), path)};
    for (const auto& row : placement) {
      const auto entries = row.toArray();
      int numericEntries = 0;
      for (const auto& value : entries) if (value.isDouble()) ++numericEntries;
      if (entries.size() != 4 || numericEntries != 4)
        return {{}, SceneModel::tr("Invalid placement in Refine Job %1 in %2.").arg(id.toString(), path)};
    }
    for (int role = 0; role < planes.size(); ++role)
      if (!validStoredPlane(planes.at(role).toObject(), role))
        return {{}, SceneModel::tr("Invalid plane %1 in Refine Job %2 in %3.").arg(role + 1).arg(id.toString(), path)};
  }
  return {store, {}};
}

QString writeRefineJob(const QJsonObject& previous, const QJsonObject& next)
{
  const QDir directory(QStringLiteral(ROBOCRAP_SETTINGS_DIR));
  const QString path = refineStorePath();
  if (!directory.mkpath(".")) return SceneModel::tr("Cannot create settings directory %1.").arg(directory.path());
  QLockFile lock(path + QStringLiteral(".lock"));
  if (!lock.tryLock(0)) return SceneModel::tr("Cannot lock %1 for writing.").arg(path);
  auto [store, error] = readRefineStore(path);
  if (!error.isEmpty()) return error;
  auto jobs = store.value("jobs").toArray();
  const QUuid id((previous.isEmpty() ? next : previous).value("id").toString());
  qsizetype row = 0;
  while (row < jobs.size() && QUuid(jobs.at(row).toObject().value("id").toString()) != id) ++row;
  const auto stored = row < jobs.size() ? jobs.at(row).toObject() : QJsonObject{};
  if (stored != previous)
    return SceneModel::tr("Refine Job %1 changed on disk. Restart to load the current store.").arg(id.toString());
  if (next == previous) return {};
  if (next.isEmpty()) jobs.removeAt(row);
  else if (row == jobs.size()) jobs.append(next);
  else jobs.replace(row, next);
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
  connect(viewport, &RoboCrap3D::OccController::workpieceModelsChanged, this, &SceneModel::refreshRefineSelection);
}

void SceneModel::showSelection(const QList<quint32>& ids)
{
  const auto* object = ids.size() == 1 ? findObject(ids.front()) : nullptr;
  m_selectedRefineJob = object && object->kind() == SceneObject::RefineJob ? object->objectId() : 0;
  if (!m_selectedRefineJob && m_viewport) m_viewport->setSelectedObjects(ids);
  refreshRefineSelection();
}

std::pair<quint32, QString> SceneModel::resolveJobWorkpiece(const QJsonObject& job) const
{
  const auto direct = m_viewport->resolveRefineTarget(job);
  if (job.contains("workpieceId")) return direct;
  const SceneObject* legacy = nullptr;
  for (const auto* object : m_objects) {
    const auto* gauge = object->endGauge();
    if (!gauge || gauge->modelSha256.isEmpty() || gauge->modelSha256 != job.value("modelSha256").toString()
        || !gauge->roughMountingDigests.contains(job.value("mountingId").toString())) continue;
    if (legacy || (direct.first && direct.first != object->objectId())) return {0, tr("Ambiguous legacy workpiece mounting.")};
    legacy = object;
  }
  if (!legacy) return direct;
  if (const auto error = workpieceRevisionError(legacy->objectId(), legacy->endGauge()->modelSha256); !error.isEmpty()) return {0, error};
  auto linked = job;
  linked.insert("workpieceId", legacy->endGauge()->workpieceId);
  return m_viewport->resolveRefineTarget(linked);
}

QString SceneModel::previewRefineJob(SceneObject& object)
{
  const auto job = object.refineJob();
  const auto [target, reason] = resolveJobWorkpiece(job);
  object.setRefineTarget(target);
  auto* workpiece = findObject(target);
  if (!workpiece || !workpiece->endGauge()) {
    m_viewport->showRefineContacts(job, 0);
    return reason.isEmpty() ? tr("The Refine Job's workpiece is missing from the scene.") : reason;
  }
  setObjectVisible(workpiece, true);
  const auto error = m_viewport->showRefineContacts(job, target);
  if (!error.isEmpty() || job.contains("workpieceId")) return error;

  auto linked = job;
  linked.insert("workpieceId", workpiece->endGauge()->workpieceId);
  linked.insert("mountingId", workpiece->endGauge()->observationsSha256);
  const auto [confirmed, conflict] = m_viewport->resolveRefineTarget(linked);
  if (confirmed != target) {
    object.setRefineTarget(0);
    m_viewport->showRefineContacts(job, 0);
    return conflict;
  }
  if (const auto failure = writeRefineJob(job, linked); !failure.isEmpty()) return failure;
  object.setRefineJob(linked, target);
  return {};
}

void SceneModel::refreshRefineSelection()
{
  auto* object = findObject(m_selectedRefineJob);
  const QString error = object && m_viewport ? previewRefineJob(*object) : QString{};
  if (m_refineSelectionError == error) return;
  m_refineSelectionError = error;
  emit refineSelectionErrorChanged();
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
    SceneObject* object = findObject(id);
    if (!object) continue;
    if (object->kind() == SceneObject::RefineJob) {
      const auto job = object->refineJob();
      if (const QString error = writeRefineJob(job, {}); !error.isEmpty()) {
        emit refineJobError(error);
        continue;
      }
      removeObjects(refinePathIds(job.value("id").toString()));
    }
    if (object->endGauge()) removeObjects(refinePathIds({}, id));
    m_objectsById.remove(id);
    m_objects.removeOne(object);
    disconnect(object, nullptr, this, nullptr);
    if (m_viewport) m_viewport->removeObject(id);
    if (object->endGauge()) refreshRefineSelection();
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

void SceneModel::loadRefineJobs()
{
  const auto [store, error] = readRefineStore(refineStorePath());
  if (!error.isEmpty()) { emit refineJobError(error); return; }
  QSet<QString> loaded;
  for (const auto* object : m_objects)
    if (object->kind() == SceneObject::RefineJob) loaded.insert(object->refineJob().value("id").toString());
  const auto jobs = store.value("jobs").toArray();
  for (const auto& entry : jobs) {
    const auto job = entry.toObject();
    if (loaded.contains(job.value("id").toString())) continue;
    if (!m_nextId) { emit refineJobError(tr("Scene object IDs are exhausted.")); return; }
    appendRefineObject(job, 0);
  }
}

SceneObject* SceneModel::appendRefineObject(const QJsonObject& job, quint32 target)
{
  auto* object = createObject({job.value("name").toString(), SceneObject::RefineJob, SceneObject::Unclassified, {}});
  object->setRefineJob(job, target);
  return appendObject(object);
}

QList<quint32> SceneModel::refinePathIds(const QString& jobId, quint32 target) const
{
  QList<quint32> ids;
  for (const auto* object : m_objects) {
    const auto* path = object->touchPath();
    if (path && !path->source.uuid.isEmpty() && (jobId.isEmpty() || path->source.uuid == jobId)
        && (!target || path->source.target == target)) ids.append(object->objectId());
  }
  return ids;
}

QVariantMap SceneModel::createRefineJob(quint32 target, const QJsonArray& planes)
{
  return saveRefineJob(0, target, planes);
}

QVariantMap SceneModel::saveRefineJob(quint32 jobId, quint32 target, const QJsonArray& planes)
{
  const auto* workpiece = findObject(target);
  if (!workpiece || !m_viewport) return {{"error", tr("Select a placed workpiece.")}};
  auto* object = findObject(jobId);
  if (jobId && (!object || object->kind() != SceneObject::RefineJob)) return {{"error", tr("The Refine Job was removed.")}};
  if (!jobId && !m_nextId) return {{"error", tr("Scene object IDs are exhausted.")}};
  auto [job, error] = m_viewport->refineJobDefinition(target, planes);
  if (!error.isEmpty()) return {{"error", error}};
  const auto previous = object ? object->refineJob() : QJsonObject{};
  const QString id = object ? previous.value("id").toString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
  job.insert("id", id);
  job.insert("revision", previous.value("revision").toInt(1));
  job.insert("name", object ? object->name() : tr("Refine Job %1").arg(id.left(8)));
  job.insert("workpieceName", workpiece->name());
  if (object) {
    // Scene IDs are process-local. Reopening the same mounted object alone is not an edit.
    if (object->refineTarget() == target) job.insert("workpieceSceneId", previous.value("workpieceSceneId"));
    if (job != previous) {
      if (previous.value("revision").toInt() == std::numeric_limits<int>::max())
        return {{"error", tr("Refine Job revisions are exhausted.")}};
      job.insert("revision", previous.value("revision").toInt() + 1);
      job.insert("workpieceSceneId", qint64(target));
    }
  }
  if (const QString failure = writeRefineJob(previous, job); !failure.isEmpty()) return {{"error", failure}};
  if (!object) object = appendRefineObject(job, target);
  else if (job != previous) {
    removeObjects(refinePathIds(id));
    object->setRefineJob(job, target);
    if (m_selectedRefineJob == jobId) refreshRefineSelection();
  }
  return {{"objectId", object->objectId()}};
}

RoboCrap3D::RefineContacts SceneModel::refineContacts(quint32 jobId, std::optional<double> eg22BallRadius) const
{
  const auto* object = findObject(jobId);
  if (!object || object->kind() != SceneObject::RefineJob) return tr("Select a saved Refine Job.");
  if (!m_viewport) return tr("The workpiece CAD is unavailable.");
  return m_viewport->refineContacts(object->refineJob(), object->refineTarget(), eg22BallRadius);
}

QString SceneModel::refineScanError(const RefineScanSource& source, const ProbeTouchPlan& plan)
{
  const auto* owner = findObject(source.job);
  const auto job = owner ? owner->refineJob() : QJsonObject{};
  const auto* target = findObject(source.target);
  const auto* gauge = target ? target->endGauge() : nullptr;
  const auto& mount = plan.gauge;
  if (!m_viewport || job.value("id").toString() != source.uuid || job.value("revision").toInt() != source.revision
      || resolveRefineTarget(source.job) != source.target || !gauge || !mount
      || gauge->workpieceInBase != mount->workpieceInBase || gauge->observationsSha256 != mount->observationsSha256)
    return tr("The Refine Job or its mounted workpiece changed. Generate again.");
  return workpieceRevisionError(source.target, job.value("modelSha256").toString());
}

QString SceneModel::workpieceRevisionError(quint32 target, const QString& expected) const
{
  const auto loaded = m_viewport ? m_viewport->workpieceModelRevision(target) : QString{};
  if (loaded.isEmpty()) return tr("Workpiece CAD is unavailable.");
  if (loaded != expected)
    return tr("The workpiece model has changed. Reassign the three faces.");
  return m_viewport->workpieceRevisionError(target);
}

QString SceneModel::savedRefineJobError(quint32 jobId) const
{
  const auto [store, error] = readRefineStore(refineStorePath());
  if (!error.isEmpty()) return error;
  const auto* object = findObject(jobId);
  if (object && store.value("jobs").toArray().contains(object->refineJob())) return {};
  return tr("The sampled Refine Job changed on disk.");
}

std::pair<quint32, QString> SceneModel::publishRefineScan(const RefineScanSource& source, ProbeTouchMotion motion)
{
  if (const QString error = refineScanError(source, motion.plan()); !error.isEmpty()) return {0, error};
  const auto* owner = findObject(source.job);
  const auto oldIds = refinePathIds(source.uuid);
  const auto* old = findObject(oldIds.value(0));
  const QString name = old ? old->name() : tr("Scan Path · %1").arg(owner->name());
  auto* path = addCalibrationObject(SceneTouchPath{std::make_shared<const ProbeTouchMotion>(std::move(motion)),
                                                   source}, name);
  if (!path) return {0, tr("Cannot insert the Scan Path into the scene.")};
  if (old && !old->visible()) setObjectVisible(path, false);
  removeObjects(oldIds);
  return {path->objectId(), {}};
}

quint32 SceneModel::resolveRefineTarget(quint32 jobId)
{
  auto* object = findObject(jobId);
  if (!object || object->kind() != SceneObject::RefineJob || !m_viewport) return 0;
  const auto target = resolveJobWorkpiece(object->refineJob()).first;
  object->setRefineTarget(target);
  return target;
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
  if (object->endGauge()) refreshRefineSelection();
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
    kind = !path->source.uuid.isEmpty() || path->motion->plan().passes.front().purpose == ProbeTouchPurpose::Gauge
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
  if (object->kind() == SceneObject::RefineJob) {
    const auto previous = object->refineJob();
    auto next = previous;
    next.insert("name", trimmedName);
    if (const QString error = writeRefineJob(previous, next); !error.isEmpty()) {
      emit refineJobError(error);
      return false;
    }
    object->setRefineJob(next, object->refineTarget());
  }
  object->setName(trimmedName);
  return true;
}

bool SceneModel::setObjectVisible(SceneObject* object, bool visible)
{
  if (!object || m_objectsById.value(object->objectId()) != object) return false;
  object->setVisible(visible);
  return true;
}
