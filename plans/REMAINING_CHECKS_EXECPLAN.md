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
- [ ] Session B1 (user, equipment): calibration run with `StepLimit` = 10.
- [ ] Session B2 (user, equipment): full calibration session, then the agent's offline check of its files.
- [ ] Session B3 (user, optional): Reassign after a full session.
- [ ] Session C (user, equipment): `RSI_Chamfer_Hole` in T1.
- [ ] Session D (user and agent, optional): D1 late RSI packets; D2 unverified controller behaviours.
- [ ] Session E (agent): source plans and contracts updated with the results; `git diff --check`.


## Surprises & Discoveries

(None yet.)


## Decision Log

- Decision: the checks are grouped by session, not by source plan.
  Rationale: one full calibration session (about 17 minutes) answers checks from five plans at once, and each robot preparation (HOME, CELL BCO run, AUT EXT) costs operator time.
  Date/Author: 2026-11-21, agent.
- Decision: two open items are closed by existing evidence instead of a new run (Session A3).
  - The Milestone 4 lint item of `PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md` is superseded: `all_qmllint` and `robocrap` passed with USE_HOT_RELOAD OFF after review plans 03 and 07 (2026-11-20 and 2026-11-21).
  - The "reproduction within 1e-4 mm" item of the same plan is met by `python/calibration/calibration_fit.py`. `RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md` records that it reproduced the application's result file of that plan's run, `calibration_20261118_113045_223`, line for line.
  Date/Author: 2026-11-21, agent.
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

Plans with nothing open: review 01 to 05, and `PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md` (complete 2026-11-14; its "open" topics, lost PLC change frames and the external Passive STOP acknowledgement, are design questions in `docs/code context/ARCHITECTURE.md` §7 and `docs/solutions/KUKA_PASSIVE_STOP_EXTERNAL_ACK.md`, not checks).

Terms and places:
- **Records** are in `records/<id>/` of the repository (`ROBOCRAP_RECORDS_DIR`, git-ignored). They exist only on the computer that ran the sessions. Check that `records/` exists before any offline step that reads it.
- **Calibration `<id>`** is `calibration_<UTC yyyyMMdd_HHmmss_zzz>` of the RSI Sampling. The folder holds:
  - `rsi_plan_<id>.csv` (Sampling);
  - `rsi_packets_<id>.csv` (capture);
  - `calibration_touches_<id>.csv` (every run with detections);
  - `calibration_result_<id>.csv` (successful fit only).
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
1. Set `StepLimit` = 0. Run RSI Sampling again: each Sampling makes a new `<id>`, and B1's job may be stale after the recovery.
2. Press Start Calibration.
3. Optional STOP/Resume check (S6): while a transfer runs, not during a search, press the smartPAD STOP key once. Acknowledge "KSS01370 Passive STOP" on the smartPAD, then press Resume in the PLC panel.
4. When "Calibration complete" opens, press **Cancel**.
5. Agent afterwards, offline:

       python -I python/calibration/calibration_fit.py records/<id of B2>

   Compare the output with `calibration_result_<id>.csv`.

**B3. Reassign** (S5; optional, a user decision, because it changes the primary ball and sphere centres that every later calibration path uses). After another full session (or B2 repeated), press **Reassign** instead of Cancel. To undo it, restore `settings/probe_calibration.json` from git or from the copy.

### Session C: user, `RSI_Chamfer_Hole` in T1 (S1 chamfer part)

1. Preparation: T1, robot at HOME, a test workpiece or free space at a safe staging distance, operator holding the enabling switch.
2. In RoboCrap, import the plane and cylinder from `data/probe_points/edge_cyl_plane/`, intersect them, Generate the chamfer path (Machining) and run RSI Sampling with the RSI socket bound.
3. Start `RSI_Chamfer_Hole` on the smartPAD.
4. The chamfer contract lists the restructured SRC's compilation and `$VEL.CP`/`$ACC.CP` kept through the RSI calls as "Not yet verified". Treat the first selection and start as that check: stop at once on any unexpected message or motion.

### Session D: optional investigations

**D1. Late RSI packets** (S7). In earlier sessions some 4 ms requests arrived 15–50 ms late, in bursts, while the application thread was idle; the cause is open (`RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md`, Surprises).
1. During a full calibration session (B2 can serve), record arrival at the adapter driver with `pktmon` or Wireshark/Npcap on `KRC4-Direct`, and record DPC/ISR latency with LatencyMon.
2. Agent afterwards: compare the driver timestamps with `pc_ns` in `rsi_packets_<id>.csv`, using `python -I python/rsi/rsi_packet_timing.py <capture>` and a scratchpad script.
3. Decide whether the delay lies below or above the network driver.

Keep the RSI adapter on the ASIX USB port: the Intel I219-V port held requests 2–16 ms and stopped runs with RSITimeout (review plan 02, Outcomes).

**D2. Unverified controller behaviours** (S8; `RSI_Probe_Calibration_CONTRACT.md`, "Not yet verified on the controller"):
- that the PTP velocity and `$ACC_AXIS` = 20 % are kept through the RSI calls;
- the 2 mm of search beyond the nominal contact;
- the braking distance after ExitMoveCorr at 1 mm/s;
- a missed search and its `LIN SearchStart` exit.

