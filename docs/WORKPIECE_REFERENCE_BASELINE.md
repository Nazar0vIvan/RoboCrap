# Workpiece reference: project baseline

Snapshot: 2026-11-17, current working-tree contents, including existing uncommitted changes.
This is the baseline for research into locating a workpiece coordinate system for computer-aided
manufacturing (CAM) from computer-aided design (CAD) geometry. It records
implemented behavior, the documented sphere measurement method and the user's scope constraints.
It does not select an industrial CAM procedure or specify an implementation.

## Scope and sources

All four files in `docs/code context` and all three requested calibration documents were read in full.
The directory inventory, including hidden entries, contained no other context files.

| Document | Physical lines | Nonblank lines |
| --- | ---: | ---: |
| `docs/code context/AGENT_CONTRACTS.md` | 423 | 406 |
| `docs/code context/APP_DESCRIPTION.md` | 113 | 98 |
| `docs/code context/ARCHITECTURE.md` | 448 | 383 |
| `docs/hardware context/HARDWARE_SETUP.md` | 151 | 126 |
| `plans/PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md` | 955 | 829 |
| `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_THEORY_RU.md` | 518 | 364 |
| `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md` | 446 | 308 |
| Total | 3054 | 2514 |

Relevant C++, QML, controller code and the explicitly targeted Python measurement/study tools
were traced through their producers and consumers. No build, application run, device connection
or robot action was performed. The supplied `Blank - holes.stp` was not examined in this step.
Procedural instructions inside the calibration documents were treated as descriptions of that
work, not as requests to execute their commands or change hardware settings.

## Agreed scope for workpiece datums

The workpiece datum surfaces are planes for now. A plane may have several accessible patches;
holes, vice jaws and other obstructions can prevent touching or travelling over other portions.
The accessible patches do not have to form one continuous area.

Three different things must therefore remain distinguishable during the later research:

- The supporting plane calculated from measured points and used to locate the workpiece.
- The finite physical CAD face, including its boundaries and holes.
- The regions where the installed probe can touch, and the routes by which the probe can reach
  and leave those regions with the workpiece clamped.

This is a requirement for the proposed feature. The current application does not represent all
three things. No rule has yet been selected for assigning datum priority, axis signs or an origin
offset relative to the selected CAD planes.

## Application and coordinate systems

The application is a Windows Qt Quick program using C++17, Qt at least 6.8 and CMake at least 3.16
(`CMakeLists.txt:1-15`). Geometry, kinematics, trajectories, devices and viewport are separate
targets; application scene objects coordinate these targets on the GUI thread.

The installed equipment described by the project is a KUKA KR 10 R1420, KR C4 with KUKA System
Software (KSS) 8.3 and RobotSensorInterface (RSI) 3.3, a WP-500 probe with a nominal 3 mm ruby ball,
and a nominal 25 mm reference sphere. RSI exchanges occur every 4 ms. The probe signal passes
through programmable logic controller (PLC) input X2.0, output Y1.7 and
Beckhoff EL1809 input 8 to controller input `$IN[8]`
(`docs/code context/ARCHITECTURE.md:15-23, 73-94`).

The documents define {B} as the robot base frame, {F} as the flange frame, {P} as the probe frame
with origin at the ruby centre and axes parallel to {F}, and {S} as the sphere frame with origin
at the sphere centre and axes parallel to {B}. The proposed {W_R} and {W_P} are respectively the
rough and refined workpiece frames; neither has an implemented scene owner.

Positions are in millimetres. KUKA A, B and C are angles in degrees with Z-Y-X composition.
TCP means tool centre point. The application computes robot-base-relative poses. Both machining
and sphere-calibration programs in the KUKA Robot Language (KRL) assign `$BASE=$NULLFRAME` and use
the exported flange-to-TCP transform as `$TOOL`.
The machining contract explicitly requires robot-base/WORLD alignment when those poses are
used unchanged; the SRC does not establish that alignment by assigning `$ROBROOT`.
The calibration contract reports zero `$ROBROOT` in the archived machine data, rather than a
measurement made during this baseline inspection
(`resources/files/rsi/Chamfer/RSI_Chamfer_Hole_CONTRACT.md:22-32`;
`resources/files/rsi/Calibration/RSI_Probe_Calibration_CONTRACT.md:40-53`).

## Sphere calibration: documented calculation and implemented acquisition

### What calibration estimates

The method estimates the ruby-centre position relative to {F}; it does not estimate new robot
link dimensions or joint-zero corrections. Its nominal forward kinematics remains
`Kr10Kinematics::solveFK`.

