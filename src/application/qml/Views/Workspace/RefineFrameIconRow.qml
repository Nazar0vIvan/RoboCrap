import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Styles 1.0

RowLayout {
  id: iconRow
  required property string iconName
  property int iconAlignment: Qt.AlignHCenter
  Layout.fillWidth: true
  spacing: Metrics.sp8

  Image {
    Layout.preferredWidth: Metrics.sz20
    Layout.preferredHeight: Metrics.sz20
    Layout.alignment: iconRow.iconAlignment
    source: "qrc:/icons/" + iconRow.iconName + ".svg"
    fillMode: Image.PreserveAspectFit
    HoverHandler { id: hover }
  }
}
