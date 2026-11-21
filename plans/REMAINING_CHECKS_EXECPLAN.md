# Remaining checks of all plans: one offline pass and three equipment sessions

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root. It adds no code: it collects every check that the plans in `plans/` and `plans/review_2026-11-20_0cd8996/` left open on 2026-11-21, groups them by the equipment they need, and says where each result is recorded.

Rules that override `PLANS.md` for this plan:
1. The agent never builds, configures, lints or starts the application and never runs a test. It may run read-only `git` commands and the Python analysis scripts in `python/` on recorded files, always as `python -I` (isolated mode, so no module from a data folder is imported).
2. The agent never operates equipment: no PLC, RSI or FTS connection, no outputs, no sensor bias, no KRL program start, no robot motion. Every session marked "user" is run by the user, and every step that can move the robot needs the user's explicit authorization for that step (`AGENTS.md`, "Live PLC/FTS/RSI connections …").
3. No git staging, commits, `git rm` or `git mv` unless the user asks.
4. No project tests or test harnesses. If a check fails and code must change, stop, record the failure under `Surprises & Discoveries`, and fix it under the owning plan's rules (re-read `AGENTS.md` "Mandatory C++ style" and "Mandatory code reduction" first).


## Purpose / Big Picture

All seven review plans (`plans/review_2026-11-20_0cd8996/01` … `07`) and the five earlier plans are implemented, and their builds and disconnected checks passed. What is left is a short list of checks that need the robot, plus a few offline closing steps. After this plan every open `Progress` item of those plans is either ticked with evidence or explicitly closed as not required.

The equipment checks fit into three sessions, so the robot is prepared only three times:
- **Session B**: probe calibration in AUT EXT, first a short run with `StepLimit` = 10, then a full session.
- **Session C**: `RSI_Chamfer_Hole` in T1.
- **Session D** (optional): the late-RSI-packet measurement and the controller behaviours that have never been verified.


## Progress

