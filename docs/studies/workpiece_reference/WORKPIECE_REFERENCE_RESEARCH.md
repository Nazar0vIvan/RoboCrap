# Robotic CAM: workpiece frames and planar probing

Research date: 2026-11-19. This is step 3 of the investigation. The questions are whether robotic
computer-aided manufacturing (CAM) uses a workpiece coordinate system, how the real workpiece is
located relative to computer-aided design (CAD) geometry, and how the documented procedures compare
with the proposed rough-touch/refined-probing sequence. The supplied STEP is an example, not a
definition of supported part shapes or dimensions.
Workpiece datum surfaces are restricted to planes for this study; accessible regions may be
separated by holes, clamps or other obstructions. Plane orthogonality has not been assumed.

The [source record](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md) identifies editions, inspected
sections, links and limits. Source statements, mathematical consequences and project observations
are distinguished below. This is research, not an implementation ExecPlan or a robot operating procedure.

## 1. Answers to the two questions

**Question 1: is a workpiece frame used, or are only scanned surfaces machined in the robot base?**

Workpiece frames are explicitly supported by the inspected robot controllers and robotic CAM
systems. KUKA, ABB, RoboDK and SprutCAM document them and their practical use. A Cartesian target
can be programmed relative to the workpiece and subsequently converted into robot-base coordinates;
those two statements are compatible. The evidence establishes actual implementations, not the
market share of each method or a universal requirement that every robotic process store a named
workpiece frame. ([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01), [S02](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s02), [S03](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s03), [S06](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s06), [S07](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s07), [S08](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s08), [S09](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s09), [S10](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s10), [S11](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s11))

**Question 2: how is the frame found, and is the proposed procedure close?**

Documented methods include teaching an origin and two directions, identifying corresponding known
points, and probing geometric features to calculate a frame. RoboDK documents CAD-selected probing
followed by a measured part-frame correction. Renishaw documents a KUKA three-plane alignment macro.
These are direct industrial counterparts to important parts of the proposal. The sources do not
establish its complete seven steps as a universal sequence, its numerical accuracy on this cell,
or compatibility with the WP-500 probe and the current RobotSensorInterface (RSI) implementation. ([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01), [S03](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s03), [S09](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s09), [S10](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s10), [S17](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s17))

The most consequential qualifications are:

- Three measured planes do not, by themselves, specify the intended origin, axis signs and datum priority.
- Planar surfaces need not be nominally perpendicular. The inspected simple Renishaw routine requires this.
- A valid contact position does not establish a valid approach or transfer path.
- A small fitting residual does not establish machining accuracy at independent locations.
- Updating the displayed part pose does not establish that previously generated robot motion has been updated.

## 2. Terminology and the transform that is actually needed

**Registration** here means determining a rigid transform matrix relating corresponding geometry
in two coordinate systems. **Probe qualification** determines the probe quantities needed to convert
trigger observations into surface measurements. **Robot-model calibration** identifies corrections
to the robot's kinematic model. These are different operations; one does not automatically perform
the others. A published robot-machining study explicitly combines sensor/tool/model calibration
with a separate workpiece registration operation. Its case-specific accuracy is not transferred
to RoboCrap. ([S28](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s28); public abstract/preview only)

Use these frame names throughout this report:

- {B}: the robot's base frame, as in the project.
- {W}: the workpiece frame defining the nominal part geometry and machining task.
- {G}: the controller's WORLD frame, introduced only to distinguish it from {B}.
- {F}: the robot flange frame.
- {T}: the active tool centre point (TCP) frame, for either the probe or machining tool.
- {W_R} and {W_P}: rough and refined estimates of the same workpiece-frame definition.
  The label {W_P} does not certify a numerical accuracy.

A transform matrix from {i} to {j} is denoted as follows:

$$
{}^{j}A_i
$$

Its rotation is dimensionless and its translation is in millimetres (mm). For one TCP pose defined in {W},
the corresponding robot-base pose follows by matrix multiplication:

$$
{}^{B}A_T = {}^{B}A_W\,{}^{W}A_T
$$

If inverse kinematics requires a flange pose, the accepted flange-to-TCP transform is removed:

$$
{}^{B}A_F = {}^{B}A_W\,{}^{W}A_T\,\left({}^{F}A_T\right)^{-1}
$$

