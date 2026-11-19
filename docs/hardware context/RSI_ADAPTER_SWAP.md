# Moving RSI to the onboard Ethernet port

This procedure swaps the two wired PC adapters: the KR C4 KLI cable (RSI and the `RoboCrapJobs` share) moves from the ASIX USB adapter to the onboard Intel port, and the switch cable (PLC, force sensor, VFD) moves to the ASIX USB adapter.

Why: in sessions `calibration_20261117_130745_826`, `calibration_20261118_160007_715` and `calibration_20261118_164135_671` RSI requests were held for 16–50 ms below RoboCrap and then delivered 4–7 at once (`plans/RECORDS_AND_CAPTURE_CLEANUP_EXECPLAN.md`, Surprises & Discoveries). The ASIX adapter shares the PC's only USB host controller with the Realtek USB Wi-Fi adapter. The Intel I219-V is not on USB, so after the swap RSI no longer depends on USB. The force sensor's UDP stream then goes through USB; its rate is far above what force control needs, so occasional lost datagrams are acceptable.

## State before and after

| Adapter (hardware) | Before: Windows name, addresses, cable | After: Windows name, addresses, cable |
|---|---|---|
| ASIX USB to Gigabit Ethernet (MAC D0-37-45-A7-B6-6E) | `KRC4-Direct`; 172.31.1.47/24 and 192.168.2.100/24; default route to 192.168.2.200, metric 9999; cable to the KR C4 KLI | `Devices-Switch`; 192.168.1.100/24; cable to the DGS-1005P switch |
| Intel Ethernet Connection (14) I219-V, onboard 8P8C port (MAC 04-42-1A-A9-B8-A4) | `Devices-Switch`; 192.168.1.100/24; cable to the switch | `KRC4-Direct`; 172.31.1.47/24 and 192.168.2.100/24; default route to 192.168.2.200, metric 9999; cable to the KR C4 KLI |

Nothing changes in RoboCrap or on the controller: `resources/qrc/config/db.json` and the firewall rule `RoboCrap RSI from KUKA` use IP addresses (172.31.1.47, 192.168.1.100), not adapters, and the controller still sees 172.31.1.47 and 192.168.2.100.

## 1. Prepare

1. On the smartPAD: no program running (CELL stopped), robot at rest. The RSI and share connections are interrupted during the swap.
2. Close RoboCrap.
3. Open PowerShell as Administrator: Start → type `PowerShell` → right-click "Windows PowerShell" → "Запуск от имени администратора" (Run as administrator). All commands below run in this window.
4. Save the current configuration (for rollback):

```powershell
New-Item -ItemType Directory -Force C:\RoboCrapNetBackup | Out-Null
ipconfig /all > C:\RoboCrapNetBackup\ipconfig_before.txt
route print > C:\RoboCrapNetBackup\route_before.txt
Get-NetAdapter | Format-Table Name,InterfaceDescription,MacAddress -AutoSize > C:\RoboCrapNetBackup\adapters_before.txt
```

## 2. Remove the addresses from both adapters

```powershell
Remove-NetRoute -InterfaceAlias "KRC4-Direct" -DestinationPrefix "0.0.0.0/0" -Confirm:$false
Remove-NetIPAddress -InterfaceAlias "KRC4-Direct" -IPAddress 172.31.1.47 -Confirm:$false
Remove-NetIPAddress -InterfaceAlias "KRC4-Direct" -IPAddress 192.168.2.100 -Confirm:$false
Remove-NetIPAddress -InterfaceAlias "Devices-Switch" -IPAddress 192.168.1.100 -Confirm:$false
```

Check: `Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias "KRC4-Direct","Devices-Switch"` lists only `169.254.x.x` addresses or nothing.

## 3. Swap the Windows names

The commands select each adapter by its hardware description, so they cannot rename the wrong one:

