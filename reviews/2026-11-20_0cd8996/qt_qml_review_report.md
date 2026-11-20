# QML Code Review Report (qt-qml-review, consolidated)

- **Commit**: 0cd8996033021e7080898455cd0db10bea4fb078 (master). All line numbers refer to this commit.
- **Scope**: codebase, 38 QML files, 4,277 lines (qml/Modules/Components, qml/Modules/Styles, src/application/qml).
- **Agents**: 3 shards (lines):
  - S1: modules and Main.qml, 1,426;
  - S2: MenuBar, Dashboard, Network, Viewport3D and CalibrationResultWindow, 1,315;
  - S3: the Workspace panels, 1,536.

  Cross-shard checks are best effort.
- **qmllint**: ran (`C:/Felgo/Felgo/mingw_64/bin/qmllint.EXE`, `-I build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug/qml`). It resolved robocrap_qml_module, Components, Styles and RoboCrap.Viewport3D; only QQmlPropertyMap stayed unresolved.
- **Lint**: 329 findings in 40 rule/message pairs, unchanged in `qml_lint_summary.txt`.
- **Counts**: 29 D-blocks, 11 I-blocks; no I-block cap applied.
- **Extra check**: agents also applied the project rule "Mandatory interface stability" (AGENTS.md) under A2.

The D- and I-blocks below are the agents' reports, with a **Verdict** line from reconciliation (see `ledger.md`). Paths are relative to the repository root.

---

## Shard 1: qml/Modules/Components, qml/Modules/Styles, src/application/qml/Main.qml

Rejected by the agent:
- The tool-panel signal order is correct.
- `cancelEditing` emits `cancelRequested`.
- Hub runners have a parent.
- There is no `setContextProperty`.
- ModeToolBar has a fixed `implicitHeight: 83`.
- The Basic `Control.qml` exists in Qt 6.8.3.

#### [S1-D1] FTS force value labels change width on each sample and resize the FTS Panel window
- **File**: `qml/Modules/Components/QxProgressBar.qml:64`
- **Category**: Layout & Anchoring, confidence 82
- **Finding**: `valueText` (68, `pb.value.toFixed(3)`) has no reserved width; "-1980.000" is wider than "0.000". `QxProgressBar.implicitWidth` (17, `rl.implicitWidth`) follows it, the FtsBars ColumnLayout takes the widest bar, and Main.qml:245-250 sets the window width (min = max) to `ftsPanel.implicitWidth + 32`. The FTS Panel window therefore changes width while values stream, which breaks interface stability.
- **Trace**: FtsBars.qml:29-45 (ranges ±1980/±660/±60, value from `fts.data[axis]`) → FtsPanel.qml:34 → Main.qml:247-250.
- **Mitigation**: Give `valueText` a fixed width, for example `Layout.preferredWidth` from TextMetrics of the widest formatted from/to value, right-aligned. Alternatively, stop binding the tool window's width to the live implicit width.
- **Verdict**: CONFIRMED (read QxProgressBar.qml:10-73, FtsBars.qml:28-46). Project-rule violation.

#### [S1-D2] QxButton width follows its text, so buttons with changing captions move their neighbours
- **File**: `qml/Modules/Components/QxButton.qml:17`
- **Category**: Layout & Anchoring, confidence 85
- **Finding**: The Basic Button computes `implicitWidth = max(implicitBackgroundWidth, implicitContentWidth + padding)`, and its own background has `implicitWidth: 100`. QxButton's replacement background has none, so the width is the text width + 24. Affected consumers:
  - FtsPanel.qml:52 (Start/Stop) moves Bias, Record and Save;
  - FtsPanel.qml:69 (Record/Recording…) moves Save by about 20 px;
  - FtsPanel.qml:77 (Save/Saving…) and Network.qml:143 (Connect/Disconnect) widen their rows and, through S1-D1 and S1-D3, the windows.
