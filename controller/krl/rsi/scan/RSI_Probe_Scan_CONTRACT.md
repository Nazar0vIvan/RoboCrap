# EG22 scan controller contract

The PC requests program 2. CELL acknowledges PGNO 1 and calls RSI_Probe_Scan. Confirm the installed CASE, exclusive output 2 assignment, X1.1 routing, tool, base, HOME, share and probe RSI context before commissioning. Physical Scan remains disabled pending this evidence.

The module reads version 1 of /RoboCrapJobs/scan_data.txt using REMOUNT_SHARE, READ_FRAME and READ_POS. The header supplies the ToolFrame, transfer velocity in m/s, transfer acceleration in m/s² and up to 600 commands. The route starts with PTP, uses LIN before each numbered SEARCH, returns to that start with LIN, retreats and ends with one HOME. The controller checks the complete list, S/T, transfer limits, EOF and close before the first PTP.

PROBE_SEARCH is a global function in RSI_Probe_Calibration.src. Both programs use the same relative RSI context, TouchIndex, input 8, Start-low delay and Stop/Result routing. A clean miss returns 0 after RSI_OFF and allows retraction to the saved search start. An RSI enable/disable failure ends the scan without commanding another move. Physical recovery follows the installed controller procedure.

The first executable instruction clears output 2. Only the normal path sets it after final HOME and WAIT SEC 0. It stays high through CELL return. The PC needs a fresh low during acknowledged execution, subsequent high, HOME, Finished, all ordered AIPos detections and a complete capture. A missed search, early return or an inherited high fails the attempt.

Leaving AUT EXT fails the scan, revokes corrections and requests final capture. The PC retains the export, stream table and marker until capture closure. A failed scan or startup marker can then be released in either mode on fresh connected PLC feedback with step IDLE, APPL_RUN off, program state neither Running nor Interrupted, connected RSI reporting idle and no pending Start/Resume. Outside AUT EXT, PRO_ACT must also be off. In AUT EXT, recovery accepts either level of PRO_ACT and PGNO_REQ. Normal completion still requires the CELL and scan evidence above.

A single PLC SNAPSHOT timeout or matched error keeps the scan session open. Start and Resume are unavailable while PLC I/O is stale. The PC retries SNAPSHOT immediately with a new transaction ID and a 1 s deadline. Valid I/O feedback restores freshness. A second failure without intervening valid I/O disconnects the PLC and fails the scan through the existing disconnect handler.
