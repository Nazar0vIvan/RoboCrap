# Probe sphere calibration run: start, progress and ball-centre estimation in RoboCrap

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`,
`Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is
maintained in accordance with `PLANS.md` at the repository root.

Four rules override `PLANS.md` for this plan. First, the implementing agent never runs a build,
CMake configure, QML lint, the application or any test unless the user asks for that run
(`AGENTS.md`, "Validation and handoff"); where a milestone names a build or a run, the agent records
it as "not run" in `Progress` and continues. Second, the agent never moves the robot, never changes
controller settings and never starts a KRL program: the robot run in Milestone 5 is done by the user
and needs the user's explicit authorization for that run (`AGENTS.md`, "Live PLC/FTS/RSI connections,
outputs, sensor bias or motion require explicit authorization"). Third, no git staging or commits
unless the user asks. Fourth, no project tests or test harnesses (`AGENTS.md`). Every `AGENTS.md`
rule applies to every line written for this plan, in particular "Mandatory C++ style" (no lambdas,
no project templates, results through return values, at most four parameters) and "Mandatory
interface stability".

This plan builds on two checked-in plans whose work is complete: `plans/PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md`
(RSI Sampling of the calibration path, the KRL program `RSI_Probe_Calibration`, packet captures) and
`plans/PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md` (starting KRL programs 1–4 through the PLC in Automatic
External). This plan does not change either.


## Purpose / Big Picture

Today a probe sphere calibration needs four separate actions and an external script. In the
application (Measuring mode, Calibration submode) the user generates the calibration path and runs
"RSI Sampling"; then the user opens the PLC tool window and presses "Run" for program 1; the robot
touches the 25 mm calibration sphere 158 times in about 17 minutes while the status bar shows nothing
about it; afterwards `python/rsi_touches.py` extracts the joint angles of every touch from the packet
captures, and nothing computes the result.

