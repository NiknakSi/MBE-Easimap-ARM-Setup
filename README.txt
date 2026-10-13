MBE Easimap ARM Setup — native C++ version 0.2

GET STARTED

Download a published binary from:
  https://github.com/NiknakSi/MBE-Easimap-ARM-Setup/releases
If no release is available, build from source using README.md.

1. Install Easimap using SBD/MBE's normal installer.
2. Connect the MBE USB-CAN cable to Windows. In VMware Fusion or another VM,
   connect the USB device to the Windows guest.
3. Open MBE-Easimap-ARM-Setup.exe and approve the administrator prompt.
4. Click Install if setup is required. Wait for "Ready for Easimap".
5. Open Easimap and check the cable interface and vehicle connection.

The utility is one native Windows ARM64 executable, with no separate runtime
dependency. It needs Windows 11 ARM64 and administrator access. No separate
runtime installation, Visual C++ Redistributable, accompanying config file or
downloaded runtime is needed. Its C/C++ runtime is linked statically. All DLL
dependencies are Windows system components. Easimap remains a separate
installation.

Install is disabled for an already configured cable. With no matching cable
connected, both Install and Uninstall are disabled. The window detects
connection changes automatically every two seconds; Refresh checks immediately.
Setup rechecks the connected device before making changes. Repeated install
preserves the original uninstall backup.

The window has only a Close button, uses the cog icon, and supports sharp
per-monitor high-DPI rendering. Setup operations run in the background while
progress is displayed. Wait for an operation to finish before closing.

SUPPORTED CABLE

MBE 985 USB-CAN interface with hardware ID USB\VID_FEFE&PID_0001 only. The
utility selects Microsoft's existing WinUSB driver and registers the original
MCL interface GUID for Easimap. It does not modify ECU settings, redistribute
vendor software, replace Windows USB driver binaries, or alter driver-signing
policy.

UNINSTALL

Connect the cable, open the utility and click Uninstall. Confirm the restoration
prompt. It restores the driver selection and interface registrations saved
before installation. The shared Windows WinUSB driver package remains available
for other devices.

If a cable was configured manually and no backup exists, Uninstall removes only
the Easimap registration and retains its current Windows driver. The
confirmation explains which operation will be performed.

If another driver has since replaced the setup driver, restoration stops and
keeps the backup. If a restart is required, restart Windows, reconnect the cable
and run Uninstall again to finish. Failed operations retain their backup.

To remove the setup utility itself, close it and delete the executable. It does
not create a service, startup task or background process.

DATA AND TROUBLESHOOTING

Logs and active/archived backups remain under:
  C:\ProgramData\MBE-Easimap-ARM-Setup
Keep this folder while a cable still needs uninstall or recovery. The utility
preserves the original backup across retries and prevents simultaneous setup
instances.

No cable connected: check the USB connection and VM passthrough. Setup needs
attention: keep the cable connected, close Easimap and retry. If instructed,
reconnect the cable or restart Windows and click Refresh.

"Ready for Easimap" confirms that the Windows driver and cable interface are
active. Check vehicle/ECU communication separately in Easimap. Successful
vehicle communication has been reported on a Mac Mini with a Windows 11 ARM VM
in VMware Fusion. Other configurations still need validation. The utility is
unsigned; Windows may display publisher or SmartScreen warnings. Its
administrator prompt is expected for device configuration changes.

SOURCE AND DEVELOPMENT

Open MBE-Easimap-ARM-Setup.sln in Visual Studio 2026. src/ contains the primary
C++20 implementation and resources; tests/ contains backup-format checks and the
development-only live cable cycle. Read README.md for build/debug instructions
and docs/ARCHITECTURE.md for details. The compiler and Windows SDK are
development requirements only.

Copyright (c) 2026 Simon Hoade.

Licensed under GNU GPL v3.0 only; see LICENSE. Include the licence with binary
distributions and provide access to their corresponding source. This independent
setup tool is not supplied or endorsed by SBD or MBE.
