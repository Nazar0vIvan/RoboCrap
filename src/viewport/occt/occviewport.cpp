#include "occviewport.h"

#include "cadloadresult.h"
#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidExplorer.hxx>
#include <BRepClass_FaceExplorer.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <QCryptographicHash>
#include <QFile>
#include <QDebug>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <functional>
#include <limits>

namespace RoboCrap3D {

OccViewport::OccViewport(Aspect_Handle handle, const DrawingDocument& document)
  : m_document(document), m_viewer(handle),
    m_scene(m_viewer.context(), m_viewer.view()),
    m_robot(m_scene),
    m_application(m_scene, document.models),
    m_input(m_viewer.context(), m_viewer.view())
{
  m_timer.setSingleShot(true);
  m_timer.setInterval(0);
  QObject::connect(&m_timer, &QTimer::timeout, &m_timer, std::bind(&OccViewport::flushRender, this));
  if (!m_viewer.isValid()) return;
  m_scene.displayViewCube();
  for (const auto& object : document.objects) {
    const auto result = m_application.insertObject(object);
    if (!result.accepted) qWarning().noquote() << "Object drawing replay:" << result.error;
  }
  fitInitialCamera();
}

ViewportOperationResult OccViewport::setRobot(const RobotVisualModel& visuals,
                                              const CadLoadResult& shapes,
                                              const DrawingPose& pose)
{
  if (!isValid()) return {false, QStringLiteral("Robot installation: viewport is unavailable.")};
  auto result = m_robot.load(visuals, shapes, pose);
  if (result.accepted) fitInitialCamera();
  // Draw now rather than on the next event-loop turn, so the robot is on screen
  // when readiness (which hides the loading overlay) is reported after this call.
  flushRender();
  return result;
}

ViewportOperationResult OccViewport::setActiveTool(RobotTool active)
{
  if (!isValid()) return {false, QStringLiteral("Viewport is unavailable.")};
  auto result = m_robot.setActiveTool(active);
  requestRender();
  return result;
}

ViewportOperationResult OccViewport::removeObject(quint32 id)
{
  auto result = m_application.removeObject(id);
  if (result.accepted) requestRender();
  return result;
}

ViewportOperationResult OccViewport::insertObject(const DrawingObject& object)
{
  const auto result = m_application.insertObject(object);
  if (result.accepted && !m_robot.isLoaded()) fitInitialCamera();
  requestRender();
  return result;
}

ViewportOperationResult OccViewport::setObjectVisible(quint32 id, bool visible)
{
  const auto result = m_application.setObjectVisible(id, visible);
  requestRender();
  return result;
}

ViewportOperationResult OccViewport::setObjectDiagnostics(quint32 id, const DrawingDiagnostics& diagnostics)
{
  const auto result = m_application.setObjectDiagnostics(id, diagnostics);
  requestRender();
  return result;
}

namespace {

Handle(StdSelect_BRepOwner) faceOwner(const Handle(AIS_Shape)& shape, const TopoDS_Shape& face)
{
  const auto& selection = shape->Selection(AIS_Shape::SelectionMode(TopAbs_FACE));
  if (!selection.IsNull())
    for (const auto& entity : selection->Entities()) {
      const auto owner = Handle(StdSelect_BRepOwner)::DownCast(entity->BaseSensitive()->OwnerId());
      if (!owner.IsNull() && owner->Shape().IsSame(face)) return owner;
    }
  return {};
}

std::pair<std::optional<DrawingAxis>, QString> localFaceAxis(const TopoDS_Face& face)
{
  const BRepAdaptor_Surface surface(face);
  if (surface.GetType() != GeomAbs_Plane
      && !GeomLib_IsPlanarSurface(BRep_Tool::Surface(face), Precision::Confusion()).IsPlanar())
    return {{}, QStringLiteral("The picked face is curved. Pick a planar face.")};
  double u = (surface.FirstUParameter() + surface.LastUParameter()) / 2.0;
  double v = (surface.FirstVParameter() + surface.LastVParameter()) / 2.0;
  if (BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), Precision::PConfusion()).State() != TopAbs_IN
      && !BRepClass3d_SolidExplorer::FindAPointInTheFace(face, u, v))
    return {{}, QStringLiteral("Cannot locate the approach indicator inside the trimmed face.")};
  const auto sample = surface.EvalD1(u, v);
  const gp_Pnt& center = sample.Point;
  gp_Dir normal(sample.D1U.Crossed(sample.D1V));
  if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
  const double span = std::min((surface.LastUParameter() - surface.FirstUParameter()) * sample.D1U.Magnitude(),
                               (surface.LastVParameter() - surface.FirstVParameter()) * sample.D1V.Magnitude());
  return {DrawingAxis{V3d{center.X(), center.Y(), center.Z()},
                      V3d{normal.X(), normal.Y(), normal.Z()}, std::clamp(span / 3.0, 3.0, 15.0)}, {}};
}

