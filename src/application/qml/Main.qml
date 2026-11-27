import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs as Dialogs
import robocrap_qml_module 1.0 as App
import RoboCrap.Viewport3D 1.0 as Viewport3D

import "MenuBar"
import "Views/Dashboard/DeltaPanel"
import "Views/Dashboard/FtsPanel"
import "Views/Network"
import "Views/Viewport3D"
import "Views/Workspace"

import Styles 1.0
import Components 1.0

ApplicationWindow {
  id: root

  readonly property int workflowMode: App.WorkspaceController.mode
  readonly property int measurementSubmode: App.WorkspaceController.submode
  readonly property bool calibrationActive: root.workflowMode === WorkflowPanel.Measuring
                                            && root.measurementSubmode === WorkflowPanel.Calibration

  readonly property bool preciseActive: root.workflowMode === WorkflowPanel.Measuring
                                        && root.measurementSubmode === WorkflowPanel.Precise
  onPreciseActiveChanged: if (!root.preciseActive) refineEditor.active = false
  onWorkflowModeChanged: {
    root.toolSetupOpen = false
    if (root.chamferEditorOpen) machiningPanel.cancelEditing()
  }

  function openToolSetup() {
    root.scanJob = null
    refineEditor.active = false
    if (root.chamferEditorOpen) machiningPanel.cancelEditing()
    root.toolSetupOpen = true
  }

  // Closing the panel of a selected end-effector model also deselects the model.
  function closeToolSetup() {
    root.toolSetupOpen = false
    if (App.WorkspaceController.toolSelected) App.WorkspaceController.selectObject(0, false)
  }

  // A picked end-effector model opens its panel as the Measuring EE and Spindle EE buttons do.
  Connections {
    target: App.WorkspaceController
    function onToolSelectedChanged(selected) {
      if (selected) root.openToolSetup()
      else root.toolSetupOpen = false
    }
  }

  // Editor state stays local; application selection belongs to WorkspaceController.
  property bool chamferEditorOpen: false
  property bool toolSetupOpen: false
  readonly property list<real> selectedObjectIds: App.WorkspaceController.selectedObjectIds
  readonly property App.SceneObject selectedSceneObject: App.WorkspaceController.selectedObject
  readonly property App.SceneObject selectedRefineJob: root.preciseActive && !App.Scene.refineSelectionError
                                                      && root.selectedSceneObject?.kind === App.SceneObject.RefineJob
                                                      ? root.selectedSceneObject : null
  property App.SceneObject scanJob: null
  onSelectedRefineJobChanged: root.scanJob = null

  TapHandler {
    parent: root.contentItem.Window.contentItem
    enabled: !!(refineEditor.item as RefineFramePanel)?.pickingPoints
    onPressedChanged: if (pressed) (refineEditor.item as RefineFramePanel).outsidePointField(point.scenePosition)
  }

  Shortcut {
    sequence: "Del"
    context: Qt.WindowShortcut
    autoRepeat: false
    enabled: !refineEditor.activeFocus && !(refineEditor.item as RefineFramePanel)?.pickingPoints
             && root.selectedObjectIds.length > 0
             && !(root.activeFocusItem instanceof TextInput)
             && !(root.activeFocusItem instanceof TextEdit)
    onActivated: App.WorkspaceController.deleteSelected()
  }

  onSelectedSceneObjectChanged: {
    root.toolSetupOpen = false
    if (root.chamferEditorOpen) machiningPanel.cancelEditing()
  }

  Connections {
    target: App.Machining
    function onGenerated(objectId) {
      root.chamferEditorOpen = false
      App.WorkspaceController.selectObject(objectId, false)
    }
  }
  Component.onCompleted: {
    App.WorkspaceController.reset()
    App.Scene.loadRefineJobs()
    // The settings file is read before this window exists.
    if (App.Calibration.settingsError.length > 0) settingsErrorDialog.open()
  }

  // Native dialog: a separate window, so it neither hides behind the viewport nor moves the layout.
  Dialogs.MessageDialog {
    id: settingsErrorDialog
    title: qsTr("Probe settings")
    text: App.Calibration.settingsError
    buttons: Dialogs.MessageDialog.Ok
  }

  Connections {
    target: App.Calibration
    function onScanGenerated(objectId) {
      root.scanJob = null
      App.WorkspaceController.selectObject(objectId, false)
    }
    function onSettingsErrorChanged() {
      if (App.Calibration.settingsError.length > 0) settingsErrorDialog.open()
    }
    // Emitted only after a successful fit, inside the run end, before calibrationRunFinished.
    function onLastCalibrationChanged() {
      calibrationResultWindow.show()
      calibrationResultWindow.raise()
      calibrationResultWindow.requestActivate()
    }
  }

  App.PlaneImporter {
    id: surfaceImporter

    onLoaded: object => App.WorkspaceController.selectObject(object.objectId, false)
    onFailed: message => {
      importError.text = message
      importError.open()
    }
  }

  Dialogs.FileDialog {
    id: planeFileDialog
    title: qsTr("Import rough plane")
    fileMode: Dialogs.FileDialog.OpenFile
    nameFilters: [qsTr("JSON point files (*.json)")]
    onAccepted: surfaceImporter.load(planeFileDialog.selectedFile, root.sceneModel)
  }

  Dialogs.FileDialog {
    id: cylinderFileDialog
    title: qsTr("Import rough cylinder")
    fileMode: Dialogs.FileDialog.OpenFile
    nameFilters: [qsTr("JSON point files (*.json)")]
    onAccepted: surfaceImporter.loadCylinder(cylinderFileDialog.selectedFile, root.sceneModel)
  }

  Dialogs.MessageDialog {
    id: importError
    title: qsTr("Geometry import failed")
    buttons: Dialogs.MessageDialog.Ok
  }

  Dialogs.MessageDialog {
    id: calibrationFailedDialog
    title: qsTr("Calibration failed")
    text: App.MachiningPreparation.calibrationRunError
    buttons: Dialogs.MessageDialog.Ok
  }

  // The error binding also shows write errors that the run end sets after opening the window.
  CalibrationResultWindow {
    id: calibrationResultWindow
    transientParent: root
    rows: App.Calibration.lastCalibrationRows
    error: App.MachiningPreparation.calibrationRunError
    onReassignRequested: App.Calibration.reassignPrimaryCentres()
  }

  Connections {
    target: App.MachiningPreparation
    // A successful fit has already opened calibrationResultWindow.
    function onCalibrationRunFinished() {
      if (!calibrationResultWindow.visible) calibrationFailedDialog.open()
    }
  }

  readonly property bool geometryImportAvailable: !planeFileDialog.visible
                                                 && !cylinderFileDialog.visible
  readonly property App.SceneModel sceneModel: App.Scene
  readonly property bool intersectionAvailable: App.WorkspaceController.intersectionAvailable

  Connections {
    target: root.sceneModel
    function onIntersectionFailed(message) {
      sceneError.showMessage(qsTr("Surface intersection failed"), message)
    }
    function onRefineJobError(message) {
      sceneError.showMessage(qsTr("Refine Jobs"), message)
    }
  }

  Dialogs.MessageDialog {
    id: sceneError
    function showMessage(caption: string, message: string) {
      title = caption
      text = message
      open()
    }
    buttons: Dialogs.MessageDialog.Ok
  }

  width: 1366
  height: 768
  minimumWidth: 960
  minimumHeight: 600
  visible: true
  title: qsTr("RoboCrap")
  color: Colors.background.dp00

  menuBar: MainMenuBar {
    id: mainMenuBar
    onQuitRequested: Qt.quit()
    onSettingsRequested: {
      settingsWindow.show()
      settingsWindow.raise()
      settingsWindow.requestActivate()
    }
    onPlcPanelRequested: {
      plcPanelWindow.show()
      plcPanelWindow.raise()
      plcPanelWindow.requestActivate()
    }
    onFtsPanelRequested: {
      ftsPanelWindow.show()
      ftsPanelWindow.raise()
      ftsPanelWindow.requestActivate()
    }
  }

  Window {
    id: plcPanelWindow

    title: qsTr("PLC Panel")
    transientParent: root
    flags: Qt.Tool
    color: Colors.background.dp00
    width: minimumWidth
    height: minimumHeight
    minimumWidth: plcPanel.implicitWidth + 2 * Metrics.sp16
    minimumHeight: plcPanel.implicitHeight + 2 * Metrics.sp16
    maximumWidth: minimumWidth
    maximumHeight: minimumHeight

    DeltaPanel {
      id: plcPanel

      anchors.fill: parent
      anchors.margins: Metrics.sp16
      title: qsTr("PLC AS332T-A")
    }
  }

  Window {
    id: ftsPanelWindow

    title: qsTr("FTS Panel")
    transientParent: root
    flags: Qt.Tool
    color: Colors.background.dp00
    width: minimumWidth
    height: minimumHeight
    minimumWidth: ftsPanel.implicitWidth + 2 * Metrics.sp16
    minimumHeight: ftsPanel.implicitHeight + 2 * Metrics.sp16
    maximumWidth: minimumWidth
    maximumHeight: minimumHeight

    FtsPanel {
      id: ftsPanel

      anchors.fill: parent
      anchors.margins: Metrics.sp16
      title: qsTr("FTS Delta-IP68-SI-660-60")
    }
  }

  Window {
    id: settingsWindow

    title: qsTr("Settings")
    transientParent: root
    flags: Qt.Tool
    color: Colors.background.dp00
    width: Math.ceil(networkSettings.contentWidth)
    height: Math.ceil(networkSettings.contentHeight)
    minimumWidth: 640
    minimumHeight: 360

    Network {
      id: networkSettings

      anchors.fill: parent
    }
  }


  footer: ToolBar {
    id: statusBar

    readonly property App.DeviceRunner rsi: App.Hub.device("rsi")
    readonly property App.DeviceRunner plc: App.Hub.device("plc")
    // X1.0 $I_O_ACTCONF: the controller's Automatic External interface is active.
    readonly property bool autExt: !!statusBar.plc && statusBar.plc.isConnected
                                   && !!statusBar.plc.data.x1 && !!statusBar.plc.data.x1[0]
    // Until the robot is shown; OccController.loading covers only CAD parsing.
    readonly property bool sceneLoading: !Viewport3D.OccController.ready
                                         && Viewport3D.OccController.errorString.length === 0
    readonly property bool busy: App.Machining.calculating || App.Calibration.calculating
                                 || App.MachiningPreparation.preparing || statusBar.sceneLoading
    readonly property bool refineErrorShown: root.preciseActive && !App.MachiningPreparation.calibrationRunning
                                             && App.Scene.refineSelectionError.length > 0
    readonly property bool playbackShown: modeTools.playbackActive
                                          && (root.preciseActive || (root.calibrationActive ? !App.Calibration.error : root.chamferEditorOpen))
    readonly property string message: {
      if (App.MachiningPreparation.calibrationRunning) {
        const detected = App.MachiningPreparation.calibrationDetected
        const count = App.MachiningPreparation.calibrationTouchCount
        if (App.MachiningPreparation.scanRunning && App.MachiningPreparation.error) return App.MachiningPreparation.error
        const progress = (App.MachiningPreparation.scanRunning ? qsTr("Scan: %1 % · %2/%3 touches")
                                                               : qsTr("Calibration run: %1 % · %2/%3 touches"))
                         .arg(count > 0 ? Math.floor(100 * detected / count) : 0).arg(detected).arg(count)
        // Joined with " · " in the one status line.
        const interruption = App.MachiningPreparation.calibrationInterruption
        return interruption ? progress + "\n" + interruption : progress
      }
      if (statusBar.refineErrorShown) return App.Scene.refineSelectionError
      if (App.Machining.calculating || App.Calibration.calculating) return qsTr("Calculating trajectory…")
      if (App.MachiningPreparation.preparing) return qsTr("RSI Sampling…")
      if (statusBar.sceneLoading) return qsTr("Loading…")
      if (statusBar.playbackShown)
        return qsTr("%1 · %2 · %3 / %4 s").arg(App.Playback.playing ? qsTr("Playing") : qsTr("Paused"))
               .arg(App.Playback.phase).arg(App.Playback.elapsed.toFixed(2)).arg(App.Playback.duration.toFixed(2))
      if (statusBar.calibrationStatus) return statusBar.calibrationStatus
      if (root.chamferEditorOpen) return machiningPanel.message
      if (root.preciseActive) return App.MachiningPreparation.error || App.MachiningPreparation.summary
      if (!root.calibrationActive && root.workflowMode !== WorkflowPanel.Machining) return ""
      const preparation = App.MachiningPreparation
      const status = preparation.error || (root.calibrationActive
                     ? (preparation.canPrepareCalibration && preparation.lastCalibrationRecord
                        ? qsTr("Last sampling record: %1").arg(preparation.lastCalibrationRecord) : "")
                     : preparation.summary)
                     || (root.calibrationActive ? preparation.calibrationUnavailableReason
                                                : preparation.unavailableReason)
                     || qsTr("Ready for RSI Sampling.")
      return statusBar.rsi && statusBar.rsi.isConnected
             ? status + "\n" + qsTr("Controller: %1").arg(statusBar.rsi.data.rsiState ?? "idle") : status
    }
    // Calibration error or the reason Generate is unavailable; preparation status otherwise.
    readonly property string calibrationStatus: root.calibrationActive
                                                ? App.Calibration.error || App.Calibration.unavailableReason
                                                : root.preciseActive ? App.Calibration.error : ""
    readonly property bool errorShown: !App.MachiningPreparation.calibrationRunning && !statusBar.playbackShown
                                       && (statusBar.calibrationStatus ? App.Calibration.error.length > 0
                                           : root.chamferEditorOpen ? App.Machining.error.length > 0
                                           : App.MachiningPreparation.error.length > 0)

    padding: Metrics.sp8
    implicitHeight: Metrics.h40

    background: Rectangle {
      color: Colors.background.dp01
      border.color: Colors.background.dp12
      border.width: Metrics.w1
    }

    contentItem: QxHField {
      spacing: Metrics.sp16
      fixedLabelWidth: true
      labelWidth: Metrics.w200
      color: Colors.foreground.medium
      labelText: root.workflowMode === WorkflowPanel.Machining
                 ? qsTr("Mode: Machining") : qsTr("Mode: Measuring / %1").arg(
                     root.measurementSubmode === WorkflowPanel.Rough ? qsTr("Rough")
                     : root.measurementSubmode === WorkflowPanel.Precise ? qsTr("Precise") : qsTr("Calibration"))

      // Constant content, so its width never changes with the controller state.
      Row {
        Layout.alignment: Qt.AlignVCenter
        spacing: Metrics.sp6

        QxLed {
          anchors.verticalCenter: parent.verticalCenter
          diameter: Metrics.sz20
          isOn: true
          ledColor: statusBar.autExt ? "green" : "gray"
        }

        Text {
          anchors.verticalCenter: parent.verticalCenter
          text: qsTr("AUT EXT")
          color: Colors.foreground.medium
          font: Fonts.body
        }
      }

      Label {
        id: statusMessage

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        // The message never widens the bar or squeezes the AUT EXT indicator.
        Layout.preferredWidth: 0
        // Constant room for the busy indicator, so an elided message never runs under it.
        leftPadding: busyIndicator.width + Metrics.sp6
        text: statusBar.message.replace(/[\r\n]+/g, " · ")
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        color: statusBar.refineErrorShown || (statusBar.message.length > 0 && !statusBar.busy && statusBar.errorShown)
               ? Colors.maxColor : Colors.foreground.medium
        font: Fonts.body
        Accessible.name: statusBar.message
        HoverHandler { id: statusHover }
        ToolTip.visible: root.preciseActive && statusMessage.truncated && statusHover.hovered
        ToolTip.text: statusBar.message

        // Inline before the right-aligned text; not part of the layout, so nothing moves.
        QxBusyIndicator {
          id: busyIndicator

          x: statusMessage.width - statusMessage.contentWidth - busyIndicator.width - Metrics.sp6
          anchors.verticalCenter: statusMessage.verticalCenter
          diameter: Metrics.sz20
          running: statusBar.busy
        }
      }
    }
  }

  SplitView {
    id: workspace

    anchors.fill: parent
    anchors.margins: Metrics.sp4
    orientation: Qt.Horizontal

    handle: Rectangle {
      implicitWidth: Metrics.sp4
      color: SplitHandle.pressed ? Colors.primary.base
             : SplitHandle.hovered ? Colors.background.dp12
             : Colors.background.dp00
    }

    ColumnLayout {
      SplitView.preferredWidth: 265
      SplitView.minimumWidth: Math.max(200, workflowPanel.implicitWidth)

      spacing: Metrics.sp4

      WorkflowPanel {
        id: workflowPanel

        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(194, implicitHeight)

        mode: root.workflowMode
        submode: root.measurementSubmode
        onModeRequested: mode => App.WorkspaceController.mode = mode
        onSubmodeRequested: submode => App.WorkspaceController.submode = submode
      }

      ScenePanel {
        Layout.fillWidth: true
        Layout.fillHeight: true

        sceneModel: root.sceneModel
        selectedObjectIds: root.selectedObjectIds
        calibration: root.calibrationActive
        preciseScanning: root.preciseActive
        onSelectionRequested: (objectId, additive) => App.WorkspaceController.selectObject(objectId, additive)
        onVisibilityRequested: (object, visible) => root.sceneModel.setObjectVisible(object, visible)
        onRenameRequested: (object, name) => root.sceneModel.renameObject(object, name)
      }
    }

    ColumnLayout {
      SplitView.fillWidth: true
      SplitView.minimumWidth: 400

      spacing: Metrics.sp4

      ModeToolBar {
        id: modeTools
        Layout.fillWidth: true
        Layout.minimumHeight: implicitHeight
        Layout.maximumHeight: implicitHeight

        mode: root.workflowMode
        submode: root.measurementSubmode
        planeImportAvailable: root.geometryImportAvailable
        cylinderImportAvailable: root.geometryImportAvailable
        playbackAvailable: !App.MachiningPreparation.scanRunning
                           && (root.preciseActive || root.calibrationActive ? App.Calibration.canPlay : App.Machining.canPlay)
        playing: modeTools.playbackActive && App.Playback.playing
        playbackActive: root.preciseActive || root.calibrationActive ? App.Calibration.ownsPlayback : App.Machining.ownsPlayback
        preparationAvailable: !root.chamferEditorOpen
                              && (root.preciseActive ? App.MachiningPreparation.canPrepareScan
                                  : root.calibrationActive ? App.MachiningPreparation.canPrepareCalibration && !App.Playback.active
                                  : App.MachiningPreparation.canPrepare && !!statusBar.rsi && statusBar.rsi.isConnected)
        preparing: App.MachiningPreparation.preparing
        // A live calibration run requires the RSI socket.
        calibrationStartAvailable: App.MachiningPreparation.canStartCalibration
                                   && !!statusBar.rsi && statusBar.rsi.isConnected
        onCalibrationStartRequested: App.MachiningPreparation.startCalibration()
        scanAvailable: App.MachiningPreparation.canStartScan
        onScanRequested: App.MachiningPreparation.startScan()
        calibrationResumeAvailable: (root.preciseActive === App.MachiningPreparation.scanRunning)
                                    && App.MachiningPreparation.canResumeCalibration
                                    && !!statusBar.rsi && statusBar.rsi.isConnected
        onCalibrationResumeRequested: App.MachiningPreparation.resumeCalibration()
        onPreparationRequested: {
          if (root.preciseActive) App.Calibration.prepareScan()
          else if (root.calibrationActive) App.MachiningPreparation.prepareCalibration()
          else if (root.workflowMode === WorkflowPanel.Machining) App.MachiningPreparation.prepare()
        }
        onPauseRequested: App.Playback.pause()
        onDryRunRequested: {
          if (root.preciseActive && root.selectedSceneObject) App.Calibration.playProbePath(root.selectedSceneObject.objectId)
          else if (root.calibrationActive) App.Calibration.dryRun()
          else if (root.workflowMode === WorkflowPanel.Machining) App.Machining.dryRun()
        }
        onStopRequested: App.Playback.stop()
        generationAvailable: root.calibrationActive ? App.Calibration.canGenerate
                             : root.preciseActive ? !!root.selectedRefineJob && !App.Calibration.calculating && !App.Playback.active
                             : !App.Playback.active && root.selectedSceneObject !== null
                               && (root.selectedSceneObject.kind === App.SceneObject.Edge
                                   || root.selectedSceneObject.kind === App.SceneObject.MachiningPath)
        onToolSetupRequested: root.openToolSetup()
        onRefineFrameRequested: {
          root.scanJob = null
          refineEditor.active = true
        }
        onGenerationRequested: {
          if (root.calibrationActive) {
            App.Calibration.generate()
            return
          }
          if (root.preciseActive) {
            root.toolSetupOpen = false
            refineEditor.active = false
            root.scanJob = root.selectedRefineJob
            return
          }
          root.toolSetupOpen = false
          if (!root.chamferEditorOpen) machiningPanel.beginEditing()
          root.chamferEditorOpen = true
          machiningPanel.focusEditor()
        }
        intersectionAvailable: root.intersectionAvailable
        onIntersectionRequested: App.WorkspaceController.intersectSelected()
        onPlaneImportRequested: planeFileDialog.open()
        onCylinderImportRequested: cylinderFileDialog.open()
      }

      RobotViewport {
        id: viewport

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 0
        Layout.minimumHeight: 0
      }
    }

    Item {
      // 350 px keeps a fitted sphere center such as [1054.1403; -331.9692; 454.2727] on one line.
      SplitView.preferredWidth: 350
      SplitView.minimumWidth: 280
      SplitView.maximumWidth: 350
      Loader {
        id: refineEditor
        anchors.fill: parent
        active: false
        visible: active
        focus: active
        sourceComponent: RefineFramePanel {
          onCancelRequested: refineEditor.active = false
        }
      }
      PropertiesPanel {
        anchors.fill: parent
        visible: !root.chamferEditorOpen && !root.toolSetupOpen && !refineEditor.active && !scanEditor.active
        selectionCount: root.selectedObjectIds.length
        selectedObject: root.selectedSceneObject
        showPoints: root.selectedSceneObject ? root.selectedSceneObject.showPoints : false
        showNormals: root.selectedSceneObject ? root.selectedSceneObject.showNormals : false
        calibrationRows: App.Calibration.lastCalibrationRows
        onPointsToggled: checked => {
          if (root.selectedSceneObject)
            root.sceneModel.setObjectDiagnosticOverlays(root.selectedSceneObject, checked, root.selectedSceneObject.showNormals)
        }
        onNormalsToggled: checked => {
          if (root.selectedSceneObject)
            root.sceneModel.setObjectDiagnosticOverlays(root.selectedSceneObject, root.selectedSceneObject.showPoints, checked)
        }
        onRenameRequested: (object, name) => root.sceneModel.renameObject(object, name)
        onVisibilityRequested: (object, visible) => root.sceneModel.setObjectVisible(object, visible)
        onSphereDisplayRequested: (object, ballCenter, compensated) =>
            root.sceneModel.setSphereDisplay(object, ballCenter, compensated)
      }
      Loader {
        id: scanEditor
        anchors.fill: parent
        active: !!root.scanJob
        sourceComponent: ScanPathPanel {
          jobName: root.scanJob?.name ?? ""
          enabled: !App.Calibration.calculating
          generationAvailable: modeTools.generationAvailable
          onGenerateRequested: group => App.Calibration.generateScan(root.scanJob.objectId, group)
          onCancelRequested: root.scanJob = null
        }
      }
      EndEffectorPanel {
        anchors.fill: parent
        visible: root.toolSetupOpen && !refineEditor.active
        onCloseRequested: root.closeToolSetup()
        measuring: root.workflowMode === WorkflowPanel.Measuring
        measuringName: App.EndEffectors.measuringName
        ballDiameter: App.EndEffectors.rubyBallDiameter
        stylusLength: App.EndEffectors.stylusLength
        colletPose: App.EndEffectors.spindleColletPose
        tcp: measuring ? App.EndEffectors.measuringTcp : App.EndEffectors.spindleTcpInCollet
        tcpVisible: !measuring && Viewport3D.OccController.showSpindleTcp
        colletVisible: !measuring && Viewport3D.OccController.showSpindleCollet
        onMeasuringTcpRequested: values => App.EndEffectors.applyMeasuringTcp(values)
        onTcpVisibilityRequested: visible => Viewport3D.OccController.showSpindleTcp = visible
        onColletVisibilityRequested: visible => Viewport3D.OccController.showSpindleCollet = visible
        onPosesRequested: (colletValues, tcpValues) => {
          if (App.EndEffectors.applySpindlePoses(colletValues, tcpValues))
            Viewport3D.OccController.showSpindleTcp = true
        }
      }
      MachiningPanel {
        id: machiningPanel
        anchors.fill: parent
        visible: root.chamferEditorOpen
        coordinator: App.Machining
        inputName: root.selectedSceneObject ? root.selectedSceneObject.name : ""
        savedPath: root.selectedSceneObject && root.selectedSceneObject.kind === App.SceneObject.MachiningPath
                   ? root.selectedSceneObject.geometry as App.MachiningPath : null
        onGenerateRequested: App.Machining.generate()
        onCancelRequested: root.chamferEditorOpen = false
      }
    }
  }
}
