# Locating a CAD workpiece in the robot base

## Research objective and findings

The November 2026 study asked how robotic computer-aided manufacturing (CAM) systems place a
workpiece model in a robot scene, and whether RoboCrap could locate a clamped part by touching
three planar datums. A datum here is a selected geometric reference used to locate the part.

1. **Do CAM systems use a workpiece frame?** Yes. RoboDK and SprutCAM document a workpiece
   frame for nominal geometry and machining targets. A target can be defined once in workpiece
   coordinates and converted to robot-base coordinates for execution. RoboCrap's existing method
   of machining an edge reconstructed directly from measured surfaces is another valid workflow.
2. **How is the frame found, and how close is the proposal?** Documented methods include
   teaching an origin and two directions, matching known physical points to CAD points, and
   probing geometric features. SprutCAM teaches three identifiable points on the real part and
   selects the same points on the CAD model. RoboDK's driver-based probing is the closest match
   to the proposed refinement. The operator selects contacts on CAD, the software fits datums
   from the reported contacts, compares the measured frame with the nominal frame and offers to
   move the part frame. [SprutCAM workpiece setup](https://kb.sprutcam.com/docs/SprutCAM/17/en/10262.html) · [RoboDK probing](https://robodk.com/doc/en/Addin-Probing.html)

## Problem and project status

The physical part has an unknown position and orientation in robot base frame {B}. The STEP file
defines nominal geometry in its native workpiece frame {W}. Registration must determine the rigid
transform matrix from {W} to {B}. The rough and refined results are two estimates of that same
placement. Probe qualification answers a different question: where the ruby ball centre is
relative to the robot flange and how a trigger relates to the material surface.

The inspected `BlankHoles.stp` illustrates why the distinction matters. The block measures
84.5 × 49.5 × 29 mm, with six Ø18 mm through-holes. Its CAD origin is at the centre of the bottom
footprint. The top, front and left planes meet at (−42.25, −24.75, 29) mm in {W}. From that corner,
the nominal origin is 42.25 mm along positive CAD X, 24.75 mm along positive CAD Y and 29 mm along
negative CAD Z. Putting the CAD origin at the touched planes' intersection would misplace every
hole. Using the nominal 29 mm offset locates the CAD origin relative to the measured top. Only a
separate measurement could establish the physical bottom or the actual thickness. [STEP inspection](WORKPIECE_STEP_INSPECTION.md)

**Implemented as of 2026-11-22.** RoboCrap fits a plane and cylinder from probe-point JSON in {B},
intersects them and generates a hole-chamfer trajectory. The runtime CAD worker reads BREP files.
STEP conversion is a build tool. The KUKA KR 10 and WP-500 touch probe already support automated
reference-sphere measurements. Since the design was written, **Reassign** can apply the fitted
ruby centre and save the sphere centre, both orientation-specific contact distances and the result
ID as primary settings. Preparation also reports failed removal of stale job files and refuses a
new sampling request on that error. Runtime workpiece STEP import, associations between datum
faces and touches, and workpiece registration remain proposed. [Application workflow](../../code_context/APP_DESCRIPTION.md) · [Calibration settings](../../../src/application/scene/scenecalibration.cpp) · [Job preparation](../../../src/application/scene/scenemachiningpreparation.cpp)

## How the proposed workflow locates the clamped workpiece

### Rough placement from manual touches

RoboCrap proposes fitting three planes from manual contacts to obtain an initial placement.
The operator may collect the rough readings before CAD import by labeling the physical face
and probing side for each touch. Placing the model requires matching those faces to CAD. Hexagon PC-DMIS
provides a precedent on coordinate measuring machines for manual location followed by automatic
remeasurement. The choice to acquire stopped robot joint angles on three faces comes from the
RoboCrap design. [PC-DMIS iterative alignment](https://docs.hexagonmi.com/pcdmis/2023.1/en/helpcenter/mergedProjects/core/18_alignment_topics/Point_Target_Radius.htm) · [Project design, §§3–5](WORKPIECE_REFERENCE_DESIGN.md)

1. **Import and identify the references.** Keep the CAD origin, axes and internal placements,
   converting file lengths to millimetres. Select primary, secondary and tertiary datum groups,
   associate each with one or more coplanar CAD faces, and identify the side facing free space.
   Bind the faces to this imported model so that moving the part preserves their identity and
   replacing the CAD requires reassociation. Nominal planes may meet at oblique angles.
2. **Measure the three physical planes.** In manual reduced-speed mode (T1), the operator jogs
   the probe into contact at several positions on each selected face. At every stopped touch,
   the operator records the six actual joint angles, the datum identity and the probing side.
   The application takes these readings by entry or import. Forward kinematics and the
   flange-relative ruby centre give each ball-centre position in {B}. Fit a least-squares plane
   through at least three noncollinear centres per datum (nine touches minimum for three independent
   fits). Orient its normal toward free space and shift the centre plane toward material by the
   nominal 1.5 mm ball radius. A manually held touch has different timing from a trigger captured
   during controlled travel. The resulting placement is an initial estimate. Its uncertainty must
   fit within the planned search travel and clearance margins before automatic probing.
3. **Register the same three datums in CAD and on the part.** In each frame, solve the three signed
   plane equations for their virtual intersection. Require three independent normals and check
   conditioning: nearly dependent normals make the intersection sensitive to small measurement
   errors. Use the primary outward normal for the temporary Z axis. Project the secondary outward
   normal onto the primary plane and normalize the projection for X. Form Y by crossing Z with X.
   Place the temporary origin at the intersection. This preserves the primary orientation and
   the primary/secondary intersection line. The tertiary plane fixes position along that line.
   A global best fit could tilt the primary plane to reduce error on the other two, contrary to
   the user's chosen datum priority. Report any remaining face-angle disagreement. This engineering
   convention uses least-squares planes and makes no claim of simulating physical datum contact.

   Let {Dₙ} and {Dₘ} be the temporary frames constructed from the nominal and measured triples,
   respectively. Assign the same coordinates to corresponding points in these frames. The identity
   matrix in the middle expresses this correspondence. The inverse nominal transform accounts for
   the offset and orientation of the selected datums in the original file.

   $$
   {}^{B}A_W = {}^{B}A_{D_m}\,{}^{D_m}A_{D_n}\,\left({}^{W}A_{D_n}\right)^{-1}
   $$

   In physical terms, rotate the CAD model until its temporary datum axes align with the measured
   axes, then translate it until the two virtual intersections coincide. On the example block,
   that operation places the top/front/left corner at the measured intersection and carries the
   bottom-centre CAD origin and all six holes with it. Accepting this first estimate places the
   model in the scene and supplies the expected face locations for automatic probing.

### Refined placement from automatic probe triggers

RoboDK documents three-plane probing with at least three contacts per plane and uses the first
plane's normal for Z. Renishaw's KUKA three-plane macro also assigns the planes ordered roles and
requires nominally perpendicular planes. Projecting the secondary normal and admitting independent
oblique planes are choices specified by RoboCrap's design. [RoboDK probing](https://robodk.com/doc/en/Addin-Probing.html) · [Renishaw KUKA guide, §2.7.3](https://www.renishaw.com/resourcecentre/download/user-guide-rcs-p-series-macro-instructions-for-kuka-robot-controllers--140555?userLanguage=en) · [Project design, §5](WORKPIECE_REFERENCE_DESIGN.md)

1. **Plan discrete touches from the accepted rough placement.** Select and spread contacts over
   usable patches on each finite CAD face. Exclude holes, edges within the required margin and
   areas blocked by the vice. Several disconnected patches can measure one supporting plane.
   Plan the approach, bounded search, retract and transfers separately: on the example block a
   straight line between two valid top-face contacts crosses a hole. Check the complete search
   and retract envelope, reach and fixture clearances. For each selected CAD contact, use the rough
   placement to express its position and outward face normal in {B}. Offset the contact outward
   by the effective probe radius to predict the ball-centre position at trigger. Select a qualified
   probe orientation in {B} and a start position farther out along that normal.
2. **Remeasure the same faces.** The robot searches inward from each start at the qualified feed
   over a bounded distance, then retracts before transferring to the next start. Use more than
   three distributed touches per datum to give the plane fit redundant observations. Reconstruct
   each ball centre from the six actual joint angles in the first valid RobotSensorInterface (RSI)
   packet reporting contact for that touch. Signal and sampling delays can separate that reading
   from the instant the ruby first meets the material. Fit one plane to the new centres for
   each datum and shift it inward by the effective radius for that probe orientation. This radius
   equals the fitted sphere-contact distance minus the reference-sphere radius
   (nominally 12.5 mm). RoboCrap proposes using this sphere-derived offset for near-normal searches
   under comparable orientation, feed and approach conditions. Its accuracy on a workpiece face
   still requires measurement. Compare the fitted outward normal with the direction opposite
   the inward search. Excessive angular disagreement calls for a newly planned pass. A missed
   trigger supplies no point. A partial or failed run preserves its observations and the last
   accepted placement. [Project design, §4](WORKPIECE_REFERENCE_DESIGN.md) · [Existing sphere touch record](../../code_context/RECORDS_FORMAT.md#touchescsv-and-residual_setscsv)
3. **Recalculate the placement and update the scene.** Construct a new temporary datum frame from
   the three compensated planes using the same intersection and axis rules as in the rough stage.
   Match the original CAD datum frame to this new measured frame. The result is a fresh estimate
   of the position and orientation of {W} in {B}. Compare the two placements at the CAD origin
   and at the machining features, and assess the measurements against the criteria below. Require
   confirmed normal completion of the same job because even the last trigger can precede a retract
   failure. The proposed controller echoes the job identifier with each touch and the final outcome.
   On acceptance, the refined placement replaces the rough placement of the displayed CAD model.
   The physical workpiece stays clamped throughout both stages.
4. **Regenerate dependent motion.** Store one active placement, keep raw observations
   fixed in {B}, and retain nominal machining geometry in {W}. A changed placement invalidates
   dependent robot trajectories, exported jobs and sampled corrections. Transform the original
   geometry into {B}, solve the robot motion again and resample. Old joint angles describe the
   old physical placement. The existing controller convention uses null KUKA `$BASE`, WORLD-relative
   RSI corrections and requires {B} to coincide with WORLD. Applying the
   part transform on the PC and again through `$BASE` would place the targets twice. [Controller frame contract](../../../controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md#frames)

## What would establish accuracy

Report the spread of contacts, root mean square and maximum point-to-plane residuals, conditioning,
nominal versus measured plane angles, and displacement at the actual machining locations. Three
independent planes intersect exactly even when their fitted offsets are wrong. The sphere check
measures residuals on that sphere with the same robot and probe. Neither result bounds the error
at a hole rim. Acceptance needs a specified position and orientation tolerance at the relevant
locations and independent reference measurements with stated uncertainty there.

The study leaves the allowable search-direction mismatch, probe coverage, contact spacing,
rough-placement error, clearance and stopping distances, fixture routes and controller
completion/recovery checks to task data or commissioning. A computable placement supports
offline inspection. The study establishes no machining accuracy or validated robot probing job
for the clamped block.

## Sources and what each establishes

| Source | Evidence used in the decision |
| --- | --- |
| KUKA KSS 8.3 SI V4, §§4.13, 5.10.3, and ABB RAPID, `DefAccFrame` | KUKA distinguishes workpiece BASE from robot base ROBROOT. Teaching an origin, positive X and the positive-Y half-plane requires identified locations. KUKA also permits four known points for an inaccessible origin. ABB accepts corresponding point pairs. An arbitrary touch somewhere on a known plane supplies different information. [KUKA source record](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01) · [ABB manual](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s03) |
| RoboDK Probing Add-in and SprutCAM X 17 Workpiece Setup | RoboDK measures CAD-selected features and can apply a part-frame correction. SprutCAM pairs three taught physical points with the same CAD points. These are documented alternatives for locating a part. [RoboDK](https://robodk.com/doc/en/Addin-Probing.html) · [SprutCAM](https://kb.sprutcam.com/docs/SprutCAM/17/en/10262.html) |
| Renishaw RCS P-series for KUKA, pp. 34–37 | Its ordered three-plane macro can write a workpiece BASE. It requires nominally perpendicular planes. The general-angle solver above is a project choice. Compatibility of the commercial macro with WP-500, KSS 8.3 and RSI 3.3 was never established. [Renishaw guide](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s17) |
| Hexagon PC-DMIS and Autodesk PowerInspect | Inspection software documents datum precedence, manual location followed by automatic remeasurement, and clearance positions between touches. These are metrology precedents. [Datum construction](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s20) · [Iterative alignment](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s21) · [Transfer positions](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s15) |
| NISTIR 8300 and Shakarji & Srinivasan | Registration error at independent targets can differ from fit residuals. Uneven sampling across patches can bias a plane fit toward the denser patch. [NIST registration study](https://www.nist.gov/publications/improving-3d-vision-robot-registration-assembly-tasks) · [Plane-fitting paper](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s23) |

The industrial evidence comes from documented procedures. The study did not run commercial
software or measure registration accuracy on the clamped part. For a source audit or implementation
detail, see the [28-source record](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md), [research comparison](WORKPIECE_REFERENCE_RESEARCH.md)
and [full proposed design](WORKPIECE_REFERENCE_DESIGN.md).