- [x] (2026-11-21) A1: plan 06 final pass done (no violations; one stale AGENT_CONTRACTS sentence fixed; plan 06 Progress and Outcomes written). A3 step 1 (plan 06 baseline item) ticked with it.
- [x] (2026-11-21) A2: no `records/` on this computer (`D:\Qt\QtProjects\RoboCrap`); records are on the equipment computer.
- [x] (2026-11-21) A3: both items of `PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md` ticked from existing evidence (lint superseded; reproduction met by `calibration_fit.py`, recorded in the records plan); the rerun was dropped as redundant. Session A is complete; A4 is this plan's Session B checklist.
- [x] (2026-11-21) Expectations updated to the session records of `RECORDS_SESSION_FORMAT_EXECPLAN.md` (Milestones 3–4, implemented after this plan was written); its robot checks added as S9 (Decision Log).
- [x] (2026-11-21) Session B1 (user, equipment): calibration run with `StepLimit` = 10, Start Calibration 16:41:09, end 16:41:44 (job `calibration_20261121_134054_631`). Console (user's paste): only "Probe calibration run: 3 of 158 touches detected; the calibration needs all of them." after the Sampling lines; no `[ERROR] Records:` line. One new folder `records/2026-11-21_16-41-09_calibration_failed`, written 16:41:44 (Sampling at 16:40:55 wrote nothing to `records/`): `session.json` outcome failed with that error, no "result", start 16:41:09.692+03:00, duration 34.6 s, inputs (tool −0.048/−0.097/127, sphere centre, 158 touches, 485 steps), rsi 2,594 requests, 0 unanswered, 0 delayed, 0 dropped; `touches.csv` 3 rows, touches 1–3, each equal to the AIPos of the first Start 1/Touch 1 request in `rsi.csv` (t = 20.314, 26.202, 33.522 s); no `residual_sets.csv`; `plan.csv` 158 rows; `calibration_data.txt` (36,143 bytes); `rsi.csv` and `rsi_timing.csv` 2,594 rows each, every request answered; `plc.csv` 18 rows starting with `state`, then `run 1` at 0.000128 s and the start sequence PREP/START/SELECT → STARTED at 1.66 s. S4, S6 (fewer than 158), S7 (console) and S9 (calibration folder) met for B1. User's observations: the "Calibration failed" dialog appeared, and the robot reached the same entry pose as in earlier sessions (S1 calibration part, S5 failed run).
- [x] (2026-11-21) After B1, by user decision: `plc.csv` gained the column `y1_7_probe` (Surprises; `RECORDS_SESSION_FORMAT_EXECPLAN.md` Decision Log); the 14 existing `plc.csv` got it empty. B2 needs a build of this change.
- [x] (2026-11-21) Checks paused by the user for an interface change after the E-STOP finding (Surprises): the PLC panel's "Robot program (AUT EXT)" (Run, Resume, Abort) is removed; the Calibration toolbar has Start Calibration and a separate Resume (smartPAD STOP key case, after its acknowledgement); the status line shows the interruption and what to do; leaving AUT EXT ends the calibration run (the operator's abort, also after an E-STOP, which RoboCrap cannot resume without a PLC change the user deferred). No PLC or wiring change. B2 needs a build of this change; B2 step 3 now uses the toolbar's Resume.
- [x] (2026-11-21) User confirmed the Calibration toolbar UI checks, including Passive STOP acknowledgement and Resume. The 19:19:37 session records Resume and the subsequent abort on leaving AUT EXT. S6 STOP/Resume is complete.
- [x] (2026-11-21) B2 full run and offline record checks completed. `records/2026-11-21_19-32-00_calibration` contains all eight files, all 158 touches and 36 residual sets. The offline fit found no differences. The settings identify this run as the last calibration. The console showed one PLC reply timeout and no RSI exchange or records-writer error.
- [ ] B2 timing criterion unmet. Against the November 20 16:58 reference, delayed requests increased from 2 to 34, unanswered requests from 2 to 31, and rejected replies from 0 to 3. The last delayed request was at 36.858 s. The sole PLC timeout, transaction 180 at 835.858 s, was a separate event. The PLC reported AUT EXT and program execution active before and after it. The folder creation timestamp precedes the session end calculated from the start and elapsed time by 1.640 s, so filesystem timestamps alone cannot verify when the folder first appeared. All eight files have timestamps after the calculated end.
- [ ] Session B3 (user, optional): Reassign after a full session.
- [x] (2026-11-21) C0 completed run verified in `records/2026-11-21_20-06-10_test-lin`. The controller acknowledged stream completion and the session outcome is `ok`.
- [x] (2026-11-21) C0 interrupted-run recording verified in `records/2026-11-21_20-23-06_test-lin_failed` after two earlier attempts produced no folder. The PC sent 1,509 increments totalling 6.036 mm along WORLD +X over the captured interval. Reported X varied by only 0.000488 mm. The four files establish the incomplete-stream outcome. They do not identify the switch-release time or whether shutdown flushed this capture.
- [x] (2026-11-21) Session C recording and RSI execution verified in `records/2026-11-21_20-37-59_chamfer`. The 78.605 s capture matches the second sampled path, with 19,620 stream cycles, a completed Result handshake and zero rejected replies. The earlier 20:34:27 failed session belongs to the first sampling.
- [x] (2026-11-21) Session C operator observation: the user confirmed that the approach matched the preview and that the robot retreated through PsOut and returned HOME.
- [ ] Session D (user and agent, optional): D1 late RSI packets; D2 unverified controller behaviours.
- [ ] Session E (agent): source plans and contracts updated with the results; `git diff --check`.


## Surprises & Discoveries

