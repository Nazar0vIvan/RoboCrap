# Devices cleanup: PlcMessageManager as a gadget, only the PLC requests that are used, renamed connect/disconnect, shared endpoint parsing

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log` and `Outcomes & Retrospective` must be kept up to date as work proceeds. It is maintained in accordance with `PLANS.md` at the repository root.

Five rules override `PLANS.md` for this plan.
1. The implementing agent never runs a build, CMake configure, QML lint, the application, a test or a check script; the user builds and tests everything. At the end of every milestone the agent stops and hands the user a check list: which targets to build in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug` (after confirming that `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt` is this repository), which disconnected and equipment checks to run (from Validation and Acceptance), and the expected results. The agent records the hand-off in `Progress` and starts the next milestone only after the user reports the result. A reported failure is fixed first and recorded in `Surprises & Discoveries`.
2. The agent never operates equipment: it never connects to the PLC, RSI or FTS, never sends outputs, never sets a sensor bias, never starts a KRL program and never moves the robot. Equipment checks are part of acceptance; the user runs them, with the equipment available.
3. There are no git staging operations, commits, `git rm` or `git mv` unless the user asks.
4. There are no project tests or test harnesses.
5. Every changed line obeys `AGENTS.md`: "Mandatory C++ style" (no lambdas, no project templates, no thrown exceptions, results through return values, no output or in-out reference parameters, at most four parameters) and "Mandatory code reduction". Before each logical change, re-read both sections. After each milestone, review the changed declarations, bodies and callers, and record start/finish line counts here.

Before editing, read the "Devices and protocols" and "Build and modules" sections of `docs/code context/AGENT_CONTRACTS.md`. The contract also names these files as required reading: `src/application/devices/devicehub.{h,cpp}`, `src/application/devices/devicerunner.{h,cpp}`, `src/devices/abstractdevice.cpp`, `src/devices/include/devices/abstractdevice.h` and `src/application/main.cpp`.

This plan comes from the whole-project review of commit 0cd8996 (findings in `reviews/2026-11-20_0cd8996/`; C++ blocks S3-D1, D2, D4, D5, D6, D7, D9, D10, S3-I1, I2, I3, and the step-5 rule violation in `ftsdevice.cpp`). It follows the user's decisions of 2026-11-20, recorded in that folder's `decisions.md`. Everything needed is repeated below.


## Purpose / Big Picture

The device library `RoboCrapDevices` (`src/devices`) talks to three devices:
- the Delta PLC over TCP (`PlcDevice`);
- the force/torque sensor over UDP (`FtsDevice`);
- the KUKA RSI interface over UDP (`RsiDevice`).

Several things in it are larger or riskier than needed.
- **Unused PLC request kinds.** The PLC message code builds and decodes four request kinds that the application never sends: READ_IO, READ_REG, WRITE_REG and WRITE_RAW. They are reachable only through a debugging entry point, `writeMessage`, which the QML API exposes but nothing uses.
- **PlcMessageManager is a heap-allocated QObject.** It holds no state, and its QML registration is unused.
- **Method names hide QObject's.** The device methods `connect` and `disconnect` hide `QObject::connect`/`disconnect`. That is why every connection in these files must be written `QObject::connect(...)`, and why a call such as `device->disconnect()` silently closes a socket instead of disconnecting signals.
- **Endpoint parsing exists three times.** Each device repeats the same address/port parsing and validation, and keeps four members for it.
- **Smaller items:**
  - `FtsDevice` updates its last published values through an in-out reference parameter, which AGENTS.md forbids;
  - `FtsDevice` passes a parameter that is always 0 and has a one-line forwarding helper;
  - `PlcDevice` repeats clears;
  - `PlcDevice` closes the socket in two places without the RST linger that its own `disconnect` uses to avoid a "bound address already in use" failure;
  - the PLC log file name has second resolution, so a reconnect in the same second overwrites the previous log.

After this plan, the observable behaviour is unchanged: the same PLC panel, FTS panel and RSI streaming, with the same Settings Connect/Disconnect button. The code is smaller:
- the PLC code builds only WRITE_IO, SET_VAR and SNAPSHOT requests;
- `PlcMessageManager` is a value with gadget metadata;
- the device methods are `connectDevice`/`disconnectDevice`;
- the endpoint parsing exists once.

To see it working:
1. Build.
2. In Settings, connect and disconnect each device. The user does this on the equipment; RSI only binds a local socket.
3. On the PLC panel, set and clear one output (user, equipment).
4. Start and stop FTS streaming (user, equipment).

Without hardware: open Settings, choose each profile and press Connect. The console prints the usual bind/connect messages or errors, and the button text toggles as before.


## Progress

