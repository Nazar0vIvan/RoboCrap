pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Styles 1.0

SpinBox {
  id: root

  implicitHeight: Metrics.h24
  leftPadding: Metrics.sp6
  rightPadding: Metrics.sz16
  editable: true
  font: Fonts.caption
  palette.text: Colors.foreground.high
  palette.highlight: Colors.primary.base
  palette.highlightedText: Colors.foreground.high

  contentItem: TextInput {
    text: root.displayText
    font: root.font
    color: root.palette.text
    selectionColor: Colors.primary.highlight
    selectedTextColor: root.palette.highlightedText
    readOnly: !root.editable
    validator: root.validator
    inputMethodHints: root.inputMethodHints
    verticalAlignment: Text.AlignVCenter
    selectByMouse: true
    clip: true
  }
  background: Rectangle {
    color: Colors.background.dp00
    radius: Metrics.r4
    border.color: root.activeFocus ? Colors.primary.base : Colors.background.dp24
  }

  component StepIndicator: Rectangle {
    property bool increase: false
    width: Metrics.sz16
    height: root.height / 2
    x: root.width - width
    color: (increase ? root.up.hovered : root.down.hovered) ? Colors.background.dp08 : Colors.background.dp00
    border.color: Colors.background.dp24
    Image {
      anchors.centerIn: parent
      width: Metrics.sp8
      height: Metrics.sp4
      source: "qrc:/icons/arrow_dropdown.svg"
      fillMode: Image.PreserveAspectFit
      rotation: parent.increase ? 180 : 0
    }
  }
  up.indicator: StepIndicator { increase: true }
  down.indicator: StepIndicator { y: root.height / 2 }
}
