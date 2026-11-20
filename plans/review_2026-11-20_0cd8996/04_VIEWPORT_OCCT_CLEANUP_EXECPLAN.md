# Viewport OCCT code cleanup: remove dead guards, duplicate axis code and repeated constants

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`: "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters, try/catch only around OCCT calls) and "Mandatory code reduction". Before each logical change, re-read both sections. After each milestone, review the changed declarations, bodies and callers, and record start/finish line counts here. Remove a check only after proving that every path to it already guarantees the same condition; the proofs are given below, so re-verify them against the current code before deleting.

**Prerequisite:** `plans/review_2026-11-20_0cd8996/01_VIEWPORT_OCCT_FAILURE_HANDLING_EXECPLAN.md` must be complete first. It changes `occviewport.{h,cpp}`, `occviewwindow.{h,cpp}` and `occrobotadapter.{h,cpp}`, and this plan assumes its result. In particular, `OccViewWindow::failSurface` exists, and `OccViewport::setSelection`, `setSpindleFrames` and `setActiveTool` return `ViewportOperationResult`. If that plan is not complete, stop and record that in `Progress`.

This plan comes from the whole-project review of commit 0cd8996. The findings are in `reviews/2026-11-20_0cd8996/qt_cpp_review_report.md`, blocks S6-D5, S6-D6, S6-D7 and S7-D2 to S7-D11. Everything needed is repeated below.


## Purpose / Big Picture

The OCCT viewport code (`src/viewport/occt/`) carries a lot of code that can never run or that repeats other code:
- null checks on objects that are always created;
- a second implementation of axis display;
- constants copied into several files that must stay identical;
- empty destructor bodies;
- a duplicated block in a constructor.

None of it changes behaviour, but it makes the files longer and harder to follow, and the repeated constants can drift apart. After this plan, the same scene, selection highlighting, axes and view cube are drawn with about 60 fewer lines, and each colour or length is defined once. The user sees no difference. Validation is a build and a short disconnected session that exercises every changed path, as described under Validation and Acceptance.


## Progress

- [x] (2026-11-20) Plan written after reading `src/viewport/occt/occscene.{h,cpp}`, `occworldaxes.{h,cpp}`, `occpart.{h,cpp}`, `occpartprops.h`, `occviewcube.h` (and the guard lines of `occviewcube.cpp`), the colour lines of `occviewer.cpp`, `occviewport.{h,cpp}`, `occviewwindow.{h,cpp}`, `cadloadworker.h`, `src/viewport/include/viewport/robotgraphics.h`, `src/viewport/include/viewport/occcontroller.h` (properties) and `src/viewport/adapters/occrobotadapter.cpp`. The OCCT sources are not in the repository (`libs/OCCT_8_0_0/install-gcc1310_64` holds headers and binaries only).
- [ ] Start counts recorded (run the command in Concrete Steps; the files of `src/viewport` totalled 3264 lines at commit 0cd8996, before the prerequisite plan).
- [ ] Milestone 1: unreachable guards and the unused `OccWorldAxes::length()` removed.
- [ ] Milestone 2: world axes drawn through the frame-axes functions; duplicate display code, flags and forwarder removed; selection-mode loop.
- [ ] Milestone 3: shared constants, small API corrections, destructors, double expose resize, the drawer fetched once.
- [ ] Builds of `RoboCrap3D` and `robocrap` and a disconnected session (user's).
- [ ] Final compliance pass, line counts, `git diff --check`.


## Surprises & Discoveries

(None yet.)


## Decision Log

- Decision: keep the `Attributes().IsNull()` and `Camera().IsNull()` checks in `occpart.cpp`, `occviewcube.cpp`, `occscene.cpp` and `occviewer.cpp`. OccPart keeps one check, after merging its two blocks.
  Rationale: whether OCCT's `PrsMgr_PresentableObject` and `V3d_View` constructors always create these objects cannot be verified here, because the repository ships no OCCT sources. AGENTS.md allows removing a check only after proof.
  Date/Author: 2026-11-20, plan author.
- Decision: keep `OccController::loadRobot`'s early return `if (m_visuals) return {m_error.isEmpty(), m_error};`.
  Rationale: the contract allows a single robot load, and `main.cpp` calls it once. Without the guard, a second call would replace a running `CadLoadWorker` (`std::unique_ptr` reset while the thread runs). The guard is a safety condition, not dead code.
  Date/Author: 2026-11-20, plan author.
- Decision: `hasVisibleParts` uses `OccPart::fitExcluded()` instead of `AIS_Shape::IsInfinite()`.
  Rationale: no part's AIS object is ever marked infinite. The contract forbids it, because AIS_Shape would then draw its shaded presentation as wireframe; only presentations are marked (`occscene.cpp` displayPart). The current filter therefore excludes nothing. The intended rule, ignoring decorations for "has something to fit", is exactly `fitExcluded()`. The visible effect is limited to scenes holding only decorative parts, which do not occur after robot installation.
  Date/Author: 2026-11-20, plan author.


## Outcomes & Retrospective

(To be written at completion.)


## Context and Orientation

Read `AGENTS.md` and the "Viewport and CAD" section of `docs/code context/AGENT_CONTRACTS.md` first.

`src/viewport/occt/` wraps OCCT drawing:
- **`OccScene`** (`occscene.{h,cpp}`) owns every displayed object ("part") keyed by `PartId`, the world axes, two frame-axis sets (spindle TCP and collet) and the view cube.
- **`OccWorldAxes`** (`occworldaxes.{h,cpp}`) is three coloured `AIS_Line` objects built from four `Geom_CartesianPoint` handles.
- **`OccViewCube`** (`occviewcube.{h,cpp}`) wraps an `AIS_ViewCube`.
- **`OccPart`** (`occpart.{h,cpp}`) wraps one `AIS_Shape` with its display properties (`OccPartProps` in `occpartprops.h`, which also defines the `rgb()` helper and `kDefaultPartColor`).
- **`OccViewer`** (`occviewer.cpp`) creates the OCCT viewer, context and view, and configures the highlight styles.
- **`OccViewport`** (`occviewport.{h,cpp}`) combines the above for one native window.
- **`OccViewWindow`** (`occviewwindow.{h,cpp}`) is that native `QWindow`.

OCCT handles (`Handle(T)`) are reference-counted pointers. `new` for an OCCT class either returns a valid object or throws, so a handle assigned from `new` is never null.

The specific redundancies:

1. **`OccViewCube`.** `m_cube` is created in the constructor initializer (`occviewcube.cpp` line 26: `m_cube(new AIS_ViewCube())`). The seven `if (m_cube.IsNull()) return;` lines (49, 63, 109, 118, 135, 166, 181) and `isValid()` (37-40) are therefore never false. The only caller of `isValid()` is `OccScene::displayViewCube` (`occscene.cpp` 151-154, a warning branch).
2. **`OccWorldAxes`.** All seven handles are created unconditionally in `createAxes` (`occworldaxes.cpp` 66-79). `isValid()` (20-29), the null branch in `setLength` (42-44), the guard in `configureAxes` (83) and the scene checks on `axes.isValid()` / `m_worldAxes.isValid()` (`occscene.cpp` 76, 97, 114-117, 138) are never false. `length()` (31-34, header line 15) has no caller.
3. **`OccPart`.** The handle is null only for a null shape. `OccScene::createShapePart` rejects null shapes (occscene.cpp 247-250) before constructing an OccPart (257), so the "AIS presentation was not created" branch (259-262) is unreachable. `OccScene::displayPart` (164, 166) and `activateAllSelectionModes` (199, 201) are called only with stored parts. `OccScene` stores no part while `!isValid()`, because `createShapePart` checks `isValid()` first. An `OccViewport` whose viewer is invalid is destroyed at once by `OccViewWindow::initializeViewport`, which checks `viewport->isValid()`. So the `isValid()` terms in `hasVisibleParts` (43), `setPartVisible` (289), `removePart` (308), `displayPart` and `activateAllSelectionModes` are redundant too.
4. **World axes.** `OccScene::displayWorldAxes` (108-130) and `redisplayWorldAxes` (132-143) repeat what `setFrameAxes` (74-93) and `resizeFrameAxes` (95-101) already do for the spindle axes. `m_worldAxesDisplayed` (header 78) duplicates `m_context->IsDisplayed(m_worldAxes.xAxis())`. `m_viewCubeDisplayed` (79) guards a second call of `displayViewCube` that never happens: `displayInfrastructure` (103-106, a one-line forwarder) is called once, from the OccViewport constructor (`occviewport.cpp` line 22). `activateAllSelectionModes` (203-222) repeats one call three times, for `TopAbs_FACE`, `TopAbs_EDGE` and `TopAbs_VERTEX`.
5. **`hasVisibleParts` (41-48).** It filters with `!part.handle()->IsInfinite()`, which is always true (see the Decision Log).
6. **Copied constants.** The constants that must stay in sync:
   - the selection outline grey 48/255 (sRGB) with width 2.0, at `occscene.cpp` 367-368 and `occviewer.cpp` 223/231;
   - the hover colour `rgb(102, 179, 204)`, at `occviewer.cpp` 216 and `occviewcube.cpp` 90;
   - the default axis length 10.0, at `occscene.cpp` 228 and 234, `occworldaxes.h` 11 and 27, and `occworldaxes.cpp` 68;
   - the unnamed camera-scale ratio 0.05, at `occscene.cpp` 237.
7. **`OccViewWindow` and `OccViewport` details.**
   - `OccViewWindow::viewport() const` returns a mutable `OccViewport*` (header 24), while callers mutate through it.
   - `~OccViewport` only calls `m_timer.stop()`, which `QTimer`'s destructor does anyway.
   - `~OccViewWindow` only resets `m_viewport`, which member destruction does anyway.
   - `#include <QPointer>` in `occviewwindow.h` is unused there (QPointer is used in `occcontroller.h`, which includes it itself).
   - On the first expose, `exposeEvent` calls `initializeViewport()`, whose last step is `m_viewport->setExposed(true)`, and then calls `setExposed(isExposed())` again, so the first frame is resized twice.
   - `if (m_viewport) m_viewport->resize();` is written in `resizeViewport`, in the DevicePixelRatioChange branch of `event()` and in `resizeEvent`.
