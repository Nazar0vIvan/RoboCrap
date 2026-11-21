import QtQuick
import QtQuick.Layouts

import Styles 1.0
import Components 1.0

import robocrap_qml_module 1.0 as Backend

QxGroupBox {
  id: root

  readonly property Backend.DeviceRunner fts: Backend.Hub.device("fts")
  readonly property bool connected: root.fts?.isConnected ?? false
  readonly property bool streaming: root.fts?.data.streaming ?? false
  readonly property bool startPending: root.fts?.data.startPending ?? false
  readonly property bool stopRequested: root.fts?.data.stopRequested ?? false
  readonly property bool recording: root.fts?.data.recording ?? false
  readonly property bool saving: root.fts?.data.saving ?? false

  ColumnLayout {
    spacing: Metrics.sp12

    // Constant text on one elided line, hidden by opacity, so connecting never resizes the window.
    Text {
      Layout.fillWidth: true
      Layout.preferredWidth: 0
      opacity: root.connected ? 0 : 1
      text: qsTr("Connect FTS in Settings.")
      color: Colors.foreground.medium
      font: Fonts.caption
      elide: Text.ElideRight
    }

    FtsBars {
      fts: root.fts

      title: qsTr("Monitoring")
    }

    RowLayout {
      spacing: Metrics.sp8

      QxButton {
        enabled: root.connected && !root.startPending && !root.saving
        checked: root.streaming
        text: root.streaming ? qsTr("Stop") : qsTr("Start")
        reservedText: qsTr("Start")
        onClicked: root.fts.invoke(root.streaming ? "stopStreaming" : "startStreaming")
      }

      QxButton {
        enabled: root.connected && root.streaming && !root.startPending && !root.stopRequested
        text: qsTr("Bias")
        onClicked: root.fts.invoke("bias")
      }

      QxButton {
        enabled: root.connected && (root.fts?.data.canRecord ?? false)
        checked: root.recording
        text: root.recording ? qsTr("Recording…") : qsTr("Record")
        reservedText: qsTr("Recording…")
        onClicked: root.fts.invoke("startLogRecording")
      }

      QxButton {
        enabled: root.connected && (root.fts?.data.canSave ?? false)
        text: root.saving ? qsTr("Saving…") : qsTr("Save")
        reservedText: qsTr("Saving…")
        onClicked: root.fts.invoke("saveLogToDefaultFile")
      }
    }
  }
}
