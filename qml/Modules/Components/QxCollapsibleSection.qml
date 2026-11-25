import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Styles 1.0

ColumnLayout {
  id: root

  required property string title
  default property alias sectionContent: body.data
  spacing: Metrics.sp10

  ToolButton {
    id: header
    Layout.fillWidth: true
    implicitHeight: Metrics.h24 + Metrics.w2
    checkable: true
    checked: true
    text: root.title
    horizontalPadding: Metrics.sp8
    verticalPadding: Metrics.sp0

    contentItem: RowLayout {
      Text {
        Layout.fillWidth: true
        text: root.title
        color: Colors.foreground.medium
        font: Fonts.caption
      }
      Image {
        Layout.preferredWidth: Metrics.sp8
        Layout.preferredHeight: Metrics.sp8
        source: "qrc:/icons/arrow_dropdown.svg"
        fillMode: Image.PreserveAspectFit
        rotation: header.checked ? 180 : 0
      }
    }
    background: Rectangle {
      color: header.hovered ? Colors.background.dp12 : Colors.background.dp08
      border.color: header.visualFocus ? Colors.primary.base : color
    }
  }

  ColumnLayout {
    id: body
    Layout.fillWidth: true
    Layout.leftMargin: Metrics.sp8
    Layout.rightMargin: Metrics.sp12
    visible: header.checked
    spacing: Metrics.sp8
  }
}
