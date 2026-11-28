# RoboCrap — subsystem contracts

Area-specific requirements referenced by [AGENTS.md](../../AGENTS.md). Read the sections required for
the task before changing behavior; verify implementation claims against current code/callers. User
instructions and the root mandatory rules still apply. Hardware, design notes and open questions are
in [ARCHITECTURE.md](ARCHITECTURE.md); this file states only constraints.

## Build and modules

- Root CMakeLists.txt owns project/options, dependency discovery, target subdirectories and
  installation. src/application/CMakeLists.txt declares robocrap and owns all application
  C++/QML/resources; it links Geometry, Kinematics, Trajectories, Devices, Viewport and the existing
  Qt/QML dependencies. RSI XML uses QtCore. Application has no public include directory; no target
  uses the legacy src include directory.
- RoboCrapGeometry (surfaces/math), RoboCrapKinematics (KR10 numerics) and RoboCrapTrajectories
  (animation and RSI sampling) are owned by src/{geometry,kinematics,trajectories}/CMakeLists.txt.
  Kinematics publicly links Geometry; Trajectories publicly links Kinematics. Each library exports only
  include/<capability>; Geometry also exports Eigen.
- RoboCrapDevices (src/devices, including src/devices/rsi) links QtNetwork, privately ws2_32 for the
  PLC RST close (`SO_LINGER`), and publicly links Geometry only for the `QVector<QVector<V6d>>` RSI
  stream table; no
  Trajectories dependency, QML registration or QQmlPropertyMap; its metatype metadata is extracted for
  consumers. DeviceHub, DeviceRunner and DeviceProfileModel are private Application code.
  PlcMessageManager is a Q_GADGET value held by PlcDevice, with dictionary APIs and a published
  header for the protocol enums; it has no QML registration.
- RoboCrap3D (src/viewport, URI RoboCrap.Viewport3D) requires bundled OCCT 8.0.0 and a Windows
  64-bit MinGW Qt kit; it publicly links Geometry and QtCore/Gui/Qml, privately OCCT, and has no
  Application/Trajectories dependency. It exports only include/viewport, including the singleton
  wrapper used by Application's instance wiring.
- tools/cadstep2brep is the only target linking TKDESTEP. robocrap_deploy_occt (cmake/DeployOcct.cmake)
  converts each resources/deploy/cad/**/*.stp through cmake/ConvertStep.cmake into
  <build>/generated/cad/<name>.brep only when inputs are newer; a failed conversion leaves no BREP.
  robocrap depends on that target and lists the BREPs in LINK_DEPENDS, so a changed BREP relinks it
  and reruns the POST_BUILD deployment. No STEP file is deployed.
- Saved motions expose central-motion/arc/path values; keep those published headers until consumers
  and storage change separately.
- Preserve URIs robocrap_qml_module, RoboCrap.Viewport3D, Components and Styles. The retired
  RoboCrap.Backend element names are registered by Application's backendqmltypes.h. Main uses App;
  other QML may keep Backend as the application-URI alias. RDTResponse stays the same queued
  Q_GADGET, with rdtResponse registered by an Application foreign wrapper.
- Application singletons (Machining, MachiningPreparation, Playback, Calibration, EndEffectors, Scene,
  Hub, WorkspaceController) return main()-owned instances through create() and claimQmlSingleton:
  qFatal on a null instance, wrong thread or second live engine, then CppOwnership.
  QQmlEngine::setExternalSingletonInstance (Qt 6.12) is unavailable at the Qt 6.8 minimum.
  RobotController is not a QML type. main constructs ScenePlayback after the document and before
  machining and calibration, and the workspace after its document, machining and end-effector owners.
  Add no diagnostic context properties.
- Both hot-reload branches declare the same application module; QML_OPTIONS holds build flags only.
  Application QML/config live in src/application/qml; paths relative to Application keep the qml/...
  resource paths and the ON branch's /qml/config.json. Root hot_reload_config.json selects
  src/application/qml/Main.qml and mirrors src/application/qml; keep its qml entry (Main's cached
  qml/Modules import path) and the Components/Styles mappings. Reusable modules stay in qml/Modules.
  robocrap outputs to the build root (plus the configuration suffix); preserve explicit
  CMAKE_RUNTIME_OUTPUT_DIRECTORY overrides.
- Application, the executable's declaring directory, attaches the OCCT POST_BUILD deployment helper;
  root owns deployment setup and installation. Deployed CAD/resources stay beside the executable.
  File/type moves require owning source-list/import updates; asset moves require resource-URL updates.
- Source layout: resources/qrc/ holds the compiled resources (icons.qrc → qrc:/icons/, fonts.qrc →
  qrc:/fonts/, config.qrc → :/config/db.json); resources/deploy/ holds what the deployment copies
  beside the executable (cad/robot/kr10 BREPs, cad/**/*.stp converted to BREP); controller/
  holds KR C4 files (krl/ programs, krl/lib/ shared modules, krl/template/, rsi_config/, read-only
  snapshots/<date>/ copies of controller files); data/ holds input data that the application does not
  produce (probe point files, workpiece models). The executable-side layout (resources/cad and
  resources/occt) does not change with the source layout.
- `ROBOCRAP_RECORDS_DIR` is the CMake cache path for records (default `<repository>/records`,
  git-ignored). `SessionRecorder` writes a folder per recorded run. Manual FTS logs go directly
  under the records root. See [RECORDS_FORMAT.md](RECORDS_FORMAT.md).
- The probe settings file probe_calibration.json (primary ball centre in {F}, sphere centre in {B},
  contact distances, last calibration result) is in the root cache path ROBOCRAP_SETTINGS_DIR
  (default <repository>/settings, tracked by git so the values move between computers), passed to
  every target as a compile definition; only SceneCalibration reads and writes it. The same directory
  is the sole source of rough fixture observations (`SPH25_rough.json` and `EG22_rough.json`).
- SceneModel restores Refine Job definitions from version 1 of `planar_probe_scans.json` in the
  same settings directory. Hold the file lock across read, comparison of the previous UUID record
  and atomic replacement. A conflict, read, format, lock or write failure preserves the file and draft.

## Viewport and CAD

