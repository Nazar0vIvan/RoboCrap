---
name: whole-project-review
description: >-
  Read-only review of RoboCrap at committed and pushed HEAD: run the installed
  qt-cpp-review and qt-qml-review skills, validate their findings, and independently
  check project rules, architecture and documented decisions. Invoke only on request.
---

# Whole-project review

Review the full project once and return one report. Never modify files, configure/build CMake,
use live hardware or start unrelated sessions. Let Qt skills manage their own agents.

## Scope and revision

- Read `AGENTS.md`, `docs/code` context and linked contracts.
- Enumerate active targets and C++/QML files from CMake, QML registrations, `qmldir` and
  resources. Exclude `build/`, `libs/`, `python/`, `old*` and generated files from review.
  Note file lists and line counts outside the repository.
- Record HEAD, branch, upstream and `git status --porcelain`. Stop if HEAD differs from
  upstream or any in-scope file differs from HEAD; report paths. Note other dirty files.

## Run the installed Qt review skills

- Read the installed `qt-cpp-review` and `qt-qml-review` `SKILL.md` files; execute each entire
  workflow **once** in codebase scope with explicit file lists. Use universal C++ rules, not `framework`.
- For QML, locate the matching `build/*/CMakeCache.txt` (`CMAKE_HOME_DIRECTORY` = this
  repo); prefer `USE_HOT_RELOAD=OFF`, then newest `.qmltypes`. Record kit/compiler,
  `USE_HOT_RELOAD`, `FAST_QML_BUILD`; use `--qmllint=<kit>/bin --qmllint-args="-I <paths>"`
  when kit/imports exist, otherwise `--qmllint`. Never review build output. If cache/types
  are missing, mismatched or older than HEAD, unresolved types are tool limits, not defects.
- Preserve both skill reports and all lint rows verbatim in session notes. Their 10-target
  investigation cap applies **per skill report**; retain the full I-block lists separately.
  Trace cross-module/language I-blocks to producer and consumer or mark unresolved.

## Independent project review

- Check active C++ for lambdas, project templates, `throw` and `catch` (allow catch around
  external-library calls). Inspect declarations with >4 parameters and non-const pointer/
  reference parameters; flag the latter only when used as output/in-out channels.
- Check state-dependent QML text, visibility and content for interface-resizing/control displacement.
- Compare `target_link_libraries` and `#include` dependency edges with `AGENT_CONTRACTS.md`
  “Build and modules”. Trace path generation to playback, QML command to PLC reply,
  “Threads and ownership” (Generate, Calibration, RSI sampling/ControlIO, FTS save, CAD
  QThread jobs) and ordered shutdown from `ARCHITECTURE.md`.
- Evaluate responsibility, coupling, duplication, unused code and unnecessary abstractions.
  Before calling code dead, check QML registration, dynamic/string calls, config keys,
  external KRL/RSI/file consumers and documented “Retained interfaces”.
- Judge touched/contradicted decisions **keep/change/insufficient evidence** using implementation
  and caller `file:line`; list unexamined decisions. Changes need concrete cost, minimal
  alternative, responsibility shift and preserved behavior.

## Reconcile and report

- Independently verify **every** lint row, D-block, I-block and project finding against code,
  Qt 6.8, C++17 and `AGENTS.md`. Reject only with disproving evidence; inspect both sides
  of C++/QML boundaries, deduplicate causes, label unmeasured performance as estimates.
  Rewrite and disclose mitigations violating `AGENTS.md` (no lambdas, project templates,
  tests, new exceptions, output parameters; Rule of Zero).
- Report in order: (1) SHA/branch/upstream/dirty files; (2) confirmed defects by impact,
  with `file:line`, trigger, consequence and fix; (3) project-rule violations;
  (4) architectural decision verdicts and unexamined decisions; (5) reductions ranked
  by physical lines removed minus added, excluding RSI files; (6) rejected findings and
  evidence; (7) coverage, skipped checks, unresolved issues, qmllint kit/imports/status/
  resolved types and shard counts; (8) per-skill lint reported/rejected, D-blocks
  reported/confirmed/rejected and I-blocks resolved/unresolved for later comparisons.
