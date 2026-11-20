# Whole-project review: RoboCrap at 0cd8996 (2026-11-20)

I changed no file, ran no build and used no hardware. Five defects are confirmed. The worst is that an OCCT exception can escape a Qt slot and terminate the application. There is one C++ rule violation, and six layout-jump violations in QML.

## Reviewed state
| Item | Value |
|---|---|
| Commit | `0cd8996033021e7080898455cd0db10bea4fb078`, branch `master` |
| Upstream | `master` has no upstream configured. I compared with `origin/main`, the remote default branch, which is the same commit. |
| Working tree | Clean, so no file in scope differs from HEAD |

## Confirmed defects (highest impact first)
| # | Location | When it happens | Consequence | Correction |
|---|---|---|---|---|
| 1 | src/viewport/occt/occviewport.cpp:43, :78, :103; src/viewport/adapters/occrobotadapter.cpp:123, :138; src/viewport/occt/occviewwindow.cpp:23 | OCCT throws `Standard_Failure` inside `setSelection`, `setSpindleFrames`, `setActiveTool` or the `screenChanged` resize. These run from slots (main.cpp:120, RobotController::synchronizeTools). The same OCCT calls are guarded elsewhere (occrobotadapter.cpp:79, :116). | The exception leaves a Qt slot, which is undefined behaviour and in practice terminates the app. | Catch `Standard_Failure` at these OccViewport entry points. Return `ViewportOperationResult` so OccController can emit `message`. |
| 2 | src/application/scene/scenemachiningpreparation.cpp:417-421 | `QFile::remove` fails on `shared_data.txt` or `calibration_data.txt`, for example while the share holds the file open. | The failure is only logged with `qInfo`, and Sampling continues. The stale job stays in C:/RoboCrapJobs, so a later KRL run can move PTP PsIn and LIN LeadStart of an older path. | `clearPrepared` returns the error text. Callers show it with `setError` and refuse Sampling and the test LIN while an old file remains. |
| 3 | scenemachiningpreparation.cpp:250, :260, :335 compared with scenemachiningpreparation.h:59 | Sampling or the test LIN is clicked while the run state is Requested or Starting. The guards block only Started, while `calibrationRunning` also covers Requested and Starting. | `clearPrepared` ends the run, deletes the job and sends an empty table, but the PLC start is not aborted. Program 1 then returns after HOME, or every search stops as missed. | Block these actions whenever the run state is not Idle (one shared guard). |
| 4 | src/application/scene/scenecalibration.cpp:514-518 compared with :98-118, :183-185 | A fit gives `repeatability`, `quantile95` or a contact distance of 0 or less. This is unlikely with real touch data. | The writer saves the values. At the next start the reader rejects the whole file, including the valid `primary` block, and the next write overwrites it. | Validate with `calibrationSummary(summaryObject(...))` before saving and before Reassign. |
| 5 | src/viewport/adapters/occsceneadapter.cpp:220-228, :212-216 | `removeParts` fails or throws during object removal or replacement. | The error goes only to `qWarning`, but the presentation entry is erased anyway. The OCCT parts stay displayed and can never be removed. | Keep the IDs of failed parts, as `OccRobotAdapter::clear` does, and return a result to `OccController::message`. |
| 6 | src/viewport/occt/occinputcontroller.cpp:82 | A precision touchpad or a high-resolution wheel sends angle deltas below 6. | Those events never zoom, but every one still triggers a redraw. | Accumulate the remainder in OccInputController and skip the redraw when the zoom is 0. |
| 7 | src/application/qml/Views/Network/Network.qml:145-158 | The VFD profile (db.json:35) is selected; it has no device in the hub. | Connect is enabled, and clicking it does nothing. | Disable Connect when `runner` is null. |

Lower impact, all confirmed:
- **MachiningPanel.qml:50:** a percentage entered as 7 reopens as "7.000000000000001". Generate still works, because `validDraft` uses `Number()`, but the validator limits the field to 8 decimals.
- **EndEffectorPanel.qml:57-62:** Close keeps unapplied drafts, and they reappear when the panel is reopened.
- **scenesurfaceimporter.cpp:32 and geometry/utils.cpp:29:** an imported JSON file has no size or point-count limit, and the fit runs synchronously on the GUI thread.
- **Main.qml:301:** "Importing surface…" can never render, because `busy` is set and cleared inside one synchronous call.
- **probetouchmotion.cpp:92-94:** the strut-clearance error prints a threshold that differs from the value it compares.
- **MainMenuBar.qml:56,59:** the logo has both top and verticalCenter anchors, so `height: 20` is ignored.
- **QxTextInput and QxSwitch:** the placeholder ignores the field padding (QxTextInput.qml:45), and the switch knob is 1 px off-centre (QxSwitch.qml:44).
- **plcdevice.cpp:287-291:** the PLC log is truncated only if the previous connection was opened in the same second.
- **main.cpp:51-52:** two font loads are not checked.
- **devicehub.h:21:** `keys` is unused and wrongly marked CONSTANT.