For each recorded touch, the calculation takes the six actual joint angles from the first RSI
packet with `Touch=1`. Forward kinematics (FK) supplies the flange position and rotation in {B}.
Multiplying the candidate flange-relative ruby-centre coordinates by that rotation, then adding
the flange position, gives the calculated ruby-centre position in {B}.

The documented calibration model has eight scalar unknowns: three ruby-centre coordinates in
{F}, three sphere-centre coordinates in {B}, and two contact distances. A contact distance is
the distance between the calculated ruby centre and the sphere centre at detection. One distance
is used with the stylus vertically down; the other is used with the stylus horizontal in the
specified orientation. For each calibration touch, the residual is that calculated distance
minus the contact distance assigned to the touch's orientation class. Gauss-Newton least squares
minimizes the sum of their squares
(`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:119-195`).

The orientation groups are G1 (A=180, B=0, C=180 degrees), G2 (A=0, B=90, C=0 degrees) and G3
(A=0, B=0, C=180 degrees). G1 and G3 have the stylus down and share one contact distance; G2 has
the stylus along positive X of {B} and uses the other. The method demonstrates why G3 is needed
to identify all three ruby-centre coordinates with this plan. Merely turning the downward
probe around its stylus axis does not identify its axial TCP component
(`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:249-371`).

The six calibration passes are G1, G2, G3, G3, G2, G1, with the second pass of each group reversed.
Each pass uses 19 approach directions, giving 114 touches and 57 repeated-direction pairs.
The method uses differences between paired residuals to estimate repeatability separately from
the common systematic part. Drift between repetitions still contributes to that estimate.

### What the application currently does

1. Entering Calibration reads the deployed `resources/json/calibration_sphere.json`. Its ten
   rough jog readings supply XYZ ruby-centre positions. Although the JSON also contains
   orientation and KUKA configuration values Status/Turn (S/T), `roughPoint` reads only X, Y and Z.
2. `fitSphere` fits a centre and radius to those positions by algebraic least squares. The scene
   stores that ball-centre radius, a compensated radius obtained by subtracting 1.5 mm, and the
   separate nominal sphere radius of 12.5 mm. This rough fit does not solve for the ruby centre
   relative to the flange. For example, the method reports a fitted radius of 13.8342 mm;
   subtracting 1.5 mm gives a displayed compensated radius of 12.3342 mm.
3. `createProbeTouchPlan` constructs the six calibration passes and two verification passes.
   `ProbeTouchMotion::create` prepares HOME, joint motion (PTP) to entry, straight Cartesian
   motion (LIN) for descent, circular Cartesian motion (CIRC) for transfers, approaches, retracts,
   retreats and the final HOME for animation. HOME is the robot model's reference joint pose.
4. Each animated approach travels 3 mm to nominal contact at 1 mm/s. Calibration RSI sampling
   exports a 5 mm search: 1250 increments of 0.004 mm, one per 4 ms cycle, then 50 zero increments.
   Thus the robot search includes 2 mm beyond the animated nominal contact.
5. Sampling writes the touch plan as comma-separated values (CSV) under `records/<id>/`, then
   application completion writes `calibration_data.txt` and installs one correction stream per touch. The documented session
   has 158 touches, 485 KRL steps and 159 stream slots, including an empty slot 0.
6. KRL executes the transfers. Each search sets its touch number, enables relative RSI, waits
   12 ms, sets Start and calls `RSI_MOVECORR`. The documented RSI STOP block ends the search on
   the probe signal. A failed search retracts LIN to its recorded start and ends the program.
7. Packet captures preserve the received XML. `python/rsi_touches.py` extracts the first detected
   packet's actual joint angles, the preceding packet's angles, the controller's millisecond
   counter IPOC and the search cycle count.
   The extractor does not perform forward kinematics, estimate parameters or compute verification.

Code evidence: `src/application/scene/scenecalibration.cpp:28-105, 144-168, 241-248`;
`src/geometry/spherefit.cpp:7-50`; `src/trajectories/probe/probetouchplan.cpp:26-159`;
`src/trajectories/probe/probetouchmotion.cpp:40-163`;
`src/trajectories/probe/probersistream.cpp:20-53, 128-187`;
`src/application/scene/scenemachiningpreparation.cpp:307-348`;
`resources/files/rsi/Calibration/RSI_Probe_Calibration.src:43-100`;
`python/rsi_touches.py:49-166`.

### What verification means

Verification uses the same stationary sphere, the same search speed, and the two probe
orientations intended for measuring parts. Its approach directions differ from the calibration
directions except for the axial touch. G1 contributes 25 touches and G2 contributes 19 after
strut-clearance exclusions, giving 44. The recorded verification angles are excluded from the
calibration parameter fit.

