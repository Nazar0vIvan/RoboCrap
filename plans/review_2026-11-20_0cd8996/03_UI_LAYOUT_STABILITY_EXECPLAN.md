# Stable UI layout: no window or control jumps on ordinary state changes, plus small QML defects

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`: "Mandatory interface stability", the "QML" section of `docs/code context/AGENT_CONTRACTS.md` and "Mandatory code reduction". QML arrow functions are JavaScript and are allowed; the lambda ban applies to C++. Before each change, re-read "Mandatory interface stability". After each milestone, record line counts here.

This plan comes from the whole-project review of commit 0cd8996 (findings in `reviews/2026-11-20_0cd8996/`). Everything needed is repeated below.


## Purpose / Big Picture

`AGENTS.md` forbids user-interface elements whose ordinary state changes resize the 3D viewport, a panel or a window, or move other controls. Ordinary state changes include a device connecting, a value updating, a caption toggling and a message appearing. The review found six places that break this rule:

1. **FTS Panel width.** The window's width changes many times per second while force values stream, because each value label is as wide as its current text.
2. **PLC Panel and FTS Panel height.** Both windows change height when their device connects or disconnects, because the hint "Connect … in Settings." appears and disappears.
3. **Toggling buttons.** Buttons whose caption toggles push the buttons after them: Start/Stop, Record/Recording…, Save/Saving… and Connect/Disconnect.
4. **Settings window width.** The Settings window widens when a long RSI status message appears, because the message label never elides.
5. **Generate and Cancel.** In the chamfer editor, these move up and down when a validation or error message appears, disappears or wraps.
6. **The chamfer editor.** It jumps down when Dry Run starts, because a playback status line appears at its top.

There are also small QML defects:
- the "Connect" button is enabled for the VFD device profile but does nothing, because no VFD device exists;
- the menu-bar logo is stretched to the bar height;
- input placeholders are drawn 12 px left of where typed text starts;
- the switch knob is 1 px off-centre when on;
- a percentage entered as 7 reopens as `7.000000000000001`;
- the end-effector panel shows unapplied edits again after Close and reopen.

One related layout risk in the status bar remains unconfirmed: a very long message can squeeze the "AUT EXT" indicator. This plan applies the safe fix for it too.

After this plan, none of these state changes moves or resizes anything, and the small defects are gone. To see it, build with `USE_HOT_RELOAD=OFF` and start the application disconnected. Open the PLC Panel, the FTS Panel and the Settings window, and open the chamfer editor on a generated machining path. Then:
- type an invalid value into a field;
- start and pause Dry Run;
- connect the RSI socket (Network settings, local address only; no robot is needed).

Nothing moves.


## Progress

- [x] (2026-11-20) Plan written after reading:
  - `qml/Modules/Components/QxButton.qml`, `QxProgressBar.qml`, `QxTextInput.qml`, `QxScrollView.qml` and `QxSwitch.qml`;
  - `src/application/qml/Views/Dashboard/FtsPanel/FtsPanel.qml` and `FtsBars.qml`;
  - `src/application/qml/Views/Dashboard/DeltaPanel/DeltaPanel.qml`;
  - `src/application/qml/Views/Network/Network.qml`;
  - `src/application/qml/Views/Workspace/MachiningPanel.qml` and `EndEffectorPanel.qml`;
  - `src/application/qml/MenuBar/MainMenuBar.qml`;
  - `src/application/qml/Main.qml` (window bindings 218-280, status bar 335-390, panel instances 520-575);
  - `resources/qrc/config/db.json`.
- [x] (2026-11-20) Start counts (physical lines): QxButton 30, QxProgressBar 73, QxTextInput 51, QxSwitch 63, FtsPanel 82, DeltaPanel 101, Network 212, MachiningPanel 197, EndEffectorPanel 232, MainMenuBar 131, Main 589.
- [x] (2026-11-20) Milestone 1 implemented:
  - `QxButton`: `reservedText` with `implicitWidth` from the wider of the caption and `Math.ceil(reservedMetrics.advanceWidth)`; with an empty `reservedText` the formula equals Basic `Button`'s own, so the 11 other `QxButton` uses are unchanged;
  - consumers: `FtsPanel` Start, Recording…, Saving…; `Network.qml` `btnCon` Disconnect;
  - `QxProgressBar`: value `Text` right-aligned, `Layout.preferredWidth: Math.ceil(valueMetrics.advanceWidth)` of `-max(|from|, |to|)` with three decimals; `FtsBars` is its only user;
  - `FtsPanel` and `DeltaPanel` hints: constant text, `opacity`, one elided line, `Layout.preferredWidth: 0`;
  - `Network.qml` RSI test status `Label`: `Layout.preferredWidth: 0`.
  Review: no visibility toggles, wrapping text or text-driven widths remain in the PLC Panel, FTS Panel or Settings window; the hints and the reserved captions change no size on state changes.
  Counts (physical / nonblank): QxButton 30/25 → 42/35, QxProgressBar 73/61 → 82/69, FtsPanel 82/62 → 87/67, DeltaPanel 101/78 → 103/80, Network 212/165 → 214/167 (+12 of these from the `TextMetrics` objects and properties the reserved widths require, +3 comments).
- [x] (2026-11-20) Milestone 1 user check: `all_qmllint`, `robocrap`, disconnected and equipment checks passed (user report).
- [x] (2026-11-20) Milestone 2 implemented:
  - `MachiningPanel.qml`: `FontMetrics captionMetrics` (`Fonts.caption`); the playback status and message `Label`s lost `visible`, keep `wrapMode: Text.Wrap`, and gained `maximumLineCount: 2`, `elide` and a minimum and preferred height of `Math.ceil(2 * captionMetrics.lineSpacing)`; the playback text is empty while `duration <= 0`;
  - `Main.qml` `statusMessage`: `Layout.preferredWidth: 0` and `leftPadding: busyIndicator.width + Metrics.sp6` (the busy indicator is 20 px wide).
  Review: in the chamfer editor no label above Generate/Cancel changes visibility on ordinary state changes any more; the "Dry Run: … s" label still follows `savedPath`, which changes only with the selection (deliberate navigation). The status-bar message no longer enters the bar's implicit width.
  Counts (physical / nonblank): MachiningPanel 197/195 → 210/208, Main 589/520 → 593/524.
- [x] (2026-11-20) Milestone 2 user check: `all_qmllint`, `robocrap` and the disconnected checks passed (user report).
- [x] (2026-11-20) Milestone 3 implemented:
  - `Network.qml` `btnCon`: `enabled` returns false without a runner, so the VFD profile cannot be connected; the following checks no longer repeat `runner &&`, and `onClicked` lost its now unreachable `if (!runner) return`;
  - `MainMenuBar.qml`: the logo's `top` anchor removed, so its 20 × 20 size holds;
  - `QxTextInput.qml`: the placeholder anchors inside the field's padding (one consumer: `Network.qml` Peer Port "Learned");
  - `QxSwitch.qml`: on-state knob x `parent.width - width - 2 * Metrics.w1`;
  - `MachiningPanel.qml` `beginEditing`: percentage drafts `String(Number((value * 100).toFixed(8)))`, other fields `String(value)`;
  - `EndEffectorPanel.qml`: `onVisibleChanged` reloads both drafts when the panel is shown.
  Counts (physical / nonblank): QxTextInput 51/41 → 58/48, QxSwitch 63/53 → 63/53, Network 214/167 → 215/169, MachiningPanel 210/208 → 211/209, EndEffectorPanel 232/228 → 234/230, MainMenuBar 131/102 → 129/101.
- [x] (2026-11-20) Correction after the user's review: the empty two-line playback region left a visible gap at the top of the chamfer editor (user screenshot). The region was removed with the `playback` input of `MachiningPanel` and its binding in `Main.qml`; the status bar shows the Dry Run status while the chamfer editor is open, through a new `statusBar.playbackShown` that also covers calibration playback and keeps the playback text out of the error colour. `AGENT_CONTRACTS.md` "QML" updated. Counts (physical / nonblank): MachiningPanel 211/209 → 195/193, Main 593/524 → 595/526.
- [x] (2026-11-20) Second correction after the user's review: the empty two-line message region left a gap above Generate/Cancel (user screenshot). The region and its `FontMetrics` were removed; `MachiningPanel` exposes `readonly property string message` (same text as before), and the status bar shows it while the editor is open, red only for a Generate error (`App.Machining.error`). User request in the same review: no scrollable view springs past its ends. `boundsBehavior: Flickable.StopAtBounds` added to the five `ScrollView`s (`QxScrollView`, `ScenePanel`, `PropertiesPanel`, `EndEffectorPanel`, `MachiningPanel`, on their `contentItem` at completion) and to the `QxComboBox` popup `ListView`; `ModeToolBar`'s `Flickable` already had it, `ConnectionsTable`'s `TableView` is not interactive. `AGENT_CONTRACTS.md` "QML" updated (status-bar text, new scrolling rule). Counts against HEAD (physical / nonblank): MachiningPanel 197/195 → 181/179, Main 589/520 → 596/527, QxScrollView 42/31 → 43/32, QxComboBox 102/83 → 103/84, ScenePanel 312/264 → 313/265, PropertiesPanel 405/352 → 406/353, EndEffectorPanel 232/228 → 235/231.
- [x] (2026-11-20) Correction after completion (user screenshot of the FTS Panel): each bar reserved the width of its own range (−1980.000, −660.000, −60.000), so the right-aligned value column ended at three positions. `QxProgressBar` gained `property real widestValue` (default: its own `max(|from|, |to|)`, the previous rule); `FtsBars` sets `widestValue: 1980.0`, the largest magnitude of its six axes, on all rows. Counts: QxProgressBar 82 → 84, FtsBars 47 → 50. Check pending with the plan 05 Milestone 2 check.
- [x] (2026-11-20) Milestone 3 user check, together with both corrections: passed (user report). Earlier hand-off text: `all_qmllint`, `robocrap`, the Milestone 3 checks and the changed Milestone 2 playback check of Validation and Acceptance; result pending.
- [x] (2026-11-20) Final review of changed components and their consumers: `QxButton` (11 uses without `reservedText` unchanged), `QxProgressBar` (only `FtsBars`), `QxTextInput` (one placeholder use), `QxSwitch` (geometry only). `git diff --check` clean. Totals of the eleven files (physical / nonblank): 1761/1530 → 1817/1585; growth from the reserved-size measurements (two `TextMetrics`, one `FontMetrics`), the four reserved-size properties per region, the placeholder margins and comments; the smaller alternatives (per-consumer fixed widths, visibility toggles) are the defects being fixed.


## Surprises & Discoveries

- Observation: plan 02 changed `Network.qml` line 198 (the "RSI test LIN" button's `enabled`) without changing its line count; this plan's `Network.qml` edits (`btnCon` 136-165, status `Label` 203-210) do not overlap it. All start counts still matched on 2026-11-20.
- Observation: the toolbar captions that toggle (Dry Run/Pause, RSI Sampling/Sampling…) already keep their width, because `ModeToolBar.qml` `Tool` has `implicitWidth: Math.max(100, button.implicitWidth)`; `RobotStartPanel.qml`'s status `Text` already has `Layout.preferredWidth: 0` and elides; `ConnectionsTable.qml` has fixed column widths. None needs a change.
- Observation: with the two-line message region of Milestone 2, a long Generate error ("<phase label>: <message> (t=… s)") is cut in the chamfer editor. The full text stays available: SceneMachining prints every Generate error to the console (AGENT_CONTRACTS, "Machining and playback").


## Decision Log

- Decision: `QxButton` gets an optional `reservedText` property. The button's implicit width becomes the larger of the current caption and `reservedText`.
  Rationale: Basic `Button` sizes itself from its content, plus a background `implicitWidth` that QxButton's custom background lacks. A global minimum width would widen every short button (for example "Bias"). Per-consumer `Layout.preferredWidth` values would repeat the measurement in four places. One component input keeps the rule in one place, and AGENT_CONTRACTS ("QML") wants reusable components to take explicit inputs.
  Date/Author: 2026-11-20, plan author.
- Decision: `QxProgressBar` reserves its value label width from the widest value its range can show, `-max(|from|, |to|)` with three decimals, measured with `TextMetrics`.
  Rationale: the value text is `value.toFixed(3)`, and its widest form is the negative number with the largest magnitude.
  Date/Author: 2026-11-20, plan author.
- Decision: the connect hints in DeltaPanel and FtsPanel stay in the layout with constant text. They are hidden with `opacity`, and their wrapping is replaced by a single elided line with `Layout.preferredWidth: 0`.
  Rationale: with constant text and no wrapping, opacity does not change layout size (AGENTS.md warns only about opacity combined with changing wrapped text). `preferredWidth: 0` stops the text from driving the panel width.
  Date/Author: 2026-11-20, plan author.
- Decision: the two MachiningPanel labels keep their text binding but always occupy two caption lines: `maximumLineCount: 2`, elide, and a minimum and preferred height of two `FontMetrics` lines.
  Rationale: a fixed-height region replacing content in place is the pattern AGENTS.md prescribes. A two-line height fits the existing two-line playback text ("Playing · phase\n elapsed / duration s").
  Date/Author: 2026-11-20, plan author.
- Decision: the reserved widths are rounded up: `Math.ceil(reservedMetrics.advanceWidth)` in `QxButton`, `Math.ceil(valueMetrics.advanceWidth)` in `QxProgressBar`.
  Rationale: `TextMetrics.advanceWidth` is fractional and can differ from the shown caption's implicit width by a fraction of a pixel, which would leave a jump of up to 1 px between the two captions. With the rounded value both captions give the same width.
  Date/Author: 2026-11-20, implementing agent.
- Decision: the status-bar busy indicator gets room through a constant `leftPadding: busyIndicator.width + Metrics.sp6` on `statusMessage`; its `x` formula stays unchanged, without the planned `Math.max(0, …)`.
  Rationale: with the clamp alone, an elided message would run under the indicator at x = 0 and its first characters would be covered. With the padding the text never enters that area, `contentWidth` cannot exceed `width - leftPadding`, so `x` cannot become negative; the padding is constant, so nothing moves.
  Date/Author: 2026-11-20, implementing agent.
- Decision (user, 2026-11-20, option 1 of three): the chamfer editor has no playback region; while it is open, the status bar shows the Dry Run status in the existing calibration playback format "Playing · phase · elapsed / duration s". This supersedes the earlier decision for the playback label.
  Rationale: the fixed empty region left a gap at the top of the editor whenever no Dry Run ran. The status bar's message is empty while the editor is open, is a stable region already, and already formats calibration playback; one shared condition (`playbackShown`) serves both. The alternatives were a one-line region (half the gap remains) and a title-row overlay (a new `QxPanel` input).
  Date/Author: 2026-11-20, user and implementing agent.
- Decision (2026-11-20, after the user's second screenshot): the chamfer editor's message also moves to the status bar, as the playback status did (user's option 1); the editor keeps no reserved region. This supersedes the two-line message region and the `captionMetrics` decision below.
  Rationale: the same gap appeared above Generate/Cancel; the status bar shows nothing else while the editor is open, and Calibration already reports the reason Generate is unavailable there. The text comes from one `MachiningPanel.message` property, so the condition stays with the editor.
  Date/Author: 2026-11-20, implementing agent, following the user's choice for the playback region.
- Decision (user, 2026-11-20): scrollable views stop at their bounds. A `ScrollView` exposes its internal `Flickable` only as `contentItem` (type `Item`), so the setting is assigned once at completion through a type assertion; a `ListView` sets the property directly.
  Rationale: Qt's default `Flickable.DragAndOvershootBounds` lets the content spring past its ends on wheel and drag. A shared component for one line in five places would add a file and module registration for less code than it saves.
  Date/Author: 2026-11-20, user and implementing agent.
- Decision (superseded): the MachiningPanel message region is `Math.ceil(2 * captionMetrics.lineSpacing)` high, not `2 * captionMetrics.height`.
  Rationale: `lineSpacing` is `height` plus the font's leading, so two laid-out lines always fit the region; with `height` alone the second line could overhang by the leading. Rounding up gives a whole-pixel height.
  Date/Author: 2026-11-20, implementing agent.
- Decision: `btnCon.onClicked` drops its `if (!runner) return`.
  Rationale: a button receives clicks only while enabled, and `enabled` now returns false whenever `runner` is null; both read the same `root.currentRunner` binding, so the guard can no longer be reached.
  Date/Author: 2026-11-20, implementing agent.
- Decision: the `DeltaPanel` hint uses `opacity: root.enabled ? 0 : 1`.
  Rationale: `DeltaPanel` already sets `enabled: root.plc?.isConnected ?? false`, the same condition the hint repeated; reusing it removes the repetition.
  Date/Author: 2026-11-20, implementing agent.
- Decision: only percentage drafts are rounded: `root.isPercentage(field.key) ? String(Number((value * 100).toFixed(8))) : String(value)`.
  Rationale: the planned expression would also round the other fields to 8 decimals and write the rounded values back on Generate; today they are shown exactly, and only the multiplication by 100 creates the floating-point tail.
  Date/Author: 2026-11-20, implementing agent.


## Outcomes & Retrospective

All six layout defects of the Purpose and the six small defects are fixed and accepted by the user on 2026-11-20. The PLC Panel, FTS Panel and Settings window keep their size when devices connect, values stream or captions toggle (`QxButton.reservedText`, fixed-width `QxProgressBar` values, constant one-line hints, `preferredWidth: 0` on status text). The status bar's message no longer counts toward its width and never runs under the busy indicator. The chamfer editor reserves no empty space: after two user reviews, its Dry Run status and its message moved to the status bar, which shows nothing else while the editor is open. The VFD profile cannot be connected, the logo, placeholder and switch knob are aligned, percentages reopen as entered, and the end-effector panels discard unapplied edits on Close. At the user's request every scrollable view also stops at its ends (`Flickable.StopAtBounds`), now a rule in the "QML" contract.

The QML grew by about 50 nonblank lines over the plan's start, mainly the text measurements for the reserved widths, the shared status-bar conditions and the scroll setting in each view; `MachiningPanel.qml` shrank by 16 lines.

Lesson: a reserved empty region removes a jump but is itself visible; before reserving space, check whether a stable region that is otherwise empty (here the status bar) can carry the transient text.


## Context and Orientation

The application QML is in `src/application/qml/` (module `robocrap_qml_module`). Reusable controls are in `qml/Modules/Components/` (module `Components`), and colours, fonts and sizes in `qml/Modules/Styles/` (module `Styles`: `Colors`, `Fonts`, `Metrics`). Qt Quick Layouts (RowLayout, ColumnLayout) size each child from its implicit size and its `Layout.*` properties. An invisible child takes no space, so toggling `visible` changes the layout's size. A `Text` or `Label` with `Layout.fillWidth: true` still reports its full text width as its preferred width, so its text can widen the layout unless `Layout.preferredWidth` is set, for example to 0.

The tool windows are top-level `Window` items in `Main.qml`. Their sizes are bound to the content:
- **PLC Panel** (lines 218-235): `width: minimumWidth`, `minimumWidth: plcPanel.implicitWidth + 2 * Metrics.sp16`, with the same for height; maximum = minimum.
- **FTS Panel** (241-258): the same binding, with `ftsPanel`.
- **Settings** (260-280): `width: Math.ceil(networkSettings.contentWidth)` and `height: Math.ceil(networkSettings.contentHeight)`. `Network` is a `QxScrollView`, whose `contentWidth` is `page.implicitWidth` (QxScrollView.qml:15, 31), with an inner `ColumnLayout` that has no width of its own.

Every implicit-size change inside these panels therefore resizes the window.

The places to change:
- **Value labels.** `QxProgressBar.qml` lines 64-71: the value `Text` shows `pb.value.toFixed(3)`, and its implicit width enters `implicitWidth: rl.implicitWidth` (17). `FtsBars.qml` creates six bars, with ranges ±1980, ±660 and ±60 N or Nm, from FTS data.
- **Connect hints.** `DeltaPanel.qml` lines 25-32 and `FtsPanel.qml` lines 25-32: a word-wrapped `Text` with `visible: !connected`.
- **QxButton.** `QxButton.qml`: Basic `Button` with custom `background` (no implicit size) and a `contentItem` `Text`. The toggling consumers are `FtsPanel.qml:52` (Stop/Start), `:69` (Recording…/Record), `:77` (Saving…/Save) and `Network.qml:143` (Disconnect/Connect).
- **Network status label.** `Network.qml` lines 202-210: the RSI test status `Label`, with `Layout.fillWidth` and `elide`, but no `preferredWidth`.
- **Network Connect button.** `Network.qml` lines 136-165, button `btnCon`, uses `root.currentRunner`, which is `Backend.Hub.device(<driver>)`. `db.json` lists four profiles. The fourth has driver `"vfd"`, but `main.cpp` registers only `plc`, `fts` and `rsi`, so the runner is null. The `enabled` block then returns true once the fields validate, and `onClicked` returns at once.
- **Chamfer editor labels.** `MachiningPanel.qml`:
  - the playback status `Label` (lines 105-114) has `visible: root.playback.duration > 0` and wraps;
  - the message `Label` (174-183) has `visible: text.length > 0` and wraps; it sits right above the Generate/Cancel row.
- **Percentage drafts.** `MachiningPanel.qml` `beginEditing` (44-55) formats percentage drafts with `String(value * 100)`. In JavaScript `0.07 * 100` is `7.000000000000001`, while the field's validator allows 8 decimals (line 160).
- **Status bar.** `Main.qml` lines 345-389: a `Row` with the AUT EXT LED and text, followed by `statusMessage` (fillWidth, minimumWidth 0, no preferredWidth). Inside it is a busy indicator with `x: statusMessage.width - statusMessage.contentWidth - busyIndicator.width - Metrics.sp6`, which becomes negative when the text is elided.
- **Menu-bar logo.** `MainMenuBar.qml` lines 50-66: the logo `Image` sets both `anchors.top` and `anchors.verticalCenter`, so it stretches to the bar height.
- **Placeholder.** `QxTextInput.qml` lines 42-50: the placeholder `Text` uses `anchors.fill: parent` without the field's padding.
- **Switch knob.** `QxSwitch.qml` lines 44-46: the knob's on-state x is `parent.width - width - Metrics.w1` and its off-state x is `2 * Metrics.w1`.
- **End-effector drafts.** `EndEffectorPanel.qml` reloads its drafts only when the stored pose changes (lines 57-62). `Main.qml` shows the two instances with `visible: root.toolSetupOpen && …` (lines 542-560), so after Close and reopen the old unapplied edits are shown.


## Plan of Work

### Milestone 1: device panels and the Settings window

**`QxButton.qml`.** Add these two properties and one child object:

    property string reservedText: ""
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            Math.max(implicitContentWidth, Math.ceil(reservedMetrics.advanceWidth)) + leftPadding + rightPadding)
    TextMetrics { id: reservedMetrics; font: Fonts.body; text: control.reservedText }

The font must equal the `contentItem` font (`Fonts.body`). With an empty `reservedText`, the button keeps its current width.

**Consumers of `reservedText`.**
- `FtsPanel.qml`:
  - `btnStart`: `reservedText: qsTr("Start")`;
  - `btnLog`: `reservedText: qsTr("Recording…")`;
  - `btnSaveToFile`: `reservedText: qsTr("Saving…")`.
- `Network.qml` `btnCon`: `reservedText: qsTr("Disconnect")`.

In each case `reservedText` is the longer of the two captions in English.

**`QxProgressBar.qml`.** On the value `Text` (64-71), add:
- `horizontalAlignment: Text.AlignRight`;
- `Layout.preferredWidth: Math.ceil(valueMetrics.advanceWidth)`;
- a `TextMetrics { id: valueMetrics; font: Fonts.body; text: (-Math.max(Math.abs(pb.from), Math.abs(pb.to))).toFixed(3) }`.

**`DeltaPanel.qml` and `FtsPanel.qml` hints.** On each hint `Text`, replace `visible: !(…)` with `opacity: (…) ? 0 : 1`, keeping the same condition. Replace `wrapMode: Text.WordWrap` with `elide: Text.ElideRight`, and add `Layout.preferredWidth: 0`.

**`Network.qml` status label.** On the RSI test status `Label`, add `Layout.preferredWidth: 0`. Keep the comment, which is now true.

### Milestone 2: chamfer editor and status bar

**`MachiningPanel.qml`.** Add a `FontMetrics { id: captionMetrics; font: Fonts.caption }`. Then:
- **Playback status `Label` (105-114).** Remove `visible`. Make `text` empty when `root.playback.duration <= 0`. Add `maximumLineCount: 2`, `elide: Text.ElideRight`, `Layout.minimumHeight: 2 * captionMetrics.height` and `Layout.preferredHeight: 2 * captionMetrics.height`.
- **Message `Label` (174-183).** Remove `visible: text.length > 0` and add the same four properties.

`wrapMode` stays `Text.Wrap` on both labels, so the two allowed lines fill the width.

**`Main.qml` `statusMessage`.** Add `Layout.preferredWidth: 0` and `leftPadding: busyIndicator.width + Metrics.sp6`. The busy indicator's `x` formula stays as it is; with the padding it cannot become negative.

### Milestone 3: small defects

- **`Network.qml` `btnCon`.** As the first line of its `enabled` block, add `if (!runner) return false`.
- **`MainMenuBar.qml` logo.** Delete `top: parent.top` from the logo's anchors.
- **`QxTextInput.qml` placeholder.** Replace the placeholder Text's `anchors.fill: parent` with `anchors.fill: parent` plus margins `anchors.leftMargin: control.leftPadding`, `anchors.rightMargin: control.rightPadding`, `anchors.topMargin: control.topPadding` and `anchors.bottomMargin: control.bottomPadding`. Use a grouped `anchors { … }` block.
- **`QxSwitch.qml` knob.** Make the on-state x `parent.width - width - 2 * Metrics.w1`.
- **`MachiningPanel.qml` `beginEditing`.** Format with `root.isPercentage(field.key) ? String(Number((value * 100).toFixed(8))) : String(value)`. Eight decimals matches the validator's `decimals: 8`; `Number(...)` then `String` drops trailing zeros. Other fields stay exact.
- **`EndEffectorPanel.qml` drafts.** Add `onVisibleChanged: if (visible) { root.loadColletDraft(); root.loadTcpDraft() }`.


## Concrete Steps

Work in the repository root. Before and after each milestone:

    wc -l qml/Modules/Components/QxButton.qml qml/Modules/Components/QxProgressBar.qml qml/Modules/Components/QxTextInput.qml qml/Modules/Components/QxSwitch.qml src/application/qml/Views/Dashboard/FtsPanel/FtsPanel.qml src/application/qml/Views/Dashboard/DeltaPanel/DeltaPanel.qml src/application/qml/Views/Network/Network.qml src/application/qml/Views/Workspace/MachiningPanel.qml src/application/qml/Views/Workspace/EndEffectorPanel.qml src/application/qml/MenuBar/MainMenuBar.qml src/application/qml/Main.qml
    git diff --check

The user runs the lint and the build; the agent never does. They use `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, which has `USE_HOT_RELOAD=OFF`; check `CMAKE_HOME_DIRECTORY` first. Targets: `all_qmllint`, because Components and the application module both change, then `robocrap`. Expect no new qmllint warning in the changed files.