## Project-rule violations (step 5)
| Location | Rule | Correction |
|---|---|---|
| src/devices/fts/ftsdevice.cpp:43, callers at :370-385 | Result channels: `acceptAxis(qint32& dst, …)` is an in-out reference | Return `std::optional<qint32>` and let the caller assign `m_lastPub` |
| qml/Modules/Components/QxProgressBar.qml:64-71, Main.qml:247-250 | Interface stability: the `toFixed(3)` value width drives the FTS window width while values stream | Fixed-width, right-aligned value text |
| DeltaPanel.qml:25-31, FtsPanel.qml:25 | The "Connect … in Settings." hint changes the PLC and FTS window heights on connect and disconnect | Reserve its line, or use an overlay |
| qml/Modules/Components/QxButton.qml:17-29; FtsPanel.qml:52, :69, :77; Network.qml:143 | Button width follows the caption (Start/Stop, Record/Recording…, Connect/Disconnect), so neighbours and windows move | Stable button width, for example from TextMetrics of the longest caption |
| Network.qml:203-209, Main.qml:268 | The status label never elides; long text widens the Settings window | `Layout.preferredWidth: 0`; do not bind the window size to the live `contentWidth` |
| MachiningPanel.qml:174-183 | The error label's visibility and wrapping move Generate/Cancel | Fixed-height message region |
| MachiningPanel.qml:105-114 | The playback label appears when Dry Run starts and pushes the editor down | Permanent fixed-height row |

- **Other rules:** no lambdas, project templates or `throw`. All 17 try/catch blocks guard OCCT calls. No function has more than four parameters.
- **Mitigation compliance:** no skill mitigation conflicted with AGENTS.md. One mitigation conflicts with a recorded contract (making PlcMessageManager a non-QObject), so I moved it to the decisions table.

## Architecture
**Dependency graph.** Every `#include` and link edge matches "Build and modules":
- Geometry includes only geometry, Qt and Eigen.
- Kinematics, Devices and Viewport each depend on Geometry.
- Trajectories depends on Kinematics and Geometry.
- Application depends on all of them.
- Devices has no QML and no Trajectories edge; Viewport has no Application or Trajectories edge.

One undocumented private include: Application adds `src/devices/include/devices/{fts,plc}` for the foreign-type registrar.

**Traces.** All of these match ARCHITECTURE §4 and §5:
- Generate through Dry Run.
- One PLC command through its reply.
- Generate, Calibration and RSI sampling through `setCorrections`.
- The FTS save wait.
- The CAD workers.
- The shutdown order: `ScenePlayback::end` connects first at sceneplayback.cpp:14, before main.cpp:106-107.

**Duplication.** The Run enable rule exists three times: in the PLC, in RobotStartPanel.qml:57, and in `plcReady()` (scenemachiningpreparation.cpp:466-471). The contract requires only the QML copy. Proposal: PlcDevice publishes one `robotRunAllowed` state key, and both consumers read it.

| Decision | Verdict | Evidence and trade-off |
|---|---|---|
| main()-owned singletons via `claimQmlSingleton` | Keep | backendqmltypes.h:24-33. The engine is declared last (main.cpp:136). Qt 6.8 has no `setExternalSingletonInstance`. |
| ControlIO (FTS+RSI) and GeneralIO (PLC); blocking `stopAll` | Keep | devicehub.cpp:22-79. Cost: FTS emits 7 kHz on the RSI event loop (ftsdevice.cpp:290). The latency impact is an estimate; it was never measured. |
| String `invoke` API through `buildApi` | Keep, with one change | Also exposes `writeMessage` (plcdevice.h:27), which nothing calls. Make it private. |
| `std::async` plus a 30 ms poll | Keep | scenemachining.cpp:178-196. Destroying the future blocks at shutdown, as documented. |
| Empty stream table before every Sampling | Keep, with changes | Close defects 2 and 3. |
| Immutable shared motions with an aliasing TimedMotion handle | Keep | scenemachining.cpp:319-321; sceneplayback.cpp:64. |
| Single robot load | Keep | The re-entry branch at occcontroller.cpp:243 is unreachable. |
| QmlChartBridge stays in Application | Change | Never instantiated or registered; only commented lines main.cpp:78-80 mention it. Removing it drops the Qt6::Charts dependency, so it needs your decision. |
| PlcMessageManager remains a QObject | Insufficient evidence | Stateless; created and deleted with `new`/`delete` (plcdevice.cpp:60, 87-88). The `PlcMessage` QML type is unused, and changing it changes the QML API. |
| Retained FTS signals and RDTResponse | Keep | Your decision. Cost: `m_batch` grows on every sample; unmeasured. |
| Joint-acceleration rejection disabled | Keep | The commented block at motionbuilder.cpp:39-44 can be deleted. |
| PLC: one request in flight plus heartbeat | Keep | plcdevice.cpp:226-271; the queue is cleared on disconnect (:405-413). |

