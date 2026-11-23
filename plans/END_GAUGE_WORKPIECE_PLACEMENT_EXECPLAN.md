# Place the GE-22 end gauge in the scene from nine rough touches

## Purpose / Big Picture

On entering Measuring / Calibration, the operator can see the actual GE-22 model alongside the sphere, select the gauge, and inspect the origin and axes of its workpiece frame {W} in the robot base frame {B}. The scene uses {B}. The application computes the pose when it reads the nine labelled ball centres from `end_gauge.json`. The supplied synthetic example places the geometric centre of the tilted gauge 100 mm along positive base X from the rough sphere centre, with the same base Y and Z. This specifies a centre-to-centre distance for the **debug scene**. Installed coordinates will come from observations of the mounted gauge.

Maintain this living plan in accordance with repository-root `PLANS.md` and `AGENTS.md`. The first deliverable ends at disconnected rough placement. The speedup plan consumes this placement to generate a measuring job. Automatic remeasurement of the three datums and saving a refined placement are a later increment.

## Progress

1) [x] 2026-11-23. Inspected the GE-22 STEP vertices, sphere file and fit, deployment, scene ownership, and CAD drawing adapter. Calculated the example readings below and independently recovered the corner from the rounded readings.
2) [x] 2026-11-23. Implemented the fixture reader and three-plane placement. Both changed C++ translation units passed compiler syntax checking. An independent numerical check reproduced the expected pose and exercised six rejection cases. Compiled application execution is pending.
3) [x] 2026-11-23. Implemented independent fixture deployment, the selectable GE-22 solid, workpiece axes and Properties rows. Verified conversion, deployment preservation, C++ syntax and QML parsing.
4) [ ] Build and run the updated application disconnected. Observe initial presentation, picking, re-entry, identical/changed/invalid/missing fixture input, and restoration after leaving Calibration.

## Surprises & Discoveries

The existing `resources/deploy/cad/fixtures/GE-22.stp` is a single closed six-face solid in millimetres. Its native bounds are X from 0 to 9, Y from −22 to 0, and Z from −35 to 0 mm. Its origin is already the intersection of three accessible supporting planes. Using a centred box or assuming Y runs from 0 to +22 would move the physical measuring faces.

`cmake/DeployOcct.cmake` discovers fixture STEP files recursively and converts them to BREP, the solid format the viewport reads. Before Milestone 2 its JSON deployment named only the sphere, and `DrawingModel` stored only a translation. The scene adapter now applies the full transform to the BREP.

## Decision Log

Milestone 2 implementation. A scene value holds the gauge placement and observation source, and a scene ID tracks the independently selectable solid across submode changes. Read the gauge into that value on reload and use the existing drawing style and line primitives for its axes. Both CAD adapters need the same rigid-transform conversion, so share the existing operation with the other OCCT part helpers.

2026-11-23, Milestone 1 implementation. Reuse the sphere reader's XYZ decoder, `Plane::fromPoints`, and the existing basis and transform operations. Add a GE-22 observation value indexed by its three native face normals, a fit result carrying the transform and diagnostic, and a file reader because the existing sphere and bounded-plane results cannot represent a three-face placement. A separate contact-patch check computes in-face spread, edge mismatch and whole-ball clearance from the recovered pose.

2026-11-23, user and plan author. Use an independent gauge file and scene object. The sphere and gauge can move independently. Only the authoring calculation for the synthetic example uses the sphere. Reloading the sphere or applying **Reassign** must leave previously measured gauge coordinates fixed in {B}.

2026-11-23, plan author. Use the existing GE-22 CAD axes as {W}. Use the top as the primary datum, the 22 × 35 mm face at X = 0 as secondary, and the 9 × 35 mm face at Y = 0 as tertiary. A datum is a face used to locate the part. The opposite end face at Y = −22 mm supplies an independent measurement in the speedup plan.

2026-11-23, plan author. Interpret the requested 100 mm as a positive base-X displacement between geometric centres. Use the reproducible rough sphere fit from the source fixture, independent of the operator's primary settings. The 4°, −3°, 2° rotation makes reversed signs, swapped axes and translation-only drawing visible.

## Outcomes & Retrospective

Both software milestones are implemented. Calibration entry fits the observations, logs the source, angles, spreads, edge mismatch and three matrix rows, and installs a GE-22 scene object. Its solid and three 15 mm axis strokes share one visibility and placement. A missing fixture removes the previous gauge. Malformed data also report the filename and reason. The current sphere-only motion does not consume the gauge placement. The disconnected visual checks in Progress remain open.

## Context and Orientation