- [x] (2026-11-20) Plan written after reading:
  - `src/devices/include/devices/plc/plcmessagemanager.h`;
  - `src/devices/plc/plcmessagemanager.cpp` (helpers 1-58, `buildReq`, `outputRequest`, `robotProgramRequest`, `parseMessage`, `buildReqPayload`, `parseRespOk` start, the helper list);
  - `src/devices/include/devices/plc/plcdevice.h` and `src/devices/plc/plcdevice.cpp` (`startDevice`, `stopDevice`, `connect`, `disconnect`, `setOutput`, `writeMessage`, `runRobotProgram`, `sendNext`, `onReplyTimeout`, `onHeartbeat`, `onConnected`, `openLog`, `writeLog`, `matchResponse`, `onStateChanged`);
  - `src/devices/fts/ftsdevice.cpp` (`acceptAxis`, `connect`, `sendRequest`, `saveLogToDefaultFile`, `saveLogToFileImpl`) and the endpoint members in `ftsdevice.h`;
  - `src/devices/rsi/rsidevice.cpp` (`connect`, `stopDevice`, the peer-port uses) and `rsidevice.h`;
  - `src/devices/common/socketconfigutils.h` and `src/devices/abstractdevice.cpp` (`invoke`, `buildApi`);
  - `src/application/backendqmltypes.h` (`PlcMessageQml`), `src/application/CMakeLists.txt` (include directories) and `src/application/qml/Views/Network/Network.qml` (invoke strings).
- [x] (2026-11-20) Start counts (physical lines, before Milestone 1): `plcmessagemanager.cpp` 545, `plcmessagemanager.h` 130, `plcdevice.cpp` 525, `plcdevice.h` 80, `ftsdevice.cpp` 573, `ftsdevice.h` 91, `rsidevice.cpp` 445, `rsidevice.h` 108, `socketconfigutils.h` 14, `backendqmltypes.h` 184, `Network.qml` 215 (plan 03 added 3), `src/application/CMakeLists.txt` 133 (also 133 at 0cd8996; the earlier 131 was a miscount); 3043 in total. The device files are unchanged since 0cd8996, so their line references below hold.
- [x] (2026-11-20) Milestone 1 implemented:
  - `PlcMessageManager`: `Q_GADGET` without a QObject base or constructor; validators and `byteToBits` static; new static `ioState` decodes the seven IO-state bytes for the SNAPSHOT reply (`payload.mid(2, IoStateLength)`) and the IOs change frame (`payload.mid(1, IoStateLength)`); READ_IO, READ_REG, WRITE_REG and WRITE_RAW cases removed from `buildReqPayload` and `parseRespOk`; `readBytes`, `isValidCmd` and the trailing empty lines removed; payload lengths named (`WriteIoReplyLength` … `IoStateLength`); `buildReqPayload` returns `BAD_CMD` for any other command (see Decision Log);
  - `PlcDevice`: `m_mgr` held by value (`new`/`delete`/null lines, `Q_ASSERT(!m_mgr)` and the `!m_mgr` term removed); `writeMessage` private, without `Q_INVOKABLE`; `matchResponse` keeps the WRITE_IO (dev `Y`, module) and SET_VAR checks;
  - application: `PlcMessageQml` and its include removed from `backendqmltypes.h`; the `devices/plc` include directory removed from `src/application/CMakeLists.txt` (`devices/fts` stays for `RDTResponse`).
  Searches of Concrete Steps: none of the four removed commands in `plcdevice.cpp`; no `PlcMessage\b`, `PlcMessageQml` or `new PlcMessageManager` in `src`; the enum definitions remain.
  Compliance pass: no lambdas, templates, exceptions or output parameters; `ioState` is the one new helper (recorded requirement); no signature above two parameters. No violations found.
  Counts (physical / nonblank): plcmessagemanager.cpp 545/431 → 416/336, plcmessagemanager.h 130/109 → 131/111, plcdevice.cpp 525/433 → 511/420, plcdevice.h 80/66 → 81/67, backendqmltypes.h 184/165 → 175/157, src/application/CMakeLists.txt 133/128 → 132/127; file set 3043 → 2892.
- [x] (2026-11-20) Milestone 1 user check: builds, `robocrap_qmllint`, the disconnected Settings check and the PLC equipment check passed (user report); the `devices/plc` include directory was not needed.
- [x] (2026-11-20) Milestone 2 implemented:
  - `SocketEndpoints` in the public `abstractdevice.h` (see Decision Log); `parseSocketEndpoints` and `connectionConfigMap` inline in the private `socketconfigutils.h`;
  - FtsDevice, PlcDevice, RsiDevice: `std::optional<SocketEndpoints> m_endpoints` replaces `m_la`, `m_lp`, `m_pa` and the configured `m_pp`; RSI keeps its learned `m_pp` and drops a configured peer port; `connect`/`disconnect` renamed to `connectDevice`/`disconnectDevice` in the headers, the three `stopDevice`s and FTS's own calls; the "device is not started" guards of the three `connect` methods removed (planned for Milestone 3, done here because the bodies were rewritten);
  - FtsDevice: the order of Plan of Work (parse, completeness, one disconnect before the endpoints are replaced, store, publish, bind); `disconnectDevice` stops streaming when `isSocketReady()`; `isSocketReady` and `startStreaming` no longer re-test the peer (see Decision Log);
  - `Network.qml`: `"disconnectDevice"` and `"connectDevice"`.
  Search of Concrete Steps: nothing. No application code calls the device methods directly (only the two invoke strings).
  Compliance pass: no lambdas, templates, exceptions or output parameters; the two new functions take one parameter and return their result; `SocketEndpoints` groups one concept. No violations found.
  Counts (physical / nonblank, from the Milestone 1 state): socketconfigutils.h 14/11 → 39/33, abstractdevice.h 43/33 → 55/43 (outside the Concrete Steps set), ftsdevice.cpp 573/461 → 554/447, ftsdevice.h 91/72 → 88/69, plcdevice.cpp 511/420 → 496/409, plcdevice.h 81/67 → 77/63, rsidevice.cpp 445/414 → 438/407, rsidevice.h 108/92 → 105/89, Network.qml 215 → 215; Concrete Steps set 2892 → 2866 (with abstractdevice.h: −14 lines in total).
