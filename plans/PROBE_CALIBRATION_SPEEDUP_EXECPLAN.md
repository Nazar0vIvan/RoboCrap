# Faster and checked probe-sphere calibration: offline analysis, gauge block, transfer limits, smooth pre-approach

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan. First, the implementing agent never runs a build, CMake configure, QML lint, the application or a test unless the user asks for that run (`AGENTS.md`, "Validation and handoff"); where a milestone names a build or a run, the agent records it as "not run (user's)" in `Progress` and continues with what does not depend on it. The agent may run the Python analysis scripts in `python/` on recorded files, always as `python -I`. Second, the agent never moves the robot, never connects to PLC, RSI or force-sensor hardware, never starts a KRL program and never changes controller settings; every robot session in this plan is run by the user. Third, no git staging, commits, `git rm` or `git mv` unless the user asks, and no proposal to commit. Fourth, no project tests or test harnesses. Fifth, every C++ line obeys `AGENTS.md`, in particular "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters), "Mandatory code reduction" and "Mandatory interface stability"; before each logical code change re-read those sections, and before a new helper, class or field record its requirement and why reuse fails in at most three sentences in the Decision Log.


## Purpose / Big Picture

The probe calibration measures where the centre of the WP-500 ruby ball sits relative to the robot flange by touching a fixed 25 mm calibration sphere 158 times under RSI (the KUKA RobotSensorInterface, which lets the PC stream position corrections every 4 ms). A full session takes about 16 minutes 16 seconds from the first to the last touch, and its accuracy figure χ95 (the 95 % quantile of the absolute verification residuals, about 0.11 mm) is 7 to 11 times larger than the touch repeatability, so most of the error is systematic and not yet explained. The effective ball radius that part measurement will use, r_eff = ρ − 12.5 mm (ρ: the fitted contact distance, 12.5 mm: the nominal sphere radius), cannot be checked by the sphere at all, because the sphere's own radius is not certified.

After this plan the user can: read, from the existing session records, which part of the error comes from the probe's direction-dependent trigger travel and which plan reductions would cost no accuracy (Milestone 1); add a 22 mm gauge block to the calibration session and get the effective ball radius from a certified length instead of the uncertified sphere (Milestone 2); run the session with transfer moves at 15 mm/s and 100 mm/s² instead of 10 mm/s and 20 mm/s² (Milestone 3); and run every search with a smooth 2 mm pre-approach inside the RSI correction stream, so that only 1 mm is travelled at the slow search speed (Milestone 4). The expected time from the first to the last touch falls from about 976 s to about 528 s, plus about 3 minutes for the gauge block when it is included. Milestone 5 records, from the evidence of Milestones 1–4, whether a shorter calibration plan, a verification-only check before work, or a denser roll ring is justified; implementing those is left to a follow-up plan.

The proposals, their derivations and the numbers quoted here are in `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_SPEEDUP_RU.md` (Russian; "the speedup document"). This plan repeats everything an implementer needs.


## Progress

- [x] (2026-11-21) Speedup document updated with the user's decisions of 2026-11-21: gauge block 22 mm and its touch plan (section 4.2), transfer limits 15 mm/s and 100 mm/s² (4.3), smooth pre-approach and stream checks, start-shell gap kept at 3 mm, search speed kept at 1 mm/s, POSCORR limits large (4.4), the 3ψ blind spot of the calibration plan (4.1), time estimates (4.7).
- [x] (2026-11-21) Plan written after reading `PLANS.md`, `AGENTS.md`, the "Geometry and scene" and "Machining and playback" sections of `docs/code context/AGENT_CONTRACTS.md`, `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md`, the capture format in `controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md`, `src/trajectories/probe/{probetouchplan,probetouchmotion,probersistream,probecalibrationfit}.cpp` and their headers, `src/trajectories/motion/jointsegments.{h,cpp}` (`linearProfile`, `smoothProgress`), `src/application/scene/scenecalibration.cpp` (sphere file reader, Generate), `src/application/scene/scenegeometry.h` (`SceneSphere`, `CalibrationTable`), `src/application/scene/sceneobject.cpp` (sphere drawing), `src/geometry/include/geometry/plane.h`, `controller/krl/rsi/calibration/RSI_Probe_Calibration.{src,dat}`, `python/calibration/calibration_fit.py`, `python/rsi/rsi_packet_timing.py` and `settings/probe_calibration.json`.
- [ ] Milestone 1 (agent, at the equipment computer with `records/`): `python/calibration/calibration_analysis.py` with the subcommands `sessions`, `search` and later `gauge`; results recorded in Artifacts and Notes and in the speedup document.
- [ ] Milestone 2 (agent): gauge block in the sphere file, plan, motion, Sampling and drawing; contracts updated; compliance pass and counts.
- [ ] Milestone 2 (user): build; disconnected checks; gauge dimensions and jog points written; full session with the gauge in AUT EXT; agent runs `calibration_analysis.py gauge`.
- [ ] Milestone 3 (user): at least two full sessions with Generate transfer 15 mm/s and 100 mm/s²; agent evaluates them against the reference sessions.
- [ ] Milestone 4 (agent): smooth pre-approach in every search stream and the stream checks in Sampling; contracts and method document updated; compliance pass and counts.
- [ ] Milestone 4 (user): build; Sampling check; `StepLimit` = 10 run; at least two full sessions; agent runs `calibration_analysis.py search` and `sessions`.
- [ ] Milestone 5 (agent): decisions on a reduced plan, a verification-only check and a 12-roll ring recorded from the evidence; speedup document updated.