double fittedGridPitch(double usableMm, int count)
{
  const double units = std::floor(1000.0 * (usableMm + Precision::Confusion()) / (count - 1));
  return std::clamp(units, 1.0, double(std::numeric_limits<int>::max())) / 1000.0;
}

bool onTrimmedFace(const gp_Pnt& point, GeomAPI_ProjectPointOnSurf& projection, BRepClass_FaceExplorer& trim)
{
  projection.Perform(point);
  const double tolerance = Precision::Confusion();
  if (!projection.IsDone() || !projection.NbPoints() || projection.LowerDistance() > tolerance) return false;
  double u, v;
  projection.LowerDistanceParameters(u, v);
  const auto state = BRepClass_FaceClassifier(trim, gp_Pnt2d(u, v), Precision::PConfusion()).State();
  return state == TopAbs_IN || state == TopAbs_ON;
}

} // namespace

DrawingGridPreview DrawingFaceSelection::preview(
    QSize counts, QSizeF spacing, const QStringList& margins) const
{
  if (!index) return {};
  if (!source || index < 1 || index > source->faces.Extent())
    return {{}, QStringLiteral("The assigned source face is unavailable.")};
  if (counts.width() < 2 || counts.height() < 2 || !std::isfinite(spacing.width())
      || !std::isfinite(spacing.height()) || (spacing.width() != -1 && spacing.width() <= 0)
      || (spacing.height() != -1 && spacing.height() <= 0))
    return {{}, QStringLiteral("Each direction needs at least two points and positive finite spacing.")};
  constexpr int maxPreviewPoints = 10000; // A synchronous preview classifies and draws one vertex per point.
  if (qint64(counts.width()) * counts.height() > maxPreviewPoints)
    return {{}, QStringLiteral("Live preview exceeds %1 points. Reduce either count.").arg(maxPreviewPoints)};

  // Panel order is left (-U), top (+V), bottom (-V), right (+U).
  std::array<double, 4> inset;
  if (margins.size() != qsizetype(inset.size())) return {{}, QStringLiteral("Enter all four margins.")};
  for (int i = 0; i < margins.size(); ++i) {
    bool ok;
    inset[i] = margins[i].toDouble(&ok);
    if (!ok || !std::isfinite(inset[i]) || inset[i] < 0)
      return {{}, QStringLiteral("Enter a finite, nonnegative margin on each side.")};
  }
  try {
    const TopoDS_Face& face = TopoDS::Face(source->faces(index));
    const auto [axis, error] = localFaceAxis(face);
    if (!axis) return {{}, error};
    const V3d& normal = axis->direction;
    Eigen::Index leastParallel;
    normal.cwiseAbs().minCoeff(&leastParallel); // First minimum gives X, Y, Z tie order.
    const V3d u = (V3d::Unit(leastParallel) - normal[leastParallel] * normal).normalized();
    const V3d v = normal.cross(u);
    const V3d origin = normal * normal.dot(axis->center);
    gp_Trsf toGrid;
    toGrid.SetTransformation(gp_Ax3({origin.x(), origin.y(), origin.z()},
                                    {normal.x(), normal.y(), normal.z()}, {u.x(), u.y(), u.z()}));
    Bnd_Box bounds;
    const auto aligned = face.Moved(TopLoc_Location(toGrid));
    for (TopExp_Explorer edge(aligned, TopAbs_EDGE); edge.More(); edge.Next())
      BRepBndLib::AddOptimal(edge.Current(), bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen()) return {{}, QStringLiteral("The trimmed face has no finite grid bounds.")};
    const auto low = bounds.CornerMin();
    const auto high = bounds.CornerMax();
    const Eigen::Vector2d lower(low.X() + inset[0], low.Y() + inset[2]);
    const Eigen::Vector2d upper(high.X() - inset[3], high.Y() - inset[1]);
    // An untouched pitch fits the current margins at the spin box's 0.001 mm resolution.
    const Eigen::Vector2d usable = upper - lower;
    if (spacing.width() == -1) spacing.setWidth(fittedGridPitch(usable.x(), counts.width()));
    if (spacing.height() == -1) spacing.setHeight(fittedGridPitch(usable.y(), counts.height()));
    const Eigen::Vector2d span((counts.width() - 1) * spacing.width(), (counts.height() - 1) * spacing.height());
    Eigen::Vector2d first = 0.5 * lower + 0.5 * upper - 0.5 * span;
    if (!first.allFinite() || !span.allFinite() || (first.cwiseAbs() + span).maxCoeff() >= Precision::Infinite())
      return {{}, QStringLiteral("The requested grid exceeds the CAD coordinate range.")};
    QStringList messages;
    for (int i = 0; i < 2; ++i) {
      if (span[i] <= usable[i] + Precision::Confusion()) continue;
      messages << QStringLiteral("%1 span %2 mm does not fit in %3 mm after margins.")
                  .arg(i ? "V" : "U").arg(span[i]).arg(usable[i]);
      const double minStart = i ? low.Y() : low.X();
      const double maxStart = (i ? high.Y() : high.X()) - span[i];
      if (minStart <= maxStart) first[i] = std::clamp(first[i], minStart, maxStart);
    }

    const BRepAdaptor_Surface surface(face);
    GeomAPI_ProjectPointOnSurf projection;
    projection.Init(BRep_Tool::Surface(face), surface.FirstUParameter(), surface.LastUParameter(),
                    surface.FirstVParameter(), surface.LastVParameter(), Precision::Confusion());
    BRepClass_FaceExplorer trim(TopoDS::Face(face.Oriented(TopAbs_FORWARD)));
    DrawingGrid grid;
    grid.normal = normal;
    grid.columns = counts.width();
    grid.fits = messages.isEmpty();
    grid.points.reserve(counts.width() * counts.height());
    QStringList invalid;
    for (int j = 0; j < counts.height(); ++j) {
      for (int i = 0; i < counts.width(); ++i) {
        const V3d point = origin + (first.x() + i * spacing.width()) * u + (first.y() + j * spacing.height()) * v;
        grid.points.append(point);
        if (!onTrimmedFace({point.x(), point.y(), point.z()}, projection, trim)) {
          grid.invalid.insert(j * grid.columns + i);
          invalid << QStringLiteral("(%1,%2)").arg(i + 1).arg(j + 1);
        }
      }
    }
    if (!invalid.isEmpty()) messages << QStringLiteral("Off the trimmed face (U,V): %1").arg(invalid.join(' '));
    return {std::move(grid), messages.join(' '), spacing};
  } catch (const Standard_Failure& failure) {
    return {{}, QStringLiteral("Cannot preview the face grid: %1").arg(QString::fromUtf8(failure.what()))};
  }
}

