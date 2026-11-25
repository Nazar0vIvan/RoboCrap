#include "viewport/occcontroller.h"

#include "occt/cadloadworker.h"
#include "occt/occviewport.h"
#include "occt/occviewwindow.h"

#include "geometry/plane.h"
#include <Eigen/SVD>
#include <QJSEngine>
#include <QDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <utility>

#include <Font_FontMgr.hxx>
#include <Standard_Failure.hxx>

namespace RoboCrap3D {

namespace {

bool usesModel(const DrawingObject& object, const QString& path)
{
  for (const auto& part : object.parts)
    if (const auto* model = std::get_if<DrawingModel>(&part.geometry); model && model->path == path) return true;
  return false;
}

} // namespace

OccController::OccController(QString occtResources, QObject* parent)
  : QObject(parent), m_occtResources(std::move(occtResources))
{
  createWindow();
}

OccController::~OccController()
{
  m_shuttingDown = true;
  ++m_generation;
  for (auto* worker : {m_worker.get(), m_modelWorker.get()}) {
    if (!worker) continue;
    worker->requestInterruption();
    worker->wait();
  }
  if (m_window) {
    detachViewWindow(m_window);
    delete m_window.data();
  }
}

QWindow* OccController::viewWindow() const
{
  return m_window.data();
}

bool OccController::isReady() const
{
  return !m_loading && m_window && m_window->isReady();
}

bool OccController::isViewportReady() const
{
  return m_window && m_window->isViewportReady();
}

OccViewport* OccController::viewport()
{
  return m_window ? m_window->viewport() : nullptr;
}

// Without a native surface nothing is drawn now; the accepted document is replayed later.
ViewportOperationResult OccController::insertObject(const DrawingObject& object)
{
  if (!object.id || object.parts.isEmpty()) return {false, QStringLiteral("Empty object drawing.")};
  auto* drawing = viewport();
  const auto result = drawing ? drawing->insertObject(object) : ViewportOperationResult{true, {}};
  if (result.accepted) {
    m_document.objects.insert(object.id, object);
    if (drawing) applySelection(*drawing);
    // A model drawn before its load finishes is skipped; finishModelLoad redraws the object.
    for (const auto& part : object.parts)
      if (const auto* model = std::get_if<DrawingModel>(&part.geometry);
          model && !m_document.models.contains(model->path) && !m_modelQueue.contains(model->path))
        m_modelQueue.append(model->path);
    startModelLoad();
  }
  if (!result.error.isEmpty()) emit message(result.error, true);
  return result;
}

const DrawingModel* DrawingDocument::workpieceModel(quint32 id) const
{
  const auto object = objects.constFind(id);
  if (object != objects.cend())
    for (const auto& part : object->parts) {
      const auto* model = std::get_if<DrawingModel>(&part.geometry);
      if (model && !model->mountingId.isEmpty() && !part.style.decoration && model->modelInBase.allFinite())
        return model;
    }
  return nullptr;
}

QString OccController::workpieceModelRevision(quint32 objectId) const
{
  const auto* model = m_document.workpieceModel(objectId);
  if (!model) return {};
  const auto loaded = m_document.models.value(model->path);
  return loaded && loaded->error.isEmpty() ? QString::fromLatin1(loaded->modelSha256.toHex()) : QString{};
}

void OccController::setRefineTarget(quint32 objectId)
{
  const auto* model = m_document.workpieceModel(objectId);
  m_document.face = model ? DrawingFaceSelection{objectId, model->path, m_document.models.value(model->path)}
                          : DrawingFaceSelection{};
  if (auto* drawing = viewport()) applySelection(*drawing);
}

QString OccController::showRefineFace(int index, bool reversed, bool picking)
{
  auto& face = m_document.face;
  const auto previous = face;
  if (face.index != index) face.highlighted = {};
  face.index = index;
  face.reversed = reversed;
  face.picking = picking;
  if (auto* drawing = viewport()) {
    const auto result = drawing->setSelection();
    if (result.accepted && !result.error.isEmpty()) emit message(result.error, true);
    if (!result.accepted) {
      face = previous;
      applySelection(*drawing);
      return result.error;
    }
  }
  return {};
}

quint32 OccController::resolveRefineTarget(const QJsonObject& job, quint32 liveTarget) const
{
  if (m_document.workpieceModel(liveTarget)) return liveTarget;
  quint32 match = 0;
  for (const auto& object : m_document.objects) {
    const auto* model = m_document.workpieceModel(object.id);
    if (!model || model->mountingId != job.value("mountingId").toString()) continue;
    if (model->path != job.value("modelPath").toString()
        && workpieceModelRevision(object.id) != job.value("modelSha256").toString()) continue;
    if (match) return 0;
    match = object.id;
  }
  return match;
}

QVariantMap OccController::restoreRefineGrids(const QJsonObject& job, const QJsonArray& planes)
{
  const auto target = m_document.face.target;
  if (workpieceModelRevision(target) != job.value("modelSha256").toString())
    return {{"error", tr("The workpiece model has changed. Reassign the three faces.")}, {"staleFaces", true}};
  auto [current, error, grids] = checkRefineJob(target, planes);
  if (!error.isEmpty()) {
    if (!m_document.face.revisionError().isEmpty())
      return {{"error", tr("%1 Reload the workpiece model before assigning faces.").arg(error)}, {"staleFaces", true}};
    return {{"error", error}};
  }
  m_document.face.grids = std::move(grids);
  return {{"placementChanged", current.value("modelInBase") != job.value("modelInBase")
                            || current.value("mountingId") != job.value("mountingId")}};
}

std::pair<QJsonObject, QString> OccController::refineJobDefinition(quint32 target, const QJsonArray& planes) const
{
  auto result = checkRefineJob(target, planes);
  return {std::move(result.definition), std::move(result.error)};
}

OccController::CheckedRefineJob OccController::checkRefineJob(quint32 target, const QJsonArray& planes) const
{
  const auto* model = m_document.workpieceModel(target);
  auto face = m_document.face;
  if (!model || target != face.target || model->path != face.path || !face.source
      || face.source != m_document.models.value(model->path))
    return {{}, tr("Select the current placed workpiece again.")};
  if (const QString error = face.revisionError(); !error.isEmpty()) return {{}, error};
  if (planes.size() != 3) return {{}, tr("Assign XPlane, YPlane and ZPlane.")};

  Eigen::Matrix3d normals;
  QSet<int> faces;
  QHash<int, DrawingGrid> grids;
  QJsonArray savedPlanes;
  int total = 0;
  for (int role = 0; role < 3; ++role) {
    const QString name = QStringLiteral("%1Plane").arg(QStringLiteral("XYZ").at(role));
    const QString prefix = name + QStringLiteral(": ");
    const auto draft = planes.at(role).toObject();
    face.index = draft.value("faceIndex").toInt();
    if (face.index <= 0) return {{}, prefix + tr("Assign a planar face.")};
    if (faces.contains(face.index)) return {{}, prefix + tr("Choose a different face for each role.")};
    faces.insert(face.index);
    const QSize counts(draft.value("uCount").toInt(), draft.value("vCount").toInt());
    const QSizeF requested(draft.value("uSpacing").toDouble(), draft.value("vSpacing").toDouble());
    QStringList margins;
    for (const auto& value : draft.value("margins").toArray()) margins.append(value.toString());
    auto preview = face.preview(counts, requested, margins);
    if (!preview.grid.fits) return {{}, prefix + preview.status};

    const auto skipped = draft.value("skipped");
    if (!skipped.isArray() || !draft.value("reversed").isBool()) return {{}, prefix + tr("Invalid plane draft.")};
    QSet<QPoint> excluded;
    for (const auto& value : skipped.toArray()) {
      const auto pair = value.toArray();
      const QPoint index(pair.at(0).toInt(), pair.at(1).toInt());
      if (pair.size() != 2 || index.x() < 1 || index.x() > counts.width()
          || index.y() < 1 || index.y() > counts.height() || excluded.contains(index))
        return {{}, prefix + tr("Each skipped index must occur once and lie inside the grid.")};
      excluded.insert(index);
    }
    QList<V3d> included;
    for (int i = 0; i < preview.grid.points.size(); ++i) {
      const QPoint index = preview.grid.indexAt(i);
      if (excluded.contains(index)) continue;
      if (preview.grid.invalid.contains(i))
        return {{}, prefix + tr("Point (%1,%2) is off the trimmed face. Skip it or change the grid.")
                                 .arg(index.x()).arg(index.y())};
      included.append(preview.grid.points.at(i));
    }
    if (included.size() < 3) return {{}, prefix + tr("At least three included points are required.")};
    const auto plane = Plane::fromPoints(included);
    if (!plane) return {{}, prefix + tr("Included points must be distinct and noncollinear.")};
    normals.row(role) = plane->normal().transpose();
    total += included.size();
    preview.grid.skipped = std::move(excluded);
    grids.insert(face.index, std::move(preview.grid));
    QJsonArray marginsMm;
    for (const auto& margin : margins) marginsMm.append(margin.toDouble());
    savedPlanes.append(QJsonObject{{"role", name}, {"faceIndex", face.index}, {"reversed", draft.value("reversed")},
        {"countsUV", QJsonArray{counts.width(), counts.height()}},
        {"spacingMmUV", QJsonArray{preview.spacing.width(), preview.spacing.height()}},
        {"autoSpacingUV", QJsonArray{requested.width() == -1, requested.height() == -1}},
        {"marginsMmLeftTopBottomRight", marginsMm}, {"skippedUV", skipped}, {"includedCount", int(included.size())}});
  }
  const Eigen::JacobiSVD<Eigen::Matrix3d> svd(normals);
  const double minimumRatio = std::sqrt(3.0) * 0.01 / 0.20;
  if (svd.info() != Eigen::Success || svd.singularValues().minCoeff() < minimumRatio * svd.singularValues().maxCoeff())
    return {{}, tr("The plane normals are dependent or ill-conditioned. Choose three independent directions.")};

  QJsonArray placement;
  for (int i = 0; i < 4; ++i)
    placement.append(QJsonArray{model->modelInBase(i,0), model->modelInBase(i,1),
                                model->modelInBase(i,2), model->modelInBase(i,3)});
  return {QJsonObject{{"method", "byThreePlanes"}, {"coordinateConvention", "workpiece-metric-grid-v1"},
                      {"workpieceSceneId", qint64(target)}, {"mountingId", model->mountingId},
                      {"modelPath", model->path}, {"modelSha256", QString::fromLatin1(face.source->modelSha256.toHex())},
                      {"modelInBase", placement}, {"planes", savedPlanes}, {"includedCount", total}}, {}, std::move(grids)};
}

QVariantMap OccController::updateRefineGrid(QSize counts, QSizeF spacing, const QStringList& margins)
{
  auto& face = m_document.face;
  auto [grid, status, usedSpacing] = face.preview(counts, spacing, margins);
  const auto invalid = grid.invalid.values();
  if (face.index) {
    auto& stored = face.grids[face.index];
    grid.skipped = std::move(stored.skipped);
    stored = std::move(grid);
  }
  if (auto* drawing = viewport()) {
    const auto displayed = drawing->setSelection();
    if (!displayed.error.isEmpty()) status = (status + " " + displayed.error).trimmed();
  }
  return {{QStringLiteral("message"), status}, {QStringLiteral("spacing"), usedSpacing},
          {QStringLiteral("invalid"), QVariant::fromValue(invalid)}};
}

void OccController::setGridVisibility(bool showAll, const QList<int>& assignedFaces)
{
  auto& face = m_document.face;
  for (auto it = face.grids.begin(); it != face.grids.end(); ) {
    if (assignedFaces.contains(it.key())) ++it;
    else it = face.grids.erase(it);
  }
  face.showAllPoints = showAll;
  if (auto* drawing = viewport()) applySelection(*drawing);
}

void OccController::setGridSelection(const QList<QPoint>& skipped, QPoint highlighted, bool picking)
{
  auto& face = m_document.face;
  face.pickingPoints = picking && face.index && !face.picking;
  if (!face.index) return;
  auto& grid = face.grids[face.index];
  const QSet<QPoint> indices(skipped.begin(), skipped.end());
  if (grid.skipped == indices && face.highlighted == highlighted) return;
  grid.skipped = indices;
  face.highlighted = highlighted;
  if (auto* drawing = viewport()) applySelection(*drawing);
}

void OccController::removeObject(quint32 id)
{
  m_document.objects.remove(id);
  if (auto* drawing = viewport()) {
    if (const auto removed = drawing->removeObject(id); !removed.error.isEmpty()) emit message(removed.error, true);
    applySelection(*drawing);
  }
}

void OccController::setObjectVisible(quint32 id, bool visible)
{
  auto it = m_document.objects.find(id);
  if (it == m_document.objects.end() || it->visible == visible) return;
  auto* drawing = viewport();
  const auto result = drawing ? drawing->setObjectVisible(id, visible) : ViewportOperationResult{true, {}};
  if (result.accepted) {
    it->visible = visible;
    if (drawing) applySelection(*drawing);
  }
  if (!result.error.isEmpty()) emit message(result.error, true);
}

void OccController::setObjectDiagnostics(quint32 id, const DrawingDiagnostics& diagnostics)
{
  auto it = m_document.objects.find(id);
  if (it == m_document.objects.end()) return;
  auto* drawing = viewport();
  const auto result = drawing ? drawing->setObjectDiagnostics(id, diagnostics) : ViewportOperationResult{true, {}};
  if (result.accepted) it->diagnostics = diagnostics;
  if (!result.error.isEmpty()) emit message(result.error, true);
}

void OccController::setDraftAxis(const std::optional<DrawingAxis>& axis)
{
  const auto& previous = m_document.axis;
  if ((!axis && !previous) || (axis && previous && axis->center == previous->center
      && axis->direction == previous->direction && axis->length == previous->length)) return;
  auto* drawing = viewport();
  const auto result = drawing ? drawing->setDraftAxis(axis) : ViewportOperationResult{true, {}};
  if (result.accepted) m_document.axis = axis;
  if (!result.error.isEmpty()) emit message(result.error, true);
}

void OccController::setShowSpindleTcp(bool visible)
{
  if (m_showSpindleTcp == visible) return;
  m_showSpindleTcp = visible;
  emit showSpindleTcpChanged();
}

void OccController::setShowSpindleCollet(bool visible)
{
  if (m_showSpindleCollet == visible) return;
  m_showSpindleCollet = visible;
  emit showSpindleColletChanged();
}

void OccController::setActiveTool(RobotTool tool)
{
  if (m_activeTool == tool) return;
  m_activeTool = tool;
  if (m_window) m_window->setActiveTool(tool);
}

void OccController::setSpindleFrames(const std::optional<M4d>& tcp, const std::optional<M4d>& collet)
{
  m_pose.tcp = tcp;
  m_pose.collet = collet;
  auto* drawing = viewport();
  if (!drawing) return;
  if (const auto frames = drawing->setSpindleFrames(tcp, collet); !frames.accepted) emit message(frames.error, true);
}

void OccController::setSelectedObjects(const QList<quint32>& ids)
{
  m_document.selected = ids;
  if (auto* drawing = viewport()) applySelection(*drawing);
}

void OccController::setToolSelected(bool selected)
{
  m_document.toolSelected = selected;
  if (auto* drawing = viewport()) applySelection(*drawing);
}

void OccController::applySelection(OccViewport& drawing)
{
  const auto result = drawing.setSelection();
  if (!result.error.isEmpty()) emit message(result.error, true);
}

QString OccController::initializeOcct() const
{
  const QDir resources(m_occtResources);
  if (!resources.exists(QStringLiteral("Shaders")) || !resources.exists(QStringLiteral("XSTEPResource"))) {
    return QStringLiteral("OCCT runtime resources are missing: %1").arg(m_occtResources);
  }

  const std::pair<const char*, const char*> paths[] = {
    {"CSF_ShadersDirectory", "Shaders"}, {"CSF_MDTVTexturesDirectory", "Textures"},
    {"CSF_SHMessage", "SHMessage"}, {"CSF_XSMessage", "XSMessage"},
    {"CSF_STEPDefaults", "XSTEPResource"}, {"CSF_PluginDefaults", "StdResource"},
    {"CSF_StandardDefaults", "StdResource"}, {"CSF_XCAFDefaults", "StdResource"}
  };
  for (const auto& [variable, directory] : paths)
    qputenv(variable, resources.filePath(QLatin1String(directory)).toUtf8());
  try {
    Font_FontMgr::GetInstance()->InitFontDataBase();
    return {};
  } catch (const Standard_Failure& failure) {
    return QStringLiteral("Cannot initialize OCCT: %1").arg(QString::fromUtf8(failure.what()));
  }
}

void OccController::createWindow()
{
  if (m_window || m_shuttingDown) return;
  if (!m_occtInitialized) {
    const QString error = initializeOcct();
    m_occtInitialized = error.isEmpty();
    if (!m_occtInitialized) {
      setError(error);
      return;
    }
  }
  m_window = new OccViewWindow(m_document, m_pose);
  QJSEngine::setObjectOwnership(m_window, QJSEngine::CppOwnership);
  QObject::connect(m_window, &OccViewWindow::readyChanged, this, &OccController::readyChanged);
  QObject::connect(m_window, &OccViewWindow::viewportReadyChanged, this, &OccController::viewportReadyChanged);
  QObject::connect(m_window, &OccViewWindow::gridPointPicked, this, &OccController::gridPointPicked);
  QObject::connect(m_window, &OccViewWindow::gridEditKeyPressed, this, &OccController::gridEditKeyPressed);
  QObject::connect(m_window, &OccViewWindow::facePicked, this, &OccController::facePicked);
  QObject::connect(m_window, &OccViewWindow::applicationSelectionRequested,
                   this, &OccController::applicationSelectionRequested);
  QObject::connect(m_window, &OccViewWindow::toolSelectionRequested, this, &OccController::toolSelectionRequested);
  QObject::connect(m_window, &OccViewWindow::errorOccurred, this, &OccController::setError);
  QObject::connect(m_window, &QObject::destroyed, this, &OccController::onViewWindowDestroyed);
  QObject::connect(m_window, &OccViewWindow::deleteSelectionRequested, this, &OccController::deleteSelectionRequested);
  if (m_visuals && m_shapes) m_window->setScene(*m_visuals, m_shapes);
  m_window->setActiveTool(m_activeTool);
  emit viewWindowChanged();
}

void OccController::detachViewWindow(QWindow* window)
{
  if (!m_window || window != m_window.data()) return;
  m_window->hide();
  m_window->releaseSurface();
  m_window->setParent(nullptr);
}

void OccController::setError(const QString& error)
{
  if (m_error == error) return;
  m_error = error;
  emit errorStringChanged();
  if (!error.isEmpty()) emit message(error, true);
}

ViewportOperationResult OccController::loadRobot(const RobotVisualModel& visuals,
                                                 const DrawingPose& pose)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_shuttingDown) return {false, QStringLiteral("The viewport is shutting down.")};
  if (m_visuals) return {m_error.isEmpty(), m_error};
  createWindow();
  if (!m_window) return {false, m_error};
  m_visuals = visuals;
  m_pose = pose;
  QStringList sources;
  for (const auto& link : visuals.links) sources.append(link.fileName);
  sources += visuals.tools;
  setError({});
  m_loading = true;
  emit loadingChanged();
  emit readyChanged();
  const quint64 generation = ++m_generation;
  m_worker = std::make_unique<CadLoadWorker>(sources);
  QObject::connect(m_worker.get(), &QThread::finished, this,
                   std::bind(&OccController::finishRobotLoad, this, generation), Qt::QueuedConnection);
  m_worker->start();
  return {true, {}};
}