## Surprises & Discoveries

- Observation: two full sessions are missing from the evidence the speedup document was first written from: `calibration_20261118_164135_671` and `calibration_20261120_135817_781`. The second has χ95 0.1086 mm, s_rep 0.0104 mm and 25 of 36 step-K7 sets outside their bound, values outside the earlier six-session ranges.
  Evidence: `settings/probe_calibration.json` (`lastCalibration`) and `plans/PROBE_PRIMARY_CENTRES_EXECPLAN.md`, Concrete Steps.
- Observation: the calibration directions sample only the cos 3ψ part of a three-lobed trigger-travel pattern and the verification directions only the sin 3ψ part (ψ: roll of the probe about its stylus). Calibration rolls 0°, 60°, …, 300° give 3ψ = 0°, 180°, …, so sin 3ψ = 0; verification rolls 30°, 90°, …, 330° give 3ψ = 90°, 270°, …, so cos 3ψ = 0. A sin 3ψ lobing therefore cannot be fitted from calibration touches but appears in the verification residuals with alternating sign. Milestone 1 tests this.
  Evidence: `CalibrationDirections` and `VerificationDirections` in `src/trajectories/probe/probetouchplan.cpp`.
- Observation: the job file of the controller program holds at most 600 steps (`DECL INT StepKind[600]` and the other arrays in `controller/krl/rsi/calibration/RSI_Probe_Calibration.dat`; `READ_JOB` rejects a count above 600). The current plan uses 485. A pre-approach as a separate KRL LIN per touch would add 158 steps (643), which is why Milestone 4 puts it into the correction stream; the gauge passes of Milestone 2 add 106 steps (591).
  Evidence: `RSI_Probe_Calibration.dat`, `RSI_Probe_Calibration.src` lines 130–131, `StepLimit = 600` in `src/trajectories/probe/probersistream.cpp`.
- Observation: the KUKA manuals in the manuals folder document jerk limits only for spline motions (`$GEAR_JERK`, `$JERK` for SLIN/SCIRC, "KUKA System Variables 8.1, 8.2 and 8.3", sections 3.66 and 3.87). How conventional LIN and CIRC shape the acceleration edges is not documented; the transfer times in this plan use the application's trapezoidal model and are estimates.


## Decision Log

- Decision: transfer limits for the first comparison sessions are 15 mm/s and 100 mm/s².
  Rationale: with v²/a = 15²/100 = 2.25 mm below the shortest move (the 3 mm retract), every LIN and CIRC has a constant-speed section, so the acceleration never jumps from +a to −a; the moves within the first-to-last-touch interval shrink from 379 s to 197 s. 20 mm/s and 200 mm/s² (144 s) is the next step only if the indicators do not change.
  Date/Author: 2026-11-21, user (values), agent (rationale).
- Decision: the PTP velocity `PtpVelAuto` stays 5 %, the start-shell gap stays 3 mm, the search speed stays 1 mm/s.
  Rationale: user decisions of 2026-11-21; the search speed is the same in calibration, verification and part measurement, because the overshoot during the signal delay is part of the fitted contact distance.
  Date/Author: 2026-11-21, user.
- Decision: the pre-approach is part of each search's RSI correction stream, with the smooth profile x(t) = v_s·t + (L_pre − v_s·T)·smoothProgress(t/T).
  Rationale: a separate KRL LIN would exceed the 600 job steps and would run without the RSI STOP object; the stream keeps 485 steps and the STOP object active. `smoothProgress` (`src/trajectories/motion/jointsegments.h`) already exists, starts and ends with zero acceleration, and costs about 7 s per session against a triangular profile.
  Date/Author: 2026-11-21, user (smooth), agent (stream).
- Decision: the gauge block is touched in the same session, after the verification passes, in G2 and then G1, through the same KRL program and job format; its evaluation is offline in Python.
  Rationale: the same probe state as the calibration; no controller change (the job format already has LIN and Search steps and no touch-number limit, `RSI_Probe_Calibration.src`); part measurement, which would consume the effective radius, does not exist yet, so a result window in the application has no consumer. The gauge passes are present only when the sphere file has a `gauge` object, so no UI switch is needed.
  Date/Author: 2026-11-21, agent; ordering "gauge before refining the radius" from the user.
- Decision: the gauge block is described by an optional `gauge` object in the existing sphere file, `resources/deploy/config/calibration_sphere.json`, read by the existing reader.
  Rationale: the file already holds the fixture (sphere, strut, table) and its rough jog points; one reader, one replacement path for the scene object. A separate file would duplicate the reader, the error path and the replacement rule.
  Date/Author: 2026-11-21, agent.
