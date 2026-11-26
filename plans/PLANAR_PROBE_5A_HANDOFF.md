# Implement 5a — ordered material contacts from a saved Refine Job

Implement only **5a** in [the planar probe ExecPlan](PLANAR_PROBE_SCANNING_EXECPLAN.md#5a-expand-the-saved-grids-into-ordered-material-contacts). Steps 4c and 4d passed. Start from the current working tree and preserve its existing edits. Read `AGENTS.md`, `PLANS.md`, ExecPlan §5a, and the **Geometry and scene** and **Viewport and CAD** sections of `docs/code_context/AGENT_CONTRACTS.md`.

## Implement

1. **Start at the saved job.** Add a C++ scene operation taking the selected Refine Job's current scene ID. In `src/application/scene/scenemodel.cpp`, use the saved `SceneObject::refineJob()` and the existing `resolveRefineTarget` logic. After restart, resolve the live mounting before using face indices. The stored `workpieceSceneId` belongs to the previous process. Compare the saved `modelSha256` with the loaded model digest and retain `DrawingFaceSelection::revisionError()` to detect a BREP replaced on disk since loading. Return a diagnostic if the job or target is missing, the mounting is ambiguous, the CAD is unavailable, or the model changed.
2. **Reconstruct the editor's grid with the editor closed.** In `src/viewport/occcontroller.cpp`, inspect `checkRefineJob` and both callers, `refineJobDefinition` and `restoreRefineGrids`. Reuse their validation with the resolved target and loaded CAD as inputs. Today the validator copies `m_document.face`, and restore installs display grids. Generation must work without selecting a viewport face or changing overlays. Read counts from `countsUV`. For each axis, pass `-1` to the grid calculation when `autoSpacingUV` is true for that axis, otherwise pass its saved `spacingMmUV`. Preserve margins in left/top/bottom/right order and the `skippedUV` pairs, as in `src/application/qml/Views/Workspace/RefineFramePanel.qml`.
3. **Return three ordered lists, or one error and no partial result.** Visit XPlane, YPlane, ZPlane in that order. Within each face, visit V rows from 1 upward. Visit U in ascending order on odd V rows and descending order on even V rows. Omit skipped cells without renumbering. Use the classified `DrawingGrid::points` (U varies fastest). Keep the role, source face index, original 1-based (U,V), material point in millimetres in workpiece coordinates, and signed unit normal in the same frame. In `src/viewport/occt/occviewport.cpp`, `DrawingFaceSelection::preview` already calls `localFaceAxis` for the oriented CAD normal. Apply `reversed` once to the returned normal only. Keep points in workpiece coordinates even when the mounting moves.

## Prove it and stop

Use a disposable copy of the saved GE-22 job in `settings/planar_probe_scans.json`. For the matching BREP, the saved X/Y/Z face indices are 2/1/5. Set up 2×2 valid points per face with automatic spacing and only XPlane (2,1) skipped. With those settings loaded and the CAD ready, call the new scene operation directly with the reloaded Refine Job’s current scene ID. Keep Refine Frame closed. Inspect the returned indices in order.

| Role | Contacts in traversal order |
| --- | --- |
| XPlane | (1,1), (2,2), (1,2) |
| YPlane | (1,1), (2,1), (2,2), (1,2) |
| ZPlane | (1,1), (2,1), (2,2), (1,2) |

Expect **11 contacts**. Compare local positions with the editor grid by face and index. Reverse XPlane in the scratch definition and repeat. Only its normal changes sign. A changed BREP must return an error before any contacts are returned. Record the actual counts, index order and reversal result. If the changed C++ cannot be executed in this session, identify the pending runtime check in the ExecPlan. Source inspection alone does not establish that the operation returned 11 contacts.

Stop at the numerical result. **5b** owns Generate Path wiring, G1/G2 selection, ball offsets, robot motion and Scan Path publication. **5c** owns playback and visual clearance review. Leave the user's settings file untouched. Follow the repository's C++ review and line-count rules, run `git diff --check`, and record actual evidence and remaining checks under 5a in the ExecPlan. No new project tests, build/configure/lint without request, hardware actions, or Git staging/commits/pushes.