- **Trace**: Qt 6.8.3 Basic/Button.qml:11,38. The buttons in the FtsPanel.qml:42 RowLayout have no `Layout.*Width`.
- **Mitigation**: Give QxButton a stable width, either a minimum implicit width on the background or a width property sized from the longest caption with TextMetrics. Otherwise each consumer that toggles a caption must set `Layout.preferredWidth`.
- **Verdict**: CONFIRMED (QxButton.qml read). Same cause as S2-D2 (merged). Project-rule violation.

#### [S1-D3] Settings window size follows Network content, so connecting and status text resize it
- **File**: `src/application/qml/Main.qml:268`
- **Category**: Layout & Anchoring, confidence 85
- **Finding**: `settingsWindow.width/height` are bound to `networkSettings.contentWidth/Height`. Network is a QxScrollView with `contentWidth: page.implicitWidth`, and its ColumnLayout has no width of its own. The width changes with:
  - the RSI test status Label (Network.qml:203-209): elide does not reduce implicitWidth, so the window widens instead (the comment at 202 says this cannot happen);
  - the Connect/Disconnect caption (S1-D2).
- **Trace**: QxScrollView.qml:15, 30-39; NetworkPanel.qml:7, 24-31.
- **Mitigation**: Do not bind the window size to the live content size. Give the label `Layout.preferredWidth: 0` and keep fillWidth.
- **Verdict**: CONFIRMED (read QxScrollView.qml, Network.qml:195-212, Main.qml:260-280). Same cause as S2-D3 (merged). Project-rule violation.

#### [S1-D4] Precise-mode dry scan toggles only UI flags; nothing scans
- **File**: `src/application/qml/Main.qml:475`
- **Finding**: `onScanToggleRequested`/`onScanStopRequested` only flip `dryScanActive/dryScanRunning`, which only ModeToolBar reads (193-212). No backend call is made.
- **Verdict**: REJECTED. docs/code context/APP_DESCRIPTION.md describes the Precise submode (Dry Scan, Scan) as a placeholder without implementation.

#### [S1-D5] QxTextInput.confirmed is never set to true
- **File**: `qml/Modules/Components/QxTextInput.qml:10`
- **Finding**: `confirmed` is only reset (39) and read by the border colour (33); no consumer sets it.
- **Mitigation**: Remove it and the green branch.
- **Verdict**: Accepted. Reduction rank 5.

#### [S1-D6] Custom placeholder sits 12 px left of the typed text and does not count in the field size
- **File**: `qml/Modules/Components/QxTextInput.qml:45`
- **Finding**: `defaultTxt` uses `anchors.fill: parent` without padding, so it starts at x = 0 while the text starts at leftPadding 12. Basic TextField sizes itself from its built-in placeholder (TextField.qml:12), so this Text is not included. The text is visible at Network.qml:128 ("Learned").
- **Mitigation**: Use the built-in `placeholderText`/`placeholderTextColor`, or anchor the Text with the padding as margins.
- **Verdict**: Accepted (low).

#### [S1-D7] "Importing surface…" status can never be displayed
- **File**: `src/application/qml/Main.qml:301`
- **Finding**: `SceneSurfaceImporter::importSurface` sets `busy` true and false inside one synchronous GUI call (scenesurfaceimporter.cpp:70-97), so line 301 and the `!surfaceImporter.busy` terms at 168 and 374 never take effect.
- **Mitigation**: Remove the dead branch and terms, or move the import to a worker.
- **Verdict**: CONFIRMED. Same cause as C++ S2-I4 (merged). Reduction rank 5.

#### [S1-D8] Repeater delegate reaches outer ids without pragma ComponentBehavior: Bound
- **File**: `qml/Modules/Components/QxBusyIndicator.qml:1`
- **Finding**: The delegate (47) reads `root.*` and `ring.*`, which causes the QL:unqualified rows at 52-65. It already has `required property int index`.
- **Mitigation**: Add `pragma ComponentBehavior: Bound`.
- **Verdict**: Accepted.

