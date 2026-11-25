pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Components 1.0
import Styles 1.0

QxPanel {
  id: root

  // Local example inputs until workpiece and job selection are connected.
  property string workpieceName: "GE-22"
  property bool editingJob: false
  property string message: root.workpieceName ? "" : qsTr("Select a placed workpiece.")
  readonly property int pickSize: Metrics.sz24 + Metrics.w2
  readonly property int pickColumn: root.pickSize + Metrics.sp8
  signal cancelRequested()

  component GridAxis: QtObject {
    property real spacing: 1
    property int count: 2
  }

  component PlaneDraft: QtObject {
    required property string axis
    required property string faceId
    property bool reversed: false
    property GridAxis u: GridAxis {}
    readonly property GridAxis v: GridAxis {}
    // Keep unfinished margin text when the operator switches planes.
    property list<string> margins: ["5", "5", "5", "5"]
    property list<point> skipped: []
  }

  readonly property list<PlaneDraft> planeDrafts: [
    PlaneDraft {
      id: xPlane
      axis: "X"
      faceId: "P234-g5j"
      u: GridAxis { count: 4 }
      skipped: [Qt.point(4, 2), Qt.point(3, 1)]
    },
    PlaneDraft { axis: "Y"; faceId: "P235-g5j"; skipped: [Qt.point(1, 1)] },
    PlaneDraft { axis: "Z"; faceId: "P236-g5j" }
  ]
  property PlaneDraft activePlane: xPlane

  title: qsTr("Properties")
  panelColor: Colors.background.dp01
  headerColor: Colors.background.dp08
  leftPadding: Metrics.w1
  rightPadding: Metrics.w1
  spacing: Metrics.sp6

  component Caption: Label {
    font: Fonts.caption
    color: Colors.foreground.medium
  }

  component Field: QxTextInput {
    id: field
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredHeight: Metrics.h24
    font: Fonts.caption
    color: readOnly ? Colors.foreground.medium : Colors.foreground.high
    leftPadding: Metrics.sp6
    rightPadding: Metrics.sp6
    topPadding: Metrics.sp4
    bottomPadding: Metrics.sp4
    background: Rectangle {
      radius: Metrics.r4
      color: field.readOnly ? root.panelColor : Colors.background.dp00
      border.color: field.activeFocus ? Colors.primary.base : Colors.background.dp24
    }
  }

  component NamedRow: QxHField {
    Layout.fillWidth: true
    fixedLabelWidth: true
    labelWidth: Metrics.sp40 + Metrics.w2
    labelFont: Fonts.caption
  }

  component PickRow: NamedRow {
    id: pick
    required property string value
    required property string pickName

    Field {
      readOnly: true
      text: pick.value
      Accessible.name: pick.labelText
    }
    QxToolButton {
      imageSource: "qrc:/icons/pick.svg"
      imageSize: root.pickSize
      padding: Metrics.sp0
      opacity: 1 - Colors.overlays.disabled
      enabled: false
      Accessible.name: pick.pickName
      ToolTip.text: pick.pickName
      ToolTip.visible: hovered || visualFocus
    }
  }

  component Divider: Rectangle {
    Layout.fillWidth: true
    implicitHeight: Metrics.w1
    color: Colors.background.dp24
  }

  component GridDirection: ColumnLayout {
    id: direction
    required property string axis
    required property GridAxis draft
    Layout.fillWidth: true
    Layout.rightMargin: root.pickColumn
    spacing: Metrics.sp6

    Caption { text: qsTr("%1-direction · mm").arg(direction.axis) }
    Repeater {
      model: ["distance", "amount"]
      delegate: RefineFrameIconRow {
        id: setting
        required property int index
        required property string modelData
        iconName: setting.modelData
        spacing: Metrics.sp16
        description: setting.index === 0 ? qsTr("%1 spacing (mm)").arg(direction.axis)
                                         : qsTr("Number of %1 points").arg(direction.axis)
        QxSpinBox {
          id: spin
          readonly property bool isSpacing: setting.index === 0
          // Qt SpinBox stores integers. One spacing unit here is 0.001 mm.
          readonly property int scale: spin.isSpacing ? 1000 : 1
          Layout.fillWidth: true
          Layout.minimumWidth: 0
          live: true
          from: spin.isSpacing ? 1 : 2
          to: 2147483647
          stepSize: spin.scale
          value: spin.isSpacing ? Math.round(direction.draft.spacing * spin.scale) : direction.draft.count
          locale: Qt.locale("C")
          validator: DoubleValidator {
            locale: "C"
            bottom: spin.from / spin.scale
            top: spin.to / spin.scale
            decimals: spin.isSpacing ? 3 : 0
            notation: DoubleValidator.StandardNotation
          }
          textFromValue: (value, locale) => String(value / spin.scale)
          valueFromText: (text, locale) => Math.round(Number(text) * spin.scale)
          onValueModified: {
            if (spin.isSpacing) direction.draft.spacing = spin.value / spin.scale
            else direction.draft.count = spin.value
          }
          Accessible.name: setting.description
        }
      }
    }
  }

  ScrollView {
    id: scroll
    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumHeight: 0
    clip: true
    contentWidth: availableWidth
    contentHeight: fields.implicitHeight
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    Component.onCompleted: (contentItem as Flickable).boundsBehavior = Flickable.StopAtBounds

    ColumnLayout {
      id: fields
      width: scroll.availableWidth
      spacing: Metrics.sp10

      PickRow {
        Layout.leftMargin: Metrics.sp8
        Layout.rightMargin: Metrics.sp12
        labelText: qsTr("Object")
        value: root.workpieceName
        pickName: qsTr("Pick workpiece")
      }
      NamedRow {
        Layout.leftMargin: Metrics.sp8
        Layout.rightMargin: Metrics.sp12 + root.pickColumn
        Layout.bottomMargin: Metrics.sp6
        labelText: qsTr("Method")
        QxComboBox {
          Layout.fillWidth: true
          Layout.minimumWidth: 0
          Layout.preferredHeight: Metrics.h24
          font: Fonts.caption
          leftPadding: Metrics.sp6
          topPadding: Metrics.sp4
          bottomPadding: Metrics.sp4
          model: [qsTr("By 3 Planes")]
          Accessible.name: qsTr("Refinement method")
        }
      }

      QxCollapsibleSection {
        Layout.fillWidth: true
        title: qsTr("Planes")
        ButtonGroup { id: planes }
        RowLayout {
          Layout.fillWidth: true
          spacing: Metrics.sp10
          Repeater {
            model: root.planeDrafts
            delegate: QxToolButton {
              id: plane
              required property PlaneDraft modelData
              padding: Metrics.sp0
              radius: 0
              text: plane.modelData.axis + "Plane"
              imageSource: "qrc:/icons/" + plane.modelData.axis.toLowerCase() + "plane.svg"
              imageSize: Metrics.sz36 - 2 * Metrics.w1
              checkable: true
              checked: root.activePlane === plane.modelData
              ButtonGroup.group: planes
              onClicked: {
                plane.forceActiveFocus()
                root.activePlane = plane.modelData
              }
              ToolTip.text: text
              ToolTip.visible: hovered || visualFocus
            }
          }
          Item { Layout.fillWidth: true }
          QxButton {
            text: checked ? qsTr("Side reversed") : qsTr("Reverse side")
            reservedText: checked ? qsTr("Reverse side") : qsTr("Side reversed")
            enabled: root.workpieceName.length > 0
            checkable: true
            checked: root.activePlane.reversed
            onToggled: root.activePlane.reversed = checked
            topPadding: Metrics.sp4
            bottomPadding: Metrics.sp4
          }
        }
        PickRow {
          labelText: root.activePlane.axis + "Plane"
          value: root.workpieceName ? root.activePlane.faceId : ""
          pickName: qsTr("Pick planar face")
        }
      }

      QxCollapsibleSection {
        Layout.fillWidth: true
        title: qsTr("Scan grid")
        GridDirection { axis: "U"; draft: root.activePlane.u }
        Divider {}
        GridDirection { axis: "V"; draft: root.activePlane.v }
        Divider {}
        Caption { text: qsTr("Margins (mm)") }
        GridLayout {
          Layout.fillWidth: true
          Layout.rightMargin: root.pickColumn
          columns: 2
          columnSpacing: Metrics.sp8
          rowSpacing: Metrics.sp10
          Repeater {
            model: [{icon: "margin-left", side: "−U"}, {icon: "margin-top", side: "+V"},
                    {icon: "margin-bottom", side: "−V"}, {icon: "margin-right", side: "+U"}]
            delegate: RefineFrameIconRow {
              id: margin
              required property int index
              required property var modelData
              iconName: margin.modelData.icon
              description: qsTr("%1 margin (mm)").arg(margin.modelData.side)
              Field {
                text: root.activePlane.margins[margin.index]
                validator: DoubleValidator { locale: "C"; bottom: 0; notation: DoubleValidator.StandardNotation }
                onTextEdited: root.activePlane.margins[margin.index] = text
                Accessible.name: margin.description
              }
            }
          }
        }
      }

      QxCollapsibleSection {
        Layout.fillWidth: true
        title: qsTr("Instances to Skip")
        RefineFrameIconRow {
          Layout.rightMargin: root.pickColumn
          iconName: "exclude"
          description: qsTr("Skipped grid indices")
          Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.h24 * 4 + Metrics.sp4
            color: Colors.background.dp00
            radius: Metrics.r4
            border.color: Colors.background.dp24
            ListView {
              id: exclusions
              anchors.fill: parent
              anchors.margins: Metrics.w1
              clip: true
              boundsBehavior: Flickable.StopAtBounds
              model: root.activePlane.skipped
              activeFocusOnTab: true
              onModelChanged: currentIndex = -1
              Keys.onDeletePressed: {
                const selected = exclusions.currentIndex
                if (selected >= 0)
                  root.activePlane.skipped = root.activePlane.skipped.filter((point, index) => index !== selected)
              }
              Accessible.name: qsTr("Skipped grid indices")
              delegate: ItemDelegate {
                id: row
                required property point modelData
                required property int index
                width: ListView.view.width
                height: Metrics.h24
                padding: Metrics.sp4
                text: "(%1,%2)".arg(row.modelData.x).arg(row.modelData.y)
                font: Fonts.caption
                palette.text: Colors.foreground.high
                background: Rectangle {
                  color: row.ListView.isCurrentItem || row.hovered ? Colors.background.dp12 : Colors.background.dp00
                  border.color: row.visualFocus ? Colors.primary.base : color
                }
                onClicked: {
                  exclusions.forceActiveFocus()
                  exclusions.currentIndex = row.index
                }
              }
            }
          }
        }
      }
    }
  }

  Caption {
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.leftMargin: Metrics.sp8
    Layout.rightMargin: Metrics.sp8
    Layout.preferredHeight: Metrics.h24
    text: root.message
    color: Colors.maxColor
    elide: Text.ElideRight
    ToolTip.text: text
    ToolTip.visible: truncated && messageHover.hovered
    HoverHandler { id: messageHover }
  }
  RowLayout {
    Layout.fillWidth: true
    Layout.leftMargin: Metrics.sp8
    Layout.rightMargin: Metrics.sp8
    spacing: Metrics.sp8
    QxButton {
      Layout.fillWidth: true
      text: root.editingJob ? qsTr("Save Changes") : qsTr("Create Refine Job")
      reservedText: qsTr("Create Refine Job")
      enabled: false
    }
    QxButton {
      text: qsTr("Cancel")
      onClicked: root.cancelRequested()
    }
  }
}
