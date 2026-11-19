# Workpiece reference and planar probing: proposed design

Design date: 2026-11-19. This is step 4, following the
[project baseline](WORKPIECE_REFERENCE_BASELINE.md),
[example inspection](WORKPIECE_STEP_INSPECTION.md) and
[industrial research](WORKPIECE_REFERENCE_RESEARCH.md).
This document specifies proposed behavior; it does not describe an implemented feature or authorize
hardware actions. The current-code observations below were checked against the working tree.

The user selected primary, secondary and tertiary planes, preserving the primary plane's orientation
when measured planes disagree with CAD. The STEP example does not restrict supported dimensions,
part shapes, plane angles or coordinate origins. Accessible portions of a plane may be disconnected.

## 1. Design decisions

1. Keep nominal CAD geometry permanently in its native workpiece frame {W}. Estimate one rigid
   transform matrix placing that frame in robot base {B}; rough and refined results estimate the
   same frame definition.
2. Use three ordered datum groups. Each group contains one supporting plane and one or more
   associated coplanar CAD faces. Fit measured planes by the existing least-squares operation,
   then construct the ordered registration described in section 5.
3. Treat that construction as an engineering registration convention. It is not a simulation
   of physical datum contact or an assertion of ISO/ASME compliance. A least-squares plane through
   measurements differs from an external plane tangent to material high points; the
   [research](WORKPIECE_REFERENCE_RESEARCH.md#42-datum-precedence-and-global-best-fit-are-different-choices)
   records that distinction.
4. Interpret workpiece scanning as repeated discrete touch-trigger searches. Continuous-contact
   scanning and force correction are outside this design.
5. Preserve raw actual-angle measurements and fitted measurement planes in {B}. Moving the nominal
   CAD must not move those measurements.
6. Keep the existing robot-base motion engine and null-KUKA-BASE/World-RSI execution convention.
   Convert workpiece-local geometry into {B} before robot-motion preparation.
7. Keep numerical construction, acceptance against specified requirements and independent accuracy
   evidence distinct. No machining tolerance follows from a successful plane fit.

## 2. Current behavior and the proposed replacement

| Area | Current behavior | Proposed behavior |
| --- | --- | --- |
| Plane input | JSON XYZ samples, one radius and a numerical compensation sign | Identified touches associated with a datum, an immutable robot/probe model and a prepared search |
| Plane fitting | Plane::fromPoints uses singular value decomposition; the normal sign follows its largest component | Reuse the fit; determine the physical normal sign from the recorded probing side |
| Plane bounds | BoundedPlane displays one enclosing rectangle | Keep that display option; use CAD trim boundaries and separate allowed probing patches for planning |
| Registration | No three-plane workpiece frame | Apply the same ordered construction to nominal and measured plane triples and compose their transform matrices |
| CAD loading | Runtime BREP only; build-time STEP conversion | Add runtime STEP reading to the existing private CAD worker and retain native coordinates |
| Selection | Subshape detection becomes a whole-object selection | Preserve whole-object picking; add model-bound planar-face references for datum selection |
| CAD placement | DrawingModel permits translation only | Give a CAD model one rigid placement matrix, preserving canonical topology |
| Machining | Saved motion contains geometry and TCP targets in robot base coordinates | Also retain the original workpiece-local machining recipe for workpiece-dependent motion |
| Probe result | Sphere-fit results are displayed/written, without accepting a numerical measuring model | Explicitly accept the coupled fitted tool position, contact distances and result provenance |
| Prepared output | Robot/tool revisions and immutable motion identity determine compatibility | Include workpiece/definition revisions and reject stale generation/sampling results |

Code evidence: `src/geometry/plane.cpp:8-47`;
`src/geometry/boundedplane.cpp:20-91`;
`src/application/scene/scenesurfaceimporter.cpp:23-95`;
`src/application/scene/scenemachining.cpp:107-180`;
`src/application/scene/scenemachiningpreparation.cpp:568-590`;
`src/viewport/include/viewport/occcontroller.h:46-50`;
`tools/cadstep2brep/main.cpp:33-42`.
The historical baseline's statement that the sphere estimator is absent no longer describes
the current code.

## 3. Frames, units and the order of the operator's work

{B} is the project's robot-base frame. {W} is the native CAD frame, with file length units converted
to millimetres (mm) without recentering or rotating the part. {F} is the flange frame. {P} is the
probe frame at the calculated ruby centre, with axes parallel to {F} as required by the present
sphere method. {T} is the machining tool centre point (TCP) frame.

{D_N} is a temporary datum frame constructed from nominal CAD planes. {D_R} and {D_P} are the
corresponding temporary frames from rough and refined measured planes. Their origins are virtual
plane intersections, not necessarily accessible material points. {W_R} and {W_P} denote estimates
of the placement of {W}; they do not introduce different nominal geometry.

Translations and contact distances use mm. Joint angles and KUKA A/B/C use degrees; A/B/C retain
the project's Z-Y-X composition. Calculations retain full numerical values, not formatted UI text.
A transform matrix from frame {i} to frame {j} is written:

$$
{}^{j}A_i
$$

### Required correction to the original order

Rough touches may be recorded before loading CAD. Those touches can determine {D_R}, but cannot
determine the CAD origin and axes without the nominal datum correspondence and offsets.
Therefore, finalize {W_R} only after CAD has been loaded and its datum groups associated.
Do not call a measured corner frame {W_R} and silently place the CAD origin at that corner.

The resulting operator sequence is:

1. Create a workpiece entry. Load CAD and define the three ordered datum groups, or import earlier
   rough measurements first and complete that CAD association afterward.
2. Obtain/import rough T1 contact observations for each group and identify the probing side.
   Calculate a rough candidate using sections 4-5. Display a preliminary {D_R} if useful, but label
   the CAD unregistered until both nominal and measured definitions exist.
3. Accept the rough placement for visualization and probing preparation. Place CAD using {W_R};
   its original coordinates remain unchanged.
4. In the placed CAD, select accessible planar patches, contact distribution, probe orientation,
   search bounds and transfer routes. Prepare and preview a complete probing job.
5. Execute the separately authorized probing job. Preserve detections and failures. Fit the
   measured planes after the run; if the search directions need refinement, prepare another pass.
6. Compare the refined candidate with the specified acceptance requirements. Apply an acceptable
   refined placement after the run has ended, then regenerate dependent robot motion.

Loading unregistered CAD may show an explicitly labeled selection preview at its native origin.
That preview is not an accepted physical placement and supplies no executable robot targets.
No CAD origin convention is inferred from the example workpiece.

## 4. Measurement inputs and probe compensation

### 4.1 Accept one coupled measuring model

The existing sphere result contains fitted flange-relative ruby-centre coordinates and two contact
distances. `SceneMachiningPreparation::endRun` writes the result but does not apply it.
`SceneEndEffectors::applyMeasuringTcp` currently accepts a manually entered pose, without storing
the contact distances or calibration identity.

Extend the existing tool owner to accept one immutable numerical measuring model, denoted by
$\mathcal Q$: fitted ruby centre, contact distances, reference-sphere radius, robot-model identity, probe mounting identity,
search feed, lead-in conditions, orientation definitions, measured direction sets and result diagnostics.
Apply the fitted centre and associated compensation values together. A manual TCP change or changed
mounting/model invalidates that accepted association. Do not reconstruct the model from result rows.
Accepting a model does not certify its accuracy.
Require finite parameters and physically usable positive radii. Check the assumed parallel
probe/flange axes when accepting the measuring TCP; a nonzero manually edited TCP rotation cannot
silently survive as though the sphere fit had estimated that rotation.

For orientation class o, let rho_o be the fitted contact distance in mm, r_s the sphere radius
used by the method, and r_eff,o the effective radius in mm. These quantities belong to the
accepted measuring model:

$$
r_{\mathrm{eff},o}(\mathcal Q)=\rho_o(\mathcal Q)-r_s(\mathcal Q)
$$

The present method uses nominal r_s=12.5 mm. Preserve that value and its provenance; substituting
a different radius later changes the measurement model. The effective radius includes the model's
mean detection/pretravel effects and need not equal the nominal physical ruby radius, 1.5 mm.
See `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:211-243`.

Initially, keep each datum pass at one accepted probe orientation/class and search feed.
The sphere method uses 1 mm/s and verifies G1/G2 orientations; its probe axes are parallel to the
flange axes. G1 has A=180, B=0, C=180 degrees; G2 has A=0, B=90, C=0 degrees in that convention.
Do not rotate those orientations with {W} or treat arbitrary measuring-TCP ABC as equivalent.
Choose a supported base-relative probe orientation separately from the transformed contact position.

The finite sphere direction sets do not establish a continuous angular accuracy envelope.
In particular, two fitted scalar radii do not verify every workpiece normal or flange roll.
Retain the actual coverage and diagnostics. Additional orientation/direction coverage or a numerical
accuracy claim requires evidence beyond matching the label G1 or G2.

Retain the calibration's approach/lead-in conditions as part of that evidence. The sphere procedure
uses a 3 mm nominal approach at constant search feed; its method also assumes settled motion before
contact (`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:401`).
Workpiece preparation must allow enough travel before the earliest plausible contact for the
accepted response model to apply, or supply evidence of equivalent response. Equal commanded feed
alone is insufficient. The 3 mm value is not a universally verified minimum clearance or lead-in.

### 4.2 Raw observations

For rough input, record the datum group, actual joint-angle snapshot, probing side,
tool/model identity and acquisition method. A stopped manually jogged contact is an approximate
observation, not the first-trigger measurement used by the precise RSI procedure.
Current `recordTouch` ignores detections outside a calibration run; the existing RSI connection
does not automatically acquire arbitrary T1 jog contacts.

For the first rough workflow, provide explicit entry/import of six actual joint angles from an
identified source, such as stopped `$AXIS_ACT_MEAS`, and the physical side for each observation.
Reconstruct centres using the project's forward kinematics. Do not mix controller Cartesian XYZ
into this reconstruction: controller BASE/TOOL definitions alone do not establish that its
kinematic corrections match the project model.
See `docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md:84-97`.
New automatic jog acquisition would require a separate controller/data mechanism.

For these manually held rough contacts, use the stated nominal physical tip radius, initially
1.5 mm, for the approximate plane offset. A stopped jog observation need not have the calibrated
search feed, lead-in or trigger timing, so it must not silently inherit the refined effective-radius
claim. Record this approximation and use the result only as an initial placement estimate.
The rough editor displays the fitted normal in {B}; the operator explicitly selects which side
contains the probe before contact. Retain that sign choice with the raw observations and associate
it with the corresponding nominal CAD face's free-space side.

For an automated workpiece attempt, retain:

- A unique job/attempt identifier, immutable plan and workpiece/model/placement revisions.
- A map from each touch number to its datum/CAD face, nominal contact, probe orientation/class,
  search direction/feed, permitted travel, and transfer/retract route.
- Actual six-axis `AIPos` values at the first valid trigger packet for that touch.
- Missing/failed touches and explicit repetitions, with their own identities.
- The robot/probe model snapshots and raw packet-capture reference.

Use a new prepared job identifier for each workpiece attempt or refinement pass. Extend the current
detection event with its source job identity; touch number alone cannot reject a queued detection
from a replaced job. Retain partial records after failure, without silently averaging or replacing
earlier observations.

The current event and detection CSV have no per-touch time or controller cycle counter IPOC.
The packet capture has real timing/IPOC values. Do not invent detection time from touch number
or animation time; preserve the packet association if timing is later required.

### 4.3 Reconstruct centres, fit, then compensate

Let $\mathcal A_j$ denote the tuple of six scalar joint-angle readings for touch j, in degrees;
it is not a Cartesian vector. Let $\mathcal K$ be the saved robot model. The robot model and accepted
measuring model are fixed for the session. Points b, f and c_j are respectively the base origin,
flange origin and calculated ruby centre at touch j.
The forward-kinematics operation supplies the flange rotation and position:

$$
{}^{B}\bar r_{bc_j}(\mathcal A_j;\mathcal K,\mathcal Q)
=
{}^{B}\bar r_{bf}(\mathcal A_j;\mathcal K)
+{}^{B}R_F(\mathcal A_j;\mathcal K)\,{}^{F}\bar r_{fc_j}(\mathcal Q)
$$

This is the calculation already used inside `fitProbeCalibration`; neither nominal contact
targets nor interpolated animation poses are measurements.

For each datum, fit its calculated centres with `Plane::fromPoints`. Require finite observations
and at least three noncollinear positions; refined acquisition uses more than three contacts
so the plane fit has redundant observations. More contacts alone do not establish accuracy.
Use deliberate spatial distribution across the available patches, keeping one equal-weight
observation per planned contact. Retain and report every observation; no automatic outlier removal
is part of this design.

Orient the fitted normal toward the recorded free-space side. When a numerical normal is reversed,
reverse its signed plane offset too. The fitter's largest-component sign is not a material-side
rule. Reject an ambiguous/grazing side indication; do not guess a 180-degree placement.
For an automated pass, choose the fitted sign whose dot product with the commanded inward search
direction is negative, then check that its angular disagreement with the planned outward normal
meets the specified limit. The planned side and CAD association remain recorded inputs.

For a unit outward normal, let h_c be the fitted centre-plane offset and h the compensated
material-plane offset, both in mm. For the refined pass's accepted orientation class:

$$
h=h_c-r_{\mathrm{eff},o}(\mathcal Q)
$$

Keep the centre plane, compensated plane and raw centres as distinct results. The existing
`BoundedPlane::fromSamples` can express a constant-radius shift once its numerical sign has been
resolved. One uniform-radius input cannot represent a mixture of orientation-class radii unchanged.

For per-contact diagnostics, let $\mathcal P_i$ be the compensated plane for datum i, with its
fitted outward unit normal; class o is fixed for that datum pass.
The corresponding surface-position estimate p_j is:

$$
{}^{B}\bar r_{bp_j}(\mathcal A_j;\mathcal K,\mathcal Q,\mathcal P_i)
=
{}^{B}\bar r_{bc_j}(\mathcal A_j;\mathcal K,\mathcal Q)
-r_{\mathrm{eff},o}(\mathcal Q)\,{}^{B}\bar e_{n_i}(\mathcal P_i)
$$

This is a model-based estimate derived from the raw touch, not an independently measured surface
point. Use these same estimates when reporting distances to the placed nominal CAD planes.

The current effective-radius derivation assumes travel along the actual surface normal.
A search normal to roughly placed CAD can be oblique to the physical plane. Fit the resulting
centres to estimate the plane normal, compare that normal with the commanded search, and reprepare/
remeasure if the mismatch exceeds an established limit. Until that limit and directional response
are supported, the compensated offset is a model-based estimate. Fitting the normal afterward
does not retrospectively make an oblique trigger measurement exact.

## 5. Ordered three-plane registration

### 5.1 One construction applied to both data sets

The following is the proposed calculation, derived geometrically. It is not a claim that a vendor
uses this exact solver. Nominal and measured inter-plane angles need not be 90 degrees.

Let $\mathcal P$ be one ordered set of three signed planes in frame {f}; i=1,2,3 identifies primary,
secondary and tertiary. Its inputs are unit normals and signed offsets h_i in mm.
Point o_f is the origin of {f}, and q is a point on plane i:

$$
\left({}^{f}\bar e_{n_i}(\mathcal P)\right)^T{}^{f}\bar r_{o_fq}=h_i(\mathcal P)
$$

The coefficient matrix $H(\mathcal P)$ has the three unit-normal transposes as rows. Solve the following
system for the virtual intersection d, using a rank-revealing numerical solve, not a stored inverse:

$$
H(\mathcal P)\,{}^{f}\bar r_{o_fd}(\mathcal P)=
\begin{bmatrix}h_1(\mathcal P)\\h_2(\mathcal P)\\h_3(\mathcal P)\end{bmatrix}
$$

Require rank three. Report the smallest singular value and condition number, defined as the
largest singular value divided by the smallest. Pairwise nonparallel planes alone are insufficient.
For example, X=0, Y=0 and X+Y=0 share a line and must be rejected.

Construct the temporary datum axes in order:

$$
{}^{f}\bar e_z(\mathcal P)={}^{f}\bar e_{n_1}(\mathcal P)
$$

$$
{}^{f}\bar u(\mathcal P)=
{}^{f}\bar e_{n_2}(\mathcal P)
-\left[\left({}^{f}\bar e_{n_2}(\mathcal P)\right)^T{}^{f}\bar e_z(\mathcal P)\right]{}^{f}\bar e_z(\mathcal P)
$$

$$
{}^{f}\bar e_x(\mathcal P)=\frac{{}^{f}\bar u(\mathcal P)}{\|{}^{f}\bar u(\mathcal P)\|},
\qquad
{}^{f}\bar e_y(\mathcal P)={}^{f}\bar e_z(\mathcal P)\times{}^{f}\bar e_x(\mathcal P)
$$

The projected secondary normal is perpendicular to the primary normal.
Reject a zero or numerically unreliable projection. Its length is the sine of the angle between
the first two normals, so nearly parallel planes also amplify orientation error.
Place the three unit axes in X/Y/Z columns and the intersection in the translation column to
obtain the temporary frame's transform matrix.
Reuse `prjUnitOnPlane`, `basis2rot`, `makeTransform` and `inverseRigidTransform` where their
preconditions hold; `vecs2basis` alone does not orthogonalize three measured normals.

Let $\mathcal P_N$ be the nominal CAD triple in {W}, and $\mathcal P_s$ the measured triple in {B},
where s=R for rough data or s=P for refined data. Their temporary frames are {D_N} and {D_s}.
The registration assigns equal coordinates to corresponding points of these temporary frames;
their corresponding-coordinate transform matrix is the identity matrix. The workpiece placement is:

$$
{}^{B}A_W(\mathcal P_s,\mathcal P_N)=
{}^{B}A_{D_s}(\mathcal P_s)\,
{}^{D_s}A_{D_N}\,
\left[{}^{W}A_{D_N}(\mathcal P_N)\right]^{-1}
$$

The middle identity expresses the chosen registration correspondence; it is not an independently
measured transform. In code, multiply the measured temporary-frame matrix by the inverse nominal
temporary-frame matrix.

Construct {W_R} and {W_P} with exactly this calculation and the same
datum roles/signs. Their difference is the acquisition/model evidence, not an origin convention.
Validate a finite, orthonormal, right-handed result before calling the rigid-inverse helper.

This construction maps the primary plane exactly and maps the primary/secondary intersection line
exactly. The tertiary plane determines the point along that line. If measured angles differ from
nominal angles, the complete secondary and tertiary planes generally do not coincide after placement.
Report their discrepancies; do not average them back into the primary orientation.
No rigid transform can simultaneously preserve different inter-plane angles.

The tertiary plane has no rotational influence in this construction. Position along the
primary/secondary intersection line follows the independently fitted tertiary plane's intersection,
not a constrained fit to a physical datum simulator. A later requirement for drawing-datum simulation
would require a different, explicitly specified surface-association operation.

Three independent measured planes always intersect; a zero intersection residual is therefore
not a redundant check of measurement quality.

### 5.2 Numerical example, independent of the supplied STEP

This hypothetical example deliberately includes a nonorthogonal secondary plane.
All coordinates and constants in the table use mm. Nominal coordinates are X/Y/Z in {W};
measured coordinates are X/Y/Z in {B}. Signed normals follow the equations as written.

| Role | Nominal equation | Measured equation |
| --- | --- | --- |
| Primary | Z = 20 | Z = 320 |
| Secondary | X + Z = 30 | Y + Z = 530 |
| Tertiary | Y = 30 | −X = −70 |

Normalize both sides of each secondary equation by the square root of 2 before using unit-normal
offsets. The nominal intersection is (10,30,20); the measured intersection is (70,210,320).
Projection of the secondary normal gives positive X in {W} and positive Y in {B}; the construction
therefore gives a 90-degree rotation about positive Z. Rotating (10,30,20) gives (−30,10,20).
Subtracting that result from (70,210,320) gives translation (100,200,300).

The CAD origin is placed at (100,200,300), not at the measured intersection. The primary/secondary
normal angle is 45 degrees; perpendicular datum planes were not required.
These are exact synthetic inputs, not a robot measurement or an accuracy estimate.

## 6. CAD, datum identity and accessible regions

### 6.1 Import and face references

Use the existing private CAD worker to read user STEP files and return immutable topology plus a
neutral face catalogue. Preserve internal CAD placements and normalize length units to mm.
Do not recenter geometry, scale it to a measured fit or recover an origin from its bounding box.
Reject empty/failed imports, ambiguous units or unsupported datum geometry with a named reason.

Runtime STEP import requires explicitly extending the current Viewport build dependency on the
STEP reader; `TKDESTEP` currently belongs only to `cadstep2brep`.
Reuse the converter's STEP-read operation where shared private code reduces the combined implementation.
Launching the build-only converter instead would add subprocess, deployment, temporary-file and
cleanup ownership; it is not the proposed runtime route.
Keep robot/tool BREP loading and its readiness independent of workpiece import failure.

The [official STEP guide](https://occt3d.com/dev/doc/overview/html/occt_user_guides__step.html)
documents unit conversion and assembly translation. The inspected online edition is OCCT 8.0.1;
implementation must check the project's bundled 8.0.0 API rather than assume every online overload.
Retain serialized parsing and avoid process-global translator settings leaking into other imports.

A face reference consists of the workpiece identity, immutable imported-model identity and ordinal
in that model's accepted face map. Build selection and catalogue references from the same canonical
topology. Preserve references during placement changes and viewer recreation; CAD replacement,
changed content at the same path or a changed import result requires reassociation.
Do not use STEP entity numbers, scene AIS handles or filenames alone as persistent face identity.
The current path-keyed drawing cache needs model identity for user imports.

The application receives plain plane definitions, face references and native-coordinate positions.
OCCT shapes and trimmed-face classification stay behind the CAD/Viewport boundary.
Return batch classification results for candidate contacts; workers do not populate scene objects.

### 6.2 Datum groups and patches

For each of the three roles, store associated planar faces, their nominal signed supporting plane
and the chosen free-space side. Several coplanar faces may describe one datum; compare their
planes using CAD tolerances before accepting the grouping. Preserve face locations and orientation.
An open surface without an unambiguous material side needs an explicit side selection.

Keep three different geometries:

- The infinite supporting plane used by registration.
- The finite trimmed CAD faces, including every inner hole boundary.
- Operator-selected usable patches and transfer geometry for the clamped setup.

For the first implementation, several rectangular patches in the datum's fixed plane coordinates
are sufficient to describe allowed contact regions. Their union can be disconnected and is always
intersected with the actual trimmed CAD faces. This smaller representation avoids a general polygon
editing framework. It does not turn a rectangle, a convex hull or BoundedPlane bounds into material.

Use one selected reference face's stored plane origin and native in-plane direction to define the
datum's patch coordinates. Keep that direction as the first axis; cross the chosen outward normal
with the first axis to obtain the second axis in right-handed order. Retain the reference face and
these native-{W} coordinates when workpiece placement changes. Patch coordinates and grid spacings are in mm.
Generate a uniform grid anchored in those coordinates: integer multiples of the two positive
spacings inside the selected rectangles. Remove duplicates from overlapping rectangles.
Keep only points classified inside an associated trimmed face and satisfying the selected boundary
margin. Allow explicit added/removed contacts; retain their identities and apply the same region,
trim and margin checks to manually added contacts.

Report the generated and discarded counts. Reject an insufficient or effectively collinear set,
even if several patches were selected. No universal grid spacing, boundary inset or minimum useful
patch span is supplied by the research. The boundary margin must account for initial-placement
error and probe/setup geometry; CAD representation tolerance is not that margin.

## 7. Probing motions and RSI preparation

### 7.1 Inputs and local search geometry

Each datum pass needs a supported base-relative probe orientation, search feed, approach distance,
overtravel, retract distance, contact pattern and entry/exit/transfer routes.
The approach distance must also satisfy the accepted lead-in conditions after allowing for rough
position error toward the probe; otherwise an earlier-than-expected contact can occur before
settled search motion.
Transform each nominal material contact and outward normal from {W} using the accepted rough pose.
Construct the expected ball-centre contact by adding the accepted effective radius along that normal.
Start outside that centre by the approach distance; search toward material along the negative normal.
Maximum travel is the approach distance plus overtravel.

The probe orientation is selected in {B} under section 4.1; it does not follow CAD rotation merely
because contact positions do. Check tool/stylus access and robot inverse kinematics for that orientation.

Generate separate retract and transfer segments. A default straight transfer between retracted
starts is only a candidate. Permit explicit clearance waypoints and bypasses; never connect contacts
by continuous motion on a surface across holes. Sphere-centred CIRC transfers and sphere/strut
clearance calculations do not apply to a vice.

Regions and routes attached to the part use {W}; fixed fixture geometry and fixture-relative
waypoints use {B}. Resolve both into {B} for one immutable preparation snapshot.
The setup input must identify which frame owns a waypoint.
For unknown fixtures, require explicit routes/corridors rather than claiming automatic avoidance.
CAD contact membership does not establish probe-body or robot-link clearance; the current application
has no complete workpiece/vice collision calculation. Report the scope of the geometric checks.

### 7.2 Maximum search, detection and recovery

Check reachability, joint limits and relevant known geometry over the complete permitted search
and retract envelope, including the part beyond nominal contact. Treat intended probe deflection
separately from an unrelated-body collision; the permissible overtravel/stop budget remains a
physical input. Current sphere animation checks only its nominal approach, which is insufficient
as a workpiece search-envelope check.

Nominal playback represents expected contact. Any extra search-envelope calculations are temporary
checks/diagnostic geometry, not measured points or additional claimed animation accuracy.
Export search bounds only after those checks succeed. Braking distance, signal delay and the
probe's permitted deflection must support the chosen speed/overtravel; their bounds are not
established by the current sphere run.

RSI uses 4 ms cycles. At the present 1 mm/s search feed, one full-cycle position increment is
0.004 mm along the search direction. Divide the permitted length into those increments, shortening
the final one when necessary so their sum equals that length. Build cumulative offsets, round them
to the wire precision, then difference consecutive rounded offsets as the current sampler does;
check the represented endpoint against the intended bound. Preserve the existing termination
handshake. The sphere's 5 mm/1250-increment search and 50-zero tail describe its existing job;
5 mm is not a default validated workpiece clearance.

Retain the first valid trigger's actual angles. A completed stream without a trigger is a missed
search, never a surface point. Reject a pre-triggered probe. Missing detections, job mismatch,
RSI errors, disconnect or interrupted execution retain partial data and leave the accepted pose
unchanged. An incomplete three-plane data set cannot publish {W_P}.

Require a controller-reported normal completion for the same job as well as complete valid touches.
Current KRL can record the final touch and then fail RSI_OFF, retract and return; a subsequent PLC
ProgramFinished with every expected touch does not distinguish that error from normal completion
(`controller/krl/rsi/calibration/RSI_Probe_Calibration.src:73-75,98-100`).

Retract from the actual stopped state along the checked search corridor to the checked start/clearance
pose. The current KRL `LIN SearchStart` failure route is a reusable mechanism, not evidence that a
workpiece/vice route is clear or that missed-search recovery has been verified on this cell.
Do not automatically move HOME from an unestablished recovery state.

### 7.3 Reuse the job executor without confusing job purpose

Reuse the existing ordered PTP/LIN/CIRC/SEARCH/HOME step execution and per-touch stream transport.
Extend the probe-job format with an explicit purpose and the bounds/counts required by workpiece
searches; keep the existing calibration job format supported. The new workpiece format requires
the identity/outcome checks below; accepting a legacy calibration file does not make it a
workpiece-registration job.
Workpiece job preparation must derive its capacity checks from the controller arrays and stream
indices, rather than inherit the sphere's fixed 158 touches or silently truncate contacts.

Evolve the current probe-program entry into the shared probe-job executor, retaining the existing
module/program-number route and updating the affected application labels/callers.
Calibration completion still invokes the sphere fit;
workpiece completion invokes plane reconstruction/registration. The completion operation must use
the captured job purpose, never the current UI submode or a filename guess.
This avoids a second copy of the controller search loop and a separate device transport.
The exact versioned file fields and controller integration belong in the implementation ExecPlan.

The controller must echo a job/run token tied to the immutable prepared job, including in its touch
requests and terminal result. The PC checks that token before supplying a search or accepting its
detection/result. Adding the current PC diagnostic identifier to a queued event alone cannot prove
that a controller request belongs to the newly installed job.
The terminal result identifies normal completion or abort/error, last completed and failed
step/touch where applicable, and recovery/retract completion. Publish normal completion only after
the planned final motion has completed. A missing or mismatched terminal result leaves the run
unresolved even when the PLC says the program finished; stream exhaustion remains a different event.

Require the declared initial robot state and a checked route from it before the first movement.
The present module executes PTP HOME before reading its job. The shared executor must check its
start precondition before such motion; it must not use that PTP to recover blindly from a previous
search or arbitrary jog pose. A planned return HOME is separate from failed-search recovery.

Create a fresh immutable job and record folder per attempt. Install streams only after successful
plan/job writes, retaining the existing atomic replacement convention.
No fitting, file writing or unfinished-worker waits belong in ControlIO receive callbacks.

## 8. Refinement and acceptance

Fit the completed pass, recompute the ordered registration and report:

- Contact counts, repeats and missing/failed observations by datum.
- Plane-fit root mean square (RMS) and maximum absolute residuals in mm; point spans and
  noncollinearity/conditioning diagnostics.
- Nominal/measured inter-plane angles and their differences in degrees.
- Search-direction versus fitted-normal mismatch and model coverage.
- Distances of measured surface estimates to the transformed nominal planes, including the
  secondary/tertiary disagreement that the ordered construction does not eliminate.
- Translation/rotation change from the rough result and displacement at selected part/task locations.

Compare these quantities with explicit task requirements. Numerical rank tolerances merely detect
an unusable calculation; they are not machining tolerances.
If the normal mismatch requires another pass, regenerate searches from the completed fit and inspect
the new routes/envelopes. Do not change a running job or correct contact motion continuously.
Repeat passes use fresh job identities and retain their earlier results.

A candidate can be calculated and inspected before accuracy requirements are supplied.
An acceptable refined registration additionally needs specified criteria and evidence that the
measurement model applies. A missing limit must remain "not specified", not count as a pass.
Do not assign the Precise classification solely because a procedure returned finite numbers.

Independent accuracy checks need known reference quantities with stated uncertainty, covering the
part region and robot/tool configurations used by the task. Extra touches from the same measuring
chain can reveal inconsistency but can miss shared systematic errors.
The current sphere chi95, the empirical 95th percentile of absolute verification residuals,
does not supply an absolute workpiece-registration tolerance. Its nominal sphere-radius uncertainty
also remains relevant to effective-radius compensation.
See the [research accuracy discussion](WORKPIECE_REFERENCE_RESEARCH.md#62-residuals-do-not-establish-downstream-machining-accuracy).

## 9. Ownership and minimum state additions

These are proposed responsibilities, not existing declarations.

| Owner | Proposed responsibility | Reason existing state is insufficient |
| --- | --- | --- |
| SceneModel / SceneObject | One Workpiece value: CAD identity, datum/patch definition, active accepted registration and provenance | Current variants contain no workpiece placement or datum association; extending them avoids a second scene document |
| One workpiece coordinator | Pending edits, candidate calculation, import/probing progress and session transitions | SceneSurfaceImporter is a synchronous point importer; SceneCalibration owns sphere-specific state and visibility |
| SceneEndEffectors | Accepted coupled measuring model and its invalidation | Accepted measuring TCP alone has no fitted contact distances or result identity |
| Viewport CAD result | Immutable topology, matching face map and neutral face descriptions | DrawingModel currently caches by path and whole-object selection discards the face |
| Saved workpiece-dependent motion | Native geometry/settings recipe plus prepared immutable base-relative motion and input revisions | A base-relative motion cannot regenerate itself at a new workpiece placement |
| Existing preparation owner | Versioned probe job, installed stream ownership, run purpose/state and contact collection | Current run state only represents calibration program 1 |

Use one active accepted placement; retain earlier rough/refined results in immutable session history.
Do not maintain separate editable current/inverse/CAD/scene copies of the same transform.
Reuse existing scene IDs, drawing acceptance, shared immutable motions, copied worker inputs and
GUI-thread publication. A general frame tree, global transform manager or dependency-graph framework
is unnecessary for one active workpiece.

Keep input revisions and accepted-placement revisions distinct: changing a patch/route invalidates
probing preparation, whereas changing an accepted transform invalidates workpiece-dependent motion.
Every worker receives numerical copies plus these identities, and returns a result/error through
its future. Publish on the GUI thread only if the same workpiece, CAD and relevant revisions still
exist; retain current accepted values on import, numerical or drawing failure. Join remaining
workers at shutdown using the existing lifetime order.

The workpiece value requires mutable property/status notifications; current SceneObject properties
and classification are constant. Extend only the affected interfaces, preserving existing enum
values, whole-object selection and legacy object behavior. Owners update their own private state
through methods; friendship does not permit direct external field changes.

Extend DrawingModel's translation to one rigid matrix; update the calibration-stand caller using
the existing translation helper. Apply registration once to presentation, without repeatedly
transforming/refitting canonical CAD. Reuse existing drawing replay and part-transform mechanisms.
During import failure/loading, retain the previous accepted workpiece; drawing insertion acceptance
alone does not mean an asynchronously loaded model is geometrically ready.

UI editors use the existing Properties region and workflow controls. Reserve fixed space for
status/progress; importing, selecting faces, showing diagnostics and changing compatibility must
not resize the viewport or displace existing controls. Face selections are UI drafts until stored
as datum associations. Avoid tutorial text in product controls.

## 10. Placement changes and machining preparation

For workpiece-dependent machining, retain nominal geometry/settings in {W} with the workpiece/CAD
identity. At Generate, transform the analytical ellipse and cylinder inputs into {B}: transform
points with rotation and translation, directions with rotation only, and retain radii/lengths.
Then reuse `ChamferPath::create` and `ChamferMotion::create` unchanged in their base-relative role.
This is smaller than adding a placement wrapper to every trajectory evaluator.

Retain the native recipe alongside the generated motion. `SceneMachining::sourceParameters`
currently regenerates saved paths from their already base-relative parameters; a revision flag
alone would regenerate the old physical placement. Source deletion may retain the saved recipe,
as saved motion already retains its own geometry. Deleting/replacing its workpiece association
makes the motion incompatible; never attach it automatically to another part.
Reuse settings already owned by the immutable saved motion; the additional required snapshot is
native geometry and its workpiece/CAD association, not a second editable copy of every setting.

Choose an opening/material side explicitly in workpiece-local geometry. The current negative-scene-X
default for `flipAxis` cannot define a side for arbitrarily placed CAD.
Legacy measured JSON surfaces remain base-relative and are not automatically parented to {W}.
Registration moves nominal CAD rigidly; it does not deform CAD to match measured form or
automatically measure each machining edge.

For a machining TCP target, composition gives:

$$
{}^{B}A_T = {}^{B}A_W\,{}^{W}A_T,
\qquad
{}^{B}A_F = {}^{B}A_W\,{}^{W}A_T\,\left({}^{F}A_T\right)^{-1}
$$

Here the flange-to-tool transform is the accepted machining tool model, distinct from the probe.
The current KRL/RSI route still requires its documented robot-base/WORLD alignment and null BASE.
Do not set KUKA BASE to the part frame while leaving base-relative targets/World increments unchanged.
A noncoincident WORLD configuration requires a separate consistent conversion of all targets and
correction directions; this design does not silently claim it is already supported.

While no live run owns the workpiece/job, apply a candidate as one coupled operation:

1. Verify its CAD, datum, measurement-model and input revisions are still current; install the
   candidate drawing before publishing the accepted state. On failure keep the old accepted result.
2. Commit the placement/provenance and advance the workpiece revision if accepted inputs changed.
   An unchanged accepted value must not invalidate motion.
3. End dependent preview playback and mark dependent prepared probing/machining motions incompatible.
   Invalidate jobs before another start can use them; notify compatibility/availability through the
   existing preparation chain.
4. Remove stale job files and clear held correction tables through that chain. Reject outstanding
   Generate/Sampling results whose copied revisions no longer match.
5. Regenerate and resample from native intent/current placement before reuse. Never rotate saved
   joint angles, interpolate old poses into a new job or rotate an already sampled correction table.

Keep old captures and motion snapshots as historical data; do not move old base-relative diagnostic
paths with CAD and present them as recalculated motion. CAD replacement, datum changes, model/TCP
changes and route edits invalidate their respective dependent candidates/jobs.

Job-file invalidation must return a failure result if removal fails. Current `clearPrepared` only
logs a failed deletion and clears application state (`src/application/scene/scenemachiningpreparation.cpp:448-461`).
The proposed owner retains an unresolved stale-file condition, reports the path and prevents
dependent starts and further installation until that condition is resolved. Do not claim that a
controller-readable stale file was removed merely because its PC motion handle was cleared.

Clearing streams is not a controller stop. Refuse placement/definition/job replacement during an
owned live run, including KRL transfers between RSI searches. RSI Idle or an open socket is not
proof that robot execution finished. Extend explicit run ownership/completion beyond the existing
calibration-only state; a disconnect/unknown completion leaves the run unresolved and prevents
another dependent start/commit until the controller state is established.

## 11. Requirements still requiring physical/task data

The design fixes the calculations and state ownership. The following values cannot be derived
honestly from the example STEP or sphere fit:

- Required workpiece/machining position and orientation errors, relevant task locations, and
  independent reference quantities/uncertainties.
- Accepted probe-model evidence, usable direction/orientation coverage, and permissible actual
  normal mismatch for the class-specific compensation model.
- Contact spacing, useful spatial span, allowable form/angle disagreement and conditioning limits.
- Rough-placement bounds, boundary margins, search/overtravel/retract values and stopping budget.
- Fixture/vice geometry or explicit permissible transfer routes, and the scope of collision checks.
- Controller job-format capacities and a verified start/completion/recovery contract for workpiece jobs.

These are explicit inputs or commissioning results, not favorable defaults. Their absence does
not prevent offline construction/visualization, but does prevent claiming an executable, accurate
procedure for an arbitrary clamped part.

The subsequent implementation ExecPlan can turn this design into code milestones, with the affected
build/scene/trajectory/controller contracts updated when their behavior changes. This design step
adds no implementation, project tests, build targets or hardware activity.