void OccController::onViewWindowDestroyed()
{
  if (m_shuttingDown) return;
  emit readyChanged();
  emit viewportReadyChanged();
  // A host may destroy its child QWindow before QML's detach handler runs.
  // QPointer prevents double deletion; renderer replay values are retained.
  QTimer::singleShot(0, this, &OccController::createWindow);
}

void OccController::finishRobotLoad(quint64 generation)
{
  if (m_shuttingDown || generation != m_generation) return;
  m_worker->wait();
  auto result = std::make_shared<CadLoadResult>(m_worker->takeResult());
  m_worker.reset();
  m_loading = false;
  emit loadingChanged();
  if (!result->error.isEmpty()) {
    setError(result->error);
    emit readyChanged();
    return;
  }
  m_shapes = std::move(result);
  if (m_window)
    m_window->setScene(*m_visuals, m_shapes);
  else
    createWindow();
  emit readyChanged();
}

void OccController::startModelLoad()
{
  if (m_shuttingDown || m_modelWorker || m_modelQueue.isEmpty()) return;
  m_modelWorker = std::make_unique<CadLoadWorker>(QStringList{m_modelQueue.first()});
  QObject::connect(m_modelWorker.get(), &QThread::finished, this, &OccController::finishModelLoad,
                   Qt::QueuedConnection);
  m_modelWorker->start();
}

