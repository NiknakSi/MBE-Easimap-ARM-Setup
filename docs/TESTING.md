# Testing and debugging

## Safe checks

Build Debug | ARM64, set BackupTests as startup project and press F5 on Windows
ARM64, or run `artifacts\bin\ARM64\Debug\BackupTests.exe`. It exercises XML
round-trip behaviour, Unicode/escaping, absent/empty values, invalid device and
version, malformed XML, and DTD rejection. It also checks that every compiled
string resource loads, Unicode captions and literal inserts survive formatting,
missing arguments are rejected, and error caption lookup preserves GetLastError.
It does not enumerate or modify cable settings, and needs neither administrator
access nor a connected cable.

These checks are a small native console test runner, without a test-framework
package. Results appear in its console and its exit code is nonzero on failure.
They are not registered in Visual Studio Test Explorer. Use F5 or the
executable.

The GUI app also accepts elevated read-only diagnostic commands:

```
MBE-Easimap-ARM-Setup.exe --status --result C:\Temp\status.txt
MBE-Easimap-ARM-Setup.exe --probe-drivers --result C:\Temp\drivers.txt
MBE-Easimap-ARM-Setup.exe --validate-backup --result C:\Temp\backup.txt
```

Create the result directory first. `--validate-backup` needs an existing active
backup. Use `--device "USB\VID_FEFE&PID_0001\SERIAL"` to select a cable. The app
is a Windows GUI executable, so `--result` captures reports instead of stdout.
Command-line arguments can be entered in Project Properties > Debugging >
Command Arguments; keep personal arguments in the ignored `.vcxproj.user` file.

## Debugging setup

Run Visual Studio as administrator, set MBE.Easimap.Setup as startup project,
and close any other setup utility. Useful breakpoints are:

- `Window::operation`: selected cable, busy-state transition and worker startup.
- `mbe::install` / `mbe::uninstall`: decisions about backups and driver
  restoration.
- `mbe::bind`: exact INF section selection and `DiInstallDevice` result.
- `saveSnapshot` / `loadSnapshot`: persisted baseline and recovery checks.
- `Window::message` at the completion message: the worker-to-UI boundary.

Normal C++ exceptions are used to report recoverable failures. Visual Studio's
Exception Settings can break on thrown C++ exceptions, including ones the app
handles. Inspect `mbe::Error::text` for its complete Unicode message.

Builds write PDB symbols beside their executables. Keep the matching executable,
PDB and source revision together when investigating a reported failure. Native
logs are under `%ProgramData%\MBE-Easimap-ARM-Setup\Logs`.

## Explicit live device cycle

LiveDeviceTests is excluded from the normal solution build. Right-click that
project and build it explicitly. It is a separate console executable requesting
administrator access. To run from an elevated terminal:

```
artifacts\bin\ARM64\Debug\LiveDeviceTests.exe --run-live-device-cycle "USB\VID_FEFE&PID_0001\SERIAL"
```

Use the actual development cable instance ID. The required argument prevents an
accidental F5 launch from performing changes. This cycle deliberately clears and
reinstalls its driver, injects unrelated interface values, verifies exact
restoration and repeated-operation behaviour, then restores entry settings and
the original active backup. It is never run by building the solution.

Keep the cable disconnected from an ECU, keep Windows powered, and close Easimap
and other setup utilities. If interrupted, retain the backups and inspect their
contents before retrying. Recovery after abrupt termination or power loss is a
validation boundary, not a guarantee of the development test runner.

For a UI change, verify connected/disconnected button states, high-DPI text and
layout, and the completion/error flow. For a runtime or packaging change,
inspect DLL imports. Do not infer vehicle communication success from an active
USB interface.