#### [S1-D9] Value label updated on every sample uses AutoText and shaping
- **File**: `qml/Modules/Components/QxProgressBar.qml:64`
- **Finding**: `valueText` changes on every FTS sample (six instances) with the default AutoText. Plain-text labels with AutoText also appear at QxButton.qml:23, QxComboBox.qml:16 and :64, QxSwitch.qml:25, QxHField.qml:17 and QxVField.qml:25.
- **Mitigation**: `textFormat: Text.PlainText`; `font.preferShaping: false` on `valueText`.
- **Verdict**: Accepted (low).

#### [S1-D10] QxSwitch text would be drawn over the indicator
- **File**: `qml/Modules/Components/QxSwitch.qml:25`
- **Finding**: `implicitWidth` is only the indicator width (10) and padding is 0, so a Text contentItem would overlap the track. It is latent: no consumer sets `text` (DeltaModuleOutput.qml:36, MachiningPanel.qml:132).
- **Mitigation**: Remove the text contentItem.
- **Verdict**: Accepted. Reduction rank 5.

#### [S1-D11] Switch knob insets differ between on and off
- **File**: `qml/Modules/Components/QxSwitch.qml:44`
- **Finding**: The off inset is 2 px and the on inset 1 px (`parent.width - width - Metrics.w1`).
- **Mitigation**: Use `2 * Metrics.w1` in the on state.
- **Verdict**: Accepted (low).

#### [S1-D12] Duplicate alias displayonly is never used
- **File**: `qml/Modules/Components/QxSwitch.qml:15`
- **Verdict**: Accepted. Reduction rank 5.

#### [S1-I1] Calibration failure dialog is suppressed if an earlier result window is still open
- **File**: `src/application/qml/Main.qml:164`, confidence 70
- **Verdict**: RESOLVED, unreachable. CalibrationResultWindow.qml:20 sets `modality: Qt.ApplicationModal`, so no new run can be started while it is open.

#### [S1-I2] Calibration playback status line turns red when an old preparation error remains
- **File**: `src/application/qml/Main.qml:322`, confidence 65
- **Finding**: While calibration playback runs with an empty `calibrationStatus`, `errorShown` equals `MachiningPreparation.error.length > 0`, so line 374 colours the playback text with `Colors.maxColor`.
- **How to verify**: Find where MachiningPreparation's error is cleared, or add `!(root.calibrationActive && App.Playback.active)` to `errorShown`.
- **Verdict**: UNRESOLVED.

#### [S1-I3] PLC Panel window size is locked to DeltaPanel's live implicit size
- **File**: `src/application/qml/Main.qml:224`, confidence 60
- **Verdict**: RESOLVED. The state-dependent cause is the connect hint (S2-D1, confirmed).

#### [S1-I4] QxComboBox popup height has no limit and its ScrollIndicator can never show
- **File**: `qml/Modules/Components/QxComboBox.qml:82`, confidence 65
- **Finding**: The popup's `implicitHeight` is the full `contentHeight` (87); margins are -1 and the `Window.height` limit of the Basic style was dropped.
- **Verdict**: RESOLVED, no current defect. The largest models have 4 entries (db.json has four profiles; RobotStartPanel offers "1"–"4"). The missing limit is a latent risk.

#### [S1-I5] QxTextInput implicit width follows the typed text
- **File**: `qml/Modules/Components/QxTextInput.qml:28`, confidence 60
- **Finding**: The background has no `implicitWidth`, so an empty field is 24 px wide and grows while the user types. The current consumers set `preferredWidth` or `fillWidth`; ScenePanel.qml:219 was not checked.
- **Verdict**: UNRESOLVED.

#### [S1-I6] Status bar: AUT EXT row can shrink and the message moves when the message is long
- **File**: `src/application/qml/Main.qml:346`, confidence 60
- **Finding**: The Row has the default minimumWidth 0, and `statusMessage` (365) has no preferredWidth. A long message shrinks the Row and moves the message's left edge, and `busyIndicator.x` (383) can become negative (about −26 px). Hypothetical example: a 117-character message at the 960 px minimum width moves the edge by about 3 px.
- **Mitigation**: `Layout.preferredWidth: 0` on `statusMessage`, or `Layout.minimumWidth: implicitWidth` on the Row.
- **Verdict**: UNRESOLVED (runtime message lengths). Potential interface-stability violation.

