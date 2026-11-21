# RSI_Chamfer_Hole nominal run: controller/PC contract

The sequence is HOME, PTP PsIn, LIN LeadStart, lead-in / machining / lead-out
under RSI, LIN PsOut and HOME. Only completed RSI permits return. Force correction is disabled.

The repository stores the SRC/DAT in `controller/krl/rsi/chamfer/`, the Ethernet configuration
in `controller/rsi_config/` and the shared KRL helpers in `controller/krl/lib/`.
- `RSI_Chamfer_Hole.src` and `RSI_Chamfer_Hole.dat`: the KRL program (module `RSI_Chamfer_Hole`)
  with its local `READ_JOB()` file reader.
- `RSI_Chamfer_Hole.rsi`, `RSI_Chamfer_Hole.rsi.xml` and `RSI_Chamfer_Hole.rsi.diagram`: the RSI Visual context, loaded by `RSI_CREATE("RSI_Chamfer_Hole.rsi")`. Copy these from the RSI Visual export; do not hand-edit them.
- `RSI_EthernetConfig.xml`: the Ethernet configuration.
- `REMOUNT_SHARE.src`, `READ_POS.src` and `READ_FRAME.src`:
  shared KRL helpers, installed as separate SRC files on the controller. The first routine of
  an SRC file is global without the `GLOBAL` keyword. Remove a previously installed
  `CLOSE_FILES.src`; nothing calls it.

PC side:
- `src/trajectories/chamfer/chamferrsistream.cpp`: computes the corrections.
- `src/application/scene/scenemachiningpreparation.cpp`: RSI Sampling and the shared file.
- `src/devices/rsi/rsidevice.cpp`: the UDP partner.

## Frames

KRL sets `$BASE=$NULLFRAME`, so BASE coincides with WORLD. The application's numerical
poses are robot-base-relative; using them unchanged also requires robot-base/WORLD alignment.
This SRC does not set or read `$ROBROOT`; it does not establish that additional alignment.
KRL sets `$TOOL` from ToolFrame:
the flange-to-TCP transform saved with the sampled path (the collet frame combined
with the TCP), not a `TOOL_DATA` entry. `$LOAD` is not set by the program; the load
data already active on the controller stays in effect. XYZ are in mm.
ABC are Z-Y-X Euler angles in degrees. POSCORR1 uses RefCorrSys = World. Each relative
A/B/C offset defines a rotation in WORLD axes at the TCP. Compose its rotation matrix
before the preceding TCP rotation matrix; do not add the offset to absolute TCP Euler angles.

## Signals

ETHERNET1 exchanges these signals with the PC at 172.31.1.47:5555 (sensor type
ImFree). The KR C4 RSI interface is 172.31.1.147/24 on the KLI, cabled directly to the PC's
USB network adapter ("KRC4-Direct"); no switch may be in this path: the D-Link DGS-1005P held
requests for up to 19 ms (`plans/RSI_NETWORK_SPLIT_EXECPLAN.md`). The controller also
sends RIst, AIPos, MACur and Delay (late-packet count, `DEF_Delay`); the PC ignores them, and
`python/rsi/rsi_packet_timing.py` reads RIst and Delay from the packet capture. The shared
configuration also sends `TouchIndex` (LONG, In3) and `Touch` (BOOL, In4) for
`RSI_Probe_Calibration`; this context leaves both inputs unconnected, and the controller then
sends the texts `not linked:3` and `not linked:4` in them (RSI_Probe_Check capture, 2026-11-11).

| Direction | Port | XML | Connection |
| --- | --- | --- | --- |
| Controller → PC | In1 | `Start`, BOOL | SEN_PINT1 reads `$SEN_PINT[1]` |
| Controller → PC | In2 | `Result`, LONG | SEN_PINT2 reads `$SEN_PINT[2]` |
| PC → controller | Out1..6 | `RKorr.X/Y/Z/A/B/C`, DOUBLE, HOLDON 0 | POSCORR1 CorrX..CorrC |
| PC → controller | Out7 | `Stop`, BOOL, HOLDON 1 | STOP1, Mode ExitMoveCorr |
| PC → controller | Out8 | `Result`, LONG, HOLDON 1 | MAP2SEN_PINT1 writes `$SEN_PINT[2]` |

KRL owns `$SEN_PINT[1]`:
- It is cleared at the start of the program.
- It is set to 1 immediately before `RSI_MOVECORR`.
- It is cleared after `Completed` has been read.

`$SEN_PINT[2]` is written every cycle by the PC's Result through
MAP2SEN_PINT1. It keeps its value after the program ends, so KRL clears it at the start
of the program; a previous run's Result 1 cannot then count as completion.
Only `Completed=$SEN_PINT[2]==1` and a successful `RSI_OFF()` allow LIN PsOut
and the return HOME.