8. **`OccPart` constructor.** It fetches or creates the `Prs3d_Drawer` twice (occpart.cpp 23-27 inside the marker/line block, and 35-39 unconditionally). Neither `SetColor` nor `SetTransparency` between them replaces the drawer object.
9. **Small items.**
   - `OccViewport::setRobot`, `applyTransforms` and `OccViewWindow::applyTransforms` return a `const auto result` local alongside braced returns, which forces a copy instead of a move.
   - `enum class OccSelectionMode { PartOnly, All, None };` (occpartprops.h) and `enum class RobotTool { Measuring, Spindle };` (robotgraphics.h) have no trailing comma.
   - `OccRobotAdapter` hard-codes `ToolCount = 2` (occrobotadapter.cpp line 10) next to the enum it counts.
   - `CadLoadWorker` (cadloadworker.h) derives from `QThread` without `Q_OBJECT`.
   - The seven `Q_PROPERTY` lines of `OccController` (occcontroller.h 109-115) lack `FINAL`, although the class is final.


## Plan of Work

### Milestone 1: unreachable guards and the unused method

**`occviewcube.{h,cpp}`.** Delete `isValid()` and the seven `if (m_cube.IsNull()) return;` lines. Keep every OCCT style check that tests an object returned by OCCT, such as `BoxSideStyle().IsNull()` and `Attributes().IsNull()`.

