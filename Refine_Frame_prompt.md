# Refine Frame — UI Requirements

Update Milestones 3–4 of `PLANAR_PROBE_SCANNING_EXECPLAN.md` using the attached Figma screenshots and requirements below. Preserve existing architecture and safety constraints. Do not implement code.

## 1. Editor

**Refine Frame** in **Measuring / Precise** opens an editor in the existing right-side **Properties** panel, with fields in this order:

- **Object**: read-only selected workpiece name plus a button to pick a placed workpiece in the 3D scene. Prepopulate from a valid Scene-tree selection. Support multiple workpieces. Confirm before switching an already configured target; clear its model-dependent face assignments.
- **Method**: dropdown; initially **By 3 Planes**. Future methods may have different settings.

## 2. Planes

- Three cube buttons select the configuration slot **XPlane**, **YPlane**, or **ZPlane**. Exactly one is active. These are measurement roles, **not** requirements to align CAD faces with world axes.
- Show the active slot label, a read-only **source BREP face ID** (not a transient viewer ID or editable name), and a face-pick button.
- The pick button enables planar-face picking on the selected workpiece in OCCT. Reject wrong-object, nonplanar, duplicate, or stale faces with a visible reason; preserve the current assignment on failure.
- Highlight the face, show the probe approach side, and provide a visible control to reverse it.
- Switching slots restores each slot's own face, approach side, grid, and exclusions.

## 3. Scan grid

Each slot defines a live-previewed **2D rectangular linear point pattern** on its selected CAD face:

- **U-direction**: `Spacing` (mm, > 0) and `#` (integer ≥ 2, number of U points).
- **V-direction**: `Spacing` (mm, > 0) and `#` (integer ≥ 2, number of V points).
- **Margins**: four inward offsets `−U`, `+U`, `−V`, `+V` (mm, ≥ 0).

Use the face's metric local U/V bounds, reduced by the four margins, as the grid's available region. Center the specified Count × Spacing pattern within that region; asymmetric margins shift its center. Validate every point against the **actual trimmed CAD face, including holes**. Flag invalid points; never silently discard them or change the requested counts/spacing. Show an error when the pattern does not fit. Update the viewport preview immediately after each edit, without **Generate Path**.

The mockup's `# = 1`, 5 mm margins, and skipped indices `(4,2)` / `(3,1)` are **illustrative, not valid defaults or a mutually consistent configuration**.

## 4. Instances to Skip

- Clicking the **Instances to Skip** list enables instance-picking mode. Clicking a preview point toggles its inclusion; skipped points remain visible and visually distinct.
- List exclusions as `(U_index, V_index)` with **1-based** indices. Selecting a row highlights its point; **Delete** restores it; **Esc** ends picking mode. Indicate the active mode.
- Store exclusions per plane **by grid index**, not world coordinates. Recalculate the preview when Count/Spacing changes. Flag out-of-range exclusions rather than silently moving them to other indices.
- Skipped points produce no contacts. Before job creation, require **≥ 3 distinct, noncollinear included points per plane**. For a 2 × 2 grid, at most one point may be skipped.

## 5. Viewport

- Live-highlight the active face and distinguish **included**, **skipped**, and **invalid** points.
- Keep workpiece picking, face picking, and point picking as separate modes; preserve camera controls and normal selection outside those modes.
- Store face bindings and point geometry in the workpiece-local frame `{W}`; changes in its scene placement must not alter these coordinates.
- Do **not** add polygon/rectangle drawing, manual point placement, or waypoint controls to this panel. Route construction and safety validation remain downstream responsibilities under the existing ExecPlan.

## 6. Job actions

Place these buttons at the **bottom of Properties** (not shown in the mockup):

- **Create Refine Job**: validate the target, three suitable independent planar faces, pattern bounds, and ≥ 3 noncollinear included points on each face. Create a separate **Refine Job** in the Scene tree containing model/mounting identity, method, face references, approach sides, grid parameters, and exclusions. **Do not generate motion.**
- **Cancel**: discard draft changes and temporary preview; close the editor without creating or modifying a job.
- Reopening a job loads its settings. In edit mode, replace the creation button with **Save Changes**; update the same job and invalidate dependent generated paths.

Show errors during editing, but prevent commit only when the final definition is invalid.

## 7. Path workflow

**Refine Frame** opens the editor → **Refine Job** stores the definition → **Generate Path** acts on the selected job and creates a separate linked **Scan Path** → **Dry Run** plays that path using existing playback. Preserve support for future refinement methods.

## 8. Appearance and constraints

- Follow Figma for layout, ordering, and compactness. Use the existing QML **`Styles`** module for colors, fonts, sizes, spacing, borders, radii, and hover/focus/disabled/selected states. Reuse shared QML controls; no hardcoded styling or separate theme. **Figma defines layout; `Styles` defines appearance.**
- No modal grid dialogs, viewport resizing during edits, or required robot connection.
- Retain existing geometry/model-revision validation, persistence, stale-result invalidation, and fixture/motion-safety rules. Job creation must neither change the active workpiece placement nor arm robot motion.

**Workflow:** `Refine Frame → Object → Method → XPlane/YPlane/ZPlane → Scan grid → Instances to Skip → Create Refine Job → select Refine Job → Generate Path → Dry Run`.