RSI calls use the documented defaults: `RSI_CREATE` activates the container, `RSI_ON(#RELATIVE)`
runs in `#IPO_FAST` and `RSI_MOVECORR()` stops with `#RSIBRAKE`. There is no container ID or
`RSI_DELETE`: `RSI_RESET()` at program start deletes every container left by an earlier run.
`RSI_ON`/`RSI_OFF` act on all created containers, so any other RSI program on this controller
must also start with `RSI_RESET()`.

## PC behavior

The controller sends one `<Rob>` packet per 4 ms cycle. The PC echoes the IPOC of the newest
valid request read in each receive callback. A reply can miss the controller's deadline.
The receive/reply path writes no console output. The 2026-10-21 captures
showed log lines delaying replies past the controller's acceptance time, so the controller
used zero while the PC had already advanced to the next correction.

- **Start = 0:** zero corrections, Stop 0, Result 0, and the stream index returns to the first correction.
- **Start = 1 with a stream:** one correction per packet, Result 0, Stop 0. The stream ends with 50 zero corrections so the robot settles.
- **After the last correction:** zero corrections with Result 1, until a packet echoes Result = 1.
- **Then:** zero corrections with Result 1 and Stop 1, until Start returns to 0.
- **Start = 1 without a stream:** zero corrections with Stop 1 and Result 0. RSI_MOVECORR ends, and KRL performs no return motion.

Every accepted RSI Sampling click discards the GUI-side result and queues an empty correction
array before starting a fresh sampling worker, even when no GUI result is held. On ControlIO,
every correction update immediately replaces the array and resets the sender to Idle at index 0,
including from Streaming, Finished or Stopping. A successful current sample and shared-file export
install the new array; failed sampling/export leaves the sender empty. The first accepted Start = 1
request after installation sends increments[0], then subsequent requests send the following values;
no preceding Start = 0 packet is required. While empty, Start = 1 sends Stop 1 and Result 0.
Before clearing/installing, the device discards requests already buffered in its socket without
replying or advancing either stream; outgoing accepted-peer rows remain in the outgoing capture.
It resets the learned UDP source port, learning it again from a
subsequent valid request. Configured addresses and the bound socket remain available.
These queued updates reset PC state; sampling does not clear corrections accumulated in the
controller's current RSI context or start a new KRL program. Start the new KRL run after Sampling succeeds.

Offsets are written with six fixed decimal places. Initialize reconstruction from the
six-decimal exported LeadStart XYZABC. Subtract the preceding reconstructed position from
the desired position. Multiply the desired rotation by the preceding reconstructed rotation's
transpose, then extract Z-Y-X ABC from that relative rotation. Round all six offsets,
add XYZ to the preceding position and compose the rounded offset rotation before the
preceding rotation. Reject a nonfinite result or a TCP residual above 0.0001 mm / 0.000001
rotation entries before appending the offset. Use the composed pose for the next offset.

Run the existing checked TCP IK/FK on the rounded initial pose and every reconstructed
pose. Check joint positions, rated joint speeds and XYZ acceleration over 0.004 s,
including the transition to zero velocity. Retain the composed pose for the next
offset and the checked joint configuration for the next IK. At the endpoint require
the saved LeadEnd Status and nearest 360-degree joint representatives (each angle
difference below 180 degrees); TCP accuracy uses the residual checks above.
Turn encodes angle signs, so it is not compared as a revolution count.

The summary's mm value is the maximum absolute X, Y or Z displacement from LeadStart.
Its degree value is the maximum shortest rotation angle from LeadStart, in [0, 180].
It is an orientation diagnostic; it neither counts full revolutions nor specifies MaxRotAngle.

## Run records

Sampling computes the correction table and retains the plan in memory. After the application
exports `shared_data.txt`, it arms the recorder with the job identifier, plan and exported bytes.
For a matching capture the recorder writes one `yyyy-MM-dd_HH-mm-ss_chamfer` folder under
`ROBOCRAP_RECORDS_DIR` (default `records/`). The folder time and duration cover the first through
last stored RSI request. Approach and retreat under KRL lie outside that interval.

`plan.csv` starts with the time-zero TCP and joints. Each motion row gives the analytical target,
joints checked against the pose reconstructed from rounded corrections, and the increment.
The final 50 settling cycles are counted in `session.json`. `rsi.csv` pairs each stored request
with its attempted reply, if any. `rsi_timing.csv` gives local dequeue and send timing for the
same rows. The recorder also archives `shared_data.txt` and writes `plc.csv` when PLC rows exist.

The controller's Result 1 echo marks completion. A capture ending earlier gets `_failed`.
The recorder writes `session.json` last. See the [record format](../../../../docs/code%20context/RECORDS_FORMAT.md)
for capture limits, session boundaries and column definitions.

Example request and reply (values illustrative):

    <Rob TYPE="KUKA"> ... <Start>1</Start><Result>0</Result><IPOC>123456</IPOC></Rob>
    <Sen Type="ImFree"><RKorr X="0.020000" Y="-0.000312" Z="0.001000" A="0.000000"
      B="0.000000" C="0.012000" /><Stop>0</Stop><Result>0</Result><IPOC>123456</IPOC></Sen>