- [x] (2026-11-20) Milestone 2 user check: the user built, confirmed the FTS Panel and asked to proceed; no failure was reported.
- [x] (2026-11-20) Milestone 3 implemented:
  - FtsDevice: `std::optional<qint32> changedAxis(qint32 last, qint32 current)` replaces the in-out `acceptAxis`; each of the six axes stores and publishes the returned value; `sendRequest(quint16 cmd)` writes `kContinuousSamples = 0`; `saveLogToFileImpl` inlined into `saveLogToDefaultFile`;
  - PlcDevice: the two clears after `disconnectDevice()` in `stopDevice` removed; `connectDevice` and the partial write in `sendNext` close with `disconnectDevice()` (RST); the abort after a failed bind stays (no connection); log name `plc_io_yyyyMMdd_HHmmss_zzz.csv`;
  - RsiDevice: `CaptureIdleMs = 250` named;
  - traced `!m_sock` checks (Decision Log): removed from the three `disconnectDevice`s, `PlcDevice::setOutput`, `PlcDevice::writeMessage` and `FtsDevice::startStreaming`; the three `stopDevice` guards kept.
  Compliance pass: no in-out reference parameter remains in the devices; no lambdas, templates or exceptions; `changedAxis` has two parameters and returns its result. No violations found.
  Counts (physical, Concrete Steps set): plcdevice.cpp 496 → 493, ftsdevice.cpp 554 → 535, ftsdevice.h 88 → 87, rsidevice.cpp 438 → 439; set 2866 → 2844 (3043 at the start, −199; abstractdevice.h 43 → 55 outside the set).