---

## Shard 2: MenuBar, Dashboard (Delta, FTS), Network, Viewport3D, CalibrationResultWindow

#### [S2-D1] Connect hint resizes the PLC and FTS panel windows when the device connects or disconnects
- **File**: `src/application/qml/Views/Dashboard/DeltaPanel/DeltaPanel.qml:25` (also FtsPanel.qml:25)
- **Category**: Layout & Anchoring, confidence 90
- **Finding**: The word-wrapped hint Text ("Connect PLC in Settings." / "Connect FTS in Settings.") is a ColumnLayout child whose `visible` follows `isConnected` (27 in both files). An invisible item leaves the layout, so connecting changes `implicitHeight` by the text height plus `Metrics.sp12`. Two things follow:
  - the module row, LedsPanel and RobotStartPanel move (FtsBars and the button row in the FTS panel);
  - the window resizes, because Main.qml:224-227 and 247-250 bind min = max = implicit + 2·sp16.
- **Mitigation**: Keep the hint's space reserved: opacity or empty text with a fixed single-line height, or an overlay outside the ColumnLayout. Opacity alone is not enough while WordWrap can change the height.
- **Verdict**: CONFIRMED (read DeltaPanel.qml:18-34, FtsPanel.qml:20-30, Main.qml:218-258). Project-rule violation.

#### [S2-D2] FTS buttons change width when their text toggles and push the buttons after them
- **File**: `src/application/qml/Views/Dashboard/FtsPanel/FtsPanel.qml:52` (also :69, :77; Network.qml:143)
- **Verdict**: CONFIRMED. Same cause as S1-D2 (merged; see there).

#### [S2-D3] RSI test status Label never elides; long messages widen the Settings window
- **File**: `src/application/qml/Views/Network/Network.qml:203`
- **Finding**: The size grows along this chain: Label (fillWidth only, so preferred width = text width) → rsiTestPanel RowLayout → NetworkPanel.implicitWidth → QxScrollView layout (no width of its own) → `page.implicitWidth` → contentWidth → `settingsWindow.width` (Main.qml:268). The summary includes the user-editable path name (scenemachiningpreparation.cpp:280). Compare RobotStartPanel.qml:78, which sets `Layout.preferredWidth: 0` for exactly this reason.
- **Mitigation**: `Layout.preferredWidth: 0` on the Label.
- **Verdict**: CONFIRMED. Same cause as S1-D3 (merged).

#### [S2-D4] Connect button is enabled for the VFD profile but does nothing
- **File**: `src/application/qml/Views/Network/Network.qml:145`
- **Finding**: db.json lists "VFD ATV320U40N4C" (driver "vfd"), but main.cpp:70-72 registers only plc, fts and rsi, so `Hub.device("vfd")` returns null (devicehub.cpp:44). The `enabled` block (145-154) then returns true once the fields validate, and `onClicked` returns immediately (157-158).
- **Mitigation**: Disable the button while `runner` is null and show why in a reserved status line, or leave unregistered profiles out of the selector.
- **Verdict**: CONFIRMED (read 136-165; db.json:35), final-report defect 7. ARCHITECTURE §7 lists the VFD endpoint as an open question.

#### [S2-D5] Logo Image sets both top and verticalCenter anchors, so its height: 20 is ignored
- **File**: `src/application/qml/MenuBar/MainMenuBar.qml:56`
- **Finding**: With `anchors.top` (56) and `anchors.verticalCenter` (59), the item stretches to the full MenuBar height. Only PreserveAspectFit keeps the drawn logo near 20 px.
- **Mitigation**: Remove `anchors.top`.
- **Verdict**: Accepted (low).

