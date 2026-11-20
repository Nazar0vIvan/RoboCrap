#include "occviewwindow.h"

#include "occviewport.h"

#include <QExposeEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <Standard_Failure.hxx>

#include <cmath>
#include <utility>

namespace RoboCrap3D {

OccViewWindow::OccViewWindow(const DrawingDocument& document, const DrawingPose& pose)
  : m_document(document), m_pose(pose)
{
  setSurfaceType(QSurface::OpenGLSurface);
  QObject::connect(this, &QWindow::screenChanged, this, &OccViewWindow::resizeViewport);
}

// Connected to QWindow::screenChanged, outside the protection of event().
void OccViewWindow::resizeViewport()
{
  try {
    if (m_viewport) m_viewport->resize();
  } catch (const Standard_Failure& failure) {
    failSurface(QStringLiteral("Viewport resize failed: %1").arg(QString::fromUtf8(failure.what())));
  }
}

void OccViewWindow::failSurface(const QString& error)
{
  releaseSurface();
  m_presentationFailed = true;
  emit errorOccurred(error);
}

OccViewWindow::~OccViewWindow() = default;

void OccViewWindow::setScene(const RobotVisualModel& visuals,
                              std::shared_ptr<const CadLoadResult> shapes)
{
  if (m_shapes) return;
  m_visuals = visuals;
  m_shapes = std::move(shapes);
  if (m_viewport)
    attachRobot();
  else if (isExposed())
    initializeViewport();
}

ViewportOperationResult OccViewWindow::applyTransforms(const DrawingPose& pose)
{
  if (!isReady()) return {false, QStringLiteral("Robot preview is unavailable.")};
  auto result = m_viewport->applyTransforms(pose);
  if (result.accepted) return result;
  // m_pose is still the controller's last accepted pose: it commits only after acceptance.
  QString error = QStringLiteral("Robot pose update: ") + result.error;
  const auto restored = m_viewport->applyTransforms(m_pose);
  if (!restored.accepted) {
    error += QStringLiteral("; Robot pose restoration: ") + restored.error;
    m_presentationFailed = true;
    emit readyChanged();
  }
  return {false, error};
}

void OccViewWindow::setActiveTool(RobotTool active)
{
  m_activeTool = active;
  attachActiveTool();
}

void OccViewWindow::attachActiveTool()
{
  if (!m_viewport) return;
  if (const auto shown = m_viewport->setActiveTool(m_activeTool); !shown.accepted) emit errorOccurred(shown.error);
}

bool OccViewWindow::isReady() const
{
  return !m_presentationFailed && m_viewport && m_viewport->isRobotReady();
}

bool OccViewWindow::isViewportReady() const
{
  return m_viewport && m_viewport->isValid();
}

void OccViewWindow::releaseSurface()
{
  const bool wasReady = isReady();
  const bool wasViewportReady = isViewportReady();
  m_viewport.reset();
  m_presentationFailed = false;
  if (wasReady) emit readyChanged();
  if (wasViewportReady) emit viewportReadyChanged();
}

bool OccViewWindow::event(QEvent* event)
{
  try {
    if (event->type() == QEvent::PlatformSurface) {
      const auto* surface = static_cast<QPlatformSurfaceEvent*>(event);
      if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        releaseSurface();
      }
    } else if (event->type() == QEvent::DevicePixelRatioChange) {
      resizeViewport();
    } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::UngrabMouse
               || event->type() == QEvent::Hide) {
      if (m_viewport) {
        m_viewport->cancelGesture();
        if (event->type() == QEvent::Hide) m_viewport->setExposed(false);
      }
    }
    return QWindow::event(event);
  } catch (const Standard_Failure& failure) {
    failSurface(QStringLiteral("Viewport operation failed: %1").arg(QString::fromUtf8(failure.what())));
    return true;
  }
}

// A new viewport is already exposed by initializeViewport, which returns while the window is hidden.
void OccViewWindow::exposeEvent(QExposeEvent*)
{
  if (!m_viewport) initializeViewport();
  else m_viewport->setExposed(isExposed());
}