After this plan the user presses one toolbar button, "Start Calibration", next to "RSI Sampling".
The status bar shows the progress of the run as a percentage ("Calibration run: 37 % · 58/158
touches") and, on its left, a round indicator labelled "AUT EXT" that is green while the robot
controller is in Automatic External mode and gray otherwise. When program 1 finishes, the
application computes, from the joint angles the controller reported at every touch, the centre of the
WP-500 ruby ball in the flange frame, the centre of the calibration sphere in the robot base frame,
the two contact distances and the 95 % quantile of the verification residuals, and shows them in a
modal window "Calibration complete" with an "OK" button. The result is only displayed (and saved to
a file in the run's record folder); it is not applied as the measuring TCP.

Terms used throughout. The robot is a KUKA KR 10 R1420 with a KR C4 controller. KRL is the robot's
programming language. RSI (KUKA RobotSensorInterface) is a controller option that exchanges one XML
packet with the PC every 4 ms while a KRL program runs a sensor-guided motion; the PC answers each
packet with a position correction. The PC side of RSI is the class `RsiDevice`. A "touch" is one
search motion of the probe toward the sphere that the controller stops when the probe signal
`$IN[8]` switches on. TCP is the tool center point; for the measuring tool it is the ruby ball
centre. Frames: {B} is the robot base frame (equal to KUKA WORLD here), {F} is the flange frame.
Automatic External ("AUT EXT") is the controller mode in which an external device, here the Delta
PLC, starts programs; RoboCrap commands the PLC over TCP (`PlcDevice`).


## Progress

- [x] (2026-11-18) Plan written after reading `RsiDevice` (header and source), `PlcDevice`
  (program state), `RobotStartPanel.qml`, `SceneCalibration`, `SceneMachiningPreparation`,
  `probetouchplan.h`, `probetouchmotion.h`, `probersistream.h`/`.cpp`, `Kr10Kinematics`,
  `timedmotion.h`, `main.cpp`, `DeviceRunner`, `Main.qml` (status bar, dialogs), `ModeToolBar.qml`,
  `QxLed.qml`, `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md`,
  `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_THEORY_RU.md` and
  `resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md`.
- [x] (2026-11-18) Start of implementation, counts (physical / nonblank lines):
  `rsidevice.cpp` 369/343, `rsidevice.h` 95/80, `scenemachiningpreparation.h` 94/85,
  `scenemachiningpreparation.cpp` 374/342, `main.cpp` 163/121, `devicerunner.h` 48/37,
  `devicerunner.cpp` 61/48, `src/trajectories/CMakeLists.txt` 30/30, `Main.qml` 489/430,
  `ModeToolBar.qml` 239/203.
- [x] (2026-11-18) Milestone 1, code: `src/trajectories/include/trajectories/probecalibrationfit.h`
  (54/46 lines, new) and `src/trajectories/probe/probecalibrationfit.cpp` (267/242, new);
  `src/trajectories/CMakeLists.txt` 30 → 32 lines. Compliance pass: no lambdas, templates or
  exceptions; every function has at most three parameters (`findSet` reduced from four by passing
  the set key, a `ProbeResidualSet`, instead of group, kind and value); results only through return
  values; the solver borrows the model owned by the input motion. Growth is the new feature; the
  calibration and verification fits share one Gauss–Newton function.
- [x] (2026-11-18) Milestone 1, reference values for sessions `calibration_20261117_113235_576` and
  `calibration_20261114_170013_165` computed offline (Artifacts and Notes).
- [x] (2026-11-18) Milestone 1, build of `RoboCrapTrajectories`: passed (user).
- [x] (2026-11-18) Milestone 2, code: `decodeRequest` reads `Touch` and `AIPos` (new helper
  `decodeAxes`); `Request::touch`, `Request::joints`; signal `touchDetected(int, const V6d&)`
  emitted after the reply and the capture rows; `reportsTouch`; `m_reportedTouch` reset by every
  Start = 0 packet and by `setCorrections`; `main.cpp` registers `V6d`. Counts: `rsidevice.cpp`
  369/343 → 403/375, `rsidevice.h` 95/80 → 105/89, `main.cpp` 163/121 → 164/122. Compliance pass: no
  lambdas or exceptions; the new functions take one parameter; results through return values;
  before the reply only the reset assignment runs (no console output, file I/O or numerics). The
  connection to `SceneMachiningPreparation::recordTouch` moved to Milestone 3, where the slot is
  created, so that Milestone 2 builds on its own.
- [x] (2026-11-18) Milestone 2, build of `RoboCrapDevices` and `robocrap`: passed (user).
- [x] (2026-11-18) Milestone 3, code: `DeviceRunner::dataUpdated`; in `SceneMachiningPreparation`
  `attachPlc`, `canStartCalibration`, `startCalibration`, `recordTouch`, `onPlcData`, `onPlcSocket`,
  `endRun`, the run phase `CalibrationRun`, `PreparedCalibration::diagnosticId`, the six properties
  and two signals; result rows, console line and `records/<id>/calibration_result_<id>.csv`; RSI
  Sampling and the test LIN are refused while program 1 runs; `main.cpp` calls `attachPlc` and
  connects `touchDetected` to `recordTouch`. Counts: `scenemachiningpreparation.h` 94/85 → 139/127,
  `.cpp` 374/342 → 577/529, `devicerunner.h` 48/37 → 50/39, `devicerunner.cpp` 61/48 → 62/49,
  `main.cpp` 164/122 → 168/126. Compliance pass: no lambdas, templates or exceptions; at most two
  parameters per new function; results through return values; other objects' state only through
  their public methods (`DeviceRunner::data`, `isConnected`, `invoke`). Growth: the run logic is
  about 110 lines and the result rows and file about 90; the smaller alternative without the result
  file was not taken because the user accepted the recommendation that included it.
- [x] (2026-11-18) Builds after Milestones 1 and 2 passed (user); the build after Milestone 3 was
  running when Milestone 4 started.
- [x] (2026-11-18) Milestone 4, code: `ModeToolBar.qml` property `calibrationStartAvailable`,
  signal `calibrationStartRequested`, tool "Start Calibration" (Calibration submode only, after
  "RSI Sampling", icon `run.svg`); `Main.qml` binds it to `canStartCalibration` and the bound RSI
  socket and calls `startCalibration()`; status bar: `plc` runner, `autExt` (X1.0), a fixed `Row` with
  `QxLed` (light green or light gray) and "AUT EXT" after the mode label, the run progress as the
  first branch of `message`, `errorShown` false during a run; `calibrationResultDialog`
  (`MessageDialog`, OK) opened by `onCalibrationRunFinished`, titled "Calibration complete" when
  result rows exist and "Calibration failed" otherwise, text one "label: value" line per row plus
  the run error. Counts: `Main.qml` 489/430 → 543/479, `ModeToolBar.qml` 239/203 → 250/213. Review:
  the indicator row has constant content and sits in the status bar's `RowLayout` with
  `Layout.alignment` only; the progress replaces the text of the existing label; the dialog is a
  separate window; the new tool appears only on a submode change. No layout shift in ordinary state
  changes.
- [x] Milestone 4, build of `robocrap` and `robocrap_qmllint` (USE_HOT_RELOAD=OFF), disconnected
  runtime check of the Calibration submode: superseded (2026-11-21). The build and the disconnected
  check passed on 2026-11-18 (entries below); `all_qmllint` with USE_HOT_RELOAD OFF passed after
  `plans/review_2026-11-20_0cd8996/03_UI_LAYOUT_STABILITY_EXECPLAN.md` and `07_APPLICATION_QML_CLEANUP_EXECPLAN.md`
  (user reports 2026-11-20 and 2026-11-21), on QML that includes this milestone's files.
- [x] (2026-11-18) Build of the whole project after Milestone 4 passed (user).
- [x] (2026-11-18) Milestone 5, documentation: `docs/code context/AGENT_CONTRACTS.md` "Devices and
  protocols" (RSI `Touch`/`AIPos` decoding, `touchDetected` once per search after the reply),
  "Machining and playback" (calibration run bullet), "QML" (Start Calibration tool, AUT EXT
  indicator, progress, result window); `docs/code context/APP_DESCRIPTION.md` Workflow step 1;
  `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md` section 1 (К6, В3, В4 implemented,
  К7 reported only); `resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md` ("AUT EXT
  start", "PC behavior").
- [x] (2026-11-18) Milestone 5, final compliance pass over the whole diff (12 tracked files, 390
  insertions, 16 deletions, plus the two new fit files): no C++ lambdas, templates, exceptions or
  output parameters; no function with more than three parameters; `git diff --check` clean.
- [x] (2026-11-18) Milestone 5, disconnected runtime check of the Calibration submode (Validation
  and Acceptance, first paragraph): passed (user).
- [x] (2026-11-18) Milestone 5, user-authorized robot run, session `calibration_20261118_113045_223`:
  "Calibration complete", result file written, values within the reference magnitudes (Artifacts
  and Notes) (user).
- [x] Milestone 5, reproduction of the displayed values within 1e-4 mm by `python/rsi_touches.py`
  and the Milestone 1 reference script: met by its replacement (2026-11-21). Both scripts were
  replaced by `python/calibration/calibration_fit.py`, whose output for this run,
  `calibration_20261118_113045_223`, equals the application's `calibration_result_…csv` line for line
  (`plans/RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md`, Progress, Milestone 4, 2026-11-18): a stricter
  match than 1e-4 mm.


## Surprises & Discoveries

- Observation: the controller already sends everything the estimation needs in every packet; the PC
  currently ignores it. A received packet of session `calibration_20261117_113235_576` (decoded from
  `records/calibration_20261117_113235_576/rsi_packets_20261117_113235_576.csv` beside the
  executable):

      <Rob Type="KUKA"><RIst X="1054.143188" .../><AIPos A1="17.476164" A2="-54.583924"
      A3="80.662037" A4="0.000058" A5="63.921136" A6="17.476263"/><MACur .../><Delay D="0"/>
      <Start>1</Start><Result>0</Result><TouchIndex>1</TouchIndex><Touch>1</Touch>
      <IPOC>4977706</IPOC></Rob>

- Observation: reading the packet capture files after a run is slow; one session's captures are
  116 MB with 268,572 rows, of which 267,624 are Base64 XML packets; decoding them read-only in
  Python took 4.4 s. This is why the detections are collected live (Decision Log).
- Observation: the PC cannot distinguish T1, T2 and AUT. The mode outputs `$T1`, `$T2`, `$AUT`,
  `$EXT` are internal outputs 993–996, not wired to the EtherCAT output module; the only mode signal
  reaching the PLC is X1.0 (`$I_O_ACTCONF`, "Automatic External interface active")
  (`docs/hardware context/HARDWARE_SETUP.md`, "KR C4 Automatic External configuration").
- Observation: C++ cannot observe the PLC state through `DeviceRunner::data()`: the runner fills
  its `QQmlPropertyMap` with `insert()` (`src/application/devices/devicerunner.cpp`,
  `DeviceRunner::onStateReady`), and Qt emits `QQmlPropertyMap::valueChanged` only for changes made
  from QML, not for `insert()`. Milestone 3 therefore adds a runner signal (Decision Log).
- Observation: the nominal forward kinematics reproduces the controller's own Cartesian pose. For
  touch 1 of session `calibration_20261117_113235_576`, FK(`AIPos`)·TOOL_DATA[5] =
  [1054.1432; -331.9701; 467.9593] mm, and the packet's `RIst` is X 1054.143188, Y -331.970093,
  Z 467.959290: equal within 0.0001 mm. The PC model and the controller model agree, and the
  measuring TCP of the run is TOOL_DATA[5].
- Observation: the two recorded sessions give the same ball centre within 0.002 mm, but the base
  model leaves systematic residuals: chi95 is 0.11 mm while s_rep is 0.011–0.015 mm, and 22 and 23
  of the 36 step-К7 sets have a mean residual outside 3·s_rep/√m (Artifacts and Notes). This plan only
  reports these sets (Decision Log); a model extension of step К7 is a separate decision.
- Observation: `python/probe_sphere_calibration/` (with `kr10.py`) was deleted from the working tree
  by the user on 2026-11-18. The reference script takes the joint table of
  `src/application/robot/robotdefinition.cpp` (`Joints`) directly; the table equals the one in
  `kr10.py` of git `HEAD`, and its FK is the same product as `Kr10Kinematics::solveFK`
  (local transform, then rotation about the signed z axis by q − qHome).


## Decision Log

- Decision: show only AUT EXT on / off, as a round indicator, instead of the four modes.
  Rationale: four modes need two more wired signals and a PLC program change; the user chose the
  indicator that the existing wiring supports (user, 2026-11-18).
  Date/Author: 2026-11-18, user and agent.
- Decision: "Start Calibration" sends the existing PLC command "run program 1"; it does not run RSI
  Sampling itself and is enabled only when the sampled calibration job is held.
  Rationale: program 1 of CELL is `RSI_Probe_Calibration` (`resources/files/AutExt/cell.src`); RSI
  Sampling stays a separate, visible step (user, 2026-11-18).
  Date/Author: 2026-11-18, user and agent.
- Decision: progress is the number of touches detected so far divided by the touch count of the
  held job, shown in percent (user, 2026-11-18). The KRL step number is not transmitted to the PC.
  Date/Author: 2026-11-18, user and agent.
- Decision: the run ends when the PLC reports program state "finished" after it reported "running"
  for this run; "interrupted" keeps the run open (it can be resumed from the PLC panel).
  Rationale: `PlcDevice` already derives these states from the Automatic External outputs.
  Date/Author: 2026-11-18, agent, accepted by the user.
- Decision: the joint angles of every touch are collected live: `RsiDevice` remembers nothing new
  but emits one queued signal per detected touch (touch number, `AIPos`); the application stores the
  158 detections and computes on the GUI thread when the run ends. No worker thread, no file parsing.
  Rationale: the socket thread (`ControlIO`) also runs `FtsDevice`, must not do numerical work
  (`docs/code context/AGENT_CONTRACTS.md`, "Machining and playback", RSI: "Keep numerical work, file
  I/O … out of ControlIO receive callbacks"), and `RoboCrapDevices` must not depend on
  `RoboCrapTrajectories` ("Build and modules"); parsing 116 MB of captures would block the GUI
  thread for seconds, whereas the fit of 158 touches takes milliseconds. Cost: a run cannot be
  recomputed after an application restart; `python/rsi_touches.py` still reads past sessions.
  Date/Author: 2026-11-18, agent, accepted by the user.
- Decision: computed quantities are the base model of the method document, section 5: ball centre in
  {F}, sphere centre in {B}, contact distances of the vertical and horizontal stylus classes,
  repeatability from the 57 repeated pairs, the smallest singular value of the Jacobian, the sphere
  centre from the verification touches, the 95 % quantile, and the step К7 report (mean residuals per
  tilt ring and per roll, with their bounds). No model extension of step К7.
  Rationale: user accepted the recommendation (2026-11-18).
  Date/Author: 2026-11-18, user and agent.
- Decision: the result is displayed in the window and written to `records/<id>/calibration_result_<id>.csv`;
  it is not applied as the measuring TCP (user, 2026-11-18).
  Date/Author: 2026-11-18, user and agent.
- Decision: the window opens also when the run fails, titled "Calibration failed", with the reason.
  Rationale: after a 17-minute unattended run a status-bar line alone is easy to miss; the same
  window component serves both outcomes.
  Date/Author: 2026-11-18, agent.
- Decision: the run coordination lives in the existing `SceneMachiningPreparation`, not in a new class.
  Requirement: the run needs the held calibration job (its motion, touch count and diagnostic
  identifier), which only `SceneMachiningPreparation` owns. Reuse: a new class would need the same
  job through new accessors, its own QML singleton registration in `src/application/backendqmltypes.h`
  and construction in `main.cpp`, which is more code for the same state.
  Date/Author: 2026-11-18, agent.
- Decision: the numerics are a free function in a new file pair of `RoboCrapTrajectories`
  (`src/trajectories/probe/probecalibrationfit.cpp`, published header
  `src/trajectories/include/trajectories/probecalibrationfit.h`). Requirement: the fit needs the
  saved touch plan and the kinematic model, both published by `RoboCrapTrajectories`/`Kinematics`.
  Reuse: no existing function solves this least-squares problem; `fitSphere`
  (`src/geometry/spherefit.cpp`) fits only a sphere through points and cannot estimate the ball
  centre in {F}.
  Date/Author: 2026-11-18, agent.
- Decision: the AUT EXT state for C++ comes from the PLC runner attached with a setter
  (`SceneMachiningPreparation::attachPlc`), like `ScenePlayback`'s collaborator attached in
  `main.cpp`. Requirement: the start conditions and the end of the run depend on PLC state.
  Reuse: the constructor already takes three parameters; a fourth would need the justification of
  `AGENTS.md`, and the runner exists only after `DeviceHub` construction, so a setter is the smaller
  change.
  Date/Author: 2026-11-18, agent.
- Decision: `DeviceRunner` gains the signal `dataUpdated(const QVariantHash& values)`, emitted at the
  end of `onStateReady` after `insert()`. Requirement: `SceneMachiningPreparation` must react to PLC
  state changes on the GUI thread. Reuse fails: `QQmlPropertyMap::valueChanged` is not emitted for
  `insert()` (Surprises & Discoveries), and connecting `PlcDevice::stateReady` directly would add a
  second cross-thread path beside the runner, which `AGENT_CONTRACTS.md` names as the one publication
  path.
  Date/Author: 2026-11-18, agent.
- Decision: new fields. `RsiDevice::Request::touch` and `::joints` carry the decoded `Touch` and
  `AIPos`, which no existing field holds. `RsiDevice::m_reportedTouch` makes the device emit once per
  touch: `Touch` stays 1 for many 4 ms packets while the stylus is deflected, and without it every
  such packet would cross to the GUI thread. `PreparedCalibration::diagnosticId` names the record
  folder of the result file; the held job stores no identifier today. The run members of
  `SceneMachiningPreparation` (detections, active flag, saw-running flag, error, result rows) are the
  run's own state; no existing member holds it.
  Date/Author: 2026-11-18, agent.
- Decision: `m_reportedTouch` is also reset to 0 by every Start = 0 packet, not only by a new table.
  Rationale: KRL sends three Start = 0 packets before every search
  (`resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md`, "Verified on the
  controller"); without the reset, rerunning a job whose last reported touch equals the next
  search's touch number (for example a `StepLimit` run of touch 1 followed by a full run) would
  suppress that touch's report. With it the rule is exactly "once per search".
  Date/Author: 2026-11-18, agent.
- Decision: the run is tracked by one phase `CalibrationRun { Idle, Requested, Starting, Started }`
  instead of the flags `m_runActive` and `m_runSawRunning`. Rationale: `PlcDevice::publishIo` sends
  `robotResult` with every 2 s heartbeat SNAPSHOT, so an old TIMEOUT from an earlier start would end
  a new run at once if a start failure were taken from that key alone. A start failure is now taken
  only when this run's start sequence (`robotStep` above IDLE, phase Starting) returns to IDLE
  without program 1 running; a sequence that returns with STARTED also counts as running, because
  the PLC's running change frame can be lost (`docs/code context/ARCHITECTURE.md`, open question
  "PLC change frames"). The success end needs phase Started and `robotProgramState` finished, which
  `PlcDevice` publishes only on a change.
  Date/Author: 2026-11-18, agent.
- Decision: while program 1 runs (phase Started), RSI Sampling, chamfer Sampling and the test LIN are
  refused ("Calibration run in progress."); in phases Requested and Starting they stay possible and
  end the run with "The calibration job was replaced.". Rationale: a new Sampling replaces the stream
  table the robot is using; before the program runs, the same action is the only way out of a run
  whose start the PLC never began.
  Date/Author: 2026-11-18, agent.
- Decision: `calibrationTouchCount` is notified by `preparedChanged` and `canStartCalibration` by
  `availabilityChanged`, the other run properties by `calibrationRunChanged`; the result file is a
  comma-separated record like the other files in `records/<id>/` (rows `quantity,value_1,value_2,value_3`,
  then `set,group,value_deg,mean_mm,bound_mm,count`). Rationale: each property has one notifier,
  and the other record files use commas.
  Date/Author: 2026-11-18, agent.
- Decision: the status bar shows "Calibration run: P % · D/N touches"; `SceneMachiningPreparation`
  exposes `calibrationDetected` (D) and `calibrationTouchCount` (N) and QML computes P =
  floor(100·D/N). Rationale: two integer properties give both the percentage the user asked for and
  the counts, without a third, derived property.
  Date/Author: 2026-11-18, agent.


## Outcomes & Retrospective

Milestone 1 (2026-11-18): the fit exists as `fitProbeCalibration`. The offline reference for the
two recorded sessions gives a ball centre of about [0.024; -0.056; 126.968] mm in {F} and chi95 of
about 0.11 mm.

Milestones 2–5 (2026-11-18): all code and documentation are in place and the project builds. The
disconnected UI check passed, and the robot run `calibration_20261118_113045_223` ended with
"Calibration complete": its ball centre in {F} differs from both offline references by at most
0.0014 mm per coordinate, chi95 is 0.1107 mm and 23 of 36 step-К7 sets lie outside their bound, as
in the recorded sessions. Not done: the 1e-4 mm reproduction by an offline fit of the same run. Lessons:
the PLC state could not be observed through `QQmlPropertyMap::valueChanged`, and the heartbeat
repetition of `robotResult` would have ended runs falsely; both were found by reading
`DeviceRunner` and `PlcDevice::publishIo` before writing the run logic.


## Context and Orientation

The repository is a Windows Qt Quick application (C++17, Qt 6.8, CMake, bundled Eigen). The
calibration-related parts and how they fit together:

The touch plan and motion. `src/trajectories/probe/probetouchplan.cpp` (header
`src/trajectories/include/trajectories/probetouchplan.h`) builds `ProbeTouchPlan`: a list of
`ProbeTouchPass` (purpose Calibration or Verification, `group` 1–3, `repetition`, and the pass's
`ProbeTouch` entries with `tilt` τ and `roll` ψ in degrees and the outward sphere `normal`).
Group 1 (G1) has the stylus down, group 2 (G2) along +X, group 3 (G3) down and turned 180° about the
stylus. The current plan has six calibration passes (G1, G2, G3, G3, G2, G1; 19 touches each, the
second pass of a group in reverse order, 114 touches) and two verification passes (G1 with 25, G2
with 19 touches, 44 touches). `src/trajectories/probe/probetouchmotion.cpp` turns the plan into a
timed robot motion (`ProbeTouchMotion`), keeps the plan (`plan()`) and the robot setup (`robot()`:
`ChamferRobotSetup{model, flangeToTcp, timing}` from `src/trajectories/include/trajectories/timedmotion.h`).
`probeMotionSteps(plan)` lists the motion steps; every step with segment `ProbeSegment::Approach`
is one touch, and the k-th Approach step in that list is global touch number k (1–158). The RSI job
builder `sampleProbeCalibrationRsi` (`src/trajectories/probe/probersistream.cpp`) numbers touches
the same way: it appends one stream per Approach step, stream index = touch number, index 0 empty.

The scene side. `src/application/scene/scenecalibration.*` (`SceneCalibration`, QML singleton
`App.Calibration`) owns the sphere and the generated calibration path (`path()` returns the
`SceneTouchPath` whose `motion` is a `std::shared_ptr<const ProbeTouchMotion>`).
`src/application/scene/scenemachiningpreparation.*` (`SceneMachiningPreparation`, QML singleton
`App.MachiningPreparation`) implements "RSI Sampling": `prepareCalibration()` runs
`sampleProbeCalibrationRsi` in a `std::async` worker polled every 30 ms, writes
`C:/RoboCrapJobs/calibration_data.txt` and emits `correctionsReady(streams, diagnosticId)`, which
`src/application/main.cpp` connects (queued) to `RsiDevice::setCorrections`. The held job is
`m_preparedCalibration` (`PreparedCalibration{motion, touchCount, stepCount}`); `clearPrepared()`
drops it whenever another Sampling starts or the path changes. The diagnostic identifier is
`calibration_<UTC timestamp>` and names the record folder `records/<id>/` beside the executable.

The devices. `src/devices/rsi/rsidevice.cpp` (`RsiDevice`) runs on the I/O thread `ControlIO`
(shared with the force sensor `FtsDevice`). `onReadyRead()` reads all pending datagrams, decodes each
with `decodeRequest()` (today only IPOC, Start, Result, TouchIndex), answers only the newest, and
appends every packet to the capture. No console output or other work may happen before the reply is
written. `src/devices/plc/plcdevice.cpp` (`PlcDevice`, thread `GeneralIO`) publishes PLC state keys
through `stateReady` into the GUI-thread proxy `DeviceRunner` (`src/application/devices/devicerunner.h`),
whose `data()` is a `QQmlPropertyMap`. Keys used here: `x1` (list of 8 bools: index 0 = X1.0
`$I_O_ACTCONF` = AUT EXT active, index 6 = APPL_RUN, index 7 = `$IN_HOME`), `robotStep` (0 = IDLE),
`robotResult` (0 "-", 1 STARTED, 2 RESUMED, 3 TIMEOUT, 4 ABORTED, 5 AUT EXT LOST) and
`robotProgramState` (0 unknown, 1 running, 2 finished, 3 interrupted). `DeviceRunner::invoke("runRobotProgram",
{{"program", 1}})` starts program 1 through the PLC. The PLC panel's Run button
(`src/application/qml/Views/Dashboard/DeltaPanel/RobotStartPanel.qml`) is enabled when
`robotStep === 0 && x1[0] && x1[7] && !x1[6]`; those are the PLC's own start checks. `DeviceHub::device(key)`
(`src/application/devices/devicehub.h`) returns the runner for "plc" or "rsi".

Kinematics. `Kr10Kinematics` (`src/kinematics/include/kinematics/kr10kinematics.h`) is constructed
from a `Kr10KinematicModel` (it stores a reference, so the model must outlive it); `solveFK(q)`
takes the six joint angles in degrees (`V6d`) and returns seven 4×4 link transforms in {B}; the last
is the flange pose.

The QML. `src/application/qml/Main.qml` holds the status bar (`footer: ToolBar { id: statusBar }`,
a `QxHField` with a fixed-width mode label and the right-aligned `statusMessage` label whose text
is `statusBar.message`) and already uses `QtQuick.Dialogs` `MessageDialog` (`importError`).
`src/application/qml/Views/Workspace/ModeToolBar.qml` holds the toolbar tools (`Tool` component with
`text`, `iconSource`, `available`, `onTriggered`); in the Calibration submode the last tool is
"RSI Sampling". `qml/Modules/Components/QxLed.qml` is a round indicator (`isOn`, `ledColor`,
`diameter`).

The mathematics (method document `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md`,
section 5), restated. For touch i let q_i be the six `AIPos` angles (degrees) of the first packet of
that touch's search in which `Touch` is 1. Let p_F(q_i) and R_F(q_i) be the flange position (mm) and
rotation in {B} from `solveFK`. The unknowns are the vector x of 8 numbers: the ball centre b in {F}
(3, mm), the sphere centre c in {B} (3, mm), and two contact distances rho_v and rho_h (mm), one for
touches whose stylus points down (groups 1 and 3, class "v") and one for touches whose stylus is
horizontal (group 2, class "h"). The calculated ball centre is r_i = p_F(q_i) + R_F(q_i) b, and the
residual of calibration touch i is e_i(x) = |r_i − c| − rho_o(i). The estimate minimizes the sum of
e_i² over the 114 calibration touches by Gauss–Newton: with u_i = (r_i − c)/|r_i − c| the Jacobian
row is [u_iᵀ R_F(q_i), −u_iᵀ, −1 in the column of the touch's class, 0 in the other]. Start values:
b = translation of the path's measuring TCP (`motion.robot().flangeToTcp`, TOOL_DATA[5] by default),
c = the plan's sphere centre (`plan().parameters.sphereCentre`, the fit of the manual jog touches),
rho_v = rho_h = `plan().parameters.contactDistance()` (14 mm). Repeatability: every calibration
direction is touched twice in the same group (same group, tilt and roll); with d the difference of
the two residuals of a pair at the estimate, s_rep = sqrt(Σ d² / (2·57)). Verification: with b and
rho fixed, the sphere centre c_ver is fitted again from the 44 verification touches (3 unknowns,
residual |r_i − c_ver| − rho_o(i)); the quantile chi95 is the value number ceil(0.95·44) = 42 of the
44 absolute verification residuals sorted ascending. Step К7 report: for each group and tilt ring,
and for each group and roll, the mean calibration residual of that set and its bound 3·s_rep/sqrt(m),
m being the number of touches in the set.


## Plan of Work

Milestone 1 adds the numerics. Create `src/trajectories/include/trajectories/probecalibrationfit.h`
and `src/trajectories/probe/probecalibrationfit.cpp` and add the source to `src/trajectories/CMakeLists.txt`
next to `probe/probersistream.cpp` (read that file first; the header directory is already exported).
The function `fitProbeCalibration(const ProbeTouchMotion& motion, const QVector<ProbeTouchDetection>& detections)`
returns `ProbeCalibrationFitResult` (types in "Interfaces and Dependencies"). It first maps touch
numbers to plan touches by walking `probeMotionSteps(motion.plan())` and numbering the Approach steps
1, 2, …; each entry gives the pass (purpose, group) and the `ProbeTouch` (tilt, roll). It rejects,
with an error string and no result: a detection whose touch number is not in 1…touch count, a touch
number detected twice, non-finite angles, and any missing calibration or verification touch ("N of
158 touches detected; the calibration needs all of them."). It builds one `Kr10Kinematics` from
`motion.robot().model` (the solver stores a reference; the input motion owns the model and outlives
the call, as in `probersistream.cpp`) and computes every flange pose once. Gauss–Newton: build
the 114×8 Jacobian and the residual vector, solve the step with `Eigen::JacobiSVD` (thin U and V) as
the least-squares solution of J·dx = −e, stop when the largest |dx| is below 1e-9 mm, fail after 50
iterations or on a non-finite value ("The calibration fit did not converge."), and fail if the
singular-value rank of J is below 8 ("The calibration parameters are not identifiable."). Keep the
smallest singular value for the report. The verification fit is the same loop with 3 unknowns
(c_ver) started at the calibration's c. Then compute s_rep, chi95 and the step К7 sets as described
in "Context and Orientation". Helpers are named functions in the file's unnamed namespace; no
lambdas. The function is pure: it has no Qt object, file or console side effects.

Milestone 1 also produces reference values without adding anything to the project: in the session
scratchpad (not in the repository) write a one-off Python script that reads
`records/calibration_20261117_113235_576/rsi_touches_20261117_113235_576.csv` and
`records/calibration_20261117_113235_576/rsi_calibration_plan_20261117_113235_576.csv` (both beside
the executable in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug/`), implements the forward
kinematics from the joint table `Joints` of `src/application/robot/robotdefinition.cpp` (confirm it by
comparing FK(`AIPos`)·TOOL_DATA[5] of one touch with the `RIst` element of the same packet within
0.001 mm) and computes the same quantities. Record the numbers in `Artifacts and Notes`;
they are the expected magnitudes for Milestone 5. Run Python with `-I` (isolated mode, so no module
from the data folders is imported).

Milestone 2 changes `RsiDevice`. In `decodeRequest()` (unnamed namespace of
`src/devices/rsi/rsidevice.cpp`) also read the element `Touch` (true only for the text "1"; any other
text, including "not linked:<n>" of contexts without the probe, is false and does not invalidate the
packet, so the chamfer and test contexts are unaffected) and the empty element `AIPos` with
attributes A1…A6 (decimal numbers; when one is missing or not a number the request carries no
angles). Add to `RsiDevice::Request` the fields `bool touch = false;` and `std::optional<V6d> joints;`.
Add the signal `void touchDetected(int touch, const V6d& joints);` and the member
`qsizetype m_reportedTouch = 0;` (touch number whose detection was last reported; reset to 0 in
`setCorrections`). In `onReadyRead()`, while looping over the pending datagrams in arrival order,
remember the first decoded request that satisfies `start && touchIndex > 0 && touch && joints &&
touchIndex != m_reportedTouch`; after the reply has been written and the capture rows appended, set
`m_reportedTouch` to its touch index and emit `touchDetected`; every Start = 0 packet resets
`m_reportedTouch` to 0 (Decision Log). Nothing is emitted, printed or computed before the reply. In
`src/application/main.cpp` register `qRegisterMetaType<V6d>("V6d")` next to the existing
registrations (no registration of `V6d` existed).

Milestone 3 extends `SceneMachiningPreparation` (`src/application/scene/scenemachiningpreparation.*`)
and connects, in `src/application/main.cpp`, `RsiDevice::touchDetected` queued to
`SceneMachiningPreparation::recordTouch`.
Add `QString diagnosticId;` to `PreparedCalibration` and fill it in `finishCalibration()` from
`result.job->diagnosticId`. In `src/application/devices/devicerunner.h`/`.cpp` add the signal
`void dataUpdated(const QVariantHash& values);` and emit it at the end of `DeviceRunner::onStateReady`
after `m_data->insert(vals)`. Add `void attachPlc(DeviceRunner* plc);` (called once in `main.cpp`
after `DeviceHub` and the preparation exist, with `hub.device(QStringLiteral("plc"))`); it stores the
runner and connects `DeviceRunner::dataUpdated` to a private slot
`onPlcData(const QVariantHash& values)` and `DeviceRunner::socketStateChanged` to a private slot
`onPlcSocket()`. The accumulated PLC values are read from `m_plc->data()->value(key)`. Add the
invokable `Q_INVOKABLE void startCalibration();` and the read-only properties
`canStartCalibration` (bool), `calibrationRunning` (bool), `calibrationDetected` (int, touches
detected in the current run), `calibrationTouchCount` (int, touch count of the held job, 0 without
one), `calibrationRunError` (QString) and `calibrationResult` (QVariantList of `{label, value}` maps,
the rows of the result window), all notified by one new signal `calibrationRunChanged()`, plus the
signal `calibrationRunFinished()` that QML uses to open the window. `canStartCalibration()` is true
when a calibration job is held and current (`m_preparedCalibration` with `isCurrentCalibration`),
no Sampling and no run is active, the PLC runner is connected and its data satisfy the PLC's start
checks (`robotStep == 0`, `x1[0]`, `x1[7]`, `!x1[6]`). `startCalibration()` returns without effect when
`canStartCalibration()` is false; otherwise it clears the detections (`QVector<ProbeTouchDetection>
m_detections`) and the previous result, sets the run phase to Requested and invokes
`runRobotProgram` with program 1. `recordTouch(int touch, const V6d& joints)` appends a detection only
while a run is active and the touch number is not yet recorded, then emits
`calibrationRunChanged()`. `onPlcData` advances the phase and then emits `availabilityChanged()`:
in Started, `robotProgramState` 2 (finished) ends the run successfully; in Requested or Starting,
`robotProgramState` 1 (running), or the start sequence returning to IDLE with `robotResult` 1
(STARTED), moves to Started; `robotStep` above IDLE moves to Starting; in Starting, `robotStep`
back at IDLE with another `robotResult` ends the run with "Program 1 did not start: <name>." (names
of `RobotStartPanel.qml`). `onPlcSocket` emits `availabilityChanged()` and, when
the runner is no longer connected during a run, ends it with "PLC disconnected during the
calibration run."; `clearPrepared()`
ends a run with "The calibration job was replaced.". Ending successfully calls `fitProbeCalibration`
on the held motion and the detections; on success it fills `calibrationResult` (rows below), logs one
console line `Probe calibration result: ball centre [x; y; z] mm in {F}, sphere centre … , rho_v …,
rho_h …, chi95 … mm, s_rep … mm` and writes `records/<id>/calibration_result_<id>.csv` beside the
executable with `QSaveFile` (key;value rows, the step К7 sets as rows `ring;group;tilt;mean;bound;m`
and `roll;group;roll;mean;bound;m`); a write failure goes to `calibrationRunError` but keeps the
displayed result. On fit failure the error string becomes `calibrationRunError`. Every end emits
`calibrationRunChanged()` and `calibrationRunFinished()`. Result rows, four decimals, vectors as
`[x; y; z]` like the Properties panel: "Ball centre in {F}, mm", "Sphere centre in {B}, mm",
"Contact distance, stylus down, mm", "Contact distance, stylus horizontal, mm",
"Quantile χ95, mm", "Repeatability s_rep, mm", "Sphere centre from verification, mm",
"Smallest singular value", "Touches (calibration / verification)", "Model check: sets outside
3·s_rep/√m" (count out of total).

Milestone 4 changes QML. In `ModeToolBar.qml` add `property bool calibrationStartAvailable: false`
and `signal calibrationStartRequested()`, and a `Tool` after "RSI Sampling", visible only when
`root.calibration`, text "Start Calibration", icon `qrc:/pics/run.svg` (already used by "Start
Machining"), `preserveIconColors: true`, `available: root.calibrationStartAvailable`. In `Main.qml`
bind `calibrationStartAvailable: App.MachiningPreparation.canStartCalibration && statusBar.rsi &&
statusBar.rsi.isConnected` (the RSI socket must be bound, as for RSI Sampling) and call
`App.MachiningPreparation.startCalibration()` on the signal. In the status bar: add
`readonly property App.DeviceRunner plc: App.Hub.device("plc")`; make the first branch of
`statusBar.message` (before "Importing surface…") return `qsTr("Calibration run: %1 % · %2/%3
touches")` with P = `Math.floor(100 * calibrationDetected / calibrationTouchCount)`, D and N while
`App.MachiningPreparation.calibrationRunning`; add `readonly property bool autExt: !!statusBar.plc
&& statusBar.plc.isConnected && !!statusBar.plc.data.x1 && !!statusBar.plc.data.x1[0]`; add, as the
first child of the `QxHField` before `statusMessage`, a fixed `Row` with a `QxLed` (diameter
`Metrics.sz20`, `isOn: true`, `ledColor: statusBar.autExt ? "green" : "gray"`: `QxLed` lightens its
colour when on, so the indicator is light green or light gray) and a `Text` "AUT EXT" (`Fonts.body`,
`Colors.foreground.medium`); the row's content is constant, so it never changes width. Read `qml/Modules/Components/QxHField.qml`
first to place the row inside its layout correctly (it owns its children's geometry: use
`Layout.*`, no anchors). Add a `Dialogs.MessageDialog` with `id: calibrationResultDialog`,
`buttons: Dialogs.MessageDialog.Ok`, title "Calibration complete" or "Calibration failed", `text`
built from `calibrationResult` rows (label: value, one per line) or from `calibrationRunError`, opened
in a `Connections { target: App.MachiningPreparation; function onCalibrationRunFinished() {…} }`.

Milestone 5 updates documentation and verifies. Update, in their owning documents only:
`docs/code context/AGENT_CONTRACTS.md` "Devices and protocols" (RSI: `Touch`/`AIPos` decoding and
the per-touch `touchDetected` signal after the reply), "Machining and playback" (the calibration run:
start conditions, end rule, the estimate is displayed and written, not applied) and "QML" (the
Calibration toolbar gains "Start Calibration"; the status bar shows the run progress first and the
AUT EXT indicator); `docs/code context/APP_DESCRIPTION.md` Workflow step 1 (the least-squares solution
is implemented); `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md` section 1 (steps
К6, В3, В4 implemented, К7 only reported); `resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md`
"PC behavior" (the PC reports each detection). Then do the `AGENTS.md` compliance pass and the
validation below.


## Concrete Steps

All commands from the repository root `D:\Qt\QtProjects\RoboCrap` (or `E:\Qt\QtProjects\RoboCrap`)
in Git Bash. Before editing, record sizes:

    wc -l src/devices/rsi/rsidevice.cpp src/devices/include/devices/rsi/rsidevice.h \
      src/application/scene/scenemachiningpreparation.* src/application/main.cpp \
      src/application/devices/devicerunner.h src/application/devices/devicerunner.cpp \
      src/trajectories/CMakeLists.txt src/application/qml/Main.qml \
      src/application/qml/Views/Workspace/ModeToolBar.qml

After each milestone:

    git status --short
    git diff --check

The build is the user's (rule one). When the user builds, the owning targets are
`RoboCrapTrajectories` (Milestone 1), `RoboCrapDevices` (Milestone 2) and `robocrap` (Milestones 3–4),
followed by `robocrap_qmllint` with `USE_HOT_RELOAD=OFF` for Milestone 4; the kit is the existing
build folder `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (check `CMakeCache.txt` before
reusing it).

Offline reference values (Milestone 1), in the session scratchpad:

    python -I <scratchpad>/calibration_reference.py \
      build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug/records/calibration_20261117_113235_576

Expected output shape (numbers are produced by the script and copied into `Artifacts and Notes`):

    touches: 114 calibration, 44 verification
    ball centre in {F}: [x; y; z] mm   (near TOOL_DATA[5]: -0.048; -0.097; 127)
    sphere centre in {B}: [x; y; z] mm (near 1054.14; -331.97; 454.27)
    rho_v, rho_h: ... mm (near 14)
    s_rep: ... mm, smallest singular value: ... , chi95: ... mm


## Validation and Acceptance

Without the robot (application started with the PLC and RSI disconnected): in Measuring mode,
Calibration submode, the toolbar shows "Generate Path", "Dry Run", "Stop", "RSI Sampling" and
"Start Calibration"; "Start Calibration" is disabled. The status bar shows on its left a gray round
indicator with "AUT EXT"; switching submodes, generating a path or changing status messages does not
move the indicator, the mode label or the toolbar buttons, and the viewport does not resize. Other
modes and the chamfer RSI Sampling behave as before. With the PLC connected and the controller in
AUT EXT the indicator is green; with the controller in T1 it is gray.

With the robot (user-authorized run, PLC connected, RSI bound, controller in AUT EXT at HOME): after
"Generate Path" and "RSI Sampling", "Start Calibration" is enabled; pressing it starts program 1
(the PLC panel shows the start sequence), and the status bar shows "Calibration run: 0 % · 0/158
touches", increasing with every touch to 100 %. When the program ends, the window "Calibration
complete" appears with the rows of Milestone 3; "OK" closes it. The console shows the line
"Probe calibration result: …", and `records/<id>/calibration_result_<id>.csv` exists. Running
`python -I python/rsi_touches.py records/<id> <id>` (from the executable folder) on the same run and
the Milestone 1 script on its output reproduce the displayed ball centre, sphere centre, contact
distances and chi95 within 1e-4 mm. Interrupting the program with the smartPAD STOP key keeps the
progress; resuming from the PLC panel continues it. A run that ends with fewer than 158 detections
shows "Calibration failed" with the count.


## Idempotence and Recovery

Every milestone is additive; repeating an edit step must leave the same text. If a build fails, fix
the introduced error before continuing. Pressing "Start Calibration" twice is impossible while a run
is active (disabled); a new run clears the previous detections and result. A failed or interrupted
run moves nothing back automatically: the controller-side recovery is that of
`resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md` ("KRL flow and failures").


## Artifacts and Notes

Reference values of Milestone 1 (2026-11-18). The script is the session scratchpad file
`ref/calibration_reference.py`, outside the repository; its arguments are the record folder, the
identifier and the deployed `resources/json/calibration_sphere.json`. Start values: the rough sphere
centre [1054.1403; -331.9692; 454.2727] mm, contact distance 14 mm, TOOL_DATA[5].

    == calibration_20261117_113235_576
    touches: 114 calibration, 44 verification, 57 pairs; GN iterations 5
    ball centre in {F}: [0.0234; -0.0566; 126.9683] mm
    sphere centre in {B}: [1054.2087; -331.9417; 454.1824] mm
    rho_v 13.8446 mm, rho_h 13.8629 mm
    s_rep 0.0148 mm, smallest singular value 2.0380
    verification sphere centre: [1054.2124; -331.9314; 454.1959] mm, chi95 0.1120 mm (max 0.1846)
    K7 sets outside 3 s_rep/sqrt(m): 22 of 36

    == calibration_20261114_170013_165
    touches: 114 calibration, 44 verification, 57 pairs; GN iterations 4
    ball centre in {F}: [0.0242; -0.0556; 126.9675] mm
    sphere centre in {B}: [1054.1705; -331.9492; 454.2280] mm
    rho_v 13.8426 mm, rho_h 13.8604 mm
    s_rep 0.0107 mm, smallest singular value 2.0414
    verification sphere centre: [1054.1837; -331.9401; 454.2381] mm, chi95 0.1112 mm (max 0.1851)
    K7 sets outside 3 s_rep/sqrt(m): 23 of 36

The smallest singular value matches the 2.03 of the method document's planning calculation (section
6.2), computed there in nominal poses.

Robot run of Milestone 5 (2026-11-18), computed by RoboCrap,
`records/calibration_20261118_113045_223/calibration_result_calibration_20261118_113045_223.csv`:

    == calibration_20261118_113045_223
    touches: 114 calibration, 44 verification
    ball centre in {F}: [0.0228; -0.0554; 126.9679] mm
    sphere centre in {B}: [1054.2032; -331.9299; 454.1821] mm
    rho_v 13.8449 mm, rho_h 13.8572 mm
    s_rep 0.0107 mm, smallest singular value 2.0383
    verification sphere centre: [1054.2167; -331.9202; 454.1978] mm, chi95 0.1107 mm
    K7 sets outside 3 s_rep/sqrt(m): 23 of 36


## Interfaces and Dependencies

In `src/trajectories/include/trajectories/probecalibrationfit.h` (includes
`trajectories/probetouchmotion.h`, `<QString>`, `<QVector>`, `<optional>`):

    // One detected touch: global touch number (1-based, order of the Approach steps of
    // probeMotionSteps) and the controller's actual joint angles AIPos, degrees.
    struct ProbeTouchDetection
    {
      int touch = 0;
      V6d joints = V6d::Zero();
    };

    // Mean calibration residual of one tilt ring or one roll of a group (method step K7).
    struct ProbeResidualSet
    {
      int group = 0;
      bool ring = true; // true: tilt ring (value = tilt), false: roll (value = roll).
      double value = 0.0; // deg
      double mean = 0.0; // mm
      double bound = 0.0; // 3 * repeatability / sqrt(count), mm
      int count = 0;
    };

    struct ProbeCalibrationFit
    {
      V3d ballCentre = V3d::Zero(); // {F}, mm
      V3d sphereCentre = V3d::Zero(); // {B}, mm
      double contactVertical = 0.0; // rho_v, groups 1 and 3, mm
      double contactHorizontal = 0.0; // rho_h, group 2, mm
      double repeatability = 0.0; // s_rep, mm
      double smallestSingularValue = 0.0;
      V3d verificationSphereCentre = V3d::Zero(); // {B}, mm
      double quantile95 = 0.0; // chi95, mm
      int calibrationTouches = 0;
      int verificationTouches = 0;
      QVector<ProbeResidualSet> residualSets;
    };

    struct ProbeCalibrationFitResult
    {
      std::optional<ProbeCalibrationFit> fit;
      QString error;
    };

    ProbeCalibrationFitResult fitProbeCalibration(const ProbeTouchMotion& motion,
                                                  const QVector<ProbeTouchDetection>& detections);

Dependencies: Eigen (`JacobiSVD`), `Kr10Kinematics` (already linked publicly by
`RoboCrapTrajectories` through `RoboCrapKinematics`). No new library, no new CMake target.

In `src/devices/include/devices/rsi/rsidevice.h`: `Request` gains `bool touch` and
`std::optional<V6d> joints`; new signal `void touchDetected(int touch, const V6d& joints);`; new
member `qsizetype m_reportedTouch = 0;`. `RoboCrapDevices` keeps linking only Geometry and Qt Network.

In `src/application/devices/devicerunner.h`: signal `void dataUpdated(const QVariantHash& values);`.

In `src/application/scene/scenemachiningpreparation.h`: `void attachPlc(DeviceRunner* plc);`,
`Q_INVOKABLE void startCalibration();`, slot `void recordTouch(int touch, const V6d& joints);`,
properties `canStartCalibration`, `calibrationRunning`, `calibrationDetected`,
`calibrationTouchCount`, `calibrationRunError`, `calibrationResult` (notify
`calibrationRunChanged`; `canStartCalibration` also on `availabilityChanged`), signal
`calibrationRunFinished()`; private `onPlcData(const QVariantHash&)`, `onPlcSocket()`,
`plcReady()` and `endRun(const QString& error)`; members `DeviceRunner* m_plc = nullptr;`,
`CalibrationRun m_run = CalibrationRun::Idle;` (enum `Idle, Requested, Starting, Started`),
`QVector<ProbeTouchDetection> m_detections;`, `QString m_runError;`, `QVariantList m_runResult;`,
and `PreparedCalibration::diagnosticId`.

In `ModeToolBar.qml`: `property bool calibrationStartAvailable`, `signal calibrationStartRequested()`.


Revision note (2026-11-18): first version, written from the user's requirements and decisions of
2026-11-18 (AUT EXT indicator only; Start Calibration runs program 1; progress in percent; end by PLC
program state; live detections; base-model result displayed and written, not applied; К7 reported
only). Same day, review before handoff: PLC state is observed through a new `DeviceRunner::dataUpdated`
signal because `QQmlPropertyMap::valueChanged` does not fire for `insert()`; the progress is exposed
as two counts instead of a percentage property; the AUT EXT indicator colours are fixed in the plan
instead of being left to the implementer.