- The public controller header defines concrete drawing values without OCCT math types or
  scene/machining objects. SceneObject supplies analytic patches/curves/spheres, saved FK TCP
  phase/segment polylines and bounded diagnostics; SceneMachining supplies the normalized signed draft
  axis and length. Preserve 256 markers/128 normal positions, 64 ellipse markers and one edge-centre
  normal. SceneModel requests drawing acceptance before publishing; failed installation keeps the old
  object and accepted drawing cache. main attaches the empty document first.
- OccController owns the accepted DrawingDocument and DrawingPose; its window borrows both read-only
  and is destroyed first. OccController calls the window's OccViewport directly (looked up per call,
  null without a native surface) for document, selection, draft-axis and spindle-frame updates and
  re-applies selection after insert/remove/visibility changes. Replay follows stable ID order, restores
  visibility/diagnostics/selection/draft axis, never reallocates IDs, and needs no application QObject
  pointer, curve resampling or FK.
- RobotController supplies DrawingPose with BASE-relative link, tool (spindle model), measuringTool
  (probe model) and composed TCP/collet matrices;
  renderer axis setters take those absolute frames and compose no tool transforms. OccRobotAdapter
  converts matrices to gp_Trsf; OccScene/OccPart keep OCCT types. OccPart applies style/transform at
  creation and keeps its AIS shape/selection/topmost state. World/TCP/ER axes are separate
  OccWorldAxes objects.
- OccScene stores only live parts keyed by PartId; its optional increasing counter starts at zero,
  permits the final representable ID, stays exhausted afterward and is never reset by removal. Erase a
  part only after OCCT removal succeeds; OccSceneAdapter keeps the IDs of application-object parts whose
  removal threw, retries them at its next removal and reports failures through OccController::message.
  Track selected visible part IDs (curves included) and keep
  unchanged outlines; equal application IDs can resolve to hidden/replaced parts. Keep unfinished
  outline handles/completion state through external failures; remove selection before hiding/deleting.
  Preserve selection styling and decorative picking/fit exclusion. A part without face boundaries
  (DrawingSphere: seam and pole edges not drawn) gets no outline; a hidden DrawingPart is not
  installed; a transparent part gets its own Selected/LocalDynamic style copies with its transparency.
- RobotViewport.qml hosts a native QWindow; no Qt Quick item draws over it; busy state goes to the
  status bar (docs/OCCT_SCENE_OVERLAY.md records the verified overlay method). CadLoadWorker parses
  CAD; robot state, QML properties and OCCT presentation stay on the GUI thread. viewportReady means
  viewer initialization; ready means robot-preview readiness for kinematics.
- main.cpp requests the single robot load once after console wiring; QML recreation does not. There
  is no robot replacement or link deletion. Missing startup graphics data means wait, not clear. A
  failed link/tool insertion or initial HOME transform reports its stage and removes only that
  installation's parts; readiness requires complete success including both end-effector parts.
  Cleanup keeps part IDs until removal succeeds and reports failures without retrying.
- After initialization, a failed playback transform reports a console error and reapplies the last
  accepted matrices once to existing links, tools and axes, without deleting/recreating links,
  reloading CAD or running FK/IK; reject the candidate and stop playback without advancing elapsed
  time/phase. If restoration fails, report it and disable robot readiness; never claim restoration.
- Native-surface recreation may install cached graphics in the new empty scene without reloading
  assets or republishing the model; it is not movement-error recovery. Preserve application
  presentations. End-effector models are fixed:
  no tool CAD selection, Browse or Retry. Initial camera fitting stops after a gesture on that
  surface. Decorative parts (axes, markers, normals, calibration table and fixture stands) are non-selectable
  and excluded from fit bounds: OccScene marks their presentation structures infinite after display
  (OccPartProps::fitExcluded), never their AIS objects, because AIS_Shape computes an infinite
  object's shaded presentation as wireframe.
- RobotDefinition::kr10() builds the model and ordered link filenames/RGB without OCCT; robot geometry
  changes mean editing its constant table. RobotController's
  HOME/FK check and Generate's validRobot guard the values. robotgraphics.h supplies neutral graphics
  and the two tool paths in RobotTool order; adapters construct OCCT values; renderer link count comes
  from the supplied graphics.
- CadLoadWorker accepts absolute .brep paths and returns ordered shapes/errors (CadLoadResult is
  worker-independent); OCCT parsing is serialized. One robot job loads seven links, then
  EE_WP-500.brep (measuring) and EE_GDZ-65-800A.brep (spindle); OccRobotAdapter installs them together
  and shows only the active tool. The spindle model sits at the flange matrix. The probe model keeps
  its CAD frame (ball centre at (0, 0, 127.7) mm) and sits at the flange matrix times the translation by
  the measuring TCP position minus that point; an applied measuring TCP redraws the current pose.
  Only the displayed end-effector part is pickable (as a whole; links are not): a left click on it
  gives OccController::toolSelectionRequested, and DrawingDocument::toolSelected highlights it in
  the same OccScene selection as the selected objects.
- A DrawingModel part (absolute BREP path, rigid placement in {B}) loads once per path on a
  separate worker, one path at a time; finished loads, failures included, stay in
  DrawingDocument::models. Insertion accepts an object whose model is loading or failed and skips that
  part; a successful load redraws its users. A failure is reported once, never retried and never
  affects robot readiness. Single-model loads retain the SHA-256 of the BREP bytes parsed by OCCT.
  A workpiece solid supplies its mounting reference on DrawingModel, alongside its path and placement.
  Parsed single-model loads index trimmed faces in the native BREP frame. Put the mounting transform
  on the AIS object so detected face owners retain the source topology and its subshape locations.
- RobotController owns the stable model/solver and accepted pose. Keep the checked initial HOME
  candidate unpublished until graphics acceptance; publish the model to ScenePlayback once; commit
  later poses only after transform acceptance. Surfaces/windows are restored from drawing values/CAD
  results without a robot-state/model pointer or model revision change.
- Application assetpaths.* selects the deployed CAD and OCCT resource directories; main passes the
  OCCT resource directory to OccController at construction. initializeOcct keeps resource checks,
  environment variables and external Font_FontMgr failure handling; Viewport selects no paths.