- Observation: after this plan was written, `RECORDS_SESSION_FORMAT_EXECPLAN.md` changed every record: one folder per run `records/yyyy-MM-dd_HH-mm-ss_<kind>[_failed]` with fixed file names (`docs/code context/RECORDS_FORMAT.md`), written only after the run; Sampling writes nothing to `records/`; no `plc_io_*.csv`; `calibration_fit.py` takes a session folder and compares itself with its `session.json`; `rsi_packet_timing.py` takes a session folder. The old records were converted and deleted. Acceptance lines and commands below are updated accordingly.
  Evidence: `plans/RECORDS_SESSION_FORMAT_EXECPLAN.md`, Progress of 2026-11-21.
- Observation (B1): `plc.csv` has six `io` rows in which no column changes (t = 20.312, 20.685, 26.200, 26.563, 33.519, 33.854 s). They are changes of PLC output Y1.7, the PLC's copy of the probe input X2.0 to `$IN[8]`, which is not a column (only Y1.0–Y1.4 are): each "on" row lies 1.5–2.8 ms before the first RSI request with Touch 1 of that touch (20.314, 26.202, 33.522 s) and each "off" row about 0.35 s later.
  Evidence: `records/2026-11-21_16-41-09_calibration_failed/plc.csv` and `rsi.csv`.
- Observation (B2 attempt, 2026-11-21): the user pressed the EMERGENCY STOP (not the STOP key) during the first PTP of program 1 and released it. PLC panel afterwards: X1.0 AUT EXT 1, X1.3 STOPMESS 1, X1.2 PERI_RDY 0, X1.4 PRO_ACT 0, X1.5 PGNO_REQ 0, X1.6 APPL_RUN 0, X1.7 IN_HOME 0; status "Step IDLE · last start STARTED · program interrupted"; Run, Resume and Abort disabled. APPL_RUN fell with the E-STOP, as on 2026-11-13 (A3 of `PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md`). The Resume rule of `RobotStartPanel.qml` and of the PLC's `FB_RunSetVar` needs APPL_RUN ON and STOPMESS OFF; it was designed for the STOP key, which keeps APPL_RUN. The PC's `robotProgramState` still says "interrupted" (Running, then PRO_ACT off), so status text and Resume rule disagree in this state. Run needs IN_HOME, so it is disabled too.
  Evidence: user screenshot of the PLC panel.
- Observation (same attempt): after the release the smartPAD shows KSS01200 (EMERGENCY STOP message); acknowledging it on the smartPAD in AUT EXT is refused with KSS01367 "Active status required". So in AUT EXT the E-STOP message is acknowledged only externally (`$CONF_MESS`, as A6 of `PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md` showed), while "KSS01370 Passive STOP" is acknowledged only on the smartPAD. RoboCrap raises `$CONF_MESS` (Y1.2) only in ST_PREP of Run or Resume, and both are disabled in this state (Run needs IN_HOME; Resume needs APPL_RUN ON and STOPMESS OFF); the panel cannot write module 1. Consequence: after an E-STOP during a program in AUT EXT, RoboCrap can neither acknowledge nor resume; recovery goes through T1 (smartPAD active); since the interface change of 2026-11-21 leaving AUT EXT ends the application's calibration run (before, only a PLC disconnect did). KSS Fig. 6-26 (SI §6.17.4) describes the external recovery: cause removed, `$DRIVES_ON` until `$PERI_RDY`, `$CONF_MESS` edge, `$STOPMESS` FALSE, `$EXT_START` continues the program; an E-STOP is distinguishable from the STOP key by `$PERI_RDY` falling. Open design question for the PLC Resume rule and `RobotStartPanel.qml` (change both sides together, `AGENT_CONTRACTS.md` "Devices and protocols"); not changed in this plan.
  Evidence: user report, 2026-11-21.


## Decision Log

- Decision: the checks are grouped by session, not by source plan.
  Rationale: one full calibration session (about 17 minutes) answers checks from five plans at once, and each robot preparation (HOME, CELL BCO run, AUT EXT) costs operator time.
  Date/Author: 2026-11-21, agent.
