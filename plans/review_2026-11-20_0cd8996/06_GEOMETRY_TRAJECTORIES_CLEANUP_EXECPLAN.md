# Geometry, kinematics and trajectories cleanup: one joint-limit check, simpler chamfer motion data, shared geometry helpers

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`: "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters) and "Mandatory code reduction". Remove a check only after proving every path already checked the same value, condition, tolerance and lifetime. Before each logical change, re-read both sections. After each milestone, review the changed declarations, bodies and callers, and record start/finish line counts here.

Before editing, read the "Geometry and scene" and "Machining and playback" sections of `docs/code context/AGENT_CONTRACTS.md`. Milestone 5 also edits RSI files; before it, read `controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md` ("PC behavior", "shared_data.txt") and `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md` ("calibration_data.txt"). If `plans/review_2026-11-20_0cd8996/02_CALIBRATION_JOB_AND_SETTINGS_SAFETY_EXECPLAN.md` is not complete, finish it first. Both plans edit `src/application/scene/scenemachiningpreparation.cpp`, in different functions.

This plan comes from the whole-project review of commit 0cd8996. The findings are in `reviews/2026-11-20_0cd8996/qt_cpp_review_report.md`: C++ blocks S4-D1 to D11, S4-I2, S5-D1, D2, D4 to D9, S5-I1, S5-I2, S6-D4 and S2-D2. The plan also follows the user's decision of 2026-11-20 to remove the `IgnorePreviewJointPositionLimits` switch. Everything needed is repeated below.


## Purpose / Big Picture

The numerical libraries are:
- `src/geometry`: surfaces, fitting and math helpers;
- `src/kinematics`: the KR10 forward and inverse kinematics;
- `src/trajectories`: chamfer and probe motions, RSI sampling.

They contain several kinds of excess:
- **A dead switch.** The diagnostic switch `IgnorePreviewJointPositionLimits` is permanently false, and its branches are spread over four files.
- **Duplicated checks and solvers.** Two copies of the joint-limit check exist, only one of which honours that switch, and a copy of the TCP inverse-kinematics routine returns a value nobody reads.
- **Never-optional data.** An optional field that is always set, four result fields that are never read, and a parameter struct no caller sets.
- **Repeated helpers.** The same sign rule, centring code and validation are written two or three times in the geometry files.
- **Unnamed numbers.** Magic phase indices and literals whose error text can drift from their value.

One functional gap is included: importing a probe JSON file has no size limit, and the fit runs on the GUI thread, so a very large or wrongly chosen file freezes the window.

After this plan:
- Generate (chamfer and calibration) and RSI Sampling produce identical results with less code.
- Importing a file with more than 10,000 points is refused with a clear error.

To see it working, the user records a baseline before the first change, repeats it after each milestone and compares:
- the console line `Chamfer preparation: <N> points, <T> s movement`;
- the calibration Generate console line;
- the job files `C:/RoboCrapJobs/shared_data.txt` and `calibration_data.txt`;
- the plan files `records/<id>/rsi_plan_<id>.csv`.

The numbers and file contents must match, apart from the identifiers and timestamps that every run creates.


## Progress

- [x] (2026-11-20) Plan written after reading:
  - `src/trajectories/motion/tcpsolver.h` and `src/trajectories/motion/jointsegments.cpp` (spline checks, `rigid`, `withinLimits`, `validRobot`);
  - `src/trajectories/chamfer/chamfermotion.{h,cpp}` (compiler, `appendStart`, `appendCentral`, constructor, `create`);
  - `src/trajectories/include/trajectories/chamfercentralmotion.h` (ChamferCentralPose) and `src/trajectories/chamfer/chamfercentralmotion.cpp` (`evaluate`);
  - `src/trajectories/include/trajectories/chamferarclength.h`;
  - `src/trajectories/motion/motionbuilder.cpp` (rate inspection);
  - `src/kinematics/kr10kinematics.cpp` (`resolveJointAngle`) and `src/kinematics/include/kinematics/kr10kinematicmodel.h`;
  - `src/geometry/utils.{h,cpp}` (`decodeProbeSamples`), `plane.cpp`, `spherefit.cpp`, `surfaceintersection.cpp` and `boundedcylinder.cpp` (the duplicated helpers);
  - `src/application/robot/robotcontroller.cpp`.

  The probe files in `data/probe_points/` hold at most 8 points.
- [x] (2026-11-20) Start counts (command in Concrete Steps): 5833 lines, unchanged since 0cd8996.
- [x] (2026-11-20) Baseline recorded in `C:\RoboCrapBaseline\before_m1\`: console lines (`Chamfer preparation: 373 points, 25.2874890374 s movement`; calibration: 114 + 44 touches, 6176 points, 894.846511369 s, clearances 3.0000 / 3.2588 mm), `shared_data.txt`, `calibration_data.txt` (from a second calibration Sampling), `rsi_plan_20261120_161930_857.csv`, `rsi_plan_calibration_20261120_161839_874.csv`. The second calibration Sampling's plan file equals the first apart from `# plan_id` (`[] 161 161`), so Sampling is deterministic.
- [x] (2026-11-20) Milestone 1 implemented:
  - `IgnorePreviewJointPositionLimits` and its comment removed from `kr10kinematicmodel.h`; its branches removed from `jointsegments.cpp` (spline test, `withinLimits`), `resolveJointAngle` and `robotcontroller.cpp` (console message, preview FK term);
  - `chamferJointPositionError` moved from `tcpsolver.h` into `jointsegments.cpp`, declared in `jointsegments.h`; `withinLimits` returns `chamferJointPositionError(joints, model).isEmpty()`;
  - `appendStart` uses `solveTcp`; `ChamferTcpSolver::inverse` and the then unused `<utility>` include of `tcpsolver.h` removed;
  - `appendCentral`'s seed check and `seedError` removed (proof: `compile` stops at the first error; the seed is the start chosen by the start search, or the last `solveTcp` solution of the preceding LIN).
  Searches: no `IgnorePreviewJointPositionLimits`, `inverse(` or `seedError` in `src`.
  Compliance pass: no lambdas, templates, exceptions or output parameters; no new helper (one function moved). No violations found.
  Counts (physical / nonblank): kr10kinematicmodel.h 55/43 → 50/39, kr10kinematics.cpp 303/223 → 296/217, robotcontroller.cpp 202/176 → 198/172, tcpsolver.h 101/90 → 73/64, jointsegments.h 116/98 → 118/100, jointsegments.cpp 331/309 → 339/316, chamfermotion.cpp 159/141 → 159/141; Concrete Steps set 5833 → 5799.