- The application reads BREP only from applicationDirPath()/resources/cad (seven hand-maintained link
  BREPs in kr10/, plus BREPs generated from resources/deploy/cad/**/*.stp); no STEP import, runtime BREP cache or
  TKDESTEP link. cmake/DeployOcct.cmake stages OCCT DLLs/resources and both BREP sets beside the
  executable and at installation.

## Devices and protocols

Before device/UI integration changes, read src/application/devices/{devicehub,devicerunner}.{h,cpp},
src/devices/abstractdevice.cpp, src/devices/include/devices/abstractdevice.h and
src/application/main.cpp; for QML registration also Build and modules.

- Hub.device(key) returns a DeviceRunner on the QML thread; keep its QQmlPropertyMap there.
  DeviceHub::add takes a parentless AbstractDevice and moves it to I/O. FTS/RSI share ControlIO and
  PLC uses GeneralIO; main.cpp's FTS-to-RSI signal relies on this, so recheck connections/data races
  before changing groups.
- The runner queues startReq, stopReq and invokeReq. Never call device methods from QML's thread;
  AbstractDevice's internal direct meta-call already runs on its I/O thread.
- Create/use/destroy sockets and timers on their I/O thread via startDevice/stopDevice. Preserve
  thread-finished/deleteLater cleanup, finish device stops before quitting/joining I/O threads, and
  keep shutdown order and repeat-call safety. stopAll uses BlockingQueuedConnection from outside both
  I/O threads; calling it inside the receiving thread deadlocks.
- Publish values through stateReady → DeviceRunner::onStateReady → data, socket state through
  socketStateReady → onSockState → socketState. Preserve QML-consumed keys/notifications; mutate
  QML-facing models only on their owning thread.
- Log device diagnostics with qInfo().noquote() on the device thread with category label, device name
  and text; main.cpp configures timestamps. No logging singleton, message type or GUI-thread relay.
- `FtsDevice` retains its manual JSON recording, saves copied data asynchronously and joins saves at
  shutdown. PLC and RSI devices send records to the GUI-thread `SessionRecorder` through queued signals.
- DeviceRunner::invoke(name, args) dispatches public Q_INVOKABLE methods of AbstractDevice subclasses
  returning void with zero arguments or one QVariantMap. Names must be unique (buildApi indexes names,
  not signatures); inspect string call sites. Socket devices expose connectDevice/disconnectDevice,
  never connect/disconnect, which would hide QObject's, and parse their configuration with
  parseSocketEndpoints into one SocketEndpoints value (struct in abstractdevice.h).
- Keep device/protocol processing and substantial numerics in C++; never block the GUI or ControlIO
  receive callbacks. Single-file helpers/constants go in an unnamed namespace; match the edited file's
  naming, braces and indentation.
- Protocol changes preserve or explicitly update framing, byte order, state keys and endpoint
  validation with their consumers; validate with offline inputs or local simulation. The root
  hardware-authorization rule applies.
- PLC: keep one request in flight with the reply timeout and the idle SNAPSHOT heartbeat (below the
  PLC socket keep-alive), and send nothing at connect. The PC builds and decodes only WRITE_IO,
  SET_VAR and SNAPSHOT requests; writeMessage is private. The IO change frame and the SNAPSHOT reply both
  carry the start-sequence step/program/result; `VAR_TYPE` and the step/result codes equal the PLC
  DUTs. A PLC protocol or check change updates `docs/plc_project_snapshot/ISPSoft project.md`, and
  SceneMachiningPreparation's plcReady and canResumeCalibration repeat the PLC's FB_RunSetVar Run and
  Resume checks; change both sides together. The PLC panel never drives module 1 outputs and starts no
  program; programs are started only by Start Calibration and resumed by its Resume.
- RSI: `RsiDevice` holds a stream table and plays `streams[TouchIndex]` from the first Start = 1
  request after Idle; a Start = 1 request with another TouchIndex while not Idle first ends the run
  as Start = 0 does (consecutive searches may send no Start = 0 packet). `decodeRequest` reads a
  missing TouchIndex or `not linked:<n>` (an unconnected ETHERNET input) as 0, a decimal integer as
  the index and any other text as an invalid packet; `Touch` is on only for "1" and `AIPos` gives the
  angles, neither invalidates a packet. Once per search (reset by every Start = 0 packet and by a new
  table) the oldest pending packet with Start 1, Touch 1, angles and TouchIndex > 0 is emitted as
  `touchDetected(diagnosticId, touch, joints)` after the send attempt and capture bookkeeping. `RsiDevice` stores
  accepted-peer requests under the installed table, including unanswered ones, up to each buffer's
  limits. `captureReady` passes a capture to the recorder. Protocol details are in
  `controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md` ("PC behavior") and
  `controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md`.

## Geometry and scene

For numerical geometry/path changes, read geometry/mathtypes.h, geometry/utils.cpp and the affected
producer/consumer; for OCCT adapters or robot presentation also Viewport and CAD.

- Preserve X/Y/Z/A/B/C order, degree-based Euler APIs, Z-Y-X composition, units, tolerances,
  optional/error results and trajectory timing unless the task changes them. Check failed optionals
  before use; validate finite/degenerate inputs. Initialize Eigen transforms to identity and persistent
  vectors to zero.
- Joint rotations use arbitrary-axis makeRotation without small-component cleanup; keep cleanup in
  the principal-axis overload used by trajectories. inverseRigidTransform assumes orthonormal rotation
  without scale/shear. KR10 tolerances: 1e-4 mm position, 1e-6 rotation entries; wrist singularity
  threshold GeomConst::Eps (1e-9).
- Angle-only calculateIK returns angles/Status/Turn from accepted Status and preceding angles and
  computes only the three-joint transform needed for the wrist. Callers run full FK and flange/TCP
  checks and derive the accepted configuration from those frames. IkSolution has no FK frames; there
  is no angles-only checked IK; callers carry the accepted IkSolution as seed.