`src/application/scene/scenecalibration.cpp` reads the deployed sphere on entry, replaces the sphere scene object and manages visibility through entry and exit. `SceneModel::addCalibrationObject` in `src/application/scene/scenemodel.cpp` currently maps a sphere to the sphere kind and everything else passed there to the calibration-path kind. `src/application/scene/sceneobject.{h,cpp}` owns the shape variant and constructs its drawing. `src/application/scene/scenegeometry.h` contains the sphere value. The Calibration list filter in `src/application/qml/Views/Workspace/ScenePanel.qml` explicitly admits two kinds. These are the integration points for a separately selectable gauge.

`Plane::fromPoints` in `src/geometry/plane.cpp` fits a plane through at least three noncollinear samples. It orients the largest component of the normal positively. The fixture reader must resolve which side faces free space before compensating for ball radius. `src/geometry/boundedplane.cpp` already implements a signed radius shift for imported planes. Reuse existing geometry operations and Eigen's linear solvers. Read the Build and modules, Viewport and CAD, Geometry and scene, and QML sections of `docs/code_context/AGENT_CONTRACTS.md` before editing the corresponding code. See `docs/studies/workpiece_reference/WORKPIECE_REFERENCE_SUMMARY.md` for the wider CAD workflow.

## Plan of Work

### Milestone 1. Recover the native CAD placement from the fixture

Add `resources/deploy/config/end_gauge.json`. Use a version-1 object containing `model: "GE-22"`, `source: "synthetic"` (or `"measured"` for installed observations), `ballRadiusMm: 1.5`, and `roughPoints`. Each point is an object with `face` equal to `"top"`, `"wide"` or `"end"` and finite numeric `X`, `Y`, `Z` in base millimetres. Coordinates denote ball centres at a touch. The radius is the one used for those observations. Model identity fixes the native bounds above. Use the nine base readings below as the default. Omit the expected pose and the sphere coordinates from the input file so the loader must solve for them.

Parse JSON at the application boundary. Put the numerical operation alongside `src/geometry/plane.cpp`, expose its declaration in `src/geometry/include/geometry/` and add the sources to the owning CMake target. Pass the three labelled groups and observation radius to the calculation. For this roughly upright installation the expected **outward** base directions are +Z on top, −X on wide, +Y on end. Normalize and flip each fitted normal toward its corresponding direction. For this fixture require the signed normal within 15° of its expected base direction, and each pair of fitted normals within 5° of 90°. These are proposed rough-input rejection limits for this mounting. Their role is to diagnose mislabelled or incompatible faces. They establish no measurement accuracy.

For each group compute the mean projection of its ball centres onto its outward unit normal. Subtract the ball radius from that scalar to obtain the material plane offset in mm. Solve the three simultaneous equations whose rows are the outward normals and whose right-hand sides are the three offsets. Their intersection gives the native GE-22 origin in {B}. For orientation, take the top outward normal as positive workpiece Z. Take the **negative** of the wide outward normal, remove its component along that Z, and normalize it to obtain positive X. Form positive Y by crossing Z with X and check its agreement with the end outward normal. The rotation columns are X, Y, Z in that order. Return the right-handed rigid transform matrix with that rotation and the intersection as its translation. Deriving all three axes from the measurements accommodates tilt about every base axis. Preserve the position of raw ball centres in {B}.

Reject malformed versions, unknown source or model, absent or unknown labels, fewer than three distinct noncollinear centres per face, nonfinite inputs, nonpositive radius, ill-conditioned intersection or invalid rotation. For the intersection use a rank-revealing solve on the three unit normals and reject a smallest-to-largest singular-value ratio below 0.1. Check projected contacts lie within the corresponding finite face to the input rounding tolerance for the synthetic example, and check the lower clamp exclusion in gauge coordinates. For side faces the entire ball stays above the bottom 8 mm (5 mm jaw height plus 3 mm margin), giving a maximum ball-centre depth of 25.5 mm with this ball. When checking measured data, report the fitted pair angles, contact spread and any edge mismatch separately from the numerical precision of the synthetic case. Three noncollinear points determine a plane exactly.

### Milestone 2. Display the fitted gauge on entry and reload

Extend the existing deploy-if-absent rule in `cmake/DeployOcct.cmake` to copy the two named JSON fixtures independently to the executable's `resources/json/`. Keep the installed observations when rebuilding. Use the existing STEP conversion and CAD asset directory for `GE-22.brep`.

On entering Calibration, load the gauge and sphere independently. Add an end-gauge kind and shape to the scene. Store the source and one calculated placement on the gauge. Give the solid its own ID, selection and fit bounds, and show three coloured positive workpiece axes from the native origin as nonselectable decorations. Show the source, origin in base mm and orientation in Properties, with enough digits to check the example below. For verification, expose the three rotation columns in Properties or the existing diagnostic output. Carry the full rigid placement into `DrawingModel` and apply it once to the loaded shape in `modelShape`. Reuse the Eigen-to-OCCT conversion already used by `src/viewport/adapters/occrobotadapter.cpp`. Preserve the stand's translated placement with identity rotation. Keep existing asynchronous model loading, selection and visibility behavior. A model load failure must surface its diagnostic.

