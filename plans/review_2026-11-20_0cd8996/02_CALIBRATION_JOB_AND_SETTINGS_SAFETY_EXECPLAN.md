# Calibration and RSI job safety: no stale job files, no Sampling during a run start, no unreadable or overwritten probe settings

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`, in particular "Mandatory C++ style": no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters. "Mandatory code reduction" applies too. Before each logical change, re-read those two sections. After each milestone, review the changed declarations, bodies and callers, and record start/finish line counts here.

This plan comes from the whole-project review of commit 0cd8996 (findings in `reviews/2026-11-20_0cd8996/`) and the user's decisions of 2026-11-20. Everything needed is repeated below.


## Purpose / Big Picture

RoboCrap prepares robot jobs for the KUKA controller. "RSI Sampling" writes a job file into `C:/RoboCrapJobs`, a folder the controller mounts as `/RoboCrapJobs`:
- `shared_data.txt` for the chamfer program `RSI_Chamfer_Hole`;
- `calibration_data.txt` for the probe calibration program `RSI_Probe_Calibration`.

Sampling also hands the robot interface (RSI) a table of correction streams. The application deletes the job file whenever it holds no current job, so that a KRL program started later stops after HOME instead of moving to targets of an old path.

Four defects break this safety or lose calibration data.

1. **A failed deletion is ignored.** If deleting the old job file fails (for example while the share holds the file open), the application only logs a line and carries on. The stale file stays, and a later `RSI_Chamfer_Hole` run can move PTP to the staging point and LIN to the lead start of an older path.
2. **Sampling stays enabled while a run starts.** After "Start Calibration" the application waits for the PLC to start program 1. During that wait the buttons "RSI Sampling" and "RSI test LIN" stay enabled. Pressing either deletes the calibration job and the stream table while the program is about to start. The program then finds no job, or every touch search is stopped as missed.
3. **A result the reader rejects can be saved.** A calibration result whose repeatability, 95 % quantile or contact distance is zero or negative is still saved to `settings/probe_calibration.json`. The application's own reader rejects such a file at the next start, and then also discards the valid primary centres in it.
4. **An unreadable settings file is overwritten.** When that file cannot be read (invalid JSON, a merge-conflict marker, a wrong version or an invalid value), the next save silently replaces it with nearly empty settings.

After this plan:
- A failed deletion is shown as the preparation error, and Sampling and the test LIN do not start while it remains.
- From "Start Calibration" until the run ends, both buttons are disabled and the calls are refused with "Calibration run in progress.".
- A result the reader would reject is not saved, and the failure is reported.
- After a failed read, nothing is written to the settings file in that session. Each attempted save shows the read error again, until the user fixes or deletes the file and restarts.

To see it working without hardware, start the application disconnected:
- Open `settings/probe_calibration.json` in another program and make it unreadable, for example by typing `x` before the first `{`. Restart, change the measuring TCP and Apply. The "Probe settings" dialog appears each time, and the file keeps its contents.
- Lock `C:/RoboCrapJobs/shared_data.txt` with a PowerShell `[System.IO.File]::Open(...)` handle. Press RSI Sampling: the status shows "Cannot remove …" and no Sampling runs.

The user checks the run-start blocking on the equipment during a calibration start (see Validation).


## Progress

- [x] (2026-11-20) Plan written after reading `src/application/scene/scenemachiningpreparation.{h,cpp}` (constructor, `clearPrepared`, the three prepare entry points, the reason getters, `startCalibration`, `onPlcData`, `endRun`, `replaceJobFile`), `src/application/scene/scenecalibration.{h,cpp}` (`readProbeSettings`, `calibrationSummary`, `positiveNumber`, `summaryObject`, `writeProbeSettings`, constructor, `onMeasuringTcpChanged`, `recordCalibrationResult`, `reassignPrimaryCentres`, `setSettingsError`), `src/application/qml/Main.qml:86-109, 448-470` and `src/application/qml/Views/Network/Network.qml:190-212`.
- [x] (2026-11-20) Start counts (physical lines): `scenemachiningpreparation.cpp` 556, `scenemachiningpreparation.h` 134, `scenecalibration.cpp` 546, `scenecalibration.h` 131, `Network.qml` 212.
- [ ] Before editing: read `controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md` ("PC behavior", "shared_data.txt"), `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md` ("calibration_data.txt", "PC behavior") and the "Machining and playback" and "QML" sections of `docs/code context/AGENT_CONTRACTS.md` (required by `AGENTS.md` for RSI work).
- [ ] Milestone 1: a failed job-file deletion is reported and blocks Sampling and the test LIN.
- [ ] Milestone 2: one shared busy guard; Sampling and the test LIN are refused in every non-Idle run state; the test LIN button follows it; `endRun` uses local copies.
- [ ] Milestone 3: a calibration result the reader would reject is not saved; after a failed settings read, nothing is written.
- [ ] Builds and disconnected checks of all milestones (user's).
- [ ] Contract update, final compliance pass, line counts, `git diff --check`.


## Surprises & Discoveries

(None yet.)


## Decision Log

- Decision: `clearPrepared` returns the removal error text (empty on success), and the three prepare entry points stop when it is not empty.
  Rationale: the deletion is the safety mechanism for KRL, so continuing after a failed deletion defeats it. A return value is the project's result channel (AGENTS.md). The stream table is still cleared and an empty table still sent, because dropping the held job is correct even when the file remains.
  Date/Author: 2026-11-20, plan author.
- Decision (user, 2026-11-20, option A): refuse Sampling, calibration Sampling and the test LIN whenever the calibration run state is not Idle (Requested, Starting or Started), instead of sending a PLC abort when they are used during a start.
  Rationale: it matches the contract rule that these are refused while program 1 runs, and it is a one-condition change. The user can still abort a start with the PLC panel's Abort button.
  Date/Author: 2026-11-20, user.
- Decision: the three copies of the busy checks move into one private method, `QString busyReason() const`.
  Rationale: the same three conditions and texts appear in `unavailableReason`, `calibrationUnavailableReason` and `prepareTestLin`, and Milestone 2 changes one of them. Reuse of an existing method fails because none returns only the shared part.
  Date/Author: 2026-11-20, plan author.
- Decision: the test LIN button in `Network.qml` adds `&& !Backend.MachiningPreparation.calibrationRunning` to its `enabled` binding; no new C++ property is added.
  Rationale: `calibrationRunning` (`m_run != Idle`) already exists and notifies on every run-state change.
  Date/Author: 2026-11-20, plan author.
- Decision: `recordCalibrationResult` validates the new summary with the reader's own check, `calibrationSummary(summaryObject(summary))`, before keeping or saving it.
  Rationale: one rule for the writer and the reader, with no new validation code.
  Date/Author: 2026-11-20, plan author.
- Decision (user, 2026-11-20): after a failed read of `probe_calibration.json`, nothing is written to it during that session.
  Implementation: a new member `QString m_settingsReadError` and a private `QString saveSettings()` replace the three `writeProbeSettings(m_settings)` calls. The member is the only state that distinguishes "read failed at start" from "a later write failed", and both are reported through the existing `settingsError`.
  Date/Author: 2026-11-20, user and plan author.


## Outcomes & Retrospective

(To be written at completion.)


## Context and Orientation

Read `AGENTS.md`, the RSI contracts and the AGENT_CONTRACTS sections named in Progress before editing.

`SceneMachiningPreparation` (`src/application/scene/scenemachiningpreparation.{h,cpp}`) is a QObject singleton (QML name `MachiningPreparation`) owned by `main()`. It owns RSI Sampling and the calibration run:

- **`prepare()`** (chamfer path, line 299), **`prepareCalibration()`** (calibration path, 316) and **`prepareTestLin()`** (a fixed test stream, 332) each call `clearPrepared()` first. `prepare()` and `prepareCalibration()` then start a background sampling worker; `prepareTestLin()` installs its stream at once.
- **`clearPrepared()`** (413-427):
  - ends a calibration run if one is active;
  - deletes `C:/RoboCrapJobs/shared_data.txt` and `C:/RoboCrapJobs/calibration_data.txt` with `QFile::remove`; on failure it only prints `[ERROR] RSI Sampling: cannot remove …` with `qInfo`;
  - resets the held job;
  - emits `correctionsReady({}, {})`, an empty table for the RSI device on its thread.

  It is also called by the constructor (to remove files left by an earlier session), by `onAvailabilityChanged` and `onCalibrationAvailabilityChanged` (when the sampled path is removed or changed) and by `shutdown`.
- **The calibration run state** is `enum class CalibrationRun { Idle, Requested, Starting, Started }`. Requested means "Run sent to the PLC", Starting "PLC start sequence seen", Started "program 1 seen running".
  - `startCalibration()` (479-489) sets Requested and invokes the PLC's `runRobotProgram`.
  - `onPlcData` (502-524) advances the state.
  - `endRun` (532-556) returns to Idle, writes the touch file and, on success, fits the result and calls `SceneCalibration::recordCalibrationResult`.
  - `calibrationRunning()` (header line 59) is `m_run != Idle`; Main.qml uses it for the status bar.

  However, `unavailableReason()` (257-266), `calibrationUnavailableReason()` (247-255) and `prepareTestLin()` (335-337) refuse only `m_run == Started`. The QML property `canPrepare`/`canPrepareCalibration` is their empty-reason test and drives the RSI Sampling button (Main.qml:455-458). The RSI test LIN button (Network.qml:194-199) checks only `preparing`.

`SceneCalibration` (`src/application/scene/scenecalibration.{h,cpp}`, QML name `Calibration`) owns `settings/probe_calibration.json`. The file is git-tracked, lives in the folder `ROBOCRAP_SETTINGS_DIR`, and holds the primary ball centre and sphere centre plus the last calibration result.
- **Reading.** `readProbeSettings()` (156-189) returns `{settings, error}`. A missing file gives empty settings without an error. Any present but invalid part returns empty settings with an error text:
  - the file is not JSON;
  - the version is not 1;
  - a `primary` value is malformed;
  - a `lastCalibration` key is missing or non-positive.

  The `lastCalibration` check is `calibrationSummary` (98-118), which requires five strictly positive numbers through `positiveNumber` (49-54).
- **Startup.** The constructor (285-300) stores the error in `settingsError`. Main.qml:86-102 opens the native "Probe settings" dialog at start, and again whenever `settingsError` becomes non-empty.
- **Writing.** `writeProbeSettings(m_settings)` is called from three places: `onMeasuringTcpChanged` (480), `recordCalibrationResult` (518) and `reassignPrimaryCentres` (530). After a failed read, `m_settings` is empty, so any of these replaces the file with nearly nothing.
- **Recording a result.** `recordCalibrationResult` (514-521) copies the fit into `m_settings.last` without the reader's check, saves, emits `lastCalibrationChanged` (which opens the result window in QML) and returns `m_settingsError`.

`endRun` (532-556) calls `recordCalibrationResult`, which emits a signal synchronously, and afterwards reads `m_preparedCalibration->diagnosticId`. Any future handler that reached `clearPrepared` would leave the optional empty there. Copying the identifier and motion handle into locals at the start of `endRun` removes that risk.


## Plan of Work

### Milestone 1: report a failed job-file deletion and stop

In `scenemachiningpreparation.{h,cpp}`, change `void clearPrepared()` to `QString clearPrepared()`. It collects the failure text `QStringLiteral("Cannot remove %1.").arg(QDir::toNativeSeparators(path))` for each file whose `QFile::remove` failed, joined with a space. Keep the existing `qInfo` line. Everything else in the function stays the same, in the same order.

The callers change as follows:
- **`prepare()` and `prepareCalibration()`.** Capture the text; if it is not empty, call `setError(text)` and return before `setError({})` and before starting the worker. `prepare()` sets `m_pending` before `clearPrepared()` today; reset it to `{}` on this early return. `prepareCalibration()` likewise resets `m_pendingCalibration`.
- **`prepareTestLin()`.** Return after `setError(text)`, before installing the test stream.
- **Constructor, `onAvailabilityChanged`, `onCalibrationAvailabilityChanged`.** Call `setError(text)` when it is not empty.
- **`shutdown()`.** Ignores the text; the `qInfo` line already reports it.

### Milestone 2: one busy guard, refused in every non-Idle run state

Add `QString busyReason() const`, returning in this order:
1. `tr("Application is shutting down.")` when `m_shuttingDown`;
2. `tr("Calibration run in progress.")` when `m_run != CalibrationRun::Idle`;
3. `tr("RSI Sampling…")` when `m_preparing`;
4. otherwise empty.

Use it as the first check of `unavailableReason()` and `calibrationUnavailableReason()`, replacing their first three lines. Replace the guard of `prepareTestLin()` with `if (const QString reason = busyReason(); !reason.isEmpty()) { setError(reason); return; }`. The order of the checks changes from shutting down / Started / preparing to shutting down / not Idle / preparing; the texts stay the same.

In `Network.qml`, the "RSI test LIN" button's `enabled` becomes `rsiTestPanel.rsi && rsiTestPanel.rsi.isConnected && !Backend.MachiningPreparation.preparing && !Backend.MachiningPreparation.calibrationRunning`.

In `endRun`, copy `m_preparedCalibration->motion` and `->diagnosticId` into locals right after the existing `Q_ASSERT`, and use only those locals below.

### Milestone 3: settings writes follow the reader's rules

In `scenecalibration.cpp`, in `recordCalibrationResult`, build the `CalibrationSummary` into a local first. If `!calibrationSummary(summaryObject(summary))`, return `tr("Calibration result not saved: the fit gave a zero or negative contact distance, repeatability, quantile or singular value.")` without changing `m_settings.last` and without emitting `lastCalibrationChanged`. `endRun` then puts the text into `calibrationRunError`, and `calibrationRunFinished` opens the "Calibration failed" dialog, because the result window was not opened. Otherwise assign it, save and emit as today.

In `scenecalibration.h`, add the private member `QString m_settingsReadError;` ("The start read's error; while set, nothing is written to the settings file.") and the private method `QString saveSettings();`. The constructor stores `loaded.error` in it, next to `setSettingsError(loaded.error)`. `saveSettings()`:
- returns `m_settingsReadError + tr(" Not saved: fix or delete the file, then restart RoboCrap.")` when `m_settingsReadError` is not empty;
- otherwise returns `writeProbeSettings(m_settings)`.

Replace the three `writeProbeSettings(m_settings)` calls (480, 518, 530) with `saveSettings()`.

`setSettingsError` returns early only for a repeated empty error. A repeated non-empty error therefore re-emits `settingsErrorChanged`, and Main.qml reopens the dialog on every refused save, as the contract requires.

`reassignPrimaryCentres` needs no new check: `m_settings.last` now comes either from a successful read, where the reader checked it, or from `recordCalibrationResult`, which now checks it.


## Concrete Steps

Work in the repository root. Before and after each milestone:

    wc -l src/application/scene/scenemachiningpreparation.* src/application/scene/scenecalibration.* src/application/qml/Views/Network/Network.qml
    git diff --check

The user builds; the agent never does. Target: `robocrap` in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, after checking `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt`. The QML lint target is `robocrap_qmllint`; it needs `USE_HOT_RELOAD=OFF`, which this build has.

Disconnected check for Milestone 1 (user, PowerShell). The application must be running and a generated machining path selected:

    $f = [System.IO.File]::Open("C:\RoboCrapJobs\shared_data.txt", 'OpenOrCreate', 'Read', 'None')
    # press RSI Sampling in RoboCrap now (RSI socket bound in Network settings)
    $f.Close()

Expected: the status bar shows "Cannot remove C:\RoboCrapJobs\shared_data.txt." and no "RSI Sampling: … cycles" console line appears. After `$f.Close()`, pressing RSI Sampling works as before.

Disconnected check for Milestone 3 (user):

    Copy-Item settings\probe_calibration.json $env:TEMP\probe_calibration.backup.json
    # add the character x before the first { in settings\probe_calibration.json, start RoboCrap
    # Measuring EE: change X of the ball centre, Apply
    Compare-Object (Get-Content settings\probe_calibration.json) (Get-Content $env:TEMP\probe_calibration.backup.json)

Expected: the "Probe settings" dialog appears at start and again after Apply, its text ending in "Not saved: fix or delete the file, then restart RoboCrap.". `Compare-Object` lists only the line with the added `x`. Restore the file from the backup afterwards.


## Validation and Acceptance

**Milestone 1** is accepted when every caller of `clearPrepared` handles its result as described, and the locked-file check behaves as stated.

**Milestone 2** is accepted by code inspection: `unavailableReason`, `calibrationUnavailableReason` and `prepareTestLin` each start with `busyReason()`, and `busyReason` tests `m_run != CalibrationRun::Idle`. In the running application (disconnected), the RSI Sampling and test LIN buttons behave as before while no run is active. During a calibration start on the equipment (user), both buttons are disabled from the moment "Start Calibration" is pressed until the run ends.

Equipment check for Milestone 2, done by the user. Prepare the run:
- the PLC is connected and the RSI socket is bound;
- the robot is in AUT EXT at HOME, with CELL selected and its BCO run done (`docs/hardware context/HARDWARE_SETUP.md`);
- in Measuring/Calibration, Generate and then RSI Sampling have been run;
- `StepLimit` in `RSI_Probe_Calibration.dat` is 10, so the run is short (touches 1–3).

Then:
1. Press "Start Calibration". The toolbar "RSI Sampling" button and the Settings "RSI test LIN" button turn disabled at once, before the PLC reports the program running.
2. They stay disabled while program 1 runs.
3. They become enabled again after the run ends: the program finishes, or the start fails, or is aborted with the PLC panel's Abort.

Before the change, both buttons stayed enabled until the program was seen running.

**Milestone 3** is accepted by the settings-file check above. By code inspection, `recordCalibrationResult` returns early, without emitting, when `calibrationSummary` rejects the summary. A fit with such values cannot be produced from the UI.


## Idempotence and Recovery

The edits are source edits only, and repeating them changes nothing. The settings check restores the file from the backup copy. The locked-file check leaves nothing behind once `$f.Close()` has run.


## Artifacts and Notes

The current guard, for orientation:

    QString SceneMachiningPreparation::unavailableReason() const
    {
      if (m_shuttingDown) return tr("Application is shutting down.");
      if (m_run == CalibrationRun::Started) return tr("Calibration run in progress.");
      if (m_preparing) return tr("RSI Sampling…");
      ...


## Interfaces and Dependencies

No dependency or QML API change. At the end:

    // scenemachiningpreparation.h (private)
    QString clearPrepared();
    QString busyReason() const;

    // scenecalibration.h (private)
    QString saveSettings();
    QString m_settingsReadError;

After Milestone 3, update `docs/code context/AGENT_CONTRACTS.md`, "Machining and playback". Change the text "While program 1 runs, Sampling and the test LIN are refused" to "From Start Calibration until the run ends (Run requested, PLC start sequence or program 1 running), Sampling and the test LIN are refused". Add these three facts:
- a job file that cannot be deleted is shown as the preparation error and blocks Sampling and the test LIN;
- after a failed read of `probe_calibration.json`, SceneCalibration writes nothing to it in that session, and every refused save reports the read error;
- a calibration result that the settings reader would reject is not saved and is reported as a run error.

In the "QML" section, the test LIN button also requires that no calibration run is active.