- SceneEndEffectors owns accepted tool XYZABC and its optional reconstructed transform on the GUI
  thread; consumers reuse spindleTcpFrame, workers copy it. Reconstruct from accepted coordinates,
  not the raw Apply product or rounded display values; compose FLANGE_ER * ER_TCP in that order.
  Commit coupled list/frame values before notifications; unchanged values must not invalidate paths;
  a collet change still emits the TCP notification even when the TCP is equivalent. Preserve the
  flange-based Kr10Kinematics FK/IK APIs; there is no manual FK/IK command. The measuring TCP (TOOL_DATA[5]
  by default) is applied with applyMeasuringTcp and reconstructed as measuringTcpFrame; an unchanged
  value emits nothing. SceneCalibration persists only its X, Y, Z: its constructor applies a saved
  primary ball centre (A, B, C kept) before connecting its save slot, which saves a changed X, Y, Z
  with source "manual" and writes nothing when they equal the saved value.
- SceneModel owns SceneObject; QML changes scene state through model methods. SceneObject holds a
  plane, cylinder, intersection edge (with its two source IDs), GE-22 placement, calibration sphere or generated probe path
  (the shared immutable ProbeTouchMotion) by value in a std::variant; only a machining path has a child
  QObject (exposed as geometry). Its properties list supplies the {label, value} rows of the
  Properties panel. Preserve stable scene-allocated IDs (0 = none), shared references and GUI-thread
  mutations. Numerical fitting/intersection belongs to Geometry; OCCT presentation to adapters.
  Decorative points/normals/axes take no part in picking or fit bounds. The calibration sphere's
  drawing adds the table top (sphere file `table`) and `CALIBRATION_STAND_SPH25.brep` as decorative parts that share
  its visibility; diagnostic overlay updates keep them.
- A Refine Job owns the saved definition as scene metadata. Publish it after the file commit.
  Creation checks the current BREP revision, three independent trimmed planes and each role's
  included contacts. Skipping a point can resolve a trim error but cannot make an oversized span fit.
  Creation leaves the placement and existing motion untouched.
  An applied edit keeps the job UUID and scene object, advances its definition revision and removes
  only Scan Paths carrying its UUID. Cancel and an unchanged save preserve the revision and paths.
  Resolve the saved workpieceId across restarts. The built-in end gauge uses fixture:end-gauge
  unless the fixture definition supplies its own workpieceId. For a legacy job, match the mounting
  observations (LF and CRLF are equivalent) and CAD revision to exactly one workpiece. Save the
  resulting key under the existing file lock, keeping the job revision. Recheck the loaded and on-disk BREP before restoring
  face indices. A moved mounting retains model-local points and needs fresh clearance checks at generation.
- In Precise, Generate Path opens Scan Path in Properties for the selected saved Refine Job.
  Generate uses the chosen group (G1 by default, or G2) for all three passes. Failure keeps the
  editor and group for retry. Cancel discards the draft. A changed or invalid selection or leaving
  Precise closes the editor. Each point keeps its saved
  face and grid cell. Check the placed sphere, strut and finite table top at the stored motion points
  when present. Tool-body and mounting clearance need visual review.
  Publish and select the new Scan Path only after rechecking the job revision, target, placement,
  model digest and on-disk BREP. Replace the previous linked path only after scene insertion succeeds.
  Selecting a job keeps its tree row selected, reveals and highlights its workpiece, and draws its
  included material contacts on all three faces. A missing or ambiguous target clears the preview,
  displays the reason in red in the Status Bar and disables Generate Path. Refresh after CAD loading
  or workpiece insertion/removal. Selecting another row clears the saved-job markers.
- Before import/intersection changes read the geometry factories and SceneSurfaceImporter/SceneModel
  callers; preserve units and probe-compensation semantics. BoundedPlane/BoundedCylinder accept points
  or decoded ProbeSamples, without file factories; SceneSurfaceImporter reads/decodes plane and
  cylinder inputs; there is no circle import. SceneSurfaceImporter refuses files above 4 MiB before
  reading them, and decodeProbeSamples refuses more than MaximumProbeSamples (10,000) points.
- fitSphere rejects fewer than four or nonfinite points, a smallest singular value below 1e-9 of the
  largest, and a nonpositive squared radius. SceneCalibration reads `SPH25_rough.json` on the GUI
  thread at every entry into Calibration: version 1, positive sphereDiameter, strutDiameter and
  strutRecess below the diameter, at least four finite roughPoints X/Y/Z; other keys ignored.
  SceneSphere::roughCenter and the ball-centre radius come from the readings; the compensated radius
  subtracts the ruby ball radius; the nominal radius only sizes the touch plan. SceneSphere::center
  is the saved primary sphere centre when present, otherwise roughCenter; it places the drawn
  spheres, table, stand and touch plan. A changed sphere replaces the object. While
  Calibration is active the sphere, end gauge and calibration path form the Calibration group. Entry records and
  hides other visible objects (also ones added meanwhile), exit shows the recorded ones that still
  exist. Delete is disabled for the Calibration group.
- On entry to Calibration or Precise, load accepted `EG22_precise.json` if present. Otherwise
  `EG22_rough.json` supplies GE-22 ball centres in {B} and their observation radius in mm. Native X=0 (wide), Y=0 (end)
  and Z=0 (top) have outward normals near base −X, +Y and +Z respectively. Compensate each
  fitted plane inward before intersecting them. Rough observations use their stored ballRadiusMm
  for both plane compensation and physical clamp clearance. Scan observations use the applied
  group's effective radius for the plane offset and the sampled ruby radius for clearance.
  Report signed distances to the outward-oriented ball-centre planes before compensation.
  The top fixes Z and the wide face fixes X
  projected onto that top. Keep gauge placement independent of sphere placement and probe settings.
  Place the native frame of `EG22.brep` and the three positive workpiece axes at the fitted origin
  and orientation. Translate `CALIBRATION_STAND_EG22.brep` to that origin with its axes parallel to
  the base axes. The gauge solid participates in picking and fit bounds. Its stand and axes share
  the gauge visibility and are excluded from picking and fit bounds. Absent fixtures and invalid
  rough input retain the rough loader's removal behavior. Invalid precise input or failed scene
  insertion preserves the old drawing for reference and blocks gauge operations until reload succeeds
  and the loaded and on-disk CAD agree. Replacement transfers name, visibility and selection.
  Stable workpiece keys and the original rough mounting digests resolve existing Refine Jobs.