#### [S2-D6] Ids that nothing references
- **File**: `src/application/qml/Views/Dashboard/FtsPanel/FtsPanel.qml:21`
- **Finding**:
  - FtsPanel: `cl` (21), `bars` (35), `rl` (43), `btnStart` (48), `btnBias` (57), `btnLog` (65), `btnSaveToFile` (74).
  - DeltaPanel: `rl` (35), `ledPanel` (40), and `moduleAP_P` (56, already STY-6).
  - DeltaModuleInput: `led` (33).
  - MainMenuBar: `logoImage` (50).
  - Network: `configPanel` (60), `btnCon` (137), `conPanel` (169), `conTable` (176).
- **Mitigation**: Remove these ids.
- **Verdict**: Accepted. Reduction rank 5.

#### [S2-D7] Frequently updated status Text uses the default AutoText format
- **File**: `src/application/qml/Views/Dashboard/DeltaPanel/RobotStartPanel.qml:76`
- **Mitigation**: `textFormat: Text.PlainText`, as Network.qml:206 already does.
- **Verdict**: Accepted (low).

#### [S2-I1] Logo Image may be added as a MenuBar item and moved into the menu Row
- **File**: `src/application/qml/MenuBar/MainMenuBar.qml:52`, confidence 60
- **How to verify**: At runtime, look for the "Cannot specify … anchors for items inside Row" warning, or check that `count` is 4.
- **Verdict**: UNRESOLVED (Qt Controls internals).

#### [S2-I2] FTS panel window width probably follows the button text changes
- **File**: `src/application/qml/Views/Dashboard/FtsPanel/FtsPanel.qml:69`, confidence 70
- **Verdict**: RESOLVED. The window width follows the panel's implicit width, which the bars (S1-D1) and the buttons (S1-D2) both drive. Fixing those two removes this case.

---

## Shard 3: Workspace panels (EndEffectorPanel, MachiningPanel, ModeToolBar, PropertiesPanel, ScenePanel, WorkflowPanel)

Rejected by the agent:
- Cancel-on-selection order is correct (workspacecontroller.cpp:98-100).
- Field ranges match the C++ limits.
- ModeToolBar labels stay within their 100 px minimum.
- Mode buttons stay in sync.
- The sphere toggle resyncs.
- WorkflowPanel's "Not available" text keeps its space.

#### [S3-D1] Validation/error message in the chamfer editor moves the Generate/Cancel buttons
- **File**: `src/application/qml/Views/Workspace/MachiningPanel.qml:179`
- **Category**: Layout & Anchoring, confidence 90
- **Finding**: The message Label (174-183) has `visible: text.length > 0` and `wrapMode: Text.Wrap`, and sits directly above the Generate/Cancel RowLayout (184). The text appears and disappears on ordinary changes:
  - typing an invalid value ("Enter valid values in all fields.");
  - a failed Generate (`coordinator.error`);
  - `unavailableReason` changing with the playback state.

  Messages of different lengths also wrap to different line counts.
- **Trace**: Every keystroke calls `root.edit()` (161), which updates `validDraft` (35-40). `setSettings` clears the error (scenemachining.cpp:85). `ScenePlayback::stateChanged` is connected to `availabilityChanged` (scenemachining.cpp:76).
- **Mitigation**: A fixed-height message region sized for the longest expected message (maximumLineCount and elide), or move the message below the buttons or into an overlay.
- **Verdict**: CONFIRMED (read 170-197). Project-rule violation.

#### [S3-D2] Playback status line appears above the editor fields and pushes all content down
- **File**: `src/application/qml/Views/Workspace/MachiningPanel.qml:107`
- **Finding**: The first item in the editor ColumnLayout is a two-line wrapping Label with `visible: root.playback.duration > 0`. When Dry Run starts while the editor is open, it pushes everything down, and the `phase` text can change its line count during playback.
- **Trace**: `duration` notifies on stateChanged (sceneplayback.h:23) and `phase` on playbackChanged (h:24). Dry Run is enabled through `canPlay` (Main.qml:452), which `chamferEditorOpen` does not gate.
- **Mitigation**: Keep the row permanently, at a fixed two-line height and elided, or show the playback status in the status bar.
- **Verdict**: CONFIRMED (read 95-125). Project-rule violation.

