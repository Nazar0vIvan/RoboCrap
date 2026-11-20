# Verification ledger: whole-project review of 0cd8996 (2026-11-20)

This file holds the reconciliation notes (step 8 of the whole-project review): the evidence behind every verdict, merged causes, corrected consequences and measurements.
- The finding texts are in `qt_cpp_review_report.md` and `qt_qml_review_report.md`. Block IDs are `S<shard>-D<n>`/`S<shard>-I<n>`; "QML" marks the QML skill.
- Line numbers refer to commit 0cd8996.
- "Read" means I opened the cited lines myself. "Accepted" means I accepted the agent's trace without re-reading the code. That applies to low-severity and style items only.

## 1. State and environment
| Item | Value |
|---|---|
| HEAD | 0cd8996033021e7080898455cd0db10bea4fb078, branch master; working tree clean |
| Upstream | None configured for master; origin/main (the remote HEAD) = 0cd8996 |
| HEAD commit content | Only `.claude/skills/whole-project-review/SKILL.md`, the rename toadd.txt → ai_info.txt and the move sphere.dat → data/probe_points/; no C++, QML or CMake file |
| Coverage | 120 C++ files (16,185 lines), 38 QML files (4,277 lines); every one is listed in a CMakeLists.txt |
| Build cache | build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug: CMAKE_HOME_DIRECTORY = repository, prefix C:/Felgo/Felgo/mingw_64, g++ C:/Felgo/Tools/mingw1310_64, USE_HOT_RELOAD=OFF, FAST_QML_BUILD=ON |
| Build freshness | Newest .qmltypes mtime 1795137045 < HEAD commit time 1795137178, so stale by the skill rule. The previous source commit (8fcbfbc, 1795134464) is older than the build, and the sampled object files are newer than their sources, so the types match HEAD |
| qmllint | `C:/Felgo/Felgo/mingw_64/bin/qmllint.EXE`, `-I <build>/qml`, status "ran". Resolved robocrap_qml_module, Components, Styles and RoboCrap.Viewport3D; only QQmlPropertyMap is unresolved |
| Framework mode | Off: 0 of the 6 markers found in src |
| Shards | C++ 7: 2,359 / 2,304 / 2,722 / 2,317 / 2,277 / 2,294 / 1,912 lines. QML 3: 1,426 / 1,315 / 1,536 lines |

## 2. Project rules (step 5)
| Check | Result | Evidence |
|---|---|---|
| Lambdas | None | Grep for `[...](...){`, `[=`, `[&`, `[this`, `[]{` and `, [`/`( [` introducers in src and tools: 0 hits |
| Project templates, `throw` | None | Grep for `template\s*<` and `\bthrow\b`: 0 hits |
| try/catch | 17 blocks, all compliant | Every try body was read. Each guards OCCT calls (or project wrappers that only call OCCT): occviewwindow.cpp:98, 180, 208; occviewport.cpp:196; occviewer.cpp:48; cadloadworker.cpp:35 (plus `std::exception` from std::string/stream); occcontroller.cpp:188; occsceneadapter.cpp:128, 143, 234; occrobotadapter.cpp:34, 77, 84, 101; cadstep2brep/main.cpp:62 |
| More than 4 parameters | None in project code | The script scan of headers and .cpp definitions found only `gp_Trsf::SetValues` (an OCCT call) |
| Output / in-out references | **1 violation** | ftsdevice.cpp:43 `acceptAxis(qint32& dst, qint32 src)` reads and writes `dst` (callers 370-385). Correction: return `std::optional<qint32>` and let the caller assign `m_lastPub` |
| Non-const reference parameters that are not output channels | Compliant | `OccScene::setFrameAxes`, `resizeFrameAxes` (OccWorldAxes&) and `displayPart` (OccPart&) operate on OccScene's own member objects. The OccRobotAdapter/OccSceneAdapter constructors borrow the scene |
| Private access via friendship | Compliant | `friend class SceneModel` (sceneobject.h:79) is used only for SceneObject's private methods, not its members |
| Interface stability | **6 confirmed violations, 1 unresolved** | See §5 (QML S1-D1, S1-D2, S1-D3, S2-D1, S3-D1, S3-D2); unresolved: QML S1-I6 |
| Skill mitigations vs AGENTS.md | No rewrite needed | None proposes lambdas, templates, tests, exceptions or output parameters. C++ S3-D5 conflicts with a contract instead; see §7 |