After calibration, the verification calculation keeps the estimated ruby centre in {F} and both
contact distances fixed. It recalculates ruby centres in {B} from the 44 recorded joint-angle
sets, then fits only three sphere-centre coordinates to those centres. A verification residual
is the distance to this newly fitted sphere centre minus the already fixed contact distance
for that touch's class.

The criterion chi95 is the empirical 95th percentile of absolute verification residuals, in mm.
The specified estimator sorts 44 magnitudes in ascending order and takes value 42 because
ceil(0.95 * 44) = 42. It is not the calibration-fit root mean square (RMS) residual or an
independently measured absolute TCP-position error. The theory supplies no numerical acceptance
limit. The sphere stays in place; the theory explicitly limits transfer of this result to other
robot poses and locations.

The effective ruby radius for subsequent part measurements is the estimated class-specific
contact distance minus the nominal sphere radius, 12.5 mm. The method uses it to compensate
measured ruby-centre positions. This can differ from the nominal physical ruby radius of 1.5 mm.
An uncertified sphere radius is consequently relevant to part-measurement compensation even
though it cancels from the same-sphere verification residual.

Source: theory section 4.3 (`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_THEORY_RU.md:237-303`)
and method sections 5.3 and 7 (`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:211-243,
407-446`). The existing free-radius `fitSphere` cannot replace this verification calculation
unchanged: verification keeps the estimated contact distances fixed.

### Implementation boundary and evidence limits

The repository implements acquisition of calibration and verification touches. The application
does not integrate the documented eight-parameter estimator, repeatability analysis, fixed-distance
verification or chi95 report. Verification motion follows calibration motion in the same session;
no newly estimated TCP is applied between those passes. Manual measuring-TCP Apply exists, is
not persisted, and invalidates the generated calibration path
(`src/application/scene/sceneendeffectors.cpp:62-71`;
`src/application/scene/scenecalibration.cpp:223-229`).

Separate Python research helpers already exist: `python/probe_sphere_calibration/lsq.py:39-71`
contains a seven-parameter Gauss-Newton fit with one common contact distance and a free-radius
sphere fit. Its inspected caller `conditioning.py:23-36` supplies nominal poses from historical
study plans. Those tools do not form a measured-session pipeline for the current two-distance
model or integrate results into the application.

The ExecPlan records two user-run sessions with all 158 touches, on 2026-11-14 and 2026-11-17.
Its Outcomes section explicitly leaves estimation, numerical verification, model comparison and
a verification-only session outside the completed plan (`plans/PROBE_SPHERE_RSI_CALIBRATION_EXECPLAN.md:403-419`).
Its opening descriptions of an animation-only application are historical starting conditions.
The method's earlier `Precision=1` discussion is also historical: the plan records successful
six-decimal transmission after changing Precision to 6. Neither old passage describes the latest
documented acquisition state.

Runtime session records and live settings were not rechecked. The repository calibration folder
does not contain RSI Visual context exports; its STOP-block wiring and World correction frame
are specified by the contract. The generated motion checks the ruby ball against the sphere and
strut at stored points; this is not a robot-link, probe-body, workpiece or vice collision check.

## Existing workpiece geometry and CAM

