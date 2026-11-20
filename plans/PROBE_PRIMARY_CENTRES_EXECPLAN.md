# Primary ball and sphere centres: one saved source, Reassign after calibration, last result in the Properties panel

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan. First, the implementing agent never runs a build, CMake configure, QML lint, the application or a test unless the user asks for that run (`AGENTS.md`, "Validation and handoff"); where a milestone names a build or a run, the agent records it as "not run (user's)" in `Progress` and continues. Second, the agent never moves the robot, never connects to PLC, RSI or force-sensor hardware, never starts a KRL program and never changes controller settings (TOOL_DATA stays manual). Third, no git staging, commits, `git rm` or `git mv` unless the user asks. Fourth, no project tests or test harnesses. Fifth, every line obeys `AGENTS.md`, in particular "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters), "Mandatory code reduction" and "Mandatory interface stability"; before each logical code change re-read those sections, and before a new helper, class or field record its requirement and why reuse fails in at most three sentences in the Decision Log.


## Purpose / Big Picture

Today the application keeps the probe's ruby ball centre (the measuring TCP, relative to the robot flange) only in memory: it starts from the controller value TOOL_DATA[5] (X −0.048, Y −0.097, Z 127 mm) on every start, and the user can change it in the Measuring End Effector panel until the application closes. The calibration sphere centre is fitted again from ten manual jog touches at every entry into the Calibration submode. A calibration run computes better values (least squares over 114 calibration touches) and shows them in an OK-only message, but nothing uses them.

After this plan: one settings file in the repository folder holds the primary centres, the ball centre in the flange frame {F} and the sphere centre in the robot base frame {B}, plus the two contact distances (stylus down, stylus horizontal) and the most recent calibration result. The application loads it at start; the Measuring End Effector panel shows the primary ball centre; calibration paths are generated from the primary ball and sphere centres, and therefore the KRL program `RSI_Probe_Calibration` uses them too (it sets `$TOOL` from the job file the application writes). After a successful calibration run a window shows the result and asks "Reassign the ball center and sphere center based on the calibration results?" with the buttons Reassign and Cancel; Reassign makes the new values primary, saves them, moves the calibration stand model with the sphere centre and shifts the probe model with the ball centre. When nothing is selected in the scene, the Properties panel on the right shows the most recent calibration result.

To see it working: apply a changed measuring TCP in the panel, restart, and see the same value; finish a calibration run, press Reassign, and see the new ball centre in the panel and the stand at the new sphere centre; deselect everything and read the last result in the Properties panel.


## Progress

- [x] (2026-11-19) Plan written after reading `SceneEndEffectors`, `SceneCalibration`, `SceneMachiningPreparation` (`endRun`, `resultRows`, `writeCalibrationResult`), `RobotController` (`drawingPose`, `synchronizeTools`, `loadRobot`), `OccRobotAdapter::applyRobotTransforms`, `DrawingPose` in `occcontroller.h`, `Main.qml` (result dialog, `PropertiesPanel` use), `PropertiesPanel.qml`, `RSI_Probe_Calibration.src` (`$TOOL=ToolFrame`), the root `CMakeLists.txt` and the contract sections "Build and modules", "Viewport and CAD", "Geometry and scene", "Machining and playback" and "QML" of `docs/code context/AGENT_CONTRACTS.md`.
- [x] (2026-11-19) Counts at plan time (physical lines): `scenecalibration.cpp` 303, `scenecalibration.h` 69, `scenemachiningpreparation.cpp` 591, `scenemachiningpreparation.h` 139, `sceneendeffectors.cpp` 134, `sceneendeffectors.h` 69, `robotcontroller.cpp` 183, `robotcontroller.h` 59, `occrobotadapter.cpp` 152, `occcontroller.h` 186, `Main.qml` 543, `PropertiesPanel.qml` 360, root `CMakeLists.txt` 76. Re-count at the start of Milestone 1.
- [x] (2026-11-19) Plan review against the code: Reassign re-entry through `measuringTcpChanged`, settings-file error display, the result-file write error channel, the result window binding, QML registration, `DrawingPose` initialization, the Properties label width, saved A, B, C, the Reassign side effect on the prepared job and the contract wording folded into the Decision Log, Plan of Work, Validation and Interfaces.
- [x] (2026-11-19) Milestone 1: settings file, loading at start, saving on every accepted measuring TCP with a changed ball centre; sphere centre from the primary value; settings errors in a native dialog. Files: root `CMakeLists.txt`, `scenecalibration.h/.cpp`, `scenegeometry.h`, `sceneobject.cpp`, `Main.qml`, `PropertiesPanel.qml`. Counts (physical/nonblank lines, before → after): root `CMakeLists.txt` 76/60 → 80/64; `scenecalibration.cpp` 303/273 → 477/435; `scenecalibration.h` 69/63 → 116/107; `scenegeometry.h` 149/138 → 150/139; `sceneobject.cpp` 262/241 → 264/243; `Main.qml` 543/479 → 562/496; `PropertiesPanel.qml` 360/313 → 360/313. C++ +224 physical lines, QML +19, build +4.
- [x] (2026-11-19) Milestone 1: build successful and the six disconnected checks of Validation and Acceptance passed (user): saved ball centre, load at start, A-only change writes nothing, `calibration_data.txt` line 2, invalid file dialog, defaults without the file. The optional hand-written `primary.sphereCentreB` check was not reported.
- [x] (2026-11-19) Milestone 2: last calibration result recorded in the settings file; result window with Reassign and Cancel. Files: `scenecalibration.h/.cpp`, `scenemachiningpreparation.h/.cpp`, `Main.qml`, `src/application/CMakeLists.txt`, new `CalibrationResultWindow.qml`. Counts (physical/nonblank, before → after): `scenecalibration.cpp` 477/435 → 546/497; `scenecalibration.h` 116/107 → 131/122; `scenemachiningpreparation.cpp` 591/542 → 556/510; `scenemachiningpreparation.h` 139/127 → 134/122; `Main.qml` 562/496 → 573/506; `src/application/CMakeLists.txt` 132/127 → 133/128; `CalibrationResultWindow.qml` new 89/75. C++ +44 physical lines (the rows moved, not copied), QML +100, build +1.
- [x] (2026-11-19) Milestone 2: build successful (user).
- [x] (2026-11-19) Milestone 2: software check (prepared `lastCalibration` kept by an Apply): passed (user).
- [ ] Deferred equipment checks (user decision 2026-11-19; Validation and Acceptance, "Deferred equipment checks"): Milestone 2 calibration run, Cancel, Reassign and failed run. Not run; to be done when the robot can move.
- [x] (2026-11-19) Milestone 3: probe model shifted by the ball centre. Files: `occcontroller.h`, `occrobotadapter.cpp`, `robotcontroller.h/.cpp`. Counts (physical/nonblank, before → after): `occcontroller.h` 186/160 → 187/161; `occrobotadapter.cpp` 152/137 → 154/139; `robotcontroller.cpp` 183/161 → 202/176; `robotcontroller.h` 59/47 → 61/49. C++ +24 physical lines.
- [x] (2026-11-19) Milestone 3: build and software check passed (user): Z 137 moves the probe model 10 mm with the ball at the TCP, Z 127 returns it, the spindle model is unchanged.
- [x] (2026-11-19) Milestone 4: last calibration result in the Properties panel when nothing is selected. Files: `PropertiesPanel.qml`, `Main.qml`. Counts (physical/nonblank, before → after): `PropertiesPanel.qml` 360/313 → 405/352; `Main.qml` 573/506 → 574/507. QML +46 physical lines.
- [x] (2026-11-19) Milestone 4: build, `robocrap_qmllint` (USE_HOT_RELOAD=OFF) and the six software checks passed (user): "No calibration result.", the 10 rows of a hand-written `lastCalibration` with the measuring TCP unchanged, one and two selected objects, stable panel and viewport size.
- [x] (2026-11-19) Milestone 5: `docs/code context/AGENT_CONTRACTS.md` ("Build and modules", "Viewport and CAD", "Geometry and scene", "Machining and playback", "QML") and `docs/code context/APP_DESCRIPTION.md` ("Fixed equipment and editable settings", Workflow step 1) updated; compliance pass over the whole diff (findings in Outcomes); final counts against HEAD (physical/nonblank): C++ 2093 → 2385 (+292), QML 903 → 1068 (+165), build 208 → 213 (+5), documents 577 → 603 (+26).


