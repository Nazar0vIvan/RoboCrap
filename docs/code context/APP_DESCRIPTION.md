# RoboCrap: application description

RoboCrap is a Windows desktop application for preparing and visually checking robotic edge
machining with a KUKA KR 10 R1420 on a KR C4 controller (KSS 8.3, RobotSensorInterface (RSI) 3.3).
The current implementation supports chamfering the edge of a hole; other edge geometries are part
of the broader intended scope. Hardware and design notes are in [ARCHITECTURE.md](ARCHITECTURE.md);
the controller/PC protocol is in
[RSI_Chamfer_Hole_CONTRACT.md](../../controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md).

Terms used below: TCP is the tool center point. PTP is a synchronized joint motion to a target;
LIN is a straight Cartesian motion; CIRC is a circular-arc Cartesian motion. P_s is the staging
point on the hole axis, outside the hole.

## Fixed equipment and editable settings

- The robot model and both end-effector models (measuring tool MEE and spindle tool SEE) are fixed
  by the application. They cannot be loaded or replaced from the UI.
- Spindle EE (Properties panel, Machining mode): the ER collet pose relative to the flange is
  displayed read-only. The TCP pose relative to the ER collet is edited and committed with Apply.
  Changing it invalidates every generated path until it is regenerated.
- Measuring EE (Measuring mode): name, ruby ball diameter and stylus length are displayed
  read-only. The ruby ball centre relative to the flange (default: controller TOOL_DATA[5]) is
  edited and committed with Apply; its X, Y, Z are saved in `settings/probe_calibration.json` in
  the repository (A, B, C are not) and loaded at the next start, and the probe model is drawn with
  its ball at this point. Changing it removes the calibration path.
- Only workpiece surfaces are imported, as JSON point files measured with a touch probe.

## Workflow

1. **Prepare, animate and run the probe calibration (Measuring mode, Calibration submode).**
   Entering the submode reads `resources/json/calibration_sphere.json` beside the executable
   (copied there by the build only when absent): the 25 mm sphere, the strut that holds it and ten
   ruby ball centres recorded by jogging onto the sphere. The fitted sphere is shown as the
   ball-centre sphere and the compensated sphere; a sphere centre made primary by *Reassign*
   (below) replaces the fitted centre, and the table and stand move with it. Every other scene
   object is hidden until the submode is left. *Generate Path* computes a fixed motion from HOME back to HOME: 114
   calibration touches in six passes and then 44 verification touches in two passes. Each pass
   keeps one probe orientation: G1 stylus down, G2 stylus along +X (as at HOME), G3 stylus down
   with the flange turned 180°; calibration runs G1, G2, G3, G3, G2, G1 and verification G1, G2.
   Each touch is a 3 mm LIN approach at a constant 1 mm/s and a LIN retract at the transfer
   speed (10 mm/s, 20 mm/s²); touches are joined by CIRC
   transfers 3 mm above the sphere surface, and passes of different orientations by a retreat, a
   PTP turn of the probe and a descent. *Dry Run* animates it; editing the file and entering the
   submode again uses the new sphere.

   With the RSI socket bound, *RSI Sampling* prepares one search stream per touch (5 mm at the
   configured search feed), keeps the touch plan in memory and exports `C:/RoboCrapJobs/calibration_data.txt`
   (485 KRL steps for the 158-touch plan). *Start Calibration* opens a record and requests program 1
   in Automatic External. After a Passive STOP, acknowledge the message on the smartPAD and use
   *Resume* in the calibration toolbar. Leaving AUT EXT ends the run. `RSI_Probe_Calibration`
   executes the steps; each search runs under RSI and stops on the probe signal `$IN[8]`. The status
   bar shows the share of touches detected so far and, at its left, whether the controller is in
   AUT EXT. When the program finishes, the application fits the ball centre in the flange frame, the
   sphere centre, the two contact distances (stylus down, stylus horizontal) and the 95 % quantile
   of the verification residuals from the actual joint angles of every touch, shows them in the window
   “Calibration complete”, records the result in `session.json` and saves the last result in
   `settings/probe_calibration.json`. *Reassign* makes the fitted centres and contact distances
   primary and requires generating the path again. *Cancel* keeps the previous primary values.
   With nothing selected, the Properties panel shows the last result. Each start has its own
   session folder. `touches.csv` retains detected joint angles even after an interrupted run.
   `python/calibration/calibration_fit.py` repeats the fit from those angles and `plan.csv`.
   See the [calibration contract](../../controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md).

