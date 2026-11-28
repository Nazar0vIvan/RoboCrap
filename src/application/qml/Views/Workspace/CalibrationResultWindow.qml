import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import Components 1.0
import Styles 1.0

// Result of a successful calibration run and the choice to make its centres primary. A top-level
// window, so the native 3D viewport cannot cover it; its fixed size keeps the buttons in place
// when an error text appears.
Window {
  id: root

  required property var rows // {label, value}
  required property string error // Result-file or settings-file write error; empty when none.
  required property var displacement // XYZ in the previous workpiece frame, then relative KUKA ABC.
  property bool scan: false

  signal reassignRequested()
  signal scanDismissed()

  function present(scanResult: bool) {
    scan = scanResult
    show()
    raise()
    requestActivate()
  }

  onClosing: { if (root.scan) root.scanDismissed() }
  title: scan ? qsTr("Scan complete") : qsTr("Calibration complete")
  modality: Qt.ApplicationModal
  flags: Qt.Dialog
  color: Colors.background.dp00
  width: minimumWidth
  height: minimumHeight
  minimumWidth: scan ? 460 : 620
  minimumHeight: scan ? 228 : 440
  maximumWidth: minimumWidth
  maximumHeight: minimumHeight

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: root.scan ? Metrics.sp12 : Metrics.sp16
    spacing: root.scan ? Metrics.sp6 : Metrics.sp8

    Label {
      visible: root.scan
      text: qsTr("Workpiece frame displacement")
      font: Fonts.title
      color: Colors.foreground.high
    }

    RowLayout {
      visible: root.scan
      Layout.fillWidth: true
      uniformCellSizes: true
      spacing: Metrics.sp24
      Repeater {
        model: root.scan ? 2 : 0
        delegate: ColumnLayout {
          id: section
          required property int index
          Layout.fillWidth: true
          spacing: Metrics.sp4
          Label {
            text: section.index === 0 ? qsTr("Translation") : qsTr("Rotation")
            font: Fonts.subtitle
            color: Colors.foreground.high
          }
          Repeater {
            model: 3
            delegate: RowLayout {
              id: axisRow
              required property int index
              readonly property int axis: section.index * 3 + index
              Layout.fillWidth: true
              Label {
                text: "Δ" + "XYZABC"[axisRow.axis]
                color: Colors.foreground.high
                font: Fonts.body
              }
              Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                readonly property real value: root.displacement[axisRow.axis] ?? 0
                text: (value >= 0 ? "+" : "") + value.toFixed(4) + (section.index === 0 ? " mm" : "°")
                color: Colors.foreground.high
                font: Fonts.body
              }
            }
          }
        }
      }
    }

    Repeater {
      model: root.scan ? [] : root.rows
      delegate: Label {
        id: rowLabel
        required property var modelData

        Layout.fillWidth: true
        text: rowLabel.modelData.label + ": " + rowLabel.modelData.value
        color: Colors.foreground.high
        font: Fonts.body
        elide: Text.ElideRight
      }
    }

    // Fills the free height whether or not an error is shown.
    Label {
      id: errorLabel
      Layout.fillWidth: true
      Layout.fillHeight: true
      text: root.error
      color: Colors.maxColor
      font: Fonts.body
      wrapMode: Text.WordWrap
      elide: Text.ElideRight
      verticalAlignment: Text.AlignTop
      HoverHandler { id: errorHover }
      ToolTip.visible: errorLabel.truncated && errorHover.hovered
      ToolTip.text: root.error
    }

    Label {
      Layout.fillWidth: true
      text: root.scan ? qsTr("Reassign the workpiece frame based on the scan results?")
                      : qsTr("Reassign the ball center and sphere center based on the calibration results?")
      color: Colors.foreground.high
      font: Fonts.body
      elide: Text.ElideRight
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: Metrics.sp8

      Item { Layout.fillWidth: true }

      QxButton {
        text: qsTr("Reassign")
        onClicked: root.reassignRequested()
      }

      QxButton {
        text: qsTr("Cancel")
        onClicked: root.close()
      }
    }
  }
}