- Decision: the gauge clearance in the motion check models the block as a box from its top face down to the table top.
  Rationale: the holder is not modelled; the user keeps it inside the block's footprint above 17 mm below the top face, and the ball never goes lower than 13.5 mm below the top face, so the box also covers the holder in the region the ball can reach.
  Date/Author: 2026-11-21, agent.
- Decision: the gauge result counts as significant when |ΔL| > 3 × the standard deviation of the measured plane distance, the same three-sigma rule as the step-K7 bound 3·s_rep/√m.
  Rationale: one rule for "systematic, not noise" throughout the method; the speedup document said only "larger than its random error".
  Date/Author: 2026-11-21, agent.
- Decision: the animation keeps modelling the search as a constant-speed LIN; Sampling checks each stream instead.
  Rationale: the pre-approach runs 2 mm of the same line as the retract LIN, in the opposite direction and below the retract's speed (11.0 mm/s against up to 15 mm/s), and the retract's joint speeds are checked by the motion check; an animation phase for the pre-approach would break the one-to-one pairing of animation phases and job steps that Sampling relies on. The POSCORR limits in the RSI context are set very large (user), so they would not catch a faulty stream; the Sampling check does.
  Date/Author: 2026-11-21, agent; POSCORR limits from the user.


## Outcomes & Retrospective

Nothing implemented yet.


## Context and Orientation

The repository is a Windows Qt Quick application in C++17 (Qt ≥ 6.8, CMake, Eigen). Paths below are relative to the repository root, `D:\Qt\QtProjects\RoboCrap` or `E:\Qt\QtProjects\RoboCrap` depending on the computer; the session records (`records/`) exist only on the equipment computer.

Terms used throughout. A touch is one approach of the probe to a surface until the probe's trigger signal stops the robot. The probe WP-500 has a 30 mm stylus with a ruby ball of 3 mm diameter (ball radius r_b = 1.5 mm). A search is the slow constant-speed part of a touch (search speed v_s = 1 mm/s) during which the controller's RSI STOP object halts the motion at the rising edge of the probe input `$IN[8]`. A correction stream is the list of per-cycle position increments (one per 4 ms RSI cycle, X/Y/Z in WORLD, mm) that the PC sends during one search. The start shell is the sphere of radius 17 mm around the calibration sphere centre (contact distance L = 14 mm plus a 3 mm gap) on which the probe moves between touches with CIRC arcs. The waiting point is where a pre-approach hands over to the search. G1, G2, G3 are the three fixed probe orientations (KUKA A, B, C = 180, 0, 180: stylus down; 0, 90, 0: stylus along +X; 0, 0, 180: stylus down turned 180° about it). τ (tilt) is the angle between the stylus and the approach direction, ψ (roll) the rotation of the probe frame about the stylus; the approach direction in the probe frame {P} is (−sin τ cos ψ, sin τ sin ψ, cos τ). Class v holds G1 and G3 (stylus down), class h holds G2; each class has its own contact distance ρ_v, ρ_h. Step K7 of the method (`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md`, section 6.3) groups the calibration residuals by group and tilt ring and by group and roll (36 sets) and flags sets whose mean exceeds 3·s_rep/√m (s_rep: repeatability from the twice-touched directions, m: touches in the set). χ95 is the 95 % quantile (value number ceil(0.95·m) of the sorted absolute values) of the verification residuals.