| Operation | Implemented behavior and evidence |
| --- | --- |
| Import | JSON plane/cylinder dialogs call `SceneSurfaceImporter`, then `decodeProbeSamples`. Input is a point array or an object with points, radius and dir. No frame, units, timestamp or probe-calibration identity is carried. No placement transform is applied (`src/application/qml/Main.qml:82-96`; `src/application/scene/scenesurfaceimporter.cpp:23-91`; `src/geometry/utils.cpp:45-80`). |
| Fit a plane | `Plane::fromPoints` requires at least three finite points with two independent directions. Singular value decomposition (SVD) finds the supporting plane; the largest-magnitude normal component is made positive. This selects a numerical sign, not a physical material side. No flatness/RMS acceptance threshold is applied (`src/geometry/plane.cpp:8-47`). |
| Bound and compensate a plane | `BoundedPlane` encloses projected sample positions in one rectangle, choosing an in-plane axis from global X or Y. Its origin is the rectangle centre. Compensation shifts the plane by dir times radius along the fitted normal; original readings remain unchanged (`src/geometry/boundedplane.cpp:20-91`). |
| Intersect | `SceneModel` accepts a plane and cylinder. `intersectSurfaces` computes their full supporting-surface ellipse; patch dimensions affect tolerances, not clipping. There is no plane-plane intersection or three-plane frame construction (`src/application/scene/scenemodel.cpp:57-110`; `src/geometry/surfaceintersection.cpp:59-99`). |
| Store and display | `SceneObject` owns numerical shapes and supplies their coordinates directly to drawing values. It has no workpiece parent, placement matrix or datum role (`src/application/scene/sceneobject.h:36-100`; `src/application/scene/sceneobject.cpp:138-184`). |
| Read CAD | Runtime loading reads BREP. STEP reading belongs to `tools/cadstep2brep`, used during the build. The existing `DrawingModel` has a BREP path and translation-only origin in {B}, currently used for the calibration stand (`src/viewport/occt/cadloadworker.cpp:30-55`; `tools/cadstep2brep/main.cpp:36`; `src/viewport/include/viewport/occcontroller.h:46-50`). |
| Select with the mouse | OCCT detection enables subshapes, but application selection returns the owning scene-object ID. It does not preserve an individual CAD-face identity for assigning a datum (`src/viewport/occt/occscene.cpp:197`; `src/viewport/occt/occviewport.cpp:134`; `src/viewport/adapters/occsceneadapter.cpp:290`; `src/application/main.cpp:117`). |
| Generate machining | `SceneMachining` takes the ellipse and source cylinder directly from scene coordinates. `ChamferPathSample::tcp` is explicitly BASE-relative. Local frames used to construct tool orientation are not stored workpiece frames (`src/application/scene/scenemachining.cpp:107-129`; `src/trajectories/include/trajectories/chamferpath.h:29-37`). |
| Send machining motion | The sampler evaluates the analytical trajectory, calculates Cartesian corrections, and exports base-relative staging/lead targets. KRL uses the exported tool and null BASE (`src/trajectories/chamfer/chamferrsistream.cpp:146-202`; `resources/files/rsi/Chamfer/RSI_Chamfer_Hole.src:19-21`). |
| Precise scanning | Dry Scan changes UI flags; Scan is disabled. The Precise classification and ScanPath kind do not provide a scan implementation (`src/application/qml/Main.qml:376`; `src/application/qml/Views/Workspace/ModeToolBar.qml:231`). |

Separated coplanar readings can already enter the plane fitter. Their enclosing display rectangle
fills intervening holes and gaps, so that rectangle cannot serve as an accessible probing region.
The fit result carries no CAD-face identity; associating a fitted plane with one of several
coplanar CAD faces is not implemented.

One current placement convention needs attention in later design: `SceneMachining::setInputId`
chooses the default outward-axis flip from the source cylinder's X component because the opening
is expected toward negative scene X (`src/application/scene/scenemachining.cpp:89-100`). The
current robot/TCP revision mechanism also has no workpiece-placement revision to invalidate
prepared paths or RSI jobs after relocating a part.

## Baseline against the proposed seven operations

| Proposed operation | Present support | Missing capability |
| --- | --- | --- |
| Rough touches of datum planes in manual reduced-speed mode T1 | Offline import and compensated fitting of supplied XYZ readings | A workpiece-datum measurement session with touch identity and acquisition state |
| Determine {W_R} from planes | Plane fitting and rigid-transform/basis utilities | Plane association, datum roles, origin/axis construction and acceptance criteria |
| Add {W_R} to the scene | Existing robot/world/tool axes and stable scene IDs | An owned workpiece frame and its scene presentation |
| Import CAD in {W_R} | BREP reader and build-time STEP conversion | User workpiece import, scene ownership of imported workpiece topology and full rigid placement |
| Select datum faces and prepare RSI probing | Whole-object selection and the sphere probing pipeline | Persistent face selection, accessible planar regions and workpiece probing paths |
| Probe and determine {W_P} | Per-touch RSI searches, packet capture and joint-angle extraction | Part-point reconstruction with the accepted probe model, plane refinement and frame estimation |
| Update workpiece placement | Accepted drawing installation and path invalidation patterns | Workpiece transform ownership, replacement and dependent-path/job invalidation |

Existing useful operations are the plane fitter, transform and basis utilities, FK/tool
composition, immutable motion generation, playback, touch numbering, per-touch RSI streams and
measurement captures. Sphere-specific touch positions, CIRC transfers and sphere/strut clearance
calculations do not establish accessible regions or transfer paths for a clamped workpiece.

The baseline leaves the datum hierarchy, treatment of nonorthogonal measured planes, probe
orientation coverage, effective-radius use, frame accuracy criteria and placement-update semantics
open for the subsequent workpiece inspection and external research. It establishes no robot-motion
authorization and makes no claim about the uploaded workpiece's geometry.