## Surprises & Discoveries

- Observation: the KRL program already takes its tool from the application. `controller/krl/rsi/calibration/RSI_Probe_Calibration.src` line 44 sets `$TOOL=ToolFrame`, and `ToolFrame` is line 2 of `C:\RoboCrapJobs\calibration_data.txt`, written by `writeCalibrationJob` in `src/application/scene/scenemachiningpreparation.cpp` from the measuring TCP of the sampled path. No KRL change is needed for "RSI_Probe_Calibration uses the latest primary centres"; generating the path from the primary ball centre is enough. TOOL_DATA[5] on the controller is used only for jogging and stays manual.
  Evidence: `RSI_Probe_Calibration.src:44`, `:125`; `RSI_Probe_Calibration_CONTRACT.md`, "calibration_data.txt", item 2.
- Observation: both end-effector models get the same transform, the flange matrix. `RobotController::drawingPose` sets `DrawingPose::tool = links.back()`, and `OccRobotAdapter::applyRobotTransforms` applies `pose.tool` to every tool part. The measuring model can therefore only be shifted with its own matrix.
- Observation: a `QtQuick.Dialogs` `MessageDialog` offers only standard buttons (Ok, Yes, Cancel, …), not a button labelled "Reassign"; an in-window `QtQuick.Controls` `Dialog` would be hidden behind the native 3D viewport window (`AGENT_CONTRACTS.md`, "Viewport and CAD": no Qt Quick item draws over `RobotViewport`). A separate top-level QML `Window` is not covered by the viewport.
- Observation: `SceneCalibration::activate()` calls `setError(file.error)` on every entry into the Calibration submode, and the status bar in `Main.qml` shows `App.Calibration.error` only while `calibrationActive` is true. An error of the settings file reported through `error` would be replaced at the first entry and is never shown in the Rough, Precise or Machining states, where the Measuring End Effector panel can also apply a TCP.
- Observation: `SceneEndEffectors::applyMeasuringTcp` emits `measuringTcpChanged` synchronously. Reassign applies the ball centre through it, so any slot connected to that signal runs inside `reassignPrimaryCentres`.
- Observation: in `SceneMachiningPreparation::endRun` the fit rows are produced before `m_runError` receives the result-file write error and the touch-file write error, and `calibrationRunChanged` (the NOTIFY of `calibrationRunError`) is emitted after both. A signal emitted at the point of the fit precedes the final error text.
- Observation: only the `USE_HOT_RELOAD=OFF` branch of `src/application/CMakeLists.txt` lists `QML_FILES`; the ON branch has none.
- Observation: `RobotController::drawingPose` builds `DrawingPose result{links, links.back(), {}, {}}` (`robotcontroller.cpp:67`) and returns early on line 68 when the active tool is not the spindle. The robot installation (`OccRobotAdapter::load`, line 57) and `applyRobotTransforms` both apply tools through `OccRobotAdapter::applyTransforms`, so one change there covers the HOME pose.
- Observation: `PropertiesPanel.qml` computes one label column width from a constant label list for all rows; the panel is at most 350 px wide (`Main.qml`), and value fields clip without wrapping. The calibration result labels ("Contact distance, stylus horizontal, mm", "Model check: sets outside 3·s_rep/√m") would widen the column for every object and clip vector values such as `[1054.1858; -331.9361; 454.1889]`.
- Observation (unverified): the position of the ball centre (0; 0; 127) mm in the frame of `EE_WP-500.brep` is a user statement, not checked against the CAD file. The Milestone 3 check shows it.


## Decision Log

- Decision: the settings file is `settings/probe_calibration.json` in the repository, located through a new root CMake cache path `ROBOCRAP_SETTINGS_DIR` (default `${PROJECT_SOURCE_DIR}/settings`) passed to every target as a compile definition, like `ROBOCRAP_RECORDS_DIR`. The file is tracked by git (not ignored).
  Rationale: user decision (2026-11-19: a settings file in the repository folder). Tracking it keeps the primary centres when the build folder is deleted and carries them between the two computers (`E:` and `D:`) through git; the user commits it when it changes.
  Date/Author: 2026-11-19, user (location) and agent (mechanism, tracking).