## Machining and playback

Apply Geometry and scene to numerical changes.

- SceneMachining exposes SceneMachiningSettings as settings; Main calls App.Machining.generate()
  (SceneMachining::generate), separate from dryRun, which only plays a prepared immutable
  ChamferMotion. End-effector Apply actions are separate.
- Animation code lives in src/trajectories/{chamfer,probe} on the motion module src/trajectories/motion,
  with published values in src/trajectories/include/trajectories, and calls no scene/device APIs.
  Inspect the relevant producer/consumer among chamferpath, chamferarclength, chamfercentralmotion,
  chamfercentralvalidation, chamfermotion, probetouchmotion and motion/{timedmotion,motionbuilder,
  tcpsolver,jointsegments}.
- Keep geometric parameter, traveled distance and physical time distinct. Recover TCP pose by
  evaluating the analytical curve at the inverse arc-length parameter; never substitute stored XYZ/ABC
  interpolation without explicit redesign. Inverse-map knots, animation points and RSI cycles are
  different data.
- Lead evaluation may skip overwritten machining transform composition only after the existing
  axis-sign/basis, positive-depth and finite-position checks. Preserve the final lead basis checks and
  exact complete junction poses. Fixed chamfer trigonometry belongs to each immutable path; keep
  tangent division/reciprocal and copy the cache with its owned parameters.
- Preserve the piecewise quintic Hermite inverse, all nine inverse-check fractions and endpoint
  derivative conditions. Reuse accepted endpoint differentials and the original integral/error; keep
  stored-distance rounding checks. The per-phase 800000 limit counts original logical differential
  calls, including reused work, rejects the phase when exceeded and is not exported. Charge only
  reached work at the original check positions.
- Fix central points/times before IK from curvature/time. Within one Generate worker share three
  central arc-length maps, timing and schedule, constructed only when a candidate reaches the curve
  (ChamferCentralPreparation::prepare). Cache analytical targets/failures only at reached scheduled
  indices (CentralPath::target); solve/check joints anew for each seed.
- MotionBuilder builds every motion from HOME with appendPtp (joint steps ≤ 2°, ptpDuration, quintic
  smoothstep) and appendPath for a TimedPath (LinearPath for LIN/CIRC, CentralPath for the chamfer
  curve). appendPath checks in order: path start against the preceding stored TCP (0.0001 mm,
  0.000001 rotation entries); per scheduled point seeded ChamferTcpSolver::solveTcp and the new
  interval's joint speed; the grouped rate inspection (secants ≥ 1e-4 s from rest to rest: grouped
  joint speeds, XYZ acceleration of prescribed targets, the stop at the segment end); the segment's
  spline. A segment is stored only after all checks pass. Errors read
  "<phase label>: <message> (t=<path-local s> s)"; the first failure is the first failing point in
  point order (user decision, 2026-10-26).
- ScenePlayback owns Dry Run, the robot model copy and availability. It plays a
  std::shared_ptr<const TimedMotion> aliasing the timed() motion of a saved ChamferMotion or
  ProbeTouchMotion, keeping the owner alive. TimedMotion::evaluateJoints(seconds) returns optional
  angles and the phase index without FK (smoothstep on PTP, stored cubics on splines, joint-linear on
  constant-speed segments, exact knots; a point shared by two segments/phases belongs to the ending
  one). ScenePlayback calls RobotController::applyPlayback with those angles and the saved tool
  transform; main attaches this collaborator after both exist. present keeps a local copy of the motion
  handle because callbacks may release the active motion. The coordinator owning the path starts/resumes
  it; stateChanged reports start/resume/pause/end, playbackChanged each presented time,
  failed(ownerId, error) a failure. RobotController runs one FK, checks all link matrices and the
  saved-tool TCP even with hidden axes, and reuses that TCP in drawing. Commit the pose only after
  drawing acceptance; advance elapsed/phase only after success while playback stays active. Numerical
  errors use the robot console path; drawing errors the renderer path.
- Saved animation points are nodal joint angles with timestamps, no velocity field. Keep FK-derived
  TCP for accepted LIN/central nodes and reuse it when storing them; keep analytical targets separate
  for central XYZ acceleration; FK of interpolated playback angles is separate. Check HOME derivatives
  locally with the existing timing error.
- LIN also fixes points before IK: N = max(1, ceil(T·sqrt(a/(8·0.01 mm)))) per constant-acceleration
  span (T in s, a in mm/s²); cruise has one interval; acceleration 0 is constant-speed LIN without ramps
  (one interval). CIRC (circularProfile: arc about a centre with rotation slerp, used for the probe
  transfers on the start shell) adds peak²/radius to a of every span, so its cruise is subdivided too.
  No recursive subdivision, quarter-time IK checks, added central points or automatic retiming after
  IK/rate failures. The position criterion bounds neither orientation nor actual TCP error after joint
  interpolation. Keep exact shared phase endpoints and continuous joint winding; never wrap saved
  angles modulo 360°.
- During Generate prepare one clamped joint spline per LIN/CIRC and one shared central spline, with
  zero endpoint joint velocities; the constant-speed probe Approach has no spline and plays
  joint-linearly. Check polynomial position/speed extrema and write checked intervals into the
  candidate's final coefficient array; discard the candidate on failure without adding points or
  retiming. Dry Run performs no IK, integration, inverse-map/spline construction or preparation pauses.
  Displayed TCP between saved points is approximate; spline smoothness gives no exact Cartesian error
  guarantee.
- Rated joint speeds come from the robot model (equal to KR C4 machine data), without simulation caps.
  ChamferMotion::create and ProbeTouchMotion::create take model/tool/timing once through
  ratedRobotSetup and resolve jointSpeed before IK or splines; inner candidates reuse that setup. Path
  and RSI speeds share chamferJointSpeedError. The grouped inspection skips adjacent speed comparisons
  already accepted while appending but rechecks grouped intervals. The XYZ bound defaults to
  $ACC_MA.CP = 10000 mm/s² (KSS ceiling for programmed CP acceleration; not monitored for RSI). The
  controller has no fixed joint-acceleration value (dynamic model), so joint-acceleration magnitude
  rejection stays disabled in path rate inspection (terminal stops included), spline checks and RSI
  Sampling; do not restore it without a user request. Keep acceleration finiteness, joint
  position/speed and XYZ acceleration checks. HOME timing still uses configured joint acceleration.
