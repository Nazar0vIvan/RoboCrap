pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import robocrap_qml_module 1.0 as App
import RoboCrap.Viewport3D 1.0 as Viewport3D
import Components 1.0
import Styles 1.0

QxPanel {
  id: root

  property App.SceneObject workpiece: null
  property App.SceneObject pendingWorkpiece: null
  property App.SceneObject savedJob: null
  readonly property bool editingJob: !!savedJob
  property bool restoreSavedFaces: false
  property string placementMessage: ""
  property string pickError: ""
  property string gridMessage: ""
  property bool restoreFaceOnGridEdit: false
  property bool pickingFace: false
  property bool pickingPoints: false
  property list<int> invalidOffsets: []
  readonly property bool picking: App.WorkspaceController.pickingWorkpiece
  readonly property string message: root.pickError || (root.picking ? qsTr("Click a placed workpiece in the viewport.")
                                       : root.pickingFace ? qsTr("Pick a planar face for %1Plane.").arg(root.activePlane.axis)
                                       : root.pickingPoints ? qsTr("Pick points to skip or restore · Esc to finish") + (root.gridMessage ? " · " + root.gridMessage : "")
                                       : root.workpiece ? root.gridMessage || root.placementMessage : qsTr("Select a placed workpiece."))
  readonly property int pickSize: Metrics.sz24 + Metrics.w2
  readonly property int pickColumn: root.pickSize + Metrics.sp8
  signal cancelRequested()

  component GridAxis: QtObject {
    property real editedSpacing: 0
    property real fittedSpacing: 1
    readonly property real spacing: editedSpacing || fittedSpacing
    property int count: 2
  }

  component PlaneDraft: QtObject {
    id: draft
    required property string axis
    property int faceIndex: 0
    readonly property int defaultUCount: axis === "X" ? 4 : 2
    readonly property bool configured: faceIndex > 0 || reversed || skipped.length > 0
                                       || u.editedSpacing > 0 || v.editedSpacing > 0
                                       || u.count !== defaultUCount || v.count !== 2
                                       || margins.some(value => value !== "0")
    property bool reversed: false
    readonly property GridAxis u: GridAxis { count: draft.defaultUCount }
    readonly property GridAxis v: GridAxis {}
    // Keep unfinished margin text when the operator switches planes.
    property list<string> margins: ["0", "0", "0", "0"]
    property list<point> skipped: []
    property int selectedSkip: -1
  }

  readonly property list<PlaneDraft> planeDrafts: [
    PlaneDraft { id: xPlane; axis: "X" },
    PlaneDraft { axis: "Y" },
    PlaneDraft { axis: "Z" }
  ]
  property PlaneDraft activePlane: xPlane

  function objectLabel(object: App.SceneObject): string {
    return object ? qsTr("#%1  %2").arg(object.objectId).arg(object.name) : ""
  }

  function canTarget(object: App.SceneObject): bool {
    return !!object && Viewport3D.OccController.workpieceModelRevision(object.objectId).length > 0
  }

  function setTarget(object: App.SceneObject) {
    root.pickingPoints = false
    root.invalidOffsets = []
    root.placementMessage = ""
    if (root.workpiece && root.workpiece !== object) {
      for (const plane of root.planeDrafts) {
        plane.faceIndex = 0
        plane.u.fittedSpacing = 1
        plane.v.fittedSpacing = 1
        plane.reversed = false
        plane.selectedSkip = -1
        plane.skipped = []
      }
    }
    if (!object || object !== root.workpiece)
      Viewport3D.OccController.setRefineTarget(object ? object.objectId : 0)
    else if (!root.showFace(0, false, false)) return
    root.restoreFaceOnGridEdit = !!object && root.activePlane.faceIndex > 0
    root.gridMessage = ""
    root.workpiece = object
    root.syncGridVisibility()
    root.pickingFace = false
    if (object) App.WorkspaceController.selectObject(object.objectId, false)
    root.pickError = ""
    App.WorkspaceController.pickingWorkpiece = !object
    if (object && root.restoreSavedFaces) root.restoreFaces()
  }

  function refreshGrid() {
    const slot = root.activePlane
    if (!slot?.faceIndex) {
      root.gridMessage = ""
      return
    }
    if (root.restoreFaceOnGridEdit) {
      root.showFace(slot.faceIndex, slot.reversed, false)
      return
    }
    if (!root.picking && !root.pickingFace) root.pickError = ""
    const preview = Viewport3D.OccController.updateRefineGrid(
        Qt.size(uDirection.acceptsInput() ? slot.u.count : 0, vDirection.acceptsInput() ? slot.v.count : 0),
        Qt.size(slot.u.editedSpacing || -1, slot.v.editedSpacing || -1), slot.margins)
    root.invalidOffsets = preview.invalid
    root.gridMessage = preview.message
    if (preview.spacing.width > 0 && !slot.u.editedSpacing) slot.u.fittedSpacing = preview.spacing.width
    if (preview.spacing.height > 0 && !slot.v.editedSpacing) slot.v.fittedSpacing = preview.spacing.height
  }

  function syncGridVisibility() {
    Viewport3D.OccController.setGridVisibility(showPoints.checked, root.planeDrafts.map(slot => slot.faceIndex))
  }

  function draftPlanes(): var {
    return root.planeDrafts.map(slot => ({
      faceIndex: slot.faceIndex, reversed: slot.reversed,
      uCount: slot.u.count, vCount: slot.v.count,
      uSpacing: slot.u.editedSpacing || -1, vSpacing: slot.v.editedSpacing || -1,
      margins: slot.margins.slice(), skipped: slot.skipped.map(point => [point.x, point.y])
    }))
  }

  function restoreFaces() {
    root.restoreSavedFaces = false
    const job = root.savedJob.refineJob
    for (let i = 0; i < root.planeDrafts.length; ++i) {
      const slot = root.planeDrafts[i]
      const plane = job.planes[i]
      slot.faceIndex = plane.faceIndex
      slot.reversed = plane.reversed
      slot.skipped = plane.skippedUV.map(pair => Qt.point(pair[0], pair[1]))
    }
    const result = Viewport3D.OccController.restoreRefineGrids(job, root.draftPlanes())
    if (result.staleFaces) {
      for (const slot of root.planeDrafts) {
        slot.faceIndex = 0
        slot.reversed = false
        slot.skipped = []
      }
      root.pickError = result.error
      return
    }
    if (result.error) root.placementMessage = result.error
    else if (result.placementChanged)
      root.placementMessage = qsTr("Workpiece placement changed. Recheck fixture clearance before generation.")
    root.syncGridVisibility()
    if (root.showFace(root.activePlane.faceIndex, root.activePlane.reversed, false)) {
      root.syncGridSelection()
      Qt.callLater(root.refreshGrid)
    }
  }

  function saveDraft() {
    const result = App.Scene.saveRefineJob(root.savedJob?.objectId || 0, root.workpiece?.objectId || 0, root.draftPlanes())
    if (result.error) {
      root.pickError = result.error
      return
    }
    App.WorkspaceController.selectObject(result.objectId, false)
    root.cancelRequested()
  }

  function showFace(index: int, reversed: bool, picking: bool): bool {
    const wasPickingPoints = root.pickingPoints
    root.pickingPoints = false
    root.pickError = Viewport3D.OccController.showRefineFace(index, reversed, picking)
    if (root.pickError.length > 0) {
      root.pickingPoints = wasPickingPoints
      return false
    }
    if (index && root.restoreFaceOnGridEdit) {
      root.restoreFaceOnGridEdit = false
      Qt.callLater(root.refreshGrid)
    }
    root.pickingFace = picking
    return true
  }

  function syncGridSelection() {
    const slot = root.activePlane
    const selected = slot.selectedSkip >= 0 ? slot.skipped[slot.selectedSkip] : Qt.point(0, 0)
    Viewport3D.OccController.setGridSelection(slot.skipped, selected, root.pickingPoints)
  }

  onPickingPointsChanged: root.syncGridSelection()

  function beginPointPicking() {
    const slot = root.activePlane
    if (!root.workpiece || !slot.faceIndex) {
      root.pickError = qsTr("Assign a face before picking grid points.")
      return
    }
    if (!root.pickingPoints) {
      App.WorkspaceController.pickingWorkpiece = false
      App.WorkspaceController.selectObject(root.workpiece.objectId, false)
      if (!root.showFace(slot.faceIndex, slot.reversed, false)) return
      root.pickingPoints = true
    }
    exclusions.forceActiveFocus()
  }

  function selectSkip(row: int) {
    const slot = root.activePlane
    if (row < 0 || row >= slot.skipped.length) return
    root.beginPointPicking()
    slot.selectedSkip = row
    root.syncGridSelection()
  }

  function removeSkip(row: int) {
    const slot = root.activePlane
    if (row < 0 || row >= slot.skipped.length) return
    if (slot.selectedSkip === row) slot.selectedSkip = -1
    else if (slot.selectedSkip > row) --slot.selectedSkip
    slot.skipped = slot.skipped.filter((point, index) => index !== row)
    root.syncGridSelection()
  }

  function outsidePointField(scenePos: point) {
    if (!exclusionField.visible || !scroll.contains(scroll.mapFromItem(null, scenePos))
        || !exclusionField.contains(exclusionField.mapFromItem(null, scenePos))) root.finishPointPicking()
  }

  function finishPointPicking() {
    root.pickError = ""
    root.pickingPoints = false
  }

  Shortcut {
    sequence: "Esc"
    enabled: root.pickingPoints
    autoRepeat: false
    onActivated: root.finishPointPicking()
  }
  Shortcut {
    sequence: "Del"
    enabled: root.pickingPoints && !(root.Window.window?.activeFocusItem instanceof TextInput)
    autoRepeat: false
    onActivated: root.removeSkip(root.activePlane.selectedSkip)
  }

  Connections {
    target: Viewport3D.OccController
    function onWorkpieceModelsChanged() {
      if (root.restoreSavedFaces && !root.workpiece) root.bindSavedTarget()
    }
    function onGridEditKeyPressed(key) {
      if (key === Qt.Key_Escape) root.finishPointPicking()
      else root.removeSkip(root.activePlane.selectedSkip)
    }
    function onGridPointPicked(index) {
      if (!root.pickingPoints) return
      if (index.x < 1) {
        root.finishPointPicking()
        return
      }
      root.pickError = ""
      const slot = root.activePlane
      const row = slot.skipped.findIndex(point => point.x === index.x && point.y === index.y)
      if (row >= 0) root.removeSkip(row)
      else {
        slot.skipped.push(index)
        root.syncGridSelection()
      }
    }
    function onFacePicked(index, error) {
      if (error.length > 0) {
        root.pickError = error
        return
      }
      const slot = root.activePlane
      const other = root.planeDrafts.find(plane => plane !== slot && plane.faceIndex === index)
      if (other) {
        root.pickError = qsTr("Face %1 is already assigned to %2Plane.").arg(index).arg(other.axis)
        return
      }
      App.WorkspaceController.selectObject(root.workpiece.objectId, false)
      const reversed = (slot.faceIndex === 0 || slot.faceIndex === index) && slot.reversed
      if (!root.showFace(index, reversed, false)) return
      if (slot.faceIndex !== index) {
        slot.selectedSkip = -1
        slot.skipped = []
        root.syncGridSelection()
      }
      slot.faceIndex = index
      root.syncGridVisibility()
      slot.reversed = reversed
      Qt.callLater(root.refreshGrid)
    }
  }

  Component.onCompleted: {
    const selected = App.WorkspaceController.selectedObject
    if (selected?.kind === App.SceneObject.RefineJob) {
      root.savedJob = selected
      const job = selected.refineJob
      for (let i = 0; i < root.planeDrafts.length; ++i) {
        const slot = root.planeDrafts[i]
        const plane = job.planes[i]
        slot.margins = plane.marginsMmLeftTopBottomRight.map(value => String(value))
        const axes = [slot.u, slot.v]
        for (let j = 0; j < axes.length; ++j) {
          axes[j].count = plane.countsUV[j]
          axes[j].fittedSpacing = plane.spacingMmUV[j]
          axes[j].editedSpacing = plane.autoSpacingUV[j] ? 0 : plane.spacingMmUV[j]
        }
      }
      root.restoreSavedFaces = true
      root.bindSavedTarget()
    } else root.setTarget(root.canTarget(selected) ? selected : null)
  }

  function bindSavedTarget() {
    const target = App.Scene.findObject(App.Scene.resolveRefineTarget(root.savedJob.objectId))
    root.setTarget(root.canTarget(target) ? target : null)
    if (!root.workpiece)
      root.pickError = qsTr("Pick the mounted workpiece for %1 when its model is visible.").arg(root.savedJob.name)
  }
  Component.onDestruction: {
    App.WorkspaceController.pickingWorkpiece = false
    Viewport3D.OccController.setRefineTarget(0)
  }

  Connections {
    target: App.WorkspaceController
    function onWorkpiecePicked(id, tool) {
      const candidate = App.Scene.findObject(id)
      if (!root.canTarget(candidate)) {
        root.pickError = tool ? qsTr("The end effector is a tool. Pick a workpiece.")
                       : candidate ? qsTr("%1 has no loaded workpiece model.").arg(root.objectLabel(candidate))
                                   : qsTr("No workpiece at this position. Pick its solid.")
        return
      }
      if (root.workpiece && candidate !== root.workpiece && root.planeDrafts.some(plane => plane.configured)) {
        root.pickError = ""
        root.pendingWorkpiece = candidate
        confirmSwitch.open()
      } else {
        root.setTarget(candidate)
      }
    }
  }

  Connections {
    target: App.Scene
    function onObjectRemoved(id) {
      if (root.savedJob?.objectId === id) {
        root.cancelRequested()
        return
      }
      if (root.workpiece?.objectId === id || root.pendingWorkpiece?.objectId === id) {
        confirmSwitch.close()
        root.pendingWorkpiece = null
        if (root.workpiece?.objectId === id) root.setTarget(null)
        root.pickError = qsTr("Workpiece removed from the scene. Pick another.")
      }
    }
  }

  Dialogs.MessageDialog {
    id: confirmSwitch
    title: qsTr("Switch workpiece?")
    text: qsTr("Switch from %1 to %2 and clear all face assignments and skipped points?")
            .arg(root.objectLabel(root.workpiece)).arg(root.objectLabel(root.pendingWorkpiece))
    buttons: Dialogs.MessageDialog.Ok | Dialogs.MessageDialog.Cancel
    onAccepted: {
      if (root.canTarget(root.pendingWorkpiece)) root.setTarget(root.pendingWorkpiece)
      else root.pickError = qsTr("The proposed workpiece is no longer available.")
      root.pendingWorkpiece = null
    }
    onRejected: {
      root.pendingWorkpiece = null
      App.WorkspaceController.pickingWorkpiece = !root.workpiece
    }
  }

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
    property bool pickEnabled: false
    property bool picking: false
    signal pickRequested()

    Field {
      readOnly: true
      text: pick.value
      Accessible.name: pick.labelText
    }
    QxToolButton {
      imageSource: "qrc:/icons/pick.svg"
      imageSize: root.pickSize
      padding: Metrics.sp0
      enabled: pick.pickEnabled
      checked: pick.picking
      onClicked: pick.pickRequested()
      Accessible.name: pick.pickName
      ToolTip.text: pick.pickName
      ToolTip.visible: hovered || visualFocus
    }
  }

  component PlaneTool: QxToolButton {
    padding: Metrics.sp0
    radius: 0
    imageSize: Metrics.sz36 - 2 * Metrics.w1
    Accessible.name: text
    ToolTip.text: text
    ToolTip.visible: hovered || visualFocus
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

    function acceptsInput(): bool {
      return !!settings.itemAt(0)?.valid && !!settings.itemAt(1)?.valid
    }

    Caption { text: qsTr("%1-direction · mm").arg(direction.axis) }
    Repeater {
      id: settings
      model: ["distance", "amount"]
      delegate: RefineFrameIconRow {
        id: setting
        readonly property TextInput editor: spin.contentItem as TextInput
        readonly property bool valid: !!setting.editor?.acceptableInput
        onValidChanged: Qt.callLater(root.refreshGrid)
        required property int index
        required property string modelData
        iconName: setting.modelData
        spacing: Metrics.sp16

        QxSpinBox {
          id: spin
          readonly property bool isSpacing: setting.index === 0
          // Qt SpinBox stores integers. One spacing unit here is 0.001 mm.
          readonly property int unitsPerStep: spin.isSpacing ? 1000 : 1
          Layout.fillWidth: true
          Layout.minimumWidth: 0
          live: false
          from: spin.isSpacing ? 1 : 2
          to: 2147483647
          stepSize: spin.unitsPerStep
          value: spin.isSpacing ? Math.round(direction.draft.spacing * spin.unitsPerStep)
                                : direction.draft.count
          locale: Qt.locale("C")
          validator: DoubleValidator {
            locale: "C"
            bottom: spin.from / spin.unitsPerStep
            top: spin.to / spin.unitsPerStep
            decimals: spin.isSpacing ? 3 : 0
            notation: DoubleValidator.StandardNotation
          }
          textFromValue: (value, locale) => String(value / spin.unitsPerStep)
          valueFromText: (text, locale) => setting.valid ? Math.round(Number.fromLocaleString(locale, text) * spin.unitsPerStep)
                                                       : spin.value
          function commitValue(units: int) {
            if (spin.isSpacing) direction.draft.editedSpacing = units / spin.unitsPerStep
            else direction.draft.count = units
            Qt.callLater(root.refreshGrid)
          }
          onValueModified: spin.commitValue(spin.value)
          Connections {
            target: setting.editor
            function onAccepted() {
              spin.commitValue(spin.valueFromText(setting.editor.text, spin.locale))
            }
          }
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
        value: root.objectLabel(root.workpiece)
        pickName: qsTr("Pick workpiece")
        pickEnabled: true
        picking: root.picking
        onPickRequested: {
          root.finishPointPicking()
          if (root.pickingFace && !root.showFace(root.activePlane.faceIndex, root.activePlane.reversed, false)) return
          root.pickError = ""
          App.WorkspaceController.pickingWorkpiece = !root.picking
        }
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
          Layout.rightMargin: root.pickColumn
          spacing: Metrics.sp10
          Repeater {
            model: root.planeDrafts
            delegate: PlaneTool {
              id: plane
              required property PlaneDraft modelData
              text: plane.modelData.axis + "Plane"
              imageSource: "qrc:/icons/" + plane.modelData.axis.toLowerCase() + "plane.svg"
              checkable: true
              checked: root.activePlane === plane.modelData
              ButtonGroup.group: planes
              onClicked: {
                plane.forceActiveFocus()
                const slot = plane.modelData
                if (root.workpiece) {
                  App.WorkspaceController.pickingWorkpiece = false
                  App.WorkspaceController.selectObject(root.workpiece.objectId, false)
                }
                if (root.showFace(slot.faceIndex, slot.reversed, !!root.workpiece)) {
                  root.activePlane = slot
                  root.invalidOffsets = []
                  root.syncGridSelection()
                  Qt.callLater(root.refreshGrid)
                }
              }
            }
          }
          Item { Layout.fillWidth: true }
          PlaneTool {
            id: showPoints
            text: qsTr("Show points")
            imageSource: "qrc:/icons/show_points.svg"
            checkable: true
            enabled: !!root.workpiece
            onToggled: root.syncGridVisibility()
          }
          PlaneTool {
            text: qsTr("Reverse side")
            imageSource: "qrc:/icons/reverse.svg"
            enabled: !!root.workpiece
            checked: root.activePlane.reversed
            onClicked: {
              const slot = root.activePlane
              if (root.showFace(slot.faceIndex, !slot.reversed, root.pickingFace)) slot.reversed = !slot.reversed
            }
          }
        }
        PickRow {
          labelText: root.activePlane.axis + "Plane"
          value: root.activePlane.faceIndex ? qsTr("Face %1").arg(root.activePlane.faceIndex) : ""
          pickName: qsTr("Pick planar face")
          pickEnabled: !!root.workpiece
          picking: root.pickingFace
          onPickRequested: {
            App.WorkspaceController.pickingWorkpiece = false
            App.WorkspaceController.selectObject(root.workpiece.objectId, false)
            root.showFace(root.activePlane.faceIndex, root.activePlane.reversed, !root.pickingFace)
          }
        }
      }

      QxCollapsibleSection {
        Layout.fillWidth: true
        title: qsTr("Scan grid")
        GridDirection { id: uDirection; axis: "U"; draft: root.activePlane.u }
        Divider {}
        GridDirection { id: vDirection; axis: "V"; draft: root.activePlane.v }
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
              Field {
                text: root.activePlane.margins[margin.index]
                validator: DoubleValidator { locale: "C"; bottom: 0; notation: DoubleValidator.StandardNotation }
                Accessible.name: qsTr("%1 margin in mm").arg(margin.modelData.side)
                onTextEdited: {
                  root.activePlane.margins[margin.index] = text
                  root.refreshGrid()
                }
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
          iconAlignment: Qt.AlignTop
          Rectangle {
            id: exclusionField
            Layout.fillWidth: true
            Layout.preferredHeight: Metrics.h24 * 4 + Metrics.sp4
            color: Colors.background.dp00
            radius: Metrics.r4
            border.color: root.pickingPoints ? Colors.primary.base : Colors.background.dp24
            ListView {
              id: exclusions
              anchors.fill: parent
              anchors.margins: Metrics.w1
              clip: true
              boundsBehavior: Flickable.StopAtBounds
              model: root.activePlane.skipped
              activeFocusOnTab: true
              currentIndex: root.activePlane.selectedSkip
              keyNavigationEnabled: false
              Keys.onUpPressed: root.selectSkip(Math.max(0, root.activePlane.selectedSkip - 1))
              Keys.onDownPressed: root.selectSkip(Math.min(exclusions.count - 1, root.activePlane.selectedSkip + 1))
              Keys.onDeletePressed: event => {
                if (!event.isAutoRepeat) root.removeSkip(root.activePlane.selectedSkip)
                event.accepted = true
              }
              TapHandler { onTapped: root.beginPointPicking() }
              Accessible.name: qsTr("Skipped grid indices")
              delegate: ItemDelegate {
                id: row
                required property point modelData
                required property int index
                width: ListView.view.width
                height: Metrics.h24
                padding: Metrics.sp4
                readonly property bool outOfRange: modelData.x > root.activePlane.u.count || modelData.y > root.activePlane.v.count
                readonly property bool offFace: !outOfRange && root.invalidOffsets.includes(
                    (modelData.y - 1) * root.activePlane.u.count + modelData.x - 1)
                text: "(%1,%2)".arg(modelData.x).arg(modelData.y)
                      + (outOfRange ? qsTr(" · outside grid counts") : offFace ? qsTr(" · off trimmed face") : "")
                font: Fonts.caption
                palette.text: Colors.foreground.high
                background: Rectangle {
                  color: row.index === root.activePlane.selectedSkip || row.hovered ? Colors.background.dp12 : Colors.background.dp00
                  border.color: row.visualFocus ? Colors.primary.base : color
                }
                onClicked: root.selectSkip(row.index)
              }
            }
          }
        }
      }
    }
  }

  QxTextInput {
    id: status
    implicitWidth: 0
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.leftMargin: Metrics.sp8
    Layout.rightMargin: Metrics.sp8
    Layout.preferredHeight: Metrics.h24
    text: root.message
    color: Colors.maxColor
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    font: Fonts.caption
    readOnly: true
    clip: true
    onTextChanged: cursorPosition = 0
    Accessible.name: qsTr("Refine Frame status")
    ToolTip.text: text
    ToolTip.visible: status.contentWidth > status.width && status.hovered
  }
  RowLayout {
    Layout.fillWidth: true
    Layout.leftMargin: Metrics.sp8
    Layout.rightMargin: Metrics.sp8
    spacing: Metrics.sp8
    QxButton {
      Layout.fillWidth: true
      text: root.editingJob ? qsTr("Save Changes") : qsTr("Create Refine Job")
      reservedText: root.editingJob ? qsTr("Create Refine Job") : qsTr("Save Changes")
      enabled: !!root.workpiece && uDirection.acceptsInput() && vDirection.acceptsInput()
      onClicked: {
        forceActiveFocus()
        Qt.callLater(root.saveDraft)
      }
    }
    QxButton {
      text: qsTr("Cancel")
      onClicked: {
        if (root.savedJob) App.WorkspaceController.selectObject(root.savedJob.objectId, false)
        root.cancelRequested()
      }
    }
  }
}
