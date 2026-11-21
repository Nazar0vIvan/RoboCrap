# RoboCrap architecture

Hardware, design notes the code does not show, and open questions for new sessions. Paths are repository-relative; named functions and
files are authoritative. Not repeated here: binding constraints ([AGENT_CONTRACTS.md](AGENT_CONTRACTS.md)),
controller/PC protocols ([RSI_Chamfer_Hole_CONTRACT.md](../../controller/krl/rsi/chamfer/RSI_Chamfer_Hole_CONTRACT.md),
[RSI_Probe_Calibration_CONTRACT.md](../../controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md),
[RSI_Probe_Check_CONTRACT.md](../../controller/krl/rsi/probe_check/RSI_Probe_Check_CONTRACT.md)) and operator
procedures ([HARDWARE_SETUP.md](../hardware%20context/HARDWARE_SETUP.md)). Unverified items are in §7; this document
verifies neither compilation nor runtime behavior.

## 1. Hardware

Supplied by the user unless a source is named.

| Device | Model | Code entry point | Transport |
|---|---|---|---|
| Robot | KUKA KR10 R1420, KR C4 smallsize-2, KSS 8.3.31, RSI 3.3 | `RsiDevice`, key `rsi` | XML over UDP, local 172.31.1.47:5555 |
| PLC | Delta AS332T with AS16AP11P-A and AS16AP11T-A | `PlcDevice`, key `plc` | Custom binary over TCP to 192.168.1.2:5051, fixed source port 1234 |
| Force/torque sensor | Schunk Delta IP68 SI-660-60 (ATI Net F/T electronics) | `FtsDevice`, key `fts` | ATI RDT over UDP, peer 192.168.1.3:49152 |
| Spindle | GDZ-65-800A on an H100-1.5S2-1B frequency inverter (VFD) | none | Started through PLC output Y2.0 |
| Touch probe | WP-500 V6 | none | Workpiece probe data enters as JSON point files; calibration touches as the application's touch file with the actual joint angles at detection ([RSI_Probe_Calibration_CONTRACT.md](../../controller/krl/rsi/calibration/RSI_Probe_Calibration_CONTRACT.md)) |
| Calibration sphere | ⌀25 mm bearing ball, grade G40, ISO 3290 | none | Passive, on a vertical pole on a table in front of the robot; sits 2 mm deep in the top of a ⌀17.8 mm strut, so only the surface above 10.5 mm below its centre is reachable. Strut and table geometry (table top 170.5 mm below the centre, tape-measured 2026-10-29) are in `resources/deploy/config/calibration_sphere.json`; the stand CAD `resources/deploy/cad/fixtures/CALIBRATION_STAND.stp` has its origin at the sphere centre |
| EtherCAT I/O | Beckhoff EK1100 coupler, EL1809 (16 DI), EL2809 (16 DO) on the KR C4 | none | Automatic External interface and probe signal to/from the PLC (wiring below); no PC code addresses them |
| Network switch | D-Link DGS-1005P (5 Gigabit ports, 4 PoE, unmanaged) | none | Connects the PC to the PLC, sensor and VFD |

### Network

Since 2026-10-23 the PC uses two adapters with separate subnets:

| PC adapter (Windows name) | PC address | Peer |
|---|---|---|
| ASIX AX88179 USB ("KRC4-Direct"), cabled directly to the KR C4 KLI | 172.31.1.47/24 | KR C4 RSI interface 172.31.1.147 (`db.json` RSI endpoint) |
| same adapter | 192.168.2.100/24 | KR C4 Windows interface 192.168.2.200, through which KRL mounts the `RoboCrapJobs` share |
| Intel I219-V ("Devices-Switch"), through the DGS-1005P | 192.168.1.100/24 | PLC 192.168.1.2, sensor 192.168.1.3, VFD 192.168.1.4 |