void OccViewWindow::resizeEvent(QResizeEvent*)
{
  resizeViewport();
}

void OccViewWindow::mousePressEvent(QMouseEvent* event)
{
  if (!isViewportReady()) return;
  const auto picked = m_viewport->mousePress(nativePosition(event->position()), event->button());
  if (picked && picked->tool) emit toolSelectionRequested();
  else if (picked) emit applicationSelectionRequested(picked->objectId,
                                                       (event->modifiers() & Qt::ControlModifier) != 0);
  event->accept();
}

void OccViewWindow::mouseMoveEvent(QMouseEvent* event)
{
  if (!isViewportReady()) return;
  if (event->buttons() == Qt::NoButton) m_viewport->cancelGesture();
  m_viewport->mouseMove(nativePosition(event->position()));
  event->accept();
}

void OccViewWindow::mouseReleaseEvent(QMouseEvent* event)
{
  if (!isViewportReady()) return;
  m_viewport->mouseRelease(event->button());
  if (parent()) parent()->requestActivate();
  event->accept();
}

void OccViewWindow::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
    if (!event->isAutoRepeat()) emit deleteSelectionRequested();
    event->accept();
    return;
  }
  QWindow::keyPressEvent(event);
}

void OccViewWindow::wheelEvent(QWheelEvent* event)
{
  if (!isViewportReady()) return;
  m_viewport->wheel(nativePosition(event->position()), event->angleDelta().y());
  event->accept();
}

void OccViewWindow::initializeViewport()
{
  if (m_viewport || m_presentationFailed || !isExposed()) return;
  try {
    auto viewport = std::make_unique<OccViewport>(reinterpret_cast<Aspect_Handle>(winId()), m_document);
    if (!viewport->isValid()) {
      m_presentationFailed = true;
      emit errorOccurred(QStringLiteral("Cannot create the OCCT viewport."));
      return;
    }
    m_viewport = std::move(viewport);
    const auto axis = m_viewport->setDraftAxis(m_document.axis);
    if (!axis.accepted) emit errorOccurred(axis.error);
    if (const auto frames = m_viewport->setSpindleFrames(m_pose.tcp, m_pose.collet); !frames.accepted)
      emit errorOccurred(frames.error);
    if (const auto selection = m_viewport->setSelection(m_document.selected, m_document.toolSelected);
        !selection.accepted)
      emit errorOccurred(selection.error);
    m_viewport->setExposed(true);
  } catch (const Standard_Failure& failure) {
    failSurface(QStringLiteral("Cannot initialize the viewport: %1").arg(QString::fromUtf8(failure.what())));
    return;
  }
  // Robot failures must not enter the viewport teardown path above.
  attachRobot();
  emit viewportReadyChanged();
}

void OccViewWindow::attachRobot()
{
  if (!isViewportReady() || !m_visuals || !m_shapes || isReady() || m_presentationFailed) return;
  try {
    const auto result = m_viewport->setRobot(*m_visuals, *m_shapes, m_pose);
    m_presentationFailed = !result.accepted;
    if (m_presentationFailed) {
      emit errorOccurred(result.error);
    } else {
      attachActiveTool();
      // The end-effector part exists only now; a recreated surface restores its selection.
      if (const auto selection = m_viewport->setSelection(m_document.selected, m_document.toolSelected);
          !selection.accepted)
        emit errorOccurred(selection.error);
    }
  } catch (const Standard_Failure& failure) {
    m_presentationFailed = true;
    emit errorOccurred(QStringLiteral("Robot installation: %1").arg(QString::fromUtf8(failure.what())));
  }
  emit readyChanged();
}

QPoint OccViewWindow::nativePosition(const QPointF& position) const
{
  return {static_cast<int>(std::lround(position.x() * devicePixelRatio())),
          static_cast<int>(std::lround(position.y() * devicePixelRatio()))};
}

} // namespace RoboCrap3D