## 3. Own traces and architecture notes (step 6)
- **Include graph.** Matches "Build and modules":
  - geometry includes only geometry, Qt and Eigen;
  - kinematics includes geometry;
  - trajectories includes geometry and kinematics;
  - devices includes geometry only for `rsidevice.h → geometry/mathtypes.h`, with no QQml or QML_ inside src/devices;
  - viewport includes geometry only for `robotgraphics.h → geometry/mathtypes.h`, and has no application or trajectories include;
  - application includes every library.

  One undocumented private include: src/application/CMakeLists.txt adds `src/devices/include/devices/{fts,plc}` for the foreign-type registrar.
- **Trace, Generate → Dry Run:**
  1. `SceneMachining::generate` (scenemachining.cpp:160-181) starts `std::async(ChamferMotion::create, copy)`.
  2. A 30 ms poll in `finishCalculation` (191-235) checks revision, input and availability, then `addMachiningPath`, inserting the replacement before removing the old object.
  3. `dryRun` (314-322) hands an aliasing `shared_ptr<const TimedMotion>` to `ScenePlayback::start`.
  4. `present` (sceneplayback.cpp:60-75) keeps a local copy of the handle and calls `evaluateJoints`, then `RobotController::applyPlayback` (robotcontroller.cpp:191-200): FK, TCP finiteness, `applyPose`, and the pose committed after drawing acceptance.
- **Trace, PLC command:**
  1. QML `plc.invoke(...)` (RobotStartPanel.qml:57, DeltaModuleAP.qml:85, Network.qml:161-163), or C++ scenemachiningpreparation.cpp:486.
  2. `DeviceRunner::invoke` emits `invokeReq` (queued), and `AbstractDevice::invoke` runs on GeneralIO (abstractdevice.cpp:187-211).
  3. PlcDevice: `writeMessage` → queue → `sendNext` (one request in flight, plcdevice.cpp:226-262) → reply → `matchResponse` → `stateReady`.
  4. `DeviceRunner::onStateReady` updates `QQmlPropertyMap::insert` and emits `dataUpdated`.

  The queue is cleared in `onStateChanged(Unconnected)` (plcdevice.cpp:405-413).
- **Threads (ARCHITECTURE §4):**
  - Generate: scenemachining.cpp:178.
  - Calibration: scenecalibration.cpp:419, polled at 424-428.
  - RSI sampling: scenemachiningpreparation.cpp:310 and 326; it emits `correctionsReady`, which reaches `RsiDevice::setCorrections` through the queued link at main.cpp:99-100.
  - FTS save: copied buffer at ftsdevice.cpp:486; `stopDevice` waits at 116-117.
  - CAD: `~OccController` calls `requestInterruption` and `wait` (occcontroller.cpp:37-49); `finished` is queued (257, 298).

  All match §4.
- **Shutdown (ARCHITECTURE §5):** `ScenePlayback` connects `aboutToQuit → end` in its constructor (sceneplayback.cpp:14), before main.cpp:106 (`preparation.shutdown`) and :107 (`hub.stopAll`). Then:
  - `stopAll` sends a blocking stop per device, then quit/wait (devicehub.cpp:60-79);
  - `~DeviceHub` calls `stopAll` again (17-20), which is a no-op;
  - `~SceneMachiningPreparation` calls `shutdown` (no-op);
  - `~RobotController` sets availability false (robotcontroller.cpp:38-41);
  - the engine is declared last (main.cpp:136).

  All match §5. On the return-77 path (no root object), `~SceneMachiningPreparation` still runs `shutdown`.
