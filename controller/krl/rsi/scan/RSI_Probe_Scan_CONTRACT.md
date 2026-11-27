# EG22 scan controller contract

The PC requests program 2. CELL acknowledges PGNO 1 and calls RSI_Probe_Scan. Confirm the installed CASE, exclusive output 2 assignment, X1.1 routing, tool, base, HOME, share and probe RSI context before commissioning. The PC also requires physically justified axial, lateral and angular measurement bounds. Physical Scan remains disabled pending this evidence.

The module reads version 1 of /RoboCrapJobs/scan_data.txt using REMOUNT_SHARE, READ_FRAME and READ_POS. The header supplies the ToolFrame, transfer velocity in m/s, transfer acceleration in m/s² and up to 600 commands. The route starts with PTP, uses LIN before each numbered SEARCH, returns to that start with LIN, retreats and ends with one HOME. The controller checks the complete list, S/T, transfer limits, EOF and close before the first PTP.

PROBE_SEARCH is a global function in RSI_Probe_Calibration.src. Both programs use the same relative RSI context, TouchIndex, input 8, Start-low delay and Stop/Result routing. A clean miss returns 0 after RSI_OFF and allows retraction to the saved search start. An RSI enable/disable failure ends the scan without commanding another move. Physical recovery follows the installed controller procedure.

The first executable instruction clears output 2. Only the normal path sets it after final HOME and WAIT SEC 0. It stays high through CELL return. The PC needs a fresh low during acknowledged execution, subsequent high, HOME, Finished, all ordered AIPos detections and a complete capture. A missed search, early return or an inherited high fails the attempt.

Abort revokes PC corrections. RoboCrap keeps the export and table until the previous program has terminated and the GUI has received the final capture or classified its absence. After an unknown start or restart, controller recovery must establish an idle, deselected program outside AUT EXT followed by fresh CELL-ready feedback in AUT EXT. The marker in settings is managed by RoboCrap.
