# Decisions on the review of 0cd8996 (user, 2026-11-20)

These are the user's answers to the questions the review left open. The ExecPlans in `plans/` that come from this review follow them.

| # | Question | Decision |
|---|---|---|
| 1 | Remove QmlChartBridge and the Qt6::Charts dependency | Remove them, and update the AGENT_CONTRACTS "Build and modules" sentence "QmlChartBridge stays in Application with its QtCharts requirement". |
| 2 | Make `PlcDevice::writeMessage` private and drop the unused PLC request kinds | Yes: drop READ_IO, READ_REG, WRITE_REG and WRITE_RAW request building and response decoding. `writeMessage` is no longer reachable through `invoke`. |
| 3 | PlcMessageManager | Make it a `Q_GADGET`, not a QObject, held by value in PlcDevice. Remove the unused `PlcMessage` QML registration (`PlcMessageQml` in src/application/backendqmltypes.h). Update the contract sentence "PlcMessageManager remains a QObject with dictionary APIs". |
| 4 | Rename the devices' `connect`/`disconnect` | Rename them in FtsDevice, PlcDevice and RsiDevice, and update the invoke strings in Network.qml. |
| 5 | Remove the `IgnorePreviewJointPositionLimits` switch | Remove the switch and all of its branches. |
| 6 | Defect 3 (Sampling and the test LIN while a calibration run starts) | Option A: refuse `prepare`, `prepareCalibration` and `prepareTestLin`, and disable their buttons, whenever the calibration run state is not Idle (Requested, Starting or Started). |
| 7 | An unreadable `settings/probe_calibration.json` | After a failed read, write nothing to the file. Keep showing the error until the user fixes or deletes the file and the application reads it successfully. |
