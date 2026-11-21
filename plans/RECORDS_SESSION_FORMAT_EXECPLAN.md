# One record folder per robot run: fixed file names, one clock, Excel-ready files written after the run

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan. First, the implementing agent never runs a build, CMake configure, QML lint, the application or a test unless the user asks for that run (`AGENTS.md`, "Validation and handoff"); where a milestone names a build or a run, the agent records it as "not run (user's)" in `Progress` and continues. Python scripts on recorded files may be run by the agent with `python -I` (isolated mode, so no module from a data folder is imported). Second, the agent never moves the robot, never connects to the PLC, RSI or force sensor hardware and never starts a KRL program; runs that need the robot are done by the user and need the user's explicit authorization for that run. Third, no git staging, commits, `git rm` or `git mv` unless the user asks; files are created, moved and deleted in the working tree only. Fourth, no project tests or test harnesses. Fifth, every C++ line obeys `AGENTS.md`, "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, no output parameters, at most four parameters) and "Mandatory code reduction"; before each logical change re-read both sections, and after each milestone perform the compliance pass of "Mandatory change procedure" and record its findings and line counts in `Progress`.

Deleting existing records (Milestone 5) is a permanent deletion of measurements that exist nowhere else. The agent lists what will be deleted and deletes only after the user confirms that list in chat.


## Purpose / Big Picture

Before migration the folder `records/` (repository root, git-ignored) contained 61 entries and 873 MB after one week. Every RSI Sampling creates a folder even when the robot never runs (8 chamfer folders and 10 calibration folders hold only a plan), folder identifiers are UTC while PLC log names are local time (a calibration started at 16:58 local is in `calibration_20261120_135817_781`, its PLC log in `plc_io_20261120_165735.csv`), the PLC log of a run lies outside the run's folder, 16 of the 30 PLC logs contain only a header and one row, file names repeat the identifier (`calibration_touches_calibration_20261120_…`), three generations of formats coexist, a rerun of one Sampling appends to the same capture and overwrites the touch and result files, and the run inputs (tool, sphere, chamfer settings) are not recorded at all.

A native calibration session records one accepted start request. A chamfer or test LIN session records one matching RSI capture. A local start time, program kind and optional `_failed` suffix identify the folder. The worker commits the session files after close, with `session.json` last. Sampling alone creates no record. The [record format](../docs/code%20context/RECORDS_FORMAT.md) defines which files are present, the CSV columns and the clocks used by native and converted records.

To see it working: after a user-authorized calibration run, open `records/` and find one new folder named by the local start time; double-click `rsi.csv` and see one row per RSI cycle in Excel columns; `python -I python/calibration/calibration_fit.py records/<folder>` prints the values of `session.json` "result". A Sampling without a run leaves `records/` unchanged.


## Progress