- Decision: file format, version 1:

        {
          "version": 1,
          "primary": {
            "source": "calibration_<id>",
            "ballCentreF": [x, y, z],
            "sphereCentreB": [x, y, z],
            "contactVertical": rho_v,
            "contactHorizontal": rho_h
          },
          "lastCalibration": {
            "id": "calibration_<id>",
            "ballCentreF": [x, y, z],
            "sphereCentreB": [x, y, z],
            "verificationSphereCentreB": [x, y, z],
            "contactVertical": rho_v,
            "contactHorizontal": rho_h,
            "quantile95": chi95,
            "repeatability": s_rep,
            "smallestSingularValue": s_min,
            "calibrationTouches": 114,
            "verificationTouches": 44,
            "setsOutsideBound": 23,
            "setCount": 36
          }
        }

  Lengths in mm. `primary` is written by Reassign; `primary.ballCentreF` is also written whenever the measuring TCP is applied in the panel with X, Y or Z different from the saved `primary.ballCentreF` (`source` then becomes `"manual"`, the sphere centre and contact distances stay); `lastCalibration` is written after every successful calibration fit. Only X, Y, Z of the measuring TCP are saved: A, B, C start at TOOL_DATA[5] (0, 0, 0) after every start, and a change of only A, B or C writes nothing. Every key is optional except `version`: a missing file or key means the built-in default (ball centre TOOL_DATA[5], sphere centre fitted from the jog touches, no contact distances, no last result). An unreadable or invalid file is reported through `SceneCalibration::settingsError` (Decision Log, settings-file errors) and treated as missing; the application does not overwrite it until the next accepted change.
  Rationale: one source for the ball centre, the sphere centre, the contact distances (user decision 2026-11-19: Reassign also saves them) and the result shown in the Properties panel, readable without parsing `records/`.
  Date/Author: 2026-11-19, agent.
- Decision: `SceneCalibration` owns the settings file: it reads it in its constructor, writes it, and holds the primary sphere centre, the contact distances and the last result. Requirement: one owner must read and write the file so that writes of the ball centre, Reassign and the last result never overwrite each other. Reuse: `SceneCalibration` already owns the sphere and holds the `SceneEndEffectors` pointer; `SceneMachiningPreparation` already holds the `SceneCalibration` pointer; a new class would need both and a QML registration.
  Date/Author: 2026-11-19, agent.
- Decision: `onMeasuringTcpChanged` compares X, Y, Z of `m_effectors->measuringTcp()` with `m_settings.primary.ballCentre`; when they are equal it does nothing, otherwise it stores them, sets `source` to `"manual"` and writes the file. `reassignPrimaryCentres` sets `m_settings.primary` from the last result first, then calls `applyMeasuringTcp`, so the slot finds equal values and leaves `source` = the run id; Reassign then writes the file once.
  Rationale: `measuringTcpChanged` is synchronous (Surprises & Discoveries); without the comparison Reassign would end with `source` = `"manual"` and two writes. The same comparison makes a change of only A, B or C write nothing.
  Date/Author: 2026-11-19, agent (plan review).
- Decision: settings-file errors use a separate property. `SceneCalibration` gets `QString m_settingsError` and `Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)`, set by the read in the constructor and by every write (empty after a successful write) through one private `QString saveSettings()` that returns the same text; a non-empty value is also printed with `qInfo().noquote() << "[ERROR]"`, like the job-file removal error in `scenemachiningpreparation.cpp`. The status bar in `Main.qml` shows `App.Calibration.settingsError` in every mode, in red, after the busy messages and before `calibrationStatus`. Requirement: the error arises outside the Calibration submode and `error` is replaced on every entry (Surprises & Discoveries); reuse of `error` fails for both reasons. The status label is elided and of fixed height, so the message does not change the layout.
  Date/Author: 2026-11-19, agent (plan review).
- Decision (Milestone 1, supersedes the status-bar display and `saveSettings()` of the previous decision): a settings-file error opens a native `QtQuick.Dialogs` `MessageDialog` "Probe settings" (OK) in `Main.qml`, from `Connections` on `settingsErrorChanged` and, for the error of the start read (emitted before QML exists), from `Component.onCompleted`. `settingsErrorChanged` is emitted for every non-empty error, also a repeated one, and when the error becomes empty. There is no `saveSettings()`: callers write `setSettingsError(writeProbeSettings(m_settings))`, and `setSettingsError` (the counterpart of `setError`) also prints a non-empty error with `qInfo`.
  Rationale: the error stays until the next successful write, so a status-bar message ahead of the other statuses would hide the RSI Sampling status in the Machining mode for as long as the file is invalid, and one behind them would never be seen there. A native dialog is a separate window: the viewport cannot cover it and it does not change the layout. One setter used by the start read and every write replaces a helper that would have duplicated the log line.
  Date/Author: 2026-11-19, agent (implementation).
- Decision (Milestone 1): `readProbeSettings` and `writeProbeSettings` already handle `lastCalibration` (`CalibrationSummary`). Requirement: a write of the ball centre rewrites the whole file and must keep a `lastCalibration` that was prepared by hand (Concrete Steps) or written by Milestone 2. Helpers and why reuse fails: `vectorValue` (JSON [x, y, z]; `roughPoint` reads {X, Y, Z} objects), `countValue` (nonnegative integers; `positiveNumber` rejects 0 and accepts fractions), `calibrationSummary`, `summaryObject`, `primaryObject` and `vectorArray` (no JSON writer exists in `SceneCalibration`); the positive values reuse `positiveNumber`. `QSettings` was considered as a smaller alternative and rejected because the decided format is the JSON file above and `QSettings` gives no validation of the stored values.
  Date/Author: 2026-11-19, agent (implementation).
- Decision: the settings types `PrimaryCentres`, `CalibrationSummary` and `ProbeSettings` are declared at namespace scope in `scenecalibration.h`, above `SceneCalibration`. Rationale: `m_settings` needs the complete type in the header, and the free functions in the unnamed namespace of `scenecalibration.cpp` cannot name private nested types; they are not scene geometry, so `scenegeometry.h` is the wrong owner. The path is the constant `SettingsPath` (`ROBOCRAP_SETTINGS_DIR "/probe_calibration.json"`), so the read and write functions take no path parameter.
  Date/Author: 2026-11-19, agent (implementation).