**`occworldaxes.{h,cpp}`.** Delete `isValid()`, `length()` and the null branch in `setLength`, and remove the `isValid()` guard from `configureAxes`. Keep `if (length <= 0.0) return;`.

**`occpart.{h,cpp}`.** Delete `isValid()`. `setTransform` loses its null check only after the next step proves a stored part always has a handle; it does, via `createShapePart`. Keep `if (shape.IsNull()) return;` in the constructor, because the constructor is public.

**`occscene.cpp`.**
- In `setFrameAxes` (76), `resizeFrameAxes` (97) and `redisplayWorldAxes`, remove the `axes.isValid()` / `m_worldAxes.isValid()` terms.
- Remove the warning branches in `displayWorldAxes` (114-117) and `displayViewCube` (151-154). Milestone 2 deletes `displayWorldAxes` anyway; do the minimum here.
- Remove the `part.isValid()` checks in `displayPart`, `activateAllSelectionModes` and `createShapePart` (the 259-262 branch).
- Remove the `isValid()` terms of `hasVisibleParts`, `setPartVisible`, `removePart`, `displayPart` and `activateAllSelectionModes`.
- In `setPartVisible`/`removePart`, keep the unknown-identifier branch and its warning: identifiers come from callers.
- Keep `isValid()` in `updateCameraDependentObjects`, `setFrameAxes`, `createShapePart`, `selectParts` and `displayViewCube`, the entry points used during construction and replay.