## Validation and Acceptance

All checks below are run by the user with the application disconnected unless stated.

**Milestone 1.**
- **PLC Panel and FTS Panel.** Open both. The hint text is visible and the window has some size. Connect the PLC from Settings (user, equipment): the hint disappears and the PLC Panel window keeps exactly its size. Also check by code that the window height binding no longer depends on the connection state, because the hint is always laid out.
- **Toggling buttons.** With the FTS connected (user, equipment), start streaming. The FTS Panel window keeps its width, and Bias, Record and Save do not move. Also check by code that the value labels' preferred width depends only on `from` and `to`.
- **Settings window.** Open Settings and select the RSI profile. Bind the socket (local address and port only), and use "RSI test LIN" if desired; the window width stays constant.

**Milestone 2.**
- **Generate/Cancel.** Open the chamfer editor on a generated path. Clear a field: the status bar shows "Enter valid values in all fields.", and nothing in the editor moves; the "Dry Run: … s" lines sit directly above Generate/Cancel. Restore the value: the status bar is empty again.
- **Playback status.** With the editor open, start Dry Run (the toolbar allows it while the editor is open). The status bar shows "Playing · phase · elapsed / duration s" in the normal colour, and the editor has no empty region at its top. Pause: "Paused · …". Stop: the status bar is empty again. Nothing in the editor moves.
- **Status bar.** Narrow the main window to its minimum width (960 px). In Machining mode with nothing selected, the status label shows a reason text, and with the busy indicator running during Generate it shows "Calculating trajectory…". Compare the left edge of the AUT EXT indicator before and after the change: after it, the indicator does not move when the message changes, and the busy indicator never overlaps it.