- [x] (2026-11-21) Plan written after reading `docs/code context/AGENT_CONTRACTS.md`, `APP_DESCRIPTION.md`, `ARCHITECTURE.md`, `docs/hardware context/HARDWARE_SETUP.md`, the previous plan `plans/RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md`, `src/devices/rsi/rsidevice.cpp` and header, `src/devices/plc/plcdevice.cpp` and header, `src/devices/fts/ftsdevice.cpp` (record part), `src/trajectories/chamfer/chamferrsistream.cpp`, `src/trajectories/probe/probersistream.cpp` and both headers, `src/application/scene/scenemachiningpreparation.cpp` and header, `src/application/scene/scenecalibration.cpp` (settings `id`), `src/application/main.cpp`, the record sections of the three RSI contracts in `controller/krl/rsi/`, `python/calibration/calibration_fit.py`, `python/rsi/rsi_packet_timing.py`, and every folder and file of `records/`.
- [x] (2026-11-21) Start counts (physical lines): `rsidevice.h` 105, `rsidevice.cpp` 439, `plcdevice.h` 77, `plcdevice.cpp` 493, `chamferrsistream.h` 33, `chamferrsistream.cpp` 231, `probersistream.h` 41, `probersistream.cpp` 181, `scenemachiningpreparation.h` 136, `scenemachiningpreparation.cpp` 542, `main.cpp` 166, `src/application/CMakeLists.txt` 129; Python `rsi_packet_timing.py` 237, `calibration_fit.py` 204. Records: 61 top-level entries, 873 MB.
- [x] (2026-11-21) Milestone 1: format document `docs/code context/RECORDS_FORMAT.md` (new, 116 lines: terms, session start/end and writing, folder name, file list, CSV conventions, every file's keys and columns); link added to the records bullet of `docs/code context/AGENT_CONTRACTS.md` "Build and modules" (+1 line). The chamfer plan's time column is `time_s` (Decision Log). Documentation only: no C++ changed, no compliance pass needed; `git diff --check` clean.
- [x] (2026-11-21) Milestone 2: converter `python/records/convert_records.py` (new, 370 lines; dry run 1 min 51 s); `--write` created 13 session folders beside the old entries (names in Artifacts and Notes; 9 complete sessions of about 37 MB each against about 45 MB before, 361 MB in total; old entries untouched); dry-run deletion list: 61 old entries (31 folders, 30 `plc_io_*.csv`). Per session: no orphan reply (every reply found its request by IPOC), no capture time running backwards, requests equal the old rx counts (`2026-11-18_19-00-12_calibration`: 134,090). `python/calibration/calibration_fit.py` (204 → 275 lines: `read_session`, `plan_touches`, `fit` and `differences` replace `read_touches` and the printing in `main`; the fit itself unchanged): on all 9 complete sessions "differences to the stored result: none", ball centre and χ95 of the six reference sessions equal Artifacts and Notes to all six decimals; the 4 failed sessions print "3 of 158 touches detected.", "84 of 158 …", "23 of 158 …", "No touch detected.". `python/rsi/rsi_packet_timing.py` (237 → 215 lines: reads a session folder; the Base64 and decoded readers and their regular expressions removed): on `2026-11-18_19-00-12_calibration` its 148 output lines equal the old script's output on the old capture line by line in wording and classification (requests 134,090, replies 133,983, 33 rejected replies), numbers differ by at most 0.001 ms (the 1 µs resolution of `t_s`); on `2026-11-18_14-31-39_calibration` 133,853 requests, 133,852 replies, 1 rejected. Format document: converted `inputs` hold only `touches`. `.gitignore`: `records/` became `/records/`, because the unanchored pattern also ignored `python/records/` (the converter); `git check-ignore` confirms the records root is still ignored and the converter is not. Python only, no C++ compliance pass; `git diff --check` clean. Not checked by the agent: opening a converted `rsi.csv` in Excel (user's).
- [x] (2026-11-21) Milestone 2 check by the user: a converted `rsi.csv` opens in Excel in columns.
- [x] (2026-11-21) Milestone 3 code: `devices/recordclock.h` (new, 13 lines; listed in `src/devices/CMakeLists.txt`); `RsiDevice` stores accepted requests in reserved memory (`storeRequest`, `emitCapture`, `flushCapture`, `onCaptureTimer`, signal `captureReady`, value types `RsiCaptureRecord`/`RsiCapture`; the text trace, `captureRow`, the save workers, `m_diagnosticId` and `m_traceClock` removed; `m_traceTimer` became `m_captureTimer`; `advance` sets `completed` at Finished → Stopping); `PlcDevice` emits `ioRecorded(PlcIoRecord)` from `recordIo` (renamed `writeLog`; `openLog`, `m_log`, the header and name tables removed; `y2` added to the change keys); `sampleProbeCalibrationRsi` returns `plan` (separator `;`, new header) and writes no file (`writePlan` removed); new `src/application/records/sessionfiles.{h,cpp}` and `sessionrecorder.{h,cpp}` (listed in `src/application/CMakeLists.txt`); `SceneMachiningPreparation`: `attachRecorder`, `calibrationData` returns the job file text (saved by the caller), `calibrationInputs`, `resultObject`, `residualSetsCsv`, `touchesCsv` replace `writeCalibrationResult`, `writeCalibrationTouches`, `resultLine`, `vectorLine`; `PreparedCalibration::diagnosticId` removed (unused); `endRun` hands touches, sets and result to the recorder and closes the session, the settings id is the session name; `shutdown` ends an active run first; `main.cpp` registers the two types, connects `captureReady`, `ioRecorded`, `captureRequested`, attaches the recorder and runs `recorder.shutdown` after `stopAll`. The chamfer Sampling still writes its old `rsi_plan_<id>.csv`, and chamfer captures are not recorded until Milestone 4. Counts (physical lines) `rsidevice.h` 105 → 134, `rsidevice.cpp` 439 → 383, `plcdevice.h` 77 → 91, `plcdevice.cpp` 493 → 450, `probersistream.h` 41 → 43, `probersistream.cpp` 181 → 165, `scenemachiningpreparation.h` 136 → 140, `scenemachiningpreparation.cpp` 542 → 563, `main.cpp` 166 → 180 (existing C++ −31); new C++ 544 (`recordclock.h` 13, `sessionfiles.h` 52, `sessionfiles.cpp` 208, `sessionrecorder.h` 74, `sessionrecorder.cpp` 197); build inputs +5. Growth: session ownership across Sampling, run and shutdown, the writer worker, the XML decoding moved from the device into the writer, PLC rows inside sessions, ordered `session.json` (Decision Log); the smaller alternative, the recorder inside `SceneMachiningPreparation`, was rejected (Decision Log). Compliance pass over declarations, bodies and callers: no lambdas, project templates, thrown exceptions or output parameters; largest signature 3 parameters (`timingRow`); `storeRequest` returns its index and changes only its own object; the recorder and preparation touch each other only through public methods; no allocation, formatting, file work or console output added to `onReadyRead`; `git diff --check` clean. Converter aligned (`session.json` indent 4, nested keys sorted) and its 13 folders regenerated; `calibration_fit.py` again "none" on all 9 complete sessions.
- [x] (2026-11-21) Milestone 3 checks without the robot (user): `robocrap.exe` rebuilt 16:03:37; after calibration RSI Sampling and a PLC connect/disconnect, `records/` holds no new entry (only the 13 converted folders) and no `plc_io_*.csv` newer than 2026-11-20 20:36. `C:/RoboCrapJobs/` was empty when the agent looked (application closed; shutdown deletes the job file; folder changed 16:06:41), so the presence of `calibration_data.txt` during the session was not confirmed from disk.
- [x] (2026-11-21) B1 verified the short failed calibration record. The user also confirmed the updated calibration UI. In `records/2026-11-21_19-19-37_calibration_failed`, `plc.csv` contains `run 1`, `resume`, a RESUMED result and the later loss of AUT EXT. The same folder's `session.json` reports “AUT EXT was left during the calibration run.”
- [x] (2026-11-21) B2 full calibration record verified in `records/2026-11-21_19-32-00_calibration`. Start 19:32:00.401 local, duration 1002.162 s, outcome `ok`. All eight files are present. All 158 planned touches match the first detected joint angles in the RSI capture. Both RSI files have the same 132,872 rows by IPOC and receive time. The offline fit reproduced the result and all 36 residual sets. The settings name this session.
- [ ] B2 timing criterion unmet. The November 20 16:58 reference had 2 delayed requests, 2 unanswered requests and 0 rejected replies. This run had 34, 31 and 3 respectively, all during searches 1, 3 and 4. No requests were dropped from the recording. The cause of the timing difference remains open.
- [x] (2026-11-21) Milestone 4, completed test LIN. `records/2026-11-21_20-06-10_test-lin` contains all four expected files. The exchange lasted 100.321 s. All 25,081 requests have a reply and a matching timing row. The PC sent 25,000 increments of +0.004 mm along WORLD X and received the controller's completion acknowledgement. No delayed or dropped requests.
- [x] (2026-11-21) Milestone 4, interrupted test LIN record (C0). `records/2026-11-21_20-23-06_test-lin_failed` contains all four expected files. The 6.048 s capture contains 1,513 requests with matching timing rows and 1,509 replies. The PC never signalled stream completion and the session reports the expected incomplete-stream error. Two earlier attempts had no folder. Preparation now retains the job during normal shutdown, but this record does not identify which event flushed the capture.
- [x] (2026-11-21) Milestone 4, chamfer record (C). `records/2026-11-21_20-37-59_chamfer` contains all six expected files for Machining Path 2. All 19,570 motion corrections match the plan in order on all six axes, followed by 50 zero corrections. The controller echoed Result 1 and the PC sent Stop. Both RSI files contain 19,652 matching rows, every request received a reply, and the timing analysis found zero rejected replies. The operator confirmed the approach matched the preview, the retreat passed through PsOut and the robot returned to HOME.
- [x] (2026-11-21) Milestone 3 check by the user: `calibration_data.txt` is written by the calibration RSI Sampling.
- [x] (2026-11-21) Milestone 4 code: `ChamferRsiStream` gains `plan`; the sampler appends each `recordSample` row to it (separator `;`, new header, `time_s`) instead of the `QSaveFile m_trace`; `writeTrace`, the `#` metadata lines and the three console lines are removed (their values are the chamfer `inputs`). `SceneMachiningPreparation`: `writeSharedData` became `sharedData` returning the file-local `JobFile{contents, error}`, saved by the caller; new `chamferInputs(stream, motion)` (the caller adds `path_name`); a successful chamfer Sampling arms the recorder with `plan.csv` (moved out of the held stream) and `shared_data.txt`; `prepareTestLin` arms a test LIN job (no files, the four test LIN inputs). After this milestone only `SessionRecorder` (and the manual FTS log) writes to `records/` (`git grep ROBOCRAP_RECORDS_DIR src`). Counts `chamferrsistream.cpp` 231 → 192, `chamferrsistream.h` 33 → 36, `scenemachiningpreparation.cpp` 563 → 612 (+49: `chamferInputs` 27 lines, `JobFile` 6, the two `arm` calls). Compliance pass: no lambdas, templates, thrown exceptions or output parameters; new signatures have 2 parameters; `JobFile` is the combined result required by `AGENTS.md` (Decision Log); `git diff --check` clean.
- [x] (2026-11-21) Milestone 4 checks without the robot (user, verified by the agent on disk): `robocrap.exe` rebuilt 16:20:37; after chamfer RSI Sampling and the RSI test LIN button no entry of `records/` changed after 16:10; the user's copy of `shared_data.txt` is version 2 with CRLF line ends: PsIn, LeadStart and LeadEnd XYZABC with S/T, PsOut S/T, the tool frame, `$VEL.CP` 0.010000 m/s and `$ACC.CP` 0.020000 m/s², as `sharedData` writes it; PsIn and LeadStart share A, B, C as the code composes them.
- [x] (2026-11-21) Milestone 5: the converter's dry run listed the same 61 old entries (873 MB: 13 converted calibration folders, 18 plan-only folders, each checked to hold one `rsi_plan_*.csv`, 30 `plc_io_*.csv` with 3,032 rows); the user was told what exists nowhere else afterwards (`MACur` and the exact request XML of the 13 old captures, PLC rows outside the 13 runs, the 18 plans) and confirmed in chat; the application was not running; exactly those 61 entries were deleted. `records/` now holds only the 13 session folders (361 MB). `settings/probe_calibration.json` `lastCalibration.id` `calibration_20261120_135817_781` → `2026-11-20_16-58-26_calibration` (no `primary.source` in the file); the file parses as JSON. The converter stays in `python/records/` until the second PC (`D:` root) is checked (Decision Log).
- [x] (2026-11-21) Milestone 6. Updated the record format and ten owning documents. Compared both samplers, the device capture and PLC producers, GUI run and shutdown calls, writer and converter with the documentation. The final counts and offline checks are below.


## Surprises & Discoveries

- Observation: on this PC Windows uses decimal point `.` and list separator `;` (locale ru-RU), and Excel has no own separator override, so Excel splits a double-clicked CSV at `;` and reads `17.476175` as a number. A comma-separated file opens as one column.
  Evidence: `HKCU:\Control Panel\International` sDecimal ".", sList ";"; no `UseSystemSeparators` value under `HKCU:\Software\Microsoft\Office\16.0\Excel\Options` (read 2026-11-21). The second PC (`D:` root) was not checked.
- Observation: the PC time zone is Russian Standard Time, UTC+3 without daylight saving. A Sampling identifier `calibration_20261120_135817_781` (UTC) is 16:58:17 local; the matching PLC log shows `run 1` at 16:58:26.391 local.
  Evidence: `Get-TimeZone`; `records/plc_io_20261120_165735.csv`.
- Observation (corrected in Milestone 2): the oldest identifier is UTC as well. `calibration_20261114_170013_165` (20:00:13 local) holds `plc_io_20261114_195852.csv` with two `run 1` rows, 19:59:10 (the run of an earlier Sampling, the one stopped by `RSITimeout` in `docs/hardware context/HARDWARE_SETUP.md`) and 20:00:40 (the run of this Sampling). The time rule therefore applies to PLC logs inside folders too; taking the first `run` row of such a log gave a session of 10 PLC rows, the rule gives 324.
  Evidence: converter dry runs of 2026-11-21.
- Observation: one RSI request is 426–430 bytes of XML; a full calibration run has 132,812 requests (about 531 s of RSI inside a 16–17 min run, because RSI is on only during the 158 searches).
  Evidence: `records/calibration_20261118_113045_223/rsi_packets_…_part10.csv` (Base64 XML decoded), `records/calibration_20261120_135817_781/rsi_packets_…csv` (265,623 rows, 132,812 rx).
