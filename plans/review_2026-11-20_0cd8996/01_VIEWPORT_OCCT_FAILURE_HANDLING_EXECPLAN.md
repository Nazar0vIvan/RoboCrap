# Viewport: OCCT failures reported instead of escaping Qt slots, removal keeps failed parts, smooth touchpad zoom

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`, in particular "Mandatory C++ style": no lambdas, no project templates, no thrown exceptions, results through return values, at most four parameters, and try/catch only around external-library (OCCT) calls. "Mandatory code reduction" applies too. Before each logical change, re-read those two sections of `AGENTS.md`. After each milestone, review the changed declarations, bodies and callers, and record start/finish line counts here.

This plan comes from the whole-project review of commit 0cd8996. The review's findings are in `reviews/2026-11-20_0cd8996/`. Everything this plan needs is repeated below.


## Purpose / Big Picture

The 3D view of RoboCrap is drawn with OCCT (Open CASCADE Technology, the C++ CAD library in `libs/OCCT_8_0_0`). OCCT reports internal failures by throwing a C++ exception of type `Standard_Failure`. Most code that calls OCCT already catches it and turns it into an error message, but four entry points do not. Each runs inside a Qt slot (a member function called by a Qt signal–slot connection). Qt documents that an exception leaving a slot is undefined behaviour; in practice the application terminates.

The four entry points:
- changing the selected objects;
- showing or hiding the spindle TCP/collet axes;
- switching the displayed end effector (probe or spindle);
- the screen-change resize of the view window.

There are two smaller defects as well.
- When OCCT fails to remove an object's parts, the application forgets their identifiers anyway. The parts then stay on screen and can never be removed, and the error goes only to `qWarning`, not to the application console channel that the other viewport errors use.
- Wheel events from precision touchpads and high-resolution wheels, with an angle delta below 6, never zoom, yet each still schedules a redraw.

After this plan:
- An OCCT failure in any of these paths is printed to the console as `[ERROR] 3D: …`, and the application keeps running.
- A part whose removal fails is kept and retried at the next removal, with each failure reported.
- Slow touchpad scrolling zooms smoothly, in proportion to the accumulated scrolling.

To see it working, build `robocrap` and start it with no device connected. Exercise the four entry points by selecting and deselecting objects, toggling the TCP/collet axes, switching between Measuring and Machining mode, and moving the window to another monitor. Each still works, with no new console error. Zoom with a touchpad or a high-resolution mouse: the view zooms on slow scrolls. An OCCT failure itself cannot be provoked from the user interface. Its handling is therefore checked by reading the code paths listed under Validation and Acceptance.


## Progress

- [x] (2026-11-20) Plan written after reading `src/viewport/occt/occviewport.{h,cpp}`, `src/viewport/occt/occviewwindow.{h,cpp}`, `src/viewport/occcontroller.cpp`, `src/viewport/include/viewport/occcontroller.h` (slot declarations), `src/viewport/adapters/occrobotadapter.{h,cpp}`, `src/viewport/adapters/occsceneadapter.{h,cpp}`, `src/viewport/occt/occscene.{h,cpp}` (selectParts, removePart), `src/viewport/occt/occinputcontroller.{h,cpp}` (wheel), `src/application/main.cpp:120-129`, `src/application/robot/robotcontroller.cpp:22-36, 90-97`, `src/application/scene/scenemodel.cpp:70-83`, and the AGENT_CONTRACTS.md section "Viewport and CAD".
- [x] (2026-11-20) Start counts (physical lines): `occviewport.cpp` 204, `occviewport.h` 71, `occviewwindow.cpp` 231, `occviewwindow.h` 67, `occrobotadapter.cpp` 162, `occrobotadapter.h` 37, `occsceneadapter.cpp` 299, `occsceneadapter.h` 42, `occcontroller.cpp` 341, `occcontroller.h` 190, `occinputcontroller.cpp` 204, `occinputcontroller.h` 58 (total 1906).
- [x] (2026-11-20) Milestone 1: OCCT failures in the selection, spindle-frame, active-tool and screen-change paths are caught and reported. The changes:
  - **`OccRobotAdapter`.** `setSpindleFrames` and `setActiveTool` return `ViewportOperationResult` and catch `Standard_Failure`.
  - **`OccViewport`.** `setSpindleFrames`, `setActiveTool` and `setSelection` return the result. In `setSelection` the try covers only `OccScene::selectParts`, the OCCT call; the part list is built before it.
  - **`OccViewWindow`.**
    - The new private `failSurface` holds the release/fail/emit handling of `event()` and `initializeViewport()`.
    - `resizeViewport` (the `screenChanged` slot) catches `Standard_Failure` and calls `failSurface`.
    - `attachActiveTool`, `initializeViewport` and `attachRobot` emit `errorOccurred` for a rejected result.
  - **`OccController`.**
    - The new private `applySelection(OccViewport&)` replaces the six `setSelection(m_document.selected, m_document.toolSelected)` calls and emits `message` on failure.
    - `setSpindleFrames` emits `message` on failure.
    - The public header keeps every public signature; only one private declaration is added.

  Counts, start → finish:

  | File | Start | Finish |
  |---|---|---|
  | `occviewport.cpp` | 204 | 211 |
  | `occviewport.h` | 71 | 71 |
  | `occviewwindow.cpp` | 231 | 242 |
  | `occviewwindow.h` | 67 | 69 |
  | `occrobotadapter.cpp` | 162 | 174 |
  | `occrobotadapter.h` | 37 | 37 |
  | `occcontroller.cpp` | 341 | 349 |
  | `occcontroller.h` | 190 | 192 |

  C++ total: +42 lines. The growth is the required try/catch blocks and the propagation of results. The two helpers remove repetition: `failSurface` replaces three copies of three statements, and `applySelection` replaces six result checks.

  Compliance pass on the diff:
  - no lambdas, templates or `throw`;
  - every new try/catch guards an OCCT call;
  - results go through return values; `applySelection` takes the viewer by reference as an explicit operation on an existing object, not as an output;
  - at most two parameters;
  - `git diff --check` is clean.
- [x] (2026-11-20) Milestone 1 handed to the user for build and check (see the hand-off below). The user reported the build and the checks as passed.
  - **Hand-off.** Build `RoboCrap3D`, then `robocrap`, in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, whose `CMAKE_HOME_DIRECTORY` is this repository. Start disconnected, wait for the robot, then do the four checks of Validation and Acceptance, Milestone 1: selection, mode switch, spindle axes and monitor change. Expect identical behaviour and no new `[ERROR] 3D:` line.
- [x] (2026-11-20) Milestone 2: object-part removal keeps failed identifiers, retries them and reports errors through `OccController::message`. The changes:
  - **`OccSceneAdapter::removeParts`.** It first retries `m_unremovedParts` (new private member), then the given parts. A part whose OCCT removal throws goes back into the member. A `false` from `OccScene::removePart` is reported but not kept, because the scene no longer knows the part.
  - **`OccSceneAdapter::removeObject`.** It returns `ViewportOperationResult`, and its `qWarning` is gone (the `<QDebug>` include was replaced by `<utility>` for `std::exchange`).
  - **`OccViewport::removeObject`.** It forwards the result and renders when accepted.
  - **`OccController::removeObject`.** It emits `message` for a non-empty error; its public `void` signature is unchanged.
  - **Other callers.** `insertObject`, `setObjectDiagnostics`, `setDraftAxis` and the cleanup in `createPresentation` keep their flow and now leave failed parts in `m_unremovedParts` instead of dropping them.
  - **Contract.** In `docs/code context/AGENT_CONTRACTS.md`, "Viewport and CAD", the OccScene bullet now says the adapter keeps, retries and reports failed object-part removals.

  Counts, start → finish: `occsceneadapter.cpp` 299 → 304, `occsceneadapter.h` 42 → 45, `occviewport.cpp` 211 → 213 (after Milestone 1), `occviewport.h` 71 → 71, `occcontroller.cpp` 349 → 349. C++ total: +10 lines, for the retry list and the result channel.

  Compliance pass on the diff: no lambdas, templates or `throw`; the try/catch still guards only `OccScene::removePart`; the result goes through the return value; no new parameter; `git diff --check` is clean.
- [x] (2026-11-20) Milestone 2 handed to the user for build and check. The user reported the build and the checks as passed.
  - **Hand-off.** Build `RoboCrap3D`, then `robocrap`. Start disconnected, then:
    - import a plane from `data/probe_points/edge_cyl_plane/` and delete it;
    - import plane and cylinder, intersect, and delete the edge;
    - in Machining, generate a chamfer path and generate it again; the second replaces the first.

    Expect each object to disappear from the 3D view and no `[ERROR] 3D:` line. Removing a scene object that has no drawing must print nothing.
- [x] (2026-11-20) Milestone 3: wheel zoom accumulates sub-pixel scrolling and skips zero zoom steps.
  - **`OccInputController`.** New private member `m_wheelPixels`. `wheel` adds `angleDeltaY / 120 * 10` to it, applies the integer part (truncated toward zero) and keeps the fraction. It resets the remainder when the scroll direction reverses, and returns a non-accepted result, with no redraw and no camera-scale flag, when the applied step is 0.
  - **Notch behaviour.** A standard notch (±120) still gives exactly ±10 pixels.
  - **Include.** `<cmath>` was removed; `std::round` was its only use.

  Counts: `occinputcontroller.cpp` 204 → 208, `occinputcontroller.h` 58 → 60.
- [x] (2026-11-20) Milestone 3 handed to the user for build and check. The user reported the build and the checks as passed.
  - **Hand-off.** Build `RoboCrap3D`, then `robocrap`. Then:
    1. Zoom with a standard mouse wheel: each notch zooms by the same step as before.
    2. On a precision touchpad, or a mouse in high-resolution wheel mode, scroll slowly: the view now zooms in small steps; before, it did not move.
    3. Reverse direction mid-scroll: the zoom reverses at once.
- [x] (2026-11-20) Final compliance pass over the whole plan diff (12 viewport files, plus `docs/code context/AGENT_CONTRACTS.md`):
  - no lambdas, templates or `throw`;
  - every new try/catch guards an OCCT call;
  - results go through return values;
  - at most two parameters on any changed signature;
  - the public `occcontroller.h` keeps every public signature;
  - `git diff --check` is clean;
  - the contract sentence was added in Milestone 2.

  Counts of the 12 files: 1906 → 1964 (+58 C++ lines). The growth is the try/catch blocks, the result channels, the retry list and the wheel remainder. The two helpers `failSurface` and `applySelection` keep the remaining copies to one line each.


## Surprises & Discoveries

- Observation: `OccController::removeObject` is called by `SceneModel::removeObjects` for every removed scene object, including objects that have no drawing and were therefore never installed in the adapter. The plan's text for an unknown identifier, `{false, "Object drawing is not installed."}`, would have printed that line for each such removal.
  Evidence: `SceneObject::drawing()` returns `std::nullopt` for metadata-only objects (`src/application/scene/sceneobject.cpp`, "Metadata-only objects have no drawing"); `SceneModel::removeObjects` (`src/application/scene/scenemodel.cpp` 70-83) calls `m_viewport->removeObject(id)` unconditionally. Implemented instead as `{false, {}}`: not accepted, no error. See Decision Log.


## Decision Log

- Decision: catch the robot-presentation failures inside `OccRobotAdapter::setSpindleFrames` and `OccRobotAdapter::setActiveTool`, and the selection failure inside `OccViewport::setSelection`. Report them through return values, not by tearing the viewport down.
  Rationale: the adapters already catch `Standard_Failure` around the same calls in `load`, `clear` and `applyTransforms`. `OccViewport::setSelection` is the only place that calls `OccScene::selectParts`. A selection or axis failure leaves the scene usable, so releasing the native surface (what `OccViewWindow::event` does) would be needlessly drastic.
  Date/Author: 2026-11-20, plan author.
- Decision: the screen-change resize is caught in `OccViewWindow::resizeViewport` with the same handling as `OccViewWindow::event` (release the surface, mark the presentation failed, emit `errorOccurred`). One private helper, `failSurface`, holds that handling for the three sites that need it.
  Rationale: a failed resize can leave the OCCT view inconsistent with the native window, the same situation `event()` already handles that way. A shared helper is smaller than a third copy of the three statements.
  Date/Author: 2026-11-20, plan author.
- Decision: `OccController` reports selection failures through one private helper, `applySelection(OccViewport& drawing)`, instead of a result check at each of its six call sites.
  Rationale: all six calls pass the document's own selection (`m_document.selected`, `m_document.toolSelected`). One helper both applies and reports it, so every site stays a single line, and no reporting is forgotten when a call is added.
  Date/Author: 2026-11-20, implementing agent (Milestone 1).
- Decision: `OccSceneAdapter::removeObject` returns `{false, {}}` for an identifier it never installed: not accepted, no error text.
  Rationale: such removals are ordinary (objects without a drawing), so reporting them would flood the console; the earlier `bool` returned false silently too. See Surprises & Discoveries.
  Date/Author: 2026-11-20, implementing agent (Milestone 2).
- Decision: failed part removals are kept in a new member `OccSceneAdapter::m_unremovedParts` and retried at the start of every later `removeParts` call.
  Rationale: `OccScene::removePart` erases its own record only after OCCT succeeds, so a failed part stays displayed. The adapter must keep the identifier, or nothing can ever remove the part. The robot adapter (`OccRobotAdapter::clear`) already keeps failed identifiers; reusing that idea needs one member, not a new class.
  Date/Author: 2026-11-20, plan author.
- Decision: `OccController::removeObject` keeps its `void` signature and reports errors with `emit message(error, true)`.
  Rationale: its only caller, `SceneModel::removeObjects` (`src/application/scene/scenemodel.cpp:70-83`), cannot act on a failure, and the public header API stays unchanged. The sibling slots `setObjectVisible` and `setDraftAxis` report in the same way.
  Date/Author: 2026-11-20, plan author.


## Outcomes & Retrospective

(2026-11-20) Complete. All three milestones were built and checked by the user, and every check passed.

The purpose is met:
- **No OCCT failure escapes a Qt slot.** The selection, spindle-axis, active-tool and screen-change paths catch `Standard_Failure` and report it as `[ERROR] 3D: …` through `OccController::message` or `errorOccurred`.
- **Failed removals are kept.** Object parts whose OCCT removal fails stay recorded in `OccSceneAdapter::m_unremovedParts`, are retried at the next removal and are reported.
- **Smooth touchpad zoom.** Precision scrolling zooms in proportion to the accumulated delta, and a standard wheel notch zooms as before.

Size: the 12 viewport files went from 1906 to 1964 lines (+58). The growth is the required failure handling and result channels.

There is one deviation from the plan text. An identifier that was never installed is removed silently, `{false, {}}`, because objects without a drawing are removed routinely (see Surprises & Discoveries).

The OCCT failure paths cannot be triggered from the UI, so they are verified by code reading only.

Remaining, outside this plan:
- `plans/review_2026-11-20_0cd8996/04_VIEWPORT_OCCT_CLEANUP_EXECPLAN.md` builds on these signatures (`failSurface`, the `ViewportOperationResult` returns);
- the hover-redraw cost and the `screenChanged` redundancy stay open review items.


## Context and Orientation

Read `AGENTS.md` and the "Viewport and CAD" section of `docs/code context/AGENT_CONTRACTS.md` before changing code.

The viewport is the CMake target `RoboCrap3D` in `src/viewport/`. It is a static QML module (URI `RoboCrap.Viewport3D`) that links OCCT privately. Its parts, from the application side down to OCCT:

- `src/viewport/include/viewport/occcontroller.h` and `src/viewport/occcontroller.cpp` define `RoboCrap3D::OccController`, a QObject owned by `main()`. It keeps the accepted drawing document (`m_document`: objects, selection, draft axis, loaded models) and the accepted robot pose (`m_pose`). The application calls its public slots, which `main.cpp:112-129` connects to:
  - `setSelectedObjects` (from `WorkspaceController::selectionChanged`);
  - `setToolSelected` (from `WorkspaceController::toolSelectedChanged`);
  - `setActiveTool` and `setSpindleFrames` (from `RobotController::synchronizeTools`, `src/application/robot/robotcontroller.cpp:90-97`, itself a slot of end-effector change signals);
  - `removeObject` (from `SceneModel::removeObjects`).

  `OccController::message(QString text, bool error)` is the console channel: `main.cpp:131` connects it to `logViewportMessage`, which prints `[ERROR] 3D: <text>`. `OccController::setError` stores an error and also emits `message`.
- `src/viewport/occt/occviewwindow.{h,cpp}` define `OccViewWindow`, the native `QWindow` that QML embeds. It owns `std::unique_ptr<OccViewport> m_viewport` while a native surface exists. `OccViewWindow::event` wraps every window event in `try { … } catch (const Standard_Failure&)`. On failure it calls `releaseSurface()`, sets `m_presentationFailed = true` and emits `errorOccurred`, which `OccController::setError` receives. Mouse and wheel handlers run inside that `event()` call, so they are already protected. The `QWindow::screenChanged` signal, however, is connected directly to the private slot `resizeViewport` (constructor, line 23), outside `event()`.
- `src/viewport/occt/occviewport.{h,cpp}` define `OccViewport`, a plain class that owns the OCCT viewer (`OccViewer`), the scene (`OccScene`), and two adapters. `OccRobotAdapter` draws the robot links and the two end-effector models. `OccSceneAdapter` draws application objects such as planes, cylinders, paths and spheres. `OccViewport::setSelection` (lines 149-155) calls `OccScene::selectParts` directly, which performs `new AIS_Shape`, `AIS_InteractiveContext::Display` and `SetZLayer` (`src/viewport/occt/occscene.cpp:340-375`). `OccViewport::setSpindleFrames` (174-178) and `OccViewport::setActiveTool` (114-120) forward to the robot adapter.
- `src/viewport/adapters/occrobotadapter.cpp`: in this file,
  - `setSpindleFrames` (123-128) calls `synchronizeSpindleFrames`, which calls `OccScene::setSpindleTcpFrame`/`setSpindleColletFrame` (`AIS_InteractiveContext::Erase`/`Display`/`Redisplay`);
  - `setActiveTool` (138-143) calls `showActiveTool`, which calls `OccScene::setPartVisible` and through it `Display`;
  - neither has a try/catch, while `load` (34-66), `clear` (77-90) and `applyTransforms` (101-119) catch `Standard_Failure` around the same calls.
- `src/viewport/adapters/occsceneadapter.cpp`: `removeParts` (124-135) removes a list of parts, catching `Standard_Failure` per part, and returns an error text. Its callers all drop the identifiers even when the text is not empty:
  - `insertObject` (205-217): replacing an object;
  - `removeObject` (219-228): only `qWarning`, returns true;
  - `setObjectDiagnostics`: diagnostics replacement;
  - `setDraftAxis`: draft-axis replacement.

  `OccScene::removePart` (`occscene.cpp:305-318`) erases its own record only after `AIS_InteractiveContext::Remove` returns, so a part whose removal threw stays in the scene and on screen.
- `src/viewport/occt/occinputcontroller.cpp`, `wheel` (74-90), computes `delta = round(angleDeltaY / 120 * 10)` pixels and calls `V3d_View::Zoom`. Qt reports wheel rotation in eighths of a degree; one standard notch is 120. Precision touchpads send small values such as 2, 5 or 15, which round to 0 or lose their fraction. The function returns `needsRedraw` and `cameraScaleChanged` even for a zero step, so `OccViewport::applyInput` recomputes the axes and schedules a render for nothing.

Terms used here:
- **AIS**: OCCT's interactive-object layer.
- **Part identifier**: `OccScene::PartId`, a `std::size_t` counter value that OccScene assigns. It is never reused.
- **ViewportOperationResult**: `{bool accepted; QString error;}`, declared in `occcontroller.h`, the project's result type for viewport operations.


## Plan of Work

### Milestone 1: catch and report the four unprotected OCCT paths

In `src/viewport/adapters/occrobotadapter.h` and `.cpp`:
- change `void setSpindleFrames(...)` to return `ViewportOperationResult`;
- change `bool setActiveTool(RobotTool)` to return `ViewportOperationResult`.

In both bodies, wrap the call to `synchronizeSpindleFrames()` or `showActiveTool()` in `try { … } catch (const Standard_Failure& failure)`, as `applyTransforms` does. Return `{false, QStringLiteral("Spindle axes: %1").arg(QString::fromUtf8(failure.what()))}` or `{false, QStringLiteral("End-effector display: %1")…}`. Keep the invalid-tool check in `setActiveTool` and the `showActiveTool()` false result as `{false, QStringLiteral("Cannot update the end-effector presentation.")}`. The members `m_spindleTcpFrame`, `m_spindleColletFrame` and `m_activeTool` are still assigned before the call, as today, so a later successful call or a recreated surface replays the latest request.

In `src/viewport/occt/occviewport.h` and `.cpp`, change these to return `ViewportOperationResult`:
- `setSpindleFrames` and `setActiveTool`, which forward the adapter result and call `requestRender()` as today;
- `setSelection`, which wraps the `selectedParts`/`selectParts` lines in a try/catch on `Standard_Failure`, returning `{false, QStringLiteral("Selection: %1")…}`, and always calls `requestRender()`.

`setActiveTool` keeps its `!isValid()` early return as `{false, QStringLiteral("Viewport is unavailable.")}`.

In `src/viewport/occt/occviewwindow.{h,cpp}`:
- **New helper.** Add the private method `void failSurface(const QString& error)` containing the three statements `releaseSurface(); m_presentationFailed = true; emit errorOccurred(error);`. Use it in the `catch` of `event()` (lines 114-120) and of `initializeViewport()` (193-199); both currently write these statements inline. (Requirement: the screen-change slot needs the same handling as `event()`; a shared method avoids a third inline copy. Reuse of `event()` itself is impossible because the slot is not an event.)
- **`resizeViewport()`.** Wrap `m_viewport->resize()` in try/catch on `Standard_Failure` and call `failSurface(QStringLiteral("Viewport resize failed: %1").arg(…))`.
- **`attachActiveTool()`.** Use the returned result: `if (m_viewport) { const auto result = m_viewport->setActiveTool(m_activeTool); if (!result.accepted) emit errorOccurred(result.error); }`.
- **`initializeViewport()` and `attachRobot()`.** These also call `setSpindleFrames` and `setSelection`; emit `errorOccurred` for a non-accepted result, keeping the existing try blocks.

`OccViewWindow::setActiveTool` stays `void`, because `OccController::setActiveTool` calls it and already receives `errorOccurred` through `setError`.

In `src/viewport/occcontroller.cpp`, every `drawing->setSelection(...)` and `drawing->setSpindleFrames(...)` call reports a non-accepted result with `emit message(result.error, true)`. The calls are in `insertObject` (line 78), `removeObject` (97), `setObjectVisible` (107), `setSpindleFrames` (158), `setSelectedObjects` (164), `setToolSelected` (170) and `finishModelLoad` (around 321). The document members (`m_document.selected`, `m_pose.tcp`, …) are still updated before the call, as today: the document is the replay source for a recreated surface. Do not change the public signatures in `occcontroller.h`.

### Milestone 2: keep and retry parts whose removal failed

In `src/viewport/adapters/occsceneadapter.h`, add the private member `std::vector<OccScene::PartId> m_unremovedParts;` with the comment "Parts whose OCCT removal failed; retried by every later removeParts call." The requirement for this member is recorded in the Decision Log.

In `occsceneadapter.cpp`, change `removeParts` as follows:
- build the list to remove as `m_unremovedParts` followed by `ids`, then clear `m_unremovedParts`;
- for each identifier, call `m_scene.removePart(id)` inside the existing try;
- when it returns false or throws, push the identifier back into `m_unremovedParts` and add the existing error text.

`removePart` returns false only for an unknown identifier or an invalid scene. An unknown identifier must not be retried forever, so push it back only when the failure was an exception. A `false` result is reported but not kept: OccScene no longer knows the part.

The return type stays `QString`. Callers keep their current flow, so `insertObject`, `setObjectDiagnostics` and `setDraftAxis` replace their part lists and return the error text in `ViewportOperationResult`, as today.

Change `removeObject` to return `ViewportOperationResult`: `{false, QStringLiteral("Object drawing is not installed.")}` for an unknown id, otherwise erase the entry and return `{true, error}`, and delete the `qWarning` line. Change `OccViewport::removeObject` to return the adapter result and request a render when it is accepted. In `OccController::removeObject`, emit `message(result.error, true)` when the error is not empty.

### Milestone 3: wheel zoom with sub-pixel accumulation

In `src/viewport/occt/occinputcontroller.h`, add the private member `double m_wheelPixels = 0.0;` with the comment "Zoom pixels not yet applied; precision devices send fractions of a notch." This is the gesture state the class already owns. The requirement for the member: without it, a delta below 6 units is lost. Reuse fails because no existing member holds wheel state.

In `wheel`, compute `m_wheelPixels += angleDeltaY / kNotch * kPixelsPerNotch;`, then `const int delta = static_cast<int>(m_wheelPixels);` (truncation toward zero), then `m_wheelPixels -= delta;`.
- If `delta == 0`, return `{}` (not accepted, no redraw).
- Otherwise call `m_view->Zoom(...)` as today and return the accepted result.

Reset `m_wheelPixels` to 0 when the sign of `angleDeltaY` differs from the sign of the stored remainder, so that reversing direction does not first cancel a stale fraction. Leave `cancelGesture` unchanged: it concerns mouse drags.


## Concrete Steps

Work in the repository root (`D:\Qt\QtProjects\RoboCrap` or `E:\Qt\QtProjects\RoboCrap`; check the session's working directory). Before each milestone, record line counts:

    wc -l src/viewport/occt/occviewport.* src/viewport/occt/occviewwindow.* src/viewport/adapters/occ*.h src/viewport/adapters/occ*.cpp src/viewport/occcontroller.cpp src/viewport/include/viewport/occcontroller.h src/viewport/occt/occinputcontroller.*

After each milestone:

    git diff --check
    git diff --stat -- src/viewport

Expect no whitespace errors, and changes only in the files this plan names.

The user builds; the agent never does. The build uses the existing build directory `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`; first verify that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository. The build targets are `RoboCrap3D` and `robocrap`, with the Qt kit from that cache, `C:/Felgo/Felgo/mingw_64`.


## Validation and Acceptance

**Milestone 1.** A code-path check, done by the agent: after the change, every call chain from a slot to an OCCT display call passes through a `catch (const Standard_Failure&)`. Each must lead to a `try`:
- `OccController::setSelectedObjects` → `OccViewport::setSelection`;
- `OccController::setSpindleFrames` → `OccRobotAdapter::setSpindleFrames`;
- `OccController::setActiveTool` → `OccViewWindow::setActiveTool` → `attachActiveTool` → `OccRobotAdapter::setActiveTool`;
- `QWindow::screenChanged` → `OccViewWindow::resizeViewport`.

To find all slot-reachable calls of these functions, search:

    grep -n "setSelection\|setSpindleFrames\|setActiveTool\|resize()" src/viewport -r

Runtime check, done by the user: build, start the application with no device connected, and wait for the robot to appear. Then:
- select and deselect a scene object in the Scene panel, and click the end-effector model in the 3D view;
- switch to Machining mode and back (the displayed end effector changes);
- in Machining mode, toggle the spindle TCP and collet axes;
- move the window to another monitor.

Each action behaves as before, and the console shows no new `[ERROR] 3D:` line.

**Milestone 2.** Code-path check: every caller of `removeParts` either stores the error in its returned `ViewportOperationResult` (insertObject, setObjectDiagnostics, setDraftAxis, removeObject), or `OccController` prints it through `message`. No call drops a part identifier whose removal threw. Runtime check (user): delete an imported surface, and regenerate a machining path, which replaces it. The objects disappear and no error is printed.

**Milestone 3.** User: on a precision touchpad (or a mouse in high-resolution wheel mode), scroll slowly. The view zooms in small steps; before the change it did not move. A standard mouse wheel notch still zooms by the same 10 pixels as before.


## Idempotence and Recovery

All edits are source edits in `src/viewport`. Repeating a step that is already applied changes nothing. To undo a milestone, restore the named files with `git checkout -- <file>`, and only when the user asks, because discarding changes needs their request.


## Artifacts and Notes

The current unprotected chain for the spindle axes, for orientation:

    RobotController::synchronizeTools (slot)          robotcontroller.cpp:90-97
      OccController::setSpindleFrames                 occcontroller.cpp:154-159
        OccViewport::setSpindleFrames                 occviewport.cpp:174-178
          OccRobotAdapter::setSpindleFrames           occrobotadapter.cpp:123-128   (no catch)
            OccScene::setSpindleTcpFrame / setFrameAxes  occscene.cpp:64-93 (Erase/Display/Redisplay)


## Interfaces and Dependencies

No dependency changes. At the end of the plan these signatures exist:

    // src/viewport/adapters/occrobotadapter.h
    ViewportOperationResult setSpindleFrames(const std::optional<M4d>& tcp, const std::optional<M4d>& collet);
    ViewportOperationResult setActiveTool(RobotTool active);

    // src/viewport/adapters/occsceneadapter.h
    ViewportOperationResult removeObject(quint32 id);
    // private: std::vector<OccScene::PartId> m_unremovedParts;

    // src/viewport/occt/occviewport.h
    ViewportOperationResult setSpindleFrames(const std::optional<M4d>& tcp, const std::optional<M4d>& collet);
    ViewportOperationResult setActiveTool(RobotTool active);
    ViewportOperationResult removeObject(quint32 id);
    ViewportOperationResult setSelection(const QList<quint32>& objectIds, bool toolSelected);

    // src/viewport/occt/occviewwindow.h (private)
    void failSurface(const QString& error);

    // src/viewport/occt/occinputcontroller.h (private)
    double m_wheelPixels = 0.0;

The public header `src/viewport/include/viewport/occcontroller.h` keeps all its signatures. After Milestone 2, update the "Viewport and CAD" section of `docs/code context/AGENT_CONTRACTS.md` with one sentence: application-object parts whose OCCT removal failed stay recorded in the scene adapter, are retried at the next removal, and are reported through `OccController::message`. The robot sentence "Cleanup keeps part IDs until removal succeeds and reports failures without retrying" stays as it is.