How a calibration session works today. In the application's Measuring mode, Calibration submode, Generate (`SceneCalibration::generate` in `src/application/scene/scenecalibration.cpp`) builds `ProbeTouchPlanParameters` (sphere centre, nominal radii, strut) and runs `createProbeTouchPlan` (`src/trajectories/probe/probetouchplan.cpp`) and `ProbeTouchMotion::create` (`src/trajectories/probe/probetouchmotion.cpp`) in a worker. The plan has 8 passes (calibration G1, G2, G3, G3, G2, G1 on 19 directions, the second three reversed; verification G1 on 25 and G2 on 19 directions), 158 touches. `probeMotionSteps` turns the plan into steps: per pass in a new group a LIN retreat of the previous pass, a PTP to the pass entry (60 mm out along the first normal and 80 mm up) and a LIN descend to the first start; per touch a CIRC transfer on the start shell (not for the first touch), an Approach (the search) and a LIN retract to the start. `TouchMotionBuilder` (same file) times every step with `MotionBuilder`; LIN and CIRC use the Generate transfer limits (`ChamferTimingParameters::transferFeed` and `transferAcceleration`, default 10 mm/s and 20 mm/s², set in the Machining panel as `$VEL.CP` and `$ACC.CP`), the Approach runs at the constant search feed. `clearanceError` rejects any stored point whose ball is less than 3 mm from the strut, or less than 3 mm outside the contact shell except during Approach and Retract. RSI Sampling (`SceneMachiningPreparation::prepareCalibration` in `src/application/scene/scenemachiningpreparation.cpp`, which calls `sampleProbeCalibrationRsi` in `src/trajectories/probe/probersistream.cpp`) turns each step into a job step (PTP, LIN, CIRC with an auxiliary point on the start shell, Search, HOME), builds one correction stream per touch (`searchStream`: 1250 increments of 0.004 mm along the approach direction, 5 mm, then 50 zero increments), writes the plan file `records/<id>/rsi_plan_<id>.csv` (one row per touch: touch, search_step, purpose, group, repetition, pass_touch, tilt, roll, normal_X/Y/Z, start and contact poses, start S/T) and the job file `C:\RoboCrapJobs\calibration_data.txt`, read by the KRL program `RSI_Probe_Calibration` (`controller/krl/rsi/calibration/`). The user starts the program through the PLC (AUT EXT). Each detection's joint angles (AIPos of the first packet with `Touch` 1) go to `records/<id>/calibration_touches_<id>.csv`; after the run `fitProbeCalibration` (`src/trajectories/probe/probecalibrationfit.cpp`) fits the ball centre, the sphere centre and ρ_v, ρ_h from the calibration touches and χ95 from the verification touches and writes `records/<id>/calibration_result_<id>.csv`. `python/calibration/calibration_fit.py <records/<id>>` repeats that fit offline and matches the application to six decimals. Every RSI packet of the run is in `records/<id>/rsi_packets_<id>.csv` (decoded columns including `ipoc`, `touch_index`, `touch`, `start` and the joint angles `a1`…`a6` on request rows; captures before 2026-11-18 are `_partN` files with Base64 XML; `python/rsi/rsi_packet_timing.py` reads both formats).