- Decision (Milestone 2): two private helpers. `SceneCalibration::applyBallCentre(const V3d&)` replaces X, Y, Z of the measuring TCP and keeps A, B, C; the constructor and `reassignPrimaryCentres` both need it, and `applyMeasuringTcp` takes all six values. `joinedError(first, second)` in `scenemachiningpreparation.cpp` joins two error texts with a space; `endRun` needs it for the settings error and already repeated the same expression for the touch-file error, which it now replaces. In `Main.qml` the OK-only dialog becomes `calibrationFailedDialog` with the fixed title "Calibration failed" and its text bound to `calibrationRunError`; `onCalibrationRunFinished` only opens it when `calibrationResultWindow` is not visible.
  Date/Author: 2026-11-19, agent (implementation).
- Decision (Milestone 3): `RobotController::synchronizeMeasuringTool()`, connected to `SceneEndEffectors::measuringTcpChanged`, returns while the robot is not ready and otherwise calls `m_viewport->applyRobotTransforms(drawingPose(*m_pose))` without changing `m_pose`. It ignores the returned result, because `OccController::applyRobotTransforms` already reports a rejected transform through its error and `message`. The offset uses `makeTranslation` from `geometry/utils.h` and the constant `MeasuringModelBallCentre` (0; 0; 127) mm in the unnamed namespace of `robotcontroller.cpp`. Requirement: `synchronizeTools` only updates the spindle frames, so a new slot is needed to redraw the tool parts. A TCP applied while the robot CAD is still loading appears with the next applied pose (playback, Stop to HOME), because the installation pose is built when `loadRobot` starts; the start load of the settings file runs before `loadRobot` in `main.cpp`, so a saved ball centre is drawn from the start.
  Date/Author: 2026-11-19, agent (implementation).
- Decision (Milestone 4): `PropertiesPanel.qml` gets the inline component `ValueText` (the selectable, clipped one-line `TextEdit` that `PropertyValue` already used), so the result rows and the object rows share one value definition. The block `calibrationContent` lies in the existing `ScrollView` next to `propertiesContent`; `contentHeight` takes the height of the visible one. One heading label shows "Last calibration" or "No calibration result.", so both states have the same height. The label "Select an object." / "N objects selected." is hidden while nothing is selected (`calibrationShown`, that is `selectionCount === 0`).
  Date/Author: 2026-11-19, agent (implementation).
- Decision: `recordCalibrationResult` returns the settings-file write error as `QString` (empty on success); `endRun` appends it to `m_runError` like `touchesError`. `reassignPrimaryCentres` returns false when no last result exists or the write fails; the reason is in `settingsError`.
  Rationale: `AGENTS.md`, results through the return value; the result window and the console already show `calibrationRunError`.
  Date/Author: 2026-11-19, agent (plan review).
- Decision: Reassign invalidates the calibration path (the measuring TCP and, in the Calibration submode, the sphere change). `SceneMachiningPreparation::onCalibrationAvailabilityChanged` then drops the prepared calibration job, and `clearPrepared` deletes `C:\RoboCrapJobs\calibration_data.txt`. This is intended: a held job would carry the old ToolFrame. The next run needs Generate Path and RSI Sampling again.
  Date/Author: 2026-11-19, agent (plan review).
- Decision: the "Last calibration" block in the Properties panel shows each row as two lines, the label above the value, in a full-width column; the object rows keep the shared label column. The shared constant label list gains only the new sphere labels "Fitted center (jog)" and "Center". Rationale: the result labels are too long for a label column in a panel of at most 350 px (Surprises & Discoveries); stacked rows need no label width and keep the object rows unchanged.
  Date/Author: 2026-11-19, agent (plan review).
- Decision: the primary centres come only from the calibration estimate of the 114 calibration touches (`ProbeCalibrationFit::ballCentre`, `sphereCentre`, `contactVertical`, `contactHorizontal`); the sphere centre refitted from the verification touches is shown but never becomes primary.
  Rationale: user decision 2026-11-19.
  Date/Author: 2026-11-19, user.
- Decision: Reassign keeps the measuring TCP's A, B, C (today 0, 0, 0) and replaces only X, Y, Z with the ball centre.
  Rationale: the calibration determines the ball centre, not an orientation.
  Date/Author: 2026-11-19, agent.
- Decision: the calibration path keeps the nominal contact distance L = 14 mm for its start and contact points; the saved contact distances are not used by path generation in this plan.
  Rationale: the user asked to save them for measuring parts; using them for the touch plan changes the search geometry and is a separate decision (`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_SPEEDUP_RU.md`, section 4.5).
  Date/Author: 2026-11-19, agent.
- Decision: the sphere object keeps the jog fit for display and uses the primary sphere centre when one is saved. `SceneSphere` gains `V3d roughCenter` (the jog fit centre); `center` becomes the primary centre if saved, otherwise the jog fit centre. The table and the stand are drawn relative to `center`, so they move with the primary centre.
  Rationale: requirement 1 (paths from the primary source) and requirement 2 (the stand moves with the reassigned centre); the jog fit radius and RMS stay meaningful only about their own centre.
  Date/Author: 2026-11-19, agent.
- Decision: the probe model keeps its CAD frame (user decision 2026-11-19, second approach). In `EE_WP-500` the ball centre lies on the model's z axis at 127 mm. `RobotController` places the measuring model at the flange matrix times a translation by the primary ball centre minus (0; 0; 127) mm, expressed in {F}; the spindle model keeps the flange matrix. `DrawingPose` gains `M4d measuringTool` as its last member, so the existing positional initialization keeps `tcp` and `collet` in place; `OccRobotAdapter` applies it to the measuring part (`RobotTool::Measuring`, the first tool) and `tool` to the spindle part. Requirement: the two models need different matrices; `DrawingPose::tool` is shared (Surprises & Discoveries).
  Date/Author: 2026-11-19, user and agent.
