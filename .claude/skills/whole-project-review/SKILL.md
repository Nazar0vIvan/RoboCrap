---
name: whole-project-review
description: >-
  Read-only whole-project review of RoboCrap at the committed and pushed HEAD: runs
  /qt-development-skills:qt-cpp-review and /qt-development-skills:qt-qml-review in codebase scope,
  then checks AGENTS.md rules, architecture and recorded design decisions, and returns one report.
  Invoke only when the user asks for a whole-project review.
disable-model-invocation: true
---

# Whole-project review

Perform one read-only whole-project review of RoboCrap at the committed HEAD.
Return one report in the chat. Change no project file, run no CMake configure or build, and use
no live hardware. Use the review skills' own shard agents; start no separate sessions.

This skill overrides two instructions of the review skills, and only these two:

- (a) "The linter is authoritative": the skill reports keep every lint row unchanged; only the
  final report rejects rows (step 8).
- (b) The cap of 10 investigation targets: it applies to each skill report; the full I-block list
  is kept separately (step 4).

## 0. Verify the reviewed state

- Record `git rev-parse HEAD`, the branch, and `git rev-parse @{u}`.
- Run `git status --porcelain`. If any file that step 1 places in scope differs from HEAD, or HEAD
  differs from its upstream, stop and report the differing paths. List other differing files in
  the report and continue.

## 1. Establish coverage

- Read AGENTS.md, every file in docs/code context, and every contract those files link to,
  including controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md.
- From the root and module CMakeLists.txt files, QML registrations, qmldir files and resource
  declarations, list the active targets and their C++ and QML sources. Apply the repository's
  exclusions (build, libs, python, old*).
- Write the file lists and their line counts to the session scratchpad.

## 2. Prepare qmllint before running the QML skill

- Find build/*/CMakeCache.txt whose CMAKE_HOME_DIRECTORY is this repository. If several match,
  prefer USE_HOT_RELOAD=OFF, then the newest generated .qmltypes. Read CMAKE_PREFIX_PATH, the
  compiler, USE_HOT_RELOAD and FAST_QML_BUILD from it.
- Compare the newest .qmltypes modification time with `git log -1 --format=%ct`. If the types are
  older than HEAD, report the build as stale and treat unresolved-type rows as tool limits.
- If a matching cache is found, take the Qt kit from CMAKE_PREFIX_PATH. Use the
  build directory's QML module output only as qmllint import paths; never review it. Run no build.
- If the cache is missing, stale or points elsewhere, run qmllint with the kit from the cache if
  one is named, and report unresolved-type rows as tool limits, not defects.

## 3. Run both skills once, each in codebase scope with universal rules

- /qt-development-skills:qt-cpp-review — codebase scope, no `framework` argument, explicit C++
  file list from step 1. If the skill suggests framework mode, do not enable it; note the
  suggestion.
- /qt-development-skills:qt-qml-review — codebase scope, explicit QML file list from step 1, with
  `--qmllint=<kit>/bin --qmllint-args="-I <import paths>"` from step 2. Record the kit, the import
  paths, the printed qmllint status, and which project types it resolved.
- Save both skill reports unchanged in the scratchpad.

## 4. Resolve cross-module questions

- Collect every I-block from both skills, including those beyond the cap of 10. Each I-block
  whose "Unverified because" names another module or language is an open question. Follow it to
  its producer and consumer, or mark it unresolved.

## 5. Check the project rules, which the skills do not cover

- Grep the active C++ for lambda introducers, `template\s*<`, `\bthrow\b` and `\bcatch\s*\(`.
  Read each hit; a try/catch is a violation only if the guarded call is not external-library code.
- In headers, list function and constructor declarations with more than four parameters or with
  a non-const reference or pointer parameter. Read each body; report the parameter only if it is
  an output or in-out channel.
- In QML, find messages, progress indicators and controls whose visible, text or content changes
  in ordinary state changes. Report each one whose change resizes the viewport, a panel or a
  window, or displaces other controls ("Mandatory interface stability").
- Every mitigation, from a skill or from this review, must comply with AGENTS.md: no lambdas,
  project templates, tests, new exceptions or output parameters, and Rule of Zero for ordinary
  values. Rewrite a conflicting skill mitigation and say it was rewritten.

## 6. Review the architecture independently

- Build each target's dependency graph from target_link_libraries and #include directives.
  Compare it with "Build and modules" in AGENT_CONTRACTS.md, and report every edge that the
  contract does not allow.
- Trace once each: path generation through playback; one QML device command through its PLC
  reply; each row of ARCHITECTURE.md "4. Threads and ownership" (Generate, Calibration, RSI
  sampling through setCorrections on ControlIO, FTS save, CAD QThread jobs); and shutdown as
  ordered in ARCHITECTURE.md "5. Shutdown".
- From the code and its callers, assess class responsibilities, cohesion, coupling, duplication,
  obsolete code and unnecessary abstractions.
- Before declaring code unused, check QML registrations, string-based invocations (invokeMethod,
  QML string calls, JSON/config keys) and external consumers (KRL programs, RSI XML, saved files).
  Entries under "Retained interfaces" in ARCHITECTURE.md are not dead code unless that decision
  is itself reconsidered.
- When tracing callers, exclude build/, libs/, python/ and old*; generated moc and qrc files are
  not callers.

## 7. Evaluate decisions on their merits

- Use AGENT_CONTRACTS.md and ARCHITECTURE.md to identify the requirement and rationale behind
  each current design.
- Give a verdict (keep, change, or insufficient evidence) for each recorded decision that a trace
  in step 6 passed through, that a finding touches, or that the code contradicts. Cite the
  implementation and its callers by file and line. List the other recorded decisions as not
  examined.
- For a proposed change, state what happens today, the concrete cost or failure, the smallest
  viable alternative, where responsibility would move, and which behavior must survive.
  A proposal may explicitly reconsider an existing contract.

## 8. Reconcile the findings

- Verify every lint row, D-block and I-block, and every finding from steps 5 and 6, against the
  code, the Qt 6.8 documentation, C++17 and AGENTS.md. Reject a lint row only with the code that
  disproves it.
- Pay particular attention to QObject ownership and thread affinity, QML bindings and model roles,
  C++ and Eigen lifetimes, API result channels, error recovery and performance.
- At a C++/QML boundary, inspect both ends and report one finding per cause.
- Explain each consequential conflict between sources and the evidence that settles it.
  Label every unmeasured performance claim as an estimate.

## 9. Report

Return one concise report, in this order:

- Reviewed state: commit SHA, branch, upstream match, other differing files.
- Confirmed defects, highest impact first: file and line, triggering condition, consequence,
  suggested correction.
- Project-rule violations from step 5: file and line, rule, correction.
- Architectural decisions: verdict, evidence, trade-off; then the decisions not examined.
- Reduction opportunities, ranked by physical lines removed minus lines added: what is removed,
  behavior kept, files affected. Exclude RSI files.
- Rejected lint rows and rejected skill findings, each with its disproving evidence.
- Coverage and limits: targets and boundaries examined; files not read; unresolved questions;
  the qmllint kit, import paths and status; shard counts per skill; checks executed and checks
  skipped, each with its reason.
- Run record, for comparison with later runs at other commits: lint rows reported/rejected,
  D-blocks reported/confirmed/rejected, I-blocks resolved/unresolved, per skill.
