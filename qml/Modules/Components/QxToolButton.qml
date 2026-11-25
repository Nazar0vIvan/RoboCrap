import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

import Styles 1.0

ToolButton {
  id: root

  required property string imageSource

  property int imageSize: Metrics.sz16
  property int radius: Metrics.r4

  padding: Metrics.sp4
  opacity: enabled ? (pressed ? 0.9 : hovered ? 1.0 : 0.9) : 0.4

  contentItem: Image {
    sourceSize.width: root.imageSize
    sourceSize.height: root.imageSize
    fillMode: Image.PreserveAspectFit
    source: root.imageSource
    mipmap: true
    smooth: true
  }
  background: Rectangle {
    color: root.checked ? Qt.tint(Colors.background.dp01, Qt.alpha(Colors.secondary.base, Colors.overlays.pressed))
                        : Colors.background.dp04
    radius: root.radius
    border {
      width: Metrics.w1
      color: root.visualFocus ? Colors.primary.light : root.checked ? Colors.secondary.base : Colors.background.dp12
    }
  }
}