- [x] (2026-11-20) Milestone 3 user check: builds, the disconnected Settings check and the equipment checks passed (user report).
- [x] (2026-11-20) Builds of `RoboCrapDevices` and `robocrap`, `robocrap_qmllint`, disconnected Settings check and connected checks with each device (user's), after every milestone.
- [x] (2026-11-20) Contract and ARCHITECTURE updates, final compliance pass, line counts, `git diff --check` (clean):
  - `AGENT_CONTRACTS.md` "Build and modules": PlcMessageManager is a Q_GADGET value held by PlcDevice, without QML registration;
  - "Devices and protocols": socket devices expose `connectDevice`/`disconnectDevice` and parse with `parseSocketEndpoints` into one `SocketEndpoints` (struct in `abstractdevice.h`); the PC builds and decodes only WRITE_IO, SET_VAR and SNAPSHOT, `writeMessage` is private. The contract named no `disconnect` method otherwise ("a PLC disconnect" at the calibration run is the event);
  - `ARCHITECTURE.md` section 3, `PlcDevice`: `disconnectDevice`.
  Code acceptance of Validation and Acceptance verified by search: `writeMessage` not public, no QObject base, no in-out reference parameter in `src/devices`, three users of `parseSocketEndpoints`.


## Surprises & Discoveries

- Observation: three references of this plan were stale on 2026-11-20. `abstractdevice.cpp` has 133 lines: `invoke` is at 46-70 (its not-started guard at 50-53, as cited) and `buildApi` at 72-, not 187-211 and 213-238. The invoke strings of `Network.qml` are at 163 and 165 since plan 03. `src/application/CMakeLists.txt` had 133 lines at 0cd8996.
  Evidence: `grep -n buildApi src/devices/abstractdevice.cpp`; `grep -n 'invoke("' src/application/qml/Views/Network/Network.qml`.
- Observation: `PlcDevice::connect` aborts the socket also after a failed bind (line 143). No connection exists there, so the RST close (`SO_LINGER`) has nothing to reset; Milestone 3 converts only line 132 and the partial write in `sendNext`, as planned.


## Decision Log

- Decision (user, 2026-11-20): `writeMessage` becomes private, and the PC stops building and decoding READ_IO, READ_REG, WRITE_REG and WRITE_RAW.
  Rationale: no code sends them; `grep -rn writeMessage src` finds only internal callers. `AbstractDevice::buildApi` exposes public methods only, so a private method leaves the invoke API. The `CMD`, `MessageError`, `Type`, `VAR_TYPE` and `DEV` enum values stay unchanged: they mirror the PLC program's data types and error codes, which the PLC still uses.
  Date/Author: 2026-11-20, user.
- Decision (user, 2026-11-20): `PlcMessageManager` becomes a `Q_GADGET` class without a QObject base, held by value as `PlcDevice::m_mgr`. The unused QML registration `PlcMessageQml` is deleted.
  Rationale: the class has no state, signals or slots, and only its `Q_ENUM`s need metadata, which `Q_GADGET` provides. Holding it by value removes the `new`/`delete`, the nulling and the `!m_mgr` checks.
  Date/Author: 2026-11-20, user.
- Decision (user, 2026-11-20): rename `connect`/`disconnect` to `connectDevice`/`disconnectDevice` in FtsDevice, PlcDevice and RsiDevice.
  Rationale: the new names do not hide QObject's and match the existing `startDevice`/`stopDevice`. Network.qml's two invoke strings change with them.
  Date/Author: 2026-11-20, user and plan author.
- Decision: one aggregate `SocketEndpoints` (local address, local port, peer address, optional peer port) with two free functions in `src/devices/common/socketconfigutils.h`. All three devices use it, RsiDevice included.
  Rationale: the three `connect` bodies repeat the same parsing, validation, error texts and `connectionConfig` map. The value groups one concept (a socket's configured endpoints), not unrelated parameters. RSI's learned peer port stays its own member `m_pp`, so the receive path is untouched.
  Date/Author: 2026-11-20, plan author.
- Decision: the redundant "device not started" checks (`if (!m_sock)` at the top of the three `connect` methods) are removed.
  Rationale: every call reaches these methods through `AbstractDevice::invoke`, which rejects calls while the device is not started (`abstractdevice.cpp` 50-53). `m_sock` is non-null exactly while started. AGENTS.md: validate once at a proven entry point.
  Date/Author: 2026-11-20, plan author.
- Decision: Milestone 3 also traces every path to the other `!m_sock` checks (`PlcDevice::setOutput`, `writeMessage`, the three `disconnect` methods and their `stopDevice` callers) and removes each one only where every path is proven to run while the device is started.
  Rationale: the same proof may cover them, but they also have internal callers (`stopDevice`, timers), so each needs its own check.
  Date/Author: 2026-11-20, implementing agent.
- Decision (result of that trace): removed from the three `disconnectDevice`s (callers: their own `stopDevice` after its guard, their own `connectDevice`, `invoke`, and `PlcDevice::sendNext`, all while started), from `PlcDevice::setOutput` and `FtsDevice::startStreaming` (invoke only), and from `PlcDevice::writeMessage` (four invoke-only methods and the heartbeat, a timer created in `startDevice` and deleted in `stopDevice`). The `stopDevice` guards stay: `AbstractDevice::stop` calls `stopDevice` only while started, but the contract asks to keep repeat-call safety in shutdown code, so it is not left to the base class alone.
  Date/Author: 2026-11-20, implementing agent.
- Decision: the `SocketEndpoints` struct is defined in the public `src/devices/include/devices/abstractdevice.h`; its two functions stay in the private `src/devices/common/socketconfigutils.h`, which includes it.
  Rationale: the planned placement in `socketconfigutils.h` cannot compile. The device headers, which now hold `std::optional<SocketEndpoints>` members, are public and included by `main.cpp`, while `src/devices` is a PRIVATE include directory of `RoboCrapDevices` (`src/devices/CMakeLists.txt`). Every device header already includes `abstractdevice.h`, so the struct needs no new file or CMake entry; a separate public header would add both.
  Date/Author: 2026-11-20, implementing agent.
- Decision: FtsDevice's `isSocketReady` drops its peer test, `disconnectDevice` stops streaming when `isSocketReady()`, and `startStreaming` loses its "FTS peer is not configured" branch.
  Rationale: the socket becomes bound only in `connectDevice`, after `m_endpoints` (always with a peer port for FTS) is stored, and `m_endpoints` is never cleared; so every bound-state test already implies a configured peer, and the removed branch followed such a test.
  Date/Author: 2026-11-20, implementing agent.
- Decision: RsiDevice resets a configured `peerPort` after parsing.
  Rationale: RSI learns its peer port from the controller and never used a configured one; the reset keeps `connectionConfig` without a peer port, as before. A present but invalid `peerPort` key is now rejected as an invalid configuration; Network.qml never sends one for RSI.
  Date/Author: 2026-11-20, implementing agent.
- Decision: `buildReqPayload`'s `default:` returns `BAD_CMD` instead of `break`, and `isValidCmd` is removed.
  Rationale: with the four unused cases deleted, `default: break` would have built a header-only frame for any other command value and sent it. `isValidCmd` accepted every `CMD` value, so after this change its two tests duplicated the switch defaults: in `buildReqPayload` and in `parseRespOk`, where `default` returns the same `BAD_CMD` with the same note (the command byte). Every input gives the same result as with the planned "keep `isValidCmd`".
  Date/Author: 2026-11-20, implementing agent.
- Decision: the eight empty lines at the end of `plcmessagemanager.cpp` (538-545) are deleted in Milestone 1.
  Rationale: whitespace only, in a file this milestone rewrites.
  Date/Author: 2026-11-20, implementing agent.


## Outcomes & Retrospective

Completed and accepted by the user on 2026-11-20, with unchanged behaviour on the PLC, FTS and RSI equipment. `PlcMessageManager` is a stateless Q_GADGET value; the PC builds and decodes only WRITE_IO, SET_VAR and SNAPSHOT, decodes the IO state once, names its payload lengths, and rejects any other request command instead of sending a header-only frame; `writeMessage` and the unused `PlcMessage` QML type left the public API. The three devices use `connectDevice`/`disconnectDevice` and one `SocketEndpoints` value with one parser. FTS lost its in-out reference, always-zero argument and forwarder; PLC closes with RST on every connected close and logs with millisecond names. The plan's file set shrank from 3043 to 2844 lines (−199), with 12 lines added to `abstractdevice.h` for the shared struct.

Deviations, all in the Decision Log: the struct moved to the public `abstractdevice.h` because the planned private header cannot be reached by the public device headers; `isValidCmd` was removed together with the unused commands; traced `!m_sock` checks beyond the three connect methods were removed, while the `stopDevice` guards were kept for shutdown repeat-call safety.

Lesson: a plan that adds members of a new type to public headers must check where that type is visible (public versus private include directories) before naming its header.


## Context and Orientation

**How QML reaches a device.** QML asks the hub for a runner with `Hub.device("plc")` (or "fts", "rsi") and calls `runner.invoke("methodName", args)`. `DeviceRunner::invoke` emits a queued signal. On the device's I/O thread, `AbstractDevice::invoke` (`src/devices/abstractdevice.cpp` 46-70) looks the name up in `m_api` and calls the method. `m_api` is built by `buildApi` (72-) from every public method returning void with zero arguments or one `QVariantMap`, so the invokable API is exactly those public methods.

The current invoke string users are:
- `Network.qml` 163 (`"disconnect"`) and 165 (`"connect"`);
- `RobotStartPanel.qml` (`runRobotProgram`, `resumeRobot`, `abortRobotStart`);
- `DeltaModuleAP.qml` (`setOutput`);
- `FtsPanel.qml` (`startStreaming`, `stopStreaming`, `bias`, `startLogRecording`, `saveLogToDefaultFile`);
- `scenemachiningpreparation.cpp:486` (`runRobotProgram`).

**PLC message code.** `PlcMessageManager` (`src/devices/include/devices/plc/plcmessagemanager.h`, `src/devices/plc/plcmessagemanager.cpp`) is `class PlcMessageManager : public QObject` with `Q_OBJECT` and these parts:
- the enums `MessageError`, `Type`, `CMD`, `VAR_TYPE`, `CHG_TYPE` and `DEV`, each with `Q_ENUM`;
- the structs `Header` and `ParseResult`;
- the const methods `buildReq`, `outputRequest`, `robotProgramRequest` and `parseMessage`, plus private parsers and validators;
- no data members.

`PlcDevice::startDevice` (plcdevice.cpp 54-76) creates it with `new PlcMessageManager(this)`, and `stopDevice` (78-99) deletes it.

`buildReqPayload` (145-237) has cases for READ_IO (159-170), READ_REG (171-178), WRITE_IO (179-198), WRITE_REG (199-212), WRITE_RAW (213-220), SNAPSHOT and SET_VAR. `parseRespOk` (274-420) decodes READ_IO (297-318), READ_REG/WRITE_REG (319-340), WRITE_IO (341-362, the same 20 lines as READ_IO), WRITE_RAW (364-368), SNAPSHOT (369-394) and SET_VAR. The 7-byte IO state (x1, y1, x2, y2 bits; robotStep, robotProgram, robotResult) is decoded twice: in the SNAPSHOT reply (369-394) and in `parseStateChange`'s IOs frame (463-489).

The application builds only:
- WRITE_IO, through `outputRequest`, from `setOutput`;
- SET_VAR, through `robotProgramRequest` (from `runRobotProgram`) and directly in `resumeRobot`/`abortRobotStart`;
- SNAPSHOT, from the heartbeat (`onHeartbeat`).

`PlcDevice::matchResponse` (plcdevice.cpp 318-345) checks reply fields per command, with branches for READ_IO/WRITE_IO and READ_REG/WRITE_REG. `writeMessage` (186-196), public and `Q_INVOKABLE`, queues a map; internally it is called by `setOutput`, `runRobotProgram`, `resumeRobot`, `abortRobotStart` and `onHeartbeat`.

The application registers the class for QML in `src/application/backendqmltypes.h` 159-165 (`struct PlcMessageQml { Q_GADGET QML_FOREIGN(PlcMessageManager) QML_NAMED_ELEMENT(PlcMessage) QML_UNCREATABLE(...) }`). No QML file uses `PlcMessage`. `src/application/CMakeLists.txt` adds the include directories `src/devices/include/devices/fts` and `src/devices/include/devices/plc` with the comment "Original header names used by the foreign-type registrar": the generated registration code includes foreign headers by bare file name.

**Endpoints.**
- `FtsDevice::connect` (ftsdevice.cpp 126-163) and `PlcDevice::connect` (plcdevice.cpp 101-148) read localAddress, peerAddress, localPort and peerPort from the map. They validate with `parseSocketPort` (`socketconfigutils.h`), print "Invalid socket configuration" or "Socket configuration is incomplete", store `m_la`, `m_lp`, `m_pa` and `m_pp`, and emit `stateReady({{"connectionConfig", …}})`.
- `RsiDevice::connect` (rsidevice.cpp 191-226) does the same without a configured peer port. Its `m_pp` is learned from the first accepted controller packet (lines 232, 256, 264-265, 286, 300).
- FtsDevice disconnects before overwriting the endpoints when its socket is not unconnected (146), so the Stop command reaches the old peer, then checks the same state again (158).

**Small items.**
- **`acceptAxis`.** `ftsdevice.cpp` 43-52, `bool acceptAxis(qint32& dst, qint32 src)`, writes `dst` (an in-out reference); its callers are at 370-385.
- **`sendRequest`.** `sendRequest(quint16 cmd, quint32 count = 0)` (391-410) is always called with `count` 0 (220, 236, 254). In the ATI RDT protocol, sample count 0 means continuous output.
- **`saveLogToFileImpl`.** It (484-489, header 59) has one caller, `saveLogToDefaultFile` (481).
- **PLC clears.** `PlcDevice::stopDevice` clears `m_rx` and `m_pend` (97-98) right after `disconnect()`, which cleared them as its first statements.
- **PLC close without linger.** `PlcDevice::connect` (131-133) and `sendNext` after a partial write (246) call `m_sock->abort()` directly. `disconnect()` (150-168) first sets `SO_LINGER {1, 0}`, so the close sends RST, because the PLC connection uses a fixed source port and a normal close would leave it in TIME_WAIT.
- **PLC log name.** `openLog` (284-298) names the log `plc_io_yyyyMMdd_HHmmss.csv` and opens it `WriteOnly`, which truncates.
- **Unnamed values.** The values `250` (`rsidevice.cpp` 165, capture idle delay) and the PLC payload sizes (`plcmessagemanager.cpp` 298, 321, 342, 371, 397, 424, 466) have no names.


## Plan of Work

### Milestone 1: PlcMessageManager

**`plcmessagemanager.h`.**
- Replace `class PlcMessageManager : public QObject { Q_OBJECT` with `class PlcMessageManager { Q_GADGET`, and delete the constructor declaration.
- Keep `#include <QObject>`. It provides the `Q_GADGET` and `Q_ENUM` macros, and keeping it avoids depending on Qt's internal header layout. The class itself no longer inherits QObject.
- Make `isValidType`, `isValidCmd`, `isValidDev`, `isValidMod`, `isValidVar` and `byteToBits` static.
- Add the static private `QVariantMap ioState(const QByteArray& bytes)`. Requirement: the SNAPSHOT reply and the IOs change frame decode the same seven bytes; one helper removes the second copy, and no existing function decodes them.

**`plcmessagemanager.cpp`.**
- Delete the constructor.
- In `buildReqPayload`, delete the READ_IO, READ_REG, WRITE_REG and WRITE_RAW cases.
- In `parseRespOk`, delete the READ_IO, READ_REG/WRITE_REG and WRITE_RAW cases, keep WRITE_IO, and move the IO-state decoding of SNAPSHOT into `ioState`. Do the same in `parseStateChange`. Each caller keeps its own payload-size check and then inserts `ioState(payload.mid(<prefix length>))` into its map.
- Delete `readBytes`, whose only caller was the WRITE_RAW case.
- Keep `readUInt16`, which `readUInt8` calls, and `isValidDev`, which the remaining WRITE_IO reply check calls (old line 353).
- Name the remaining payload sizes in the anonymous namespace and use them in place of the literals:

      constexpr int WriteIoReplyLength = 6;   // parseRespOk, WRITE_IO (old line 342)
      constexpr int SnapshotReplyLength = 9;  // parseRespOk, SNAPSHOT (old line 371)
      constexpr int SetVarReplyLength = 4;    // parseRespOk, SET_VAR (old line 397)
      constexpr int ErrorReplyLength = 4;     // parseRespErr (old line 424)
      constexpr int IoChangeLength = 8;       // parseStateChange, IOs (old line 466)
      constexpr int IoStateLength = 7;        // bytes passed to ioState

  The SNAPSHOT reply has a 2-byte prefix (cmd, status) before the state, and the IOs change frame a 1-byte prefix (chg type). So `ioState` receives `payload.mid(2, IoStateLength)` and `payload.mid(1, IoStateLength)`. Both offsets were verified at commit 0cd8996: SNAPSHOT is 2 + 7 = 9 bytes and the IOs frame 1 + 7 = 8.
- `ioState` reads the seven bytes with a big-endian `QDataStream`, in the order x1, y1, x2, y2, step, program, result. It returns `{x1, y1, x2, y2}` as `byteToBits` lists plus `robotStep`, `robotProgram` and `robotResult`. Each caller merges the returned entries into its own `out` map with `out.insert(ioState(...))`. `QMap::insert(const QMap&)`, available since Qt 5.15, overwrites equal keys, and there are none.

`isValidCmd` keeps accepting every `CMD` value. A reply with an unexpected command then still reaches `parseRespOk`'s default branch with the existing error, so the reply handling stays defined.

**`plcdevice.{h,cpp}`.**
- Declare `PlcMessageManager m_mgr;` (by value) and use `m_mgr.` instead of `m_mgr->`.
- Delete the `new`/`delete`/null lines in `startDevice`/`stopDevice` and the `!m_mgr` term in `setOutput`.
- Move `writeMessage` to the private section without `Q_INVOKABLE`.
- In `matchResponse`, delete the READ_IO, READ_REG and WRITE_REG labels and the `cmd == WRITE_IO ? … : request.value("dev")` choice, leaving the WRITE_IO and SET_VAR checks.

**Application side.**
- In `src/application/backendqmltypes.h`, delete `struct PlcMessageQml`. Keep the `#include "devices/plc/plcmessagemanager.h"` only if something else in the file needs it; nothing does, so delete it too.
- In `src/application/CMakeLists.txt`, delete the include directory line `${PROJECT_SOURCE_DIR}/src/devices/include/devices/plc`. If the user's build then fails in generated QML registration code, restore the line and record it in Surprises.

### Milestone 2: names and endpoints

In `src/devices/common/socketconfigutils.h`, add:

    struct SocketEndpoints
    {
      QHostAddress localAddress;
      quint16 localPort = 0;
      QHostAddress peerAddress;
      std::optional<quint16> peerPort; // Absent when the peer port is learned (RSI).
    };
    // localAddress, localPort and peerAddress must be valid; peerPort is parsed when present.
    std::optional<SocketEndpoints> parseSocketEndpoints(const QVariantMap& config);
    // The connectionConfig value published to QML; peerPort only when configured.
    QVariantMap connectionConfigMap(const SocketEndpoints& endpoints);

Define both inline next to `parseSocketPort` (the header is private to the library and already holds an inline function), and add `#include <QHostAddress>`. `parseSocketEndpoints` returns `std::nullopt` in two cases:
- an address is null or the local port is invalid;
- the map contains a `peerPort` key that `parseSocketPort` rejects.

In each device, replace the four members with `std::optional<SocketEndpoints> m_endpoints;`. RsiDevice keeps `m_pp`, its learned peer port. Rename `connect`/`disconnect` to `connectDevice`/`disconnectDevice` in the headers and in every internal call (ftsdevice.cpp 115, 146, 158; plcdevice.cpp 82; rsidevice.cpp 184, 213).

The body of each `connectDevice`:
1. When `config` is not empty, parse it; on failure print "Invalid socket configuration" and return.
2. FtsDevice and PlcDevice also require `peerPort`; when it is missing, print the same text and return.
3. FtsDevice keeps its disconnect-before-overwrite.
4. Store `m_endpoints`.
5. When `m_endpoints` is empty, print "Socket configuration is incomplete" and return.
6. Emit `stateReady({{"connectionConfig", connectionConfigMap(*m_endpoints)}})`, then bind and connect as today, using the fields of `*m_endpoints`.

FtsDevice today has two unconnected-state checks: one before overwriting the endpoints (146) and one after the completeness check (158). Replace them with one check, placed so it covers both the new-config and the empty-config path, and so it still runs before the old endpoints are overwritten. The order in `FtsDevice::connectDevice` is:
1. If `config` is not empty, parse it into a local optional. On failure, or when `peerPort` is missing, print "Invalid socket configuration" and return.
2. If there is neither a parsed value nor `m_endpoints`, print "Socket configuration is incomplete" and return.
3. `if (m_sock->state() != QAbstractSocket::UnconnectedState) disconnectDevice();` Stop goes to the old peer, because `m_endpoints` is not yet replaced.
4. If a value was parsed, store it in `m_endpoints`.
5. Emit `connectionConfig` and bind.

Without stored endpoints the socket cannot be connected, so step 2 returning before step 3 changes nothing. PlcDevice and RsiDevice keep their single check where it is today, after the completeness check.

In `Network.qml`, change the invoke strings to `"disconnectDevice"` and `"connectDevice"`.

### Milestone 3: small items

**FtsDevice.**
- Replace `bool acceptAxis(qint32& dst, qint32 src)` with `std::optional<qint32> changedAxis(qint32 last, qint32 current)`. It returns `current` when the difference is at least the tolerance, otherwise `std::nullopt`. Callers write `if (const auto fx = changedAxis(m_lastPub.Fx, sample.Fx)) { m_lastPub.Fx = *fx; vals.insert("fx", *fx / kCount); }`, and likewise for the other five axes.
- Drop the `count` parameter of `sendRequest` and write `constexpr quint32 kContinuousSamples = 0;` (RDT: 0 = continuous) into the request.
- Inline `saveLogToFileImpl` into `saveLogToDefaultFile` and delete its declaration.

**PlcDevice.**
- Delete lines 97-98 of `stopDevice`.
- In `connectDevice` (old 131-133) and in `sendNext` after a partial write (246), call `disconnectDevice()` instead of `m_sock->abort()`. In `sendNext`, `disconnectDevice` clears `m_rx` and `m_pend` and aborts; `onStateChanged(UnconnectedState)` then clears the queue, so the loop ends as before.
- In `openLog`, use `yyyyMMdd_HHmmss_zzz` in the file name.

**RsiDevice.** Name the capture idle delay in the anonymous namespace (`constexpr int CaptureIdleMs = 250;`) and use it at line 165.


## Concrete Steps

Work in the repository root. Counts and checks before and after each milestone:

    wc -l src/devices/plc/*.cpp src/devices/include/devices/plc/*.h src/devices/fts/ftsdevice.cpp src/devices/include/devices/fts/ftsdevice.h src/devices/rsi/rsidevice.cpp src/devices/include/devices/rsi/rsidevice.h src/devices/common/socketconfigutils.h src/application/backendqmltypes.h src/application/qml/Views/Network/Network.qml src/application/CMakeLists.txt
    git diff --check

After Milestone 1, both searches must find nothing:

    grep -rn "READ_REG\|WRITE_REG\|WRITE_RAW\|READ_IO" src/devices/plc/plcdevice.cpp
    grep -rn "PlcMessage\b\|PlcMessageQml\|new PlcMessageManager" src

The enum definitions in `plcmessagemanager.h` remain.

After Milestone 2, this must find only comments, if any:

    grep -rn "\"connect\"\|\"disconnect\"\|::connect(const QVariantMap\|::disconnect()" src

The user builds; the agent never does. Targets: `RoboCrapDevices`, `robocrap`, then `robocrap_qmllint`, in `build/Felgo_SDK_Desktop_Qt_6_8_3_MinGW_64_bit-Debug`, after checking `CMAKE_HOME_DIRECTORY` in its `CMakeCache.txt`.


## Validation and Acceptance

**Disconnected (user).** Start the application. In Settings, select each of the PLC, RSI and FTS profiles and press Connect with the default addresses.
- **PLC and FTS.** The console prints the bind or connect attempt. Without the device, the PLC prints a connection error and the FTS binds its UDP socket.
- **RSI.** It binds 172.31.1.47:5555 only if that adapter exists; otherwise "Bind failed: …".
- **Button.** For a device that connected or bound, the button turns into Disconnect, and pressing it disconnects.
- **Network panel.** The current configuration is shown after Connect, because `connectionConfig` is published as before.

**Equipment checks (user).**
- **PLC.** It connects; the heartbeat keeps it connected for over 10 s; setting and clearing a module-2 output on the PLC panel works; the PLC log `records/plc_io_<date>_<time>_<ms>.csv` appears.
- **FTS.** Streaming starts and stops, the force bars move, and Bias works.
- **RSI.** RSI Sampling installs its streams as before. Exercising streaming needs a robot run, which is the user's.

**Code acceptance.**
- `writeMessage` is not in the public section of `plcdevice.h`.
- `PlcMessageManager` has no QObject base.
- No device method takes an in-out reference.
- The three devices share `parseSocketEndpoints`.


## Idempotence and Recovery

The edits are source edits, and repeating a completed step changes nothing. If removing the plc include directory breaks the build, restore that one line. Each milestone can be reverted with git, at the user's request, without affecting the other milestones.


## Artifacts and Notes

The current duplicated decoding, for orientation:

    case CMD::READ_IO:  { size 6; dev/module/state; isValidDev; isValidMod; out[...] }   // plcmessagemanager.cpp 297-318
    case CMD::WRITE_IO: { size 6; dev/module/state; isValidDev; isValidMod; out[...] }   // 341-362, identical body


## Interfaces and Dependencies

No library or minimum-version change. At the end:

    // src/devices/include/devices/plc/plcmessagemanager.h
    class PlcMessageManager { Q_GADGET public: ... same enums, Header, ParseResult, buildReq, outputRequest,
        robotProgramRequest, parseMessage ...; private: static QVariantMap ioState(const QByteArray& bytes); ... };

    // FtsDevice, PlcDevice, RsiDevice
    Q_INVOKABLE void connectDevice(const QVariantMap& config);
    Q_INVOKABLE void disconnectDevice();

    // src/devices/common/socketconfigutils.h
    struct SocketEndpoints { QHostAddress localAddress; quint16 localPort; QHostAddress peerAddress; std::optional<quint16> peerPort; };
    std::optional<SocketEndpoints> parseSocketEndpoints(const QVariantMap& config);
    QVariantMap connectionConfigMap(const SocketEndpoints& endpoints);

**Documents.**
- **`docs/code context/AGENT_CONTRACTS.md`, "Build and modules":** replace "PlcMessageManager remains a QObject with dictionary APIs and a published header for the PlcDevice/PlcMessage enums" with "PlcMessageManager is a Q_GADGET value held by PlcDevice, with dictionary APIs and a published header for the protocol enums; it has no QML registration".
- **Same file, "Devices and protocols", PLC bullet:** add "the PC builds only WRITE_IO, SET_VAR and SNAPSHOT requests". In the RSI/PLC text, rename `disconnect` to `disconnectDevice` where it names the method.
- **`docs/code context/ARCHITECTURE.md`, section 3, `PlcDevice` bullet:** rename `disconnect` to `disconnectDevice`.

`docs/plc_project_snapshot/ISPSoft project.md` is unchanged, because the PLC program does not change.