DrawingDiagnostics DrawingFaceSelection::markers() const
{
  DrawingDiagnostics result;
  for (auto it = grids.cbegin(); it != grids.cend(); ++it) {
    const bool active = it.key() == index;
    if (!active && !showAllPoints) continue;
    const auto& grid = it.value();
    for (int i = 0; i < grid.points.size(); ++i) {
      const auto& point = grid.points.at(i);
      const QPoint uv = grid.indexAt(i);
      const bool red = grid.invalid.contains(i) || grid.skipped.contains(uv);
      (red ? result.redMarkers : result.markers).append(point);
      if (active && !picking && uv == highlighted) result.highlightedMarkers.append(point);
    }
  }
  return result;
}

QPoint OccViewport::pickedGridPoint(QPoint pos) const
{
  const auto& face = m_document.face;
  const auto* model = m_document.workpieceModel(face.target);
  if (!m_application.hasFaceOverlay() || m_selectedFace.IsNull() || !model
      || m_document.models.value(model->path) != face.source) return {};
  const auto& view = m_viewer.view();
  const auto& camera = view->Camera();
  const int tolerance = m_viewer.context()->PixelTolerance();
  double bestDistance = tolerance * tolerance;
  double bestDepth = std::numeric_limits<double>::infinity();
  QPoint nearest;
  const auto grid = face.grids.value(face.index);
  for (int i = 0; i < grid.points.size(); ++i) {
    const V3d world = model->modelInBase.topRows<3>() * grid.points.at(i).homogeneous();
    const gp_Vec eyeToPoint(camera->Eye(), {world.x(), world.y(), world.z()});
    const double depth = eyeToPoint.Dot(gp_Vec(camera->Direction()));
    if (depth < camera->ZNear() || depth > camera->ZFar()) continue;
    int x, y;
    view->Convert(world.x(), world.y(), world.z(), x, y);
    const double dx = double(x) - pos.x(), dy = double(y) - pos.y();
    const double distance = dx * dx + dy * dy;
    if (distance > bestDistance || (distance == bestDistance && depth >= bestDepth)) continue;
    bestDistance = distance;
    bestDepth = depth;
    nearest = grid.indexAt(i);
  }
  return nearest;
}