- Decision: two open items are closed by existing evidence instead of a new run (Session A3).
  - The Milestone 4 lint item of `PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md` is superseded: `all_qmllint` and `robocrap` passed with USE_HOT_RELOAD OFF after review plans 03 and 07 (2026-11-20 and 2026-11-21).
  - The "reproduction within 1e-4 mm" item of the same plan is met by `python/calibration/calibration_fit.py`. `RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md` records that it reproduced the application's result file of that plan's run, `calibration_20261118_113045_223`, line for line.
  Date/Author: 2026-11-21, agent.
- Decision: the robot checks of `RECORDS_SESSION_FORMAT_EXECPLAN.md` (Milestones 3 and 4, batched by the user to one equipment pass) join B1, B2 and C as S9; the test LIN run of that plan is the optional step C0.
  Rationale: the same runs produce the session folders that those checks inspect; one more preparation of the robot is avoided.
  Date/Author: 2026-11-21, user (batching) and agent.
- Decision: the open controller behaviours of the contracts are optional (Session D2), and a deliberately missed search is run only on the user's explicit decision.
  Rationale: no plan's acceptance depends on them; `PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md` lists them only as "Remaining outside this plan".
  Date/Author: 2026-11-21, agent.


## Outcomes & Retrospective

(To be written at completion.)


## Context and Orientation

Where the open items come from (path relative to `plans/`):

| ID | Source plan | Open item | Session |
|---|---|---|---|
| S1 | review …/06_GEOMETRY_TRAJECTORIES_CLEANUP_EXECPLAN.md | Milestone 5 equipment sessions: `RSI_Probe_Calibration` with StepLimit 10, `RSI_Chamfer_Hole` in T1 | B1, C |
| S4 | review …/07_APPLICATION_QML_CLEANUP_EXECPLAN.md | calibration touch and result files written through `saveFile` | B1, B2 |
| S5 | PROBE_PRIMARY_CENTRES_EXECPLAN.md | deferred: result window, Cancel, Reassign, failed run | B1, B2, B3 |
| S6 | PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md | Milestone 5 validation, not yet seen on the robot: STOP/Resume keeps progress; fewer than 158 detections gives "Calibration failed" | B1, B2 |
| S7 | RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md | console shows nothing from a calibration run; investigation of late RSI packets | B1, B2, D1 |
| S8 | PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md | not verified on the controller: missed search, 2 mm beyond contact, braking after ExitMoveCorr, PTP velocity kept through RSI calls | D2 |
| S9 | RECORDS_SESSION_FORMAT_EXECPLAN.md | robot checks of Milestones 3 and 4: calibration, chamfer and test LIN session folders, timing no worse than before | B1, B2, C0, C |

Plans with nothing open: review 01 to 05, and `PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md` (complete 2026-11-14; its "open" topics, lost PLC change frames and the external Passive STOP acknowledgement, are design questions in `docs/code context/ARCHITECTURE.md` §7 and `docs/solutions/KUKA_PASSIVE_STOP_EXTERNAL_ACK.md`, not checks).