Update submode visibility, scene list filtering, property kind labels, and the owning contracts. Leave and re-enter Calibration to reload the gauge. Identical input preserves the scene object. Missing input means no gauge. Present invalid input reports the gauge file and cause, removes the stale gauge and leaves a valid sphere available. Show neither duplicate objects nor orphan axes. When a later job depends on this placement, changing it or failing to reload it invalidates that job before another run. A synthetic fixture supplies preview data only for hardware purposes.

## Concrete Steps

From the repository root inspect `Get-Content resources/deploy/config/calibration_sphere.json`, `Get-Content resources/deploy/cad/fixtures/GE-22.stp`, and, after adding the fixture, run:

    (Get-Content -Raw resources/deploy/config/end_gauge.json | ConvertFrom-Json).roughPoints | Group-Object face
    git diff --check

Expect three entries per named face in the shipped fixture. Record affected physical/nonblank line counts and the required declaration/body/caller review in this plan. For an explicitly requested build, discover the matching installed Qt/compiler kit and verify the build cache belongs to this checkout. In PowerShell from the repository root with `$buildDir` set to that verified build directory, use `cmake --build $buildDir --target robocrap`. For requested QML lint use the applicable targets from `AGENTS.md`. Open the disconnected application, enter Measuring / Calibration and select **End gauge**. No socket or controller is needed to recover or inspect the placement.

## Validation and Acceptance

Use the six-decimal XYZ triples under Artifacts and Notes. The recovered origin must agree with the expected corner to 0.0001 mm and each rotation entry to 0.000001. Transform the CAD geometric centre and verify its base displacement from the reference sphere is (100, 0, 0) mm to 0.0001 mm per component. Check independently that the transformed centres of the two end faces are 22 mm apart and that the face at Y = −22 mm lies in the negative workpiece Y direction from the tertiary datum. Observe the solid and all three axes at the same tilt. The prior translation-only drawing would pass the origin check and fail the face and axis alignment.

In a disposable deployed copy, permute the order of readings within a face and re-enter. The recovered pose must agree within rounding. Replace a face's three points with collinear points, then separately exchange the top and end labels. Each must produce a diagnostic and remove the old gauge. Remove the test gauge file and re-enter to verify the sphere-only view. Restore the valid fixture and confirm exactly one gauge reappears. Check that a sphere centre change leaves the gauge at the absolute coordinates recorded in the file. Report separately which numerical, build and disconnected GUI checks were performed.

## Idempotence and Recovery

Re-entry recomputes placement from the current deployed inputs. Restore the original contents after editing a test deployment. An absent or invalid gauge cannot overwrite probe settings or move the sphere, table or stand. The later refined result will need its own settings entry tied to the same gauge and mounting. A remount requires new rough input.

## Artifacts and Notes

Milestone 2 verification (2026-11-23). The existing `tools/cadstep2brep` executable, run through `cmake/ConvertStep.cmake`, produced a 2,647-byte GE-22 BREP in a temporary directory. The fixture-copy statements from `cmake/DeployOcct.cmake` created both JSON files in an empty destination and preserved each edited fixture when redeploying the missing other file. The configured compiler accepted the five changed translation units with `-fsyntax-only`. Qt's `qmlformat` parsed both changed panels. Application linking, picking, actual rendering and runtime reload were not exercised.

Milestone 2 review. `scenegeometry.h:145` holds the matrix and source by value. `scenecalibration.cpp:270` returns the optional scene value and diagnostic together. At line 395 the reload operation takes one file path, retains the ID for identical source and matrix, clears missing or invalid data, and publishes a changed fixture only after insertion succeeds. `sceneobject.cpp:189` supplies the solid and nonselectable axes from the same matrix. `occsceneadapter.cpp:97` uses the same one-argument matrix converter as the robot adapter. The sphere stand caller supplies identity rotation. The two panels use the existing group and scrollable row layout. Across ten C++ files, physical/nonblank lines changed from 2,221/2,020 to 2,263/2,061. The two QML panels changed from 720/619 to 723/622. The deployment script stayed at 118/112. The additional scene value, ID, read-only accessor and reload method support an independent lifetime and source. Renderer loading, error reporting and selection reuse the existing model pipeline.