These are coordinate-transform identities for a fixed chosen pose, not a vendor-specific
probing algorithm. The active tool transform differs between the probe and the spindle.

### KUKA BASE is not the project's {B}

KUKA System Software (KSS) 8.3 defines `BASE` as the workpiece frame relative to `WORLD`, and `ROBROOT` as the robot
base relative to `WORLD`. A configured offset can separate WORLD and ROBROOT. Thus KUKA's
workpiece `BASE` commonly serves the role of {W}; the label is not synonymous with the project's
robot-base {B}. ([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01), section 4.13, p.65)

For a robot with a nonzero WORLD-to-robot-base placement, the required conversion is:

$$
{}^{B}A_W = \left({}^{G}A_B\right)^{-1}\,{}^{G}A_W
$$

The first input describes the robot's placement in WORLD; the second describes the workpiece's
placement in WORLD. Treating those frames as identical is a configuration assumption, not a
general property of robotic CAM.

ABB uses an additional hierarchy: targets refer to an object frame, that frame refers to a user
frame, and the ordinary fixed user frame refers to world. A fixture and a particular part can
therefore have separate frame ownership. A user frame driven by a mechanical unit is a distinct
case. ([S02](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s02))

## 3. What the industrial procedures actually do

### 3.1 Manual frame teaching and corresponding-point registration

KUKA's direct method records the TCP at the chosen origin, on positive X, and in the positive-Y
half of the XY plane. Its indirect method instead uses four accessible points with known
coordinates in the intended workpiece frame, including coordinates from CAD. The latter permits
an inaccessible origin. Neither operation means fitting three independently measured planes.
([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01), sections 5.10.3.1-5.10.3.3, pp.132-134)

ABB's `DefAccFrame` uses 3-10 corresponding position pairs and reports a frame plus fitting-error
information. `SearchL` captures a position when a signal condition is detected; the detection
position differs from the eventual stopped position. These facilities supply components of an
automated procedure, not an entire CAM application. ([S03](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s03), selected sections)

SprutCAM X 17 gives a robot setup procedure: teach three real points, select the same CAD points,
enter the resulting measured placement in Workpiece Setup, and associate the controller frame
number. The dedicated placement controls must be distinguished from its general workpiece coordinate
system (WCS) control, whose geometric coordinate system selector uses only the origin. ([S10](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s10), [S11](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s11))

**Measurement consequence:** native point-teaching methods require corresponding TCP positions.
An arbitrary contact on a plane is not an identifiable CAD point merely because the plane is
known. Furthermore, the probe-ball centre is displaced from the physical surface contact. Those
differences must be accounted for before supplying data to a corresponding-point procedure.
This follows from the input definitions above and the compensation distinction in S25.

### 3.2 Feature probing that determines a frame

RoboDK's documented add-in workflow starts with CAD at the origin of a dedicated part frame.
The operator selects surface contacts and sets probe and movement parameters. The software supports
three-plane alignment with at least three points per plane. In its driver-based mode, searches
return measured contacts; the software refits datums, compares the measured and nominal frames,
and offers a correction that moves the part reference. Renishaw execution styles instead export
controller macro calls and calculate alignment there. ([S09](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s09))

The page does not establish a mandatory manual rough stage, RSI transport, a full fitting objective,
uncertainty assessment or arbitrary-fixture avoidance. Its defaults and warning thresholds are not
justified RoboCrap settings. ([S09](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s09))

Renishaw's KUKA `RswAlgnPPP` uses measured data from three nominally orthogonal planes, with
distributed, noncollinear contacts. Plane order controls the result: the first establishes positive
Z and its origin component, the second positive X and its component, and the third the remaining
origin component. The result can be written to `BASE_DATA`. The inspected guide establishes no
compatibility with WP-500, KSS 8.3 or RSI 3.3. ([S17](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s17), pp.34-37)

Historical ABB SmarTac documentation independently illustrates searches that translate and rotate
an object frame, with programmed points following that frame. Its welding sensor and illustrated
two-dimensional plate setup are materially different from general three-plane probing. Summing
simple one-dimensional offsets is expressly limited to translation in that manual. ([S04](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s04))

### 3.3 Rough and refined stages