- Decision: the result after a successful run opens a new top-level QML `Window` (`src/application/qml/Views/Workspace/CalibrationResultWindow.qml`, modal, fixed size) with the result rows, the question and two `QxButton`s, Reassign and Cancel; a failed run keeps the existing OK-only `MessageDialog`. Requirement: a button labelled Reassign (not available in `MessageDialog`) in a window the native viewport cannot cover (Surprises & Discoveries).
  Date/Author: 2026-11-19, agent.
- Decision: the result rows move from `SceneMachiningPreparation` to `SceneCalibration`. `resultRows` and `setsOutsideBound` move with them; `SceneMachiningPreparation` keeps `calibrationRunError` and `calibrationRunFinished` but drops `calibrationResult` and `m_runResult`, and on success calls `SceneCalibration::recordCalibrationResult(fit, id)`. Main.qml reads `App.Calibration.lastCalibrationRows`. Rationale: one holder of the displayed result for the window, the Properties panel and the file (code reduction: no second copy of the rows).
  Date/Author: 2026-11-19, agent.
- Decision: the milestones are implemented and checked in software now (build, disconnected application checks); every check that needs the robot, a KRL program, the PLC or the force sensor is collected in Validation and Acceptance, "Deferred equipment checks", and in `Progress`, and is run later. A milestone counts as implemented after its build and software check; its deferred checks stay open in `Progress`.
  Rationale: user decision 2026-11-19 (no equipment work available now).
  Date/Author: 2026-11-19, user.
- Decision: postponed, not in this plan: Import Sphere from jog touches (with the TOOL_DATA[5] used while jogging recorded in the point file, so that the application can replace it by the primary ball centre), the table position in {B}, and any write of TOOL_DATA[5] on the controller.
  Rationale: user decisions 2026-11-19.
  Date/Author: 2026-11-19, user.


## Outcomes & Retrospective