- **Dead or obsolete code found by own trace:**
  - QmlChartBridge (qmlchartbridge.h 42 lines + .cpp 76 lines) has no instantiation or QML registration. main.cpp:78-80 is commented code naming a nonexistent `SocketFTS`. Qt6::Charts exists only for this class.
  - `IgnorePreviewJointPositionLimits` is constexpr false (kr10kinematicmodel.h:22), so its branches are dead: robotcontroller.cpp:67, 166; kr10kinematics.cpp:227-232; jointsegments.cpp:62, 88.
  - The devicehub.h class comment "shared I/O thread" is stale (ARCHITECTURE §7 already notes it).
- **Duplicated rule:** the Run enable condition `step == 0 && autExt && inHome && !applRun` exists in RobotStartPanel.qml:57 (required by the contract) and again in `plcReady()` (scenemachiningpreparation.cpp:466-471), a third copy after the PLC's own check. Proposal: PlcDevice publishes `robotRunAllowed`.

## 4. C++ D-/I-block verdicts
| ID | Verdict | Evidence / note |
|---|---|---|
| S1-D1 | CONFIRMED | Read scenecalibration.cpp:45-60 (`positiveNumber` <= 0), 95-190 (`calibrationSummary` and the reader returning `{{}, error}` at 183-185) and 505-546 (unchecked writer, Reassign). Low probability, high consequence |
| S1-D2, D5, D6, D7, D8, D9 | Accepted (low/style) | |
| S1-D3 | Accepted | Agent grep: no `Hub.keys` use. Reduction rank 10 |
| S1-D4 | CONFIRMED | Own grep: QmlChartBridge is used only in its own files and the commented main.cpp:78-80 |
| S1-I1 | UNRESOLVED (intent) | Main.qml:86-102 opens the "Probe settings" dialog at start and on every failed write, so the user is warned before an overwrite |
| S1-I2 | RESOLVED, keep | Application already links the Viewport (robotcontroller.cpp includes robotgraphics.h); merging saves one mapping line |
| S1-I3 | RESOLVED, minor | Each failure reaches the UI: `ScenePlayback::fail` → `failed` → `SceneMachining::onPlaybackFailed` → `setError` (scenemachining.cpp:301-304). Only the console line is de-duplicated |
| S2-D1 | CONFIRMED (defect 2) | Read scenemachiningpreparation.cpp:413-427 and 299-349. Consequence: a stale shared_data.txt lets KRL move PTP PsIn and LIN LeadStart of an old path |
| S2-D2 | CONFIRMED, merged into S4-D9 | Same cause: unbounded import on the GUI thread |
| S2-D3, D4, D5, D6, D7, D8, D9 | Accepted | D3 is in an RSI file (excluded from the ranking). D5 and D4 are reduction rank 7. D6 is the fix site for S2-I1 |
| S2-I1 | RESOLVED → CONFIRMED (defect 3) | Read h:59 (`calibrationRunning = m_run != Idle`), h:83 (enum), cpp:250, 260, 335 (only Started blocked) and Main.qml:456-457 (button enable). Consequence: run ended, job deleted, empty table sent, PLC not aborted |
| S2-I2 | UNRESOLVED | PLC firmware behaviour (robotStep only in CHG IO and SNAPSHOT frames) |
| S2-I3 | RESOLVED, latent | The `lastCalibrationChanged` handler (Main.qml:105-109) only opens the window |
| S2-I4 | RESOLVED, confirmed dead branch | `busy` is set and cleared inside one synchronous call (scenesurfaceimporter.cpp:79, 96; ARCHITECTURE §3). Merged with QML S1-D7 |
| S2-I5 | UNRESOLVED (runtime) | Same cause as QML S3-I1 |
| S3-D1, D2, D4, D6, D8, D9, D10 | Accepted | D1 and D2 (FTS/PLC part) are rank 8. D4 and D10 are rank 10. D6 is an invoke API change |
| S3-D3 | REJECTED | ARCHITECTURE §7 "Retained interfaces": the FTS signals and RDTResponse registrations are kept for future force control (user decision) |
| S3-D5 | REJECTED as finding | Contract "PlcMessageManager remains a QObject with dictionary APIs"; moved to §7 |
| S3-D7 | CONFIRMED, consequence corrected | The file name uses the time of each `openLog` (plcdevice.cpp:287-291), and `WriteOnly` truncates. Only a log whose connection was opened in the same second can be lost |
| S3-I1 | RESOLVED, minor | Network.qml:145-154 enables Connect only while `isDisconnected`; plcdevice.cpp:132 is reachable only through a double-click race |
| S3-I2 | RESOLVED | Every device call goes through `invoke` (grep: scenemachiningpreparation.cpp:486 and the QML call sites above), so the extra guards are redundant |
| S3-I3 | RESOLVED | No in-repo `invoke("writeMessage")`. READ_IO, READ_REG, WRITE_REG and WRITE_RAW are unreachable: plcmessagemanager.cpp:159-178, 199-220, 297-340, 364-368; plcdevice.cpp:330-341. Reduction rank 2, needs your decision |
| S4-D1, D3, D4, D5, D7, D8, D10, D11 | Accepted | D1 rank 6; D3 rank 4; D7 rank 9 |
| S4-D2 | CONFIRMED | Read plane.cpp:10-41, spherefit.cpp:8-25, surfaceintersection.cpp:18-30, boundedcylinder.cpp:208-216 |
| S4-D6 | CONFIRMED | Read surfaceintersection.cpp:66-99. A non-finite local centre also makes the translated centre non-finite, which line 96 catches |
| S4-D9 | CONFIRMED | Merged with S2-D2 |
| S4-I1 | RESOLVED, no defect | Every model comes from `RobotDefinition::kr10()`; `RobotController::initialize` runs the HOME check; `validRobot` (jointsegments.cpp:96, called at 163) runs before any motion exists. The other Kr10Kinematics constructions use `motion.robot().model` |
| S4-I2 | RESOLVED, dead code | Constexpr false; removal depends on whether you still want the diagnostic (rank 9) |
| S5-D1 | CONFIRMED | Read chamfermotion.h:10-37, chamfermotion.cpp:136-159; grep for callers of `centralMotion()` (scenegeometry.h:80, chamferrsistream.cpp:52, 144, 165) |
| S5-D2 | CONFIRMED | Read chamfercentralmotion.h:20-30 |
| S5-D3 | REJECTED | ARCHITECTURE §7 "Retained interfaces": "ChamferPathSample/ChamferPathResult serve analytical evaluation" |
| S5-D4, D7 | Accepted | RSI files, excluded from the ranking |
| S5-D5, D6, D8, D9 | Accepted | D5 and D6 are rank 4 |
| S5-I1 | RESOLVED | AGENT_CONTRACTS "Machining and playback" keeps joint-acceleration rejection disabled; motionbuilder.cpp:39-44 is removable commented code |
| S5-I2 | RESOLVED | The only call (chamfercentralmotion.cpp:123) uses the defaults |
| S5-I3 | RESOLVED | Matches RSI_Chamfer_Hole_CONTRACT ("After successful Sampling, rsi_plan_<id>.csv …") |
| S5-I4 | RESOLVED, no defect | The only subclasses are LinearPath (motionbuilder.h:41) and CentralPath (chamfercentralvalidation.h:43), used only through `const TimedPath&` (motionbuilder.h:67, .cpp:108) |
| S5-I5 | UNRESOLVED | API naming decision |
| S6-D1 | CONFIRMED (defect 1) | Read occrobotadapter.cpp:120-162, occviewport.cpp:40-56 and 100-110, occcontroller.cpp:140-165. Merged with S7-D1 |
| S6-D2 | CONFIRMED (defect 5) | Read occsceneadapter.cpp:205-232 and 120-136. Merged with S7-I4 |
| S6-D3 | Accepted (defect 6) | |
| S6-D4, D5, D6, D7 | Accepted (low/style) | |
| S6-I1 | RESOLVED | The contract allows one robot load (main.cpp:134; "no robot replacement"), so occcontroller.cpp:243 is unreachable |
| S6-I2 | UNRESOLVED | Performance; needs measurement |
| S6-I3 | REJECTED | Scene IDs are never reused (SceneModel `m_nextId++`; contract "never reallocates IDs") |
| S6-I4 | UNRESOLVED | OCCT UTF-8 path support and BREP sizes |
| S7-D1 | CONFIRMED, merged into S6-D1 (defect 1) | Adds `setSelection` (occviewport.cpp:78-84 → occscene.cpp:364-373) and the `screenChanged` resize (occviewwindow.cpp:23, 26-29). Slot path: main.cpp:120-121 |
| S7-D2, D3, D7, D8, D9 | Accepted | Reduction rank 3 |
| S7-D4, D6, D10, D11 | Accepted (low/style) | |
| S7-D5 | CONFIRMED, no practical effect | Read occscene.cpp:40-48. The robot links are always visible parts |
| S7-I1 | UNRESOLVED | OCCT constructor internals |
| S7-I2 | UNRESOLVED | QPA behaviour |
| S7-I3 | RESOLVED | Same cause as S7-D1 |
| S7-I4 | RESOLVED | Merged with S6-D2 |
| S7-I5 | REJECTED, no defect | Read cmake/ConvertStep.cmake: it converts to `${OUTPUT}.tmp`, removes it on a nonzero result and renames only on success |