#### [S3-D3] Read-only ER collet fields keep a dead editing and validation path
- **File**: `src/application/qml/Views/Workspace/EndEffectorPanel.qml:117`
- **Finding**: The collet QxHField has `enabled: false`, so:
  - `onTextEdited`/`onEditingFinished` (130-134) and `editColletValue()` (47-51) never run;
  - the opacity at 118 is constant;
  - `validPoseDraft(root.colletDrafts)` in `validDraft` (25) validates values that Apply never sends (222 sends `root.colletPose`).
- **Mitigation**: Make the collet rows display-only, remove the handler and function, and drop the collet term from `validDraft`.
- **Verdict**: Accepted. Reduction rank 5.

#### [S3-D4] Close keeps unapplied TCP edits, so reopening shows values that differ from the stored pose
- **File**: `src/application/qml/Views/Workspace/EndEffectorPanel.qml:227`
- **Finding**: Drafts reload only in `onTcpChanged`, `onColletPoseChanged` and `Component.onCompleted` (57-62). Close and `openToolSetup` (Main.qml:33-53, 74) only toggle `toolSetupOpen`. MachiningPanel's Cancel, by contrast, discards its drafts.
- **Mitigation**: Reload both drafts on Close or when the panel becomes visible.
- **Verdict**: Accepted (UX).

#### [S3-D5] Percentage drafts show floating-point noise that exceeds the field's own validator
- **File**: `src/application/qml/Views/Workspace/MachiningPanel.qml:50`
- **Finding**: `String(value * 100)` turns a stored 0.07 into "7.000000000000001". `DoubleValidator { decimals: 8 }` (160) does not accept it. EndEffectorPanel uses `toFixed(4)`.
- **Mitigation**: Format with a fixed precision no larger than the validator's decimals and trim trailing zeros.
- **Verdict**: CONFIRMED (JS check: `String(0.07*100)` = "7.000000000000001"), consequence corrected. `validDraft` (35-40) uses `Number()`, not `acceptableInput`, so Generate still works. The effects are display noise and the validator blocking further typing in that field.

#### [S3-D6] ModeToolBar Tool's checkable/checked aliases and toggled signal are never used
- **File**: `src/application/qml/Views/Workspace/ModeToolBar.qml:61`
- **Finding**: Nothing uses the `checkable` (61) and `checked` (62) aliases or the `toggled(bool)` signal (66, emitted at 105). The `checked` branches of the colour expressions (77-78, 95-100) therefore cannot vary.
- **Mitigation**: Remove them.
- **Verdict**: Accepted. Reduction rank 5.

#### [S3-D7] ModeButton color properties are never overridden and should be readonly
- **File**: `src/application/qml/Views/Workspace/WorkflowPanel.qml:26`
- **Verdict**: Accepted (style).

#### [S3-D8] Files with outer-id delegates lack pragma ComponentBehavior: Bound
- **File**: `src/application/qml/Views/Workspace/EndEffectorPanel.qml:1`, `PropertiesPanel.qml:1`
- **Finding**: The delegates reach `root`: EndEffectorPanel 94-95, 116, 126, 130, 133, 179, 183, 186; PropertiesPanel 83, 379, 383-390. This is the cause of the QL:unqualified rows there. The delegates already declare required properties.
- **Mitigation**: Add the pragma to both files.
- **Verdict**: Accepted.

#### [S3-D9] Frequently updated and user-named labels use the default AutoText format
- **File**: `src/application/qml/Views/Workspace/MachiningPanel.qml:108`
- **Finding**: Affects the playback status (108-111), `inputName` (118, a user-renamable scene name that renders markup) and EndEffectorPanel.qml:94.
- **Mitigation**: `textFormat: Text.PlainText`.
- **Verdict**: Accepted (low).