- Observation: the PLC log records neither X2.0 (probe) nor module 2 outputs (Y2.0 spindle, door lamps). The probe signal reaches the controller as RSI `Touch` every 4 ms with IPOC, so X2.0 adds nothing; Y2.0 is needed for machining runs.
  Evidence: `LogHeader` and `writeLog` in `src/devices/plc/plcdevice.cpp`.
- Observation: the settings file `settings/probe_calibration.json` (tracked by git) stores the identifier of the last calibration as `lastCalibration.id` (today `calibration_20261120_135817_781`), and a reassigned primary keeps it as `primary.source`. These identifiers name record folders and must follow the conversion.
  Evidence: `calibrationSummary`, `summaryObject`, `reassignPrimaryCentres` in `src/application/scene/scenecalibration.cpp`.


## Decision Log

- Decision: the unit of recording is the session, one execution of a KRL program on the robot: a calibration run (`RSI_Probe_Calibration`, from Start Calibration until the run ends), a chamfer run (`RSI_Chamfer_Hole`) or a test LIN run (`RSI_Probe_Check`). One session has one folder. A Sampling is not a session and writes nothing to `records/`; its plan and job file are kept in memory and written into every session that runs them. A rerun of the same Sampling is a new session.
  Rationale: user decision 2026-11-21 ("a plan without actual records is meaningless"); folder per run removes the overwrite of touch and result files by reruns.
  Date/Author: 2026-11-21, user and agent.
- Decision: folder name `yyyy-MM-dd_HH-mm-ss_<kind>` in local time of the session start, kind `calibration`, `chamfer` or `test-lin`, followed by `_failed` when the session failed; on a name collision `_2`, `_3`, … is appended. A successful session has no suffix. Because files are written after the session, the folder is created once with its final name and never renamed.
  Rationale: user decisions 2026-11-21 (name says what and when; failure in the name). Local time because the user reads the names; the UTC offset is in `session.json`.
  Date/Author: 2026-11-21, user and agent.
- Decision: CSV format: UTF-8 without BOM, separator `;`, decimal point `.`, exactly one header line, snake_case column names with units (`_s`, `_mm`, `_deg`, `_us`), booleans `0`/`1`, no comment lines, one table per file; all metadata in `session.json`. All text written is ASCII.
  Rationale: the user reads records in Excel and prefers `.` (2026-11-21); Excel on this PC splits at `;` (Surprises & Discoveries); pandas reads the same file with `sep=";"`.
  Date/Author: 2026-11-21, user and agent.
- Decision: one clock for all files: `std::chrono::steady_clock` nanoseconds, read through one inline function `recordClockNs()` in the new header `src/devices/include/devices/recordclock.h`, used by `RsiDevice`, `PlcDevice` and the recorder. CSV time columns `t_s` are seconds since the session start with six decimals. `session.json` "start" is the local wall time of `t_s` = 0 with UTC offset, obtained once at the session end from one pair (wall time now, clock now). New helper justification: three classes in two libraries must stamp rows on the same clock; one named function prevents a different clock or unit at one site, and no existing function provides process-wide nanoseconds (`QElapsedTimer` counts from its own start).
  Date/Author: 2026-11-21, agent.
- Decision: PLC rows are recorded only inside sessions, into `plc.csv`: one `state` row at `t_s` = 0 with the full state at the session start, then one row per change or PLC event until the session end. Y2.0 (spindle) is added; X2.0 is not. Outside sessions nothing is recorded; the per-connection `plc_io_*.csv` files disappear.
  Rationale: user left it to the agent (2026-11-21). Inside a run the PLC rows explain start, stop, interruption and AUT EXT loss, and Y2.0 shows the spindle; connection-only files (16 of 30) have no analysis value; a start that fails after Start Calibration is still a `_failed` session with its rows. X2.0 duplicates RSI `Touch`, which has 4 ms resolution, while PLC change frames can be lost (`docs/code context/ARCHITECTURE.md` §7).
  Date/Author: 2026-11-21, agent.
- Decision (revises the previous one for the probe): `plc.csv` gains the column `y1_7_probe`, PLC output Y1.7, the PLC's copy of X2.0 to `$IN[8]`. Its changes were already logged (the change keys compare the whole Y1 list) and appeared as `io` rows without a visible change; in the first session of the new format (`records/2026-11-21_16-41-09_calibration_failed`) each "on" row came 1.5–2.8 ms before the controller's first Touch 1, so the column shows the probe at the PLC with millisecond timing. The 14 `plc.csv` written before (13 converted, 1 from that run) got the column empty, because their Y1.7 values were not stored. Change: `sessionfiles.cpp` `Y1Bits` table instead of `Y1BitCount` (208 → 209 lines), header, `RECORDS_FORMAT.md`, the converter (empty for old logs).
  Date/Author: 2026-11-21, user (decision) and agent.