**Not examined:**
- OCCT 8.0.0/MinGW gating, BREP deployment and the records/settings directories.
- OccScene ID counter, outline reuse and decorative infinite structures.
- The probe model frame and DrawingModel loading.
- Euler and tolerance conventions, IK branches and `fitSphere` thresholds.
- Arc-length quintic Hermite inverse, the 800000 limit and the order of MotionBuilder checks.
- LIN subdivision, spline checks and the RSI chord bound.
- RSI capture files, the calibration fit, the test LIN, the QML number-format rules and hot reload.

## Reduction opportunities (RSI files excluded; counts estimated unless marked measured)
| Rank | Removal | Net lines | Behaviour kept | Files |
|---|---|---|---|---|
| 1 | QmlChartBridge and Qt6::Charts | about −125 (measured) | Everything; the class is unused | qmlchartbridge.*, CMakeLists ×2, main.cpp |
| 2 | Unused PLC request kinds READ_IO, READ_REG, WRITE_REG, WRITE_RAW; make `writeMessage` private | about −95 (measured) | WRITE_IO, SET_VAR, SNAPSHOT | plcmessagemanager.cpp:159-220, :297-368; plcdevice.cpp:330-341 |
| 3 | Dead OCCT guards; world axes drawn through `setFrameAxes`; redundant destructors; drawer fetched twice | about −60 | Display, picking, fit | occviewcube, occworldaxes, occscene, occviewport, occviewwindow, occpart |
| 4 | Trajectories motion: always-set optional, unused ChamferCentralPose fields, `inverse`, duplicate joint-limit check, ArcLength parameters, commented block | about −50 | Generate results | chamfermotion.*, chamfercentralmotion.h, tcpsolver.h, jointsegments.cpp, chamferarclength.*, motionbuilder.cpp |
| 5 | QML dead code: Tool aliases and signal, collet edit path, unused ids, `confirmed`, `displayonly`, switch text, busy branch | about −56 | The UI | ModeToolBar, EndEffectorPanel, FtsPanel, DeltaPanel, Network, QxTextInput, QxSwitch, Main |
| 6 | Duplicate geometry helpers (sign rule, centering, conversions, probe check, intersection pre-check) | about −30 | Tolerances and results | plane, spherefit, boundedcylinder, boundedplane, surfaceintersection, utils |
| 7 | Duplicated playback logic in SceneMachining/SceneCalibration; SceneObject drawing styles | about −23 | Play, resume, ownership | scenemachining, scenecalibration, sceneobject |
| 8 | PLC endpoint parsing; IO-state decode helper | about −35 | Protocol framing | ftsdevice, plcdevice, plcmessagemanager |
| 9 | `IgnorePreviewJointPositionLimits` switch; duplicate Kr10 formulas | about −20 | FK/IK results | kr10kinematics, robotcontroller, jointsegments |
| 10 | FTS `count` parameter and forwarder; repeated PLC clears; `DeviceHub::keys` | about −15 | Everything | ftsdevice, plcdevice, devicehub |