- [x] (2026-11-20) Milestone 1 user check: built; console lines identical to the baseline (373 points, 25.2874890374 s; 114 + 44 touches, 6176 points, 894.846511369 s, clearances 3.0000 / 3.2588 mm); files in `C:\RoboCrapBaseline\after_m1\` identical to `before_m1` apart from `# plan_id` (comparison script in the agent's scratchpad, same rule as Concrete Steps): shared_data.txt `[] 8 8`, calibration_data.txt `[] 966 966`, chamfer plan `[] 3349 3349`, calibration plan `[] 161 161`.
- [x] (2026-11-20) Milestone 2 implemented:
  - `ChamferMotion(TimedMotion, boundaries, ChamferCentralMotion)` stores the central motion by value; `parameters()` returns `m_centralMotion.path().parameters()` (the central motion keeps its own copy of the generating path); `centralMotion()` returns `const ChamferCentralMotion&`; `m_parameters`, `setCentralMotion` and the optional removed; the unreachable "analytical timing is missing" check in `chamferrsistream.cpp` and the ternary in `scenegeometry.h` removed;
  - `ChamferCentralPose` keeps `tcp` and `progress`; `evaluatePhase` keeps its finiteness test of `state.speed`/`state.acceleration`;
  - `ChamferArcLengthParameters` replaced by four named constants in `chamferarclength.cpp`; the tolerance check and that file's then unused `positiveFinite` removed;
  - the commented joint-acceleration block in `motionbuilder.cpp` removed;
  - `ChamferBoundary`, `ChamferBoundaryCount`, `ChamferPhaseCount` and `ChamferMotion::boundary()` in `chamfermotion.h`, used in `chamfermotion.cpp`, `chamferrsistream.cpp`, `scenemachiningpreparation.cpp` and `sceneobject.cpp`; the array accessor `boundaries()` removed, because no caller remained.
  Searches: no `setCentralMotion` or `ChamferArcLengthParameters` in `src`.
  Compliance pass: no lambdas, templates, exceptions or output parameters; constructor with three parameters; `boundary()` is the one new accessor (Plan of Work). No violations found.
  Counts (physical): chamfermotion.h 37 → 50, chamfermotion.cpp 159 → 153, chamferrsistream.cpp 252 → 251, chamfercentralmotion.h 67 → 63, chamfercentralmotion.cpp 187 → 180, chamferarclength.h 53 → 42, chamferarclength.cpp 352 → 345, motionbuilder.cpp 201 → 195, scenegeometry.h 150 → 149; Concrete Steps set 5799 → 5769. Outside the set: sceneobject.cpp 264 → 269 (named phase colours), scenemachiningpreparation.cpp 562 → 564.
- [x] Milestone 2 user check (2026-11-20): build passed; console lines identical to the baseline (Chamfer 373 points, 25.2874890374 s; calibration 114+44 touches, 6176 points, 894.846511369 s; clearances 3.0000/3.2588 mm). Files in `C:\RoboCrapBaseline\after_m2\` identical to `before_m1` ignoring `# plan_id`: shared_data.txt 8/8 lines, calibration_data.txt 966/966, chamfer rsi_plan 3349/3349, calibration rsi_plan 161/161.
- [x] (2026-11-20) Milestone 3 implemented:
  - `positiveFinite` declared in `geometry/utils.h` and defined in `utils.cpp`; the copies in `chamfercentralmotion.cpp` and `chamfercentralvalidation.cpp` removed (the third went in Milestone 2), and both files include `geometry/utils.h`;
  - `ChamferCentralProfile::switchSeconds(ChamferPhase)` used in `initializeTiming` and `makePreviewSchedule`;
  - `ChamferPhases` and `chamferPhaseLabel` in `chamferpath.h`; `ChamferCentralPreviewSpan::phase` is `ChamferPhase`; `CentralPath::schedule` appends `chamferPhaseLabel(span.phase)`; the three index casts use `ChamferPhases[index]`; the arc-length error prints the label;
  - `MaximumScheduledPoints` (200000) and `TimeResolution` (1e-9 s) in `jointsegments.h`, used in `chamfercentralvalidation.cpp`, `jointsegments.cpp` (`linearSchedule`, plus the ramp test in `timedProfile` and the spline step test, which compare seconds against the same 1e-9) and `motionbuilder.cpp`; both point-limit messages use `.arg(MaximumScheduledPoints)`; the local `MaximumSamples` removed;
  - named cylinder-fit literals in `boundedcylinder.cpp` (`MaximumIterations`, `MaximumDampingTrials`, `MinimumConditioning`, `MinimumRmsTolerance`, `RelativeRmsTolerance`) and `MaximumBernsteinDepth` in `chamferarclength.cpp`; `spherefit.cpp` uses `GeomConst::Eps`;
  - `ChamferPath::create` tests `path.stagingPoint().allFinite()` after construction and before `initializeLeadFrames` (same value: `stagingDistance * m_outwardAxis` with `m_outwardAxis` the normalized, optionally negated axis);
  - `makePreviewSchedule` sets `schedule.error` and returns `schedule` on the failures inside the loop (the caller reads spans and points only when the error is empty);
  - `clearanceError` compares the strut against `required` and prints it with `'f', 4` in both messages.
  Only failure texts change: the arc-length error names the phase ("Lead-in" instead of 0) and the strut message prints 2.9990-style thresholds.
  Searches: no local `positiveFinite`, `static_cast<ChamferPhase>`, `MaximumSamples` or literal 1e-9 time comparisons in `src`.
  Compliance pass: no lambdas, templates, exceptions or output parameters; the new functions (`positiveFinite`, `switchSeconds`, `chamferPhaseLabel`) take one parameter and are the ones Plan of Work requires. No violations found.
  Counts (physical/nonblank): utils.h 40/31 → 41/32, utils.cpp 283/219 → 288/223, boundedcylinder.cpp 253/228 → 259/233, spherefit.cpp 52/43 → 52/43, chamferpath.h 98/84 → 111/96, chamferpath.cpp 344/319 → 342/317, chamfercentralmotion.h 63/51 → 69/57, chamfercentralmotion.cpp 180/164 → 176/161, chamfercentralvalidation.h 52/45 → 52/45, chamfercentralvalidation.cpp 230/212 → 231/213, chamferarclength.cpp 345/319 → 346/320, jointsegments.h 118/100 → 120/102, jointsegments.cpp 339/316 → 340/317, motionbuilder.cpp 195/179 → 195/179, probetouchmotion.cpp 163/146 → 163/146; Concrete Steps set 5769 → 5799. The growth (+30) is the phase label function (+12, replaces an inline ternary chain and the stored strings), `switchSeconds` (+6, replaces two copies of the rule), the seven named constants and the braced error returns; the smaller alternative of keeping the literals and copies is what the milestone removes.