## 5. QML D-/I-block verdicts
| ID | Verdict | Evidence / note |
|---|---|---|
| S1-D1 | CONFIRMED (interface stability) | Read QxProgressBar.qml:10-73 (valueText 64-71, implicitWidth 17) and FtsBars.qml:28-46; Main.qml:247-250 binds min = max = implicit |
| S1-D2 | CONFIRMED (interface stability) | Read QxButton.qml: the background Rectangle has no implicitWidth. Merged with S2-D2 |
| S1-D3 | CONFIRMED (interface stability) | Read QxScrollView.qml:15, 27-41, Network.qml:195-212, Main.qml:260-280. Merged with S2-D3 |
| S1-D4 | REJECTED | docs/code context/APP_DESCRIPTION.md: the Precise submode (Dry Scan, Scan) is a placeholder without implementation |
| S1-D5, D10, D12 | Accepted | Reduction rank 5 |
| S1-D6, D8, D9, D11 | Accepted (low/style) | D8 is the cause of the QL:unqualified rows at QxBusyIndicator.qml:52-65 |
| S1-D7 | CONFIRMED | Merged with C++ S2-I4 |
| S1-I1 | RESOLVED, unreachable | CalibrationResultWindow.qml:20 `modality: Qt.ApplicationModal` |
| S1-I2 | UNRESOLVED | Where MachiningPreparation.error is cleared |
| S1-I3 | RESOLVED | The state-dependent cause is S2-D1 |
| S1-I4 | RESOLVED, no current defect | The largest models have 4 entries (db.json has 4 profiles; RobotStartPanel offers "1"–"4") |
| S1-I5 | UNRESOLVED | ScenePanel.qml:219 not checked |
| S1-I6 | UNRESOLVED | Runtime message lengths; potential interface-stability violation |
| S2-D1 | CONFIRMED (interface stability) | Read DeltaPanel.qml:18-34, FtsPanel.qml:20-30, Main.qml:218-258 |
| S2-D2 | CONFIRMED | Merged into S1-D2 |
| S2-D3 | CONFIRMED | Merged into S1-D3 |
| S2-D4 | CONFIRMED (defect 7) | Read Network.qml:136-165; db.json:35 has driver "vfd"; main.cpp:70-72 registers only plc, fts and rsi |
| S2-D5, D6, D7 | Accepted | D6 is rank 5 |
| S2-I1 | UNRESOLVED | Qt Controls internals (runtime warning check) |
| S2-I2 | RESOLVED | Follows from S1-D1 and S1-D2 |
| S3-D1 | CONFIRMED (interface stability) | Read MachiningPanel.qml:170-197 |
| S3-D2 | CONFIRMED (interface stability) | Read MachiningPanel.qml:95-125 |
| S3-D3, D6 | Accepted | Reduction rank 5 |
| S3-D4, D7, D8, D9, D10 | Accepted (low/style/UX) | D8 is the cause of QL:unqualified in EndEffectorPanel and PropertiesPanel |
| S3-D5 | CONFIRMED, consequence corrected | Read MachiningPanel.qml:14-70 and 140-168; `String(0.07*100)` = "7.000000000000001" (node). `validDraft` uses `Number()`, so Generate still works; the validator (decimals 8) blocks further typing |
| S3-I1 | UNRESOLVED (runtime) | Same cause as C++ S2-I5 |
| S3-I2 | RESOLVED, intended | APP_DESCRIPTION documents Cone from JSON and Scan as placeholders and Start Machining as disabled |
| S3-I3 | RESOLVED, no current defect | `m_measuringTcp` has a nonempty default (sceneendeffectors.h:66); the collet frame comes from the constant `spindleColletTransform()` (sceneendeffectors.cpp:57-60) |