### Milestone 2: one axis implementation

In `occscene.cpp`, rewrite `updateCameraDependentObjects` as:

    if (!isValid()) return;
    if (m_context->IsDisplayed(m_worldAxes.xAxis())) resizeFrameAxes(m_worldAxes);
    else if (hasVisibleParts()) setFrameAxes(m_worldAxes, gp_Trsf());
    resizeFrameAxes(m_spindleTcpAxes);
    resizeFrameAxes(m_spindleColletAxes);

`gp_Trsf()` is the identity, so the world axes stay at the origin. `setFrameAxes` sets the length, displays each axis with selection mode -1, deactivates it and redisplays it. The current `displayWorldAxes` displays with the default selection mode and then deactivates, which has the same effect: the axes are not selectable.

Then:
- delete `displayWorldAxes`, `redisplayWorldAxes`, `displayInfrastructure`, `m_worldAxesDisplayed` and `m_viewCubeDisplayed`;
- make `displayViewCube` public (it keeps its `isValid()` check) and call it from the OccViewport constructor instead of `displayInfrastructure`;
- rewrite `activateAllSelectionModes` as a range-for over `{TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}` calling `m_context->SetSelectionModeActive(part.handle(), AIS_Shape::SelectionMode(type), true, AIS_SelectionModesConcurrency_Multiple)`;
- in `hasVisibleParts`, replace `!part.handle()->IsInfinite()` with `!part.fitExcluded()`.

### Milestone 3: constants and small corrections

**`occpartprops.h`.** Next to `kDefaultPartColor`, add:

    inline const Quantity_Color kSelectionOutlineColor{48.0 / 255.0, 48.0 / 255.0, 48.0 / 255.0, Quantity_TOC_sRGB};
    constexpr double kSelectionOutlineWidth = 2.0;
    inline const Quantity_Color kHoverColor = rgb(102, 179, 204);
    constexpr double kDefaultAxisLength = 10.0;

Use them at `occscene.cpp` 367-368 and 228/234, `occviewer.cpp` 216, 223 and 231, `occviewcube.cpp` 90, `occworldaxes.h` 11 and 27, and `occworldaxes.cpp` 68. Name the ratio in `occscene.cpp`'s anonymous namespace: `constexpr double AxisLengthPerCameraScale = 0.05;`. The amber selection wire colour in `occviewer.cpp` stays local, because it has a single use.

**`occviewwindow.{h,cpp}`.**
- Make `viewport()` a non-const member.
- Change `~OccViewWindow` to `= default` in the .cpp; it must stay out of the header, because `OccViewport` is incomplete there.
- Remove `#include <QPointer>`.
- In `exposeEvent`, call `initializeViewport()` only when `!m_viewport`, otherwise `m_viewport->setExposed(isExposed())`. A newly created viewport is already exposed by `initializeViewport`.
- Have `resizeEvent` and the DevicePixelRatioChange branch call `resizeViewport()`.

