# Application and QML cleanup: remove QmlChartBridge and Qt Charts, shared playback rules, dead QML code

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks. Files are deleted in the working tree only.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`:
   - "Mandatory C++ style": no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters;
   - "Mandatory code reduction";
   - "Mandatory interface stability";
   - the "QML" section of `docs/code context/AGENT_CONTRACTS.md`.

   Before each logical change, re-read the C++ style and code reduction sections. After each milestone, review the changed declarations, bodies, callers and QML consumers, and record start/finish line counts here.

**Prerequisites:**
- `plans/review_2026-11-20_0cd8996/03_UI_LAYOUT_STABILITY_EXECPLAN.md` changes the same QML files (QxButton, QxTextInput, QxSwitch, FtsPanel, DeltaPanel, Network, MachiningPanel, EndEffectorPanel, Main).
- `plans/review_2026-11-20_0cd8996/06_GEOMETRY_TRAJECTORIES_CLEANUP_EXECPLAN.md` adds `ChamferBoundary`, which Milestone 2 uses.
- `plans/review_2026-11-20_0cd8996/02_CALIBRATION_JOB_AND_SETTINGS_SAFETY_EXECPLAN.md` changes `scenemachiningpreparation.cpp`, which Milestone 2 also edits.

Complete all three first. If one is not complete, record that in `Progress` and do only the milestones that do not depend on it.

This plan comes from the whole-project review of commit 0cd8996 (findings in `reviews/2026-11-20_0cd8996/`):
- C++ blocks S1-D2, D3, D4, D5, D6, D7, D8, D9; S2-D3, D4, D5, D7, D9; S2-I4;
- QML blocks S1-D5, D7, D8, D9, D10, D12; S2-D6, D7; S3-D3, D6, D7, D8, D9, D10.

It follows the user's decision of 2026-11-20 to remove QmlChartBridge and the Qt Charts dependency. Everything needed is repeated below.


## Purpose / Big Picture

The application target `robocrap` carries code that does nothing or exists twice.
- **QmlChartBridge.** A whole class, compiled but never created or registered, for which the project links Qt Charts.
- **Duplicated scene code.** Playback rules copied between the machining and calibration coordinators, three copies of an atomic file-write sequence, and a duplicated drawing-style selection.
- **Unused or wrong API.** An unused `DeviceHub::keys` property marked CONSTANT although it changes, and an import "busy" flag that can never be seen.
- **Dead QML.** QML properties and handlers that nothing sets or calls, ids nothing references, and missing `pragma ComponentBehavior: Bound` lines, which are the cause of 21 qmllint "unqualified access" warnings.

After this plan the application behaves the same. Qt Charts is no longer a build dependency, the `robocrap` sources are smaller, and qmllint reports no unqualified access in the changed files. To see it working:
1. Configure and build in the existing build directory, which re-runs CMake because `find_package` changed.
2. Start the application disconnected, then exercise the panels as listed under Validation and Acceptance.
3. Run `robocrap_qmllint` or `all_qmllint`.


## Progress

- [x] (2026-11-20) Plan written after reading:
  - `src/application/qmlchartbridge.h`, `src/application/main.cpp` and `src/application/CMakeLists.txt`, plus the root `CMakeLists.txt`;
  - `src/application/devices/devicehub.{h,cpp}` and `src/application/devices/deviceprofilemodel.cpp` (roleNames, load);
  - `src/application/scene/scenemachining.cpp` (canPlay, dryRun, unavailableReason, onPlaybackFailed);
  - `src/application/scene/scenecalibration.cpp` (canPlay, dryRun, unavailableReason);
  - `src/application/scene/sceneplayback.{h,cpp}`;
  - `src/application/scene/sceneobject.cpp` (drawing);
  - `src/application/scene/scenemachiningpreparation.cpp` (replaceJobFile, writeCalibrationResult, writeCalibrationTouches);
  - `src/application/scene/scenesurfaceimporter.{h,cpp}` (busy) and `src/application/robot/robotcontroller.cpp` (initialize);
  - the QML files named in Context and Orientation.
- [ ] Start counts (command in Concrete Steps).
- [ ] Milestone 1: QmlChartBridge and Qt Charts removed; small C++ items in devices, main and robot.
- [ ] Milestone 2: shared playback rules in ScenePlayback; one save helper; one drawing-style helper; import busy property removed; `pathById` returns const.
- [ ] Milestone 3: dead QML code and ids removed; `pragma ComponentBehavior: Bound` added; PlainText on dynamic labels; FINAL on C++ properties.
- [ ] Configure, build, `all_qmllint`, disconnected session (user's).
- [ ] Contract/ARCHITECTURE updates, final compliance pass, line counts, `git diff --check`.


## Surprises & Discoveries

(None yet.)


## Decision Log

- Decision (user, 2026-11-20): delete QmlChartBridge and remove `Charts` from `find_package` and `Qt6::Charts` from `robocrap`'s links.
  Rationale: nothing creates or registers the class; its only mentions are commented lines in main.cpp naming a class that no longer exists (`SocketFTS`). The retained FTS signals `dataSampleHFReady`, `dataBatchReady` and `streamReset` stay; ARCHITECTURE §7 keeps them for future force control.
  Date/Author: 2026-11-20, user.
- Decision: the shared playback rules move into ScenePlayback as `bool canPlayFor(quint32 ownerId, bool hasPath) const`, `QString playFor(quint32 ownerId, std::shared_ptr<const TimedMotion> motion)` and `QString busyReason() const`. Each coordinator keeps its own path lookup and its own error texts.
  Rationale: ScenePlayback owns the state the rules read (`active`, `playing`, `ownerId`, `robotAvailable`, `robotModel`). Both coordinators currently re-derive the rules line for line.
  Date/Author: 2026-11-20, plan author.
- Decision: `SceneSurfaceImporter::busy` (property, getter, signal) is removed; `m_busy` stays as a private re-entrancy guard.
  Rationale: the import runs synchronously on the GUI thread, setting and clearing the flag inside one call (scenesurfaceimporter.cpp 79-97), so QML can never observe it as true. The guard at line 72 still matters if a signal handler re-enters `load`.
  Date/Author: 2026-11-20, plan author.
- Decision: `WorkspaceController::selectObject(quint32, bool additive)` stays unchanged.
  Rationale: replacing the boolean with an enum or two invokables changes the QML API in several call sites (Main.qml 82, 115 and the viewport pick path) for no behaviour gain. AGENTS.md preserves QML APIs unless a task requires a change.
  Date/Author: 2026-11-20, plan author.


## Outcomes & Retrospective

(To be written at completion.)


## Context and Orientation

**QmlChartBridge and the CMake files.**
- `src/application/qmlchartbridge.{h,cpp}` (42 and 76 lines) are listed in `src/application/CMakeLists.txt` lines 35-36.
- The root `CMakeLists.txt` finds `Qt6 … Charts` (line 15), and `src/application/CMakeLists.txt` links `Qt6::Charts`.
- `main.cpp` lines 78-80 are commented code about the bridge.
- `docs/code context/AGENT_CONTRACTS.md`, "Build and modules", says "QmlChartBridge stays in Application with its QtCharts requirement".

**Devices.**
- `src/application/devices/devicehub.h` declares `Q_PROPERTY(QStringList keys READ keys CONSTANT)` (line 21) and `keys()` (37), defined in `devicehub.cpp` 49-52. No QML or C++ reads them.
- The class comment (lines 12-17) still says devices live in "the shared I/O thread", but there are two threads, ControlIO and GeneralIO.
- `deviceprofilemodel.cpp`:
  - `roleNames()` (92-97) rebuilds its hash on every call;
  - line 169 reads `peerPort` with `isNull() ? -1 : toInt(-1)`, equivalent to `toInt(-1)`, which line 167 uses for `localPort`.

**`main.cpp`.** Lines 51-52 ignore the results of two `QFontDatabase::addApplicationFont` calls, while 54-57 check the third.

**`robotcontroller.cpp`.** `initialize` returns a `const auto home` local (139-140, 157), which forces a copy.

**`scenegeometry.h`.** `CalibrationTable::operator==` (115) and `SceneSphere::operator==` (137) are member operators. Their only use is `scenecalibration.cpp` 379.

**Playback rules.**
- `SceneMachining::canPlay` (306-312) and `SceneCalibration::canPlay` (359-364) have the same structure. So do `dryRun` (scenemachining.cpp 314-322, scenecalibration.cpp 366-374), which builds an aliasing `std::shared_ptr<const TimedMotion>(motion, &motion->timed())`, and the first three checks of each `unavailableReason` (scenemachining.cpp 134-136, scenecalibration.cpp 400-402).
- `ScenePlayback` (`sceneplayback.{h,cpp}`) owns `active()`, `playing()`, `ownerId()`, `robotAvailable()`, `robotModel()`, `start` and `resume`.

**File writers.** `scenemachiningpreparation.cpp` writes files three times with the same sequence: mkpath, `QSaveFile` open, size-checked write, commit, error text.
- `replaceJobFile` (55-70) also calls `setDirectWriteFallback(false)`, the Qt default, and `cancelWriting()`, which the QSaveFile destructor already does.
- `writeCalibrationResult` (156-180) and `writeCalibrationTouches` (183-198) are the other two.

**`SceneObject::drawing`.** `sceneobject.cpp` 172-192 picks one of three colours (green {30,130,45}, purple {90,65,130}, blue {0,100,210}) and a width of 3.0 or 2.0 in two branches. The machining-path branch selects by bare phase indices 3, 0 and 6, and both branches copy points with `points().mid(...)` only to read positions in `tcpPolyline`.

**`pathById`.** `SceneMachiningPreparation::pathById` (cpp 229-233) is const but returns a mutable `SceneMachiningPath*`, and all three callers only read.

**`SceneSurfaceImporter::busy`.** It is read in `Main.qml` 168 (`geometryImportAvailable`), 301 ("Importing surface…") and 374 (status colour).

**QML dead code.**
- `qml/Modules/Components/QxTextInput.qml`: `confirmed` (10) is never set to true by any consumer; it is only reset (39) and read by the border colour (33).
- `qml/Modules/Components/QxSwitch.qml`:
  - `displayonly` (15) duplicates `displayOnly` and is unused;
  - the `contentItem` Text (around 25) is never used, because no consumer sets `text` (DeltaModuleOutput.qml 36, MachiningPanel.qml 132).
- `qml/Modules/Components/QxBusyIndicator.qml`: its Repeater delegate reaches the outer ids `root` and `ring` without `pragma ComponentBehavior: Bound`, which causes qmllint's unqualified-access rows at lines 52-65.
- `src/application/qml/Views/Workspace/EndEffectorPanel.qml` and `PropertiesPanel.qml`: the same missing pragma (EndEffectorPanel 94-95, 116, 126, 130, 133, 179, 183, 186; PropertiesPanel 83, 379, 383-390). Their delegates already declare `required property`.
- `EndEffectorPanel.qml`: the read-only ER collet fields keep a dead edit path:
  - `editColletValue` (47-51) and the `onTextEdited`/`onEditingFinished` handlers of the collet fields (130-134) never run, because the field has `enabled: false`;
  - the collet opacity expression (118) is constant;
  - `validDraft` (24-25) validates `colletDrafts`, which Apply never sends; it sends `root.colletPose` (222).
- `src/application/qml/Views/Workspace/ModeToolBar.qml`: the inline component `Tool` exports the aliases `checkable` (61) and `checked` (62) and the signal `toggled` (66, emitted at 105). No instance uses them, so the `checked` branches of its colours (77-78, 95-100) are constant.
- `src/application/qml/Views/Workspace/WorkflowPanel.qml`: `selectedColor`/`selectedBorderColor` (26-27) are never assigned.
- Ids nothing references:
  - FtsPanel.qml: `cl`, `bars`, `rl`, `btnStart`, `btnBias`, `btnLog`, `btnSaveToFile`. Keep any id that the UI-stability plan started using.
  - DeltaPanel.qml: `rl`, `ledPanel`, `moduleAP_P`.
  - DeltaModuleInput.qml: `led`.
  - MainMenuBar.qml: `logoImage`.
  - Network.qml: `configPanel`, `btnCon`, `conPanel`, `conTable`.
- Labels rebuilt often or showing user-named text without `textFormat: Text.PlainText`:
  - `QxProgressBar.qml` value text (64);
  - `RobotStartPanel.qml` status (76);
  - `MachiningPanel.qml` playback label (108) and input-name label (118);
  - `EndEffectorPanel.qml` measuring name (94).
- `ScenePanel.qml` 262: a full-size `MouseArea` takes clicks from its `ItemDelegate`; a `TapHandler` fits.

**C++ properties.** No `Q_PROPERTY` in `src/application` is marked `FINAL`.


## Plan of Work

### Milestone 1: QmlChartBridge, Qt Charts and small C++ items

1. **Remove QmlChartBridge.**
   - Delete `src/application/qmlchartbridge.h` and `.cpp`, and their two lines in `src/application/CMakeLists.txt`.
   - Delete `Qt6::Charts` from `robocrap`'s `target_link_libraries` and `Charts` from the root `find_package(Qt6 6.8 REQUIRED COMPONENTS …)`.
   - Delete `main.cpp` lines 78-80.
   - Check with `grep -rn "Charts\|ChartBridge" src CMakeLists.txt cmake qml`; only documents may still mention them.
2. **`DeviceHub` cleanup.** Delete `DeviceHub`'s `keys` property, declaration and definition. Fix the class comment to "AbstractDevice objects live in their group's I/O thread (ControlIO or GeneralIO) and are deleted there".
3. **`DeviceProfileModel` cleanup.**
   - Add the private member `QHash<int, QByteArray> m_roleNames;`. The constructor fills it once from `QAbstractTableModel::roleNames()` plus `DriverRole → "driver"`, and `roleNames()` returns it. The base roles do not change after construction, and a member avoids a static that would depend on the first instance. Requirement for the member: `roleNames()` must return the same hash without rebuilding it on every call.
   - Line 169 becomes `obj.value("peerPort").toInt(-1)`.
4. **`main.cpp` fonts.** Load the three fonts in a range-for over a constant array of the three resource paths, printing `qWarning() << "Failed to load font from resources:" << path;` for each that returns -1.
5. **`robotcontroller.cpp`.** Drop `const` on `home` in `initialize`.
6. **`scenegeometry.h`.** Make both `operator==` hidden friends (`friend bool operator==(const T&, const T&)`), with the bodies unchanged.

### Milestone 2: scene coordinators and writers

**ScenePlayback.** Add three public members:
- `bool canPlayFor(quint32 ownerId, bool hasPath) const`, with this body:

      if (m_playing || !m_robotAvailable) return false;
      if (active()) return m_ownerId == ownerId && ownerId != 0;
      return hasPath;

  The coordinators keep their own "calculating" test before calling it.
- `QString playFor(quint32 ownerId, std::shared_ptr<const TimedMotion> motion)`: resumes if active, otherwise starts, and returns an empty string. The coordinator keeps its own `canPlay` refusal text.
- `QString busyReason() const`, returning:
  - `tr("Stop Dry Run before generating a trajectory.")` when active;
  - `tr("Robot preview is not ready.")` when there is no model or no availability;
  - otherwise empty.

Use them in `SceneMachining::canPlay`/`dryRun`/`unavailableReason` and in the calibration counterparts, deleting the copied lines. `SceneMachining::ownsPlayback` and `SceneCalibration::ownsPlayback` stay; they look up their own paths. The aliasing handle is built by the coordinator, which owns the motion.

**One save helper.** In `scenemachiningpreparation.cpp`'s anonymous namespace, add `QString saveFile(const QString& folder, const QString& fileName, const QByteArray& contents)`. It runs mkpath, QSaveFile write and commit, and returns the error text:
- `tr("Cannot create %1.")` when the folder cannot be created;
- `tr("Cannot save %1: %2")` with the full path when writing or committing fails.

`replaceJobFile`, `writeCalibrationResult` and `writeCalibrationTouches` use it. Their error texts must keep their meaning; the job file's "Cannot write"/"Cannot replace" distinction becomes "Cannot save", and the AGENT_CONTRACTS text does not quote these strings, so no contract changes. Delete `setDirectWriteFallback(false)` and `cancelWriting()`.

**`SceneObject::drawing`.**
- Add a file-local function `RoboCrap3D::DrawingStyle pathStyle(PathStroke stroke)`, with a small file-local enum `PathStroke { Working, Ptp, Transfer, }` mapping to the three colours and widths. Use it in both branches.
- Select the machining branch's stroke with `ChamferBoundary` names, not bare indices (from the geometry plan).
- Change `tcpPolyline` to take the point vector plus first and last indices, so it no longer needs `mid` copies.

**`SceneMachiningPreparation::pathById`.** Return `const SceneMachiningPath*` via `qobject_cast<const SceneMachiningPath*>`.

**`SceneSurfaceImporter::busy`.** Remove the `busy` property, getter and `busyChanged` signal, keeping `m_busy` as the private guard. In `Main.qml`:
- line 168 becomes `!planeFileDialog.visible`;
- delete the "Importing surface…" branch (301);
- delete the `!surfaceImporter.busy` term (374).

### Milestone 3: QML dead code, pragmas, text formats, FINAL

- **`QxTextInput.qml`.** Delete `confirmed`, its reset in `onFocusChanged` and the green branch of the border colour. Keep `selectAll()` on focus.
- **`QxSwitch.qml`.** Delete the `displayonly` alias and the unused text `contentItem`; check the Basic `Switch` still reports a sane implicit size without it, since `implicitWidth` is already the indicator width.
- **Pragma.** Add `pragma ComponentBehavior: Bound` at the top of `QxBusyIndicator.qml`, `EndEffectorPanel.qml` and `PropertiesPanel.qml`. Where a delegate then fails to resolve a context property, declare it as a `required property` or qualify it through an id.
- **`EndEffectorPanel.qml`.** Delete `editColletValue`, the collet handlers and `colletDrafts`; show the collet values from `root.poseDraft(root.colletPose)` directly. Make the collet opacity constant, and make `validDraft` check only `root.drafts`.
- **`ModeToolBar.qml`.** Delete `checkable`, `checked` and `toggled` from `Tool`, its emit, and the `checked` colour branches.
- **`WorkflowPanel.qml`.** Make `selectedColor` and `selectedBorderColor` `readonly`.
- **Ids.** Delete the unused ids listed above. Check each with `grep -n "\b<id>\b" <file>` before deleting.
- **Text formats.** Add `textFormat: Text.PlainText` to the labels listed above.
- **`ScenePanel.qml`.** Replace the `MouseArea` (around line 262) with this `TapHandler`. Its signals and properties were checked in the Qt 6.8 TapHandler documentation:

      TapHandler {
        acceptedButtons: Qt.LeftButton
        enabled: !objectRow.editing
        // Exclusive grab on press, as the MouseArea had, so the ItemDelegate's onClicked
        // (non-additive selection) does not also run for a mouse click.
        gesturePolicy: TapHandler.WithinBounds
        onSingleTapped: {
          objectButton.forceActiveFocus()
          root.selectionRequested(objectRow.modelData.objectId, (point.modifiers & Qt.ControlModifier) !== 0)
        }
        onDoubleTapped: objectRow.beginRename()
      }

  The handlers behave as follows:
  - **`singleTapped`** is emitted on the first tap only. With the default `exclusiveSignals` (NotExclusive), a second tap within the double-click interval emits `doubleTapped` instead. This matches the MouseArea's clicked/doubleClicked pair, so a double click does not toggle an additive selection twice.
  - **`point`** is the handler's current `handlerPoint`, whose `modifiers` the documentation's own example reads in `onTapped`.

  Keep the ItemDelegate's own `onClicked`: keyboard activation still uses it.
- **FINAL.** Add `FINAL` to every `Q_PROPERTY` in `src/application/**/*.h` (sceneendeffectors.h, scenecalibration.h, devicerunner.h, deviceprofilemodel.h, devicehub.h, scenegeometry.h, sceneobject.h, scenemodel.h, sceneplayback.h, scenemachining.h, scenemachiningpreparation.h, scenesurfaceimporter.h, workspacecontroller.h). No class is subclassed in the project; check with `grep -rn ": public Scene\|: public Device\|: public Workspace" src`.


## Concrete Steps

Work in the repository root. Before and after each milestone:

    wc -l src/application/*.cpp src/application/*.h src/application/devices/* src/application/scene/* src/application/robot/* src/application/qml/*.qml src/application/qml/*/*.qml src/application/qml/*/*/*.qml src/application/qml/*/*/*/*.qml qml/Modules/Components/*.qml CMakeLists.txt src/application/CMakeLists.txt
    git diff --check
    git status --short

The status shows the two deleted QmlChartBridge files as ` D`. The user builds; the agent never does. Because `find_package` changes, the user re-runs CMake configure in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (Qt Creator does it on build), then builds `robocrap` and runs `all_qmllint`. Expect no warning in the changed QML files, and in particular none of the former 21 `QL:unqualified` rows.


## Validation and Acceptance

All checks below are run by the user, application disconnected, after the build.
- **Start-up.** The application starts and the robot appears. The console shows no font warning, if the fonts are present.
- **Settings.** Open Settings: the connections table shows the four profiles with their driver column, which uses DeviceProfileModel's roles.
- **Surface import.** Import a plane from `data/probe_points/edge_cyl_plane/`. Import works, the toolbar import buttons stay enabled afterwards, and no "Importing surface…" text is expected.
- **Machining playback.** Generate a chamfer path, then Dry Run, Pause, resume and Stop. The text "Stop Dry Run before generating a trajectory." appears in the editor while Dry Run is active, as before.
- **Calibration playback.** Do the same in Calibration with the calibration path.
- **Paths in the 3D view.** A machining path shows a green machining turn, purple PTPs and blue LINs as before. A calibration path shows green approaches, purple PTPs and blue transfers.
- **End-effector panels.** Measuring EE and Spindle EE show the same values, and Apply works. The ER fields of Spindle EE stay read-only.
- **Scene panel.** Click selects, Ctrl+click adds to the selection, and a double click starts renaming.
- **Toolbar.** Tool colours are unchanged.
- **Calibration result files.** Run one calibration session on the equipment (user, `StepLimit` = 10 is enough) and confirm that `records/<id>/calibration_touches_<id>.csv` and, after a full run, `calibration_result_<id>.csv` are written as before. Also check by code that `writeCalibrationResult` and `writeCalibrationTouches` produce the same contents through `saveFile`.


## Idempotence and Recovery

The edits are source edits and repeating them changes nothing. The two deleted files can be restored from git, at the user's request. If removing `Charts` breaks the configure because another target needs it, which the code search says none does, restore that one `find_package` component and record it in Surprises.


## Artifacts and Notes

The duplicated playback rule, for orientation:

    bool SceneMachining::canPlay() const                     bool SceneCalibration::canPlay() const
    { if (m_calculating || m_playback->playing()            { if (calculating() || m_playback->playing()
          || !m_playback->robotAvailable()) return false;          || !m_playback->robotAvailable()) return false;
      if (m_playback->active()) return ownsPlayback();        if (m_playback->active()) return ownsPlayback();
      … compatible path … }                                    return path() != nullptr; }


## Interfaces and Dependencies

`Qt6::Charts` is no longer required (root `find_package` and `robocrap` links). Removed C++/QML API:
- `QmlChartBridge`;
- `DeviceHub::keys`;
- `SceneSurfaceImporter::busy`/`busyChanged`;
- `QxTextInput.confirmed`, `QxSwitch.displayonly`, and `ModeToolBar.Tool`'s `checkable`/`checked`/`toggled`.

New:
- `ScenePlayback::canPlayFor`, `playFor` and `busyReason`;
- the file-local `saveFile`, `pathStyle` and `PathStroke`.

Documents to update:
- `docs/code context/AGENT_CONTRACTS.md`, "Build and modules": delete "QmlChartBridge stays in Application with its QtCharts requirement;" and remove `Charts` wherever the Qt dependency list is named.
- `docs/code context/ARCHITECTURE.md`, section 3: replace "`busy` is set before reading and cleared before returning, so painting waits for the call" with "the import blocks the GUI thread for the duration of the call; a private flag rejects re-entrant imports".
- ARCHITECTURE §7 "Retained interfaces": delete the sentence that the `devicehub.h` class comment still mentions one shared I/O thread.