## 6. Lint rejections
**C++: 198 rows reported, 87 rejected, 111 kept.**
| Rule (rows) | Rejected rows | Evidence |
|---|---|---|
| HDR-3 (48) | All: scenemodel, sceneobject, sceneplayback, boundedcylinder, boundedplane, surfaceintersection, chamferarclength, chamfercentralvalidation, chamferpath, chamferrsistream, jointsegments, motionbuilder, probetouchmotion, probetouchplan (44 min/max); scenecalibration.cpp:70, plcmessagemanager.cpp:10, 46, occscene.cpp:266 (4 numeric_limits) | Grep for windows.h/winsock2.h/NOMINMAX: only plcdevice.cpp, which has no HDR-3 row. Object files are newer than their sources, so the MinGW build compiles these units |
| LCY-3 (21) | devicehub.cpp:28, 65, 66; devicerunner.cpp:33; robotcontroller.cpp:26, 109, 193; scenemachiningpreparation.cpp:208, 301, 318, 334, 455, 481; sceneplayback.cpp:123; workspacecontroller.cpp:14; abstractdevice.cpp:28, 39, 48, 102; occcontroller.cpp:241, 327 | Each line was read: `QThread::currentThread() == thread()`, `x->thread() == thread()` and null/state comparisons, with no side effects |
| LCY-4 (3) | devicehub.cpp:24 | main.cpp:64-72 passes `new` results |
| | abstractdevice.cpp:101 | `attachSocket` receives `new QUdpSocket`/`QTcpSocket` (rsidevice.cpp:167, plcdevice.cpp:59, ftsdevice.cpp:87) |
| | scenemachiningpreparation.cpp:534 | `m_run` leaves Idle only in `startCalibration`, guarded by `canStartCalibration`, which requires `m_preparedCalibration`. The job is reset only in `clearPrepared`, after `endRun` |
| LCY-5 (7) | rsidevice.cpp:335 | Capped by `TraceCapacity` |
| | rsidevice.cpp:361 | One comment line after the cap |
| | rsidevice.cpp:364 | Finished saves are erased at 343-350 |
| | scenecalibration.cpp:456 | `std::exchange` at 333 |
| | scenemodel.cpp:153 | `removeOne` at 76 |
| | chamfercentralvalidation.cpp:202 | Bounded by `m_plannedSamples` (199) |
| | deviceprofilemodel.cpp:175 | One load of qrc db.json |
| ERR-6 (3) | sceneobject.cpp:44; abstractdevice.cpp:129; motionbuilder.cpp:23 | Multi-argument `QString::arg(a, b)` |
| PAT-10 (4) | chamferarclength.cpp:136, 140, 145; cadloadworker.cpp:27 | `return std::move(m_result)` moves a member, where NRVO does not apply |
| ERR-1 (1) | rsidevice.cpp:377 | `open()` is checked inside the `||` condition at 377-378 |