Terms and places:
- **Records** are session folders `records/<folder>` of the repository (`ROBOCRAP_RECORDS_DIR`, git-ignored), format in `docs/code context/RECORDS_FORMAT.md`. They exist only on the computer that ran the sessions. Check that `records/` exists before any offline step that reads it.
- **Session folder** `<folder>` = `yyyy-MM-dd_HH-mm-ss_<kind>[_failed]`, local start time, kind `calibration`, `chamfer` or `test-lin`; written once, after the run ended. A calibration folder holds `session.json`, `plan.csv`, `calibration_data.txt`, `rsi.csv`, `rsi_timing.csv`, `plc.csv`, and `touches.csv` (any detection) and `residual_sets.csv` (successful fit). The result is the "result" object of `session.json`. A Sampling alone writes nothing to `records/`.
- **Job files**: `C:/RoboCrapJobs/calibration_data.txt` (calibration) and `shared_data.txt` (chamfer). The application deletes both at start-up and shutdown and before every Sampling, so RSI Sampling must run in the same session as the robot run.
- **`StepLimit`** is a DAT value of `RSI_Probe_Calibration`, set on the smartPAD: 10 runs touches 1–3, 58 the first pass, 0 all 485 steps. After a `StepLimit` end or a missed search the robot stands about 3 mm from the sphere; jog it clear in T1, select CELL and make its BCO run to HOME before the next Run (`controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md`, "AUT EXT start", "KRL flow and failures").
- **Reference magnitudes of a full session** (earlier sessions):
  - ball centre in {F} ≈ [0.023; −0.056; 126.968] mm;
  - contact distances ≈ 13.84 / 13.86 mm;
  - χ95 ≈ 0.11 mm, s_rep 0.010–0.015 mm, smallest singular value ≈ 2.04;
  - 22–23 of 36 K7 sets outside their bound;
  - 158 touches, detection 765–876 cycles after the search start.


## Plan of Work

### Session A: agent, offline (no build, no equipment)

Complete (2026-11-21, Progress A1–A3). Every remaining check needs the robot, except the agent's offline evaluations after B2 and D1, which need that session's `records/` on the equipment computer.

### Session B: user, probe calibration in AUT EXT

Preparation (as for the sessions of 2026-11-14 to 2026-11-18):
- Build the current working tree (all seven review plans) and start RoboCrap with the PLC and RSI connected; the PC power settings are those of `docs/hardware context/HARDWARE_SETUP.md`, "Power settings for RSI".
- Make sure `settings/probe_calibration.json` is committed or copied, because B3 may overwrite it.
- Robot at HOME, cell clear, operator at the E-STOP. In T1 select CELL and make its BCO run, then switch to AUT EXT; the status bar's "AUT EXT" indicator turns green.
- In Measuring/Calibration: Generate Path, then RSI Sampling.

**B1. Short run** (S1 calibration part, S4 touch file, S5 failed run, S6 fewer than 158 detections, S7 console).
1. Set `StepLimit` = 10 on the smartPAD.
2. Press Start Calibration and watch the console.
3. When the program has finished, recover the robot: T1, jog it clear of the sphere, CELL BCO run to HOME, AUT EXT.

**B2. Full session** (S4 result file, S5 window and Cancel, S6 STOP/Resume, S7 console).
1. Set `StepLimit` = 0. Run RSI Sampling again: B1's job may be stale after the recovery (each Sampling makes a new job id).
2. Press Start Calibration.
3. Let calibration run uninterrupted to 158/158 touches. The separate STOP/Resume check passed.
4. When "Calibration complete" opens, press **Cancel**.
5. Agent afterwards, offline:

       python -I python/calibration/calibration_fit.py records/<folder of B2>
       python -I python/rsi/rsi_packet_timing.py records/<folder of B2>

   The fit script compares itself with the folder's `session.json` "result" and `residual_sets.csv`.

**B3. Reassign** (S5; optional, a user decision, because it changes the primary ball and sphere centres that every later calibration path uses). After another full session (or B2 repeated), press **Reassign** instead of Cancel. To undo it, restore `settings/probe_calibration.json` from git or from the copy.

### Session C: user, `RSI_Chamfer_Hole` in T1 (S1 chamfer part, S9)