The sphere file `resources/deploy/config/calibration_sphere.json` (deployed to `<build>/resources/json/calibration_sphere.json` and read there at every entry into the Calibration submode by `readSphereFile` in `scenecalibration.cpp`) holds version 1, `sphereDiameter`, `strutDiameter`, `strutRecess`, the `table` object (lengthX, widthY, sphereFromMinusX, sphereFromMinusY, sphereAboveTop) and `roughPoints` (X, Y, Z of the ruby ball centre at ten jogged touches, with the smartPAD's A, B, C, S, T). `SceneSphere` (`src/application/scene/scenegeometry.h`) carries these values; `sceneobject.cpp` draws the sphere, the table top (a `DrawingPlane`) and the stand as decorative parts. `settings/probe_calibration.json` has no `primary` object, so every recorded session was planned around the rough sphere centre from the jog touches.

Recorded full sessions (all 158 touches) on the equipment computer: `calibration_20261114_170013_165`, `calibration_20261117_113235_576`, `calibration_20261117_121258_825`, `calibration_20261117_124928_908`, `calibration_20261117_132108_323`, `calibration_20261118_113045_223`, `calibration_20261118_164135_671`, `calibration_20261120_135817_781`. `calibration_20261117_130745_826` stopped after 18 touches and is not used.


## Plan of Work

Milestone 1, offline analysis (agent, Python only, at the equipment computer). Create `python/calibration/calibration_analysis.py`. It imports the fit of `calibration_fit.py` and the packet reader of `python/rsi/rsi_packet_timing.py` by inserting their directories into `sys.path` at the top of the script (explicit repository paths; `python -I` does not add the script's own directory), so the fit stays one copy. Extend `read_packets` in `rsi_packet_timing.py` so that every request dict also carries `"joints"`: the six AIPos angles (decoded captures: columns `a1`…`a6`; Base64 captures: the `<AIPos A1="…" … A6="…"/>` attributes, one more regular expression) and `"touch_index"`, `"touch"`, `"start"`; nothing else in that script changes. The analysis script has three subcommands. `sessions <folder> [<folder> …]` reads each session with `calibration_fit.read_touches`, fits it with `calibration_fit.solve` and prints, per session: χ95 overall and per group, mean |ε| and standard deviation of the verification residuals per group, the mean verification residual per group, the ball centre, ρ_v, ρ_h, s_rep, the 36 step-K7 sets, and the standard-deviation factors of the parameters (square roots of the diagonal of (JᵀJ)⁻¹, J the 114 × 8 Jacobian at the estimate, rows ∂ε_i/∂x = [e_srᵀR_i, −e_srᵀ, −1 for the touch's class] with e_sr the unit vector from the sphere centre to the calculated ball centre and R_i the flange rotation); then the same for the two subsets "passes 1–3" (calibration rows with repetition 1) and "passes 4–6" (repetition 2), each with all 44 verification touches; then the "other-session" χ95 for every ordered pair (A, B), A ≠ B (the verification touches of B with the ball centre and ρ_v, ρ_h of A, the sphere centre refitted from B's verification touches), 56 values for eight sessions; then the lobing test per session and group: twice a_s(τ) = mean verification residual at ψ ∈ {30°, 150°, 270°} minus mean at ψ ∈ {90°, 210°, 330°} for each verification ring τ ∈ {15°, 45°, 75°, 90°}, and twice a_c = mean calibration residual of ring 30° at ψ ∈ {0°, 120°, 240°} minus mean at ψ ∈ {60°, 180°, 300°}, each with its standard error from s_rep. `search <folder>` reads the plan, the touch file and the capture, and for every touch prints the number of 4 ms cycles from the search start (first request with `start` 1 and that `touch_index`) to the detection (first request with `touch` 1), the TCP speed along the approach direction from the forward kinematics of consecutive requests' joint angles with the session's fitted ball centre (displacement difference divided by the IPOC difference), the distance travelled until that speed first stays within ±5 % of v_s for 25 cycles, the speed in the detection request and the one before, and the peak-to-peak speed in the 50 cycles before detection; it also prints the session time between the first and the last detection from their IPOC. `gauge <folder>` is added in Milestone 2. Run `sessions` on the eight folders and `search` on each, put the summary into Artifacts and Notes and into sections 1.1 and 4.1 of the speedup document (replace "вычислено для пяти сеансов" and "для четырёх сеансов" by the eight-session values).

Milestone 2, the gauge block in the calibration session (agent code, then user). The block is the user's parallelepiped gauge block of nominal length 22 mm between its two measuring faces. The user mounts it on the table in the zone of the sphere touches with the measuring faces across the WORLD y axis, its face width b along x and its face height h vertical, and with the holder inside the block's footprint above the level 17 mm below the top face. Face A is the measuring face at smaller y, face B the one at larger y.

In the sphere file, add an optional object (example values; the user writes the real ones):

    "gauge": {
      "length": 22.0, "width": 9.0, "height": 35.0,
      "roughPoints": [
        {"face": "A", "X": 0.0, "Y": 0.0, "Z": 0.0, "A": 180.0, "B": 0.0, "C": 180.0, "S": 2, "T": 10},
        ... three points with face "A", three with "B", one with "top", one with "sideMinus", one with "sidePlus"
      ]
    }

`length` is L_g, `width` is b, `height` is h (mm). The rough points are jogged touches in T1 with TOOL_DATA[5] and the stylus down (A 180, B 0, C 180), each read from the smartPAD when the probe LED lights, as for the sphere: three spread points on face A (moving +y), three on face B (moving −y), one on the top face (moving −z), one on each side face (moving +x onto the side at smaller x: "sideMinus"; moving −x: "sidePlus"). In `readSphereFile` (`scenecalibration.cpp`), parse the object when present, require finite positive length, width, height, h ≥ 20 mm and b ≥ 6 mm, exactly three A and three B points and one of each other label with finite X, Y, Z, and compute the rough block pose: face normals nA, nB with `Plane::fromPoints` (`src/geometry/include/geometry/plane.h`; three points give an exact plane; its normal has a positive largest component, which is +y for these faces), n = normalized nA + nB; reject an angle between nA and nB above 2° and |n·ŷ| below cos 10°; up = normalized (ẑ − (ẑ·n)n), w = n × up (≈ +x); dA, dB = mean n·p of the A and B points; reject |(dB − dA) − (L_g + 2 r_b)| > 1.0 mm; top t = up·p_top − r_b; side centre s = (w·p_sideMinus + w·p_sidePlus)/2, reject |(w·p_sidePlus − w·p_sideMinus) − (b + 2 r_b)| > 1.0 mm; the top-face centre c = ((dA + dB)/2)·n + s·w + t·up. Errors read like the existing ones ("Calibration sphere file …: gauge …"). Store the input values and the derived c, n, w, up in a new `CalibrationGauge` value inside `SceneSphere` (optional), include it in `operator==`, and draw the block's top and four side faces as decorative `DrawingPlane` parts beside the table in `sceneobject.cpp`.

In `probetouchplan.h`, add `ProbeTouchPurpose::Gauge` and a `ProbeGaugeBlock` value (top-face centre, the unit axes n, w, up, length, width, height and `depth`, the distance from the top face down to the table top, computed in `generate` from the sphere's table as t − (sphere centre z − sphereAboveTop)) with a member `double clearance(const V3d& ball, double ballRadius) const` returning the distance from the ball centre to the box (|n-coordinate| ≤ L_g/2, |w-coordinate| ≤ b/2, up-coordinate between −depth and 0, relative to c) minus the ball radius; add `std::optional<ProbeGaugeBlock> gauge` to `ProbeTouchPlanParameters`. In `createProbeTouchPlan`, when the gauge is present, append eight gauge passes after the verification passes: group 2 then group 1, each with faces A, B, B reversed, A reversed (repetition 1, 2 per face). A face pass has four touches at the ball-centre contact points c ∓ (L_g/2 + r_b)·n + u·w − d·up (minus sign for face A) with (u, d) in the order (−u0, 5), (+u0, 5), (+u0, 12), (−u0, 12), u0 = b/2 − 2.5 mm, d in mm below the top face; the outward normal is −n for face A and +n for face B; the start is the contact moved 3 mm (`ProbeApproachGap`) along the outward normal; the orientation is the group's; tilt and roll are computed from the approach direction e = R_gᵀ(−outward normal) in {P}: τ = acos(e_z), ψ = atan2(e_y, −e_x) in degrees, normalized to [0°, 360°) (≈ 90° and 90° for face A, 90° and 270° for face B). The pass entry is the first start moved 20 mm along the outward normal and raised to 20 mm above the top face; the exit is the last start moved the same way. Reject a gauge whose face would put a contact closer than 2.5 mm to a face edge (that is b < 6 mm or h < 15 mm, already rejected by the reader) and any touch whose start or contact is closer than 3 mm to the strut, as for the sphere.

In `probetouchmotion.cpp`, `probeMotionSteps`: a pass starts with retreat, PTP to entry and descend when it is the first pass, when its group differs from the previous pass, when it is a gauge pass after a sphere pass, or when it is a gauge pass whose first touch's normal points against the previous pass's last normal (dot product < 0); otherwise as today (the step from calibration pass 6 to verification pass 7, both G1, keeps having no transfer, so the plan without a gauge stays at 485 steps) (transfer, omitted when the starts coincide). `TouchMotionBuilder::append`: the transfer is a CIRC on the start shell only for sphere passes; gauge transfers stay LIN. `clearanceError`: every point keeps ≥ 3 mm from the strut and, when the gauge is present, from the gauge box, except the Approach and Retract of gauge passes; every point keeps ≥ 3 mm outside the sphere's contact shell except the Approach and Retract of sphere passes. Add `gauge` to `ProbeClearances` (infinity without a gauge) and `minimumGaugeClearance()` to `ProbeTouchMotion`, and the gauge touch count and clearance to the Generate log line in `SceneCalibration::finishCalculation`.

In `probersistream.cpp`, a Transfer of a gauge pass is a LIN job step; the plan row's purpose is `gauge`; the plan file gets the header line `# gauge_length_mm,<L_g>` after `# plan_id,<id>` when the plan has gauge passes. `fitProbeCalibration` needs no change beyond accepting the new purpose: `fitTouches` already lists every Approach and requires every detection, and `selectTouches` keeps only calibration or verification touches. `calibration_fit.py` already filters by purpose and needs no change; confirm on the first gauge session that its output still equals the application's result file.

Add the subcommand `gauge <folder>` to `calibration_analysis.py`: read the touches and fit the sphere as `sessions` does; for each gauge group compute the ball centres of its 16 touches with the fitted ball centre, fit two parallel planes with a common normal (the normal is the eigenvector of the smallest eigenvalue of the pooled scatter matrix of the points minus their face mean), and print the plane distance L_c, its standard deviation σ_L = σ·√(1/8 + 1/8) (σ from the plane residuals with N − 4 degrees of freedom), r_eff,gauge = (L_c − L_g)/2, r_eff,sphere = ρ_o − 12.5 (ρ_o of the group's class), ΔL = L_c − 2·r_eff,sphere − L_g and "significant" when |ΔL| > 3·σ_L. L_g comes from the plan file's header line.

Update `docs/code context/AGENT_CONTRACTS.md` ("Geometry and scene": the optional `gauge` object of the sphere file and its checks; "Machining and playback": the gauge passes, the LIN gauge transfers, the gauge clearance, the purpose and the plan header) and `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md` (touch numbers up to 190, step count 591 with the gauge, purpose `gauge`, LIN transfers between gauge touches; no KRL change). Then perform the `AGENTS.md` compliance pass and record counts.

Milestone 3, transfer limits (user, no code). Set Generate transfer feed 15 mm/s and acceleration 100 mm/s² in the Machining panel, regenerate the calibration path, run at least two full sessions (with the gauge if Milestone 2 is done). The agent runs `calibration_analysis.py sessions` on the new folders together with the reference folders and applies the acceptance rule of Validation and Acceptance.

Milestone 4, smooth pre-approach (agent code, then user). In `probersistream.cpp`, `searchStream` gets the timing (`m_motion.robot().timing`: `transferFeed` v_tr, `transferAcceleration` a_tr) in addition to the normal and the search feed v_s. With L_pre = 2 mm and k = 10/√3, the pre-approach time is T = (−k·v_s + √(k²v_s² + 4·a_tr·k·L_pre))/(2·a_tr), rounded up to whole 4 ms cycles (N_pre = ceil(T/0.004)); the offset along the approach direction after cycle j (j = 1 … N_pre) is v_s·t + (L_pre − v_s·T)·smoothProgress(t/T) with t = 0.004·j, then the search continues at v_s for 3 mm (750 cycles at 1 mm/s), then 50 zeros; increments remain differences of consecutive six-decimal offsets, as now. If the peak speed v_s + 1.875·(L_pre − v_s·T)/T exceeds v_tr, increase N_pre one cycle at a time until it does not. Add the stream check to Sampling (a helper next to `searchStream`, returning an error text): the increments sum to 5 mm along the approach direction within 0.0001 mm; no increment is longer than v_tr·0.004 mm + 0.000001; no difference of consecutive increment lengths exceeds a_tr·0.004² mm + 0.000001, except the first increment (the jump to v_s, as today). With the defaults of Milestone 3 (a_tr 100 mm/s², v_tr 15 mm/s): T = 0.316 s (79 cycles), peak 11.0 mm/s, peak acceleration 97.4 mm/s², 879 entries per stream instead of 1300. Update `RSI_Probe_Calibration_CONTRACT.md` ("Each search starts …", the expected detection cycle: about 79 + 290 cycles), `AGENT_CONTRACTS.md` ("A search stream moves 5 mm …"), the method document (section 6.1 "Предподхода нет", step К3 of section 6.3, the search row of section 6.4) and the speedup document's status. Then the compliance pass and counts.

Milestone 5, decisions (agent). From Milestone 1 and the sessions of Milestones 3–4: if the passes-1–3 or passes-4–6 subset changes no decision indicator beyond the reference range, record "reduced plan supported" (the time estimate is in section 4.7 of the speedup document); if the other-session χ95 values stay within the full-session range, record "verification-only check before work supported" with its threshold (the largest other-session χ95); if 2a_s is larger than three of its standard errors in both G1 and G3 with the same sign, record "12-roll ring 30° needed" (rolls 0°, 30°, …, 330°, 36 more touches; the job would then exceed 600 steps with the gauge, so that follow-up must also enlarge the KRL arrays). Write these into the Decision Log and section 4 of the speedup document; implementation is a follow-up ExecPlan.


## Concrete Steps

All commands run in the repository root in Git Bash on the equipment computer (records exist only there).

Milestone 1:

    git status --short
    python -I python/calibration/calibration_fit.py records/calibration_20261118_113045_223
    python -I python/calibration/calibration_analysis.py sessions records/calibration_20261114_170013_165 records/calibration_20261117_113235_576 records/calibration_20261117_121258_825 records/calibration_20261117_124928_908 records/calibration_20261117_132108_323 records/calibration_20261118_113045_223 records/calibration_20261118_164135_671 records/calibration_20261120_135817_781
    python -I python/calibration/calibration_analysis.py search records/calibration_20261120_135817_781

The first command is a regression check of the shared fit: its rows must equal `records/calibration_20261118_113045_223/calibration_result_calibration_20261118_113045_223.csv` (ball centre 0.0228, −0.0554, 126.9679; χ95 0.1107; 23 of 36 sets outside). The `sessions` output for that folder must show the same values in its full-plan line.

Milestone 2, before any robot run, the disconnected software check (user builds; the agent never builds): copy `<build>/resources/json/calibration_sphere.json` aside, add a `gauge` object with test values to the deployed copy only (never to `resources/deploy/config/` with invented points), enter the Calibration submode and Generate. Remove the test object afterwards. Then the user writes the real dimensions and jog points into `resources/deploy/config/calibration_sphere.json`, rebuilds (deployment copies the file), generates, checks Dry Run, runs RSI Sampling and a full session in AUT EXT, and the agent runs:

    python -I python/calibration/calibration_fit.py records/<id>
    python -I python/calibration/calibration_analysis.py gauge records/<id>

Milestones 3 and 4 use the same `sessions` and `search` commands with the new folders appended.


## Validation and Acceptance

Milestone 1 is accepted when the regression check matches and the `sessions` and `search` outputs for all eight sessions are recorded. Expected orders of magnitude from the existing summaries: χ95 0.109–0.114 mm, s_rep 0.010–0.015 mm, 22–25 sets outside; the search output shows 765–876 cycles to detection.

Milestone 2, disconnected (user): with a valid test gauge the console shows "Probe calibration preparation: 114 calibration, 44 verification and 32 gauge touches, … minimum gauge clearance … mm"; the scene shows the block's faces near the table; Dry Run plays the gauge passes after the verification passes, entering face A and face B from above. An invalid gauge object (for example two "A" points) shows the sphere-file error in the status bar and no calibration sphere, as an invalid table does today. RSI Sampling writes `calibration_data.txt` with 591 steps and a plan file with 190 rows, the last 32 with purpose `gauge`, and the header line `# gauge_length_mm,22`. Without a `gauge` object everything is as before (158 touches, 485 steps). With the robot (user, explicit authorization): the session finishes, all 190 touches are detected, the application's result equals `calibration_fit.py`, and `calibration_analysis.py gauge` prints for each group L_c near 24.7 mm, σ_L of a few micrometres, r_eff,gauge and r_eff,sphere, ΔL and its significance. Record the values in Artifacts and Notes and in section 4.2 of the speedup document.

Milestone 3 and 4 acceptance rule for a change (speedup document, section 4.5): the change is rejected if, for χ95 or for the absolute mean verification residual of G1 or of G2, every session with the change exceeds the largest value of the same indicator over the reference sessions (the eight recorded ones plus any later full sessions with the unchanged plan and motion). Assuming exchangeable sessions, an ineffective change is rejected with probability 1/C(N_cur + N_new, N_new) per indicator, 1/45 for 8 + 2 sessions. Milestone 4 additionally requires: Sampling succeeds with 879 entries per stream at 15 mm/s and 100 mm/s² and the stream check passes; the `StepLimit` = 10 run (touches 1–3) stops on all three touches; in `search`, every detection lies more than 79 cycles after the search start plus the settling distance, and the speed in the detection request equals the reference sessions' within their spread; the first-to-last-detection time is near 528 s with transfers 15/100 (about 3 minutes more with the gauge).


## Idempotence and Recovery

The analysis scripts only read records and print; they can be rerun at will. The sphere-file `gauge` object is optional: removing it restores the previous plan exactly. Generate, Sampling and runs can be repeated; each Sampling makes a new record identifier. After any robot run that ends near the sphere or the block (a missed search, a `StepLimit` end), the user brings the robot back as the calibration contract requires: T1, jog clear, CELL BCO run to HOME, AUT EXT. If Milestone 4's stream turns out to misbehave on the controller, `searchStream` can be restored from git; the job format is unchanged in every milestone.


## Artifacts and Notes

Values from the summaries available at plan time (records not on this computer):

    calibration_20261117_113235_576: ball [0.0234; -0.0566; 126.9683], rho_v 13.8446, rho_h 13.8629, s_rep 0.0148, chi95 0.1120 (max 0.1846), 22/36 outside
    calibration_20261114_170013_165: ball [0.0242; -0.0556; 126.9675], rho_v 13.8426, rho_h 13.8604, s_rep 0.0107, chi95 0.1112 (max 0.1851), 23/36
    calibration_20261118_113045_223: ball [0.0228; -0.0554; 126.9679], rho_v 13.8449, rho_h 13.8572, s_rep 0.0107, chi95 0.1107, 23/36
    calibration_20261118_164135_671: ball [0.023743; -0.056159; 126.970980], rho_v 13.844765, rho_h 13.858389, s_rep 0.010241, chi95 0.111184
    calibration_20261120_135817_781: ball [0.021323; -0.055675; 126.971923], rho_v 13.842660, rho_h 13.854590, s_rep 0.010405, chi95 0.108647, 25/36

Time budget of the current session (speedup document, section 1.2): 974–978 s between the first and last detection; searches about 515 s; CIRC 188 s, retracts 122 s, exit and descent LIN at group changes 69 s (calculated with the trapezoidal profile); the rest about 82 s (PTP at 5 %, RSI on/off, step transitions).


## Interfaces and Dependencies

No new library. Python 3 with numpy, as `calibration_fit.py` already uses.

In `src/trajectories/include/trajectories/probetouchplan.h`, at the end of Milestone 2:

    enum class ProbeTouchPurpose { Calibration, Verification, Gauge };

    // Gauge block of the calibration fixture in BASE: top-face centre and unit axes (normal from
    // face A to face B, width axis, up), mm.
    struct ProbeGaugeBlock
    {
      V3d topCentre = V3d::Zero();
      V3d normal = V3d::UnitY();
      V3d widthAxis = V3d::UnitX();
      V3d up = V3d::UnitZ();
      double length = 0.0;
      double width = 0.0;
      double height = 0.0;
      double depth = 0.0; // Top face to the table top; the clearance box reaches down to it.

      // Distance from the ball surface to the box, mm; negative inside.
      double clearance(const V3d& ball, double ballRadius) const;
    };

    struct ProbeTouchPlanParameters
    {
      ... existing members ...
      std::optional<ProbeGaugeBlock> gauge;
    };

In `src/trajectories/include/trajectories/probetouchmotion.h`: `ProbeClearances::gauge` (double, infinity without a gauge) and `double ProbeTouchMotion::minimumGaugeClearance() const`.

In `src/application/scene/scenegeometry.h`: `struct CalibrationGauge` holding the file values (length, width, height, the rough points by label) and the derived `ProbeGaugeBlock` fields except `depth`; `std::optional<CalibrationGauge> gauge` in `SceneSphere` and in its `operator==`.

In `src/trajectories/probe/probersistream.cpp` (Milestone 4), internal functions only: `searchStream(const V3d& normal, double feed, const ChamferTimingParameters& timing)` and a stream check returning an empty string or an error text. No public header changes in Milestone 4.

In `python/rsi/rsi_packet_timing.py`: request dicts of `read_packets` gain `"joints"` (six floats or None), `"touch_index"`, `"touch"` and `"start"` (ints or None). In `python/calibration/calibration_analysis.py`: the subcommands `sessions`, `search` and `gauge` described in Plan of Work.


Revision note (2026-11-21): first version, written after the user's decisions on the gauge block (22 mm, dimensions to be written at the equipment computer, rough position by jog touches, gauge before refining the radius), transfer limits 15 mm/s and 100 mm/s², the smooth pre-approach and keeping PtpVelAuto, the 3 mm gap and the 1 mm/s search speed.