**`occviewport.{h,cpp}`.** Delete the `~OccViewport` declaration and definition; every member type is complete in the header. Drop `const` on the `result` locals of `setRobot` and `applyTransforms`, and likewise in `OccViewWindow::applyTransforms`.

**`occpart.cpp`.** Fetch or create the drawer once, before the marker/line block, and use it for both the point/line aspects and the face-boundary settings. Keep the order: aspects, then `SetColor`, `SetTransparency`, `SetFaceBoundaryDraw` and `SetFaceBoundaryAspect`.

**Enums and tool count.**
- Add trailing commas to `OccSelectionMode` (one enumerator per line) and `RobotTool`.
- In `robotgraphics.h`, after `RobotTool`, add `constexpr qsizetype RobotToolCount = 2; // Enumerators of RobotTool.`. The header already includes `<QList>`, which provides `qsizetype`.
- Replace `ToolCount` in `occrobotadapter.cpp` with `RobotToolCount`.

**`cadloadworker.h`.** Add `Q_OBJECT`; the header is in an AUTOMOC target.

**`occcontroller.h`.** Add `FINAL` to its seven `Q_PROPERTY` lines.


## Concrete Steps

Work in the repository root. Record counts at the start and after each milestone:

    wc -l src/viewport/occt/*.h src/viewport/occt/*.cpp src/viewport/adapters/*.h src/viewport/adapters/*.cpp src/viewport/include/viewport/*.h src/viewport/occcontroller.cpp
    git diff --check

After Milestone 1, these searches must find no definitions or calls:

    grep -rn "isValid" src/viewport/occt/occviewcube.* src/viewport/occt/occworldaxes.* src/viewport/occt/occpart.*
    grep -rn "\.length()" src/viewport/occt

After Milestone 2:

    grep -rn "displayWorldAxes\|redisplayWorldAxes\|displayInfrastructure\|m_worldAxesDisplayed\|m_viewCubeDisplayed" src/viewport

The user builds; the agent never does. Targets: `RoboCrap3D`, then `robocrap`, in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, after checking `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt`.


## Validation and Acceptance

Run by the user, application disconnected:
- **Start-up.** The robot appears, the world axes appear at the origin, and the view cube is shown at its corner, as before.
- **View cube.** Clicking a cube face turns the view, and the cube highlights on hover in the light blue as before.
- **Zoom.** Zoom with the mouse wheel: the world axes keep a constant screen size.
- **Selection.** Import or select a scene object. The selection outline is dark grey and 2 px wide, as before, and face, edge and vertex picking on an imported surface still selects it.
- **Spindle axes.** In Machining mode with an applied spindle TCP, the TCP and collet axes toggle as before.
- **Transparent parts.** Enter the Calibration submode (needs `resources/json/calibration_sphere.json` beside the executable). The transparent spheres keep their transparency when selected.
- **Monitor change.** Move the window to another monitor; the view stays correctly sized.
- **Console.** No new console warning appears.


## Idempotence and Recovery

The edits are source edits only, and repeating a completed step changes nothing. Each milestone stands on its own, so a milestone can be reverted with git, at the user's request, without affecting the others.


## Artifacts and Notes

The current duplicate pair, for orientation:

    void OccScene::displayWorldAxes()          // occscene.cpp 108-130: length, Display ×3, Deactivate ×3, flag
    void OccScene::setFrameAxes(axes, frame)   // occscene.cpp 74-93: transform, length, Display ×3, Deactivate, Redisplay


## Interfaces and Dependencies

No dependency changes. At the end of the plan:
- `OccScene::displayViewCube()` is public;
- `OccScene::displayInfrastructure()`, `OccViewCube::isValid()`, `OccWorldAxes::isValid()`, `OccWorldAxes::length()` and `OccPart::isValid()` no longer exist;
- `OccViewWindow::viewport()` is non-const;
- `RobotToolCount` is defined in `src/viewport/include/viewport/robotgraphics.h`.

The public header `occcontroller.h` changes only by `FINAL` on its properties, which is source-compatible for QML. No contract text changes.