**QML: 329 rows reported, 28 rejected, 301 kept.**
| Rule (rows) | Rejected rows | Evidence |
|---|---|---|
| QL:unresolved-type (20) | All | Tool limit: QQmlPropertyMap (DeviceRunner::data) is not exposed declaratively |
| BND-2 (8) | Main.qml:117, 176; Network.qml:30-33; ScenePanel.qml:177 | The targets (importError, intersectionError, laInput, lpInput, paInput, ppInput, nameEditor) have no `text:` binding |
| | RobotViewport.qml:28 | Runs in `Component.onDestruction` |

Kept for information: QL:incompatible-type Main.qml:60. `QList<quint32>` bound to `list<real>` converts at runtime, but qmlsc cannot compile the binding.

## 7. Decision verdicts (step 7)
| Decision | Verdict | Evidence |
|---|---|---|
| main()-owned singletons via `claimQmlSingleton` | Keep | backendqmltypes.h:24-33; main.cpp:83-117; engine declared last (136); Qt 6.8 lacks `setExternalSingletonInstance` |
| ControlIO/GeneralIO grouping, blocking `stopAll` | Keep | devicehub.cpp:22-79; cost: FTS 7 kHz emits on the RSI loop (ftsdevice.cpp:290), latency unmeasured |
| String `invoke` API through `buildApi` | Keep, with one change | Make `writeMessage` private (S3-I3) |
| `std::async` + 30 ms poll | Keep | scenemachining.cpp:178-196 |
| Empty table before every Sampling | Keep, with changes | Fix defects 2 and 3 |
| Immutable shared motions with an aliasing handle | Keep | scenemachining.cpp:319-321; sceneplayback.cpp:64 |
| Single robot load | Keep | occcontroller.cpp:243 unreachable |
| QmlChartBridge stays (contract) | Change | Dead code (§3); needs a contract and dependency change |
| PlcMessageManager remains a QObject (contract) | Insufficient evidence | Stateless; `new`/`delete` at plcdevice.cpp:60, 87-88; the `PlcMessage` QML type is unused in QML; changing it is a QML API change |
| Retained FTS signals and RDTResponse | Keep | User decision; `m_batch` cost unmeasured |
| Joint-acceleration rejection disabled | Keep | Commented block motionbuilder.cpp:39-44 removable |
| PLC one request in flight + heartbeat | Keep | plcdevice.cpp:226-271, 405-413 |