Milestone 1 verification (2026-11-23). An independent cross-product calculation on the shipped JSON gave origin (1148.00004117, −321.99069472, 471.88588472) mm and reproduced the +100 mm centre offset within 0.000002 mm per component. The largest rotation-entry difference from the expected matrix was 0.000000049. Reversing the input order preserved the pose, and the two end-face centres were 22 mm apart. Independent negative cases exercised a missing reading, a collinear triple, swapped top/end labels, nonperpendicular planes, a contact beyond an edge and a ball entering the clamp band. The configured MinGW compiler parsed `endgaugefit.cpp` and `scenecalibration.cpp` using the verified project flags with `-fsyntax-only`. These checks exercised the fixture numerically and checked the C++ types. The linked reader and GUI have yet to run.

Milestone 1 review. In `src/geometry/include/geometry/endgaugefit.h:24` and `:27`, the fit takes zero arguments and the private contact check takes two read-only matrices. Both return placement and diagnostic together. In `src/geometry/endgaugefit.cpp:14` the fit owns its three plane fits and matrix solve. At line 63 the contact check reads the observations and returns a failed result for invalid projected positions. In `src/application/scene/scenecalibration.cpp:270` the file reader owns the decoded points and reuses the sphere's XYZ decoder. The caller at line 356 handles missing, invalid, and fitted inputs independently of sphere replacement. No additional QObject, stored scene state, new dependency or test target was required.

Milestone 1 footprint, physical/nonblank lines. C++ changed from one existing file at 557/507 to three at 739/672, including two new value declarations and three operations (reader, fit, contact check). The Geometry build input grew from 20/20 to 22/22, the fixture from zero to 17/17, and the geometry contract from 508/491 to 513/496. Reusing a bounded plane alone would omit the corner solve, datum priority and cross-face extent checks. No QML or tooling changed.

For reproducibility, the ten source sphere readings and the centred least-squares calculation in `src/geometry/spherefit.cpp` yield a rough centre (1054.140313, −331.969235, 454.272658) mm. The target geometric centre is (1154.140313, −331.969235, 454.272658) mm. In the gauge's native frame its geometric centre is (4.5, −11, −17.5) mm. Rotate the CAD model first +2° about X, then −3° about Y, then +4° about Z, all about fixed axes. Set the translation to the target centre minus the rotated native centre. This gives the following expected transform, with the rotation given row by row:

    0.99619692  -0.07153603  -0.04974220   1148.000042
    0.06966087   0.99682895  -0.03846303   -321.990694
    0.05233596   0.03485167   0.99802120    471.885885
    0            0            0                1

The left three columns below are nominal material contact coordinates in {W}. To generate each right-hand triple, offset its material point 1.5 mm outward in the native frame and then apply the above pose at full precision. Only the face label and right-hand triple are stored in the JSON. Fitting the rounded triples gave the corner (1148.000041, −321.990695, 471.885885) mm in an independent offline calculation.

    face   material X   Y    Z       ball-centre X_B    Y_B          Z_B (mm)
    top         2      -4    0       1150.203966    -325.896382    473.348182
    top         7      -4    0       1155.184951    -325.548078    473.609862
    top         2     -18    0       1151.205471    -339.851988    472.860259
    wide        0      -4   -5       1147.040601    -325.890186    466.677869
    wide        0     -18   -5       1148.042106    -339.845791    466.189946
    wide        0      -4  -20       1147.786734    -325.313240    451.707551
    end         2       0   -5       1150.133842    -320.163814    467.052729
    end         7       0   -5       1155.114827    -319.815509    467.314409
    end         2       0  -20       1150.879975    -319.586868    452.082411

## Interfaces and Dependencies

Use Qt JSON, the project's `V3d` and `M4d` types, Eigen and the existing viewport. The geometry result carries either a finite rigid transform matrix or a specific error. The scene needs only a read-only gauge placement and source for `SceneCalibration::generate` to snapshot when the speedup milestone is implemented. That consumer derives top-face centre from native (4.5, −11, 0) mm and takes width, measuring-face normal and up from the three rotation columns. Face A is native Y = −22 mm and face B is native Y = 0. The 32 future touches measure those two ends independently.

Later RSI refinement will remeasure the three datum faces, reconstruct ball centres from recorded joint angles, compensate using the qualified probe for that orientation and feed, and recompute the same placement. Persist the accepted transform and mounting identity separately from `settings/probe_calibration.json`. Generate the dependent measurement job after acceptance. Registration touches and that separate job's controller budget belong to the later refinement design.

Revision note (2026-11-23). Created the rough-placement prerequisite to Milestone 2 of `plans/PROBE_CALIBRATION_SPEEDUP_EXECPLAN.md` and defined a reproducible +100 mm debug placement using the existing native CAD model.

Revision note (2026-11-23). Recorded Milestone 1 implementation, source review and numerical verification. The next step deploys the fixture and draws the GE-22 solid and axes using the fitted transform.

Revision note (2026-11-23). Implemented scene deployment and presentation, recorded the disconnected checks completed without building the application, and left interactive acceptance open.
