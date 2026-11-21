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
- [x] (2026-11-21) Prerequisites: plans 02 and 03 complete; plan 06 implemented in code (`ChamferBoundary` exists), its Milestone 5 equipment sessions and final pass still open. Milestone 1 depends on none of them; Milestone 2 uses plan 06's `ChamferBoundary` and plan 02's `scenemachiningpreparation.cpp`. Working tree clean at 34d4b81; `CMAKE_HOME_DIRECTORY` of `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` is this repository.
- [x] (2026-11-21) Start counts: Concrete Steps set 9091 lines. Milestone 1 files (physical / nonblank): qmlchartbridge.h 42/33, qmlchartbridge.cpp 76/65, src/application/CMakeLists.txt 132/127, CMakeLists.txt 80/64, main.cpp 172/130, devicehub.h 53/41, devicehub.cpp 79/59, deviceprofilemodel.cpp 177/148, robotcontroller.cpp 198/172, scenegeometry.h 149/138.
- [x] (2026-11-21) Milestone 1 implemented:
  - `qmlchartbridge.{h,cpp}` deleted in the working tree; their two source entries, `Qt6::Charts` and the root `Charts` component removed; `main.cpp`'s three commented bridge lines and the following blank line removed;
  - `DeviceHub`: `keys` property, declaration, definition and the then unused `<QStringList>` include removed; class comment names ControlIO and GeneralIO;
  - `DeviceProfileModel::load` reads `peerPort` with `toInt(-1)` alone; the `roleNames()` cache was not added (Decision Log);
  - `main.cpp` loads the three fonts in a range-for over a braced list of the resource paths and warns with the failing path;
  - `RobotController::initialize`: `home` is no longer `const`, so both `return home;` move;
  - `CalibrationTable` and `SceneSphere` `operator==` are hidden friends with operands `a` and `b`; the compared fields are unchanged; the only caller (`SceneCalibration::replaceSphere`, `*existing == sphere`) finds them by argument-dependent lookup;
  - AGENT_CONTRACTS "Build and modules" no longer names QmlChartBridge/QtCharts; ARCHITECTURE §7 no longer says the `devicehub.h` comment names one shared I/O thread.
  Search: `git grep -n -E "Charts|ChartBridge" -- src CMakeLists.txt cmake qml` finds nothing.
  Compliance pass over the changed declarations, bodies and callers: no lambdas, templates, thrown exceptions, output parameters or new helpers/fields; both operators take two parameters; `addApplicationFont` results are all checked. No violations found.
  Counts (physical / nonblank): qmlchartbridge.h 42/33 and qmlchartbridge.cpp 76/65 deleted, src/application/CMakeLists.txt 132/127 → 129/124, CMakeLists.txt 80/64 → 79/63, main.cpp 172/130 → 166/126, devicehub.h 53/41 → 50/39, devicehub.cpp 79/59 → 74/55, deviceprofilemodel.cpp 177/148 → 175/146, robotcontroller.cpp 198/172 → 198/172, scenegeometry.h 149/138 → 149/138; Concrete Steps set 9091 → 8953 (−138). `git diff --check` clean.
