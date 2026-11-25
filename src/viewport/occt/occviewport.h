#pragma once

#include "occinputcontroller.h"
#include "occviewer.h"
#include "viewport/occcontroller.h"
#include "adapters/occrobotadapter.h"
#include "adapters/occsceneadapter.h"

#include <QString>
#include <QList>
#include <QTimer>

#include <optional>

class StdSelect_BRepOwner;

namespace RoboCrap3D {

// A left click: an application object (0 clears the selection) or the displayed end-effector model.
struct OccPick
{
  quint32 objectId = 0;
  bool tool = false;
};

class OccViewport final
{
public:
  OccViewport(Aspect_Handle handle, const DrawingDocument& document);
  OccViewport(const OccViewport&) = delete;
  OccViewport& operator=(const OccViewport&) = delete;

  bool isValid() const;
  bool isRobotReady() const { return isValid() && m_robot.isLoaded(); }
  ViewportOperationResult setRobot(const RobotVisualModel& visuals, const CadLoadResult& shapes,
                const DrawingPose& pose);
  ViewportOperationResult applyTransforms(const DrawingPose& pose);
  // BASE-relative drawing frames; absent values hide the corresponding axes.
  ViewportOperationResult setSpindleFrames(const std::optional<M4d>& tcp, const std::optional<M4d>& collet);
  ViewportOperationResult setActiveTool(RobotTool active);
  ViewportOperationResult insertObject(const DrawingObject& object);
  ViewportOperationResult removeObject(quint32 id);
  ViewportOperationResult setObjectVisible(quint32 id, bool visible);
  ViewportOperationResult setObjectDiagnostics(quint32 id, const DrawingDiagnostics& diagnostics);
  ViewportOperationResult setSelection();
  QPoint pickedGridPoint(QPoint pos) const;
  std::pair<int, QString> pickedPlane(quint32 objectId) const;
  ViewportOperationResult setDraftAxis(const std::optional<DrawingAxis>& axis);
  void setExposed(bool exposed);
  void resize();
  std::optional<OccPick> mousePress(const QPoint& pos, Qt::MouseButton button);
  void mouseMove(const QPoint& pos);
  void mouseRelease(Qt::MouseButton button);
  void wheel(const QPoint& pos, int delta);
  void cancelGesture();

private:
  ViewportOperationResult displaySelection();
  void setFacePickShape(const Handle(AIS_Shape)& shape);
  void applyInput(const OccInputResult& input);
  void fitInitialCamera();
  void requestRender();
  void flushRender();

  const DrawingDocument& m_document;
  OccViewer m_viewer;
  OccScene m_scene;
  OccRobotAdapter m_robot;
  OccSceneAdapter m_application;
  OccInputController m_input;
  Handle(AIS_Shape) m_facePickShape;
  Handle(StdSelect_BRepOwner) m_selectedFace;
  QTimer m_timer;
  bool m_cameraAdjusted = false;
  bool m_exposed = false;
};

} // namespace RoboCrap3D