QString DrawingFaceSelection::revisionError() const
{
  QFile file(path);
  QCryptographicHash revision(QCryptographicHash::Sha256);
  if (!source || !file.open(QIODevice::ReadOnly) || !revision.addData(&file) || file.error() != QFileDevice::NoError)
    return QStringLiteral("Cannot verify the source BREP revision.");
  if (revision.result() != source->modelSha256)
    return QStringLiteral("The source BREP has changed since it was loaded.");
  return {};
}

std::pair<int, QString> OccViewport::pickedPlane(quint32 objectId) const
{
  const auto& face = m_document.face;
  if (!objectId || objectId != face.target)
    return {0, QStringLiteral("Pick a face on the Object workpiece.")};
  const auto* model = m_document.workpieceModel(objectId);
  const auto load = m_document.models.value(face.path);
  if (!model || model->path != face.path || !load || load != face.source)
    return {0, QStringLiteral("The source model revision has changed.")};
  try {
    const auto owner = Handle(StdSelect_BRepOwner)::DownCast(m_viewer.context()->DetectedOwner());
    if (owner.IsNull() || !owner->HasShape() || owner->Shape().ShapeType() != TopAbs_FACE)
      return {0, QStringLiteral("Pick the interior of a planar face.")};
    if (owner->Selectable() != m_facePickShape)
      return {0, QStringLiteral("Pick the mounted workpiece solid.")};
    const int index = load->faces.FindIndex(owner->Shape());
    if (!index) return {0, QStringLiteral("The picked face does not belong to the loaded source BREP.")};
    const QString error = face.revisionError();
    return {error.isEmpty() ? index : 0, error};
  } catch (const Standard_Failure& failure) {
    return {0, QStringLiteral("Cannot inspect the face: %1").arg(QString::fromUtf8(failure.what()))};
  }
}