- Decision: RSI packet data is split into `rsi.csv` (one row per received request: IPOC, Start, Result, TouchIndex, Touch, Delay, RIst, AIPos, and the reply's stream index, RKorr, Stop and Result) and `rsi_timing.csv` (same rows, same order: IPOC, receive time and the handler, event-loop and reply times as microsecond offsets from the receive time). `MACur` and other XML elements are not kept; the raw XML is written (Base64) only for a request whose XML cannot be read.
  Rationale: the user wants the timing data kept for now because late requests stopped the robot (2026-11-21); a separate file keeps `rsi.csv` minimal for motion analysis and lets the timing data be dropped later by deleting one writer. Offsets from the receive time read directly as lateness in Excel. Nothing reads `MACur` (previous plan, Surprises).
  Date/Author: 2026-11-21, user and agent.
- Decision: during the exchange `RsiDevice` copies each accepted request's XML bytes into one byte buffer reserved in advance (128 MiB) and appends one fixed-size record (`RsiCaptureRecord`) to a vector reserved in advance (262,144 records, about 17 min of continuous RSI); the reply fields are assigned to the record of the answered request. No Base64, number formatting, timer restart, allocation or console output happens per packet. A full buffer counts dropped requests. XML is decoded only in the file writer. This supersedes the previous plan's decision to keep Base64 text rows on `ControlIO`.
  Rationale: user requirement 2026-11-21 (store as fast as possible, write after the session). A copy of 430 bytes into reserved memory is cheaper than decoding numbers or Base64 encoding and keeps the exact text as sent, so the writer copies values without rounding. The request record is stored before the reply (about 0.1 µs) because keeping datagrams for after the reply would need a container allocated per call.
  Date/Author: 2026-11-21, agent.
- Decision: `RsiDevice` hands its stored data to the GUI thread as one value `RsiCapture` through the queued signal `captureReady(const RsiCapture&)`: after 1 s without an accepted request (checked by the existing timer member, started once per capture, never per packet), on the new slot `flushCapture()` (then the capture's `requested` flag is true, also when empty), before a new table is installed, on disconnect and on stop. It copies the used part out and clears its buffers without releasing their capacity. Requests are stored only while a table is installed; captures without a table (`rsi_packets_unassigned_*`) no longer exist.
  Rationale: a calibration run has 158 RSI blocks separated by seconds, a chamfer run one block; silence chunks serve both, and the recorder joins the chunks of one calibration run. Copying out keeps the reserved memory, so no 128 MiB allocation happens between two searches. Packets without a table belong to no run.
  Date/Author: 2026-11-21, agent.
- Decision: a new GUI-thread class `SessionRecorder` (`src/application/records/sessionrecorder.{h,cpp}`) owns the armed job (from the last successful Sampling or test LIN), the open calibration session, the PLC rows since arming, the received RSI captures and the write futures. Free functions in `src/application/records/sessionfiles.{h,cpp}` build the file contents and write a folder on a `std::async` worker. Requirement and why reuse fails: session state must outlive one Sampling and one PLC or RSI signal and be written at shutdown; `SceneMachiningPreparation` (542 lines) owns Sampling and the calibration run state, but chamfer sessions start and end without any of its calls, and the write futures must be joined at a different point of shutdown than its Sampling futures.
  Date/Author: 2026-11-21, agent.
- Decision: chamfer and test LIN sessions are the RSI captures of the armed job: one capture with requests and the armed job's identifier is one session from its first to its last request; it failed when the stream did not reach Stopping (`RsiCapture::completed`). Calibration captures are added to the open calibration session; a calibration capture while no calibration run is open is not recorded and the recorder prints one error line.
  Rationale: the chamfer and test LIN programs are started on the smartPAD in T1, so the application learns about the run only from the RSI exchange; a calibration run without Start Calibration has no detections and no fit.
  Date/Author: 2026-11-21, agent.
- Decision: the calibration result's identifier (`lastCalibration.id`, `primary.source` after Reassign) is the session folder name without suffix (a failed session saves no result). The Sampling identifier (`diagnosticId`, unchanged format) stays the key that pairs a stream table with its job and is written as `job_id` in `session.json`.
  Rationale: the settings file then names the folder that holds the touches of its result.
  Date/Author: 2026-11-21, agent.
- Decision: the KRL job file the robot read is copied into the session under its own name (`calibration_data.txt` or `shared_data.txt`).
  Rationale: it is the exact input of the robot program (requirement 1 and 2: necessary for understanding a run) and cannot be regenerated after code changes; it is 10 lines for a chamfer and about 50 KB for a calibration.
  Date/Author: 2026-11-21, agent.
- Decision (Milestone 3): `session.json` is written with its top-level keys in document order by `sessionJson` (`QList<QPair<QString, QJsonValue>>`), nested objects by `QJsonDocument` (4-space indent, keys alphabetical, because `QJsonObject` sorts its keys); the converter writes the same layout. The KRL program is a table per `SessionKind` in the recorder, not a `SessionJob` field, and a job's plan and KRL job file are one `files` list. `rsiTables` and `plcCsv` stay file-local in `sessionfiles.cpp` (only `writeSessionFolder`, `recordNumber`, `recordArray` are used outside). Helper justifications (requirement and why reuse fails): `storeRequest`/`emitCapture`/`onCaptureTimer`/`flushCapture` replace `appendTrace`/`saveTrace`/`writeTrace`, which wrote files from the device; `xmlValues` is the moved body of `captureRow`, whose input was a Base64 text row; `bitMask` packs the published IO lists that `bitText` formatted per cell; `calibrationInputs` and `resultObject` build `session.json` objects that no existing function produces (`SceneCalibration`'s `summaryObject` has other keys and is file-local); `recordNumber`/`recordArray` round once for both callers (preparation, writer); `plcRows` cuts the session window from rows kept since arming; `writeCalibration` and `reportWrites` are shared by the timer, the capture slot and `shutdown`.
  Date/Author: 2026-11-21, agent.
- Decision: the robot checks of Milestones 3 and 4 are done together once, after Milestone 4; each milestone ends with the checks that need no robot.
  Rationale: user decision 2026-11-21; a calibration run takes about 17 min.
  Date/Author: 2026-11-21, user.
- Decision: the chamfer `plan.csv` time column is named `time_s`, not `t_s`.
  Rationale: `t_s` means the record clock since the session start in every other file; the plan's time is planned time since the lead-in start, so one name must not denote two quantities.
  Date/Author: 2026-11-21, agent (Milestone 1).
- Decision: `writeSharedData` and `writeCalibrationData` in `scenemachiningpreparation.cpp` return a file-local value `JobFile{QByteArray contents; QString error;}` instead of an error string; the caller saves the contents with the existing `saveFile` and hands them to the recorder. Requirement and why reuse fails: the job file text is needed twice (KRL share and session folder) and `writeSharedData` can fail before saving (pose mismatch), so contents and error form one result (`AGENTS.md`, results through the return value).
  Date/Author: 2026-11-21, agent.
- Decision: force sensor recording is not part of this plan; `FtsDevice` keeps its manual JSON log unchanged.
  Rationale: user decision 2026-11-21 ("add it later").
  Date/Author: 2026-11-21, user.
- Decision: the converter `python/records/convert_records.py` stays in the repository after Milestone 5, until the user confirms that the second PC (`D:` root) has no records in the old format; the user then decides on its deletion.
  Rationale: records are git-ignored, so each PC holds its own records.
  Date/Author: 2026-11-21, agent.
- Decision: an old capture that spans several runs of one Sampling is converted to one session; its RSI and PLC time origins are independent (old captures count from the device start, without wall time), which `session.json` states as `"converted_from"` and `"clock"`.
  Rationale: nothing in the old files maps capture time to wall time, so a split by PLC events would be a guess.
  Date/Author: 2026-11-21, agent.


## Outcomes & Retrospective

Milestone 6 is complete. Across all 23 folders (10 native and 13 converted), all 110 CSV headers matched the seven documented schemas. All 1,557,980 stored RSI rows paired with the timing rows by IPOC and receive time. Per-folder request, unanswered, delayed and maximum-delay counts matched `session.json`. Native input keys matched by kind. The remaining acceptance item is the deferred B2 timing comparison.

The final C++ review traced the sampler return values to preparation, `RsiDevice::storeRequest` to its reply bookkeeping, and `PlcDevice::recordIo` to the recorder. `sharedData` returns bytes and an error together. `main.cpp` connects the devices by queued signals and stops them before recorder shutdown. `writeSessionFolder` takes a session by value and returns the first write error. The largest new record-format helper, `timingRow`, takes three read-only inputs. The record changes introduce no project templates, lambdas, exceptions or output parameters. Capture replacement has one limitation: `clearPrepared` can disarm a chamfer or test LIN job before its queued capture arrives. The format reference documents the job-ID condition. Normal shutdown retains the job.

Final counts below use physical / nonblank lines. Compared with HEAD, the affected C++ files went from 11 files (2,444 / 2,148) to 16 (3,028 / 2,666). The two CMake files went from 152 / 147 to 156 / 151. Python plus `.gitignore` went from three files (449 / 383) to four (870 / 752). The five new C++ files provide a shared clock, GUI ownership across run boundaries and an independent session writer. Putting these in preparation would couple its job invalidation to outstanding writes. These totals include concurrent calibration changes in shared files. The separate UI changes went from four QML files (1,028 / 883) to three (944 / 819). Milestone 6 itself changed documentation only.


## Context and Orientation

The following describes the pre-migration implementation and the source records used by the converter.

The repository is a Windows Qt Quick application (C++17, Qt ≥ 6.8, CMake). The robot is a KUKA KR 10 with a KR C4 controller. KRL is the robot's programming language. RSI (RobotSensorInterface) is a controller option: while a KRL program runs an RSI motion, the controller sends the PC one XML packet ("request") every 4 ms and accepts a reply carrying a position correction. IPOC is the controller's millisecond time stamp in each request; it grows by 4 per cycle. A request looks like (one line in reality):

    <Rob Type="KUKA"><RIst X="1048.144653" Y="-342.383789" Z="466.311737" A="179.999252" B="0.001736"
    C="-179.999481"/><AIPos A1="18.085981" A2="-54.739406" A3="81.065860" A4="-0.000058" A5="63.671737"
    A6="18.085263"/><MACur A1="0.199175" .../><Delay D="0"/><Start>0</Start><Result>0</Result>
    <TouchIndex>10</TouchIndex><Touch>0</Touch><IPOC>3834823</IPOC></Rob>

RIst is the actual Cartesian TCP pose (mm, deg), AIPos the actual joint angles (deg), Delay D the controller's count of late replies, Start 1 while KRL wants corrections, TouchIndex the stream number (the touch number in calibration; it may be the text `not linked:3` when the input is unconnected), Touch 1 while the probe is deflected. A reply (built by `encodeReply` in `src/devices/rsi/rsidevice.cpp`) is

    <Sen Type="ImFree"><RKorr X="0.004000" Y="0.000000" Z="0.000000" A="0.000000" B="0.000000"
    C="0.000000"/><Stop>0</Stop><Result>0</Result><IPOC>3834823</IPOC></Sen>

"Sampling" is the application action RSI Sampling (`SceneMachiningPreparation::prepare` for a chamfer path, `prepareCalibration` for the calibration path; `prepareTestLin` installs the fixed test LIN stream). It computes a stream table (one list of corrections per TouchIndex), writes the KRL job file `C:/RoboCrapJobs/shared_data.txt` (chamfer) or `C:/RoboCrapJobs/calibration_data.txt` (calibration), and today also writes the plan file `records/<id>/rsi_plan_<id>.csv`; `<id>` is `diagnosticId`, a UTC time stamp (`calibration_` prefix for calibration, `test_lin_` for the test LIN). The table and the identifier reach `RsiDevice::setCorrections` through the queued signal `SceneMachiningPreparation::correctionsReady` (connected in `src/application/main.cpp`).

Threads. The GUI thread runs the scene classes, `SceneMachiningPreparation` and QML. `RsiDevice` (and `FtsDevice`) live on the I/O thread `ControlIO`, `PlcDevice` on `GeneralIO`; both threads belong to `DeviceHub` (`src/application/devices/devicehub.*`). Devices publish values to QML through `stateReady` and the `DeviceRunner` (`DeviceRunner::dataUpdated(QVariantHash)` delivers the changed keys on the GUI thread). Signals from a device to a GUI object are queued; custom types crossing threads are registered in `main.cpp` with `qRegisterMetaType`.

The RSI reply path. `RsiDevice::onReadyRead` reads all pending datagrams, keeps only those from the controller (`acceptsSender`), decodes each with `decodeRequest`, answers only the newest decodable one (`advance` steps the run state Idle → Streaming → Finished → Stopping and returns the correction), and emits `touchDetected` after replying. Today it then appends text rows (`pc_ns,rx,…,<Base64 XML>` and a reply row with timing values) to the 64 MiB buffer `m_trace` through `appendTrace`, which restarts the 250 ms timer `m_traceTimer`; `saveTrace` then starts a `std::async` worker (`writeTrace`) that converts rows with `captureRow` and appends them to `records/<id>/rsi_packets_<id>.csv`. The timing values are: `entryNs` (handler entry), `m_sleepNs`/`m_wakeNs` (the `ControlIO` event loop's last sleep and wake, from `QAbstractEventDispatcher::aboutToBlock`/`awake`), `m_handlerDoneNs` (end of the previous replying handler), the send time, the `writeDatagram` duration and its byte result. `python/rsi/rsi_packet_timing.py` uses them to classify late reads.

The calibration run. `SceneMachiningPreparation::startCalibration` asks the PLC to start program 1; `recordTouch` collects `ProbeTouchDetection{touch, joints}` from `RsiDevice::touchDetected`; `onPlcData` sees the program finish; `endRun(error)` writes `calibration_touches_<id>.csv`, runs `fitProbeCalibration` (`src/trajectories/probe/probecalibrationfit.cpp`), saves the result in the settings file through `SceneCalibration::recordCalibrationResult(fit, id)` and writes `calibration_result_<id>.csv` (two tables in one file). `clearPrepared` ends a run, deletes the job files and sends an empty table. `shutdown` runs at `aboutToQuit`, before `DeviceHub::stopAll`.

The PLC log. `PlcDevice` (`src/devices/plc/plcdevice.cpp`) opens `records/plc_io_<local time>.csv` at every connect (`openLog`) and writes a row (`writeLog(event)`) for each change of X1, Y1, step, program or result (`publishIo`, events `io`) and for `run <n>`, `resume`, `abort`, `reply timeout <tid>`, `read error …`, `unmatched reply …`, `error cmd=… code=…`. `m_logged` holds the last logged values.

Source records before migration (2026-11-21). Thirteen calibration folders contain packet captures of real runs (see the matching list in Artifacts and Notes); 18 folders (8 chamfer `2026…_…`, 10 `calibration_…`) contain only a plan; 30 `plc_io_*.csv` lie in `records/`, two more inside the two oldest calibration folders. Captures exist in two formats: Base64 (`pc_ns,direction,stream_index,write_bytes,write_ns,xml_base64` plus, on `tx` rows, the four handler times; split into `rsi_packets_<id>.csv` and `_part2` … `_partN`; the six oldest sessions) and decoded (the original 35-column schema, one file). The two oldest folders use the bare time stamp and the names `rsi_calibration_plan_<ts>.csv`, `rsi_touches_<ts>.csv`.


## Original format specification (Milestone 1)

The [current format reference](../docs/code%20context/RECORDS_FORMAT.md) incorporates the verified behavior of the writer, including optional files, per-capture limits and time origins.

Folder: `records/yyyy-MM-dd_HH-mm-ss_<kind>[_failed]`, kinds `calibration`, `chamfer`, `test-lin`. Files present depend on the kind; a file without rows is not written.

`session.json` (indented JSON, keys in this order; the `//` remarks below explain the example and are not part of the file):

    {
      "format": 1,
      "kind": "calibration",
      "krl_program": "RSI_Probe_Calibration",
      "start": "2026-11-20T16:58:26.391+03:00",
      "duration_s": 1001.234,
      "outcome": "ok",                      // or "failed"
      "error": "",                          // the run error text when failed
      "job_id": "calibration_20261120_135817_781",
      "inputs": { ... },
      "result": { ... },                    // calibration with outcome ok only
      "rsi": {"requests": 132812, "unanswered": 103, "delayed": 30, "max_delay": 6,
              "unreadable": 0, "dropped": 0}
    }

`rsi.delayed` counts requests with Delay D > 0, `max_delay` is the largest D, `unanswered` the requests without reply, `unreadable` the requests whose XML could not be read, `dropped` the requests not stored because the buffer was full. Converted sessions add `"converted_from": "<old folder>"` and `"clock": "rsi and plc times have separate origins"`.

`inputs` for calibration: `tool_xyzabc` (flange-to-TCP X, Y, Z mm, A, B, C deg, as sent to KRL), `sphere_centre_b_mm` (3), `sphere_radius_mm`, `ball_radius_mm`, `strut_radius_mm`, `strut_recess_mm`, `search_feed_mm_s`, `transfer_feed_mm_s`, `transfer_acceleration_mm_s2`, `touches`, `steps`. For chamfer: `path_name`, `tool_xyzabc`, `chamfer_size_mm`, `chamfer_angle_deg`, `flip_axis`, `staging_distance_mm`, `lead_in_clearance_mm`, `lead_in_span_deg`, `lead_out_clearance_mm`, `lead_out_span_deg`, `machining_feed_mm_s`, `acceleration_distance_fraction`, `transfer_feed_mm_s`, `transfer_acceleration_mm_s2`, `staging_joints_deg` (6), `joint_speed_limits_deg_s` (6), `phase_end_times_s` (3: lead-in end, machining end, lead-out end), `cycle_s` (0.004), `settle_zero_cycles` (50). For test LIN: `direction` ("WORLD +X"), `step_mm` (0.004), `cycles` (25000), `settle_zero_cycles` (50).

`result` (calibration): `ball_centre_f_mm` (3), `sphere_centre_b_mm` (3), `verification_sphere_centre_b_mm` (3), `contact_vertical_mm`, `contact_horizontal_mm`, `quantile95_mm`, `repeatability_mm`, `smallest_singular_value`, `calibration_touches`, `verification_touches`. Numbers are JSON numbers written with six decimals.

`plan.csv`, calibration (one row per touch):

    touch;search_step;purpose;group;repetition;pass_touch;tilt_deg;roll_deg;normal_x;normal_y;normal_z;start_x_mm;start_y_mm;start_z_mm;start_a_deg;start_b_deg;start_c_deg;contact_x_mm;contact_y_mm;contact_z_mm;contact_a_deg;contact_b_deg;contact_c_deg;start_s;start_t

`plan.csv`, chamfer (one row per RSI cycle; row 0 at time 0 has a zero increment):

    time_s;target_x_mm;target_y_mm;target_z_mm;target_a_deg;target_b_deg;target_c_deg;a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg;increment_x_mm;increment_y_mm;increment_z_mm;increment_a_deg;increment_b_deg;increment_c_deg

Values, precision and meaning are those of today's plan files (`chamferrsistream.cpp`, `probersistream.cpp`); only the separator, the header and the comment lines change.

`rsi.csv` (one row per accepted request in receive order; reply columns empty for an unanswered request; request values are the XML text as sent, reply values the six-decimal text that was sent):

    t_s;ipoc;start;result;touch_index;touch;delay;rist_x_mm;rist_y_mm;rist_z_mm;rist_a_deg;rist_b_deg;rist_c_deg;a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg;stream_index;corr_x_mm;corr_y_mm;corr_z_mm;corr_a_deg;corr_b_deg;corr_c_deg;stop;reply_result;xml_base64

`stream_index` is the index of the correction sent in this reply (the next increment before the reply), `reply_result` the reply's Result, `xml_base64` only for unreadable XML.

`rsi_timing.csv` (same rows in the same order; times in microseconds with one decimal relative to the row's receive time, negative when earlier; handler columns only on answered rows):

    ipoc;t_s;entry_us;sleep_us;wake_us;prev_done_us;reply_us;write_us;write_bytes

`entry_us` handler entry, `sleep_us`/`wake_us` the `ControlIO` event loop's last sleep and wake before the handler, `prev_done_us` end of the previous replying handler, `reply_us` the moment `writeDatagram` was called, `write_us` its duration (not an offset), `write_bytes` its result (−1 on failure).

`plc.csv`:

    t_s;event;x1_0_aut_ext;x1_2_peri_rdy;x1_3_stopmess;x1_4_pro_act;x1_5_pgno_req;x1_6_appl_run;x1_7_in_home;y1_0_ext_start;y1_1_drives_on;y1_2_conf_mess;y1_3_pgno0;y1_4_pgno1;y1_7_probe;y2_0_spindle;step;program;result

`event` is `state` (first row, t_s 0), `io`, `run 1`, `resume`, `reply timeout <tid>`, `read error <code> frame <hex>`, `unmatched reply <tid>` or `error cmd=<c> code=<k>`; `step` is IDLE/PREP/START/SELECT and `result` -/STARTED/RESUMED/TIMEOUT/ABORTED/AUT_EXT_LOST as today. Older recordings can also contain `run <n>` for programs 2–4 and `abort` from the former PLC panel. Converted sessions leave `y1_7_probe` and `y2_0_spindle` empty.

`touches.csv` (calibration): `touch;a1_deg;a2_deg;a3_deg;a4_deg;a5_deg;a6_deg`, AIPos at detection, detection order. `residual_sets.csv` (calibration with a fit): `set;group;value_deg;mean_mm;bound_mm;count`, `set` = `ring` or `roll`. Job file: `calibration_data.txt` or `shared_data.txt`, byte copy.


## Plan of Work

Milestone 1 writes the format document. Create `docs/code context/RECORDS_FORMAT.md` with the content of the section "Record format" above, in prose with the column lists as indented blocks, plus one paragraph each on when a session starts and ends and on what is written when (see Milestones 3 and 4). It is the reference for the application, the converter and the Python scripts. Add a link to it in the "Build and modules" records bullet of `docs/code context/AGENT_CONTRACTS.md` now; the other contract texts change in Milestone 6. Result: one place defines the format; acceptance is a read-through against this plan.

Milestone 2 converts the existing records and adapts the two analysis scripts, all in Python, so that the format is proven on real data before the application writes it. Create `python/records/convert_records.py`, usage `python -I python/records/convert_records.py <records folder> [--write]`; without `--write` it only prints, per old folder, the new folder name, the files it would write, the touch count and the outcome, and the list of old entries that Milestone 5 would delete. Its rules: a folder with no `rsi_packets_*` file is a plan-only folder and is only listed for deletion. For a folder with packets, the session start is the first `run` row at or after the Sampling time converted to local time (UTC + 3 h) and before the next Sampling's local time (any old folder), taken from the PLC log inside the folder when there is one, otherwise from the `plc_io_*.csv` in `records/`; the PLC rows of the session are the rows of that file from that `run` row until the next `run` row or the end of the file, with `t_s` = local time minus start; the `state` row is the last row before the `run 1` row. RSI rows: read the main capture and its `_part<n>` files in numeric order (main first), decode Base64 XML with the rule of today's `captureRow` (element text and `Element.Attribute` keys) or read the decoded columns, join each `tx` row to the latest preceding `rx` row with the same IPOC, and write `rsi.csv` and `rsi_timing.csv` with `t_s` = (pc_ns − first rx pc_ns) / 1e9. Plan: the plan file (`rsi_plan_*` or `rsi_calibration_plan_*`) without `#` lines, renamed columns (`tilt` → `tilt_deg`, `normal_X` → `normal_x`, `start_X` → `start_x_mm`, `start_A` → `start_a_deg`, …, `start_S` → `start_s`), separator `;`. Touches: `calibration_touches_*.csv` with `a<n>` → `a<n>_deg`. Result: from `calibration_result_*.csv` when present, otherwise, when all plan touches are detected, computed with the functions of `calibration_fit.py` (import by path, not by module search). Outcome ok only with all touches detected and a result; otherwise failed with error "<n> of <m> touches detected." (or "No touch detected."). `session.json` gets `inputs.touches` only (other inputs were not recorded; the KRL step count is not derivable from the plan), `converted_from` and `clock`. With `--write` it writes the new folders (never overwriting an existing folder) and deletes nothing. Then change `python/calibration/calibration_fit.py` to read `plan.csv` and `touches.csv` of a session folder (`;`, new column names) and print, in the format of `session.json` "result" and `residual_sets.csv`, its result followed by the differences to the values stored in the folder (exit status 1 when any differs by more than 1.5e-6, because both sides are rounded to six decimals). Change `python/rsi/rsi_packet_timing.py` to take a session folder, read `rsi.csv` and `rsi_timing.csv` row by row together, and build the same packet dictionaries as its present `decoded_packet` (requests: `ns` from `t_s`; replies: `ns` = request ns + `reply_us`·1000, `times` from the four handler offsets) and the plan from `plan.csv`; remove the two old-format readers. Result: converted folders for the 13 recorded runs exist beside the old entries; the scripts reproduce the reference values of Artifacts and Notes on them.

Milestone 3 makes the application write calibration sessions. In `src/devices/include/devices/recordclock.h` define `inline qint64 recordClockNs()` returning `std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()`. In `rsidevice.h` add the public value types `RsiCaptureRecord` and `RsiCapture` (Interfaces), the signal `captureReady(const RsiCapture& capture)` and the public slot `flushCapture()`; replace `m_trace`, `m_traceDropped`, `m_traceFile`, `m_traceSaves`, `m_traceClock` and `m_diagnosticId` by one member `RsiCapture m_capture` (its `diagnosticId` replaces `m_diagnosticId`); keep `m_traceTimer` renamed `m_captureTimer`; replace `appendTrace`, `saveTrace` and `writeTrace` by `qsizetype storeRequest(const QByteArray& xml, qint64 receivedNs)` (returns the record index, −1 when not stored: no table installed or a buffer full, which increments `m_capture.dropped`), `void emitCapture(bool requested)` and the timer slot `onCaptureTimer()`. In `rsidevice.cpp` delete `TraceCapacity`, `CaptureIdleMs`, `CaptureHeader`, `XmlColumns` and `captureRow`; add the constants `CaptureRecords = 262144`, `CaptureXmlBytes = 128 * 1024 * 1024`, `CaptureSilenceNs = 1'000'000'000`, `CaptureCheckMs = 250`. `connectDevice` reserves both buffers where it reserved `m_trace`. `onReadyRead`: take `entryNs = recordClockNs()`; for each accepted datagram call `storeRequest(datagram.data(), recordClockNs())` before decoding, remember the index of the newest decodable request; after `writeDatagram`, when that index is ≥ 0, assign the reply fields of that record directly (entry, sleep, wake, previous-done, reply and write times, written bytes, stream index, correction, stop, result). The `receivedRows` vector disappears. `storeRequest` starts `m_captureTimer` when the record vector was empty. `onCaptureTimer` emits the capture when the last record is older than 1 s. In `advance`, where `Finished` changes to `Stopping`, set `m_capture.completed = true`. `setCorrections` stores the drained requests (old table), calls `emitCapture(false)` when records exist, then installs the table and the new `m_capture.diagnosticId`. `disconnectDevice` and `stopDevice` call `emitCapture(false)` when records exist (`stopDevice` no longer waits for save workers). `emitCapture` copies the used records and bytes into a new `RsiCapture` (deep copies: `QVector<RsiCaptureRecord>(begin, end)`, `QByteArray(data, size)`), clears the members with `resize(0)` and `truncate(0)` (Qt 6 keeps the capacity of an unshared container), stops the timer, resets `dropped` and `completed`, and emits; `flushCapture` calls `emitCapture(true)`. Replace `m_traceClock.nsecsElapsed()` everywhere by `recordClockNs()`. In `plcdevice.h` add the public value type `PlcIoRecord` and the signal `ioRecorded(const PlcIoRecord& record)`; delete `m_log`, `openLog` and the `QFile` forward declaration; `writeLog(event)` becomes `recordIo(event)`, which builds a `PlcIoRecord` from `m_logged` (bit masks from the `x1`, `y1`, `y2` lists, codes as numbers) and emits it; `publishIo` adds `y2` to its change keys; `LogHeader` and the name tables move to the writer (`sessionfiles.cpp`). In `src/trajectories`, `probersistream.h`/`.cpp`: `ProbeCalibrationRsi` gains `QByteArray plan` (the new `plan.csv` text); `writePlan` becomes `planCsv() const` returning the text; `sampleProbeCalibrationRsi` no longer writes a file and its header comment says so. Create `src/application/records/sessionfiles.{h,cpp}` and `sessionrecorder.{h,cpp}` (Interfaces) and add the four files to `src/application/CMakeLists.txt`. In `scenemachiningpreparation.{h,cpp}`: add `attachRecorder(SessionRecorder* recorder)` next to `attachPlc`; `writeCalibrationData` returns the job file text through a value `JobFile{QByteArray contents; QString error;}` (the caller saves it with `saveFile` as now and gives the contents to the recorder); after a successful calibration Sampling call `m_recorder->arm(SessionJob{…})` with kind calibration, program `RSI_Probe_Calibration`, `job.diagnosticId`, `job.plan`, the job file name and contents and the calibration `inputs` object (built here from the motion's plan parameters and robot timing and the job's `toolCoordinates`); `clearPrepared` calls `m_recorder->disarm()`; `startCalibration` calls `m_recorder->openCalibration()`; `writeCalibrationTouches` and `writeCalibrationResult` become `touchesCsv(detections)`, `residualSetsCsv(fit)` and `resultObject(fit)` returning contents; `endRun` adds `touches.csv` and `residual_sets.csv` with `addFile`, sets the result with `setResult`, passes `m_recorder->sessionName()` to `recordCalibrationResult` and finally calls `m_recorder->closeCalibration(m_runError)`; `shutdown` ends an active run with "Application closed during the calibration run." before `clearPrepared`. In `main.cpp` declare `SessionRecorder recorder;` before `preparation`, register `RsiCapture` and `PlcIoRecord` with `qRegisterMetaType`, connect `rsiDev::captureReady` → `recorder::addRsiCapture`, `plcDev::ioRecorded` → `recorder::addPlcRecord`, `recorder::captureRequested` → `rsiDev::flushCapture` (all queued), call `preparation.attachRecorder(&recorder)`, and connect `aboutToQuit` → `recorder.shutdown` after the `stopAll` connection. Result: a calibration run writes its folder; Sampling of the calibration path writes nothing to `records/`; PLC logs are no longer written outside runs.

The recorder's behavior. `arm` stores the job, clears the PLC rows except the latest row (kept as the state source). `disarm` drops only the armed job; an open or closing calibration session keeps its own copy of the job and is not affected (`clearPrepared` disarms right after `endRun` closed the session, while its requested capture is still on the way). `addPlcRecord` updates the latest row and appends the row. `openCalibration` creates the open session (start = `recordClockNs()`, kind calibration, a copy of the armed job) and fixes its base name from the start time. `addRsiCapture`: with an open or closing calibration session whose job identifier equals the capture's, append the capture (when the session is closing and `requested` is true, write it); with an armed chamfer or test LIN job, the job's identifier and at least one record, write a session at once (start = first record's receive time, end = last record's, outcome failed with "RSI exchange ended before the stream finished." when `completed` is false); a calibration capture without an open session prints "[ERROR] Records: RSI exchange of <job id> outside Start Calibration was not recorded."; other captures are ignored. `closeCalibration(error)` stores the error and end time, emits `captureRequested` and waits for the requested capture; the 250 ms poll timer writes without it after 2 s and adds "RSI capture not received." to the error. Writing (`startWrite`): compute the outcome and folder name (collision suffix by `QDir::exists`), the wall time of the start, take the PLC rows with time ≥ start and ≤ end plus a `state` row from the latest row before start, build a `SessionRecord` and start `std::async(writeSessionFolder, record)`. The poll timer prints the errors of finished writes (`[ERROR] Records: …`) and stops when nothing is pending. `shutdown` calls `QCoreApplication::sendPostedEvents(this, QEvent::MetaCall)` to deliver the captures and PLC rows that `stopAll` produced, writes a still-closing session, and waits for all write futures. Folder writing (`writeSessionFolder`, worker thread): `QDir().mkpath`, then each file with `QSaveFile`; the RSI files and the `rsi` summary come from one decoding pass (`rsiTables`) over all captures; returns an empty string or the first error.

Milestone 4 adds chamfer and test LIN sessions. In `chamferrsistream.h`/`.cpp`: `ChamferRsiStream` gains `QByteArray plan`; the `QSaveFile m_trace` member and `writeTrace` become a `QByteArray m_plan` appended by the same row code (separator `;`, new header, no `#` lines; the comment values move to the chamfer `inputs`); the open/commit/console lines are deleted. In `scenemachiningpreparation.cpp`: `writeSharedData` returns `JobFile`; after a successful chamfer Sampling arm the recorder with kind chamfer, `RSI_Chamfer_Hole`, `stream.diagnosticId`, `stream.plan`, `shared_data.txt` and the chamfer inputs (from `motion.parameters()`, `motion.robot().timing`, the staging joints and phase times used today in the plan's comment lines, `stream.toolCoordinates`, the path name); `prepareTestLin` arms kind test-lin, `RSI_Probe_Check`, its `test_lin_` identifier, no plan, no job file, the test LIN inputs. Result: every chamfer or test LIN run that exchanges RSI packets writes one folder; a stopped or timed-out run is `_failed`.

Milestone 5 deletes the old records. Run the converter without `--write` and show the user the deletion list (all 61 old top-level entries, that is the 31 old folders and the 30 `plc_io_*.csv`, after Milestone 2 created the converted folders); after the user's confirmation in chat delete exactly those entries. In `settings/probe_calibration.json`, replace `lastCalibration.id` (and `primary.source` if present) when it equals an old identifier by the converted folder name (`calibration_20261120_135817_781` → `2026-11-20_16-58-26_calibration`, per the converter's mapping). Result: `records/` holds only session folders.

Milestone 6 updates the owning documents: `docs/code context/AGENT_CONTRACTS.md` ("Build and modules" records bullet; "Devices and protocols" RSI and FTS-recording bullets: capture in memory, `captureReady`, no files from devices except the FTS log; "Machining and playback": the RSI bullets on plan files, record folders, touch and result files, the capture ordering bullet), `controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md` (diagnostics section), `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md` ("Plan, packet and touch files"), `controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md` (capture names), `docs/code context/APP_DESCRIPTION.md` (workflow steps 1 and 5), `docs/code context/ARCHITECTURE.md` (threads table: RSI sampling writes no plan file, new "Records write (`std::async`)" row; shutdown: recorder after `stopAll`), and the record paths named in `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md`, `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_SPEEDUP_RU.md`, `docs/studies/workpiece_reference/WORKPIECE_REFERENCE_BASELINE.md` and `docs/solutions/KUKA_PASSIVE_STOP_EXTERNAL_ACK.md` where they describe current behavior (dated history stays). Completed ExecPlans are not edited. Then the final compliance pass over every changed declaration, body and caller, and the final counts.


## Concrete Steps

Commands in Git Bash from the repository root (`E:/Qt/QtProjects/RoboCrap` or `D:/Qt/QtProjects/RoboCrap`). At the start and end of every milestone:

    git status --short
    wc -l <files of the milestone>
    git diff --check

Milestone 2:

    python -I python/records/convert_records.py records
    python -I python/records/convert_records.py records --write
    python -I python/calibration/calibration_fit.py records/2026-11-20_16-58-26_calibration
    python -I python/rsi/rsi_packet_timing.py records/2026-11-18_19-00-12_calibration

The dry run must list 13 sessions (names in Artifacts and Notes, start times taken from the PLC files) and 61 old entries for deletion; the fit must print zero differences.

Milestone 5 (only after the user's confirmation of the printed list):

    python -I python/records/convert_records.py records    # list again; must equal the confirmed list

then delete the listed entries (for example `rm -r records/calibration_2026… records/2026…_…` and `rm records/plc_io_*.csv`, exactly the confirmed names).


## Validation and Acceptance

Milestone 1: the document lists every file, column, unit and naming rule of this plan; `git diff --check` is clean.

Milestone 2 (agent): 13 converted folders; for the four sessions with an application result file (`calibration_20261118_113045_223`, `…160007_715`, `…164135_671`, `calibration_20261120_135817_781`) `calibration_fit.py` prints zero differences against the converted `session.json` "result" and `residual_sets.csv`; for the six sessions of the reference table in Artifacts and Notes its ball centre and χ95 equal the reference values within 1e-6 mm; `2026-11-20_14-16-17_calibration_failed` (3 touches) and the other incomplete runs report "<n> of 158 touches detected."; every converted `rsi.csv` opens in Excel (by double-click, on this PC) as columns with numeric `t_s`; `rsi_packet_timing.py` prints request and reply counts equal to the old capture's rx and tx row counts (for `calibration_20261118_160007_715`: 134,090 requests, 133,983 replies, 33 rejected replies as reported by the previous plan).

Milestone 3. The Sampling and PLC connection checks passed. For B2, use the tested application with the PLC connected and RSI bound. Set `StepLimit = 0` on the smartPAD for all 485 steps. With CELL ready at HOME and AUT EXT active, open Measuring / Calibration, generate the path if needed, and run RSI Sampling. Click **Start Calibration** in that toolbar and note the local start time. Let the run reach 158/158 touches, then choose **Cancel** in “Calibration complete”. The Passive STOP and E-STOP UI checks already passed.

Expect one new `records/<local start>_calibration` folder, appearing only after the run ends. Compare the `session.json` start with the observed start time (within 1 s). Expect outcome `ok`, a result, about 132,800 RSI requests, matching row counts in `rsi.csv` and `rsi_timing.csv`, 158 rows in `touches.csv`, and a `plc.csv` starting with `state` and containing `run 1`. The console should be quiet during the RSI exchange. Run the offline fit and timing scripts on that folder. The fit should report zero differences, and `lastCalibration.id` in the settings should match the folder name. Compare delayed and unanswered requests with the 2026-11-20 16:58 full run (2 each).

Milestone 4 (user-authorized test LIN, then a chamfer run in T1): each produces one folder `…_test-lin` or `…_chamfer` with `plan.csv` (chamfer), `shared_data.txt` (chamfer), `rsi.csv`, `rsi_timing.csv`, `plc.csv`; releasing the enabling switch during a test LIN gives a `_failed` folder. A chamfer Sampling alone writes nothing to `records/`.

Milestone 5: `records/` contains only folders matching `yyyy-MM-dd_HH-mm-ss_<kind>[_failed]`, apart from `fts_*.json` files the force sensor panel may have saved (force recording is outside this plan); the settings file names an existing folder.

Milestone 6: outside `plans/`, no document describes `rsi_plan_<id>.csv`, `rsi_packets_<id>.csv`, `plc_io_*.csv`, `calibration_touches_<id>.csv` or `calibration_result_<id>.csv` as current behavior.


## Idempotence and Recovery

Code edits are repeatable. The converter without `--write` changes nothing; with `--write` it skips a target folder that exists, so a rerun after a fix needs the wrong converted folder deleted first (converted folders are reproducible from the old files until Milestone 5). Old records are deleted only in Milestone 5, after the user's confirmation; before that step the user may copy `records/` elsewhere as a backup (873 MB). If the application crashes or the PC loses power during a session, that session's record is lost, because files are written only after the session (user requirement); a failed folder write prints `[ERROR] Records: …` with the reason. If the RSI buffer fills (more than 262,144 requests or 128 MiB in one capture), later requests are counted in `rsi.dropped` and not stored; the run itself is unaffected.


## Artifacts and Notes

Old runs and their converted folders (written 2026-11-21; start = first `run` PLC row after the Sampling), touches detected:

    calibration_20261114_170013_165  2026-11-14_20-00-40_calibration         158 (PLC log in the old folder)
    calibration_20261117_113235_576  2026-11-17_14-33-29_calibration         158 (PLC log in the old folder)
    calibration_20261117_121258_825  2026-11-17_15-13-12_calibration         158
    calibration_20261117_124928_908  2026-11-17_15-49-32_calibration         158
    calibration_20261117_132108_323  2026-11-17_16-21-12_calibration         158
    calibration_20261118_113045_223  2026-11-18_14-31-39_calibration         158 (result file)
    calibration_20261118_160007_715  2026-11-18_19-00-12_calibration         158 (result file)
    calibration_20261118_164135_671  2026-11-18_19-41-37_calibration         158 (result file)
    calibration_20261120_111613_975  2026-11-20_14-16-17_calibration_failed    3
    calibration_20261120_114435_722  2026-11-20_14-44-37_calibration_failed   84
    calibration_20261120_125540_578  2026-11-20_16-01-14_calibration_failed   23
    calibration_20261120_132837_108  2026-11-20_16-30-08_calibration_failed    0
    calibration_20261120_135817_781  2026-11-20_16-58-26_calibration         158 (result file)

Reference fit values from the previous plan (ball centre in {F}, χ95, mm):

    20261114_170013_165             0.024193 -0.055562 126.967541  chi95 0.111165
    20261117_113235_576             0.023383 -0.056560 126.968276  chi95 0.112047
    calibration_20261117_121258_825 0.024644 -0.056340 126.970725  chi95 0.114295
    calibration_20261117_124928_908 0.023020 -0.056376 126.967089  chi95 0.110457
    calibration_20261117_132108_323 0.023805 -0.057501 126.968111  chi95 0.112924
    calibration_20261118_113045_223 0.022782 -0.055356 126.967929  chi95 0.110674

Late requests of earlier runs (previous plan): `calibration_20261118_160007_715` 107 unanswered, 140 rx with Delay > 0; `calibration_20261118_164135_671` 27 and 46.


## Interfaces and Dependencies

No new library, target or dependency. `RoboCrapDevices` gains the header `devices/recordclock.h`; the application gains `src/application/records/`.

`src/devices/include/devices/recordclock.h`:

    // Nanoseconds of std::chrono::steady_clock: the time base of every record row.
    inline qint64 recordClockNs();

`src/devices/include/devices/rsi/rsidevice.h` (public):

    struct RsiCaptureRecord
    {
      qint64 receivedNs = 0;          // recordClockNs() at dequeue
      qsizetype xmlBegin = 0;         // in RsiCapture::requestXml
      qsizetype xmlSize = 0;
      // Answered request only; replyNs < 0 otherwise.
      qint64 entryNs = -1, sleepNs = -1, wakeNs = -1, previousDoneNs = -1;
      qint64 replyNs = -1, writeNs = -1, written = -1;
      qsizetype streamIndex = -1;
      V6d correction = V6d::Zero();
      bool stop = false;
      qint64 result = 0;
    };
    struct RsiCapture
    {
      QString diagnosticId;           // the table's Sampling identifier
      QVector<RsiCaptureRecord> records;
      QByteArray requestXml;          // accepted requests, concatenated as received
      qsizetype dropped = 0;
      bool completed = false;         // the stream reached Stopping
      bool requested = false;         // emitted by flushCapture
    };
    public slots: void flushCapture();
    signals: void captureReady(const RsiCapture& capture);

`src/devices/include/devices/plc/plcdevice.h` (public):

    struct PlcIoRecord
    {
      qint64 timeNs = 0;              // recordClockNs()
      QString event;                  // "io", "run 1", ...
      quint8 x1 = 0, y1 = 0, y2 = 0;  // bit n = terminal Xm.n / Ym.n
      int step = 0, program = 0, result = 0;
    };
    signals: void ioRecorded(const PlcIoRecord& record);

`src/application/records/sessionfiles.h`:

    enum class SessionKind { Calibration, Chamfer, TestLin };
    struct RecordFile { QString name; QByteArray contents; };
    struct SessionJob
    {
      SessionKind kind = SessionKind::Calibration;
      QString jobId;                  // the table's diagnosticId
      QVector<RecordFile> files;      // plan.csv and the KRL job file, when the job has them
      QJsonObject inputs;
    };
    struct SessionRecord              // everything one folder needs; built on the GUI thread
    {
      QString folder;                 // absolute path including the final name
      QList<QPair<QString, QJsonValue>> session;  // session.json fields in document order, without "rsi"
      qint64 startNs = 0;
      QVector<RsiCapture> captures;
      QVector<PlcIoRecord> plcRows;   // first row is the state row
      QVector<RecordFile> files;      // touches, residual sets
    };
    double recordNumber(double value);                             // six decimals
    QJsonArray recordArray(const double* values, qsizetype count);
    QString writeSessionFolder(SessionRecord record);   // worker; empty string or the error

  File-local in `sessionfiles.cpp`: `xmlValues`, `replyColumns`, `timingRow`, `rsiTables` (one decoding pass for `rsi.csv`, `rsi_timing.csv` and the "rsi" summary), `plcCsv`, `jsonText`, `sessionJson`.

`src/application/records/sessionrecorder.h`:

    class SessionRecorder final : public QObject
    {
      Q_OBJECT
    public:
      SessionRecorder();
      void arm(const SessionJob& job);
      void disarm();
      void openCalibration();
      QString sessionName() const;    // base name of the open session, empty when none
      void addFile(const RecordFile& file);
      void setResult(const QJsonObject& result);
      void closeCalibration(const QString& error);
      void shutdown();
    public slots:
      void addRsiCapture(const RsiCapture& capture);
      void addPlcRecord(const PlcIoRecord& record);
    signals:
      void captureRequested();
    private:   // createSession, plcRows, writeSession, writeCalibration(note), onTimer, reportWrites(wait)
    };

`SceneMachiningPreparation` gains `void attachRecorder(SessionRecorder* recorder);` (pattern of `attachPlc`, avoids a fourth constructor parameter). The largest new signature has three parameters (`timingRow`).


Revision note (2026-11-21): first version, from the user's requirements and answers of 2026-11-21 (Excel with `;` and `.`, no plan-only records, PLC and timing format left to the agent, force sensor later, `_failed` in the name, writing only after the session with minimal work on the RSI reply path).

Revision note (2026-11-21): credited B1 and the confirmed toolbar check. B2 now checks a successful full-session record with the current interface.

The eleven format and contract documents went from 2,740 / 2,203 physical / nonblank lines to 2,694 / 2,154. This plan went from 400 / 289 to 412 / 295 with the final checks and counts.

Revision note (2026-11-21). Closed Milestone 6 after checking the implementation and saved sessions. B2 timing remains open at the user's request.