PC-DMIS documents initial manual measurements followed by automatic remeasurement in iterative
alignment, including target tolerances and checking subsequent feature access. This is evidence
for the rough-then-controlled-measurement pattern in metrology, not proof that its machine settings
apply to a robot. ([S21](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s21))

**Interpretation:** rough acquisition is one way to get a usable initial placement. Repeatable
fixturing, a previously taught frame or a different sensing system can supply that initial
placement instead. The inspected sources do not require a new rough three-plane measurement for
every part. They do require enough initial location information for the chosen measurement motions.
This conclusion combines the different documented entry procedures, rather than claiming a single
industry-wide operating sequence.

## 4. "Intersect the planes" leaves important choices unresolved

### 4.1 A common point is not a complete datum definition

Three independent supporting-plane equations determine one intersection point. The intended
CAD origin can instead be at an offset, in a hole or outside the reachable surface. Known-point
methods explicitly accommodate a virtual origin. Intersecting measured planes therefore need
not put the origin at the coordinate origin used by CAD. ([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01); geometric consequence)

The selected planes also need to constrain all three translations and all three rotations.
Pairwise nonparallel planes are not a sufficient mathematical test. For example, the hypothetical
planes X=0, Y=0 and X+Y=0 are pairwise nonparallel but share the entire Z axis; Z position remains
undetermined. The coefficient matrix whose rows contain the three plane normals has rank 2.
This example is a mathematical qualification of abbreviated documentation wording, not a verified
defect in a vendor's implementation.

Measured normals also need not be exactly perpendicular. Using all three raw normals directly
as the columns of a rotation matrix does not generally produce a rigid Cartesian frame. For
nominally nonorthogonal CAD planes, their normals are not supposed to be mutually perpendicular
even with perfect measurements. The inspected simple Renishaw PPP routine cannot be generalized
to that case merely by calling every surface a plane. Its nominal-orthogonality condition matters.

### 4.2 Datum precedence and global best fit are different choices

PC-DMIS documents a primary leveling datum, a remaining rotation constraint and origin constraints.
Its primary plane consumes two rotational freedoms; another datum fixes the third. This is why
a six-contact 3-2-1 construction is not the same calculation as three unconstrained plane fits
using at least nine contacts. ([S18](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s18))

An alignment minimizing errors across all surfaces can redistribute errors between those
surfaces. A datum-priority construction can protect the primary orientation while using subsequent
features only for remaining freedoms. PC-DMIS separately documents point least squares, vector
least squares and MinMax, so the phrase "best fit" does not identify a unique objective. ([S19](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s19))

It is also necessary to distinguish an average supporting plane from a functional datum plane.
PC-DMIS's primary-datum operation constructs an external plane tangent to material high points
after its initial fit. An ordinary least-squares plane through the observations does not
automatically represent that supporting contact condition. ([S20](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s20))

**Unresolved requirement:** does {W} represent an engineering CAD registration frame, a fixture/setup
frame, or the datum reference frame specified by a drawing? The answer determines permissible
fitting tradeoffs. This study does not claim ISO/ASME compliance: the full standards were not
read, and the ISO catalogue itself distinguishes datum specification from verification operators.
([S26](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s26))

## 5. Partially accessible planar surfaces

### 5.1 Supporting plane, material face and accessible region

These are three different inputs:

1. A supporting plane is the ideal geometry estimated from observations.
2. A finite CAD face describes where nominal material exists, including hole boundaries.
3. A usable probing region additionally depends on the vice, clamps, probe assembly, orientation
   and approach/retract route.

The weighted-fitting paper explicitly treats surfaces made of several patches. Separated patches
can contribute to one plane estimate; sampling one patch much more densely can give that patch
undue influence in an unweighted fit. It does not follow that an unseen region has the same form,
or that a particular choice of weights is justified. ([S23](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s23))

NIST explains why sampling and surface form affect the answer even with a perfect measuring
machine, and why geometry can amplify point errors. More contacts in one small patch do not
replace spatial extent or eliminate shared systematic errors. Fixture distortion and slip are
also measurement influences. These are mechanisms, not an uncertainty estimate for this cell.
([S22](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s22))

### 5.2 Contact locations and transfer paths need separate information

