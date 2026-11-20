# ExecPlans from the review of 0cd8996

The plans are written to `PLANS.md` and follow `decisions.md`. Execute them in this order: later plans assume the result of earlier ones where they share files.

| # | Plan | Covers | Depends on |
|---|---|---|---|
| 1 | `plans/review_2026-11-20_0cd8996/01_VIEWPORT_OCCT_FAILURE_HANDLING_EXECPLAN.md` | Defects 1, 5, 6 (OCCT exceptions escaping slots, removal keeps failed parts, touchpad zoom) | — |
| 2 | `plans/review_2026-11-20_0cd8996/02_CALIBRATION_JOB_AND_SETTINGS_SAFETY_EXECPLAN.md` | Defects 2, 3, 4, decisions 6–7, shared busy guard, `endRun` locals | — |
| 3 | `plans/review_2026-11-20_0cd8996/03_UI_LAYOUT_STABILITY_EXECPLAN.md` | Six interface-stability violations, defect 7 (VFD Connect), small QML defects | — |
| 4 | `plans/review_2026-11-20_0cd8996/04_VIEWPORT_OCCT_CLEANUP_EXECPLAN.md` | Reduction rank 3 (dead OCCT guards, axis duplicates, constants) | 1 |
| 5 | `plans/review_2026-11-20_0cd8996/05_DEVICES_CLEANUP_EXECPLAN.md` | Decisions 2–4, ranks 2, 8 and 10, the `acceptAxis` rule violation | — |
| 6 | `plans/review_2026-11-20_0cd8996/06_GEOMETRY_TRAJECTORIES_CLEANUP_EXECPLAN.md` | Decision 5, ranks 4, 6 and 9, the import size limit, RSI-side tidy | 2 |
| 7 | `plans/review_2026-11-20_0cd8996/07_APPLICATION_QML_CLEANUP_EXECPLAN.md` | Decision 1 (QmlChartBridge, rank 1), ranks 5 and 7, QML dead code, pragmas, FINAL | 2, 3, 6 |

These items stay open, because they need runtime measurement or a decision:
- the ScenePanel delegate rebuild (C++ S2-I5, QML S3-I1);
- hover redraw cost (C++ S6-I2);
- the `screenChanged` redundancy (C++ S7-I2);
- OCCT handle null checks (C++ S7-I1);
- the calibration run timeout (C++ S2-I2);
- the red playback status text (QML S1-I2);
- QxTextInput width (QML S1-I5);
- logo as a MenuBar item (QML S2-I1);
- the `Chamfer*` type names (C++ S5-I5);
- `selectObject`'s boolean parameter.
