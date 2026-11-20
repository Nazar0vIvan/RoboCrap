#include "occviewport.h"

#include <AIS_InteractiveContext.hxx>
#include <QDebug>
#include <Standard_Failure.hxx>

#include <functional>

namespace RoboCrap3D {

OccViewport::OccViewport(Aspect_Handle handle, const DrawingDocument& document)
  : m_viewer(handle),
    m_scene(m_viewer.context(), m_viewer.view()),
    m_robot(m_scene),
    m_application(m_scene, document.models),
    m_input(m_viewer.context(), m_viewer.view())
{
  m_timer.setSingleShot(true);
  m_timer.setInterval(0);
  QObject::connect(&m_timer, &QTimer::timeout, &m_timer, std::bind(&OccViewport::flushRender, this));
  if (!m_viewer.isValid()) return;
  m_scene.displayInfrastructure();
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
  const auto result = m_robot.load(visuals, shapes, pose);
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

ViewportOperationResult OccViewport::setSelection(const QList<quint32>& objectIds, bool toolSelected)
{
  auto parts = m_application.selectedParts(objectIds);
  if (const auto tool = m_robot.activeToolPart(); tool && toolSelected) parts.push_back(*tool);
  ViewportOperationResult result{true, {}};
  try {
    m_scene.selectParts(parts);
  } catch (const Standard_Failure& failure) {
    result = {false, QStringLiteral("Selection: %1").arg(QString::fromUtf8(failure.what()))};
  }
  requestRender();
  return result;
}

ViewportOperationResult OccViewport::setDraftAxis(const std::optional<DrawingAxis>& axis)
{
  const auto result = m_application.setDraftAxis(axis);
  requestRender();
  return result;
}

OccViewport::~OccViewport()
{
  m_timer.stop();
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
  const auto result = m_robot.applyTransforms(pose);
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