```powershell
Get-NetAdapter -InterfaceDescription "ASIX USB to Gigabit Ethernet Family Adapter" | Rename-NetAdapter -NewName "Swap-ASIX"
Get-NetAdapter -InterfaceDescription "Intel(R) Ethernet Connection (14) I219-V" | Rename-NetAdapter -NewName "KRC4-Direct"
Get-NetAdapter -Name "Swap-ASIX" | Rename-NetAdapter -NewName "Devices-Switch"
Get-NetAdapter -Name "KRC4-Direct","Devices-Switch" | Format-Table Name,InterfaceDescription -AutoSize
```

Expected: `KRC4-Direct` = Intel(R) Ethernet Connection (14) I219-V, `Devices-Switch` = ASIX USB to Gigabit Ethernet Family Adapter.

The same can be done by hand: Win+R → `ncpa.cpl` → select the adapter → F2 → type the name.

## 4. Swap the cables

1. Unplug the cable that goes to the KR C4 (KLI) from the ASIX USB adapter.
2. Unplug the cable that goes to the switch from the PC's onboard port.
3. Plug the KR C4 cable into the PC's onboard port.
4. Plug the switch cable into the ASIX USB adapter.
5. Wait for the link LEDs, then check that both adapters are up:

```powershell
Get-NetAdapter -Name "KRC4-Direct","Devices-Switch" | Format-Table Name,Status,LinkSpeed -AutoSize
```

Expected: both `Up`; `KRC4-Direct` at the KLI's speed (100 Mbps or 1 Gbps).

## 5. Assign the addresses and the route

```powershell
New-NetIPAddress -InterfaceAlias "KRC4-Direct" -IPAddress 172.31.1.47 -PrefixLength 24
New-NetIPAddress -InterfaceAlias "KRC4-Direct" -IPAddress 192.168.2.100 -PrefixLength 24
New-NetIPAddress -InterfaceAlias "Devices-Switch" -IPAddress 192.168.1.100 -PrefixLength 24
New-NetRoute -InterfaceAlias "KRC4-Direct" -DestinationPrefix "0.0.0.0/0" -NextHop 192.168.2.200 -RouteMetric 9999
```

The default route only gives the network an identity (the KR C4 Windows interface answers ARP); the KR C4 does not route, and metric 9999 keeps Internet traffic on Wi-Fi (`HARDWARE_SETUP.md`, "Network profile of KRC4-Direct"). Both stores are written: the route survives a restart.

## 6. Network profile

```powershell
Disable-NetAdapter -Name "KRC4-Direct" -Confirm:$false; Enable-NetAdapter -Name "KRC4-Direct"
Start-Sleep -Seconds 10
Get-NetConnectionProfile -InterfaceAlias "KRC4-Direct"
```

Expected: Name `Сеть` (or a new name such as `Сеть 2`), NetworkCategory `Private`. If the category is `Public`:

```powershell
Set-NetConnectionProfile -InterfaceAlias "KRC4-Direct" -NetworkCategory Private
```

`Private` is needed for the share's SMB rule (TCP 139 is allowed only for Private). `Devices-Switch` stays an unidentified Public network, as before; PLC and sensor traffic already work with that.

## 7. Power saving off on the Intel adapter (new KRC4-Direct)

By hand: Win+R → `devmgmt.msc` → "Сетевые адаптеры" (Network adapters) → double-click "Intel(R) Ethernet Connection (14) I219-V".

1. Tab "Дополнительно" (Advanced), set each property and press OK at the end:
   - Energy Efficient Ethernet → Off
   - Ultra Low Power Mode → Disabled
   - Reduce Speed On Power Down → Disabled
   - Selective Suspend → Disabled
   - Interrupt Moderation → Disabled (RSI sends 250 packets/s; without moderation each packet is handed to Windows at once)
   - System Idle Power Saver → Disabled (already)
2. Tab "Управление электропитанием" (Power Management): clear "Разрешить отключение этого устройства для экономии энергии" (Allow the computer to turn off this device to save power) → OK.