#### [S3-D10] Scene row selection uses a MouseArea where a TapHandler fits
- **File**: `src/application/qml/Views/Workspace/ScenePanel.qml:262`
- **Finding**: A full-size MouseArea takes the exclusive grab, so the ItemDelegate's `onClicked` (251) works only from the keyboard.
- **Mitigation**: Use a TapHandler (`onTapped` with `point.modifiers`, `onDoubleTapped`).
- **Verdict**: Accepted (low).

#### [S3-I1] Every scene add/remove recreates all row delegates and loses rename/focus state
- **File**: `src/application/qml/Views/Workspace/ScenePanel.qml:94`, confidence 70
- **Finding**: `group.objects` is a new JS array on each `objects` change and is the inner Repeater's model (153). Rows are recreated, losing the rename in progress and the keyboard focus, for example on Del (Main.qml:63-71).
- **How to verify**: Rename (F2) during a Generate, or press Del with focus in the list.
- **Verdict**: UNRESOLVED (runtime). Same cause as C++ S2-I5.

#### [S3-I2] Three toolbar tools are permanently disabled and wired to nothing
- **File**: `src/application/qml/Views/Workspace/ModeToolBar.qml:161`, confidence 60
- **Finding**: "Cone from JSON" (161-164), "Scan" and "Start Machining" (242-247) are always disabled and have no handler.
- **Verdict**: RESOLVED, intended. APP_DESCRIPTION documents Cone from JSON and Scan as placeholders and Start Machining as disabled.

#### [S3-I3] Spindle EE panel shows a fabricated zero pose and Apply silently fails when the stored pose is empty
- **File**: `src/application/qml/Views/Workspace/EndEffectorPanel.qml:38`, confidence 60
- **Verdict**: RESOLVED, no current defect.
  - `m_measuringTcp` has a nonempty default (sceneendeffectors.h:66).
  - The collet frame comes from the constant `spindleColletTransform()` (sceneendeffectors.cpp:57-60), so `spindleColletPose()` is empty only if that constant were invalid.
  - The empty spindle TCP before the first Apply gives "0.0000" drafts, which is the expected initial state.

---

## Lint findings (mitigation per rule; rejected rows marked)
The full row listing is in `qml_lint_summary.txt`.

| Rule | Count | Mitigation / verdict |
|---|---|---|
| BND-1 property var | 20 | Typed properties where a concrete type exists (keep var for heterogeneous data, per contract) |
| BND-2 imperative '=' destroys binding | 8 | **Rejected**: the targets have no `text:` binding (importError, intersectionError, laInput/lpInput/paInput/ppInput, nameEditor), and RobotViewport.qml:28 runs in onDestruction |
| BND-5 list<> property without granular signals | 7 | Kept |
| IMG-1 Image without sourceSize | 4 | Set sourceSize |
| IMP-2 versioned import | 66 | Drop versions |
| IMP-3 Controls without style qualifier | 10 | Use Controls.Basic where contentItem/background are customised |
| ORD-1 attribute order | 129 | Kept (style) |
| PRF-1 transparent Rectangle | 11 | Item instead |
| PRF-3 clip | 12 | Verify each use |
| PRF-5 RichText | 4 | PlainText/StyledText if possible |
| QL:incompatible-type Main.qml:60 | 1 | Kept: QList<quint32> bound to list<real>; it converts at runtime, but qmlsc cannot compile the binding |
| QL:unqualified | 21 | Kept; caused by the missing pragma (S1-D8, S3-D8) |
| QL:unresolved-type QQmlPropertyMap | 20 | **Rejected (tool limit)**: QQmlPropertyMap is not exposed declaratively |
| STY-1 / STY-3 / STY-6 | 6 / 8 / 2 | Kept (style) |

## Summary
| Category | Lint | Deep (D) | Investigate (I) |
|---|---|---|---|
| Reported | 329 | 29 | 11 |
| Rejected | 28 | 1 | resolved 6 / unresolved 5 |
