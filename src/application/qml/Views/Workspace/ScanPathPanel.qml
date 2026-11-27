import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Components 1.0
import Styles 1.0

QxPanel {
  id: root
  required property string jobName
  required property bool generationAvailable
  signal generateRequested(int group)
  signal cancelRequested()

  title: qsTr("Scan Path")
  panelColor: Colors.background.dp01
  headerColor: Colors.background.dp08

  component PanelButton: QxButton {
    leftPadding: Metrics.sp8
    rightPadding: Metrics.sp8
    topPadding: Metrics.sp6
    bottomPadding: Metrics.sp6
  }

  GridLayout {
    Layout.fillWidth: true
    Layout.maximumWidth: 292
    Layout.alignment: Qt.AlignLeft
    columns: 2
    columnSpacing: Metrics.sp8
    rowSpacing: Metrics.sp12

    Label {
      text: qsTr("Refine Job")
      color: Colors.foreground.high
      font: Fonts.body
    }
    QxTextInput {
      Layout.fillWidth: true
      Layout.minimumWidth: 0
      Layout.preferredHeight: Metrics.h24
      readOnly: true
      text: root.jobName
      Accessible.name: qsTr("Refine Job")
      color: Colors.foreground.medium
      font: Fonts.caption
      leftPadding: Metrics.sp6
      topPadding: Metrics.sp4
      bottomPadding: Metrics.sp4
      background: Rectangle {
        radius: Metrics.r4
        color: root.panelColor
        border.color: Colors.background.dp24
      }
    }
    Label {
      text: qsTr("Group")
      color: Colors.foreground.high
      font: Fonts.body
    }
    QxComboBox {
      id: groupBox
      Layout.fillWidth: true
      Layout.minimumWidth: 0
      Layout.preferredHeight: Metrics.h24
      font: Fonts.caption
      leftPadding: Metrics.sp6
      topPadding: Metrics.sp4
      bottomPadding: Metrics.sp4
      model: ["G1", "G2"]
      currentIndex: 0
      Accessible.name: qsTr("Group")
    }
  }
  RowLayout {
    Layout.topMargin: Metrics.sp6
    spacing: Metrics.sp12
    PanelButton {
      text: qsTr("Generate")
      enabled: root.generationAvailable
      onClicked: root.generateRequested(groupBox.currentIndex + 1)
    }
    PanelButton {
      id: cancel
      text: qsTr("Cancel")
      onClicked: root.cancelRequested()
      background: Rectangle {
        radius: Metrics.r4
        color: cancel.down ? Colors.background.dp04 : Colors.background.dp16
        border.color: Colors.foreground.high
      }
      contentItem: Label {
        text: cancel.text
        font: Fonts.body
        color: Colors.foreground.high
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
      }
    }
  }
  Item { Layout.fillHeight: true }
}