void OccViewport::setFacePickShape(const Handle(AIS_Shape)& shape)
{
  if (m_facePickShape == shape) return;
  const auto& context = m_viewer.context();
  context->ClearDetected(false);
  if (!m_facePickShape.IsNull() && context->IsDisplayed(m_facePickShape))
    m_scene.setFacePicking(m_facePickShape, false);
  m_facePickShape = shape;
  context->SetToHilightSelected(!shape.IsNull());
  if (!shape.IsNull()) m_scene.setFacePicking(shape, true);
}

ViewportOperationResult OccViewport::displaySelection()
{
  const DrawingFaceSelection noFace;
  const auto& face = m_document.selectedContacts ? noFace : m_document.face;
  const bool active = face.target && (face.picking || m_document.selected == QList<quint32>{face.target});
  const auto* model = active ? m_document.workpieceModel(face.target) : nullptr;
  const auto load = model ? m_document.models.value(model->path) : nullptr;
  auto parts = m_application.selectedParts(face.picking ? QList<quint32>{face.target} : m_document.selected);
  Handle(AIS_Shape) shape;
  if (load && !load->shapes.isEmpty())
    for (const auto part : parts) {
      const auto candidate = m_scene.partHandle(part);
      if (!candidate.IsNull() && candidate->Shape().IsSame(load->shapes.first())) {
        shape = candidate;
        break;
      }
    }
  setFacePickShape(face.picking ? shape : Handle(AIS_Shape){});
  const auto& context = m_viewer.context();
  if (!m_selectedFace.IsNull()) {
    if (context->IsSelected(m_selectedFace)) context->AddOrRemoveSelected(m_selectedFace, false);
    m_selectedFace.Nullify();
  }
  std::optional<DrawingAxis> axis;
  if (!shape.IsNull() && face.index && !face.picking) {
    if (model->path != face.path || load != face.source
        || face.index < 1 || face.index > load->faces.Extent())
      return {false, QStringLiteral("The assigned face is stale for this workpiece.")};
    const auto& local = TopoDS::Face(load->faces(face.index));
    auto [indicator, error] = localFaceAxis(local);
    if (!indicator) return {false, error};
    const auto rotation = model->modelInBase.topLeftCorner<3, 3>();
    const auto translation = model->modelInBase.topRightCorner<3, 1>();
    axis = DrawingAxis{rotation * indicator->center + translation,
                       rotation * indicator->direction * (face.reversed ? -1.0 : 1.0), indicator->length};
    const auto owner = faceOwner(shape, local);
    if (owner.IsNull()) return {false, QStringLiteral("The assigned face is unavailable in the viewport.")};
    m_scene.selectParts({});
    m_selectedFace = owner;
    context->AddOrRemoveSelected(owner, false);
  } else {
    if (const auto tool = m_robot.activeToolPart(); tool && m_document.toolSelected && !face.picking)
      parts.push_back(*tool);
    m_scene.selectParts(parts);
  }
  const bool drawGrid = !shape.IsNull() && load == face.source && model->path == face.path
                        && (axis || face.showAllPoints);
  if (!parts.empty() && m_document.selectedContacts && !m_document.selectedContacts->markers.isEmpty()) {
    const auto* workpiece = m_document.workpieceModel(m_document.selected.front());
    if (workpiece) return m_application.setDraftAxis({}, &*m_document.selectedContacts, workpiece->modelInBase);
  }
  const auto markers = drawGrid ? face.markers() : DrawingDiagnostics{};
  return m_application.setDraftAxis(axis, &markers, drawGrid ? model->modelInBase : M4d::Identity());
}

ViewportOperationResult OccViewport::setSelection()
{
  requestRender();
  ViewportOperationResult result;
  try {
    result = displaySelection();
  } catch (const Standard_Failure& failure) {
    result.error = QStringLiteral("Selection: %1").arg(QString::fromUtf8(failure.what()));
  }
  if (!result.accepted) {
    const DrawingDiagnostics empty;
    const auto cleared = m_application.setDraftAxis({}, &empty);
    if (!cleared.error.isEmpty()) result.error += " " + cleared.error;
  }
  return result;
}

