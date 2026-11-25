import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Styles 1.0

RowLayout {
  id: iconRow
  required property string iconName
  required property string description
  Layout.fillWidth: true
  spacing: Metrics.sp8

  Image {
    Layout.preferredWidth: Metrics.sz20
    Layout.preferredHeight: Metrics.sz20
    Layout.alignment: Qt.AlignTop
    source: "qrc:/icons/" + iconRow.iconName + ".svg"
    fillMode: Image.PreserveAspectFit
    ToolTip.text: iconRow.description
    ToolTip.visible: hover.hovered
    HoverHandler { id: hover }
  }
}
