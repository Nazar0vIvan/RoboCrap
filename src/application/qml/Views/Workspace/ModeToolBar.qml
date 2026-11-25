import QtQuick
import QtQuick.Controls.Basic

import Styles 1.0

ToolBar {
  id: root

  required property int mode
  required property int submode
  property bool planeImportAvailable: false
  property bool cylinderImportAvailable: false
  property bool intersectionAvailable: false
  property bool generationAvailable: false
  property bool preparationAvailable: false
  property bool preparing: false
  property bool calibrationStartAvailable: false
  property bool calibrationResumeAvailable: false
  property bool playbackAvailable: false
  property bool playing: false
  property bool playbackActive: false
  signal pauseRequested()
  signal dryRunRequested()
  signal stopRequested()

  signal planeImportRequested()
  signal cylinderImportRequested()
  signal intersectionRequested()
  signal generationRequested()
  signal preparationRequested()
  signal calibrationStartRequested()
  signal calibrationResumeRequested()
  signal toolSetupRequested()
  signal refineFrameRequested()

  readonly property bool measuring: root.mode === WorkflowPanel.Measuring
  readonly property bool rough: root.measuring && root.submode === WorkflowPanel.Rough
  readonly property bool precise: root.measuring && root.submode === WorkflowPanel.Precise
  readonly property bool calibration: root.measuring && root.submode === WorkflowPanel.Calibration

  implicitHeight: 83
  padding: Metrics.sp8

  onModeChanged: tools.contentX = 0
  onSubmodeChanged: tools.contentX = 0

  background: Rectangle {
    color: Colors.background.dp01
    radius: Metrics.r4
    border.width: Metrics.w1
    border.color: Colors.background.dp12
  }

  component Tool: Item {
    id: tool

    property alias text: button.text
    property alias iconSource: button.icon.source
    property bool available: false
    property bool preserveIconColors: false

    signal triggered()

    implicitWidth: Math.max(100, button.implicitWidth)
    implicitHeight: 60
    width: implicitWidth
    height: implicitHeight

    ToolButton {
      id: button

      readonly property color foregroundColor: button.enabled ? "white" : Colors.foreground.disabled

      anchors.fill: parent
      enabled: tool.available
      padding: Metrics.sp8
      spacing: Metrics.sp6
      font: Fonts.caption
      display: AbstractButton.TextUnderIcon
      icon.width: Metrics.sz24
      icon.height: Metrics.sz24
      icon.color: button.enabled && tool.preserveIconColors ? "transparent" : button.foregroundColor
      palette.buttonText: button.foregroundColor
      palette.highlight: button.foregroundColor
      palette.disabled.buttonText: Colors.foreground.disabled

      background: Rectangle {
        radius: Metrics.r4
        color: button.down ? Colors.background.dp12
               : button.hovered ? Colors.background.dp04 : "transparent"
        border.width: button.visualFocus ? Metrics.w2 : Metrics.w1
        border.color: button.visualFocus ? Colors.primary.light : Colors.background.dp12
      }

      onClicked: tool.triggered()
    }
  }

  contentItem: Flickable {
    id: tools

    clip: true
    contentWidth: toolRow.implicitWidth
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentWidth > width

    ScrollBar.horizontal: ScrollBar {
      policy: ScrollBar.AsNeeded
    }

    Row {
      id: toolRow

      spacing: Metrics.sp10

      Tool {
        text: root.measuring ? qsTr("Measuring EE") : qsTr("Spindle EE")
        iconSource: "qrc:/icons/settings.svg"
        available: true
        onTriggered: root.toolSetupRequested()
      }

      Rectangle {
        width: Metrics.w1
        height: Metrics.sz36
        y: Metrics.sp12
        color: Colors.background.dp12
      }

      Row {
        visible: root.rough
        spacing: Metrics.sp16

        Tool {
          text: qsTr("Plane from JSON")
          iconSource: "qrc:/icons/plane.svg"
          available: root.planeImportAvailable
          onTriggered: root.planeImportRequested()
        }

        Tool {
          text: qsTr("Cylinder from JSON")
          iconSource: "qrc:/icons/cylinder.svg"
          available: root.cylinderImportAvailable
          onTriggered: root.cylinderImportRequested()
        }

        Tool {
          text: qsTr("Cone from JSON")
          iconSource: "qrc:/icons/cone.svg"
        }
      }

      Tool {
        visible: root.measuring && !root.calibration
        text: qsTr("Intersect Selected")
        iconSource: "qrc:/icons/intersec.svg"
        available: root.intersectionAvailable
        onTriggered: root.intersectionRequested()
      }

      Tool {
        visible: root.precise
        text: qsTr("Refine Frame")
        iconSource: "qrc:/icons/refine_frame.svg"
        available: true
        onTriggered: root.refineFrameRequested()
      }

      Tool {
        visible: !root.rough
        text: qsTr("Generate Path")
        iconSource: "qrc:/icons/generate_path.svg"
        available: root.generationAvailable
        onTriggered: root.generationRequested()
      }

      Rectangle {
        visible: !root.rough
        width: Metrics.w1
        height: Metrics.sz36
        y: Metrics.sp12
        color: Colors.background.dp12
      }

      Tool {
        visible: !root.rough
        text: root.playing ? qsTr("Pause") : qsTr("Dry Run")
        iconSource: root.playing ? "qrc:/icons/pause.svg" : "qrc:/icons/dry.svg"
        preserveIconColors: true
        available: root.playing || root.playbackAvailable
        onTriggered: root.playing ? root.pauseRequested() : root.dryRunRequested()
      }

      Tool {
        visible: !root.rough
        text: qsTr("Stop")
        iconSource: "qrc:/icons/stop.svg"
        preserveIconColors: true
        available: root.playbackActive
        onTriggered: root.stopRequested()
      }

      Rectangle {
        visible: !root.rough
        width: Metrics.w1
        height: Metrics.sz36
        y: Metrics.sp12
        color: Colors.background.dp12
      }

      Tool {
        visible: !root.measuring || root.calibration
        text: root.preparing ? qsTr("Sampling…") : qsTr("RSI Sampling")
        iconSource: "qrc:/icons/rsi_sampling.svg"
        available: root.preparationAvailable
        onTriggered: root.preparationRequested()
      }

      Tool {
        visible: root.calibration
        text: qsTr("Start Calibration")
        iconSource: "qrc:/icons/run.svg"
        preserveIconColors: true
        available: root.calibrationStartAvailable
        onTriggered: root.calibrationStartRequested()
      }

      // A separate tool, so no label change resizes the toolbar.
      Tool {
        visible: root.calibration
        text: qsTr("Resume")
        iconSource: "qrc:/icons/run.svg"
        preserveIconColors: true
        available: root.calibrationResumeAvailable
        onTriggered: root.calibrationResumeRequested()
      }

      Tool {
        visible: !root.rough && !root.calibration
        text: root.precise ? qsTr("Scan") : qsTr("Start Machining")
        iconSource: root.precise ? "qrc:/icons/scan.svg" : "qrc:/icons/run.svg"
        preserveIconColors: true
      }
    }
  }
}