Each needs its own decision and authorization by the user, and the missed search needs a deliberately displaced sphere or path. Record any finding in the contract's "Verified on the controller".

### Session E: agent, records

1. Tick each source plan's Progress item with date, `<id>`, file names and numbers (table in Context and Orientation).
2. Write plan 06's Outcomes if Session A did not.
3. Add new controller facts to the matching contract's "Verified on the controller" list.
4. Run `git diff --check`.
5. Fill this plan's Progress and Outcomes.


## Concrete Steps

From the repository root in Git Bash:

    git status --short
    ls records | tail
    python -I python/calibration/calibration_fit.py records/<id>
    python -I python/rsi/rsi_packet_timing.py records/<id>/rsi_packets_<id>.csv
    git diff --check

Comparing the script output with a result file (ignore line endings):

    diff <(python -I python/calibration/calibration_fit.py records/<id> | tr -d '\r') <(tr -d '\r' < records/<id>/calibration_result_<id>.csv) && echo identical

Counting the touch file (expected `158 True` after a full session, `3 True` after B1):

    python -I -c "import csv,sys; r=[x for x in csv.reader(open(sys.argv[1])) if x and not x[0].startswith('#')][1:]; t=[int(x[0]) for x in r]; print(len(t), t==sorted(set(t)))" records/<id>/calibration_touches_<id>.csv


## Validation and Acceptance

**B1 (StepLimit 10)**:
- Start Calibration starts program 1. The status bar shows "Calibration run: 0 % · 0/158 touches", then 1, 2 and 3 touches.
- The robot performs touches 1–3, each stopping on the probe, and ends with `LIN SearchStart` near the sphere.
- When the program finishes, the "Calibration failed" dialog (OK only) shows "3 of 158 touches detected; the calibration needs all of them."
- `records/<id>/calibration_touches_<id>.csv` exists with 3 rows, touch numbers 1–3, written through `saveFile`.
- No `calibration_result_<id>.csv` exists.
- The console prints only that run error ("Probe calibration run: …") and no save or "saved RSI diagnostic" lines.
- `rsi_packets_<id>.csv` is one file.
- Plan 06 calibration part: the robot reaches the same entry pose as in earlier sessions.

**B2 (full session)**:
- Progress rises to 100 % (158/158).
- With the optional STOP: the percentage stays while the program is interrupted, and Resume continues from there.
- At the end the "Calibration complete" window lists the rows (values near the reference magnitudes) and asks the Reassign question. Cancel closes it and changes nothing: the measuring TCP, `primary` in `settings/probe_calibration.json` and the sphere are unchanged.
- `lastCalibration` in the settings file now holds this run.
- With nothing selected, the Properties panel shows the new "Last calibration" rows.
- `calibration_touches_<id>.csv` has 158 rows (`158 True`).
- `calibration_result_<id>.csv` exists, and `calibration_fit.py` reproduces it (`identical`).
- The console printed nothing from the run.
- `rsi_packets_<id>.csv` shows no more rejected replies than earlier sessions (`rsi_packet_timing.py` report).

**B3 (Reassign)**:
- The Measuring End Effector panel shows the run's ball centre.
- In the settings file, `primary` holds the run's values with `source` = the run's `<id>`, not "manual".
- In Calibration the stand and sphere move to the new sphere centre (Properties of the sphere: "Center").
- The calibration path and `C:/RoboCrapJobs/calibration_data.txt` are gone, and the status bar asks for Generate Path.

**C (chamfer, T1)**:
- The program selects and starts without an error message, and reaches HOME, PsIn and LeadStart as the animation shows.
- The correction stream completes with the Result handshake and the program returns through LeadEnd and PsOut.
- `records/<id>/rsi_packets_<id>.csv` shows no rejected replies beyond those of earlier sessions.

**D1**: a short note in this plan's Surprises naming where the delay arises (below or above the driver), with the session `<id>` and the tool files.

**Plan done**: every row of the source table in Context and Orientation is ticked in its source plan or closed with a reason here, and `git diff --check` is clean.


## Idempotence and Recovery

Every session can be repeated; each RSI Sampling makes a new `<id>`, and a rerun with the same Sampling appends to the capture and replaces the touch and result files. After any robot run that ends near the sphere: T1, jog clear, CELL BCO run to HOME, AUT EXT. A Reassign is undone by restoring `settings/probe_calibration.json`. If the console shows `RSITimeout` or late packets abort a run, check the PC power settings ("Power settings for RSI") and that the ASIX adapter carries RSI, then repeat the session.


## Artifacts and Notes

Error texts the checks expect, from the code:
- `src/trajectories/probe/probecalibrationfit.cpp`: "%1 of %2 touches detected; the calibration needs all of them."
- `src/application/scene/scenemachiningpreparation.cpp`: "Program 1 did not start: %1.", "PLC disconnected during the calibration run.", and from `saveFile` "Cannot create %1." / "Cannot save %1: %2".


## Interfaces and Dependencies

No code, library or build change. Tools:
- `python/calibration/calibration_fit.py` and `python/rsi/rsi_packet_timing.py` (numpy, standard library);
- for D1 only: `pktmon` (Windows), Wireshark/Npcap and LatencyMon.


Revision note (2026-11-21): first version, collected from the open Progress items and "Remaining" notes of every plan in `plans/` and `plans/review_2026-11-20_0cd8996/`.