Or in the administrator PowerShell (each command briefly resets the link):

```powershell
Set-NetAdapterAdvancedProperty -Name "KRC4-Direct" -DisplayName "Energy Efficient Ethernet" -DisplayValue "Off"
Set-NetAdapterAdvancedProperty -Name "KRC4-Direct" -DisplayName "Ultra Low Power Mode" -DisplayValue "Disabled"
Set-NetAdapterAdvancedProperty -Name "KRC4-Direct" -DisplayName "Reduce Speed On Power Down" -DisplayValue "Disabled"
Set-NetAdapterAdvancedProperty -Name "KRC4-Direct" -DisplayName "Selective Suspend" -DisplayValue "Disabled"
Set-NetAdapterAdvancedProperty -Name "KRC4-Direct" -DisplayName "Interrupt Moderation" -DisplayValue "Disabled"
Get-NetAdapterAdvancedProperty -Name "KRC4-Direct" | Where-Object DisplayName -match "Energy|Ultra|Reduce|Selective Suspend$|Interrupt Moderation$|Idle" | Format-Table DisplayName,DisplayValue -AutoSize
```

The power-management checkbox is set by hand as in item 2. The ASIX adapter (now `Devices-Switch`) keeps its settings.

## 8. Checks before starting RoboCrap

```powershell
Get-NetConnectionProfile -InterfaceAlias "KRC4-Direct"                         # Private
Get-NetRoute -DestinationPrefix "0.0.0.0/0"                                   # KRC4-Direct, NextHop 192.168.2.200, metric 9999
Find-NetRoute -RemoteIPAddress 8.8.8.8 | Select-Object InterfaceAlias -First 1  # not KRC4-Direct
ping -S 172.31.1.47 172.31.1.147                                              # KR C4 RSI interface
ping -S 192.168.2.100 192.168.2.200                                           # KR C4 Windows interface (share)
ping -S 192.168.1.100 192.168.1.2                                             # PLC
ping -S 192.168.1.100 192.168.1.3                                             # force sensor
ping -S 192.168.1.100 192.168.1.4                                             # VFD
netsh advfirewall firewall show rule name="RoboCrap RSI from KUKA" verbose    # LocalIP 172.31.1.47/32, RemoteIP 172.31.1.147/32
```

Every ping must answer. If the KR C4 RSI interface does not answer ping, check the cable and `Get-NetAdapter` first; the KR C4 may also filter ICMP on that interface, in which case the RSI test in step 9 decides.

## 9. Test run

1. Start RoboCrap, connect the PLC and bind the RSI socket (Network settings, unchanged addresses). Check in PowerShell: `Get-NetUDPEndpoint -LocalPort 5555` shows 172.31.1.47.
2. Optional, to see where any remaining delay comes from: start Wireshark, capture on `KRC4-Direct` with the capture filter `udp port 5555`, and leave it running during the run.
3. In T1: one short RSI run (the test LIN or `RSI_Probe_Check`) to confirm that RSI and the share work.
4. Then one full calibration session in AUT EXT, authorized as usual.
5. Give the session id (`records/calibration_<id>/`) to the agent: the late-request statistics are compared with the sessions listed above.

## Rollback

Repeat steps 2–6 with the old assignment: remove the addresses and the route, rename the Intel adapter back to `Devices-Switch` and the ASIX adapter back to `KRC4-Direct`, swap the cables back, assign 172.31.1.47/24, 192.168.2.100/24 and the 192.168.2.200 route to the ASIX adapter and 192.168.1.100/24 to the Intel adapter. The files in `C:\RoboCrapNetBackup` show the previous state.

## After a successful test

The network table in `docs/code context/ARCHITECTURE.md` ("Network") and the adapter rows in `docs/hardware context/HARDWARE_SETUP.md` ("Network profile of KRC4-Direct", "Power settings for RSI") still describe the ASIX adapter on the KLI; they are updated once the swap is confirmed.