- [x] Milestone 3 user check (2026-11-20): build passed; console lines identical to the baseline (Chamfer 373 points, 25.2874890374 s; calibration 114+44 touches, 6176 points, 894.846511369 s; clearances 3.0000/3.2588 mm). Files in `C:\RoboCrapBaseline\after_m3\` identical to `before_m1` ignoring `# plan_id`: shared_data.txt 8/8 lines, calibration_data.txt 966/966, chamfer rsi_plan 3349/3349, calibration rsi_plan 161/161.
- [x] (2026-11-20) Milestone 4 implemented:
  - `geometry/utils.{h,cpp}`: `dominantPositive`, `referenceAxis`, `CenteredPoints`/`centerPoints`, `toV3d`, `toArray`; used in `plane.cpp` (centring, sign rule), `spherefit.cpp` (centring; `centroid` replaces `mean`), `surfaceintersection.cpp` (`vector`, `canonical` removed), `boundedplane.cpp` and `boundedcylinder.cpp` (`coordinates`, reference vector, sign rule; the then unused `Point` alias of `boundedcylinder.cpp` removed);
  - `ProbeSamples::hasValidCompensation()` used in `decodeProbeSamples` and both `fromSamples`; the decoder maps `dir` to 1, -1 or 0 (0 fails the check), so no unchecked double-to-int conversion remains; the point finiteness loop removed;
  - `MaximumProbeSamples` (10,000) checked before decoding the array; `ReadError::TooLarge` and `MaximumImportBytes` (4 MiB) in `scenesurfaceimporter.cpp`, tested after `open` and before `readAll`; the Samples text begins "Expected at most %1 finite [x, y, z] points";
  - `intersectSurfaces`: `tolerance` and the first `fromEllipse` call removed;
  - `Kr10Kinematics`: `childTransform`, `wristCentre`, `forearmLength`, `elbowOffset` (Decision Log for `childTransform`), used in `solveFK`, `armTransform`, `calculateIK`, `isOverhead`, `isValid`, `branchFrom` and `solveArm`;
  - trailing comma in `IntersectionStatus`; contract sentence on both import limits in AGENT_CONTRACTS "Geometry and scene".
  Searches: no `canonical(`, `coordinates(` or inline `0.9 ?` reference vector in `src/geometry`.
  Compliance pass: no lambdas, templates, exceptions or output parameters; `childTransform` has three parameters, the other new functions one or none; each new function replaces two or three copies (Plan of Work). No violations found.
  Counts (physical/nonblank): utils.h 41/32 → 69/54, utils.cpp 288/223 → 326/256, plane.cpp 58/42 → 40/27, spherefit.cpp 52/43 → 43/35, surfaceintersection.cpp 99/86 → 83/72, surfaceintersection.h 39/31 → 39/31, boundedplane.cpp 92/79 → 81/71, boundedcylinder.cpp 259/233 → 249/225, scenesurfaceimporter.cpp 100/86 → 106/91, kr10kinematics.cpp 296/217 → 287/213, kr10kinematics.h 87/70 → 94/77; Concrete Steps set 5799 → 5805. The removed copies (−70 lines in the callers) are offset by the shared declarations with their comments and the import limit (+6 in the importer, +9 for `hasValidCompensation` and `MaximumProbeSamples`); the import limit is new behaviour, and keeping the copies is the larger alternative the milestone removes.
- [x] Milestone 4 user check (2026-11-20): build passed; console lines identical to the baseline (Chamfer 373 points, 25.2874890374 s; calibration 114+44 touches, 6176 points, 894.846511369 s; clearances 3.0000/3.2588 mm). Files in `C:\RoboCrapBaseline\after_m4\` identical to `before_m1` ignoring `# plan_id`: shared_data.txt 8/8 lines, calibration_data.txt 966/966, chamfer rsi_plan 3349/3349, calibration rsi_plan 161/161. The planned edge Properties comparison was dropped: the edge has no Properties rows, and the chamfer files cover the edge.
- [x] Milestone 4 import check (2026-11-20, user): "Plane from JSON" with `C:\RoboCrapBaseline\big_probe.json` (10,001 points) refused with the new text.
- [x] (2026-11-20) Milestone 5 implemented:
  - `wirePose` moved from `probersistream.cpp` into `rsicoordinates.h` (inline, with the `geometry/utils.h` and `kinematics/kr10kinematicmodel.h` includes it needs); `chamferrsistream.cpp` uses it for the initial pose, the final pose and the tool frame, so `toolCoordinates` is six-decimal and checked like the probe job's; `rotationEntryError` removed; `scenemachiningpreparation.cpp` writes `stream.toolCoordinates` unchanged; the `toolCoordinates` comment in `chamferrsistream.h` says six-decimal;
  - `RsiCyclePeriod` (0.004 s) and `RsiSettleCycles` (50) in `rsicoordinates.h` replace the local `CyclePeriod`/`SettleCycles` of both stream files; `RateTolerance` (1e-6) in `jointsegments.h` replaces the locals of `chamferrsistream.cpp` and `motionbuilder.cpp` and the `1.0 + 1e-6` literals of `jointsegments.cpp` and `tcpsolver.h`;
  - the application's `RsiCycleSeconds`/`TestLinSettleCycles` stay (Decision Log).
  Failure texts that change: a nonfinite initial pose now reports "Cannot represent the initial TCP pose at six decimal places."; the tool frame failure reports "… transform at six decimal places.". The final pose gains the position check of `wirePose`, which six-decimal rounding of a finite position always passes.
  Searches: no `rotationEntryError`, local `CyclePeriod`/`SettleCycles`/`RateTolerance` or `1.0 + 1e-6` in `src/trajectories`.
  Compliance pass: no lambdas, templates, exceptions or output parameters; `wirePose` moved, not added; the three constants each replace two to four definitions. No violations found.
  Counts (physical/nonblank): rsicoordinates.h 27/22 → 47/40, chamferrsistream.cpp 251/229 → 231/210, probersistream.cpp 197/178 → 181/163, jointsegments.h 120/102 → 121/103, jointsegments.cpp 340/317 → 340/317, motionbuilder.cpp 195/179 → 194/178, tcpsolver.h 73/64 → 73/64, chamferrsistream.h unchanged; Concrete Steps set 5805 → 5789.
