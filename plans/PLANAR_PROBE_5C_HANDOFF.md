# Implement planar probe scanning step 5c only

For GPT-6.2 Astra High. Work in the current RoboCrap checkout. Respond in English. Implement selected Scan Path playback and offline sampling input in **Measuring / Precise**. Reuse 5b’s stored motion without recalculating its route or solving the robot joints again.

## Starting state

The operator passed 5a. It expands the saved XPlane, YPlane and ZPlane grids into ordered contacts, omitting skipped cells. The reference GE-22 job has 3, 4 and 4 contacts respectively.

5b is implemented in the working tree. Selecting a Refine Job highlights its linked workpiece and previews its included contacts. A missing or ambiguous workpiece produces a red Status Bar message and disables Generate Path. Older jobs link on a unique mounting and CAD match. Generate Path publishes a separate Scan Path with immutable solved motion, source job UUID and revision. The route already includes HOME, approach, contact, retract and face transfers. The revised 5b UI check has no recorded operator acceptance. Proceed from the source as it stands. Preserve existing edits, including settings and untracked source files.

## Deliver exactly this

1. With a generated **Scan Path** selected, **Dry Run** plays its saved joints and timing through the existing robot and TCP display. Show the face role, grid cell and movement phase. Pause holds the pose, resume continues at the held time, and Stop rewinds to HOME. Deleting the playback owner stops playback. Keep Calibration and Machining playback bound to their own owners.
2. Reuse the existing invalidation paths. With jobs A and B on one workpiece, Cancel or an unchanged save preserves both paths. Saving a grid change to A invalidates only A’s path and dependent sampling result. A changed placement, measuring tool or missing source makes affected old results unusable. Keep the jobs’ saved local grids.
3. Supply offline numerical sampling input from the same saved motion. Carry the source job UUID and revision and, for each search in order, its sequential touch number, role, source face index, original U/V cell, local material point, signed direction, purpose, orientation group, pass and repetition. The reference job has 11 search entries. Preserve the combined-fixture guard in `sampleProbeCalibrationRsi` and existing calibration output. Stop at numerical input. Controller export, streaming, physical motion, plane fitting and placement updates are outside 5c.

## Read and edit narrowly

Keep the two IDs separate. Pass the selected **Scan Path’s scene object ID** to `ScenePlayback::start` as the playback owner. Use `SceneTouchPath::refineJobId` and `refineJobRevision` for the source job and sampling input. An empty `refineJobId` identifies a calibration or preset path. In `SceneCalibration::dryRun`, reuse the shared-pointer pattern that keeps the stored motion alive while playing `motion->timed()`. That method starts the calibration coordinator’s own path; calling it directly from Precise would play the wrong path. Use `ScenePlayback::canPlay` so a paused path can only be resumed by its owner.

1. Follow root `AGENTS.md` and `PLANS.md`. Read **5c. Play the selected path and verify which results survive an edit** in `plans/PLANAR_PROBE_SCANNING_EXECPLAN.md`. Update the existing plan’s 5c progress and outcomes. In `docs/code_context/AGENT_CONTRACTS.md`, read **Machining and playback**, **Geometry and scene**, and **QML**. Consult another section only if a necessary edit touches its area.
2. Use Ponytail for the smallest complete change. Use Serena MCP for C++ declarations, implementations and references. Start with `SceneCalibration::dryRun`, `ScenePlayback::start/pause/resume/stop`, `SceneTouchPath`, and `SceneModel::publishRefineScan`. For sampling, trace `sampleProbeCalibrationRsi` and its caller in `src/application/scene/scenemachiningpreparation.cpp`. Follow direct dependencies only to answer a concrete implementation question.
3. Use scoped text searches for QML and Markdown. In `src/application/qml/Main.qml`, inspect the toolbar's playback bindings, `onDryRunRequested` and the Status Bar phase label. Precise currently disables Dry Run, and its handler falls through to Machining. Inspect the matching controls in `src/application/qml/Views/Workspace/ModeToolBar.qml`. Offline sampling must work with the RSI device disconnected. Keep Precise out of the live preparation handler.

## Execution boundary

Authorized work is source and plan edits, targeted source review and read-only Git status/diff checks, including `git diff --check`. Keep tool output scoped to the changed symbols and hunks. Do not launch subagents, scan the repository, read `libs/`, search the web, revisit finished milestones or generate extra reports.

Do not configure, compile, link, build, debug, launch the app or run tests. Do not invoke any verifier, linter or formatter, directly or through a script, plugin or skill. This includes syntax-only checks, Qt/Felgo tools, scratch programs, temporary harnesses and offscreen runs. Do not connect to hardware, edit operator settings, stage or commit. Plan acceptance checks are for the operator. Only a separate, explicit user request authorizes running a named check.

Finish after the required source review and Git checks. Give the operator short numbered steps with the exact clicks and expected results. Leave UI acceptance, including visual clearance at face changes, explicitly pending. If a specific defect in 5b blocks 5c and fixing it expands scope, identify the failing symbol and ask before changing 5b.