## 8. Reduction measurements (step 9; RSI files excluded)
- **Measured:**
  - Rank 1, QmlChartBridge: 42 + 76 file lines + 2 CMake source entries + 3 commented lines in main.cpp + Charts in find_package and target_link_libraries, about −125.
  - Rank 2, PLC request kinds: buildReqPayload cases 159-178 and 199-220 (42 lines) + response cases 297-340 and 364-368 (49) + matchResponse branches about 6, about −95.
- **Estimated (not counted line by line):**

| Rank | Change | Net lines |
|---|---|---|
| 3 | OCCT dead code | about −60 |
| 4 | Trajectories motion | about −50 |
| 5 | QML dead code | about −56 |
| 6 | Geometry helpers, probe check and intersection pre-check | about −30 |
| 7 | Playback and drawing duplication | about −23 |
| 8 | PLC/FTS endpoint parsing and IO-state helper | about −35 |
| 9 | IgnorePreview switch and Kr10 formulas | about −20 |
| 10 | FTS/PLC/DeviceHub small items | about −15 |

## 9. Tallies (run record)
| Skill | Lint reported / rejected | D reported / confirmed / rejected | I resolved / unresolved |
|---|---|---|---|
| qt-cpp-review | 198 / 87 | 66 / 63 / 3 (S3-D3, S3-D5, S5-D3) | 19 / 8 (S1-I1, S2-I2, S2-I5, S5-I5, S6-I2, S6-I4, S7-I1, S7-I2) |
| qt-qml-review | 329 / 28 | 29 / 28 / 1 (S1-D4) | 6 / 5 (S1-I2, S1-I5, S1-I6, S2-I1, S3-I1) |

The confirmed counts include merged duplicates (one finding per cause):

| Kept as | Merged into it |
|---|---|
| C++ S6-D1 | C++ S7-D1 |
| C++ S4-D9 | C++ S2-D2 |
| QML S1-D2 | QML S2-D2 |
| QML S1-D3 | QML S2-D3 |
| C++ S2-I4 | QML S1-D7 |

The I-block counts treat REJECTED I-blocks (C++ S6-I3, S7-I5) as resolved.