2. **Load measured geometry (Measuring mode, Rough submode).**
   Import the workpiece end surface with *Plane from JSON* and the hole with *Cylinder from JSON*.
   Each file is fitted and compensated for the probe ball radius. Select both surfaces and run
   *Intersect Selected* to create their intersection edge: an ellipse when the end plane is
   inclined to the hole axis. *Cone from JSON* and the Precise submode (Dry Scan, Scan) are
   placeholders without an implementation.

3. **Generate the animation trajectory (Machining mode).**
   Select the intersection edge and open *Generate Path*. Settings: chamfer size (mm) and angle
   (degrees from the hole axis), outward direction, axial distance of P_s from the hole (mm),
   lead-in and lead-out axial clearance (mm) and span (degrees of the ellipse parameter), machining
   speed (mm/s), acceleration/deceleration distance as a percentage of each lead, LIN transfer
   speed `$VEL.CP` (mm/s) and acceleration `$ACC.CP` (mm/s²), and the HOME PTP simulation speed (%).
   *Generate* computes the motion on a background thread and saves it as a machining path object.

   The motion is: PTP HOME to P_s, LIN P_s to the lead-in start, lead-in, one full machining turn,
   lead-out, LIN lead-out end to P_s, PTP P_s to HOME. Lead-in, machining and lead-out are timed
   by the machining speed with an acceleration ramp over the chosen part of each lead. Generation
   fails, without retiming, if inverse kinematics (joint angles for a TCP pose) has no solution,
   or if joint limits, rated joint speeds or the Cartesian acceleration bound (10000 mm/s², the
   controller's `$ACC_MA.CP`) are violated.

4. **Inspect and play (Dry Run).**
   The path is drawn in the 3D scene; the Scene and Properties panels select objects, control
   visibility and show saved path settings. *Dry Run* plays the saved joint motion without
   computing new points. Pause/resume keeps the current position; *Stop* returns the preview to
   HOME. Generation is blocked during playback.

5. **Prepare the robot run (RSI Sampling).**
   With a compatible generated path selected and the RSI UDP socket bound (Network settings),
   *RSI Sampling* computes one Cartesian correction per 4 ms RSI cycle for lead-in, machining and
   lead-out, followed by 50 zero corrections. It writes `C:/RoboCrapJobs/shared_data.txt`
   (version 2): the PTP/LIN targets PsIn, LeadStart and LeadEnd with their KUKA Status/Turn
   (S/T) joint-configuration values, PsOut's S/T, the flange-to-TCP frame and the transfer
   `$VEL.CP`/`$ACC.CP` in m/s and m/s². Only after a successful write are the corrections handed
   to the RSI device. Sampling keeps the plan and exported job bytes in memory. A recorded chamfer
   exchange gets its own folder with `plan.csv`, `shared_data.txt`, both RSI tables, `session.json`
   and available PLC rows. See the [record format](RECORDS_FORMAT.md).

6. **Run on the controller (T1, manual start).**
   The operator starts `controller/krl/rsi/chamfer/RSI_Chamfer_Hole.src`/`.dat` with the helpers in
   `controller/krl/lib/` and the RSI Visual context. KRL reads the shared file,
   moves HOME, PTP PsIn, LIN LeadStart, follows the streamed corrections under RSI, and only after
   the PC reports completion moves LIN PsOut and PTP HOME. The file is deleted whenever its path
   is removed, regenerated or invalidated, at a new sampling request, at startup and at shutdown,
   so KRL stops after HOME instead of using stale targets.

## Scope and status

- In scope: animation preparation and playback, nominal RSI Sampling/streaming for the
  `RSI_Chamfer_Hole` T1 run, and the probe sphere calibration session `RSI_Probe_Calibration`.
  Force correction is out of scope.
- Two complete calibration sessions (158 touches each) ran in Automatic External on 2026-11-14 and
  2026-11-17 (user runs; `plans/PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md`).
- The user reported on 2026-10-24 that RSI Sampling works and the real robot motion matches it.
- Device panels (PLC, force/torque sensor, network settings) show and control the connected
  devices; the Network page also starts the *RSI test LIN* probe-stop test
  ([RSI_Probe_Check_CONTRACT.md](../../controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md)).
  *Start Machining* is disabled.