Milestone 1 (2026-11-19): the settings file is read at construction of `SceneCalibration`, a saved ball centre replaces X, Y, Z of TOOL_DATA[5] before the panel, the path generation and the robot see the measuring TCP, every applied TCP with a changed ball centre is saved as "manual", and a saved sphere centre becomes `SceneSphere::center` (stand, table and touch plan) while the jog fit stays as `roughCenter`. Not built or run (user's). Growth: about 140 C++ lines are the validated JSON read and write of 18 keys; the rest are the settings types, the slot, the error setter and the dialog.

Milestone 2 (2026-11-19): after a successful fit `endRun` calls `SceneCalibration::recordCalibrationResult`, which saves `lastCalibration` and emits `lastCalibrationChanged`; `Main.qml` then shows `CalibrationResultWindow` with the rows of `lastCalibrationRows`, the bound `calibrationRunError` and the question. Reassign makes the last result primary (`source` = the run id), applies the ball centre, saves the file and, in the Calibration submode, moves the sphere and the stand. A run without a fit opens "Calibration failed". Build successful (user); its window checks are deferred equipment checks.

Milestone 3 (2026-11-19): `DrawingPose::measuringTool` places the probe model at the flange matrix times a translation by the measuring TCP position minus (0; 0; 127) mm, the spindle model keeps the flange matrix, and an applied measuring TCP redraws the current pose. Build and software check passed (user).

Milestone 4 (2026-11-19): with nothing selected the Properties panel shows "Last calibration" and each row of `App.Calibration.lastCalibrationRows` as a label line above a selectable value line, or "No calibration result."; one selected object shows its properties, several show "N objects selected.". Build, lint and software checks passed (user).

Milestone 5 and compliance pass (2026-11-19): the contracts and the application description state the persisted X, Y, Z of the measuring TCP, the settings file, the primary sphere centre, `DrawingPose::measuringTool`, Reassign and its removal of the prepared job, the result window, the "Probe settings" dialog and the Properties block. The pass over the changed declarations, bodies and callers found no lambda, project template, thrown exception, output parameter or function with more than two new parameters; results return by value (`ProbeSettingsResult`, `QString` errors, `bool` of Reassign); `SceneCalibration` changes `SceneEndEffectors` only through `applyMeasuringTcp`. Accepted risk: `readProbeSettings` requires `quantile95`, `repeatability` and `smallestSingularValue` to be positive; a fit with an exact 0 would be saved but make the file invalid at the next start (reported by the dialog, defaults used). Remaining: the deferred equipment checks; the user commits `settings/probe_calibration.json` when its values are wanted (it now holds the hand-written test `lastCalibration` of the Milestone 4 check).


## Context and Orientation

The repository is a Windows Qt Quick application (C++17, Qt 6.8, CMake). The robot is a KUKA KR 10 with a KR C4 controller; KRL is its programming language. The measuring probe is a WP-500 with a ruby ball; the "measuring TCP" is the ball centre relative to the robot flange. {F} is the flange frame, {B} the robot base frame. The calibration sphere (Ø25 mm) stands on a strut on a table; a calibration run touches it 158 times (114 calibration, 44 verification touches) and fits the ball centre in {F}, the sphere centre in {B} and two contact distances by least squares (`fitProbeCalibration`, `src/trajectories/probe/probecalibrationfit.cpp`, result type `ProbeCalibrationFit` in `src/trajectories/include/trajectories/probecalibrationfit.h`).

`SceneEndEffectors` (`src/application/scene/sceneendeffectors.*`, QML singleton `App.EndEffectors`) holds `m_measuringTcp`, six values X, Y, Z (mm), A, B, C (deg), initialized to TOOL_DATA[5], and `m_measuringTcpFrame`, the 4×4 transform. `applyMeasuringTcp(pose)` accepts six finite values, ignores an unchanged pose and emits `measuringTcpChanged`. The Measuring End Effector panel (`src/application/qml/Views/Workspace/EndEffectorPanel.qml`, created in `Main.qml` around line 500) shows `App.EndEffectors.measuringTcp` and calls `applyMeasuringTcp` on Apply.

`SceneCalibration` (`src/application/scene/scenecalibration.*`, singleton `App.Calibration`) is constructed in `src/application/main.cpp` after `SceneEndEffectors`. On every entry into the Calibration submode `activate()` reads `<exe>/resources/json/calibration_sphere.json` with `readSphereFile` (sphere, strut and table geometry plus `roughPoints`, the ball centres of ten jog touches), fits a sphere through the points (`fitSphere`), and inserts a `SceneSphere` (`src/application/scene/scenegeometry.h`) with `center`, radii, RMS, points, strut, table and the stand model path (`CALIBRATION_STAND.brep`, origin at the sphere centre) through `replaceSphere`. `generate()` builds the touch plan from `sphere()->center` and the measuring TCP frame. A changed measuring TCP or sphere invalidates the path (`invalidatePath`).

`SceneMachiningPreparation` (`src/application/scene/scenemachiningpreparation.*`) runs the calibration: `endRun` writes `records/<id>/calibration_touches_<id>.csv`, fits, fills `m_runResult = resultRows(fit)` (QML property `calibrationResult`) and writes `records/<id>/calibration_result_<id>.csv`; `calibrationRunFinished` makes `Main.qml` (lines 104–121) open `calibrationResultDialog`, a `MessageDialog` with OK.

`RobotController` (`src/application/robot/robotcontroller.*`) builds `DrawingPose` (`src/viewport/include/viewport/occcontroller.h`: `links`, `tool`, optional `tcp`, `collet`) for the viewport; `tool` is the flange matrix used for both end-effector models (`EE_WP-500.brep` measuring, `EE_GDZ-65-800A.brep` spindle). `OccRobotAdapter::applyRobotTransforms` (`src/viewport/adapters/occrobotadapter.cpp`) sets each link part and each tool part transform.

`PropertiesPanel.qml` (`src/application/qml/Views/Workspace/`) shows the selected object; when nothing is selected it shows the label "Select an object." (or "N objects selected.") and hides its `ScrollView` content. Its label column width is computed once from a constant list of labels.


## Plan of Work

Milestone 1, the settings file. In the root `CMakeLists.txt`, next to `ROBOCRAP_RECORDS_DIR`, add the cache path `ROBOCRAP_SETTINGS_DIR` (default `${PROJECT_SOURCE_DIR}/settings`) and `add_compile_definitions(ROBOCRAP_SETTINGS_DIR="${ROBOCRAP_SETTINGS_DIR}")`. In `scenecalibration.h`, above the class, declare `struct PrimaryCentres { QString source; std::optional<V3d> ballCentre; std::optional<V3d> sphereCentre; std::optional<double> contactVertical; std::optional<double> contactHorizontal; };`, `struct CalibrationSummary` with the `lastCalibration` fields of the Decision Log and `struct ProbeSettings { PrimaryCentres primary; std::optional<CalibrationSummary> last; };`. In the unnamed namespace of `scenecalibration.cpp` add the constant `SettingsPath`, `struct ProbeSettingsResult { ProbeSettings settings; QString error; };` (settings empty for a missing or invalid file), `ProbeSettingsResult readProbeSettings()` (a missing file returns empty settings without error; a present key must be valid) and `QString writeProbeSettings(const ProbeSettings& settings)` (`QSaveFile`, creates the folder, writes only saved keys, returns an empty string or the error), with the JSON helpers of the Decision Log. `SceneCalibration` gets the members `ProbeSettings m_settings` and `QString m_settingsError`, the property `settingsError` with the signal `settingsErrorChanged`, and the private `void setSettingsError(const QString& error)` (stores the error, prints a non-empty one with `qInfo`, emits for every non-empty error and for a change to empty). Its constructor reads the file into `m_settings` and calls `setSettingsError` with the read error; if `primary.ballCentre` exists, it calls `m_effectors->applyMeasuringTcp` with that X, Y, Z and the current A, B, C before connecting `measuringTcpChanged`. A new private slot `onMeasuringTcpChanged()` (connected after the start load, together with the existing `invalidatePath` connection) returns when X, Y, Z of `m_effectors->measuringTcp()` equal `primary.ballCentre`; otherwise it stores them as `primary.ballCentre`, sets `primary.source` to `"manual"` and calls `setSettingsError(writeProbeSettings(m_settings))`. In `Main.qml` a native `MessageDialog` "Probe settings" (OK) shows `App.Calibration.settingsError`; `Connections` on `App.Calibration` opens it on `settingsErrorChanged` when the error is not empty, and `Component.onCompleted` opens it for the error of the start read. `SceneSphere` gains `V3d roughCenter`; `readSphereFile` fills it with the jog fit centre and `center` with the same value; `activate()` then replaces `center` by `primary.sphereCentre` when saved, before `replaceSphere`. The Properties rows of the sphere (`SceneObject` properties for `Sphere`) label the jog fit centre "Fitted center (jog)" and the used centre "Center" (row builder in `SceneObject::properties`, `src/application/scene/sceneobject.cpp` lines 85–99); add both labels to the constant label list of `PropertiesPanel.qml` (`labelColumnWidth`).

Milestone 2, last result and Reassign. In `SceneCalibration` add `QString recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id)` (fills `m_settings.last`, calls `setSettingsError(writeProbeSettings(m_settings))`, emits `lastCalibrationChanged`, returns `m_settingsError`), the property `Q_PROPERTY(QVariantList lastCalibrationRows READ lastCalibrationRows NOTIFY lastCalibrationChanged)` built by the moved `resultRows` from `CalibrationSummary` (move `resultRows`, `resultRow`, `vectorText` and `setsOutsideBound` from `scenemachiningpreparation.cpp`, adapting them to the summary), and `Q_INVOKABLE bool reassignPrimaryCentres()`: it returns false without `m_settings.last`; otherwise it sets `primary` from it (`source` = its id) before anything else, applies the ball centre to `m_effectors` (current A, B, C kept; `onMeasuringTcpChanged` then finds equal values and writes nothing), calls `setSettingsError(writeProbeSettings(m_settings))`, and if the Calibration submode is active replaces the sphere by a copy of the current one with `center` = the new sphere centre (the stand moves with it; the path is invalidated, and with it the prepared job and `calibration_data.txt`, Decision Log); it returns whether the write succeeded. In `SceneMachiningPreparation::endRun` replace `m_runResult = resultRows(*result.fit)` by `const QString settingsError = m_calibration->recordCalibrationResult(*result.fit, m_preparedCalibration->diagnosticId)`, keep `m_runError = writeCalibrationResult(...)` after it, and append `settingsError` to `m_runError` the same way as `touchesError`; delete `m_runResult`, the `calibrationResult` property and the moved helpers. Create `src/application/qml/Views/Workspace/CalibrationResultWindow.qml`: a `Window` with `modality: Qt.ApplicationModal`, fixed width and height, title "Calibration complete", the rows from a required `var rows` input as "label: value" lines, the question "Reassign the ball center and sphere center based on the calibration results?", and `QxButton`s Reassign and Cancel emitting `reassignRequested()` and closing; register the file in the `QML_FILES` list of the `USE_HOT_RELOAD=OFF` branch of `src/application/CMakeLists.txt`, next to the other `Views/Workspace` files (the ON branch has no list). In `Main.qml`, a `Connections` on `App.Calibration` opens this window on `lastCalibrationChanged` (emitted only by `recordCalibrationResult`, that is after a successful fit; the start load in the constructor runs before QML exists), and the window shows `App.MachiningPreparation.calibrationRunError` below the rows when it is not empty (for example a result-file write error). The window receives the error through a binding, not a value copied when it opens: `lastCalibrationChanged` is emitted before `endRun` sets the final `m_runError` (Surprises & Discoveries). `endRun` emits `calibrationRunFinished` after `recordCalibrationResult`, so `onCalibrationRunFinished` opens the existing OK-only `MessageDialog` "Calibration failed" only when the window is not open, that is when the run produced no fit; `onReassignRequested` calls `App.Calibration.reassignPrimaryCentres()`.

Milestone 3, the probe model. In `occcontroller.h` add `M4d measuringTool = M4d::Identity();` as the last member of `DrawingPose`; in `OccRobotAdapter::applyTransforms` (`occrobotadapter.cpp` line 92, used by both the installation at line 57 and `applyRobotTransforms`) apply `pose.measuringTool` to the first tool part (`RobotTool::Measuring`) and `pose.tool` to the second. In `RobotController::drawingPose` pass the fifth initializer `links.back() * translation(ball − (0; 0; 127))` to `result` on line 67, before the early return of line 68, with the ball centre from `m_endEffectors->measuringTcp()` and the constant `MeasuringModelBallCentre = (0; 0; 127)` mm in the unnamed namespace of `robotcontroller.cpp`, next to the `EE_WP-500.brep` path; connect `SceneEndEffectors::measuringTcpChanged` to a slot that reapplies the current pose through `m_viewport->applyRobotTransforms(drawingPose(*m_pose))` when the robot is ready.

Milestone 4, the Properties panel. Add to `PropertiesPanel.qml` a required `var calibrationRows` input and, when `selectionCount === 0`, a "Last calibration" block (or the line "No calibration result." when the list is empty) in place of "Select an object."; with more than one object selected keep "N objects selected.". Each row of the block is a full-width column of two lines, the label (`Colors.foreground.medium`) above a read-only, selectable value (`Colors.foreground.high`, no wrap, clipped); the result labels are not added to the shared label list (Decision Log). Put the block inside the existing `ScrollView` region so that switching between the block and an object's rows does not resize the panel. `Main.qml` passes `App.Calibration.lastCalibrationRows`.

Milestone 5, documents. Update in their owning places: `docs/code context/AGENT_CONTRACTS.md` "Build and modules" (`ROBOCRAP_SETTINGS_DIR`, `settings/` tracked), "Geometry and scene" (the measuring TCP is persisted in the settings file; the primary sphere centre; `SceneSphere::roughCenter`), "Viewport and CAD" (`DrawingPose::measuringTool`, the probe model offset), "Machining and playback" (the result goes to `SceneCalibration`; Reassign; replace "never applied as the measuring TCP" by the Reassign rule; Reassign drops the prepared calibration job) and "QML" (the result window, the Properties panel without selection, `settingsError` in the status bar); `docs/code context/APP_DESCRIPTION.md` (Workflow step 1). Then the `AGENTS.md` compliance pass and final counts.


## Concrete Steps

Commands in Git Bash from the repository root. At the start of each milestone:

    git status --short
    wc -l <files of the milestone>

At the end of each milestone:

    git status --short
    git diff --check

To prepare a settings file for the disconnected checks of Milestones 2–4 without a robot run, the agent may write `settings/probe_calibration.json` by hand from `records/calibration_20261118_164135_671/calibration_result_calibration_20261118_164135_671.csv` (ball centre 0.023743, −0.056159, 126.970980; sphere centre 1054.185828, −331.936070, 454.188879; ρᵥ 13.844765; ρₕ 13.858389; χ95 0.111184; s_rep 0.010241; smallest singular value 2.040178; 114/44 touches; sets outside bound counted from the file's set rows) as `lastCalibration`, without `primary`.


## Validation and Acceptance

Milestone 1 (user builds and starts, no robot): change the measuring TCP Z in the Measuring End Effector panel to 126.9 and Apply; `settings/probe_calibration.json` now has `primary.ballCentreF` [−0.048, −0.097, 126.9] and `source` "manual"; after a restart the panel shows 126.9. Enter Calibration, Generate Path, RSI Sampling (RSI socket bound, no robot motion): line 2 of `C:\RoboCrapJobs\calibration_data.txt` starts with `-0.048000;-0.097000;126.900000`. Delete the settings file and restart: the panel shows TOOL_DATA[5] again. Apply a TCP that differs only in A: the settings file does not change, and after a restart A is 0 again. Make the settings file invalid JSON and restart: the dialog "Probe settings" names the file and the parse error, the console prints the same line after `[ERROR]`, and the file is left unchanged until the next Apply with a changed ball centre.

Milestone 2, software check (no equipment): with the app closed, add the prepared `lastCalibration` (Concrete Steps) to `settings/probe_calibration.json`, start the app, change the measuring TCP Z and Apply: the file still holds the same `lastCalibration` next to the new `primary.ballCentreF`. The window itself opens only after a calibration fit, so its checks are equipment checks.

Deferred equipment checks (user decision 2026-11-19: no robot motion, no KRL program start and no PLC or force-sensor work for now; run these later, each with the user's explicit authorization, and record them in `Progress`). Milestone 2, with the next user-authorized calibration run: after "program finished" the window "Calibration complete" lists the rows and the question; Cancel closes it and changes nothing; Reassign sets the panel to the run's ball centre, the settings file's `primary` to the run's values with `source` = the run id (not `"manual"`), and, in the Calibration submode, the stand to the new sphere centre (Properties of the sphere: "Center" = the run's sphere centre); the calibration path and `C:\RoboCrapJobs\calibration_data.txt` are gone, and the status bar asks for Generate Path. A failed run still shows "Calibration failed" with OK.

Milestone 3: apply a measuring TCP with Z 137 (10 mm longer) in the panel: the probe model moves 10 mm along the flange axis and its ball sits at the TCP; apply Z 127 again: it returns. The spindle model does not move.

Milestone 4: with nothing selected, the Properties panel shows "Last calibration" and the rows of `lastCalibration`; selecting an object shows its properties; selecting two objects shows "2 objects selected."; the panel's width and the viewport size do not change in any of these states; the sphere centre of the selected sphere still fits on one line, and every result value is readable on its own line.

Milestone 5: `AGENT_CONTRACTS.md` no longer says the measuring TCP is not persisted or that the result is never applied as the measuring TCP; `git diff --check` is clean.


## Idempotence and Recovery

Every edit is repeatable. The settings file is written atomically with `QSaveFile`; deleting it restores the built-in defaults at the next start. If a saved value is wrong, edit or delete the file with the application closed. Reassign is undone by applying the old values in the panel (ball centre) or by restoring the file from git (all values).


## Artifacts and Notes

Values of the most recent session for the checks, `records/calibration_20261118_164135_671/calibration_result_calibration_20261118_164135_671.csv`:

    ball_centre_F,0.023743,-0.056159,126.970980
    sphere_centre_B,1054.185828,-331.936070,454.188879
    verification_sphere_centre_B,1054.201311,-331.926711,454.199635
    contact_vertical,13.844765
    contact_horizontal,13.858389
    quantile95,0.111184
    repeatability,0.010241
    smallest_singular_value,2.040178
    touches,114,44

With this ball centre the probe model offset is (0.023743; −0.056159; −0.029020) mm in {F}.


## Interfaces and Dependencies

Root `CMakeLists.txt`: cache path and compile definition `ROBOCRAP_SETTINGS_DIR`.

`src/application/scene/scenecalibration.h`: `QString recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id);`, `Q_INVOKABLE bool reassignPrimaryCentres();`, `Q_PROPERTY(QVariantList lastCalibrationRows READ lastCalibrationRows NOTIFY lastCalibrationChanged)`, `Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsErrorChanged)`, signals `lastCalibrationChanged()` and `settingsErrorChanged()`, private slot `onMeasuringTcpChanged()`, private `void setSettingsError(const QString& error)`, members `ProbeSettings m_settings` and `QString m_settingsError`; at namespace scope above the class `PrimaryCentres`, `CalibrationSummary` and `ProbeSettings`.

`src/application/scene/scenegeometry.h`: `SceneSphere::roughCenter` (included in `operator==`).

`src/application/scene/scenemachiningpreparation.h`: `calibrationResult` property and `m_runResult` removed.

`src/viewport/include/viewport/occcontroller.h`: `DrawingPose::measuringTool` (last member).

`src/application/qml/Main.qml`: `MessageDialog` `settingsErrorDialog` ("Probe settings") opened from `Connections` on `App.Calibration.settingsErrorChanged` and from `Component.onCompleted`.

`src/application/qml/Views/Workspace/CalibrationResultWindow.qml`: `required property var rows`, `signal reassignRequested()`.

`PropertiesPanel.qml`: `required property var calibrationRows`.

No new library or dependency.


Revision note (2026-11-19): first version, from the user's requirements and decisions of 2026-11-19 (settings file in the repository; Reassign also saves the contact distances; primary centres only from the 114 calibration touches; probe model kept in its CAD frame with the ball at (0; 0; 127) mm; Import Sphere, table position and TOOL_DATA writes postponed; RSI_Probe_Calibration keeps reading the tool from the job file).

Revision note (2026-11-19, plan review): the slot that saves the ball centre now compares X, Y, Z with the saved value, so Reassign keeps `source` = the run id and writes once; settings-file errors go to a separate `settingsError` shown in the status bar in every mode; `recordCalibrationResult` returns its write error into `calibrationRunError`; the result window binds that error; the QML file is registered only in the hot-reload OFF branch; `DrawingPose::measuringTool` is the last member and is set before the early return; the Properties result block uses stacked rows instead of the shared label column; only X, Y, Z are saved; the Reassign side effect on the prepared job and the contract sentence "never applied as the measuring TCP" are recorded.

Revision note (2026-11-19, Milestone 1 implemented): settings-file errors open a native "Probe settings" dialog instead of a status-bar message, because a persistent error in the status bar would hide or be hidden by the RSI Sampling status; `saveSettings()` became `setSettingsError(writeProbeSettings(m_settings))`; the settings types are declared at namespace scope in `scenecalibration.h`; the read and write functions use the constant path and already handle `lastCalibration`, so a ball-centre write keeps it. Progress, Decision Log, Plan of Work, Validation, Interfaces and Outcomes updated.

Revision note (2026-11-19, Milestone 2 implemented): recorded the helpers `applyBallCentre` and `joinedError`, the renamed `calibrationFailedDialog`, the counts and the outcome; the plan of work is unchanged otherwise.

Revision note (2026-11-19, equipment checks deferred, Milestone 3 implemented): by user decision every check that needs the robot, a KRL program, the PLC or the force sensor is collected under "Deferred equipment checks" in Validation and Acceptance and kept open in Progress; Milestone 2 got a software check of the kept `lastCalibration`. Milestone 3 is implemented with the slot `synchronizeMeasuringTool`; counts, decision and outcome recorded.

Revision note (2026-11-19, Milestone 4 implemented): recorded the passed Milestone 2 software check and Milestone 3 checks, the `ValueText` component and the layout of the result block, counts and outcome.

Revision note (2026-11-19, Milestone 5 implemented): recorded the passed Milestone 4 checks, the document updates, the compliance findings, the accepted zero-value risk and the final counts; only the deferred equipment checks remain open.