## shared_data.txt

RSI Sampling writes `C:\RoboCrapJobs\shared_data.txt`. Before file access, `REMOUNT_SHARE()`
closes all KRL files from both the robot and submit interpreters (`krl_fclose_all`, `-99`),
removes `/RoboCrapJobs` and reconnects it to `//192.168.2.100/RoboCrapJobs` (KR C4 Windows
interface 192.168.2.200) using the
configured share credentials. Unmount error `-2` permits a fresh mount attempt; other
cleanup errors abort. The helper returns TRUE only when the final mount reports
`#DATA_OK`; only then does `READ_JOB()` open `/RoboCrapJobs/shared_data.txt`.
The application writes version 2: eight CRLF rows, `;` separators, six-decimal
floats and decimal integer S/T. XYZ is mm; ABC is principal Z-Y-X in degrees.

1. `2`: format version.
2. PsIn: staging XYZ and LeadStart ABC, then its own S/T.
3. LeadStart: initial RSI XYZABC, then its own S/T.
4. LeadEnd: composed final TCP XYZABC, then its own S/T.
5. PsOut: its own S/T only; KRL copies XYZ from PsIn and ABC from LeadEnd.
6. ToolFrame: saved flange-to-TCP XYZABC, without S/T.
7. `$VEL.CP` in m/s: Generate transfer speed (mm/s) / 1000.
8. `$ACC.CP` in m/s²: Generate transfer acceleration (mm/s²) / 1000.

Generate accepts only transfer speeds in (0, 3000] mm/s and accelerations in
(0, `cartesianAcceleration`] mm/s², that is at most `$VEL_MA.CP` = 3 m/s and `$ACC_MA.CP`
= 10 m/s² from the archived `$machine.dat`. The controller additionally rejects an
assignment above its live `$VEL_MA`/`$ACC_MA` ("Value assignment inadmissible") and stops.

S/T comes from saved animation joints at boundaries 1, 2, 5 and 6 respectively,
using the existing configuration classifier and one local FK per boundary. Turn
sets bits 0-5 for negative A1-A6. The writer checks all four exported/constructed
TCP poses against their saved boundary TCPs with 0.0001 mm / 0.000001 rotation-entry
tolerances before opening QSaveFile. A failure leaves corrections uninstalled.

`READ_JOB()` (local to the SRC) requires version 2 and accumulates one success flag:
READ_POS and READ_FRAME combine it with their own result (`Ok=Ok AND ...`).
READ_POS reads six REAL and two INT components into a POS initialized with S/T = -1;
#DATA_OK and S in 0-7 / T in 0-63 are required, so a truncated row fails. READ_FRAME
reads ToolFrame's six components. PsOut starts as a copy of PsIn, takes A/B/C from
LeadEnd and then reads and checks its own S/T pair. Both CP rows require #DATA_OK.
All four motion targets are POS in DAT. The file is closed before READ_JOB returns,
including after a wrong version; a failed `krl_fclose` also fails the job, and the next
run's REMOUNT_SHARE closes any file left open. Every input failure returns after HOME and
before `RSI_CREATE`. `$VEL.CP`/`$ACC.CP` are assigned before PTP PsIn, so a controller
limit violation also stops before PsIn. A matching app export, SRC/DAT and FILE_UTILS
helpers are required together. S/T selects the configuration for PTP; LIN ignores these
fields. No live configuration comparison before RSI_ON is required.

The PC holds the stream only while the sampled path exists and is compatible.
Whenever it holds no stream, it deletes the file: at application start, at a new
sampling request, on invalidation and at shutdown. KRL then stops after HOME
instead of moving to an old hole.

The operator confirmed on 2026-11-21 that the approach matched the preview and the robot retreated
through PsOut and returned to HOME. The RSI capture covers only the streamed part of that run.

## Not yet verified on the controller

- ETHERNET1 `Timeout` = 10 and `Precision` = 1 are not documented in the local RSI 3.3 manual. A Precision that rounded received values would make the robot stand still.
- Whether configured POSCORR1 limits cover the intended movement; the summary's shortest-angle excursion is not a MaxRotAngle bound.
- The BOOL spelling the controller sends for Start. The PC accepts `0`/`1` (the 2026-10-21 captures
  contain only these); a packet it cannot decode is kept in the packet capture and not answered.
- Whether a direct `$TOOL` assignment updates the smartPAD active-tool display (`$ACT_TOOL`); it does not affect motion.
- Releasing the T1 Start key during RSI_MOVECORR is not supported. Cancel the program and restart from HOME.
- Whether `krl_fscanf` reports a partially converted FRAME or REAL row as `#DATA_OK`
  (`STATE_T.HITS` would show the converted count), and that `$VEL.CP`/`$ACC.CP` keep their
  assigned values through the RSI calls up to LIN PsOut. Compilation of the restructured SRC
  and helpers is also unverified.