- Generate workers own copied inputs and return cohesive results through futures. Accept results and
  insert scene objects on the GUI thread; reject stale source/model/TCP revisions; publish only
  complete motions; insert a replacement before removing its old object, keeping name/visibility. Wait
  for remaining workers at shutdown.
- Saved paths own immutable geometry/model/TCP/settings snapshots and survive source deletion. Robot
  model or TCP changes invalidate playback until regeneration; a sphere replacement, measuring TCP
  change or robot model change removes the calibration path. Preserve pause/resume, Stop-to-HOME,
  completion at HOME and cleanup on readiness loss, mode change or path deletion. Selection cannot
  replace running motion; block generation during playback.
- Generate has no profiling command or statistics output. After a worker finishes, SceneMachining logs
  one line (point count and movement seconds, or the error); SceneCalibration logs calibration and
  verification touch counts, points, movement seconds and minimum transfer/strut clearances, or the
  error. The candidate's last error
  string remains the UI error. MotionBuilder, the chamfer start search and RSI sampling share
  ChamferTcpSolver::solveTcp.
- Calibration Generate builds createProbeTouchPlan and ProbeTouchMotion::create. The plan is fixed in
  code (accuracy decisions in plans/PROBE_SPHERE_TOUCH_PLAN_EXECPLAN.md; sphere centre, radii,
  strut, search feed and the placed gauge are inputs): passes with one fixed probe orientation each, G1 stylus down
  (A 180, B 0, C 180), G2 stylus along +X (A 0, B 90, C 0), G3 stylus down turned 180° (A 0, B 0,
  C 180); calibration passes G1, G2, G3, G3, G2, G1 (second pass reversed) on the same 19 directions
  (tilt, roll) in every group, then verification passes G1 and G2 on 25 directions, unreachable ones
  skipped. An unreachable calibration direction is an error. The motion: PTP HOME to the first entry,
  LIN descend; per touch a CIRC transfer on the start shell, a constant-search-feed LIN Approach and
  a LIN Retract with the transfer limits; between passes of different groups LIN retreat, PTP to the next entry and LIN
  descend, between passes of one group a transfer (none when the next pass starts at the last start);
  finally LIN retreat and PTP HOME. During sphere passes each stored point keeps the ball ≥ 3 mm from the strut, and every
  point outside Approach/Retract ≥ 3 mm from the sphere (0.001 mm tolerance). Errors start with the
  phase text ("Calibration G2 pass 1, touch 12/19: transfer: ..."), also shown by Dry Run.
  ProbeTouchMotion keeps the plan used by Calibration RSI Sampling (below).
  Calibration requires the placed gauge. Its single Generate Path appends eight four-point gauge passes after sphere verification. Each probe orientation (G2 then G1) visits A, B, B reversed and A reversed.
  Stage face or orientation changes through exit and entry. Each motion step carries its own
  movement kind. Gauge transfers are straight and sphere transfers are circular. Draw and play
  the saved timed joints with the same ScenePlayback and robot as Calibration. Both orientation-change
  waypoints lie 50 mm above the local gauge top. Generation requires at least 20 mm between
  the ruby surface and the top plane throughout the previewed joint turn. The operator reviews the move from the
  sphere exit to the first gauge entry in Dry Run. Calibration RSI Sampling automatically saves the combined plan in the configured records directory
  for synthetic and measured placements. Controller delivery is deferred. The snapshot includes the placement
  file hash and active gauge pose. Replacing either fixture or the measuring TCP invalidates the combined path.