void OccController::finishModelLoad()
{
  if (m_shuttingDown) return;
  m_modelWorker->wait();
  const auto result = std::make_shared<const CadLoadResult>(m_modelWorker->takeResult());
  m_modelWorker.reset();
  const QString path = m_modelQueue.takeFirst();
  // A failed path stays recorded, so it is reported once and never retried.
  m_document.models.insert(path, result);
  if (!result->error.isEmpty()) {
    emit message(result->error, true);
  } else if (auto* drawing = viewport()) {
    for (const auto& object : std::as_const(m_document.objects)) {
      if (!usesModel(object, path)) continue;
      const auto redrawn = drawing->insertObject(object);
      if (!redrawn.error.isEmpty()) emit message(redrawn.error, true);
    }
    applySelection(*drawing);
  }
  emit workpieceModelsChanged();
  startModelLoad();
}

ViewportOperationResult OccController::applyRobotTransforms(const DrawingPose& pose)
{
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isReady()) return {false, QStringLiteral("The robot preview is not ready.")};
  const auto result = m_window->applyTransforms(pose);
  if (result.accepted) {
    m_pose = pose;
    setError({});
  } else if (m_error == result.error) {
    emit message(result.error, true);
  } else {
    setError(result.error);
  }
  return result;
}

} // namespace RoboCrap3D
