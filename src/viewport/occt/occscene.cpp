#include "occscene.h"

#include <QDebug>
#include <algorithm>
#include <limits>

#include <AIS_InteractiveContext.hxx>
#include <AIS_SelectionModesConcurrency.hxx>
#include <AIS_Shape.hxx>

#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Drawer.hxx>
#include <PrsMgr_Presentation.hxx>

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Builder.hxx>

#include <V3d_View.hxx>

namespace RoboCrap3D {

namespace {

// Axis length per unit of camera scale, so the axes keep a constant size on screen.
constexpr double AxisLengthPerCameraScale = 0.05;

} // namespace

OccScene::OccScene(
    const Handle(AIS_InteractiveContext)& context,
    const Handle(V3d_View)& view)
    : m_context(context)
      , m_view(view)
      , m_worldAxes(currentWorldAxisLength())
      , m_spindleTcpAxes(currentWorldAxisLength())
      , m_spindleColletAxes(currentWorldAxisLength())
      , m_viewCube()
{}

bool OccScene::isValid() const
{
  return !m_context.IsNull() && !m_view.IsNull();
}

// Parts are stored only while the scene is valid (createShapePart), so no validity check is needed.
bool OccScene::hasVisibleParts() const
{
  for (const auto& [id, part] : m_parts) {
    if (!part.fitExcluded() && m_context->IsDisplayed(part.handle())) return true;
  }
  return false;
}

void OccScene::updateCameraDependentObjects()
{
  if (!isValid()) return;
  // The world axes appear with the first visible part, at the origin (identity frame).
  if (m_context->IsDisplayed(m_worldAxes.xAxis())) resizeFrameAxes(m_worldAxes);
  else if (hasVisibleParts()) setFrameAxes(m_worldAxes, gp_Trsf());
  resizeFrameAxes(m_spindleTcpAxes);
  resizeFrameAxes(m_spindleColletAxes);
}

void OccScene::setSpindleTcpFrame(const std::optional<gp_Trsf>& frame)
{
  setFrameAxes(m_spindleTcpAxes, frame);
}

void OccScene::setSpindleColletFrame(const std::optional<gp_Trsf>& frame)
{
  setFrameAxes(m_spindleColletAxes, frame);
}

void OccScene::setFrameAxes(OccWorldAxes& axes, const std::optional<gp_Trsf>& frame)
{
  if (!isValid()) return;
  const bool displayed = m_context->IsDisplayed(axes.xAxis());
  if (!frame) {
    if (!displayed) return;
    for (const auto& axis : {axes.xAxis(), axes.yAxis(), axes.zAxis()})
      m_context->Erase(axis, false);
    return;
  }
  for (const auto& axis : {axes.xAxis(), axes.yAxis(), axes.zAxis()})
    axis->SetLocalTransformation(*frame);
  if (displayed) return;
  axes.setLength(currentWorldAxisLength());
  for (const auto& axis : {axes.xAxis(), axes.yAxis(), axes.zAxis()}) {
    m_context->Display(axis, 0, -1, false);
    m_context->Deactivate(axis);
    m_context->Redisplay(axis, false);
  }
}

void OccScene::resizeFrameAxes(OccWorldAxes& axes)
{
  if (!m_context->IsDisplayed(axes.xAxis())) return;
  axes.setLength(currentWorldAxisLength());
  for (const auto& axis : {axes.xAxis(), axes.yAxis(), axes.zAxis()})
    m_context->Redisplay(axis, false);
}

void OccScene::displayViewCube()
{
  if (!isValid()) return;
  m_context->Display(m_viewCube.handle(), false);
  m_context->Activate(m_viewCube.handle(), 0, false);
}

// Only stored parts are displayed: the scene is valid and the part has a handle (createShapePart).
void OccScene::displayPart(const OccPart& part)
{
  const int selectionMode = part.selectionMode() == OccSelectionMode::None ? -1 : 0;

  // Transparent parts keep their transparency when selected or hovered, so parts behind them
  // stay visible; opaque parts use the viewport styles of OccViewer::configureHighlightStyles.
  const Handle(AIS_Shape)& handle = part.handle();
  if (handle->IsTransparent()) {
    const Handle(Prs3d_Drawer) selected = new Prs3d_Drawer(*m_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected));
    const Handle(Prs3d_Drawer) hover = new Prs3d_Drawer(*m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic));
    selected->SetTransparency(static_cast<float>(handle->Transparency()));
    hover->SetTransparency(static_cast<float>(handle->Transparency()));
    handle->SetHilightAttributes(selected);
    handle->SetDynamicHilightAttributes(hover);
  }

  m_context->Display(handle, handle->DisplayMode(), selectionMode, false);
  // FitAll skips infinite structures. Marking the AIS object infinite instead would make
  // AIS_Shape compute its shaded presentation as wireframe.
  if (part.fitExcluded())
    for (const auto& presentation : handle->Presentations()) presentation->SetInfiniteState(true);

  if (part.topmost()) m_context->SetZLayer(handle, Graphic3d_ZLayerId_Topmost);

  // Imported surfaces: faces, edges and vertices are pickable besides the whole shape.
  if (part.selectionMode() == OccSelectionMode::All)
    for (const TopAbs_ShapeEnum type : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX})
      m_context->SetSelectionModeActive(handle, AIS_Shape::SelectionMode(type), true,
                                        AIS_SelectionModesConcurrency_Multiple);
}