0. Optional test LIN (S9, `controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md`): the program moves PTP from HOME to `TestStart`, then 100 mm along WORLD +X at 1 mm/s unless the probe touches, so the space ahead of the probe must be free. With the RSI socket bound press "RSI test LIN" (Network page), start `RSI_Probe_Check` on the smartPAD in T1 and let it complete; for the failed case, release the enabling switch during the LIN in a second run.
1. Preparation: T1, robot at HOME, a test workpiece or free space at a safe staging distance, operator holding the enabling switch.
2. In RoboCrap, import the plane and cylinder from `data/probe_points/edge_cyl_plane/`, intersect them, Generate the chamfer path (Machining) and run RSI Sampling with the RSI socket bound.
3. Start `RSI_Chamfer_Hole` on the smartPAD.
4. The chamfer contract lists the restructured SRC's compilation and `$VEL.CP`/`$ACC.CP` kept through the RSI calls as "Not yet verified". Treat the first selection and start as that check: stop at once on any unexpected message or motion.

### Session D: optional investigations

**D1. Late RSI packets** (S7). In earlier sessions some 4 ms requests arrived 15–50 ms late, in bursts, while the application thread was idle; the cause is open (`RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md`, Surprises).
1. During a full calibration session (B2 can serve), record arrival at the adapter driver with `pktmon` or Wireshark/Npcap on `KRC4-Direct`, and record DPC/ISR latency with LatencyMon.
2. Agent afterwards: compare the driver timestamps with `t_s` in `rsi.csv` and `rsi_timing.csv` of the session folder (record clock; the driver clock needs one common reference, for example the first request), using `python -I python/rsi/rsi_packet_timing.py records/<folder>` and a scratchpad script.
3. Decide whether the delay lies below or above the network driver.

Keep the RSI adapter on the ASIX USB port: the Intel I219-V port held requests 2–16 ms and stopped runs with RSITimeout (review plan 02, Outcomes).

**D2. Unverified controller behaviours** (S8; `RSI_Probe_Calibration_CONTRACT.md`, "Not yet verified on the controller"):
- that the PTP velocity and `$ACC_AXIS` = 20 % are kept through the RSI calls;
- the 2 mm of search beyond the nominal contact;
- the braking distance after ExitMoveCorr at 1 mm/s;
- a missed search and its `LIN SearchStart` exit.

Each needs its own decision and authorization by the user, and the missed search needs a deliberately displaced sphere or path. Record any finding in the contract's "Verified on the controller".

### Session E: agent, records

1. Tick each source plan's Progress item with date, session folder, file names and numbers (table in Context and Orientation).
2. Write plan 06's Outcomes if Session A did not.
3. Add new controller facts to the matching contract's "Verified on the controller" list.
4. Run `git diff --check`.
5. Fill this plan's Progress and Outcomes.


## Concrete Steps

From the repository root in Git Bash:

    git status --short
    ls records | tail
    python -I python/calibration/calibration_fit.py records/<folder>
    python -I python/rsi/rsi_packet_timing.py records/<folder>
    git diff --check

Counting the touch file (expected `158 True` after a full session, `3 True` after B1):

    python -I -c "import csv,sys; t=[int(r['touch']) for r in csv.DictReader(open(sys.argv[1]), delimiter=';')]; print(len(t), t==sorted(set(t)))" records/<folder>/touches.csv


## Validation and Acceptance

**B1 (StepLimit 10)**:
- Start Calibration starts program 1. The status bar shows "Calibration run: 0 % · 0/158 touches", then 1, 2 and 3 touches.
- The robot performs touches 1–3, each stopping on the probe, and ends with `LIN SearchStart` near the sphere.
- When the program finishes, the "Calibration failed" dialog (OK only) shows "3 of 158 touches detected; the calibration needs all of them."
- RSI Sampling before the run added nothing to `records/`; after the run exactly one new folder `records/<start>_calibration_failed` appears, its name the local time of Start Calibration (S9).
- In it: `session.json` with `"outcome": "failed"`, the error above, no "result"; `touches.csv` with 3 rows, touch numbers 1–3; no `residual_sets.csv`; `plan.csv` and `calibration_data.txt`; `rsi.csv` and `rsi_timing.csv` with equal row counts; `plc.csv` starting with a `state` row and containing `run 1` (S4, S9).
- The console prints only that run error ("Probe calibration run: …") and no `[ERROR] Records:` line.
- Plan 06 calibration part: the robot reaches the same entry pose as in earlier sessions.