PowerInspect permits user-selected contact positions and separate intermediate, non-contact
positions for clearance and head rotation. Its scanning methods require suitable scanning heads;
they are not interchangeable with discrete touch-trigger searches. SprutCAM likewise exposes
probing transitions and specific protected-cycle geometry. These documented controls do not
prove automatic recognition of an unknown vice. ([S12](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s12), [S15](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s15))

For the proposed WP-500 procedure, "scanning" should therefore identify whether it means repeated
individual touches or a continuous-contact measurement. The present research establishes an
industrial basis for the former; it does not establish the latter for WP-500.

**Geometric consequence:** a route connecting valid points can cross a hole or obstruction.
A rough placement also moves the expected contact relative to the actual search line. Its error
must be compatible with both the finite search stroke and a contact region away from unwanted
surfaces; successful rough fitting alone does not establish those conditions. No inspected source
supplies universal approach, overtravel or clearance values for this project.

## 6. Measurement compensation and evidence of accuracy

### 6.1 State what each measured position represents

For an ideal spherical tip contacting a plane, the ball centre is displaced from the surface
point by the tip radius along the plane normal, with sign determined by the material side.
Compensation along commanded travel is equivalent only when that travel is normal to the plane.
This is a geometric result; PC-DMIS explicitly distinguishes surface-point fitting from fitting
probe-centre data with compensation during fitting. ([S25](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s25))

Trigger detection introduces further effects. The TP7 guide distinguishes pretravel, average
qualification compensation and directional variation, and discusses matching qualification and
measurement speeds. These facts establish mechanisms for that probe; TP7 numerical specifications
and correction capabilities are not specifications for WP-500. ([S24](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s24))

The measurement record consequently needs an unambiguous distinction between actual joint angles,
calculated ball-centre positions and compensated surface points, including the tool model and
frame used. A future algorithm cannot safely infer those meanings from three XYZ columns.
This is an inference from the different measurement definitions, not an existing file-format proposal.

### 6.2 Residuals do not establish downstream machining accuracy

NISTIR 8300 distinguishes error at points used to calculate registration from error at independent
targets. It warns that registration-fit error can give false assurance. Its experiments also show
different actual tool locations for the same commanded Cartesian point in different robot
configurations. These observations do not predict the size of errors for KR10, but they reject
an assumption that one rigid frame automatically removes all robot pose errors. ([S27](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s27))

A separate mathematical example shows the effect of orientation error: at a hypothetical point
100 mm from the error's rotation axis, an orientation error of 0.1 degree produces displacement
2 × 100 × sin(0.1 degree / 2) = 0.17453 mm. Those are illustrative inputs, not measured project
errors. A small residual near a measured patch can coexist with a material error farther away.

Determining a workpiece frame, qualifying the measuring probe and qualifying the machining TCP
therefore have different results and evidence. Independent checks need to represent the part
region and robot/tool configurations where the result will be used. The inspected sources do
not establish a machining tolerance achievable by the proposed workflow on this hardware.

**Metrological consequence:** accuracy checks also require reference values for the assessed
quantities, from independently known geometry or comparison measurements with stated uncertainty. Additional held-out touches
using the same robot/probe chain can miss shared systematic errors. Merely reserving some touches
from a fit does not make those touches an independent metrological reference. ([S27](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s27))

## 7. Current project facts checked for this research

The 2026-11-17 baseline is historical. The current code now implements `fitProbeCalibration`:
eight calibration parameters, repeatability statistics, and verification that refits only the
sphere centre while keeping the flange-relative ruby centre and contact distances fixed.
The verification statistic chi95 is the empirical 95th percentile of absolute verification
residuals, in mm. Successful run completion displays and writes the result. That completion
path does not apply the result as the measuring TCP. This was checked against
`src/trajectories/probe/probecalibrationfit.cpp:117-265` and
`src/application/scene/scenemachiningpreparation.cpp:568-590`, not inferred from the old baseline.

The current run ExecPlan reports a user run with chi95=0.1107 mm and 23 of 36 residual groups
outside their diagnostic bound (`plans/PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md:292-305`). This
research did not repeat the run or independently reprocess its captures. That value is a sphere
verification statistic, not a full Cartesian error bound, a workpiece-registration tolerance,
or proof of a machining capability. The diagnostic result gives a reason to retain model-error
questions, rather than declaring calibration accuracy adequate for an unspecified task.

