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

  signal reassignRequested()

  title: qsTr("Calibration complete")
  modality: Qt.ApplicationModal
  flags: Qt.Dialog
  color: Colors.background.dp00
  width: 620
  height: 440
  minimumWidth: root.width
  minimumHeight: root.height
  maximumWidth: root.width
  maximumHeight: root.height

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: Metrics.sp16
    spacing: Metrics.sp8

    Repeater {
      model: root.rows
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
      Layout.fillWidth: true
      Layout.fillHeight: true
      text: root.error
      color: Colors.maxColor
      font: Fonts.body
      wrapMode: Text.WordWrap
      elide: Text.ElideRight
      verticalAlignment: Text.AlignTop
    }

    Label {
      Layout.fillWidth: true
      text: qsTr("Reassign the ball center and sphere center based on the calibration results?")
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
        onClicked: {
          root.reassignRequested()
          root.close()
        }
      }

      QxButton {
        text: qsTr("Cancel")
        onClicked: root.close()
      }
    }
  }
}