RSI bypasses the switch: through the DGS-1005P (store-and-forward, 802.3az EEE, 802.3x flow
control, 802.1p QoS per D-Link's B1 datasheet) KR C4 requests sometimes arrived up to 19 ms late and
several at once, and the controller rejected the late replies; over a direct cable none was late
(measured 2026-10-22 with `python/rsi/rsi_packet_timing.py` and Wireshark; the cause inside the switch
was not examined). RSI also requires a Windows firewall rule and a Private profile on
"KRC4-Direct" ([HARDWARE_SETUP.md](../hardware%20context/HARDWARE_SETUP.md)).

The KLI cable stays on the ASIX adapter. Moving it to the onboard Intel I219-V (driver 12.19.1.37)
was tried on 2026-11-20 and reverted: the ASIX adapter shares the PC's only USB host controller with
the Realtek USB Wi-Fi adapter, which was suspected of causing the occasional late requests of
2026-11-17/18. On the I219-V the late requests became 20–60 times more frequent: requests reached
the adapter driver 2–16 ms late (Npcap time stamps against IPOC), the controller's Delay counter
reached 10 and the runs stopped with KSS29002 `RSITimeout` after 2.5–9 min, also with Interrupt
Moderation enabled. Back on the ASIX adapter, the full calibration run had no request late at the
driver and a maximum Delay of 2, against 46 and 140 delayed requests in the ASIX runs of 2026-11-18.
USB sharing alone therefore does not produce the delays; those of 2026-11-17/18 remain unexplained.

Probable cause (not proven): the I219-V receive path, that is the adapter or its driver, holds
received frames before Windows sees them. Windows and RoboCrap added at most 1.7 ms after the
driver; LatencyMon during the runs showed no ISR or DPC longer than 1 ms; the held times are spread
evenly up to 16 ms (4 RSI cycles) and are not aligned to the Windows timer tick. The KR C4 and the
cable are unlikely, because they were the same in the ASIX run. Separating the adapter from the
cable or the KR C4 would need a capture outside the PC (network tap or mirroring switch port).

### Key device specifications

Datasheet values per component, not measurements of this installation, from the manuals folder
(`E:\Qt\QtProjects\manuals` on DESKTOP-31IFLFK, `D:\Qt\QtProjects\manuals` on the second PC;
checked 2026-10-24) unless marked as user-supplied.

| Device | Key data | Source |
|---|---|---|
| KR C4 smallsize-2 | KLI: Ethernet socket of the KUKA PC for RSI, the Windows interface and WorkVisual. X69 (KSI, cabinet door) is a separate service bus, not used by RSI. RSI cycle 4 ms (`#IPO_FAST`). | Spez KR C4 smallsize-2 §3.7, §6.6.6; RSI 3.3 §5.1, §7.1.4 |
| Delta AS332T-A | 24 VDC; Ethernet, 2× RS-485, USB, micro-SD; onboard 16 DI X0.0–X0.15 (response < 2.5 µs for X0.0–X0.11, < 50 µs for X0.12–X0.15) and 16 DO Y0.0–Y0.15 (sinking transistor, 5–30 VDC); up to 1024 I/O; 40K steps/ms; constant scan cycle 1–32000 ms in 1 ms steps; 128K steps program. | AS Hardware and Operation Manual §2 |
| AS16AP11P-A / AS16AP11T-A | 8 DI + 8 DO each. Inputs: 24 VDC, 5 mA, 4.7 kΩ, sinking or sourcing; ON > 15 V, OFF < 5 V; hardware response < 20 µs on, < 200 µs off, **plus a software filter of 0–25 ms per module, default 10 ms**. Outputs: transistor (P-A sourcing, T-A sinking), 5–30 VDC, 0.5 A per output, 4 A per COM, response ≤ 0.5 ms, ≤ 100 Hz resistive load. | AS Module Manual §2.1.1 |
| Beckhoff EK1100 | EtherCAT coupler, 100 Mbit/s, 24 VDC, E-bus supply 2000 mA, delay ≈ 1 µs. | Datasheet EK1100 |
| Beckhoff EL1809 | 16 DI, 24 VDC (−15 %/+20 %), EN 61131-2 type 1/3; "0" −3…+5 V, "1" 11…30 V; typ. 3 mA; **input filter typ. 3.0 ms**; reference ground 0 V power contact. | Datasheet EL1809 |
| Beckhoff EL2809 | 16 DO, 24 VDC, 0.5 A per channel, short-circuit-proof; **switching typ. 60 µs on, 300 µs off**. | Datasheet EL2809 |
| SI-660-60 (ATI Net F/T) | Ranges Fx, Fy ±660 N; Fz ±1980 N; Tx, Ty, Tz ±60 Nm; loads must stay within range on all six axes. RDT streaming over UDP port 49152. | FT Transducer manual §5.10 |
| WP-500 | Wired optical probe; body Ø42 mm, stylus thread M3, standard stylus 30 mm with Ø3 mm ruby ball; repeatability 0.001 mm; overtravel XY 20°, Z 3 mm; IP67; CR2032 (≈ 6 months standby); 3 m two-core cable with Type-C connector. User-supplied: normally open NPN output, conducting on trigger, which cannot drive EL1809 inputs and is therefore wired to PLC input X2.0 (working, verified 2026-10-24); an LED on the probe lights on contact, so jog touches need no signal. **The user states the probe can run on 24 V; the manual (ENG and CHN) does not mention a 24 V supply.** | WP-500 manual (values matched by row order from garbled PDF text) |
| H100-1.5S2-1B | X1–X6 are multi-function digital inputs; F044 sets the X1 function (factory 02 forward run, 01 RUN); jumper J1 selects NPN or PNP wiring. The F044 and J1 settings of this inverter are not recorded. | H100 manual §3.4, §6.3 |

### Additional hardware and Delta I/O wiring

Supplied by the user. Delta expansion modules:

| Module | Inputs (8 DI) | Outputs (8 DO) |
|---|---|---|
| AS16AP11P-A | X1.0–X1.7; sourcing; S/S to 0 V | Y1.0–Y1.7; sourcing; C0 to +24 V |
| AS16AP11T-A | X2.0–X2.7; sinking; S/S to +24 V, which suits the NPN WP-500 output pulling the input to 0 V | Y2.0–Y2.7; sinking; C0 to 0 V |

All connected PLC terminals (2026-11-13; every other terminal, including the onboard X0.x/Y0.x, is
unconnected):

| PLC terminal | Connected to |
|---|---|
| X1.0, X1.2 … X1.7 | EL2809 outputs 1, 3 … 8: `$I_O_ACTCONF`, `$PERI_RDY`, `$STOPMESS`, `$PRO_ACT`, PGNO_REQ, APPL_RUN, `$IN_HOME` |
| X1.1 | EL2809 output 2 (`$OUT[2]`, unassigned) |
| X2.0 | WP-500 NPN output |
| Y1.0 … Y1.4 | EL1809 inputs 1–5: `$EXT_START`, `$DRIVES_ON`, `$CONF_MESS`, PGNO bits 0 and 1 |
| Y1.7 | EL1809 input 8 (`$IN[8]`); the PLC program copies X2.0 to Y1.7 |
| Y2.0 | VFD terminal X1 |
| Y2.3, Y2.4, Y2.5, Y2.6, Y2.7 | Door panel lamps LED_RUN, LED_GREEN, LED_RED3, LED_RED2, LED_RED1 |

WorkVisual maps EL1809 input n to `$IN[n]` and EL2809 output n to `$OUT[n]` for n = 1–16. EL1809
inputs 1–5 and 8 and EL2809 outputs 1–8 are wired; inputs 1–5 and outputs 1 and 3–8 carry the
Automatic External signals listed in [HARDWARE_SETUP.md](../hardware%20context/HARDWARE_SETUP.md). The PLC programs
`RobotIo` and `RobotHandler` (text snapshot in `docs/plc_project_snapshot/ISPSoft project.md`) drive
Y1.0 … Y1.4 only while X1.0 (`$I_O_ACTCONF`) is ON; RoboCrap's panel shows them display-only and the
PLC refuses WRITE_IO for module 1.

Probe trigger path: WP-500 → X2.0 → PLC program → Y1.7 → EL1809 input 8 → `$IN[8]`, TRUE while the
probe is deflected (since 2026-11-06; from 2026-10-29 it was Y1.5 → input 11 → `$IN[11]`). Datasheet
delays in order: AS16AP11T-A input (< 20 µs, then the software filter, default 10 ms), the PLC scan,
Y1.7 (≤ 0.5 ms), the EL1809 filter (typ. 3.0 ms), EtherCAT. The AS filter dominates on the PLC side,
the EL1809 filter on the KR C4 side.

Automatic External is configured on `$IN[1..5]` and `$OUT[1..8]` (2026-11-13); its signal
assignments, `CELL.SRC` (override 100 %, PTP velocity by operating mode through `INIT_PTP_VEL`) and the
PC power settings that RSI needs are in [HARDWARE_SETUP.md](../hardware%20context/HARDWARE_SETUP.md).

Starting a KRL program from RoboCrap in Automatic External: design and PLC sequence in
`plans/PLC_AUT_EXT_PROGRAM_START_EXECPLAN.md`, PLC program `RobotHandler` in
`docs/plc_project_snapshot/ISPSoft project.md`. Running, finished and interrupted are derived on the
PC from X1; the PLC does not supervise a running program (`$MOVE_ENABLE` and `$DRIVES_OFF` stay on
`$IN[1025]`).

### Controller tool data

TOOL_DATA[5] is the WP-500 TCP (2026-10-24): ruby ball centre X −0.048, Y −0.097, Z 127 mm,
A = B = C = 0° relative to the flange. A CMM measured the probe clamped in its adapter, off the robot,
in the adapter frame {AD} defined by the adapter's flange-side locating surfaces; {AD} is assumed to
coincide with the flange frame {F}. Mounting clearance and the difference between the physical flange
and the controller's FLANGE are not included; the sphere calibration
(`docs/calibration/ROBOT_PROBE_SPHERE_CALIBRATION_METHOD_RU.md`) determines the actual value. With TOOL_DATA[5]
and base $NULLFRAME, HOME is X 1017.00, Y −0.1, Z 1080.05 mm, A 0°, B 90°, C 0°, S 110, T 111010
(smartPAD bits; decimal S 6, T 58): the stylus points along +X of WORLD.

## 2. Modules

Target ownership, links, exported includes, QML registration and resources: "Build and modules" in
AGENT_CONTRACTS.md. `old_imp/` is not built; `python/` holds analysis scripts.

## 3. Subsystems

- Geometry and kinematics (src/geometry, src/kinematics): plain value types with `std::optional`
  factories; no QObjects, signals or mutable statics.
- `Kr10Kinematics::calculateIK`: Status chooses shoulder/elbow/wrist, angles take the nearest
  permitted ±360° equivalents, Turn records angle signs, not revolutions. `Kr10Kinematics` stores
  `const Kr10KinematicModel&`; `RobotController` disables copy/move and destroys the solver before
  the model.
- `SceneSurfaceImporter` imports JSON planes and cylinders synchronously with probe compensation;
  the import blocks the GUI thread for the duration of the call; a private flag rejects re-entrant
  imports.
- `SceneMachining::generate`: the chamfer parameters use only the ellipse and the cylinder
  origin/axis, but the plane must still exist.
- `PlcDevice`: `disconnectDevice` sets `SO_LINGER` {1, 0} and aborts, so the close sends RST and the
  fixed source port has no TIME_WAIT that would make the next connect fail with "The bound address is
  already in use". Requests are queued and sent one at a time (the PLC has one receive buffer); a
  reply must arrive within 1 s, otherwise "PLC reply timeout" is logged and the next request is
  sent; requests while not connected are dropped. A SNAPSHOT heartbeat every 2 s while idle keeps
  the PLC socket below its 10 s keep-alive; no request is sent at connect (the PLC never answered
  it) because the PLC sends an IO change frame on every new connection. The framing is defined by
  the PLC program (snapshot `docs/plc_project_snapshot/ISPSoft project.md`).
- `RsiDevice` behavior: RSI_Chamfer_Hole_CONTRACT.md "PC behavior".
- OCCT viewport: rendering is coalesced by a 0 ms single-shot `QTimer` on the GUI thread, except
  robot installation (`OccViewport::setRobot`), drawn immediately so the robot shows before
  `ready`. No render thread, no `QOpenGLContext`.

## 4. Threads and ownership

| Thread | Objects / work | Created by |
|---|---|---|
| GUI | `QApplication`, `DeviceHub`, `DeviceRunner`s, scene classes, `OccController`/`OccViewWindow` and OCCT rendering, QML engine; JSON import, `ChamferPath::create`, `shared_data.txt` and `calibration_data.txt` writes, 16 ms Dry Run timer | `main()` stack |
| `ControlIO` | `FtsDevice`, `RsiDevice`, their sockets/timers | `DeviceHub` |
| `GeneralIO` | `PlcDevice`, its socket | `DeviceHub` |
| Generate (`std::async`) | `ChamferMotion::create` on a copied `ChamferPath` + `ChamferRobotSetup`; 30 ms GUI poll | `SceneMachining::generate` |
| Calibration (`std::async`) | `createProbeTouchPlan` + `ProbeTouchMotion::create` on copied inputs; 30 ms poll | `SceneCalibration::generate` |
| RSI sampling (`std::async`) | `sampleChamferRsiStream` on a copied `shared_ptr<const ChamferMotion>`, or `sampleProbeCalibrationRsi` on a copied `shared_ptr<const ProbeTouchMotion>` (one at a time); 30 ms poll; each writes its plan file | `SceneMachiningPreparation::prepare` / `prepareCalibration` |
| FTS save (`std::async`) | JSON write of a copied buffer | `FtsDevice` |
| CAD (`QThread` subclasses) | Robot job (seven links, two tools) and `DrawingModel` jobs; reads serialized by a static mutex | `OccController` |

- Application singletons are parentless `main()` stack objects; the QML engine is declared last and
  destroyed first.
- Devices are deleted on their own thread by `deleteLater` when it finishes; runners are hub children.
- A `SceneObject` is a `SceneModel` child; removal emits `objectRemoved`, then `deleteLater`.
- Shared immutable data: `ChamferMotion`, `ProbeTouchMotion`, their aliased `TimedMotion` and
  `CadLoadResult` as `shared_ptr<const ...>`; the RSI stream table as an implicitly shared
  `QVector<QVector<V6d>>`, read with const access on ControlIO.

## 5. Shutdown

1. `aboutToQuit`, in connection order: `ScenePlayback::end`;
   `SceneMachiningPreparation::shutdown` (blocks on a sampling future, emits an empty
   `correctionsReady`, deletes `shared_data.txt` and `calibration_data.txt`); `DeviceHub::stopAll`.
2. `stopAll` (returns at once if stopped; asserts it runs outside both I/O threads): a
   `BlockingQueuedConnection` stop per device, then `quit()`/`wait()` both threads. The empty
   `setCorrections` from step 1 is posted by the same GUI thread before the RSI stop, so it arrives
   first; this is not a general FIFO guarantee. `FtsDevice::stopDevice` waits for its save.
3. After `app.exec()`, reverse declaration order: QML engine (`RobotViewport` calls
   `detachViewWindow`); RobotController marks the robot unavailable and OccController waits for both
   CAD workers (a BREP read cannot be interrupted) and deletes the window;
   `SceneMachiningPreparation` (no-op `shutdown`); `SceneCalibration`, `SceneMachining` (future
   destructors block on running workers); `ScenePlayback`, `SceneEndEffectors`, `SceneModel`;
   `DeviceHub` (no-op `stopAll`); `QApplication`.

## 6. External-library boundaries

- OCCT: while the native surface is destroyed (exit, Felgo hot reload), availability is false
  and a Generate result is rejected (user-confirmed).

## 7. Open questions

Controller-side RSI items are listed in the RSI contracts.

- **Net F/T.** The manual confirms header 0x1234, commands 0x0000 stop / 0x0002 streaming / 0x0042
  bias, UDP 49152, internal 7000 Hz with RDT output 1–7000 Hz, and calibration-defined counts per
  unit. Unverified: the configured RDT rate (code assumes 7000 Hz) and counts (hard-coded 1e6).
- **PLC change frames.** Frames the PLC sends right after another frame are often lost (the
  DRIVES_ON pulse after a command reply is rarely logged); the 2 s SNAPSHOT heartbeat repairs the
  state. Likely cause, unverified: `PCHandler` takes a send as complete from socket flags that are
  refreshed only by socket instructions (AS manual).
- **VFD endpoint.** `db.json` names the 192.168.1.4 device "VFD ATV320U40N4C" (`driver: "vfd"`,
  Modbus TCP) with no device class, while the spindle inverter is the H100-1.5S2-1B.
- **Not exercised at runtime:** binding notification for every key of
  `QQmlPropertyMap::insert(QVariantHash)`; ControlIO latency with RSI and FTS sharing one event loop;
  shutdown blocking durations (Generate, sampling, BREP reads); Felgo hot reload with the one-engine
  `qFatal` checks; OccScene outline reuse, picking, replacement, partial OCCT failures and replay.
- **Calibration clearance.** Generate checks only the ball centre against sphere and strut; links are
  not checked against the fixture. The fixed probe orientations never point the stylus upward; link
  clearance to the table and stand is not computed, only seen in Dry Run (user reported it works,
  2026-11-11) and in the full robot sessions of 2026-11-14 and 2026-11-17 (158 touches each).
- **Data and units.** Imported files carry no validated unit; coordinates and probe radius must be mm.
  `FLANGE_ER` is hard-coded from CMM data (ER y-axis and origin; roll from projecting the flange
  z-axis), only a reference for entering the spindle TCP (user, 2026-10-24). Link BREPs have no STEP
  sources; OCCT compatibility is checked only by `BRepTools::Read`. End-effector STEP files must be in
  the flange frame and the stand in {S}; models are only translated. `IkSolution::status` selects the
  next IK branch; the bit mapping's match with installed KUKA semantics is unverified.
- **Retained interfaces.** `SceneModel` is registered as `SceneModel` and singleton `Scene` (not a
  documented Qt pattern). `FtsDevice::dataSampleHFReady`, `dataBatchReady`, `streamReset` and the
  `RDTResponse` registrations are kept for future force control (user decision);
  `ChamferPathSample`/`ChamferPathResult` serve analytical evaluation. Device/viewport error
  properties and QML displays are deferred (user). Link BREPs, STEP sources and `controller/`
  are retained assets.