## Rejected lint rows and skill findings
| Item | Evidence |
|---|---|
| HDR-3 ×48 | None of these files includes `<windows.h>`; only plcdevice.cpp includes winsock2.h, and it has no HDR-3 row. Every object file is newer than its source in the MinGW build. |
| LCY-3 ×21 | Every assert is a pure thread query (`QThread::currentThread() == thread()`, `x->thread()`). |
| LCY-4 ×3 | devicehub.cpp:24 and abstractdevice.cpp:101 receive `new` results. At scenemachiningpreparation.cpp:534, a non-Idle run implies a prepared job, and that job is reset only after `endRun`. |
| LCY-5 ×7 | All bounded: `TraceCapacity` (rsidevice.cpp:335), saves erased when done (:343-350), the exchange at scenecalibration.cpp:333, `removeOne` at scenemodel.cpp:76, `m_plannedSamples`, and a single db.json load. |
| ERR-6 ×3, PAT-10 ×4, ERR-1 ×1 | Multi-argument `arg()`; `return std::move(m_result)` moves a member, where NRVO cannot apply; `open()` is checked inside the condition at rsidevice.cpp:377-378. |
| QL:unresolved-type ×20 | Tool limit: `QQmlPropertyMap` is not exposed declaratively. |
| BND-2 ×8 | The targets have no `text:` binding to destroy, and RobotViewport.qml:28 runs in `onDestruction`. |
| C++ shard 3 D-003 (FTS signals); C++ shard 5 D-003 (`edgePoint`) | Listed under "Retained interfaces" in ARCHITECTURE §7. |
| C++ shard 3 D-005 (PlcMessageManager as a QObject) | The contract says it remains a QObject; moved to the decisions table. |
| QML shard 1 D-004 (dry scan does nothing) | APP_DESCRIPTION documents the Precise submode as a placeholder. |
| C++ shard 6 I-003; C++ shard 7 I-005; QML shard 1 I-001 | IDs are never reused; ConvertStep.cmake writes to `.tmp` and removes it on failure; CalibrationResultWindow is `ApplicationModal`. |

## Coverage and limits
**Targets reviewed:**
- C++ libraries: RoboCrapGeometry, Kinematics, Trajectories, Devices, RoboCrap3D.
- QML modules: Styles, Components.
- The robocrap executable and the cadstep2brep tool.

In total 120 C++ files (16,185 lines) and 38 QML files (4,277 lines). Each shard agent read every file in its shard. I read only the files on the traced paths myself.

**Shards:** C++ 7 (2,359 / 2,304 / 2,722 / 2,317 / 2,277 / 2,294 / 1,912 lines). QML 3 (1,426 / 1,315 / 1,536 lines).

**qmllint:**
- Kit: `C:/Felgo/Felgo/mingw_64/bin/qmllint.EXE`.
- Import path: `-I build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug/qml`.
- Status: "ran". It resolved robocrap_qml_module, Components, Styles and RoboCrap.Viewport3D; only `QQmlPropertyMap` stayed unresolved.
- Build freshness: by the skill's rule the build is stale, because the .qmltypes files are 133 s older than HEAD. HEAD changed no source, though, and every object file is newer than its source, so the types match HEAD.

**Unresolved questions:**
- C++ shard 1 I-001: is overwriting an unreadable settings file intended?
- C++ shard 2 I-002: a run in Requested or Started has no timeout.
- JS-array model rebuilds in ScenePanel.
- Hover redraw cost.
- OCCT UTF-8 paths and handle allocation.
- QPA `screenChanged` behaviour.
- Logo placement in the MenuBar.
- Status-bar Row shrinking.
- QxTextInput width in ScenePanel.
- The Chamfer* type names.

**Skipped checks:**
- CMake configure and build: the skill forbids them.
- Runtime and visual UI checks: the review is read-only. The layout findings come from layout rules, not from observation.
- Hardware: not authorized.
- Framework mode: 0 of the 6 markers were found.
- Qt documentation lookups: I did not run the Qt-docs tool myself. Qt API claims rely on the agents' traces and the installed Basic style sources.
- Style-level D-blocks (FINAL, trailing enum commas, AutoText, unused ids) are accepted from the agents' traces without my re-reading them.

## Run record
| Skill | Lint rows reported / rejected | D-blocks reported / confirmed / rejected | I-blocks resolved / unresolved |
|---|---|---|---|
| qt-cpp-review | 198 / 87 | 66 / 63 / 3 | 19 / 8 |
| qt-qml-review | 329 / 28 | 29 / 28 / 1 | 6 / 5 |

Confirmed D-blocks include merged duplicates (one finding per cause): two in C++, plus two QML pairs and one C++/QML pair.

## Files in this folder
| File | Content |
|---|---|
| final_report.md | This report |
| qt_cpp_review_report.md | Every C++ D-/I-block: finding, trace, mitigation and verdict, with IDs `S<shard>-D<n>` (e.g. "C++ shard 5 D-001" above = S5-D1) |
| qt_qml_review_report.md | Every QML D-/I-block with verdicts, same ID scheme |
| cpp_lint_summary.txt, qml_lint_summary.txt | Linter output, unchanged (per-row locations) |
| ledger.md | Working notes: verification evidence, merged causes, corrected consequences, line-count measurements |

For an ExecPlan, also read PLANS.md, the matching AGENT_CONTRACTS.md sections and, for defects 2–3, the RSI contracts. Line numbers refer to commit 0cd8996; check them against the current code first. Reduction counts are estimates except ranks 1–2.