- RSI: before edits read controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md (frames, signals, PC
  behavior, offset reconstruction, diagnostics, shared_data.txt and the KRL reader) and, for
  calibration, controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md (job file, search
  streams, KRL flow, record files), then chamferrsistream, probersistream, scenemachiningpreparation
  and devices/rsi/rsidevice. The constraints below are not repeated there:
  - Sampling success, a bound socket or a delivered reply is not controller readiness and never grants
    permission to move the robot. Physical RSI Sampling also requires RSI DeviceRunner.isConnected. Combined calibration samples offline for either placement source.
  - sampleChamferRsiStream targets come only from ChamferCentralMotion::evaluate (inverse quintic arc
    map), never from interpolated stored poses. The chord bound relies on every saved central Generate
    interval being ≥ 4 ms. Keep composed TCP feedback separate from FK residual checking.
  - ChamferMotion::create rejects transfer speed above $VEL_MA.CP and acceleration above
    cartesianAcceleration; KRL does not repeat these checks and the controller enforces its live
    limits when KRL assigns $VEL.CP/$ACC.CP before PTP PsIn. Keep QSaveFile replacement and install
    corrections only after export succeeds.
  - The stream belongs to its sampled path, not the selection. Deliver it to ControlIO as an implicitly
    shared stream table QVector<QVector<V6d>> plus its diagnosticId through the queued main.cpp link
    (chamfer and test LIN: one stream at index 0; calibration: index t = touch number, index 0 empty);
    keep that identifier paired with the active corrections.
  - At most one table and its job file are held. After the previous controller job is released, preparing chamfer, test LIN, calibration or EG22 first removes shared_data.txt and scan_data.txt and sends an empty table. A removed,
    incompatible or regenerated source path drops its table and file. A job file that cannot be removed
    is shown as the preparation error and stops that Sampling or test LIN request before sampling or
    installing a stream; startup and invalidation show it too.
  - EG22 RSI Sampling accepts the saved three-face motion after checking its Refine Job, mounted
    CAD revision, native face planes, signed approach sides and ruby clearance from the clamp.
    The shared probe sampler numbers each Approach once for plan.csv and the stream table. Scan Path Properties shows the sampled job ID.
    Archive the sampled record before exporting the same scan_data.txt bytes and installing the table.
    Physical Scan requires the same selected path, applied primary, measured mounting, acknowledged table ID,
    connected devices, PLC Run conditions and a fresh SHA-256 of the live bytes. The commissioning gate
    stays closed pending installed-cell evidence and justified physical pose bounds.
    Installing a scan table grants no correction permission. ControlIO requires the admitted job ID and
    acknowledged program 2. Passive STOP holds the stream index. Abort or invalidation revokes permission
    synchronously. The preparation owner retains the export and table until fresh CELL feedback and capture closure.
    The settings marker scan_in_progress preserves the replacement block across restart. See the scan module contract
    for recovery and the completion levels at X1.1.
    At capture closure, use the sampled pass and touch indices to compare each actual FK/TCP with
    its Approach. Check signed inward travel, perpendicular distance and smallest rotation angle
    against the sampled physical bounds. Require their commissioning basis. Fit raw centres only
    after complete acquisition, current inputs and accepted poses. The scan record retains available
    centres and signed residuals on failure. The fitted frame belongs to the execution record.
    Keep the successful fit in memory until the matching session writer reports its actual folder.
    Block physical starts during archival and operator review. Reassign rechecks the sampled setup,
    Refine Job, CAD and mounting, then commits precise observations and invalidates old motions even
    if scene insertion fails. Cancel and window close release the candidate. Restart loads committed
    observations only. Archive retry uses the retained acquisition and its original job ID.
  - Calibration RSI Sampling (prepareCalibration, App.Calibration path present, no calculation)
    runs sampleProbeCalibrationRsi on the saved ProbeTouchMotion: probeMotionSteps pairs one to one
    with the timed phases (PTP to entry, LIN descend/retract/retreat, CIRC transfer, Search for each
    Approach, HOME); targets are the saved end-point TCPs at six decimals within 0.0001 mm and
    0.000001 rotation entries, S/T from the saved joints; at most 600 steps (the KRL DAT arrays). A
    search stream moves 5 mm along the approach direction at the plan's searchFeed, then 50 zeros.
    After checking that the sampled motion is still current, preparation saves plan.csv, calibration_data.txt
    and session.json under records with outcome sampled. Start remains unavailable pending controller integration.
  - The RSI test LIN (controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md)
    uses prepareTestLin, which drops any held stream and job file like a new request and queues 25000
    increments of +0.004 mm WORLD X plus 50 zeros with a `test_lin_` ID through the same link; a later
    chamfer or test LIN request, startup or shutdown drops it.
  - Calibration run (startCalibration, plans/PROBE_SPHERE_CALIBRATION_RUN_EXECPLAN.md): allowed with a
    held current calibration job, no Sampling or run active and the PLC's own Run checks met (step
    IDLE, X1.0 and X1.7 on, X1.6 off). It opens the session before sending PLC runRobotProgram 1.
    Detections arrive through the recorder GUI queue from RsiDevice::touchDetected to recordTouch
    (first per touch number). The PLC state comes
    through DeviceRunner::dataUpdated, because QQmlPropertyMap::valueChanged does not report insert().
    Once the PLC start sequence has left IDLE, a return to IDLE without STARTED or a running program
    ends the start attempt. A running state or STARTED confirms the start. A subsequent finished state
    ends the program. From Start Calibration until the run ends (Run requested, PLC start
    sequence or program 1 running), Sampling and the test LIN are refused; a replaced job, a PLC
    disconnect, a failed start or X1.0 AUT EXT off (the operator's abort, e.g. T1 after an EMERGENCY
    STOP) ends the run with an error; an interrupted program keeps it. resumeCalibration sends PLC
    resumeRobot only when canResumeCalibration holds (program interrupted, step IDLE, X1.0, X1.6 on,
    X1.4 and X1.3 off). After a Passive STOP the operator acknowledges the message on the smartPAD
    before Resume. An observed resume sequence returning to IDLE without success keeps the same run
    and shows its error. With X1.2 off, calibrationInterruption directs the operator to T1.
    Leaving AUT EXT closes the session. When detections exist, the recorder retains `touches.csv`
    even on failure. After program completion, fitProbeCalibration runs on the GUI thread.
    A fit supplies `residual_sets.csv` and SceneCalibration::recordCalibrationResult updates
    lastCalibration. The recorder includes `result` in `session.json` only on success. An invalid result
    is reported as a run error. After a failed read of probe_calibration.json, SceneCalibration writes nothing to
    it in that session, and every refused save reports the read error through settingsError. It becomes
    primary only through Reassign (reassignPrimaryCentres): primary = the last result with source =
    its id, the ball centre applied with A, B, C kept, the sphere moved in the submode; the primary
    centres come only from the calibration touches, never from the verification refit. Reassign
    removes the calibration path. Saved sampling records remain available.
    `python/calibration/calibration_fit.py` repeats the fit from `plan.csv` and `touches.csv`.
  - ControlIO timestamps and stores requests, decodes them for the live protocol, and encodes the
    reply. File formatting, disk writes and waiting for writers stay off the receive path. Replacing
    a table flushes its capture before installing the new table. The recorder associates a capture
    by job ID with the open calibration or currently armed job. On shutdown preparation retains the
    job until the recorder drains queued captures after DeviceHub::stopAll. A worker decodes the
    stored XML for both RSI tables and commits the session files individually, `session.json` last.

## QML

Inspect changed components and their custom base types (properties, signals, default content,
sizing). Read device/scene contracts when UI changes touch those APIs, and Build and modules for
registration, module names or resources.

- Reuse qml/Modules/Components controls and Styles Colors/Fonts/Metrics. No explanatory/tutorial
  labels: concise field names, units, statuses and actionable errors; explanations go to docs/chat.
- Measuring EE/Machining EE settings open in the right Properties area; Close restores object
  properties. Measuring EE edits the ruby ball centre TCP with Apply. End-effector numeric fields show
  four decimals and accept at most four fractional digits; read-only ER rounding never changes the
  stored ER transform.
- Use implicitWidth/implicitHeight for natural size and keep useful inherited sizing. Before
  overriding styled Controls inspect contentItem/background sizes, padding and insets; Item wrappers
  may need their own implicit size.
- A Qt Quick Layout owns its immediate children's geometry: use Layout.* and implicit sizes, not
  concurrent x/y/width/height or anchors. Anchors on a layout are valid when its parent is not a
  layout. Check parent/child size dependencies for binding cycles.