ViewportOperationResult OccViewport::setDraftAxis(const std::optional<DrawingAxis>& axis)
{
  const auto result = m_application.setDraftAxis(axis);
  requestRender();
  return result;
}

bool OccViewport::isValid() const
{
  return m_viewer.isValid();
}

ViewportOperationResult OccViewport::setSpindleFrames(const std::optional<M4d>& tcp, const std::optional<M4d>& collet)
{
  auto result = m_robot.setSpindleFrames(tcp, collet);
  requestRender();
  return result;
}

ViewportOperationResult OccViewport::applyTransforms(const DrawingPose& pose)
{
  if (!isValid()) return {false, QStringLiteral("Viewport is unavailable.")};
  auto result = m_robot.applyTransforms(pose);
  if (result.accepted) requestRender();
  return result;
}

void OccViewport::setExposed(bool exposed)
{
  m_exposed = exposed;
  if (exposed) {
    resize();
  } else {
    m_timer.stop();
    cancelGesture();
  }
}

void OccViewport::resize()
{
  if (!m_exposed || !isValid()) return;
  m_viewer.resize();
  m_scene.updateCameraDependentObjects();
  requestRender();
}

std::optional<OccPick> OccViewport::mousePress(const QPoint& pos, Qt::MouseButton button)
{
  if (!m_exposed) return std::nullopt;
  const OccInputResult input = m_input.mousePress(pos, button);
  if (input.accepted && (button == Qt::RightButton || button == Qt::MiddleButton
                         || input.cameraScaleChanged)) m_cameraAdjusted = true;
  applyInput(input);
  if (button != Qt::LeftButton || !input.selectionChanged) return std::nullopt;
  if (!m_viewer.context()->HasDetected()) return OccPick{}; // Background clears UI selection.
  const auto detected = m_viewer.context()->DetectedInteractive();
  if (const auto tool = m_robot.activeToolPart(); tool && m_scene.partHandle(*tool) == detected)
    return OccPick{0, true};
  return OccPick{m_application.objectIdFor(detected), false}; // Other geometry gives 0.
}

void OccViewport::mouseMove(const QPoint& pos)
{
  if (m_exposed) applyInput(m_input.mouseMove(pos));
}

void OccViewport::mouseRelease(Qt::MouseButton button)
{
  applyInput(m_input.mouseRelease(button));
}

void OccViewport::wheel(const QPoint& pos, int delta)
{
  if (!m_exposed) return;
  const OccInputResult input = m_input.wheel(pos, delta);
  if (input.accepted) m_cameraAdjusted = true;
  applyInput(input);
}

void OccViewport::cancelGesture()
{
  m_input.cancelGesture();
}

void OccViewport::applyInput(const OccInputResult& input)
{
  if (!isValid() || !input.accepted) return;
  if (input.cameraScaleChanged) m_scene.updateCameraDependentObjects();
  if (input.needsRedraw) requestRender();
}

void OccViewport::fitInitialCamera()
{
  if (!m_scene.hasVisibleParts()) return;
  if (!m_cameraAdjusted) m_viewer.fitAll();
  m_scene.updateCameraDependentObjects();
}

void OccViewport::requestRender()
{
  if (m_exposed && isValid() && !m_timer.isActive()) m_timer.start();
}

void OccViewport::flushRender()
{
  if (!m_exposed || !isValid()) return;
  try {
    // UpdateCurrentViewer redraws the viewer; do not immediately redraw it twice.
    m_viewer.updateCurrentViewer();
  } catch (const Standard_Failure& failure) {
    qWarning() << "OCCT redraw failed:" << failure.what();
  }
}

} // namespace RoboCrap3D
