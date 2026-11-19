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
- [ ] Milestone 1: settings file, loading at start, saving on every accepted measuring TCP; sphere centre from the primary value.
- [ ] Milestone 1: build and disconnected check: not run (user's).
- [ ] Milestone 2: last calibration result recorded in the settings file; result window with Reassign and Cancel.
- [ ] Milestone 2: build, disconnected check and user-authorized calibration run: not run (user's).
- [ ] Milestone 3: probe model shifted by the ball centre.
- [ ] Milestone 3: build and disconnected check: not run (user's).
- [ ] Milestone 4: last calibration result in the Properties panel when nothing is selected.
- [ ] Milestone 4: build, `robocrap_qmllint` (USE_HOT_RELOAD=OFF) and disconnected check: not run (user's).
- [ ] Milestone 5: documents and contracts; compliance pass over the whole diff; final counts.


## Surprises & Discoveries

- Observation: the KRL program already takes its tool from the application. `controller/krl/rsi/calibration/RSI_Probe_Calibration.src` line 44 sets `$TOOL=ToolFrame`, and `ToolFrame` is line 2 of `C:\RoboCrapJobs\calibration_data.txt`, written by `writeCalibrationJob` in `src/application/scene/scenemachiningpreparation.cpp` from the measuring TCP of the sampled path. No KRL change is needed for "RSI_Probe_Calibration uses the latest primary centres"; generating the path from the primary ball centre is enough. TOOL_DATA[5] on the controller is used only for jogging and stays manual.
  Evidence: `RSI_Probe_Calibration.src:44`, `:125`; `RSI_Probe_Calibration_CONTRACT.md`, "calibration_data.txt", item 2.
- Observation: both end-effector models get the same transform, the flange matrix. `RobotController::drawingPose` sets `DrawingPose::tool = links.back()`, and `OccRobotAdapter::applyRobotTransforms` applies `pose.tool` to every tool part. The measuring model can therefore only be shifted with its own matrix.
- Observation: a `QtQuick.Dialogs` `MessageDialog` offers only standard buttons (Ok, Yes, Cancel, …), not a button labelled "Reassign"; an in-window `QtQuick.Controls` `Dialog` would be hidden behind the native 3D viewport window (`AGENT_CONTRACTS.md`, "Viewport and CAD": no Qt Quick item draws over `RobotViewport`). A separate top-level QML `Window` is not covered by the viewport.


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

  Lengths in mm. `primary` is written by Reassign; `primary.ballCentreF` is also written whenever the measuring TCP is applied in the panel (`source` then becomes `"manual"`, the sphere centre and contact distances stay); `lastCalibration` is written after every successful calibration fit. Every key is optional except `version`: a missing file or key means the built-in default (ball centre TOOL_DATA[5], sphere centre fitted from the jog touches, no contact distances, no last result). An unreadable or invalid file is reported through `SceneCalibration::error` and treated as missing; the application does not overwrite it until the next accepted change.
  Rationale: one source for the ball centre, the sphere centre, the contact distances (user decision 2026-11-19: Reassign also saves them) and the result shown in the Properties panel, readable without parsing `records/`.
  Date/Author: 2026-11-19, agent.
- Decision: `SceneCalibration` owns the settings file: it reads it in its constructor, writes it, and holds the primary sphere centre, the contact distances and the last result. Requirement: one owner must read and write the file so that writes of the ball centre, Reassign and the last result never overwrite each other. Reuse: `SceneCalibration` already owns the sphere and holds the `SceneEndEffectors` pointer; `SceneMachiningPreparation` already holds the `SceneCalibration` pointer; a new class would need both and a QML registration.
  Date/Author: 2026-11-19, agent.
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
- Decision: the probe model keeps its CAD frame (user decision 2026-11-19, second approach). In `EE_WP-500` the ball centre lies on the model's z axis at 127 mm. `RobotController` places the measuring model at the flange matrix times a translation by the primary ball centre minus (0; 0; 127) mm, expressed in {F}; the spindle model keeps the flange matrix. `DrawingPose` gains `M4d measuringTool`; `OccRobotAdapter` applies it to the measuring part (`RobotTool::Measuring`, the first tool) and `tool` to the spindle part. Requirement: the two models need different matrices; `DrawingPose::tool` is shared (Surprises & Discoveries).
  Date/Author: 2026-11-19, user and agent.
- Decision: the result after a successful run opens a new top-level QML `Window` (`src/application/qml/Views/Workspace/CalibrationResultWindow.qml`, modal, fixed size) with the result rows, the question and two `QxButton`s, Reassign and Cancel; a failed run keeps the existing OK-only `MessageDialog`. Requirement: a button labelled Reassign (not available in `MessageDialog`) in a window the native viewport cannot cover (Surprises & Discoveries).
  Date/Author: 2026-11-19, agent.
- Decision: the result rows move from `SceneMachiningPreparation` to `SceneCalibration`. `resultRows` and `setsOutsideBound` move with them; `SceneMachiningPreparation` keeps `calibrationRunError` and `calibrationRunFinished` but drops `calibrationResult` and `m_runResult`, and on success calls `SceneCalibration::recordCalibrationResult(fit, id)`. Main.qml reads `App.Calibration.lastCalibrationRows`. Rationale: one holder of the displayed result for the window, the Properties panel and the file (code reduction: no second copy of the rows).
  Date/Author: 2026-11-19, agent.
- Decision: postponed, not in this plan: Import Sphere from jog touches (with the TOOL_DATA[5] used while jogging recorded in the point file, so that the application can replace it by the primary ball centre), the table position in {B}, and any write of TOOL_DATA[5] on the controller.
  Rationale: user decisions 2026-11-19.
  Date/Author: 2026-11-19, user.


## Outcomes & Retrospective

Not started.


## Context and Orientation

The repository is a Windows Qt Quick application (C++17, Qt 6.8, CMake). The robot is a KUKA KR 10 with a KR C4 controller; KRL is its programming language. The measuring probe is a WP-500 with a ruby ball; the "measuring TCP" is the ball centre relative to the robot flange. {F} is the flange frame, {B} the robot base frame. The calibration sphere (Ø25 mm) stands on a strut on a table; a calibration run touches it 158 times (114 calibration, 44 verification touches) and fits the ball centre in {F}, the sphere centre in {B} and two contact distances by least squares (`fitProbeCalibration`, `src/trajectories/probe/probecalibrationfit.cpp`, result type `ProbeCalibrationFit` in `src/trajectories/include/trajectories/probecalibrationfit.h`).

`SceneEndEffectors` (`src/application/scene/sceneendeffectors.*`, QML singleton `App.EndEffectors`) holds `m_measuringTcp`, six values X, Y, Z (mm), A, B, C (deg), initialized to TOOL_DATA[5], and `m_measuringTcpFrame`, the 4×4 transform. `applyMeasuringTcp(pose)` accepts six finite values, ignores an unchanged pose and emits `measuringTcpChanged`. The Measuring End Effector panel (`src/application/qml/Views/Workspace/EndEffectorPanel.qml`, created in `Main.qml` around line 500) shows `App.EndEffectors.measuringTcp` and calls `applyMeasuringTcp` on Apply.

`SceneCalibration` (`src/application/scene/scenecalibration.*`, singleton `App.Calibration`) is constructed in `src/application/main.cpp` after `SceneEndEffectors`. On every entry into the Calibration submode `activate()` reads `<exe>/resources/json/calibration_sphere.json` with `readSphereFile` (sphere, strut and table geometry plus `roughPoints`, the ball centres of ten jog touches), fits a sphere through the points (`fitSphere`), and inserts a `SceneSphere` (`src/application/scene/scenegeometry.h`) with `center`, radii, RMS, points, strut, table and the stand model path (`CALIBRATION_STAND.brep`, origin at the sphere centre) through `replaceSphere`. `generate()` builds the touch plan from `sphere()->center` and the measuring TCP frame. A changed measuring TCP or sphere invalidates the path (`invalidatePath`).

`SceneMachiningPreparation` (`src/application/scene/scenemachiningpreparation.*`) runs the calibration: `endRun` writes `records/<id>/calibration_touches_<id>.csv`, fits, fills `m_runResult = resultRows(fit)` (QML property `calibrationResult`) and writes `records/<id>/calibration_result_<id>.csv`; `calibrationRunFinished` makes `Main.qml` (lines 104–121) open `calibrationResultDialog`, a `MessageDialog` with OK.

`RobotController` (`src/application/robot/robotcontroller.*`) builds `DrawingPose` (`src/viewport/include/viewport/occcontroller.h`: `links`, `tool`, optional `tcp`, `collet`) for the viewport; `tool` is the flange matrix used for both end-effector models (`EE_WP-500.brep` measuring, `EE_GDZ-65-800A.brep` spindle). `OccRobotAdapter::applyRobotTransforms` (`src/viewport/adapters/occrobotadapter.cpp`) sets each link part and each tool part transform.

`PropertiesPanel.qml` (`src/application/qml/Views/Workspace/`) shows the selected object; when nothing is selected it shows the label "Select an object." (or "N objects selected.") and hides its `ScrollView` content. Its label column width is computed once from a constant list of labels.


## Plan of Work

Milestone 1, the settings file. In the root `CMakeLists.txt`, next to `ROBOCRAP_RECORDS_DIR`, add the cache path `ROBOCRAP_SETTINGS_DIR` (default `${PROJECT_SOURCE_DIR}/settings`) and `add_compile_definitions(ROBOCRAP_SETTINGS_DIR="${ROBOCRAP_SETTINGS_DIR}")`. In `scenecalibration.cpp` (unnamed namespace) add the settings types and two functions: `struct PrimaryCentres { QString source; std::optional<V3d> ballCentre; std::optional<V3d> sphereCentre; std::optional<double> contactVertical; std::optional<double> contactHorizontal; };`, `struct CalibrationSummary` with the `lastCalibration` fields of the Decision Log, `struct ProbeSettings { PrimaryCentres primary; std::optional<CalibrationSummary> last; };`, `struct ProbeSettingsResult { std::optional<ProbeSettings> settings; QString error; };`, `ProbeSettingsResult readProbeSettings(const QString& path)` (a missing file returns empty settings without error) and `QString writeProbeSettings(const ProbeSettings& settings, const QString& path)` (`QSaveFile`, creates the folder, returns an empty string or the error). `SceneCalibration` gets the member `ProbeSettings m_settings`. Its constructor reads the file; if `primary.ballCentre` exists, it calls `m_effectors->applyMeasuringTcp` with that X, Y, Z and the current A, B, C before connecting `measuringTcpChanged`. A new private slot `onMeasuringTcpChanged()` (connected after the start load, together with the existing `invalidatePath` connection) stores X, Y, Z of `m_effectors->measuringTcp()` as `primary.ballCentre`, sets `primary.source` to `"manual"` and writes the file. `SceneSphere` gains `V3d roughCenter`; `readSphereFile` fills it with the jog fit centre and `center` with the same value; `activate()` then replaces `center` by `primary.sphereCentre` when saved, before `replaceSphere`. The Properties rows of the sphere (`SceneObject` properties for `Sphere`) label the jog fit centre "Fitted center (jog)" and the used centre "Center"; read `src/application/scene/sceneobject.cpp` for the row builder first.

Milestone 2, last result and Reassign. In `SceneCalibration` add `void recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id)` (fills `m_settings.last`, writes the file, emits `lastCalibrationChanged`), the property `Q_PROPERTY(QVariantList lastCalibrationRows READ lastCalibrationRows NOTIFY lastCalibrationChanged)` built by the moved `resultRows` from `CalibrationSummary` (move `resultRows`, `resultRow`, `vectorText` and `setsOutsideBound` from `scenemachiningpreparation.cpp`, adapting them to the summary), and `Q_INVOKABLE bool reassignPrimaryCentres()`: it requires `m_settings.last`, sets `primary` from it (`source` = its id), applies the ball centre to `m_effectors` (current A, B, C kept), writes the file, and if the Calibration submode is active replaces the sphere with the new centre (the stand moves with it; the path is invalidated). In `SceneMachiningPreparation::endRun` replace `m_runResult = resultRows(*result.fit)` by `m_calibration->recordCalibrationResult(*result.fit, id)`, delete `m_runResult`, the `calibrationResult` property and the moved helpers. Create `src/application/qml/Views/Workspace/CalibrationResultWindow.qml`: a `Window` with `modality: Qt.ApplicationModal`, fixed width and height, title "Calibration complete", the rows from a required `var rows` input as "label: value" lines, the question "Reassign the ball center and sphere center based on the calibration results?", and `QxButton`s Reassign and Cancel emitting `reassignRequested()` and closing; register the file in `src/application/CMakeLists.txt` like the other `Views/Workspace` QML files (both hot-reload branches). In `Main.qml`, a `Connections` on `App.Calibration` opens this window on `lastCalibrationChanged` (emitted only by `recordCalibrationResult`, that is after a successful fit; the start load in the constructor runs before QML exists), and the window shows `App.MachiningPreparation.calibrationRunError` below the rows when it is not empty (for example a result-file write error). `endRun` emits `calibrationRunFinished` after `recordCalibrationResult`, so `onCalibrationRunFinished` opens the existing OK-only `MessageDialog` "Calibration failed" only when the window is not open, that is when the run produced no fit; `onReassignRequested` calls `App.Calibration.reassignPrimaryCentres()`.

Milestone 3, the probe model. In `occcontroller.h` add `M4d measuringTool = M4d::Identity();` to `DrawingPose`; in `OccRobotAdapter::applyRobotTransforms` apply `pose.measuringTool` to the first tool part (`RobotTool::Measuring`) and `pose.tool` to the second; check the installation path that uses the HOME pose (`occrobotadapter.cpp` around line 47) for the same. In `RobotController::drawingPose` set `result.measuringTool = links.back() * translation(ball − (0; 0; 127))` with the ball centre from `m_endEffectors->measuringTcp()` and the constant `MeasuringModelBallCentre = (0; 0; 127)` mm in the unnamed namespace of `robotcontroller.cpp`, next to the `EE_WP-500.brep` path; connect `SceneEndEffectors::measuringTcpChanged` to a slot that reapplies the current pose through `m_viewport->applyRobotTransforms(drawingPose(*m_pose))` when the robot is ready.

Milestone 4, the Properties panel. Add to `PropertiesPanel.qml` a required `var calibrationRows` input and, when `selectionCount === 0`, a "Last calibration" block of `PropertyValue` rows (or the line "No calibration result." when the list is empty) in place of "Select an object."; with more than one object selected keep "N objects selected.". Put the block inside the existing `ScrollView` region so that switching between the block and an object's rows does not resize the panel, and add the result labels to the constant label list so the label column width does not change with the state. `Main.qml` passes `App.Calibration.lastCalibrationRows`.

Milestone 5, documents. Update in their owning places: `docs/code context/AGENT_CONTRACTS.md` "Build and modules" (`ROBOCRAP_SETTINGS_DIR`, `settings/` tracked), "Geometry and scene" (the measuring TCP is persisted in the settings file; the primary sphere centre; `SceneSphere::roughCenter`), "Viewport and CAD" (`DrawingPose::measuringTool`, the probe model offset), "Machining and playback" (the result goes to `SceneCalibration`; Reassign) and "QML" (the result window, the Properties panel without selection); `docs/code context/APP_DESCRIPTION.md` (Workflow step 1). Then the `AGENTS.md` compliance pass and final counts.


## Concrete Steps

Commands in Git Bash from the repository root. At the start of each milestone:

    git status --short
    wc -l <files of the milestone>

At the end of each milestone:

    git status --short
    git diff --check

To prepare a settings file for the disconnected checks of Milestones 2–4 without a robot run, the agent may write `settings/probe_calibration.json` by hand from `records/calibration_20261118_164135_671/calibration_result_calibration_20261118_164135_671.csv` (ball centre 0.023743, −0.056159, 126.970980; sphere centre 1054.185828, −331.936070, 454.188879; ρᵥ 13.844765; ρₕ 13.858389; χ95 0.111184; s_rep 0.010241; smallest singular value 2.040178; 114/44 touches; sets outside bound counted from the file's set rows) as `lastCalibration`, without `primary`.


## Validation and Acceptance

Milestone 1 (user builds and starts, no robot): change the measuring TCP Z in the Measuring End Effector panel to 126.9 and Apply; `settings/probe_calibration.json` now has `primary.ballCentreF` [−0.048, −0.097, 126.9] and `source` "manual"; after a restart the panel shows 126.9. Enter Calibration, Generate Path, RSI Sampling (RSI socket bound, no robot motion): line 2 of `C:\RoboCrapJobs\calibration_data.txt` starts with `-0.048000;-0.097000;126.900000`. Delete the settings file and restart: the panel shows TOOL_DATA[5] again.

Milestone 2: with the prepared `lastCalibration` (Concrete Steps) and the next user-authorized calibration run: after "program finished" the window "Calibration complete" lists the rows and the question; Cancel closes it and changes nothing; Reassign sets the panel to the run's ball centre, the settings file's `primary` to the run's values with `source` = the run id, and, in the Calibration submode, the stand to the new sphere centre (Properties of the sphere: "Center" = the run's sphere centre). A failed run still shows "Calibration failed" with OK.

Milestone 3: apply a measuring TCP with Z 137 (10 mm longer) in the panel: the probe model moves 10 mm along the flange axis and its ball sits at the TCP; apply Z 127 again: it returns. The spindle model does not move.

Milestone 4: with nothing selected, the Properties panel shows "Last calibration" and the rows of `lastCalibration`; selecting an object shows its properties; selecting two objects shows "2 objects selected."; the panel's width and the viewport size do not change in any of these states.

Milestone 5: `AGENT_CONTRACTS.md` no longer says the measuring TCP is not persisted; `git diff --check` is clean.


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

`src/application/scene/scenecalibration.h`: `void recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id);`, `Q_INVOKABLE bool reassignPrimaryCentres();`, `Q_PROPERTY(QVariantList lastCalibrationRows READ lastCalibrationRows NOTIFY lastCalibrationChanged)`, signal `lastCalibrationChanged()`, private slot `onMeasuringTcpChanged()`, member `ProbeSettings m_settings` (types in the unnamed namespace of the `.cpp` cannot be members; declare `ProbeSettings`, `PrimaryCentres` and `CalibrationSummary` in the header's private section or in `scenegeometry.h` next to `SceneSphere`, whichever adds fewer lines).

`src/application/scene/scenegeometry.h`: `SceneSphere::roughCenter` (included in `operator==`).

`src/application/scene/scenemachiningpreparation.h`: `calibrationResult` property and `m_runResult` removed.

`src/viewport/include/viewport/occcontroller.h`: `DrawingPose::measuringTool`.

`src/application/qml/Views/Workspace/CalibrationResultWindow.qml`: `required property var rows`, `signal reassignRequested()`.

`PropertiesPanel.qml`: `required property var calibrationRows`.

No new library or dependency.


Revision note (2026-11-19): first version, from the user's requirements and decisions of 2026-11-19 (settings file in the repository; Reassign also saves the contact distances; primary centres only from the 114 calibration touches; probe model kept in its CAD frame with the ball at (0; 0; 127) mm; Import Sphere, table position and TOOL_DATA writes postponed; RSI_Probe_Calibration keeps reading the tool from the job file).