The current machining contract still exports robot-base poses and uses null KUKA BASE with
World-relative RSI corrections (`controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md:22-33`).
Changing `$BASE` alone would not redefine the PC's already prepared correction coordinates.
The relationship between part placement, controller frame and RSI correction frame must remain
consistent; this is a consequence of the inspected contract, not a proposed controller edit.

Saved paths own immutable geometry/model/tool/settings snapshots
(`docs/code context/AGENT_CONTRACTS.md:326-334`). A part-frame correction must therefore be
distinguished from a new robot-motion calculation. The external CAM sources describe program
update operations; they do not prove that an old exported controller job is automatically safe
after a new measured placement. ([S08](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s08), [S13](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s13), [S16](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s16))

## 8. Comparison with the proposed seven steps

| Proposed step | Evidence and qualification |
| --- | --- |
| 1. Rough T1 contacts on datum planes | A plausible initial-location method. KUKA teaching and CMM manual initial alignment provide related entry procedures, but arbitrary plane contacts are not native three-point frame teaching. ([S01](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s01), [S21](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s21)) |
| 2. Intersect rough planes to obtain {W_R} | Plane-based registration is documented. Datum roles, axis directions, nominal offsets and sufficient independent constraints remain necessary; a common point alone is insufficient. ([S17](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s17), [S18](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s18), [S19](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s19), [S20](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s20); geometric deductions in section 4) |
| 3. Add {W_R} to the scene | Dedicated part frames and CAD ownership are documented CAM concepts. Rough and refined estimates should preserve the chosen meaning of the same nominal {W}. ([S07](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s07), [S10](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s10); interpretation) |
| 4. Import CAD with matching coordinates | Consistent CAD/toolpath coordinates and placement are documented. Loading CAD is useful for geometric probing but is not mandatory for every robot-machining job. ([S07](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s07), [S08](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s08)) |
| 5. Select planes and generate probing motion | CAD-selected contact positions and probing programs are documented. Point selection, probe orientation, search strokes and transfer geometry are additional inputs. ([S12](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s12), [S15](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s15)) |
| 6. Probe and calculate {W_P} | Measured feature alignment and refinement exist. Simple routines have geometric assumptions and product-specific execution requirements; no universal accuracy follows. ([S17](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s17), [S21](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s21)) |
| 7. Replace estimated placement with {W_P} | Measured frame correction is documented. Controller-side offset updates, CAM-scene changes and regeneration of prepared robot motion are distinct operations. ([S04](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s04), [S12](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s12), [S16](WORKPIECE_REFERENCE_RESEARCH_SOURCES.md#s16)) |

The proposal has a documented industrial basis. The research does not justify treating it as
a complete specification or as the sole normal CAM workflow. It also does not justify forcing
all parts into the rectangular example's face arrangement or origin convention.

## 9. What is established, and what must remain open

Established by the inspected evidence:

- Workpiece frames and measurement-based frame identification are real controller/CAM facilities.
- CAD-guided discrete probing and measured part-frame correction have direct documented examples.
- An inaccessible origin and separated measured planar patches are compatible with appropriate
  geometric registration methods.
- Datums, probe compensation, point distribution and independent checking materially affect the result.

Not established for this project:

- A justified alignment objective for arbitrary nominal plane angles and imperfect real surfaces.
- Whether the intended {W} follows selected CAD features, a setup convention or drawing datum precedence.
- Quantitative rough-placement limits, permitted transfer routes and acceptance thresholds.
- A numerical accuracy requirement or evidence that the complete probe/robot/tool chain meets it.
- Drop-in use of the cited commercial macro systems with KSS 8.3, RSI 3.3 and WP-500.
- Automatic avoidance of unknown fixtures, continuous-contact WP-500 scanning or automatic reuse
  of an old sampled machining job after relocation.

The source set is representative and selected for explicit procedures. It is not an exhaustive
market survey, a vendor benchmark or proof that undocumented features are absent. CMM procedures
are labeled as analogies; historical controller manuals and public abstracts are not presented
as current robot product guarantees. Selecting an implementation belongs to the subsequent
design step, using these established facts and unresolved requirements.