- [x] (2026-11-21) Milestone 1 handed to the user: re-run CMake configure and build `robocrap` in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, then the disconnected Start-up, Settings and Calibration checks of Validation and Acceptance (Milestone 1).
- [x] (2026-11-21) Milestone 1 user check: configure, `robocrap` build, Start-up, Settings and Calibration entry passed (user report).
- [x] (2026-11-21) Milestone 2 start counts (physical / nonblank): sceneplayback.h 76/69, scenemachining.h 73/68, scenemachining.cpp 327/301, scenecalibration.cpp 559/509, scenemachiningpreparation.h 137/125, scenemachiningpreparation.cpp 564/517, sceneobject.cpp 269/248, scenesurfaceimporter.h 30/24, scenesurfaceimporter.cpp 106/91, Main.qml 596/527; Concrete Steps set 8953.
- [x] (2026-11-21) Milestone 2 implemented (deviations from the first plan text in the Decision Log, 2026-11-21):
  - `ScenePlayback::canPlay(bool ownsActive, bool hasPath)` holds the Dry Run rule; `SceneMachining::canPlay` and `SceneCalibration::canPlay` keep only their "calculating" test, ownership and path lookup; `dryRun` and `unavailableReason` unchanged;
  - `saveFile(path, contents)` replaces `replaceJobFile` and the two record writers' own mkpath/QSaveFile sequences; `SharedDataDirectory`, `setDirectWriteFallback(false)` and `cancelWriting()` removed; job-file errors now read "Cannot save C:/RoboCrapJobs/<file>: <reason>" instead of "Cannot write/replace <file>: <reason>";
  - `SceneObject::drawing`: the constants `WorkingStroke`, `PtpStroke`, `TransferStroke` give both path branches their colour and width; `tcpPolyline(points, first, last)` reads the range without `mid` copies;
  - `SceneMachiningPreparation::pathById` removed; its three callers use `SceneMachining::machiningPath`, now public, which returns `const SceneMachiningPath*`;
  - `SceneSurfaceImporter::busy`/`busyChanged` removed, `m_busy` kept as the re-entrancy guard; `Main.qml` lines 168, 304 and 382 updated; ARCHITECTURE §3 sentence replaced.
  Searches: no `busyChanged`, `surfaceImporter.busy`, `pathById`, `replaceJobFile`, `SharedDataDirectory` or `points().mid(` in `src`.
  Compliance pass over the changed declarations, bodies and callers: no lambdas, templates, thrown exceptions or output parameters; `canPlay` and `saveFile` take two parameters, `tcpPolyline` three; `canPlay` now evaluates `ownsPlayback()` and the path lookup before the playback test, both without side effects; `SceneMachining::machiningPath` is an added public C++ method (not QML). New items and why reuse fails: `ScenePlayback::canPlay` is required because the rule reads `m_playing`, `m_robotAvailable` and `active()`, which ScenePlayback owns, and both coordinators repeated it; `saveFile` replaces three copies of one sequence; the stroke constants replace two copies of the colour scheme. No violations found.
  Counts (physical / nonblank): sceneplayback.h 76/69 → 79/72, scenemachining.h 73/68 → 74/69, scenemachining.cpp 327/301 → 325/299, scenecalibration.cpp 559/509 → 557/507, scenemachiningpreparation.h 137/125 → 136/124, scenemachiningpreparation.cpp 564/517 → 542/496, sceneobject.cpp 269/248 → 269/247, scenesurfaceimporter.h 30/24 → 27/21, scenesurfaceimporter.cpp 106/91 → 104/89, Main.qml 596/527 → 595/526; Concrete Steps set 8953 → 8924 (−29: playback rule −1, `saveFile` −15, `pathById` −7, drawing 0, `busy` −6). `git diff --check` clean.