- Scrollable views never spring past their ends: ListView/Flickable set boundsBehavior:
  Flickable.StopAtBounds, a ScrollView sets it on its Flickable at completion
  ((contentItem as Flickable).boundsBehavior).
- WorkspaceController owns mode/submode, selected IDs and toolSelected (the displayed end-effector
  model; exclusive with selected IDs, cleared by any object selection or mode change). Picking that
  model opens its EE settings as the toolbar button does; their Close deselects it. Main binds them
  read-only and sends selection/delete/intersection/mode requests. C++ connects viewport picks/Delete and selection
  drawing notifications. Before applying new input settings notify QML so Cancel restores the old
  editor settings; a successful Generate closes its editor before requesting selection. Main creation
  resets Measuring/Rough/empty selection with the existing Stop-to-HOME behavior.
- Refine Frame takes the single eligible scene selection once on opening. While its Object picker is
  active, native hits and misses go to the draft before ordinary object or tool selection. A change of
  configured target clears face bindings, side overrides and exclusions only after confirmation. The
  draft uses the live scene instance ID to distinguish copies sharing one BREP and mounting data.
  An accepted Object pick selects that instance in the Scene tree. In face-pick mode only faces of
  the target solid are active on that solid. Keep its whole-object selection orange and face hover
  blue. After acceptance, highlight only the active slot’s face and draw its signed approach arrow.
  Slot indices share the target’s source path and loaded revision. Reject changed BREP bytes on a pick.
  Compute the active grid in metric workpiece coordinates from the oriented source face, independently
  of approach side and camera. Refit untouched pitches after count or margin changes using usable extent
  divided by interval count, rounded down to 0.001 mm. Keep a manually entered pitch until edited.
  When margins cannot fit that span, report the fit error and keep its ends within the physical bounds
  whenever the span fits the full axis. Display the pitch used to draw the grid.
  Classify against all trim wires. Apply the mounting transform once
  when displaying the marker groups. Markers are nonselectable AIS decorations and excluded from camera fitting.
  In point-pick mode, hit-test the active face’s displayed grid in native pixels and send its 1-based U/V index
  to the editor. Each slot retains its skipped indices through grid edits, including out-of-range rows.
  Use red dots for skipped or off-trim points and keep the geometry reason on skipped off-trim rows.
  Delete restores the selected exclusion. Esc, a native left-click miss or a click outside the exclusion
  field in the main window ends picking. The Show points eye toggles all assigned grids. The active
  slot alone supplies the pickable indices, selected-row ring, selected face and approach arrow.
  Releasing the editor restores ordinary picking and removes its face presentation.
  Create submits the three slot drafts together. On failure keep the fields and show the reason in
  the main status bar. On success select the saved object under Precise / Refine Jobs and close the editor.
  Opening Refine Frame on a selected job copies its three roles into the editor. Save Changes updates
  the selected job only after the file commit. Cancel discards the copy and reselects the job.
- In Calibration the toolbar shows only Measuring EE, Generate Path, Dry Run/Pause, Stop, RSI
  Sampling (requires canPrepareCalibration and inactive playback for both placement sources), Start Calibration (startCalibration, enabled by
  canStartCalibration and the RSI DeviceRunner's isConnected) and Resume (resumeCalibration, enabled by
  canResumeCalibration and the RSI DeviceRunner's isConnected; a separate tool, so no label change
  resizes the toolbar); Generate Path calls
  App.Calibration.generate() without an editor; the Scene panel lists only the Calibration group;
  Delete removes nothing. The Settings "RSI test LIN" button (prepareTestLin) requires the RSI
  DeviceRunner's isConnected, no running Sampling and no active calibration run (calibrationRunning).
  The status bar keeps a fixed "AUT EXT" indicator after the mode label (green while the PLC is
  connected and X1.0 is on, gray otherwise) and shows the calibration run progress
  first in its existing label in every mode. lastCalibrationChanged opens CalibrationResultWindow (a
  modal top-level Window of fixed size, so the viewport cannot cover it: result rows, the bound
  calibrationRunError, Reassign and Cancel); calibrationRunFinished opens the OK-only "Calibration
  failed" dialog only when that window is not open. The same result window presents an archived scan
  with signed XYZ and principal KUKA ABC in the previous workpiece frame. Reassign publishes the
  candidate. Cancel or native close leaves the archive and authoritative placement intact. A settings
  write error for probe calibration opens the native "Probe
  settings" dialog at start and on every failed write, never a status message. With nothing selected,
  Properties shows the last calibration rows (label above value) or "No calibration result." in
  Measuring / Calibration and stays empty in other modes. Otherwise the label shows the playback status while calibration
  playback runs without an error, or while Dry Run plays or pauses with the chamfer editor open;
  otherwise the calibration error or the reason Generate is unavailable; with the chamfer editor open,
  its message (Generate error, reason Generate is unavailable, path incompatibility or invalid fields;
  red only for a Generate error), so the editor has no playback or message region of its own;
  otherwise the preparation error, summary,
  calibrationUnavailableReason or "Ready for RSI Sampling.", followed by the controller state while
  the RSI device is connected. WorkspaceController emits calibrationActiveChanged(false) before
  changing the tool, so calibration playback stops first.
- Keep selected input, captured Generate source and active playback separate; UI drafts, focus, panel
  visibility and group expansion stay in QML. Cached renderer replay already restores selection.
- Reusable components take explicit inputs, not caller IDs/context. Use known concrete QML types;
  keep var for heterogeneous data. Require inputs without a valid default and update all creators.
- Delegates with required properties declare every consumed role/view value (index/model/modelData,
  or TableView row/column) and qualify access through IDs. Persistent state lives in models/backend,
  not disposable delegates.
- Qualify root properties from children (root.title); declare handler parameters
  (onActivated: index => ...). Keep simple handlers inline, named functions for reused/substantial
  logic. Preserve bindings on state updates.
- Components/Styles exports update both module CMakeLists.txt and the source qmldir (maintained
  hot-reload inputs). Keep pragma Singleton, QT_QML_SINGLETON_TYPE and qmldir consistent.