**Milestone 3.**
- Select the VFD profile in Settings: Connect is disabled.
- The logo stays 20 × 20 px. To inspect it, use the in-app QML debugging, or check that the logo image is not stretched.
- In an empty field, the "Learned" placeholder in Settings aligns with typed text.
- The switch knob has equal 2 px margins in both states.
- Enter 7 in "Acceleration/deceleration distance, %", Generate, and reopen the editor on the new path: the field shows `7`.
- Open Measuring EE, change X, Close, and reopen: X shows the stored value.


## Idempotence and Recovery

The edits are QML source edits only, and repeating them changes nothing. If a component change misbehaves, revert that one file with git when the user asks.


## Artifacts and Notes

The current Settings-window width chain, for orientation:

    Label (Network.qml:203, fillWidth, no preferredWidth)  ->  rsiTestPanel RowLayout
      ->  NetworkPanel implicitWidth  ->  QxScrollView ColumnLayout (no width)  ->  page.implicitWidth
      ->  contentWidth  ->  settingsWindow.width (Main.qml:268)


## Interfaces and Dependencies

No C++ or module-registration change. New component input: `QxButton.reservedText` (string, default empty). After the change, inspect every `QxButton` consumer (`grep -rn "QxButton" src/application/qml qml`) to confirm that none is affected when `reservedText` is empty. The "QML" contract section needs no change.