- [x] (2026-11-21) Milestone 2 handed to the user: build `robocrap`, then the disconnected Surface import, Machining playback, Calibration playback and Paths in the 3D view checks; Calibration result files with the next equipment session.
- [x] (2026-11-21) Milestone 2 user check: `robocrap` build and the disconnected Milestone 2 checks passed (user report); the calibration result files follow with the next equipment session.
- [x] (2026-11-21) Milestone 3 start counts (physical / nonblank): QxTextInput 58/48, QxSwitch 63/53, QxBusyIndicator 70/56, QxProgressBar 84/71, EndEffectorPanel 235/231, PropertiesPanel 406/353, ModeToolBar 250/213, WorkflowPanel 140/118, FtsPanel 87/67, DeltaPanel 103/80, DeltaModuleInput 48/37, MainMenuBar 129/101, Network 215/169, RobotStartPanel 94/75, MachiningPanel 181/179, ScenePanel 313/265; 85 `Q_PROPERTY` lines in `src/application` headers, none FINAL; Concrete Steps set 8924.
- [x] (2026-11-21) Milestone 3 implemented (deviations in the Decision Log, 2026-11-21):
  - `QxTextInput`: `confirmed`, its reset and the green border removed; `onFocusChanged: if (focus) selectAll()`;
  - `QxSwitch`: `displayonly`, the text `contentItem` and the then unused `spacing` removed; `implicitWidth`/`implicitHeight` stay the indicator size, so the Basic `Switch`'s default content item (empty text) changes no size;
  - `pragma ComponentBehavior: Bound` in `QxBusyIndicator`, `EndEffectorPanel` and `PropertiesPanel`; their delegates already declared `required property index`/`modelData`, and no outer id clashes with a name looked up unqualified inside a delegate or inline component;
  - `EndEffectorPanel`: `colletDrafts`, `loadColletDraft`, `editColletValue`, the collet handlers, id and validator removed; the collet fields show `root.poseDraft(root.colletPose)[index]` with constant `opacity: Colors.overlays.disabled`; `validDraft` checks `root.drafts` inline (the then single-use `validPoseDraft` removed);
  - `ModeToolBar` `Tool`: `checkable`, `checked`, `toggled` and its emit removed; the colours without the `checked` branches;
  - `WorkflowPanel` `ModeButton`: `selectedColor`/`selectedBorderColor` replaced by `Colors.secondary.base`/`Colors.secondary.dark` at their one use each;
  - unused ids removed: FtsPanel `cl`, `bars`, `rl`, `btnStart`, `btnBias`, `btnLog`, `btnSaveToFile`; DeltaPanel `rl`, `ledPanel`, `moduleAP_P`; DeltaModuleInput `led`; MainMenuBar `logoImage`; Network `configPanel`, `btnCon`, `conPanel`, `conTable` (each found once, at its declaration);
  - `textFormat: Text.PlainText` on the QxProgressBar value, the RobotStartPanel status, the MachiningPanel input name and the EndEffectorPanel measuring name;
  - `ScenePanel`: the row `MouseArea` replaced by `TapHandler { id: rowTap; gesturePolicy: WithinBounds }` with `onSingleTapped` (Ctrl through `rowTap.point.modifiers`) and `onDoubleTapped`; the ItemDelegate's `onClicked` stays for the keyboard (reverted after the user check, below);
  - `FINAL` on all 85 `Q_PROPERTY` declarations of `src/application` (devicehub.h and scenesurfaceimporter.h have none left after Milestones 1 and 2); no C++ class derives from them and no QML instance redeclares one of their properties.
  Searches: no `confirmed`, `displayonly`, `onToggled`, `selectedColor` or `selectedBorderColor` in `qml` or `src/application/qml`.
  Compliance pass over the changed components and their consumers: QxSwitch consumers (DeltaModuleOutput, MachiningPanel) set no `text`, `displayonly` or `spacing`; QxTextInput consumers set no `confirmed`; no `Tool` instance used the removed members; no new helpers; C++ only gained `FINAL`. No violations found.
  Counts (physical / nonblank): QxTextInput 58/48 → 54/44, QxSwitch 63/53 → 54/45, QxBusyIndicator 70/56 → 71/57, QxProgressBar 84/71 → 85/72, EndEffectorPanel 235/231 → 213/209, PropertiesPanel 406/353 → 407/354, ModeToolBar 250/213 → 241/204, WorkflowPanel 140/118 → 138/116, FtsPanel 87/67 → 73/60, DeltaPanel 103/80 → 97/77, DeltaModuleInput 48/37 → 46/36, MainMenuBar 129/101 → 127/100, Network 215/169 → 207/165, RobotStartPanel 94/75 → 95/76, MachiningPanel 181/179 → 182/180, ScenePanel 313/265 → 315/267; C++ headers unchanged in length; Concrete Steps set 8924 → 8853 (−71). `git diff --check` clean.