- [x] Milestone 5 user check (2026-11-20): build passed; console lines identical to the baseline (Chamfer 373 points, 25.2874890374 s; RSI Sampling 3390 cycles, 13.3598 s; calibration 114+44 touches, 6176 points, 894.846511369 s; clearances 3.0000/3.2588 mm; calibration sampling 158 touches, 485 steps). Plan files in `C:\RoboCrapBaseline\after_m5\` identical to `before_m1` ignoring `# plan_id` and, for the chamfer plan, `# tool_xyzabc`: chamfer 3349/3349, calibration 161/161. The `# tool_xyzabc` line changed from `168.196357456,-0.190437929,125.814626309,-90.017876372,0.000000000,-0.179278797` to `168.196357000,-0.190438000,125.814626000,-90.017876000,0.000000000,-0.179279000`, the values of the baseline `shared_data.txt` ToolFrame row. The first job-file attachment carried no contents, because the application had already removed the files at shutdown (Surprises); a repeat Sampling with copies made while the application ran (20:36:55 and 20:37:10) gives `shared_data.txt` 8/8 and `calibration_data.txt` 966/966 lines identical to `before_m1`.
- [ ] Milestone 5 equipment sessions (user, planned 2026-11-21): `RSI_Probe_Calibration` with `StepLimit` = 10 and `RSI_Chamfer_Hole` in T1, as in Validation and Acceptance.
- [x] (2026-11-21) Baseline comparison after each milestone (user's): recorded with each Milestone 1–5 user check above, every comparison identical apart from `# plan_id` (and the expected `# tool_xyzabc` line in Milestone 5).
- [x] (2026-11-21) Final compliance pass, line counts, `git diff --check`, contract check (`plans/REMAINING_CHECKS_EXECPLAN.md`, A1):
  - counts: Concrete Steps set 5833 (0cd8996) → 5789 (HEAD, −44), equal to the Milestone 5 count; the working tree's 5787 is plan 07 (`scenesurfaceimporter.cpp` −2, `busy` removed), recorded there;
  - compliance pass over `git diff 0cd8996 HEAD` of the set (33 files, +340/−384, all in commit 34d4b81): no added lambda, template, `throw`, `try`/`catch` or non-const reference parameter; the largest new signature is `Kr10Kinematics::childTransform` with three parameters; no violation found;
  - `git diff --check 0cd8996 HEAD` of the three libraries: clean;
  - contract check: the import limits (4 MiB, `MaximumProbeSamples` 10,000), the 800000 evaluation limit (`MaximumEvaluations`) and the 1e-9 thresholds in AGENT_CONTRACTS match the code; one stale sentence fixed in "Machining and playback": "the chamfer start search uses its inverse" became "MotionBuilder, the chamfer start search and RSI sampling share ChamferTcpSolver::solveTcp", because Milestone 1 removed `ChamferTcpSolver::inverse` and `appendStart` calls `solveTcp`.


## Surprises & Discoveries

- Observation: every RSI Sampling deletes both job files before it writes its own (job-file safety rule), so after the two Samplings of baseline step 4 only the last one's job file exists. Each job file must be copied right after its own Sampling. On 2026-11-20 the chamfer Sampling ran second and removed `calibration_data.txt`.
- Observation (2026-11-20): `SceneMachiningPreparation::shutdown` calls `clearPrepared`, which removes both job files, so `C:/RoboCrapJobs` is empty after the application closes. Job files must be copied (or attached) while the application is still running.
- Observation: Milestone 5 rounds the chamfer tool frame to six decimals before export. `shared_data.txt` stays identical (it already writes six decimals), but the `# tool_xyzabc` line of `rsi_plan_<id>.csv` is written with nine decimals (`csvValues`, `'f', 9`) and changes whenever the tool frame has digits beyond the sixth decimal. The Milestone 5 comparison exempts that line.
- Observation: the start search also derives wrist-winding seeds from the starts it accepted (`branches = m_starts`). With `solveTcp`, a start that passes the flange check but fails the TCP residual check is no longer accepted, so its derived seeds are not tried. That start itself could never compile (`appendPath` rejects a start pose outside the same tolerances), but a derived winding could in principle; this matters only when every directly found start fails to compile. The Decision Log's "the first successful start is the same" holds whenever a direct start compiles, which the baseline checks.
- Observation (2026-11-20): the plan files and the start count (5833) were re-verified; only `scenemachiningpreparation.cpp` changed since 0cd8996 (plan 02, from line 217), below the lines this plan cites.


## Decision Log

- Decision (user, 2026-11-20): remove `IgnorePreviewJointPositionLimits` and every branch that depends on it.
  Rationale: the switch is `constexpr false` (`kr10kinematicmodel.h` line 22), so only the enforcing branches ever run.
  Date/Author: 2026-11-20, user.
- Decision: keep one joint-limit test.
  - `withinLimits(joints, model)` becomes `chamferJointPositionError(joints, model).isEmpty()` and keeps its name for its four boolean callers.
  - `chamferJointPositionError` stays the single comparison.
  - `ChamferTcpSolver::inverse` is deleted, and `appendStart` uses `solveTcp`.

  Rationale: both functions compare against `qMin`/`qMax` after a finiteness test. `inverse` repeats the first half of `solveTcp`, and its TCP value is unused. `solveTcp` additionally checks the TCP residual against the same tolerances that `MotionBuilder::appendPath` applies to the first LIN after the start PTP. A start that `solveTcp` rejects therefore fails to compile anyway, and the first successful start, which is the one chosen, is the same. Only the error text when every start fails can differ.
  Date/Author: 2026-11-20, plan author.
- Decision: keep the finiteness test of `state.speed` and `state.acceleration` in `ChamferCentralMotion::evaluate` when removing the unused ChamferCentralPose fields.
  Rationale: the test decides which times return `std::nullopt`, and removing it would change behaviour for non-finite profiles.
  Date/Author: 2026-11-20, plan author.
- Decision: `ChamferMotion::boundaries()` is removed in Milestone 2 instead of being kept next to `boundary()`.
  Rationale: after the named-boundary change every caller uses `boundary(ChamferBoundary)`; the array accessor had no caller left.
  Date/Author: 2026-11-20, implementing agent.
- Decision: the joint-step helper is `childTransform(const M4d& parent, std::size_t index, double q)` instead of the planned `jointTransform(index, q)`.
  Rationale: `solveFK` and `armTransform` evaluate `(T * localTransform * rotation).eval()`, which Eigen groups as `(T * localTransform) * rotation`. `T * jointTransform(index, q)` would compute `T * (localTransform * rotation)`, a different rounding, and the milestone requires bitwise-identical results. Taking the parent keeps the existing product order.
  Date/Author: 2026-11-20, implementing agent.
- Decision: `scenemachiningpreparation.cpp` keeps `RsiCycleSeconds` and `TestLinSettleCycles` instead of using the new `rsicoordinates.h` constants.
  Rationale: `rsicoordinates.h` is a private header of RoboCrapTrajectories (`src/trajectories/motion/`); the application sees only `src/trajectories/include`. Using it would need a relative include across modules or moving the header to the public include directory with a CMake change. The two application constants belong to the RSI_Probe_Check test LIN, a separate controller program.
  Date/Author: 2026-11-20, implementing agent.
- Decision: the probe import limit is 10,000 points per file.
  Rationale: hand-probed files in `data/probe_points/` hold at most 8 points. The cylinder fit makes up to 6 × 200 passes over the points on the GUI thread.
  Date/Author: 2026-11-20, plan author.


## Outcomes & Retrospective

(2026-11-21) Milestones 1–5 are implemented and accepted on the disconnected baseline: Generate (chamfer and calibration) and RSI Sampling produce identical console lines, job files and plan files, the only change being the six-decimal `# tool_xyzabc` line of the chamfer plan (Surprises). The dead `IgnorePreviewJointPositionLimits` switch, the duplicate joint-limit check, `ChamferTcpSolver::inverse`, the optional central motion, `ChamferArcLengthParameters` and the duplicated geometry helpers are gone; phase indices and RSI constants are named; imports above 4 MiB or 10,000 points are refused. The plan's file set went from 5833 to 5789 lines (−44), with growth only where named constants and shared helpers replace copies.

Remaining: the Milestone 5 equipment sessions (`RSI_Probe_Calibration` with `StepLimit` = 10 and `RSI_Chamfer_Hole` in T1), collected in `plans/REMAINING_CHECKS_EXECPLAN.md` (B1, C).

Lesson: the stale "inverse" sentence in AGENT_CONTRACTS shows that removing a function needs a search of the contract documents for its name as well as of `src`.


## Context and Orientation

**Joint limits.** A KR10 joint has limits `qMin`/`qMax` in degrees (`JointModel` in `src/kinematics/include/kinematics/kr10kinematicmodel.h`). The switch is used in these places:
- `jointsegments.cpp` line 62: the spline position test;
- `jointsegments.cpp` line 88: inside `withinLimits` (lines 85-92);
- `kr10kinematics.cpp` lines 227-232: inside `resolveJointAngle`;
- `robotcontroller.cpp` lines 67-68 (a console message) and line 166 (preview FK limits).

`chamferJointPositionError` (`tcpsolver.h` 13-23) returns an error text for non-finite or out-of-range joints. The callers of `withinLimits` are:
- `chamfermotion.cpp` 79 (assembled motion check);
- `tcpsolver.h` 89 (inside `inverse`);
- `timedmotion.cpp` 47 (playback);
- `jointsegments.cpp` 114 (`validRobot`).

**Start search.** `ChamferTcpSolver` (`tcpsolver.h` 47-101) is the checked TCP inverse kinematics. `solveTcp` (58-74) runs seeded IK, the joint limits, one FK, and flange and TCP residual checks. `inverse` (84-95) runs seeded IK, `withinLimits`, one FK and the flange check, and returns a pair whose `M4d` is never read. Its only caller is `MotionCompiler::appendStart` (`chamfermotion.cpp` 100-108). `appendCentral` (117-124) checks the seed with `chamferJointPositionError` again. The seed comes either from the last `solveTcp` solution (already checked) or from an `inverse` start (checked by `withinLimits`), so that check cannot fail.

**ChamferMotion** (`src/trajectories/include/trajectories/chamfermotion.h`, `src/trajectories/chamfer/chamfermotion.cpp`) is the saved chamfer animation.
- Its private constructor (`.cpp` 136-137) is called only from `create` (147-159), which always calls `setCentralMotion` (139-142) next.
- `m_centralMotion` is therefore `std::optional` without need. `m_parameters` duplicates the central motion path's `parameters()`.
- Two checks can never take their failure branch:
  - `chamferrsistream.cpp` 52-53 checks the optional ("analytical timing is missing");
  - `src/application/scene/scenegeometry.h` 80 has a ternary on it.

The eight phase boundaries are `std::array<qsizetype, 8>`, filled at `chamfermotion.cpp` 82-85: HOME start, P_s in, lead start, machining start, machining end, lead end, P_s out, HOME end. Code elsewhere uses bare indices into them:
- `chamferrsistream.cpp` 59, 72, 88, 101;
- `scenemachiningpreparation.cpp` 103-109, 121;
- `sceneobject.cpp` 173-175.

**ChamferCentralPose** (`chamfercentralmotion.h` 20-28) has the fields `tcp`, `phase`, `progress`, `distance`, `speed` and `tangentialAcceleration`. Only `tcp` (chamferrsistream.cpp 146, 167) and `progress` (chamfercentralvalidation.cpp 229) are read.

**`ChamferArcLength::create`** (`chamferarclength.h` 31) takes a `ChamferArcLengthParameters` struct with defaults. Its only caller (chamfercentralmotion.cpp 123) passes none.

**Commented code.** `motionbuilder.cpp` 39-44 is a commented-out per-joint acceleration check. The contract keeps joint-acceleration rejection disabled until the user asks otherwise, so the comment block is dead text. `timedmotion.h` 23-24 already documents the decision.

**Geometry duplicates.**
- **Sign rule** ("largest-magnitude component positive, ties to the lower index"), written three times: `plane.cpp` 37-41, `surfaceintersection.cpp` 21-28 (`canonical`), `boundedcylinder.cpp` 212-215.
- **Reference vector** `abs(x) < 0.9 ? UnitX : UnitY`, twice: `boundedcylinder.cpp` 34, `boundedplane.cpp` 27.
- **Centring block**, twice: `plane.cpp` 12-26 and `spherefit.cpp` 11-23. The block fills a matrix with points relative to the first point, subtracts the mean and checks finiteness.
- **V3d ↔ `std::array<double,3>` conversions**, three times: `boundedcylinder.cpp` 27-30, `boundedplane.cpp` 13-16, `surfaceintersection.cpp` 10-13.
- **Probe compensation check** (radius finite and non-negative, dir ±1), three times: `utils.cpp` 59-60, `boundedplane.cpp` 68-69, `boundedcylinder.cpp` 237. The point finiteness loop at `utils.cpp` 69-71 repeats checks that `jsonArrayToPoints` (JSON numbers are finite) and the factories already make.
- **`intersectSurfaces`** (`surfaceintersection.cpp` 72-99) computes `tolerance` only for a finiteness test at line 90, and calls `EdgeGeometry::fromEllipse` twice (90 and 98). Every failure caught at 90 also fails at 96-98 with the same result.
- **Kinematics.** `kr10kinematics.cpp` repeats the joint-step transform (`armTransform` loop 117-127 vs `solveFK` 31-37), the wrist centre (55-58 and 252-255), `alpha = atan2(bx, by)` (100 and 173) and `hypot(bx, by)` (19 and 159).

**Chamfer duplicates and literals.**
- `positiveFinite(double)` is defined three times: `chamferarclength.cpp` 309, `chamfercentralmotion.cpp` 21, `chamfercentralvalidation.cpp` 13.
- The ramp/cruise boundary `start + (index == 0 ? rampSeconds : cruiseSeconds)` is computed in `ChamferCentralMotion::initializeTiming` (103-104) and in `makePreviewSchedule` (`chamfercentralvalidation.cpp` 145-146).
- Phase vocabulary: preview spans store phase labels as `QString` (`chamfercentralvalidation.h` 11, built at `.cpp` 163-164, compared at 216). Index↔phase casts appear at `chamfercentralmotion.cpp` 123 and 152 and `chamfercentralvalidation.cpp` 139.
- `chamfercentralvalidation.cpp` 158 hard-codes "200000 points" next to `MaximumSamples` (line 10). The same limit and the 1e-9 s time resolution recur in `jointsegments.cpp` 255, `chamfercentralvalidation.cpp` 160 and `motionbuilder.cpp` 134.
- `ChamferPath::create` (`chamferpath.cpp` 143-146) recomputes `stagingPoint()` only to test finiteness.
- `makePreviewSchedule` mixes named and braced returns (141, 158, 161 vs 168).
- `TouchMotionBuilder::clearanceError` (`probetouchmotion.cpp` 82-104) recomputes `required` at 92 and prints a different threshold in its two messages.

**Import.** `SceneSurfaceImporter::readSamples` (`src/application/scene/scenesurfaceimporter.cpp` 30-35) reads the whole user-chosen file, and `decodeProbeSamples` (`src/geometry/utils.cpp` 45-73) accepts any number of points.

**RSI side (Milestone 5).**
- `probersistream.cpp` 26-36 has the helper `wirePose`: principal Euler angles, six-decimal rounding, and the Kr10PosEps/Kr10RotEps check.
- `chamferrsistream.cpp` re-implements it inline at 108-114 (tool frame, checked before rounding and exported unrounded), 118-124 and 146-154.
- The 4 ms cycle and the 50 settle cycles are defined in `chamferrsistream.cpp` 21-22, `probersistream.cpp` 20-22 and `scenemachiningpreparation.cpp` 27, 30.
- The rate tolerance 1e-6 is declared in `chamferrsistream.cpp` 24 and `motionbuilder.cpp` 12, and written as `1.0 + 1e-6` in `jointsegments.cpp` 65 and `tcpsolver.h` 30.


## Plan of Work

### Milestone 1: one joint-limit rule

- **Remove the switch.**
  - Delete the constant and its comment from `kr10kinematicmodel.h`.
  - In `jointsegments.cpp` line 62, drop the `!IgnorePreviewJointPositionLimits &&` term.
  - Move `chamferJointPositionError` from `tcpsolver.h` into `jointsegments.cpp` and declare it in `jointsegments.h` next to `withinLimits`. `tcpsolver.h` already includes `jointsegments.h`, so `solveTcp` keeps calling it unchanged.
  - Replace the body of `withinLimits` with `return chamferJointPositionError(joints, model).isEmpty();`.
  - Delete the switch branch in `resolveJointAngle` (227-232).
  - In `robotcontroller.cpp`, delete lines 67-68 and drop the switch term at 166.
- **Start search through `solveTcp`.** In `appendStart`, use `const auto solved = m_solver.solveTcp(target, previous); if (!solved.solution) return;` and `solved.solution->q`. Delete `inverse` from `tcpsolver.h`, and update the comment above `ChamferTcpSolver` ("the chamfer start search uses …" stays true with `solveTcp`).
- **Seed check.** In `appendCentral`, delete the seed check and the `seedError` text.

### Milestone 2: chamfer motion data

- **ChamferMotion.** The private constructor becomes `ChamferMotion(TimedMotion timed, const std::array<qsizetype, 8>& boundaries, ChamferCentralMotion central)`, storing `ChamferCentralMotion m_centralMotion` by value. `parameters()` returns `m_centralMotion.path().parameters()`, using the existing accessors `ChamferCentralMotion::path()` (chamfercentralmotion.h) and `ChamferPath::parameters()`. `centralMotion()` returns `const ChamferCentralMotion&`. Delete `m_parameters`, `setCentralMotion` and the optional. In `create`, construct with `compiler.centralMotion()`. Update the callers:
  - `chamferrsistream.cpp` 52-53: delete the unreachable check; use `.` instead of `->` at 144 and 165;
  - `scenegeometry.h` 80: return `motion().centralMotion().duration()`.
- **ChamferCentralPose.** Delete `phase`, `distance`, `speed` and `tangentialAcceleration` from the struct and their assignments in `evaluate` (181-185). Keep the finiteness test at 172-173.
- **ArcLength parameters.** Move `ChamferArcLengthParameters`' default values into named constants in `chamferarclength.cpp`'s anonymous namespace. Remove the struct and the parameter from `create`, and the `ArcTableBuilder` member and constructor parameter that carry it. Delete the check "Arc-length tolerances must be finite and positive." (`chamferarclength.cpp` 326-329), because it would test compile-time constants. Before deleting the struct, confirm that nothing else uses it (`grep -rn ChamferArcLengthParameters src`).
- **Commented block.** Delete `motionbuilder.cpp` 39-44.
- **Named boundaries.** In `chamfermotion.h`, add `enum class ChamferBoundary : qsizetype { HomeStart, StagingIn, LeadStart, MachiningStart, MachiningEnd, LeadEnd, StagingOut, HomeEnd, };`, `constexpr qsizetype ChamferBoundaryCount = 8;` and `constexpr qsizetype ChamferPhaseCount = 7;`. Use them for the array type, the phase-count check (`chamfermotion.cpp` 81-85) and every index in `chamferrsistream.cpp`, `scenemachiningpreparation.cpp` and `sceneobject.cpp`. Add a small inline accessor `qsizetype boundary(ChamferBoundary which) const` to ChamferMotion so callers do not cast.

### Milestone 3: chamfer helpers

- **`positiveFinite`.** Declare `bool positiveFinite(double value);` once in `src/geometry/include/geometry/utils.h`, next to `nearlyEqual`, and define it in `src/geometry/utils.cpp` (`return std::isfinite(value) && value > 0.0;`). Delete the three local copies, and add `#include "geometry/utils.h"` to each of the three `.cpp` files that does not already include it.
- **Ramp/cruise switch.** Add to `struct ChamferCentralProfile` (chamfercentralmotion.h) the const member

      // Phase-local time of the ramp/cruise switch; none for machining or a ramp-only lead.
      std::optional<double> switchSeconds(ChamferPhase phase) const
      {
        if (phase == ChamferPhase::Machining || cruiseSeconds <= 0.0) return std::nullopt;
        return phase == ChamferPhase::LeadIn ? rampSeconds : cruiseSeconds;
      }

  This is exactly the rule both copies apply today: `index != 1 && profile.cruiseSeconds > 0.0`, then `index == 0 ? rampSeconds : cruiseSeconds`. Use it in two places:
  - `initializeTiming`: `if (const auto change = profile.switchSeconds(phase)) { const double at = start + *change; … }`;
  - `makePreviewSchedule`: `if (const auto change = profile->switchSeconds(phase)) stops.append(start + *change);`.
- **Phase vocabulary.** In `src/trajectories/include/trajectories/chamferpath.h`, next to `enum class ChamferPhase`, add:

      constexpr std::array<ChamferPhase, 3> ChamferPhases{ChamferPhase::LeadIn, ChamferPhase::Machining, ChamferPhase::LeadOut};
      // "Lead-in", "Machining" or "Lead-out"; the playback status and error texts show these.
      inline QString chamferPhaseLabel(ChamferPhase phase) { … switch returning the three QStringLiteral texts … }

  Then:
  - replace `QString phase` in `ChamferCentralPreviewSpan` with `ChamferPhase phase = ChamferPhase::LeadIn;`;
  - in `CentralPath::schedule`, compare spans by `phase` and append `chamferPhaseLabel(span.phase)` to `result.phases`;
  - replace the three `static_cast<ChamferPhase>(index)` with `ChamferPhases[index]`, keeping the existing `phaseIndex` for phase → index;
  - in `ChamferArcLength::create`, print `chamferPhaseLabel(phase)` instead of `static_cast<int>(phase)`.

  The labels shown in the UI stay identical.
- **Literals.** Name the 200000-point limit and the 1e-9 s time resolution once in `jointsegments.h` and use them in the three files. Build the message at `chamfercentralvalidation.cpp` 158 with `.arg(MaximumSamples)`. Name the cylinder-fit literals (`boundedcylinder.cpp` 116, 127, 172, 225) and the subdivision depth (`chamferarclength.cpp` 48) in their files' anonymous namespaces. Replace the literal 1e-9 at `spherefit.cpp` 36 with `GeomConst::Eps`, which has the same value.
- **Staging recompute.** In `ChamferPath::create`, construct the path first and test `path.stagingPoint().allFinite()` instead of recomputing it. Keep the order of the other checks.
- **Returns.** In `makePreviewSchedule`, set `schedule.error` and `return schedule;` on the failure paths after line 137.
- **Strut message.** In `clearanceError`, compare against `required` and print it with `'f', 4` in both messages.

### Milestone 4: geometry and kinematics

**Geometry helpers.** In `src/geometry/include/geometry/utils.h` and `utils.cpp`, add these non-template free functions:
- `V3d dominantPositive(V3d direction)`: the sign rule; ties go to the lower index, matching the existing strict-greater loops and `maxCoeff(&index)`'s first maximum;
- `V3d referenceAxis(const V3d& direction)`;
- `std::optional<CenteredPoints> centerPoints(const QVector<V3d>& points)`, with `struct CenteredPoints { Eigen::MatrixXd centered; V3d centroid = V3d::Zero(); };` declared above it. It returns nullopt for an empty input or any non-finite point, centroid or centred entry. The body is the current `plane.cpp` 12-26 block: rows relative to the first point, mean offset, centroid = first point + mean offset, rows minus mean offset. Callers keep their own minimum-count checks, `Plane::fromPoints` 3 and `fitSphere` 4, before calling it. `fitSphere` uses `centroid` where it now uses `mean`;
- `V3d toV3d(const std::array<double, 3>&)` and `std::array<double, 3> toArray(const V3d&)`.

Replace the copies listed in Context and delete the local ones. Each is requirement-driven: the identical rule is needed in three or two files, and no existing helper provides it.

**Probe samples.** Give `ProbeSamples` (utils.h) a const member `bool hasValidCompensation() const` (radius finite and non-negative, dir ±1). Use it in `decodeProbeSamples` and in both `fromSamples` factories, which stay public and keep their own call. Delete the finiteness loop at `utils.cpp` 69-71.

**Import limit.** Add `constexpr qsizetype MaximumProbeSamples = 10000;` next to `ProbeSamples`. `decodeProbeSamples` returns nullopt when the array is larger. In `scenesurfaceimporter.cpp`:
- **Byte limit.** Add `constexpr qint64 MaximumImportBytes = 4 * 1024 * 1024;`. 10,000 points of about 70 characters each are about 0.7 MB, so 4 MiB leaves a wide margin.
- **New error value.** Add `ReadError::TooLarge`. In `readSamples`, return it when `file.size() > MaximumImportBytes`, before `readAll`. Its `readErrorText` is `tr("The selected file is larger than %1 MiB.").arg(MaximumImportBytes / (1024 * 1024))`.
- **Samples text.** Change the `ReadError::Samples` text to begin "Expected at most %1 finite [x, y, z] points …", with `.arg(MaximumProbeSamples)`, so a refused large array is explained.

**`intersectSurfaces`.** Delete `tolerance` (73-74) and the line-90 pre-check, so the first `fromEllipse` call goes. Every path still reaches the checks at 96-98.

**`Kr10Kinematics`.** Add private const members `M4d jointTransform(std::size_t index, double q) const` and `V3d wristCentre(const M4d& flange) const`. Use them in `solveFK`, `armTransform`, `calculateIK` and `isOverhead`. Add two more private const member functions, with no stored state, because the class keeps only its model reference by contract:
- `double forearmLength() const { return std::hypot(m_model.ik.bx, m_model.ik.by); }`, used in `isValid` (line 19, currently `hypot(by, bx)`, the same value) and `solveArm` (159);
- `double elbowOffset() const { return std::atan2(m_model.ik.bx, m_model.ik.by); }`, used in `branchFrom` (100) and `solveArm` (173).

**Enum.** Add the trailing comma to `IntersectionStatus` (`surfaceintersection.h` 31).

### Milestone 5: RSI-side tidy

- **`wirePose`.** Move it from `probersistream.cpp` into `src/trajectories/motion/rsicoordinates.h` as an inline function returning `std::optional<V6d>`. Use it in `chamferrsistream.cpp` for the initial pose, the final pose and the tool frame, so `toolCoordinates` is six-decimal and checked like the probe job's. In `scenemachiningpreparation.cpp`, write `stream.toolCoordinates` unchanged; it is now already rounded.
- **Constants.** Define the 4 ms cycle and the 50 settle cycles once in `rsicoordinates.h`, and the rate tolerance once in `jointsegments.h`. Replace the local definitions and the `1.0 + 1e-6` literals.

`shared_data.txt` should come out identical, because the tool frame was already written with six decimals. The difference is that the checked value and the written value are now the same.


## Concrete Steps

Work in the repository root. Line counts before and after each milestone:

    wc -l src/geometry/*.cpp src/geometry/include/geometry/*.h src/kinematics/*.cpp src/kinematics/include/kinematics/*.h src/trajectories/chamfer/*.cpp src/trajectories/chamfer/*.h src/trajectories/include/trajectories/*.h src/trajectories/motion/*.cpp src/trajectories/motion/*.h src/trajectories/probe/*.cpp src/application/scene/scenesurfaceimporter.cpp src/application/scene/scenegeometry.h src/application/robot/robotcontroller.cpp
    git diff --check

**Baseline (user, once before Milestone 1, then after each milestone).** Use the existing build `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, after checking `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt`, and start the application disconnected.
1. **Surfaces.** In Measuring/Rough, import "Plane from JSON" and "Cylinder from JSON" from `data/probe_points/edge_cyl_plane/`, select both and press "Intersect Selected".
2. **Chamfer path.** In Machining, select the edge and Generate with the default settings. Copy the console line `Chamfer preparation: … points, … s movement`.
3. **Calibration path.** In Measuring/Calibration, Generate. This needs `resources/json/calibration_sphere.json` beside the executable. Copy the calibration console line.
4. **RSI Sampling (only if this PC has the RSI adapter 172.31.1.47).** Bind the RSI socket in Settings; this binds a local socket only. Run RSI Sampling for the chamfer path and copy `C:/RoboCrapJobs/shared_data.txt`; then run it for the calibration path and copy `C:/RoboCrapJobs/calibration_data.txt` (each Sampling deletes both job files first). The agent copies these and the two newest `records/<id>/rsi_plan_<id>.csv` (chamfer: `records/<timestamp>/`; calibration: `records/calibration_<timestamp>/`) into `C:\RoboCrapBaseline\<stage>\`, outside the repository.

**Comparison (user runs it).** Compare the copied files of two runs, ignoring the first `# plan_id` line. The point counts, movement seconds and job files must be identical:

    python -I -c "import sys; a=open(sys.argv[1]).read().splitlines(); b=open(sys.argv[2]).read().splitlines(); print([i for i,(x,y) in enumerate(zip(a,b)) if x!=y and not x.startswith('# plan_id')][:10], len(a), len(b))" <old> <new>

Expect `[] N N`. After Milestone 1, the error texts of failing Generate attempts may differ (see the Decision Log); successful results must not.


## Validation and Acceptance

- **Milestones 1-3 and 5.** Accepted when the user's baseline comparison shows identical point counts, movement durations, job files and plan files. Run the console line check whenever RSI Sampling is unavailable.
- **Milestone 4.** Accepted when the baseline comparison is identical. The edge has no Properties rows (`SceneObject::properties` returns rows only for planes, cylinders and spheres); the chamfer path is built from the edge ellipse and the cylinder origin and axis, so identical chamfer job and plan files show that the plane, cylinder and edge are unchanged. A JSON file with 10,001 points is refused with the new error text. Create it outside the repository with `python -c "import json; json.dump([[float(i), 0.0, 0.0] for i in range(10001)], open('big_probe.json', 'w'))"`, then import it with "Plane from JSON".
- **All milestones.** The searches in the Interfaces section find no removed symbol.
- **Equipment, after Milestone 5 (user).** Run one short session of each RSI program with the files the changed code produced:
  - `RSI_Probe_Calibration` with `StepLimit` = 10, from RoboCrap's "Start Calibration" in AUT EXT;
  - `RSI_Chamfer_Hole` in T1 on a test workpiece or in free space at a safe staging distance.

  Expected:
  - both runs behave as before: the same HOME, PsIn and LeadStart poses, the calibration searches stop on the probe, and the chamfer stream completes with the Result handshake;
  - `records/<id>/rsi_packets_<id>.csv` shows no rejected replies beyond those of earlier sessions.

  The job-file comparison above already shows that the files are identical, so this run confirms the controller side only.


## Idempotence and Recovery

The edits are source edits, and repeating a completed step changes nothing. The baseline files live outside the repository and can be regenerated at any time. If a milestone changes a result, revert only that milestone's files with git, at the user's request, and record the cause in Surprises.


## Artifacts and Notes

The baseline lines and comparison output are to be pasted here as each milestone completes.


## Interfaces and Dependencies

No dependency changes. At the end:
- `IgnorePreviewJointPositionLimits`, `ChamferTcpSolver::inverse`, `ChamferMotion::setCentralMotion` and `ChamferArcLengthParameters` no longer exist; check with `grep -rn "<name>" src`;
- `ChamferMotion::centralMotion()` returns `const ChamferCentralMotion&`;
- `ChamferBoundary`, `ChamferBoundaryCount` and `ChamferPhaseCount` are in `chamfermotion.h`;
- `chamferPhaseLabel` sits next to `ChamferPhase` in `chamferpath.h`;
- the geometry helpers are in `geometry/utils.h`;
- `MaximumProbeSamples` and `ProbeSamples::hasValidCompensation` are in `geometry/utils.h`.

**Contracts.**
- "Geometry and scene", import paragraph: add one sentence on the 10,000-point limit and the file-size limit of `SceneSurfaceImporter`.
- "Machining and playback": no change. The rule that joint-acceleration rejection stays disabled is kept; only the commented code goes.