**B2 (full session)**:
- Progress rises to 100 % (158/158).
- The separate UI check covered progress through Passive STOP and Resume.
- At the end the "Calibration complete" window lists the rows (values near the reference magnitudes) and asks the Reassign question. Cancel closes it and changes nothing: the measuring TCP, `primary` in `settings/probe_calibration.json` and the sphere are unchanged.
- `lastCalibration` in the settings file now holds this run, its `id` equal to the new folder name `records/<start>_calibration`.
- With nothing selected, the Properties panel shows the new "Last calibration" rows.
- `touches.csv` has 158 rows; `session.json` has `"outcome": "ok"`, the "result" and the "rsi" summary (about 132,800 requests); `residual_sets.csv` has 36 rows.
- `calibration_fit.py` on the folder prints "differences to the stored result: none".
- The console printed nothing from the run.
- `rsi.delayed` and `rsi.unanswered` in `session.json`, and the rejected replies of `rsi_packet_timing.py`, are not larger than in earlier sessions (2026-11-20 16:58: 2 and 2; 2026-11-18 19:00: 140 delayed, 33 rejected).

**B3 (Reassign)**:
- The Measuring End Effector panel shows the run's ball centre.
- In the settings file, `primary` holds the run's values with `source` = the run's folder name, not "manual".
- In Calibration the stand and sphere move to the new sphere centre (Properties of the sphere: "Center").
- The calibration path and `C:/RoboCrapJobs/calibration_data.txt` are gone, and the status bar asks for Generate Path.

**C (chamfer, T1)**:
- The program selects and starts without an error message, and reaches HOME, PsIn and LeadStart as the animation shows.
- The correction stream completes with the Result handshake and the program returns through LeadEnd and PsOut.
- Chamfer RSI Sampling added nothing to `records/`; after the run one folder `records/<start>_chamfer` (outcome ok) holds `session.json`, `plan.csv`, `shared_data.txt`, `rsi.csv`, `rsi_timing.csv` and `plc.csv`; `rsi_packet_timing.py` on it reports no rejected replies beyond those of earlier sessions (S9).
- C0 (optional): the completed test LIN gives `records/<start>_test-lin`, the run with the released enabling switch `records/<start>_test-lin_failed` (S9).

**D1**: a short note in this plan's Surprises naming where the delay arises (below or above the driver), with the session folder and the tool files.

**Plan done**: every row of the source table in Context and Orientation is ticked in its source plan or closed with a reason here, and `git diff --check` is clean.


## Idempotence and Recovery

Every session can be repeated; every run writes its own session folder, also a rerun of the same Sampling. After any robot run that ends near the sphere: T1, jog clear, CELL BCO run to HOME, AUT EXT. A Reassign is undone by restoring `settings/probe_calibration.json`. If the console shows `RSITimeout` or late packets abort a run, check the PC power settings ("Power settings for RSI") and that the ASIX adapter carries RSI, then repeat the session.


## Artifacts and Notes

Error texts the checks expect, from the code:
- `src/trajectories/probe/probecalibrationfit.cpp`: "%1 of %2 touches detected; the calibration needs all of them."
- `src/application/scene/scenemachiningpreparation.cpp`: "Program 1 did not start: %1.", "PLC disconnected during the calibration run.", and from `saveFile` "Cannot create %1." / "Cannot save %1: %2".


## Interfaces and Dependencies

No code, library or build change. Tools:
- `python/calibration/calibration_fit.py` and `python/rsi/rsi_packet_timing.py` (numpy, standard library);
- for D1 only: `pktmon` (Windows), Wireshark/Npcap and LatencyMon.


Revision note (2026-11-21): first version, collected from the open Progress items and "Remaining" notes of every plan in `plans/` and `plans/review_2026-11-20_0cd8996/`.