- [x] (2026-11-21) Milestone 3 handed to the user: build `robocrap` and run `all_qmllint` in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (USE_HOT_RELOAD OFF, FAST_QML_BUILD ON), then the disconnected End-effector panels, Scene panel, Toolbar, Start-up and Settings checks.
- [x] (2026-11-21) Milestone 3 user check: in the Scene panel Ctrl+click and F2 failed; Ctrl+click in the 3D view worked (user report). Fixed by restoring the row `MouseArea` (Surprises, Decision Log); `ScenePanel.qml` is again identical to HEAD (313 lines); Concrete Steps set 8853 → 8851.
- [x] (2026-11-21) Milestone 3 re-check after the fix: `robocrap` build, `all_qmllint`, the Scene panel check and the other Milestone 3 checks passed (user report).
- [x] (2026-11-21) Configure, build, `all_qmllint`, disconnected session (user's), after every milestone.
- [x] (2026-11-21) Contract/ARCHITECTURE updates, final compliance pass, line counts, `git diff --check`:
  - documents: AGENT_CONTRACTS "Build and modules" (Milestone 1), ARCHITECTURE §3 and §7 (Milestones 2 and 1); a search of `docs/code context` finds no other sentence about the removed or changed items;
  - compliance pass over the whole diff (45 files): no added lambda, template, `throw`, `try`/`catch` or output parameter in C++; the added or changed functions (`ScenePlayback::canPlay`, `saveFile`, `tcpPolyline`, the two `operator==`) take two or three parameters and return their results; the QML diff adds no new JavaScript function (the one arrow function is the moved `validDraft` check). No violations found;
  - counts (`git diff --numstat`): C++ +145/−307 (−162, of which −118 the two deleted QmlChartBridge files), QML +25/−99 (−74), CMake +0/−4, documents +5/−5, this plan +118/−64; Concrete Steps set 9091 → 8851 (−240: Milestone 1 −138, Milestone 2 −29, Milestone 3 −73);
  - `git diff --check` clean; no untracked files; nothing staged or committed.
- [ ] Calibration result files (`calibration_touches_<id>.csv`, `calibration_result_<id>.csv`) through `saveFile`, at the user's next equipment session (Validation and Acceptance).


## Surprises & Discoveries

- Observation (2026-11-21): plans 03 and 06 moved some lines this plan cites; the items are named by function or content and still hold. `robotcontroller.cpp` `home` is at 137, 138 and 155; the `operator==` definitions at `scenegeometry.h` 114 and 136, their caller at `scenecalibration.cpp` 380; `surfaceImporter.busy` at `Main.qml` 168, 304 and 382.
- Observation (2026-11-21): plan 03 removed the playback status label of `MachiningPanel.qml` (the Dry Run text moved to the status bar), so Milestone 3's PlainText item for that label has nothing to change. The `Network.qml` ids `configPanel`, `btnCon`, `conPanel` and `conTable` are still declared and unreferenced.
- Observation (2026-11-21): plan 06 already replaced the bare phase indices of `SceneObject::drawing` with `ChamferBoundary` names, so Milestone 2 only shares the colours and removes the `mid` copies there.
- Observation (2026-11-21): the scene-row `TapHandler` (`gesturePolicy: WithinBounds`, declared inside the `ItemDelegate`) broke Ctrl+click and F2 in the Scene panel, while Ctrl+click in the 3D view kept working (user report). Inferred cause, not traced in the Qt sources: the handler belongs to the ItemDelegate itself, so its exclusive grab does not keep the press from the ItemDelegate's own mouse handling; the ItemDelegate takes the grab, the handler is cancelled, and the ItemDelegate's `onClicked` selects without Ctrl and sets no keyboard focus, so F2 (`Keys.onPressed` of the ItemDelegate) never arrives. The `MouseArea` is a child item above the ItemDelegate's content and accepts the press first, which is why the old code worked.
- Observation (2026-11-21): `scenecalibration.cpp` `writeProbeSettings` repeats the same mkpath/QSaveFile/commit sequence a fourth time, with native-separator paths in its texts. It stays: sharing `saveFile` across the two files needs a new shared declaration, outside this plan.


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
- Decision (user, 2026-11-21): `DeviceProfileModel::roleNames()` keeps rebuilding its hash; no `m_roleNames` member is added.
  Rationale: the member adds one field and one line net (header +1, constructor +2, `roleNames()` −2) to cache a seven-entry hash that `ConnectionsTable.qml`'s `TableView` reads when it receives the model, a few times per opened Settings window; no user-visible effect exists. A static hash listing only `display` and `driver` would be one line shorter but drops Qt's five other default role names and ties the model to one view. "Mandatory code reduction" keeps the existing implementation when it meets the same requirements.
  Date/Author: 2026-11-21, user.
- Decision: the Dry Run rule moves into ScenePlayback as `bool canPlay(bool ownsActive, bool hasPath) const`; `canPlayFor(ownerId, hasPath)`, `playFor` and `busyReason` of the 2026-11-20 decision are not added.
  Rationale: `SceneMachining::ownsPlayback` accepts any machining path as owner, not only the selected one, so `canPlayFor(m_inputId, …)` would stop a paused Dry Run from resuming after another object is selected, and `playFor` would need the motion handle of the selected object even when it resumes another path (null for an edge). `busyReason` would add eight lines to remove two duplicated lines from each coordinator (+6 net) without a requirement, which "Mandatory code reduction" rejects. Measured: the `canPlay` rule −1 line.
  Date/Author: 2026-11-21, implementing agent.
- Decision: the save helper is `QString saveFile(const QString& path, const QByteArray& contents)`, creating `QFileInfo(path).path()`, instead of `saveFile(folder, fileName, contents)`.
  Rationale: two parameters instead of three; the job-file callers keep their existing full-path constants (`SharedDataDirectory` goes), and the record writers pass `records/<id>/<file>`. The texts are the same as the plan's.
  Date/Author: 2026-11-21, implementing agent.
- Decision: the path strokes are three `constexpr RoboCrap3D::DrawingStyle` constants (`WorkingStroke`, `PtpStroke`, `TransferStroke`) instead of `PathStroke` plus `pathStyle`.
  Rationale: the same single definition of colours and widths without an enum, a function and a cast; measured 0 lines in `sceneobject.cpp` against +2 for the enum and function.
  Date/Author: 2026-11-21, implementing agent.
- Decision: `SceneMachiningPreparation::pathById` is removed and its callers use `SceneMachining::machiningPath`, made public, instead of making `pathById` return const.
  Rationale: both functions perform the same lookup (`findObject`, `qobject_cast` of `geometry()`) on the same SceneModel, and `machiningPath` already returns `const SceneMachiningPath*` ("reuse" comes first in AGENTS.md); −7 lines.
  Date/Author: 2026-11-21, implementing agent.
- Decision: `WorkflowPanel`'s `selectedColor` and `selectedBorderColor` are removed and their values written at their single use, instead of being made `readonly`.
  Rationale: no `ModeButton` instance assigns them, so the properties are an indirection without a requirement; −2 lines against 0 for `readonly`.
  Date/Author: 2026-11-21, implementing agent.
- Decision: in `EndEffectorPanel` the collet field's validator and id and the single-use `validPoseDraft` go too; in `QxSwitch` the `spacing` assignment goes with the text `contentItem`.
  Rationale: the validator and id served only the removed edit path; `validPoseDraft` had one caller left; `spacing` placed only the text content item.
  Date/Author: 2026-11-21, implementing agent.
- Decision: the scene-row `TapHandler` has `id: rowTap` and reads `rowTap.point.modifiers`.
  Rationale: ScenePanel is `ComponentBehavior: Bound`, and the project's QML rules qualify access through ids; the plan text read `point` unqualified.
  Date/Author: 2026-11-21, implementing agent. Superseded by the next decision.
- Decision: the `ScenePanel` row keeps its `MouseArea`; review item S3-D10 is not applied.
  Rationale: the `TapHandler` broke Ctrl+click and F2 (user check, Surprises). The `MouseArea` is the tested behaviour; a handler variant would need the ItemDelegate's own mouse handling switched off as well, an untested change for a low-priority style item with no line reduction (9 lines either way).
  Date/Author: 2026-11-21, implementing agent after the user's report.
- Decision: the two document sentences that describe Milestone 1 code (AGENT_CONTRACTS "Build and modules", ARCHITECTURE §7) are updated in Milestone 1, not at the end.
  Rationale: the documents then match the code at the Milestone 1 hand-off.
  Date/Author: 2026-11-21, implementing agent.


## Outcomes & Retrospective

(2026-11-21) All three milestones are implemented and passed the user's builds, `all_qmllint` and disconnected checks; the application behaves as before. Qt Charts is no longer a dependency, QmlChartBridge, `DeviceHub::keys`, `SceneSurfaceImporter::busy` and `SceneMachiningPreparation::pathById` are gone, the save sequence and the path stroke colours exist once, the Dry Run rule lives in ScenePlayback, and the dead QML members and ids are removed. The changed C++, QML and CMake inputs are 240 lines smaller (9091 → 8851).

Remaining: the calibration result files written through `saveFile` are confirmed only by code reading until the next equipment session (Progress).

Not done, by decision: `canPlayFor`/`playFor`/`busyReason` (would have changed Dry Run resume or added code), the `roleNames()` cache (user), and the ScenePanel `TapHandler` (broke Ctrl+click and F2). The fourth save sequence in `scenecalibration.cpp` `writeProbeSettings` stays outside this plan (Surprises).

Lessons: the plan's API sketches were written against the rules they replaced, not against every caller; `SceneMachining::ownsPlayback` accepting any machining path, and a pointer handler acting on its own `ItemDelegate`, were both visible only when the callers and the event path were read. Several plan items grew code (`busyReason`, the `pathStyle` enum, the `roleNames()` member); measuring each item's line count before writing it kept the milestones within "Mandatory code reduction".


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
3. **`DeviceProfileModel` cleanup.** Line 169 becomes `obj.value("peerPort").toInt(-1)`. `roleNames()` stays unchanged (Decision Log, 2026-11-21).
4. **`main.cpp` fonts.** Load the three fonts in a range-for over a constant array of the three resource paths, printing `qWarning() << "Failed to load font from resources:" << path;` for each that returns -1.
5. **`robotcontroller.cpp`.** Drop `const` on `home` in `initialize`.
6. **`scenegeometry.h`.** Make both `operator==` hidden friends (`friend bool operator==(const T& a, const T& b)`), comparing the same fields as before, now as `a.<field> == b.<field>`.
7. **Documents.** In AGENT_CONTRACTS "Build and modules" delete "QmlChartBridge stays in Application with its QtCharts requirement;"; in ARCHITECTURE §7 "Retained interfaces" delete the sentence that the `devicehub.h` class comment still mentions one shared I/O thread.

### Milestone 2: scene coordinators and writers

**ScenePlayback.** Add one public member, `bool canPlay(bool ownsActive, bool hasPath) const`, returning `!m_playing && m_robotAvailable && (active() ? ownsActive : hasPath)`. `SceneMachining::canPlay` returns `!m_calculating && m_playback->canPlay(ownsPlayback(), path && path->compatible())` with `path = machiningPath(m_inputId)`; `SceneCalibration::canPlay` returns `!calculating() && m_playback->canPlay(ownsPlayback(), path() != nullptr)`. Both `ownsPlayback` functions stay: the machining one accepts any machining path as the owner, so a paused Dry Run resumes after the selection changes. `dryRun` and `unavailableReason` stay unchanged in both coordinators (Decision Log, 2026-11-21).

**One save helper.** In `scenemachiningpreparation.cpp`'s anonymous namespace, `replaceJobFile` becomes `QString saveFile(const QString& path, const QByteArray& contents)`. It creates `QFileInfo(path).path()`, writes and commits a QSaveFile, and returns the error text:
- `tr("Cannot create %1.")` with the folder when it cannot be created;
- `tr("Cannot save %1: %2")` with the full path when writing or committing fails.

`writeCalibrationData` and `writeSharedData` pass `CalibrationDataPath` and `SharedDataPath`; `writeCalibrationResult` and `writeCalibrationTouches` pass `QDir(ROBOCRAP_RECORDS_DIR).filePath("<id>/calibration_result_<id>.csv")` (and `calibration_touches_<id>.csv`); `SharedDataDirectory` goes. Their error texts must keep their meaning; the job file's "Cannot write"/"Cannot replace" distinction becomes "Cannot save", and the AGENT_CONTRACTS text does not quote these strings, so no contract changes. Delete `setDirectWriteFallback(false)` and `cancelWriting()`.

**`SceneObject::drawing`.**
- Add the file-local constants `constexpr RoboCrap3D::DrawingStyle WorkingStroke{{30, 130, 45}, 3.0}`, `PtpStroke{{90, 65, 130}, 2.0}` and `TransferStroke{{0, 100, 210}, 2.0}`, and use them in both branches. The machining branch already selects by `ChamferBoundary` names (plan 06).
- Change `tcpPolyline` to take the point vector plus first and last indices, so it no longer needs `mid` copies.

**`SceneMachiningPreparation::pathById`.** Delete it; its three callers (`isCurrent`, `unavailableReason`, `prepare`) call `m_machining->machiningPath(id)`, which moves from the private to the public section of `SceneMachining` and already returns `const SceneMachiningPath*`.

**`SceneSurfaceImporter::busy`.** Remove the `busy` property, getter and `busyChanged` signal, keeping `m_busy` as the private guard. In `Main.qml`:
- line 168 becomes `!planeFileDialog.visible`;
- delete the "Importing surface…" branch (301);
- delete the `!surfaceImporter.busy` term (374).

### Milestone 3: QML dead code, pragmas, text formats, FINAL

- **`QxTextInput.qml`.** Delete `confirmed`, its reset in `onFocusChanged` and the green branch of the border colour. Keep `selectAll()` on focus.
- **`QxSwitch.qml`.** Delete the `displayonly` alias, the unused text `contentItem` and `spacing`; the Basic `Switch` keeps a sane implicit size without them, since `implicitWidth` and `implicitHeight` are already the indicator size.
- **Pragma.** Add `pragma ComponentBehavior: Bound` at the top of `QxBusyIndicator.qml`, `EndEffectorPanel.qml` and `PropertiesPanel.qml`. Where a delegate then fails to resolve a context property, declare it as a `required property` or qualify it through an id.
- **`EndEffectorPanel.qml`.** Delete `editColletValue`, `loadColletDraft`, the collet handlers, id and validator, and `colletDrafts`; show the collet values from `root.poseDraft(root.colletPose)` directly. Make the collet opacity constant, and make `validDraft` check only `root.drafts`, inline.
- **`ModeToolBar.qml`.** Delete `checkable`, `checked` and `toggled` from `Tool`, its emit, and the `checked` colour branches.
- **`WorkflowPanel.qml`.** Replace `selectedColor` and `selectedBorderColor` by `Colors.secondary.base` and `Colors.secondary.dark` at their single use each.
- **Ids.** Delete the unused ids listed above. Check each with `grep -n "\b<id>\b" <file>` before deleting.
- **Text formats.** Add `textFormat: Text.PlainText` to the labels listed above.
- **`ScenePanel.qml`.** No change: the row `MouseArea` (around line 263) stays. It is a child item above the ItemDelegate's content, takes the press, focuses the row (`objectButton.forceActiveFocus()`, which F2 needs) and passes Ctrl as the additive flag; the ItemDelegate's own `onClicked` stays for keyboard activation. A `TapHandler` declared inside the ItemDelegate was tried and broke Ctrl+click and F2 (Surprises, Decision Log, 2026-11-21).
- **FINAL.** Add `FINAL` to every `Q_PROPERTY` in `src/application/**/*.h` (sceneendeffectors.h, scenecalibration.h, devicerunner.h, deviceprofilemodel.h, scenegeometry.h, sceneobject.h, scenemodel.h, sceneplayback.h, scenemachining.h, scenemachiningpreparation.h, workspacecontroller.h: 85 declarations; devicehub.h and scenesurfaceimporter.h have none after Milestones 1 and 2). No class is subclassed in the project; check with `grep -rn ": public Scene\|: public Device\|: public Workspace" src`.


## Concrete Steps

Work in the repository root. Before and after each milestone:

    wc -l src/application/*.cpp src/application/*.h src/application/devices/* src/application/scene/* src/application/robot/* src/application/qml/*.qml src/application/qml/*/*.qml src/application/qml/*/*/*.qml src/application/qml/*/*/*/*.qml qml/Modules/Components/*.qml CMakeLists.txt src/application/CMakeLists.txt
    git diff --check
    git status --short

The status shows the two deleted QmlChartBridge files as ` D`. The user builds; the agent never does. Because `find_package` changes, the user re-runs CMake configure in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (Qt Creator does it on build), then builds `robocrap` and runs `all_qmllint`. Expect no warning in the changed QML files, and in particular none of the former 21 `QL:unqualified` rows.


## Validation and Acceptance

All checks below are run by the user, application disconnected, after the build. Milestone 1 needs Start-up, Settings and Calibration entry; the others follow Milestones 2 and 3.
- **Start-up.** The application starts and the robot appears. The console shows no font warning, if the fonts are present.
- **Settings.** Open Settings: the connections table shows the four profiles with their driver column, which uses DeviceProfileModel's roles, and the same Peer Port values as before.
- **Calibration entry.** Enter Calibration, leave it and enter it again: the sphere, table, stand and calibration path appear as before, without an error (the second entry compares the sphere with `operator==`).
- **Surface import.** Import a plane from `data/probe_points/edge_cyl_plane/`. Import works, the toolbar import buttons stay enabled afterwards, and no "Importing surface…" text is expected.
- **Machining playback.** Generate a chamfer path, then Dry Run, Pause, resume and Stop; Generate stays unavailable while Dry Run is active, as before. Pause again, select another scene object (for example the source edge), press Dry Run: the paused path resumes, as before.
- **Calibration playback.** Do the same in Calibration with the calibration path.
- **Paths in the 3D view.** A machining path shows a green machining turn, purple PTPs and blue LINs as before. A calibration path shows green approaches, purple PTPs and blue transfers.
- **End-effector panels.** Measuring EE and Spindle EE show the same values, and Apply works. The ER fields of Spindle EE stay read-only.
- **Scene panel.** Click selects, Ctrl+click adds to the selection, and a double click starts renaming.
- **Toolbar.** Tool colours are unchanged, and so are the checked Mode/Submode buttons of the Workflow panel.
- **Panels with removed ids.** The FTS Panel, PLC Panel (with the Door Panel LEDs) and the Settings window look and work as before; the menu-bar logo is still shown.
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
- `QxTextInput.confirmed`, `QxSwitch.displayonly`, and `ModeToolBar.Tool`'s `checkable`/`checked`/`toggled`;
- `WorkflowPanel.ModeButton`'s `selectedColor`/`selectedBorderColor` (file-local inline component).

Also removed: `SceneMachiningPreparation::pathById` (private).

New:
- `ScenePlayback::canPlay(bool ownsActive, bool hasPath)`;
- `SceneMachining::machiningPath` in the public section (C++ only);
- the file-local `saveFile`, `WorkingStroke`, `PtpStroke` and `TransferStroke`.

Documents to update:
- `docs/code context/AGENT_CONTRACTS.md`, "Build and modules": delete "QmlChartBridge stays in Application with its QtCharts requirement;" and remove `Charts` wherever the Qt dependency list is named (done in Milestone 1; no other sentence names `Charts`).
- `docs/code context/ARCHITECTURE.md`, section 3: replace "`busy` is set before reading and cleared before returning, so painting waits for the call" with "the import blocks the GUI thread for the duration of the call; a private flag rejects re-entrant imports" (done in Milestone 2).
- ARCHITECTURE §7 "Retained interfaces": delete the sentence that the `devicehub.h` class comment still mentions one shared I/O thread (done in Milestone 1).

Revision note (2026-11-21): Milestone 1 implemented. The `roleNames()` cache was dropped by the user's decision (Decision Log); the Milestone 1 document edits moved into Milestone 1; Milestone 1 checks and moved line numbers recorded.

Revision note (2026-11-21): Milestone 2 implemented. Plan of Work, Validation and Interfaces now describe the implemented `ScenePlayback::canPlay`, `saveFile(path, contents)`, stroke constants and the reuse of `SceneMachining::machiningPath`; `playFor` and `busyReason` were dropped. Reasons in the Decision Log: preserving Dry Run resume after a selection change, and no code growth without a requirement.

Revision note (2026-11-21): Milestone 3 implemented. Plan of Work describes the removed `WorkflowPanel` colour properties (instead of `readonly`), the extra dead items of `EndEffectorPanel` and `QxSwitch`, the qualified `rowTap.point` and the 85 FINAL declarations; reasons in the Decision Log.

Revision note (2026-11-21): after the user's Milestone 3 check the `ScenePanel` `TapHandler` was reverted to the original `MouseArea`; Plan of Work now keeps the `MouseArea`, and the failure and its inferred cause are in Surprises and the Decision Log.

Revision note (2026-11-21): final pass recorded (documents, compliance pass, counts, checks) and Outcomes & Retrospective written; the equipment check of the calibration result files stays open in Progress.