double OccScene::currentWorldAxisLength() const
{
  if (m_view.IsNull() || m_view->Camera().IsNull()) {
    return kDefaultAxisLength;
  }

  const double cameraScale = m_view->Camera()->Scale();

  if (cameraScale <= 0.0) {
    return kDefaultAxisLength;
  }

  return cameraScale * AxisLengthPerCameraScale;
}

std::optional<OccScene::PartId> OccScene::createShapePart(const TopoDS_Shape& shape, const OccPartProps& props)
{
  if (!isValid()) {
    qWarning() << "Cannot add OCCT shape part: context or view is null";
    return std::nullopt;
  }

  if (shape.IsNull()) {
    qWarning() << "Cannot add OCCT shape part: TopoDS_Shape is null";
    return std::nullopt;
  }

  if (!m_nextPartId) {
    qWarning() << "Cannot add OCCT shape part: part IDs are exhausted";
    return std::nullopt;
  }

  OccPart part(shape, props); // A non-null shape always gets its AIS handle.
  const PartId id = *m_nextPartId;
  m_parts.emplace(id, std::move(part));
  if (id == std::numeric_limits<PartId>::max()) m_nextPartId.reset();
  else ++*m_nextPartId;
  return id;
}

bool OccScene::setPartTransform(const PartId id, const gp_Trsf& transform)
{
  const auto found = m_parts.find(id);
  if (found == m_parts.end()) {
    qWarning() << "Invalid OCCT scene part id:" << id;
    return false;
  }

  found->second.setTransform(transform);
  if (const auto selected = m_selectedParts.find(id);
      selected != m_selectedParts.end() && !selected->second.first.IsNull())
    selected->second.first->SetLocalTransformation(transform);
  return true;
}

bool OccScene::setPartVisible(const PartId id, const bool visible)
{
  const auto found = m_parts.find(id);
  if (found == m_parts.end()) {
    qWarning() << "Invalid OCCT scene part id:" << id;
    return false;
  }

  const OccPart& part = found->second;
  if (!visible) unselectPart(id);
  if (m_context->IsDisplayed(part.handle()) == visible) return true;
  if (visible) displayPart(part);
  else m_context->Erase(part.handle(), false);
  return true;
}

bool OccScene::removePart(const PartId id)
{
  const auto found = m_parts.find(id);
  if (found == m_parts.end()) {
    qWarning() << "Invalid OCCT scene part id:" << id;
    return false;
  }

  const OccPart& part = found->second;
  unselectPart(id);
  m_context->Remove(part.handle(), false);
  m_parts.erase(found);
  return true;
}

Handle(AIS_Shape) OccScene::partHandle(const PartId id) const
{
  const auto found = m_parts.find(id);
  return found != m_parts.end() ? found->second.handle() : Handle(AIS_Shape){};
}

void OccScene::unselectPart(const PartId id)
{
  const auto selected = m_selectedParts.find(id);
  if (selected == m_selectedParts.end()) return;
  auto& [outline, ready] = selected->second;
  ready = false; // A failed removal must not leave a completed display record.
  if (!outline.IsNull()) m_context->Remove(outline, false);
  const auto handle = partHandle(id);
  if (!handle.IsNull() && m_context->IsSelected(handle))
    m_context->AddOrRemoveSelected(handle, false);
  m_selectedParts.erase(selected);
}

void OccScene::selectParts(const std::vector<PartId>& ids)
{
  if (!isValid()) return;
  for (auto selected = m_selectedParts.begin(); selected != m_selectedParts.end();) {
    const PartId id = (selected++)->first;
    const auto handle = partHandle(id);
    if (std::find(ids.cbegin(), ids.cend(), id) == ids.cend()
        || handle.IsNull() || !m_context->IsDisplayed(handle)) unselectPart(id);
  }
  for (const PartId id : ids) {
    const Handle(AIS_Shape) handle = partHandle(id);
    if (handle.IsNull() || !m_context->IsDisplayed(handle)) continue;
    if (!m_context->IsSelected(handle)) m_context->AddOrRemoveSelected(handle, false);
    auto& [outline, ready] = m_selectedParts.try_emplace(id, Handle(AIS_Shape){}, false).first->second;
    if (ready) continue;
    // Highlight color overrides face-boundary aspects. Keep the boundary in
    // its own unhighlighted presentation; standalone curves remain amber.
    // The non-null handle above was found in m_parts.
    if (outline.IsNull() && m_parts.find(id)->second.faceBoundary()
        && TopExp_Explorer(handle->Shape(), TopAbs_FACE).More()) {
      BRep_Builder builder;
      TopoDS_Compound boundaries;
      builder.MakeCompound(boundaries);
      for (TopExp_Explorer edge(handle->Shape(), TopAbs_EDGE); edge.More(); edge.Next())
        builder.Add(boundaries, edge.Current());
      outline = new AIS_Shape(boundaries); // Own it before any display/style operation can fail.
    }
    if (!outline.IsNull()) {
      outline->SetColor(kSelectionOutlineColor);
      outline->SetWidth(kSelectionOutlineWidth);
      outline->SetLocalTransformation(handle->LocalTransformation());
      outline->SetInfiniteState(true); // Decoration must not affect camera fitting.
      m_context->Display(outline, 0, -1, false);
      // Share scene depth so rear boundaries are hidden by opaque faces.
      m_context->SetZLayer(outline, Graphic3d_ZLayerId_Top);
    }
    ready = true;
  }
}

} // namespace RoboCrap3D
