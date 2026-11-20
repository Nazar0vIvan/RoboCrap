# Qt C++ Code Review Report (qt-cpp-review, consolidated)

- **Commit**: 0cd8996033021e7080898455cd0db10bea4fb078 (master). All line numbers refer to this commit.
- **Scope**: codebase, 120 C++ files (src/** and tools/cadstep2brep/main.cpp), 16,185 lines.
- **Agents**: 7 shards (lines): S1 src/application core 2,359; S2 src/application/scene 2,304; S3 src/devices 2,722; S4 geometry/kinematics/chamfer A 2,317; S5 chamfer B/motion 2,277; S6 probe/viewport adapters 2,294; S7 viewport OCCT core 1,912. Cross-shard checks are best effort.
- **Framework mode**: off (0 framework markers).
- **Lint**: 198 findings in 28 rule/message pairs, unchanged in `cpp_lint_summary.txt`.
- **Counts**: 66 D-blocks, 27 I-blocks. The cap of 10 I-blocks was not applied, per whole-project-review rule (b).

The D- and I-blocks below are the agents' reports, grouped by shard with IDs `S<shard>-D<n>`/`S<shard>-I<n>`.
Each block ends with a **Verdict** line from the reconciliation (step 8 of the whole-project review); `ledger.md` has the working notes.
All file paths are relative to the repository root.

---

## Shard 1: src/application root, devices, robot, scenecalibration, sceneendeffectors, scenegeometry

#### [S1-D1] Calibration result is saved with values the settings reader rejects, and the rejection also drops the valid primary centres
- **File**: `src/application/scene/scenecalibration.cpp:514`
- **Category**: Error Handling & Validation, confidence 80
- **Finding**: `recordCalibrationResult` (514-518) stores the fit values into `m_settings.last` without checking them, and `writeProbeSettings` writes them. On the next start, `readProbeSettings` passes `lastCalibration` through `calibrationSummary` (98-118). That function requires `contactVertical`, `contactHorizontal`, `quantile95`, `repeatability` and `smallestSingularValue` to be strictly positive (`positiveNumber`, line 52). If one is not, lines 183-185 return `{{}, error}` for the whole file, so the `primary` block, already validated at 170-180, is lost as well. The next `onMeasuringTcpChanged` (480) then overwrites the file with only a manual ball centre. `reassignPrimaryCentres` (528) copies the same unchecked contact distances into `primary`, which the reader checks for positivity at 172-177.
- **Trace**: `SceneMachiningPreparation::endRun` (scenemachiningpreparation.cpp:544) calls `recordCalibrationResult` with the result of `fitProbeCalibration` (src/trajectories/probe/probecalibrationfit.cpp). In that file:
  - `repeatability()` = sqrt(sum/(2·pairs)) can be exactly 0;
  - `quantile95()` is a residual magnitude and can be 0;
  - `solve()` rejects only non-finite, failed, non-identifiable or non-converged fits, and no check requires positive contact distances.
- **Mitigation**:
  - Give the writer and the reader one shared check. In `recordCalibrationResult`, build `summaryObject(summary)` and accept it only if `calibrationSummary` returns a value for it. Otherwise keep the previous `m_settings.last` and return an error string.
  - Apply the same check before `reassignPrimaryCentres` assigns `primary`.
  - Optionally, have `readProbeSettings` keep a valid `primary` block when only `lastCalibration` is invalid, and still report the error.
- **Verdict**: CONFIRMED (read 45-60, 95-190, 505-546). Low probability with real touch data; high consequence: the primary centres are lost and the file is overwritten.

#### [S1-D2] roleNames() rebuilds its hash on every call
- **File**: `src/application/devices/deviceprofilemodel.cpp:92`
- **Category**: Model Contracts, confidence 85
- **Finding**: Every call copies `QAbstractTableModel::roleNames()` and inserts `DriverRole` (92-97) (MDL-10). QML views call `roleNames()` while they set up delegates.
- **Trace**: `DriverRole`/"driver" is used by `ConnectionsTable.qml:108` through `required property string driver`.
- **Mitigation**: Return a function-local `static const QHash<int, QByteArray>` that is initialised by a named file-local helper adding `DriverRole` to the base roles (no lambda).
- **Verdict**: Accepted (low).

#### [S1-D3] DeviceHub::keys property and method have no users
- **File**: `src/application/devices/devicehub.h:21`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: Nothing reads `Q_PROPERTY(QStringList keys READ keys CONSTANT)` or `DeviceHub::keys()` (devicehub.cpp:49-52). The CONSTANT flag is also wrong: `add()` changes `m_devs` after construction. The order comes from QHash iteration, which is not deterministic.
- **Trace**: The only `.keys` hits in QML are JS `Object.keys` in Network.qml and MachiningPanel.qml. In C++ the only hit is the definition.
- **Mitigation**: Remove the property, the declaration (devicehub.h:37) and the definition (devicehub.cpp:49-52).
- **Verdict**: CONFIRMED. Reduction rank 10.

#### [S1-D4] QmlChartBridge is compiled but never instantiated or registered
- **File**: `src/application/qmlchartbridge.cpp:4`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: src/application/CMakeLists.txt:35-36 builds the class. It has no QML_ELEMENT and is not in backendqmltypes.h. Its only uses are commented-out lines in main.cpp:78-80, so it is dead code. It also has latent defects:
  - `m_series` is a raw `QXYSeries*` that dangles once the QML series is destroyed, and `flush()` (56-76) would dereference it;
  - the default `m_maxPoints = -1` lets `m_data` grow without limit, and `flush()` copies the whole history every 16 ms.
- **Trace**: The only remaining reference is AGENT_CONTRACTS.md:13, "QmlChartBridge stays in Application with its QtCharts requirement", a module-placement contract.
- **Mitigation**: Delete both files, the CMake entries, main.cpp:78-80 and the contract sentence; that sentence is the owner's decision to change. If the class is kept for planned FTS charting, store the series as `QPointer<QXYSeries>` and use a positive default point cap.
- **Verdict**: CONFIRMED dead code. The removal needs a contract change plus removal of Qt6::Charts from root find_package and from the robocrap link list. Reduction rank 1, about −125 lines measured.

#### [S1-D5] Redundant null check for peerPort
- **File**: `src/application/devices/deviceprofilemodel.cpp:169`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: `obj.value("peerPort").isNull() ? -1 : obj.value("peerPort").toInt(-1)` looks the key up twice, and its null branch gives the same result as `toInt(-1)`: `QJsonValue::toInt(default)` already returns the default for non-Double values. Line 167 handles `localPort` with `toInt(-1)` alone.
- **Mitigation**: Use a single `toInt(-1)`.
- **Verdict**: Accepted (low).

#### [S1-D6] Const local prevents move on return; named and unnamed returns are mixed
- **File**: `src/application/robot/robotcontroller.cpp:139`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: `const auto home = forward(...)` is returned at 140 and 157, so it is copied, not moved (MOV-2). The braced returns at 136 and 152 also prevent NRVO (RET-2). The cost is small: QList and QString are implicitly shared.
- **Mitigation**: Drop `const` on `home`.
- **Verdict**: Accepted (low).

#### [S1-D7] Results of two application font loads are ignored
- **File**: `src/application/main.cpp:51`
- **Category**: Error Handling, confidence 80
- **Finding**: The results of `addApplicationFont` for Regular (51) and Medium (52) are discarded; only Bold (54-57) is checked against -1.
- **Mitigation**: Check all three, for example with a range-for over a constant array of the three resource paths, and log the failing path.
- **Verdict**: Accepted (low).

#### [S1-D8] Equality operators are member functions, not hidden friends
- **File**: `src/application/scene/scenegeometry.h:115` (also :137)
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: `CalibrationTable::operator==` and `SceneSphere::operator==` are member operators (OPR-1). Exact floating-point comparison is correct here: the only caller is scenecalibration.cpp:379, which detects an unchanged value.
- **Mitigation**: Declare both as `friend bool operator==(const T&, const T&)`; the bodies stay the same.
- **Verdict**: Accepted (style).

#### [S1-D9] Q_PROPERTY declarations lack FINAL
- **File**: `src/application/scene/sceneendeffectors.h:15`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: No property in this shard is FINAL (PRP-1): sceneendeffectors.h:15-22, scenecalibration.h:58-66, devicerunner.h:14-17, deviceprofilemodel.h:12, devicehub.h:21, scenegeometry.h:67-72 and qmlchartbridge.h:14. QML tooling must therefore assume subclasses may shadow them. Across src, 0 of 105 Q_PROPERTY declarations are FINAL.
- **Mitigation**: Add FINAL.
- **Verdict**: Accepted (style). The same finding appears as S2-D7 and S6-D6.

#### [S1-I1] A settings file that fails to read is overwritten on the next change
- **File**: `src/application/scene/scenecalibration.cpp:480`, confidence 65
- **Finding**: When `readProbeSettings` fails (version other than 1, malformed `primary`, invalid `lastCalibration`), the constructor (286-288) keeps empty `m_settings`. The next measuring-TCP change (480), `recordCalibrationResult` (518) or `reassignPrimaryCentres` (530) then writes a new version-1 file from those empty settings, discarding the old contents. The successful write also clears `settingsError`.
- **Unverified because**: It depends on intent; overwriting may be the designed recovery.
- **How to verify**: Check the contract and the QML that shows `settingsError`. If the overwrite is not intended, skip writes after a failed read, or back up the file first.
- **Verdict**: UNRESOLVED (intent). Main.qml:86-102 does open the "Probe settings" dialog at start and on every failed write, so the user is warned before any overwrite.

#### [S1-I2] The tool enum is duplicated between scene and viewport and mapped by hand
- **File**: `src/application/robot/robotcontroller.cpp:93`, confidence 65
- **Finding**: `SceneEndEffectors::Tool {Measuring, Spindle}` (sceneendeffectors.h:25) and `RobotTool {Measuring, Spindle}` (robotgraphics.h:13) describe the same tools. Line 93 maps one to the other, and lines 124-126 rely on the `RobotTool` order.
- **How to verify**: Check the layering contracts.
- **Verdict**: RESOLVED, keep. Application already links the Viewport and includes robotgraphics.h. Merging the enums would save one mapping line and needs a Q_ENUM_NS in the viewport module.

#### [S1-I3] RobotController logs a repeated error text only once per session
- **File**: `src/application/robot/robotcontroller.cpp:127`, confidence 60
- **Finding**: `setError` (48-53) emits only when the text differs from `m_error`. `m_error` is reset only in `loadRobot` (127), which runs once, so a repeated playback failure with the same text is never logged again.
- **Verdict**: RESOLVED, minor. Each failure still reaches the UI: ScenePlayback::fail emits failed, which SceneMachining::onPlaybackFailed shows through setError. Only the console line is de-duplicated.

---

## Shard 2: src/application/scene (machining, preparation, model, object, playback, importer, workspacecontroller)

Rejected by the agent after tracing:
- DeviceRunner::invoke returns void.
- OccController::insertObject emits its own error.
- Settings are validated in jointsegments.cpp:119-128.
- setDraftAxis drops repeats.
- `m_plc` stays valid through main.cpp destruction order.
- `fromMotion` indexing is safe.
- `setRobotModel` is called once.

#### [S2-D1] Failure to remove an old KRL job file is only logged
- **File**: `src/application/scene/scenemachiningpreparation.cpp:418`
- **Category**: Error Handling & Validation, confidence 82
- **Finding**: `clearPrepared()` exists so that KRL never finds frames from another or an older path (comment at 417). When `QFile::remove` fails, for example while the controller share still has the file open, the only reaction is a `qInfo` "[ERROR]" line (420). The old shared_data.txt or calibration_data.txt stays in C:/RoboCrapJobs, and the UI shows nothing. `prepare()`, `prepareCalibration()` and `prepareTestLin()` then call `setError({})` (308, 324, 344) and carry on.
- **Trace**: `clearPrepared` has seven callers and none receives a result: the constructor (220), `onAvailabilityChanged` (289), `onCalibrationAvailabilityChanged` (295), `prepare` (307), `prepareCalibration` (323), `prepareTestLin` (341) and `shutdown` (448). `error` (header:31) is the only error the user sees.
- **Mitigation**: `clearPrepared` returns the removal error text (empty on success), and every caller shows it with `setError`. The three prepare entry points must not start while an old job file remains.
- **Verdict**: CONFIRMED, final-report defect 2. Consequence: a later KRL run can read the stale shared_data.txt and move PTP PsIn and LIN LeadStart of an older path. With an empty table, RSI then answers Stop 1.

#### [S2-D2] Imported JSON is read whole on the GUI thread with no size limit
- **File**: `src/application/scene/scenesurfaceimporter.cpp:32`
- **Category**: Error Handling, confidence 80
- **Finding**: `readSamples` calls `file.readAll()` on a user-picked file without checking `file.size()`. Parsing and fitting then run on the GUI thread (header:9).
- **Trace**: Main.qml:127/135 → load/loadCylinder → importSurface (81) → readSamples (30-35).
- **Mitigation**: Before `readAll`, compare `file.size()` with a named maximum and return a dedicated `ReadError` value with its own text.
- **Verdict**: CONFIRMED. Merged with S4-D9, which is the same cause.

#### [S2-D3] The atomic file-write sequence is written three times; one call repeats the Qt default
- **File**: `src/application/scene/scenemachiningpreparation.cpp:177`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: `writeCalibrationResult` (158-159, 177-179), `writeCalibrationTouches` (186-187, 195-197) and `replaceJobFile` (59-69) all repeat mkpath, QSaveFile open, a size-checked write, commit and the error text. `setDirectWriteFallback(false)` (62) is already the default, and `cancelWriting()` (65) is unneeded because the destructor discards uncommitted data.
- **Mitigation**: One helper that takes the folder, the file name and the bytes and returns the error text. Delete lines 62 and 65.
- **Verdict**: Accepted. The file is RSI-side, so it is excluded from the reduction ranking.

#### [S2-D4] Duplicated path-style selection, unnamed phase indices and full point copies in SceneObject::drawing()
- **File**: `src/application/scene/sceneobject.cpp:172`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: The machining-path branch (172-180) and the touch-path branch (181-192) repeat the same polyline loop. Both skip polylines with fewer than 2 positions and choose from the same three RGB triples ({30,130,45}, {90,65,130}, {0,100,210}) with width 3.0 or 2.0. The machining branch picks its style with bare phase indices 3, 0 and 6. writeSharedData depends on the same layout through boundaries 1/2/5/6. Both branches also copy every ChamferJointPoint with `points().mid(...)` only to read positions.
- **Mitigation**:
  - One style helper that takes a small enum.
  - `tcpPolyline(source, first, last)`, so no `mid` copies are needed.
  - Named phase and boundary indices in ChamferMotion (see S5-D8).
- **Verdict**: Accepted. Reduction rank 7.

#### [S2-D5] Play/resume/ownership logic is copied between SceneMachining and SceneCalibration
- **File**: `src/application/scene/scenemachining.cpp:306`
- **Category**: Performance & Code Quality, confidence 80
- **Finding**: Three SceneMachining blocks match SceneCalibration almost line for line:
  - `canPlay` (306-312) matches scenecalibration.cpp:359-364;
  - `dryRun` (314-322) matches 366-374, with the same aliasing shared_ptr handle and resume branch;
  - the first three checks of `unavailableReason` (134-136) match 400-402.

  `onPlaybackFailed` is also the same apart from the owner lookup.
- **Mitigation**: Move the shared decisions into ScenePlayback, which owns this state:
  - "may owner X play now" (owner id in, bool out);
  - "start or resume for owner X" (motion handle and owner id);
  - the common "not ready" reason text.

  Each coordinator keeps only its own path lookup.
- **Verdict**: Accepted. Reduction rank 7.

#### [S2-D6] The same busy-guard sequence is written three times in SceneMachiningPreparation
- **File**: `src/application/scene/scenemachiningpreparation.cpp:249`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: The checks "shutting down", "calibration run Started" and "preparing", with the same texts, appear in `calibrationUnavailableReason` (249-251), `unavailableReason` (259-261) and a nested ternary in `prepareTestLin` (335-337).
- **Mitigation**: One private const method that returns the common busy reason (empty when idle), used in all three places.
- **Verdict**: Accepted. It is the place to fix S2-I1 (final-report defect 3): block whenever `m_run != Idle`.

#### [S2-D7] Q_PROPERTY declarations lack FINAL
- **File**: `src/application/scene/sceneobject.h:20`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: No FINAL in sceneobject.h:20-33, scenemodel.h:14, sceneplayback.h:20-24, scenemachining.h:13-19, scenemachiningpreparation.h:26-37, scenesurfaceimporter.h:13 or workspacecontroller.h:20-26.
- **Mitigation**: Add FINAL.
- **Verdict**: Accepted (style).

#### [S2-D8] Enums without trailing commas; boolean trap in selectObject
- **File**: `src/application/scene/scenemachiningpreparation.h:83`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**:
  - ENM-1: `CalibrationRun` (h:83), `Surface` (scenesurfaceimporter.h:27) and `ReadError` (scenesurfaceimporter.cpp:14) have no trailing comma.
  - ENM-9: `WorkspaceController::selectObject(quint32 id, bool additive)` (workspacecontroller.h:45) is called as `selectObject(id, false)` in Main.qml:82 and :115.
- **Mitigation**: Add trailing commas. Replace `additive` with a scoped Q_ENUM mode (Replace, Toggle), or split it into two invokables.
- **Verdict**: Accepted (style).

#### [S2-D9] pathById is a const method that returns a mutable pointer
- **File**: `src/application/scene/scenemachiningpreparation.h:101`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: `SceneMachiningPath* pathById(quint32) const` (cpp:229-233) returns a mutable pointer, but all three callers only read: 237, 263, 305.
- **Mitigation**: Return `const SceneMachiningPath*` with `qobject_cast<const SceneMachiningPath*>`, as SceneMachining::machiningPath does (scenemachining.cpp:293).
- **Verdict**: Accepted (low).

#### [S2-I1] Sampling can replace the calibration job while the PLC is starting program 1
- **File**: `src/application/scene/scenemachiningpreparation.cpp:250`, confidence 70
- **Finding**: `calibrationRunning()` (h:59) is true for Requested, Starting and Started, and the status bar shows "Calibration run: …" (Main.qml:295). But `unavailableReason` (260), `calibrationUnavailableReason` (250) and `prepareTestLin` (335) block only Started. After Run is sent, the RSI Sampling button (Main.qml:456-457) and the RSI test LIN (Network.qml:197) stay enabled. Pressing either calls `clearPrepared()`, which:
  - calls `endRun("The calibration job was replaced.")`, so later touches are ignored;
  - deletes calibration_data.txt;
  - emits an empty table.

  No abort is sent to the PLC, although `abortRobotStart` exists.
- **How to verify**: Confirm the intended behaviour. If prepare must be blocked from Requested on, use `m_run != Idle` in the shared guard (S2-D6). Otherwise send an abort.
- **Verdict**: RESOLVED as a CONFIRMED defect, final-report defect 3. Verified at h:59, h:83 and cpp:250/260/335. Program 1 then returns after HOME without a job, or every search stops as missed under Stop 1.

#### [S2-I2] The calibration run can stay Requested or Started with no timeout
- **File**: `src/application/scene/scenemachiningpreparation.cpp:512`, confidence 62
- **Finding**: Requested ends only when robotProgramState becomes Running or a frame with robotStep != 0 arrives. Started ends only on ProgramFinished (508) or a disconnect. Interrupted blocks every prepare action with "Calibration run in progress." There is no timeout and no cancel path.
- **Unverified because**: It depends on PLC firmware: whether the step always shows a nonzero value after runRobotProgram. PlcDevice publishes robotStep only in CHG IO and SNAPSHOT frames (plcdevice.cpp:459-497).
- **Verdict**: UNRESOLVED (PLC behaviour).

#### [S2-I3] endRun dereferences the optional after an external call that emits signals
- **File**: `src/application/scene/scenemachiningpreparation.cpp:546`, confidence 60
- **Finding**: `recordCalibrationResult` (544) synchronously emits `lastCalibrationChanged`, and line 546 then reads `m_preparedCalibration->diagnosticId`. A handler that reaches `clearPrepared` would leave an empty optional, which is undefined behaviour.
- **Mitigation**: Copy the id and the motion handle into locals at the start of `endRun`.
- **Verdict**: RESOLVED, latent only. The current handler (Main.qml:105-109) just opens the window.

#### [S2-I4] The busy property of SceneSurfaceImporter is never visible to the user
- **File**: `src/application/scene/scenesurfaceimporter.cpp:79`, confidence 62
- **Finding**: `m_busy` turns true (79-80) and false (96-97) within one synchronous call, so "Importing surface…" (Main.qml:301) and the `geometryImportAvailable` gate (Main.qml:168) never take effect.
- **Verdict**: RESOLVED, confirmed dead UI branch (ARCHITECTURE §3 documents the synchronous import). Merged with QML S1-D7.

#### [S2-I5] One whole-list objectsChanged notification rebuilds every Scene panel row
- **File**: `src/application/scene/scenemodel.h:14`, confidence 60
- **Finding**: Every insert or removal emits `objectsChanged` (scenemodel.cpp:83, 158). ScenePanel.qml:36 rebuilds a JS array that is the model of the Repeater at line 153, so every row is destroyed and recreated, losing rename and focus state.
- **How to verify**: Start an inline rename, then trigger an import or a Generate.
- **Verdict**: UNRESOLVED (runtime). Same cause as QML S3-I1.

---

## Shard 3: src/devices

#### [S3-D1] Copy-pasted response decoding in PlcMessageManager
- **File**: `src/devices/plc/plcmessagemanager.cpp:341`
- **Category**: Performance & Code Quality, confidence 90
- **Finding**: `parseRespOk` decodes `READ_IO` (297-317) and `WRITE_IO` (341-362) with the same 20 lines: payload size 6, dev/module/state, the same checks and the same `out` keys. The 7-byte IO state (x1, y1, x2, y2, step, program, result) is also decoded twice: in the SNAPSHOT reply (369-394) and in the IOs change frame of `parseStateChange` (463-489). The two differ only in prefix length: payload 9 with cmd+status, 8 with chg.
- **Mitigation**: Stack the `READ_IO`/`WRITE_IO` case labels, as `READ_REG`/`WRITE_REG` already are (319-320). Add one free or static helper that takes the seven state bytes (`payload.mid(prefix)`) and returns the QVariantMap; the callers keep their own size checks. Make `byteToBits` static or free.
- **Verdict**: Accepted. Reduction rank 8; it overlaps S3-I3.

#### [S3-D2] Endpoint configuration parsing duplicated in three device classes
- **File**: `src/devices/plc/plcdevice.cpp:108`
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: `FtsDevice::connect` (ftsdevice.cpp:133-163), `PlcDevice::connect` (plcdevice.cpp:108-138) and `RsiDevice::connect` (rsidevice.cpp:197-217) each:
  - read localAddress, peerAddress, localPort and peerPort;
  - run the same null/port checks with the same "Invalid socket configuration" / "Socket configuration is incomplete" texts;
  - store four members (`m_la`/`m_lp`/`m_pa`/`m_pp`, declared three times);
  - emit the same `connectionConfig` map.

  Only `parseSocketPort` (socketconfigutils.h:8) is shared. RSI has no configured peer port. FTS calls `disconnect()` before overwriting (ftsdevice.cpp:146), so that Stop reaches the old peer.
- **Mitigation**: Put one aggregate endpoint value in socketconfigutils.h (local address and port, peer address, optional peer port). Add two free functions: parse the map into an optional value, and turn a value back into the connectionConfig map. Each device holds one member of that type. Keep the FTS ordering.
- **Verdict**: Accepted. Only the FTS and PLC parts are ranked (rank 8), because rsidevice is an RSI file.

#### [S3-D3] FTS stream signals and their types have no receivers
- **File**: `src/devices/include/devices/fts/ftsdevice.h:33`
- **Category**: Performance & Code Quality, confidence 85
- **Finding**: Nothing connects to `dataSampleHFReady`, `dataBatchReady` or `streamReset`. Yet `dataSampleHFReady` is emitted for every sample at 7 kHz (ftsdevice.cpp:290), and `m_batch` collects every sample for `dataBatchReady` (421). The registrations `qRegisterMetaType<RDTResponse>` and `QVector<RDTResponse>` (main.cpp:59-60) and the `rdtResponse` QML type (backendqmltypes.h:179-184) exist only for these signals.
- **Mitigation**: Remove them, or reconnect the planned consumer.
- **Verdict**: REJECTED. ARCHITECTURE §7 "Retained interfaces" keeps these signals and RDTResponse for future force control (user decision). The per-sample `m_batch` cost is unmeasured (estimate).

#### [S3-D4] Unused FTS helper parameter and a single-caller forwarding helper
- **File**: `src/devices/fts/ftsdevice.cpp:391`
- **Category**: Performance & Code Quality, confidence 85
- **Finding**: All three callers of `sendRequest(quint16 cmd, quint32 count = 0)` (220, 236, 254) leave `count` at 0. `saveLogToFileImpl` (484-489, h:59) has a single caller (481) and only forwards to it.
- **Mitigation**: Drop `count` and write the RDT sample-count field from a named constant (0 = continuous). Inline `saveLogToFileImpl` into `saveLogToDefaultFile`.
- **Verdict**: Accepted. Reduction rank 10.

#### [S3-D5] PlcMessageManager is a stateless heap-owned QObject with an unused QML registration
- **File**: `src/devices/include/devices/plc/plcmessagemanager.h:14`
- **Category**: Ownership & Lifecycle, confidence 80
- **Finding**: The class has no data members, signals, slots or properties, and all its methods are const. Yet:
  - it is created with `new` (plcdevice.cpp:60) and deleted and nulled (87-88);
  - `setOutput` guards it with `!m_mgr` (172), while `runRobotProgram` (200) and `sendNext` (232) do not;
  - the QML registration `PlcMessage` (backendqmltypes.h:159-165) is unused in QML.
- **Mitigation**: Make it a non-QObject (Q_GADGET for the enums) held by value, or give it static methods.
- **Verdict**: REJECTED as a finding. The contract says "PlcMessageManager remains a QObject with dictionary APIs". It appears in the final report's decisions table as insufficient evidence; it needs your decision.

#### [S3-D6] Device methods named connect/disconnect hide QObject::connect/disconnect
- **File**: `src/devices/include/devices/fts/ftsdevice.h:26` (also plcdevice.h:24-25, rsidevice.h:48-49)
- **Category**: API & C++ Correctness, confidence 80
- **Finding**: These methods hide the inherited QObject overloads, so every connection must be written `QObject::connect(...)`. `device->disconnect()` closes the socket instead of disconnecting signals.
- **Mitigation**: Rename them (for example connectToHost/disconnectFromHost) and update the invoke strings in Network.qml:161 and :163. This changes the invoke API, so it needs confirmation.
- **Verdict**: Accepted (low; API change).

#### [S3-D7] PLC I/O log of a previous connection is truncated on a reconnect in the same second
- **File**: `src/devices/plc/plcdevice.cpp:287`
- **Category**: Error Handling, confidence 80
- **Finding**: `openLog` names the file `plc_io_yyyyMMdd_HHmmss.csv` and opens it with `WriteOnly`, which truncates.
- **Mitigation**: Add milliseconds (FTS also adds a UUID), or open with `QIODevice::NewOnly`.
- **Verdict**: CONFIRMED, consequence corrected. The name uses the time of each `openLog`, so a file is truncated only when the previous connection was opened in the same second. Only a sub-second log can be lost; negligible.

#### [S3-D8] Multi-line enums without a trailing comma
- **File**: `src/devices/include/devices/plc/plcmessagemanager.h:43`
- **Finding**: `MessageError` (43), `Type` (51), `CMD` (63), `VAR_TYPE` (70) and `CHG_TYPE` (75) have no trailing comma.
- **Verdict**: Accepted (style).

#### [S3-D9] Meaningful magic numbers in RSI and PLC protocol code
- **File**: `src/devices/rsi/rsidevice.cpp:165`
- **Finding**: These values carry meaning but have no name:
  - `setInterval(250)`, the capture idle delay;
  - the PLC payload lengths 6, 8, 9, 4, 4, 8 (plcmessagemanager.cpp:298, 321, 342, 371, 397, 424, 466);
  - `*index >= 8`, the bits per module (82).
- **Mitigation**: Named constexpr values.
- **Verdict**: Accepted (style).

#### [S3-D10] PlcDevice::stopDevice repeats clears that disconnect() just did
- **File**: `src/devices/plc/plcdevice.cpp:97`
- **Finding**: `disconnect()` (152-153) clears `m_rx` and `m_pend`, lines 97-98 clear them again, and `onStateChanged` (409-411) clears them a third time.
- **Mitigation**: Delete lines 97-98.
- **Verdict**: Accepted. Reduction rank 10.

#### [S3-I1] PLC reconnect and partial-write paths close without the RST linger used by disconnect()
- **File**: `src/devices/plc/plcdevice.cpp:132`, confidence 65
- **Finding**: `connect()` on a socket that is not Unconnected (131-133), and `sendNext` after a partial write (246), call `m_sock->abort()` without `SO_LINGER {1,0}`. On an established connection that leaves TIME_WAIT on the fixed source port.
- **Mitigation**: Call `disconnect()` in both places.
- **Verdict**: RESOLVED, minor. Network.qml:144-153 enables Connect only while `isDisconnected`, so line 132 is reachable only through a double-click race. Line 246 is practically unreachable.

#### [S3-I2] Inconsistent "not started" guards in device methods
- **File**: `src/devices/plc/plcdevice.cpp:200`, confidence 65
- **Finding**: `AbstractDevice::invoke` already rejects calls while the device is not started (abstractdevice.cpp:50-53). Some methods repeat the check: `connect` checks `!m_sock` (ftsdevice.cpp:128, plcdevice.cpp:103, rsidevice.cpp:193) and `setOutput` checks `!m_mgr`. Others skip it: `runRobotProgram` dereferences `m_mgr`, and `resumeRobot`/`abortRobotStart` reach `m_log`.
- **Verdict**: RESOLVED. Every caller goes through `invoke` (scenemachiningpreparation.cpp:486, RobotStartPanel.qml:57-71, DeltaModuleAP.qml:85, Network.qml:161-163), so the extra checks are redundant (reduction).

#### [S3-I3] Unreachable PLC request kinds and an unbounded WRITE_RAW length
- **File**: `src/devices/plc/plcmessagemanager.cpp:213`, confidence 70
- **Finding**: `writeMessage` is a public Q_INVOKABLE, but nothing calls `invoke("writeMessage")`; internal code builds only WRITE_IO, SET_VAR and SNAPSHOT. That leaves unreachable:
  - the READ_IO, READ_REG, WRITE_REG and WRITE_RAW request cases (159-178, 199-220);
  - their response cases (297-339, 364-367);
  - the matching branches in matchResponse (plcdevice.cpp:330-342).

  WRITE_RAW also truncates the length field with `static_cast<quint8>(payload.size())` (71).
- **Mitigation**: Make `writeMessage` private and drop the unused cases, or bound WRITE_RAW.
- **Verdict**: RESOLVED. There is no in-repo caller; out-of-tree tooling is unknown. Reduction rank 2, about −95 lines measured. Needs your decision, because the invoke API changes.

---

## Shard 4: src/geometry, src/kinematics, chamferarclength/chamfercentralmotion/chamfercentralvalidation

#### [S4-D1] Probe compensation validated three times, and the two fromSamples factories are copies of each other
- **File**: `src/geometry/boundedcylinder.cpp:237`
- **Category**: API & C++ Correctness, confidence 85
- **Finding**: The same radius/dir check (radius finite and >= 0, dir ±1) runs at utils.cpp:59-60 (`decodeProbeSamples`), boundedplane.cpp:68-69 and boundedcylinder.cpp:237. `BoundedPlane::fromSamples` (66-74) and `BoundedCylinder::fromSamples` (235-242) are the same function: validate, then `fromPoints`, then `applyProbe`. The finite-point loop at utils.cpp:69-71 is repeated by plane.cpp:17 and boundedcylinder.cpp:183.
- **Trace**: The only caller of `fromSamples` is scenesurfaceimporter.cpp:86/90. `input.samples` comes only from `decodeProbeSamples` (36); the error text at 48-49 depends on the decode-time check.
- **Mitigation**: One const `ProbeSamples` validity member returning bool, called from `decodeProbeSamples` (which sets the error category) and from the factories (which are public). Delete the inline copies and the loop at utils.cpp:69-71.
- **Verdict**: Accepted. Reduction rank 6.

#### [S4-D2] Same geometric helpers copied across geometry translation units
- **File**: `src/geometry/plane.cpp:37`
- **Category**: Performance & Code Quality, confidence 85
- **Finding**:
  - Sign rule ("largest-magnitude component positive, ties prefer the lower index"), written three times: plane.cpp:37-41, surfaceintersection.cpp:21-28 (`canonical`), boundedcylinder.cpp:212-215.
  - Reference vector `abs(x) < 0.9 ? UnitX : UnitY`, twice: boundedcylinder.cpp:34, boundedplane.cpp:27.
  - Anchor-centering block, identical twice: plane.cpp:12-26, spherefit.cpp:11-23.
  - V3d/std::array<double,3> conversions, three times: boundedcylinder.cpp:27-30, boundedplane.cpp:13-16, surfaceintersection.cpp:10-13.
- **Trace**: The tie-breaks match: a strict `>` in the loops, and the first maximum from `maxCoeff(&index)`.
- **Mitigation**: One non-template free function per operation in geometry/utils, each returning by value. Centering returns an optional struct of centered matrix and centroid.
- **Verdict**: CONFIRMED (read plane.cpp:10-41, spherefit.cpp:8-25, surfaceintersection.cpp:18-30, boundedcylinder.cpp:208-216). Reduction rank 6.

#### [S4-D3] positiveFinite helper copied into three chamfer translation units
- **File**: `src/trajectories/chamfer/chamfercentralmotion.cpp:21`
- **Finding**: `bool positiveFinite(double)` is defined in chamferarclength.cpp:309, chamfercentralmotion.cpp:21 and chamfercentralvalidation.cpp:13.
- **Mitigation**: Declare it once in a shared header.
- **Verdict**: Accepted. Reduction rank 4.

#### [S4-D4] Lead ramp/cruise boundary computed independently in two classes
- **File**: `src/trajectories/chamfer/chamfercentralvalidation.cpp:145`
- **Finding**: `start + (index == 0 ? rampSeconds : cruiseSeconds)` with the guard `index != 1 && cruiseSeconds > 0` appears in `ChamferCentralMotion::initializeTiming` (chamfercentralmotion.cpp:103-104) and `makePreviewSchedule` (chamfercentralvalidation.cpp:145-146). It is implicit again in `leadInState`/`leadOutState` (43, 55).
- **Mitigation**: One ChamferCentralMotion member returning the ordered stop times of a phase, consumed by both.
- **Verdict**: Accepted.

#### [S4-D5] Chamfer phase vocabulary handled as strings and as ad-hoc integer casts
- **File**: `src/trajectories/chamfer/chamfercentralvalidation.h:11`
- **Finding**:
  - `ChamferCentralPreviewSpan::phase` is a QString: labels are built at chamfercentralvalidation.cpp:163-164 and compared as strings at 216.
  - The index↔phase mapping is done twice: a `phaseIndex` switch (chamfercentralmotion.cpp:11-19) and `static_cast<ChamferPhase>(index)` (chamfercentralmotion.cpp:123, 152; chamfercentralvalidation.cpp:139).
  - chamferarclength.cpp:332-333 shows "phase 1" instead of the label.
- **Mitigation**: Store `ChamferPhase` in the span and produce the label once, through one function used when appending to `PathSchedule::phases` and in the arc-length error. Use one inverse for every index↔phase conversion.
- **Verdict**: Accepted.

#### [S4-D6] intersectSurfaces runs a validation pass that its later checks repeat
- **File**: `src/geometry/surfaceintersection.cpp:73`
- **Finding**: `tolerance` (73-74) is computed only to be tested for finiteness at line 90. `residualTolerance` (96) uses the same `size` and a superset of the same magnitudes. The first `fromEllipse` (90) validates axes and radii that do not change before the second call (98). Every failure caught at 90 ends in the same `{}` result at 97 or 98.
- **Trace**: The factories guarantee finite plane and cylinder origins (boundedplane.cpp:59, 86-89; boundedcylinder.cpp:231).
- **Mitigation**: Delete `tolerance` and the line-90 pre-check.
- **Verdict**: CONFIRMED (read 66-99). Reduction rank 6.

#### [S4-D7] Kr10Kinematics repeats its joint-chain step and wrist-centre formula
- **File**: `src/kinematics/kr10kinematics.cpp:117`
- **Finding**:
  - The `armTransform` loop (117-127) repeats the `solveFK` loop body (31-37).
  - The wrist centre `flangeOrigin - flangeZ * dF` appears at 55-58 and 252-255.
  - `alpha = atan2(bx, by)` appears at 100 and 173.
  - `hypot(bx, by)` appears at 19 and 159.
- **Mitigation**: Private const members for "joint i transform at q" and "wrist centre of a flange transform". Compute alpha and the forearm length once.
- **Verdict**: Accepted. Reduction rank 9.

#### [S4-D8] Meaningful limits written as bare literals, and error text that can drift from its constant
- **File**: `src/trajectories/chamfer/chamfercentralvalidation.cpp:158`
- **Finding**:
  - The message hard-codes "200000 points" while the limit is `MaximumSamples` (10). The same 200000 and the 1e-9 s resolution are repeated in jointsegments.cpp:255; the resolution also appears at chamfercentralvalidation.cpp:160 and motionbuilder.cpp:134.
  - Cylinder-fit values have no names: rms tolerance 0.1 mm / 2 % of radius (boundedcylinder.cpp:225), ratio 0.01 (172), 200 iterations (116), 12 damping trials (127).
  - The subdivision depth 12 at chamferarclength.cpp:48 has no name.
  - spherefit.cpp:36 hard-codes 1e-9, while Plane uses `GeomConst::Eps`.
- **Mitigation**: Named constants, shared where they cross files; build the message with arg().
- **Verdict**: Accepted (style).

#### [S4-D9] Imported probe JSON has no upper bound on point count before synchronous fitting
- **File**: `src/geometry/utils.cpp:29`
- **Finding**: `jsonArrayToPoints` accepts any array length. `BoundedCylinder::fromPoints` then makes many passes over N points: up to 6 starting axes, up to 200 LM iterations, 5 Jacobian columns plus up to 12 trials per iteration, and an N×5 JacobiSVD. All of it runs synchronously on the GUI thread (scenesurfaceimporter.cpp:77, 81).
- **Mitigation**: A named maximum sample count in `decodeProbeSamples`, rejected through `ReadError::Samples`.
- **Verdict**: CONFIRMED. Merged with S2-D2 (one cause: unbounded import on the GUI thread).

#### [S4-D10] makePreviewSchedule mixes named and braced returns
- **File**: `src/trajectories/chamfer/chamfercentralvalidation.cpp:141`
- **Finding**: Lines 141, 158 and 161 return braced prvalues while 168 returns `schedule` (RET-2).
- **Mitigation**: Set `schedule.error` and return `schedule`.
- **Verdict**: Accepted (low).

#### [S4-D11] Multi-line enum without trailing comma
- **File**: `src/geometry/include/geometry/surfaceintersection.h:31`
- **Finding**: `IntersectionStatus` has no trailing comma.
- **Verdict**: Accepted (style).

#### [S4-I1] Kr10Kinematics::isValid accepts models that make the turn conversions undefined
- **File**: `src/kinematics/kr10kinematics.cpp:234`, confidence 62
- **Finding**: `JointModel` defaults qMin/qMax to ±infinity, and `resolveJointAngle` converts ceil/floor/round of /360 to int (234, 236, 240), which is undefined for infinite values. `branchFrom` divides by `axis.z()`. `isValid()` checks none of this; the checks exist only in jointsegments.cpp:105-112.
- **Verdict**: RESOLVED, no defect. Every model comes from `RobotDefinition::kr10()` (robotdefinition.cpp), checked by RobotController::initialize (HOME check), and passes `validRobot` (jointsegments.cpp:96, :163) before any ChamferMotion or ProbeTouchMotion exists. The other Kr10Kinematics constructions (scenemachiningpreparation.cpp:101, probecalibrationfit.cpp:87, probersistream.cpp:70, tcpsolver.h:51) use `motion.robot().model` of such a motion.

#### [S4-I2] Unreachable diagnostic branch behind a constexpr false switch
- **File**: `src/kinematics/kr10kinematics.cpp:227`, confidence 60
- **Finding**: `IgnorePreviewJointPositionLimits` is `constexpr false` (kr10kinematicmodel.h:22), so lines 227-232 never run. The switch also gates robotcontroller.cpp:67 and :166, and jointsegments.cpp:62 and :88.
- **Verdict**: RESOLVED, dead code by construction. Removal depends on whether you still want the diagnostic. Reduction rank 9.

---

## Shard 5: chamfermotion, chamferpath, chamferrsistream, include/trajectories headers, motion/*

#### [S5-D1] ChamferMotion::m_centralMotion is always set: the optional, the setter, the duplicate parameters and the "timing is missing" branch never matter
- **File**: `src/trajectories/include/trajectories/chamfermotion.h:20`
- **Category**: Performance & Code Quality, confidence 85
- **Finding**: The private constructor (h:25) is called only by `ChamferMotion::create` (cpp:154), which always calls `setCentralMotion` (155) before returning. Therefore:
  1. the optional and the setter (h:26, 30; cpp:139-142) only allow two-step initialisation;
  2. the check at chamferrsistream.cpp:53 ("analytical timing is missing") and the ternary at scenegeometry.h:80 can never take their failure branch;
  3. `m_parameters` (h:27) duplicates `m_centralMotion->path().parameters()`.
- **Mitigation**: The constructor takes (TimedMotion, boundaries, ChamferCentralMotion) and stores the central motion by value. `parameters()` returns the central path's parameters. Remove `m_parameters`, the setter and the optional. `centralMotion()` returns a const reference. Update the two callers and delete the unreachable branches.
- **Verdict**: CONFIRMED (read h:10-37, cpp:136-159). Reduction rank 4; the chamferrsistream.cpp:53 part touches an RSI file.

#### [S5-D2] Four ChamferCentralPose fields are computed and never read
- **File**: `src/trajectories/include/trajectories/chamfercentralmotion.h:24`
- **Finding**: `phase`, `distance`, `speed` and `tangentialAcceleration` (24, 26-28) are written at chamfercentralmotion.cpp:181, 183-185 and never read. Callers use only `tcp` (chamferrsistream.cpp:146, 167) and `progress` (chamfercentralvalidation.cpp:229).
- **Mitigation**: Remove the fields. Decide explicitly whether to keep the finite check on state.speed/state.acceleration (172-173), because removing it changes which times return nullopt.
- **Verdict**: CONFIRMED (header read). Reduction rank 4.

#### [S5-D3] ChamferPathSample::edgePoint is only a temporary inside machiningSample
- **File**: `src/trajectories/include/trajectories/chamferpath.h:32`
- **Finding**: `edgePoint` is written at chamferpath.cpp:221 and read only at 227 and 229.
- **Mitigation**: Make it a local.
- **Verdict**: REJECTED. ARCHITECTURE §7 lists "ChamferPathSample/ChamferPathResult serve analytical evaluation" under Retained interfaces.

#### [S5-D4] Wire-pose rounding and checking is duplicated; the exported tool frame is checked before rounding
- **File**: `src/trajectories/chamfer/chamferrsistream.cpp:108`
- **Finding**: probersistream.cpp:26-36 has `wirePose`: XYZ plus principal ABC, then `wireValues`, then the Kr10PosEps/Kr10RotEps check. chamferrsistream.cpp re-implements it inline three times, each differently:
  - initial pose (146-154): the full check;
  - final pose (118-124): rotation check only;
  - tool frame (108-114): checks unrounded angles and never rounds `toolCoordinates`.

  `writeSharedData` then writes `toolCoordinates` with six decimals (scenemachiningpreparation.cpp:125), so KRL receives a value that was not the checked one. The error (about 5e-7) is inside the tolerances, so current output is still accepted. scenemachiningpreparation.cpp:102 also rounds `stagingPoint` by hand.
- **Mitigation**: Move `wirePose` into rsicoordinates.h as an inline function returning an optional, and use it for all three poses.
- **Verdict**: Accepted. RSI files, so excluded from the reduction ranking.

#### [S5-D5] The joint-limit check exists twice, and the preview bypass flag affects only one copy
- **File**: `src/trajectories/motion/tcpsolver.h:13`
- **Finding**: `chamferJointPositionError` (tcpsolver.h:13-23) and `withinLimits` (jointsegments.cpp:85-92) run the same comparison. Only `withinLimits` and the spline check (jointsegments.cpp:62) honour `IgnorePreviewJointPositionLimits`:
  - `inverse` (tcpsolver.h:89) and playback (timedmotion.cpp:47) follow the flag;
  - `solveTcp` (63) and the seed check (chamfermotion.cpp:119) do not.

  The flag is false today, so the two agree, and chamfermotion.cpp:119-120 can never fail.
- **Mitigation**: Keep one check with the flag handled once, and remove the redundant seed check.
- **Verdict**: Accepted. Reduction rank 4.

#### [S5-D6] ChamferTcpSolver::inverse copies solveTcp, and its TCP result is never used
- **File**: `src/trajectories/motion/tcpsolver.h:84`
- **Finding**: `inverse` (84-95) repeats the first half of `solveTcp` (58-67): flange, IK, limits, FK and matchesFlange. Its only caller (chamfermotion.cpp:103-107) uses only `solution->first`. The extra residual check in `solveTcp` uses the same tolerances as `samePose` (motionbuilder.cpp:119), which every candidate meets at the first LIN after the PTP. So `solveTcp` accepts exactly the starts that can compile, and rejects the others before the PTP is built.
- **Mitigation**: Call `m_solver.solveTcp(target, previous)` in `appendStart` and delete `inverse`.
- **Verdict**: Accepted. Reduction rank 4.

#### [S5-D7] RSI controller constants and the rate tolerance are defined in several files
- **File**: `src/trajectories/chamfer/chamferrsistream.cpp:21`
- **Finding**:
  - The 4 ms cycle is defined three times: chamferrsistream.cpp:21, probersistream.cpp:20, scenemachiningpreparation.cpp:27.
  - The 50 settle cycles three times: chamferrsistream.cpp:22, probersistream.cpp:22, scenemachiningpreparation.cpp:30.
  - The rate tolerance 1e-6 twice (chamferrsistream.cpp:24, motionbuilder.cpp:12), plus two bare `1.0 + 1e-6` literals (jointsegments.cpp:65, tcpsolver.h:30).
- **Mitigation**: Define the cycle and settle count once in rsicoordinates.h and the rate tolerance once in jointsegments.h.
- **Verdict**: Accepted. Mostly RSI files, so excluded from the ranking.

#### [S5-D8] Chamfer phase-boundary indices are unnamed magic numbers spread across modules
- **File**: `src/trajectories/chamfer/chamfermotion.cpp:82`
- **Finding**: Phase count 7 and boundary count 8 (chamfermotion.cpp:25, 82-85; h:16, 25, 29). Individual entries: boundaries[1], [2] and [5] in chamferrsistream.cpp:59, 72, 88, 101; {1,2,5,6} and `boundary <= 2` in scenemachiningpreparation.cpp:103-109, 121; sceneobject.cpp:173-175. All depend on the append order in `MotionCompiler::compile` (72-76).
- **Mitigation**: An enum naming the eight boundaries (HOME start, P_s in, lead start, machining start, machining end, lead end, P_s out, HOME end) plus a count constant.
- **Verdict**: Accepted.

#### [S5-D9] ChamferPath::create recomputes the staging point and plane normal
- **File**: `src/trajectories/chamfer/chamferpath.cpp:143`
- **Finding**: Lines 143-146 recompute `stagingPoint()` (167-171), including the constructor's flip (101-102), only for a finiteness check. `majorAxis.cross(minorAxis)` is computed at 104, 132 and 135, and in every `machiningSample` (206) and `machiningDerivatives` (291).
- **Mitigation**: Construct the path first and check `path.stagingPoint().allFinite()`. Optionally store the normalized plane normal.
- **Verdict**: Accepted.

#### [S5-I1] Commented-out per-joint acceleration rejection contradicts the documented contract
- **File**: `src/trajectories/motion/motionbuilder.cpp:39`, confidence 72
- **Finding**: Lines 39-44 are disabled code ("Temporary Dry Run diagnostic"), with a matching note at jointsegments.cpp:68. timedmotion.h:23-24 says no axis acceleration is enforced.
- **Verdict**: RESOLVED. AGENT_CONTRACTS "Machining and playback" keeps joint-acceleration rejection disabled until you ask otherwise, so the commented block is removable text (reduction rank 4).

#### [S5-I2] ChamferArcLengthParameters is never set by any caller
- **File**: `src/trajectories/include/trajectories/chamferarclength.h:31`, confidence 70
- **Finding**: The only call (chamfercentralmotion.cpp:123) relies on the default values.
- **Verdict**: RESOLVED, unused configuration. Replace it with named constants (reduction rank 4).

#### [S5-I3] The plan CSV is discarded on failure, and a failed commit is only logged
- **File**: `src/trajectories/chamfer/chamferrsistream.cpp:131`, confidence 60
- **Finding**: Early error returns (96, 100, 105, 113, 124) discard the QSaveFile and leave an empty records/<id> folder. On success, a failed `commit()` (132-136) is only logged, yet the stream is returned with its diagnosticId.
- **Verdict**: RESOLVED. It matches the RSI_Chamfer_Hole contract ("After successful Sampling, rsi_plan_<id>.csv contains…"). The commit-failure logging is minor, and the file is RSI.

#### [S5-I4] TimedPath is a polymorphic base that can still be copied
- **File**: `src/trajectories/motion/motionbuilder.h:31`, confidence 65
- **Finding**: The class has a virtual destructor but implicit copy operations (PLY-3). CentralPath holds a reference member.
- **Verdict**: RESOLVED, no defect. The only subclasses are LinearPath (motionbuilder.h:41) and CentralPath (chamfercentralvalidation.h:43), and they are used only through `const TimedPath&` (motionbuilder.h:67, .cpp:108).

#### [S5-I5] Generic motion types carry a "Chamfer" prefix although the probe motions use them
- **File**: `src/trajectories/include/trajectories/timedmotion.h:11`, confidence 62
- **Finding**: ChamferTimingParameters, ChamferRobotSetup, ChamferJointPoint, ChamferTcpSolver, chamferJointSpeedError and chamferJointPositionError are shared by ProbeTouchMotion and MotionBuilder.
- **Verdict**: UNRESOLVED (API naming decision).

---

## Shard 6: src/trajectories/probe, viewport adapters, include/viewport, occcontroller, cadloadworker, occinputcontroller

Rejected by the agent:
- No dangling robot reference (`TimedMotion` copies the setup, timedmotion.h:90).
- `searchFeed` is never changed from 1.0.
- `quantile95` needs at least 3 touches first.
- `OccControllerQml` cannot use `claimQmlSingleton` (different module).
- The `ProbeSegment` names match the enum.

Threading checked clean: CadLoadWorker results are read after `wait()`, and the destructor waits for both workers.

#### [S6-D1] OCCT exceptions can escape slot-driven tool and spindle-frame updates
- **File**: `src/viewport/adapters/occrobotadapter.cpp:123`
- **Category**: Error Handling & Validation, confidence 80
- **Finding**: `OccRobotAdapter::setSpindleFrames` (123-128) calls `synchronizeSpindleFrames()`, and `setActiveTool` (138-143) calls `showActiveTool()` → `m_scene.setPartVisible`; neither has a try/catch. The same class treats these calls as throwing elsewhere: `clear()` (77-81), `applyTransforms()` (101-119) and `load()` (34-66) all wrap them. On the unguarded paths, the exception passes through OccViewport, OccViewWindow and OccController and out of a Qt slot.
- **Trace**: `SceneEndEffectors::activeToolChanged`/`spindleTcpChanged`/`spindleColletChanged` and `OccController::showSpindle*Changed` → `RobotController::synchronizeTools` (robotcontroller.cpp:28-32, 90-97), then:
  - tool path: `OccController::setActiveTool` (occcontroller.cpp:147-152) → OccViewWindow → `OccViewport::setActiveTool` (occviewport.cpp:43-49) → adapter → `showActiveTool` → `setPartVisible` → `displayPart`;
  - frame path: `OccController::setSpindleFrames` (154-159) → `OccViewport::setSpindleFrames` (103-107) → `OccScene::setFrameAxes` (Erase/Display, occscene.cpp:74-83).
- **Mitigation**: Catch `Standard_Failure` in these two adapter methods, return a `ViewportOperationResult`, and let OccController report it through `message`.
- **Verdict**: CONFIRMED (read occrobotadapter.cpp:120-162, occviewport.cpp:40-56/100-110, occcontroller.cpp:140-165). Merged with S7-D1 into final-report defect 1.

#### [S6-D2] Object removal errors are only logged, and tracking is dropped even when OCCT removal threw
- **File**: `src/viewport/adapters/occsceneadapter.cpp:220`
- **Category**: Error Handling & Validation, confidence 80
- **Finding**: `OccSceneAdapter::removeObject` sends the `removeParts` error only to `qWarning` (225) and returns true. Line 226 then erases the Presentation, even when `AIS_InteractiveContext::Remove` threw inside `OccScene::removePart` (occscene.cpp:315, before `m_parts.erase` at 316). The part stays in OccScene, possibly still displayed, and no later call can remove it. `insertObject` (212-216) does the same on replacement. By contrast, `OccRobotAdapter::clear` (occrobotadapter.cpp:82-90) keeps the IDs whose removal failed.
- **Trace**: `OccController::removeObject` (occcontroller.cpp:92-99, void) → `OccViewport::removeObject` (51-54, void) → adapter → `removeParts` (124-135).
- **Mitigation**: Return a `ViewportOperationResult` through the adapter, OccViewport and OccController, and keep the failed IDs.
- **Verdict**: CONFIRMED, final-report defect 5. Merged with S7-I4.

#### [S6-D3] Wheel zoom drops fine-resolution deltas but still reports a camera change
- **File**: `src/viewport/occt/occinputcontroller.cpp:82`
- **Finding**: `delta = round(angleDeltaY / 120 * 10)` turns every |angleDeltaY| < 6 into 0 and truncates other values (15 units give 1 px instead of 1.25). Each such event still calls `V3d_View::Zoom` with identical points and returns needsRedraw and cameraScaleChanged.
- **Trace**: `OccViewWindow::wheelEvent` (occviewwindow.cpp:173) → `OccViewport::wheel` (161-167) → `OccInputController::wheel` (74-90) → `applyInput` (174-179).
- **Mitigation**: Keep the sub-pixel remainder in an OccInputController member, or use a continuous zoom factor. Return no-redraw when the applied zoom is 0.
- **Verdict**: Accepted, final-report defect 6.

#### [S6-D4] Strut-clearance threshold recomputed and reported inconsistently
- **File**: `src/trajectories/probe/probetouchmotion.cpp:92`
- **Finding**: Line 87 stores `required = ProbeApproachGap - ClearanceTolerance`, and line 92 recomputes it. The strut message (93-94) prints `ProbeApproachGap` with default formatting; the ball message (99-100) prints `required` with 'f', 4.
- **Mitigation**: Compare against `required` and print it in both messages.
- **Verdict**: Accepted (low).

#### [S6-D5] QThread subclass without Q_OBJECT
- **File**: `src/viewport/occt/cadloadworker.h:11`
- **Finding**: `metaObject()` reports QThread, and `qobject_cast` to CadLoadWorker fails. Neither is used today.
- **Mitigation**: Add Q_OBJECT.
- **Verdict**: Accepted (low).

#### [S6-D6] OccController Q_PROPERTYs lack FINAL
- **File**: `src/viewport/include/viewport/occcontroller.h:109`
- **Finding**: None of the 7 properties (109-115) is FINAL.
- **Verdict**: Accepted (style).

#### [S6-D7] RobotTool enum lacks a trailing comma, and ToolCount duplicates its size
- **File**: `src/viewport/include/viewport/robotgraphics.h:13`
- **Finding**: `enum class RobotTool { Measuring, Spindle };` has no trailing comma. occrobotadapter.cpp:10 hard-codes `ToolCount = 2` and indexes by the enum at 108, 148 and 158.
- **Mitigation**: Derive the count next to the enum.
- **Verdict**: Accepted (low).

#### [S6-I1] loadRobot re-entry guard is unreachable, and a failed load can never be retried
- **File**: `src/viewport/occcontroller.cpp:243`, confidence 60
- **Finding**: `if (m_visuals) return {m_error.isEmpty(), m_error};` runs only on a second call, and it reports the shared `m_error`. After a failed load, `finishRobotLoad` (281-285) leaves `m_visuals` set, so a retry can never start.
- **Verdict**: RESOLVED. The contract allows one robot load only ("no robot replacement"; main.cpp:134), so the branch is unreachable and can be removed.

#### [S6-I2] Every hover move schedules a full viewer redraw
- **File**: `src/viewport/occt/occinputcontroller.cpp:157`, confidence 65
- **Finding**: `handleHoverMove` returns needsRedraw on every move, and the render timer then runs `updateCurrentViewer()`. An immediate-layer redraw would be enough.
- **Verdict**: UNRESOLVED (performance, needs measurement).

#### [S6-I3] Removed object id stays in the selected list
- **File**: `src/viewport/occcontroller.cpp:94`, confidence 60
- **Verdict**: REJECTED. Scene IDs are never reused (SceneModel `m_nextId++`; contract "never reallocates IDs"), so a stale ID can never select a new object.

#### [S6-I4] CAD bytes are held twice while OCCT parses
- **File**: `src/viewport/occt/cadloadworker.cpp:46`, confidence 60
- **Finding**: The file is held as a QByteArray and again as a std::string during `BRepTools::Read`, so peak memory is about twice the file size.
- **Verdict**: UNRESOLVED (OCCT UTF-8 path support; BREP sizes).

---

## Shard 7: src/viewport/occt core, tools/cadstep2brep

Rejected by the agent:
- The borrowed `m_document`/`m_pose` references are safe (the window is deleted in occcontroller.cpp:46-49).
- `setActiveTool` before load is harmless.
- `ClearSelected` is repaired by the selectObject(0) chain.

#### [S7-D1] OCCT Standard_Failure can escape OccViewport operations that Qt slots call
- **File**: `src/viewport/occt/occviewport.cpp:78`
- **Category**: Error Handling & Validation, confidence 82
- **Finding**: These entry points reach OCCT with no catch on their call path:
  - `setSelection` (78-84) → `OccScene::selectParts`: new AIS_Shape, Display, SetZLayer (occscene.cpp:364-373);
  - `setSpindleFrames` (103-107);
  - `setActiveTool` (43-49);
  - `OccViewport::resize` from the `QWindow::screenChanged` connection (occviewwindow.cpp:23, 26-29).

  They run outside the only window-level catch, `OccViewWindow::event`.
- **Trace**: main.cpp:120-121 connects `selectionChanged` → `OccController::setSelectedObjects` → `setSelection` (occcontroller.cpp:164). `insertObject`, `removeObject` and `setObjectVisible` also call `setSelection` outside the adapter try blocks (78, 97, 107). The same resize is guarded in `event()` (104-105, 114).
- **Mitigation**: Catch `Standard_Failure` at these OccViewport entry points and return `ViewportOperationResult`. Guard `resizeViewport` too, or remove the connection (see S7-I2).
- **Verdict**: CONFIRMED. Merged with S6-D1 into final-report defect 1.

#### [S7-D2] Validity and null checks that can never fail, plus a method nobody calls
- **File**: `src/viewport/occt/occviewcube.cpp:49`
- **Finding**:
  - OccViewCube: `m_cube` is set in the initializer (26), so the seven `IsNull` guards (49, 63, 109, 118, 135, 166, 181), `isValid()` (37-40) and occscene.cpp:151-154 can never fire.
  - OccWorldAxes: all handles are created in `createAxes` (70-78), so `isValid()` (20-29), the `setLength` guard (42-44), `configureAxes` (83) and occscene.cpp:76, 97, 114-117 and 138 are dead.
  - OccPart: null shapes are rejected at occscene.cpp:247, so 259-262 is unreachable.
  - OccScene: the `displayPart` (164, 166) and `activateAllSelectionModes` (199, 201) checks are redundant, as are the `isValid()` terms at 289, 308 and 43.
  - `OccWorldAxes::length()` (cpp:31-34, h:15) has no caller.
- **Mitigation**: Remove them. Keep `shape.IsNull()`, `m_view.IsNull()` in `currentWorldAxisLength`, and `OccScene::isValid()` in `setFrameAxes`, `updateCameraDependentObjects`, `createShapePart` and `selectParts`.
- **Verdict**: Accepted. Reduction rank 3.

#### [S7-D3] World-axes and view-cube display duplicate the frame-axes logic and keep redundant flags
- **File**: `src/viewport/occt/occscene.cpp:108`
- **Finding**:
  - `displayWorldAxes` (108-130) duplicates `setFrameAxes(m_worldAxes, identity)` (74-93).
  - The else branch at 57-58 together with `redisplayWorldAxes` (132-143) equals `resizeFrameAxes(m_worldAxes)` (95-101).
  - `m_worldAxesDisplayed` (h:78) duplicates `IsDisplayed`.
  - `m_viewCubeDisplayed` (h:79) guards a second call that never happens: `displayInfrastructure` is called once, from occviewport.cpp:22.
  - `displayInfrastructure` (103-106) only forwards.
  - `activateAllSelectionModes` (203-222) repeats one call three times.
- **Mitigation**: Use `setFrameAxes`/`resizeFrameAxes` for the world axes and delete both functions and both flags. Keep one view-cube display method. Iterate the three selection types over a brace list.
- **Verdict**: Accepted. Reduction rank 3.

#### [S7-D4] Colour, width and length constants are copied between classes that must stay in sync
- **File**: `src/viewport/occt/occscene.cpp:367`
- **Finding**:
  - The outline grey 48/255 with width 2.0 appears at occscene.cpp:367-368 and occviewer.cpp:223/231.
  - Hover `rgb(102,179,204)` appears at occviewer.cpp:216 and occviewcube.cpp:90.
  - Axis length 10.0 appears at occscene.cpp:228/234, occworldaxes.h:11/27 and .cpp:68.
  - The 0.05 ratio (occscene.cpp:237) has no name.
- **Mitigation**: Named constants in occpartprops.h.
- **Verdict**: Accepted (style).

#### [S7-D5] The IsInfinite() filter in hasVisibleParts never excludes a part
- **File**: `src/viewport/occt/occscene.cpp:45`
- **Finding**: No AIS object is ever marked infinite, because only presentations are (186). Fit-excluded decorations therefore count as visible parts for world-axes display (55) and fitAll (occviewport.cpp:183).
- **Mitigation**: Test `part.fitExcluded()` instead.
- **Verdict**: CONFIRMED (read 40-48), but the practical effect is nil: the robot links are always visible parts.

#### [S7-D6] Const accessor hands out a mutable OccViewport pointer
- **File**: `src/viewport/occt/occviewwindow.h:24`
- **Finding**: `OccViewport* viewport() const`, while callers mutate through it (occcontroller.cpp:77-78, 96-97, 106-107, 158, 164, 170).
- **Mitigation**: Make it non-const.
- **Verdict**: Accepted (low).

#### [S7-D7] Destructor bodies repeat what member destruction already does; one include is unused
- **File**: `src/viewport/occt/occviewport.cpp:93`
- **Finding**:
  - `~OccViewport` only calls `m_timer.stop()`.
  - `~OccViewWindow` (31-34) resets `m_viewport`, which is destroyed first anyway.
  - `<QPointer>` (occviewwindow.h:6) is unused.
- **Mitigation**: Drop the OccViewport destructor. Make `~OccViewWindow = default` in the .cpp (needed because OccViewport is incomplete in the header). Remove the include.
- **Verdict**: Accepted. Reduction rank 3. Destruction order is unchanged (thread-affine destruction is preserved).

#### [S7-D8] The first expose runs the viewport resize twice, and the resize call is copied three times
- **File**: `src/viewport/occt/occviewwindow.cpp:126`
- **Finding**: `initializeViewport` already calls `setExposed(true)` (192), and line 126 calls it again. `if (m_viewport) m_viewport->resize();` is written at 28, 105 and 131, although `resizeViewport()` exists.
- **Verdict**: Accepted. Reduction rank 3.

#### [S7-D9] OccPart constructor fetches or creates the drawer twice
- **File**: `src/viewport/occt/occpart.cpp:35`
- **Finding**: Lines 23-27 and 35-39 are the same get-or-create of `Attributes()`.
- **Mitigation**: Fetch the drawer once.
- **Verdict**: Accepted. Reduction rank 3.

#### [S7-D10] Const named results mixed with unnamed returns lose NRVO and the move
- **File**: `src/viewport/occt/occviewport.cpp:35`
- **Finding**: Affects `setRobot` (34/35/40), `applyTransforms` (111/112/114) and `OccViewWindow::applyTransforms` (occviewwindow.cpp:50-52/61).
- **Mitigation**: Drop const.
- **Verdict**: Accepted (low).

#### [S7-D11] Enum without trailing comma
- **File**: `src/viewport/occt/occpartprops.h:21`
- **Finding**: `OccSelectionMode` has no trailing comma.
- **Verdict**: Accepted (style).

#### [S7-I1] Null checks on OCCT drawer and camera handles are probably dead
- **File**: `src/viewport/occt/occpart.cpp:24`, confidence 70
- **Finding**: `Attributes().IsNull()` (occpart.cpp:24, 36; occviewcube.cpp:71, 147) and `Camera().IsNull()` (occscene.cpp:227; occviewer.cpp:191).
- **Verdict**: UNRESOLVED (OCCT constructor internals).

#### [S7-I2] The screenChanged connection may be redundant with DevicePixelRatioChange
- **File**: `src/viewport/occt/occviewwindow.cpp:23`, confidence 60
- **Finding**: Since Qt 6.6, `event()` handles DevicePixelRatioChange (104-105); removing the connection would also remove the unguarded path in S7-D1.
- **Verdict**: UNRESOLVED (QPA behaviour; test on monitors with equal and with different scaling).

#### [S7-I3] The OCCT catch in event() sits above Qt frames
- **File**: `src/viewport/occt/occviewwindow.cpp:98`, confidence 62
- **Finding**: A pick passes applicationSelectionRequested → selectObject → selectionChanged → setSelectedObjects → selectParts, so an exception unwinds through `QMetaObject::activate` frames.
- **Verdict**: RESOLVED, same cause as S7-D1. The fix at the OccViewport entry points removes this path.

#### [S7-I4] Object-removal failures never reach the UI message channel
- **File**: `src/viewport/occt/occviewport.cpp:51`, confidence 65
- **Verdict**: RESOLVED, merged with S6-D2.

#### [S7-I5] cadstep2brep leaves a truncated output file when the write fails
- **File**: `tools/cadstep2brep/main.cpp:45`, confidence 62
- **Verdict**: REJECTED, no defect. cmake/ConvertStep.cmake passes `${OUTPUT}.tmp`, removes it on a nonzero result and renames it only on success, matching the contract "a failed conversion leaves no BREP".

---

## Lint findings (mitigation per rule; rejected rows marked)
The full row listing is in `cpp_lint_summary.txt`.

| Rule | Count | Mitigation / verdict |
|---|---|---|
| DEP-10 count()/length() | 2 | Use size() |
| DEP-11 currentDateTime() | 3 | Use currentDateTimeUtc() where a UTC stamp is wanted (PLC and FTS log names use local time) |
| DEP-13 QChar object | 1 | Kept (style) |
| ENM-2 unscoped enum without type | 7 | Add an underlying type or make it scoped |
| ENM-9 two or more bool parameters | 8 | Named enums |
| ERR-1 QFile::open unchecked | 1 | **Rejected**: checked inside the condition at rsidevice.cpp:377-378 |
| ERR-6 arg() count | 3 | **Rejected**: multi-argument arg() (sceneobject.cpp:44, abstractdevice.cpp:129, motionbuilder.cpp:23) |
| HDR-3 unprotected min/max | 48 | **Rejected**: no listed TU includes windows.h, and all compile in the MinGW build |
| LCY-3 Q_ASSERT side effect | 21 | **Rejected**: pure thread queries |
| LCY-4 Q_ASSERT sole null guard | 3 | **Rejected**: callers pass `new` results; a non-Idle run implies a prepared job |
| LCY-5 unbounded growth | 7 | **Rejected**: all bounded (see final report) |
| PAT-10 return std::move | 4 | **Rejected**: these move a member, where NRVO does not apply |
| PAT-12 non-const range-for | 1 | Use `const auto&` (rsidevice.cpp:185) |
| PAT-2 default-constructed optional | 45 | Kept (style) |
| PAT-3 holds_alternative | 3 | Kept (style) |
| PAT-7 QMap | 1 | Kept (style) |
| TMO-1 integer timeouts | 6 | Kept (style) |
| VAL-5 qSwap | 1 | Use std::swap |
| VAR-3 direct brace init | 33 | Kept (style) |

## Summary
| Category | Lint | Deep (D) | Investigate (I) |
|---|---|---|---|
| Reported | 198 | 66 | 27 |
| Rejected | 87 | 3 | resolved 19 / unresolved 8 |
