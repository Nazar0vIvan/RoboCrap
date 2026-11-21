import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

import Styles 1.0

TextField {
  id: control

  property alias placeholder: defaultTxt.text
  property int radius: Metrics.r4

  leftPadding: Metrics.sp12
  rightPadding: Metrics.sp12
  topPadding: Metrics.sp8
  bottomPadding: Metrics.sp8
  color: Colors.foreground.high
  font: Fonts.body

  selectionColor: Colors.primary.highlight
  selectedTextColor: Colors.foreground.high
  selectByMouse: true

  readOnly: false
  verticalAlignment: TextInput.AlignVCenter

  background: Rectangle {
    color: "transparent"
    radius: control.radius
    border {
      width: control.readOnly ? 0 : Metrics.w1
      color: control.activeFocus ? Colors.primary.base : Colors.background.dp12
    }
  }

  onFocusChanged: if (focus) selectAll()

  Text {
    id: defaultTxt

    // Inside the field's padding, where typed text starts.
    anchors {
      fill: parent
      leftMargin: control.leftPadding
      rightMargin: control.rightPadding
      topMargin: control.topPadding
      bottomMargin: control.bottomPadding
    }
    verticalAlignment: Text.AlignVCenter
    color: Colors.foreground.medium
    font: Fonts.body
    visible: !(control.activeFocus || control.text)
  }
}
