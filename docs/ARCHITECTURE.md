# Architecture

The application is C++20 using the Windows desktop API, targeting ARM64. There
are no third-party library dependencies. MSVC and the Windows SDK are needed to
build; end users need only Windows 11 ARM64. Release uses `/MT` and Debug uses
`/MTd`, linking the C/C++ runtime statically. `build/Native.props` keeps all
projects on matching settings. The core is a static library linked into the
executable.

## Code map

| Area | Files | Responsibility |
| --- | --- | --- |
| App | `src/app/main.cpp` | Entry point, arguments, global mutex, logging and native controls. |
| Resources | `src/app/resources/` | Cog icon, version information, administrator and DPI manifest. |
| Device access | `src/core/device.cpp` | SetupAPI enumeration/binding, configuration-manager restart and registry values. |
| Workflow | `src/core/setup.cpp` | Install/uninstall decisions, XML snapshots and verification. |
| Shared API | `src/core/setup.h` | Observations, saved state, errors and documented operation contracts. |
| Text | `src/core/resources/strings.rc`, `strings.h`, `strings.cpp` | Embedded captions and complete messages with numbered inserts. |
| Tests | `tests/` | Non-mutating backup tests and an explicit live-device regression cycle. |

`Text` is a `std::wstring` (UTF-16 on Windows), used at Windows API boundaries
and for UI messages. UTF-8 is used for logs and XML. `mbe::Error` carries a
user-facing Unicode message. Small local ownership wrappers release device sets,
registry keys, file handles and COM interfaces when their scope ends, including
on errors. This is the C++ RAII pattern: destructors release owned resources
automatically when their scope ends, without callers needing to remember every
cleanup path.

## UI and worker ownership

`Window` owns the controls and all mutable UI state. Device changes run on a
worker thread. The worker posts heap-allocated progress/completion messages;
ownership transfers to the window only when posting succeeds. The window wraps
them in `unique_ptr` while handling each message. A busy flag blocks closing and
another operation until completion is handled on the UI thread.

Timer refreshes detect cable connections every two seconds. An unchanged cable
list retains its dropdown selection. DPI layout uses logical 96-DPI positions
and scales once for the actual monitor DPI. Fonts and icon sizes are rebuilt on
`WM_DPICHANGED`. The manifest requests PerMonitorV2 and themed common controls.

The global mutex prevents multiple setup instances from changing a cable
concurrently, including instances running in different Windows sessions.

## Device setup

Enumeration and every binding operation check the supported exact hardware ID.
The app selects one existing Windows INF and its exact section, using SetupAPI
class-driver enumeration. This mirrors Device Manager's manual selection,
without creating a custom INF or installing a replacement USB binary. The shared
WinUSB package is never removed.

Installation checks availability, saves the original state, binds WinUSB if
needed, rechecks presence, adds the MCL GUID, restarts the cable and verifies
its interface. Already-ready installation returns without replacing the
baseline.

Uninstall verifies that the current driver is still appropriate for restoration.
It restores the saved binding before registry values, because Windows may clear
Device Parameters during a driver switch. The active backup is archived only
once restoration is verified. A manually configured cable with no backup loses
only the MCL registration; its driver stays installed.

## Strings and resources

`strings.rc` contains the native Windows string-table resources. The app and
both test executables compile the shared table; the static core loads text from
whichever executable hosts it. The native resource editor can edit captions
without changing C++ code. English is the current resource language.

`textResource` copies a length-delimited string without a fixed buffer,
preserving Unicode and long captions. `formatResource` replaces numbered
`{0}`–`{9}` inserts with literal argument text, allowing their order or
repetition to change in a message. Invalid syntax or missing arguments throw.
Both preserve GetLastError: loading the caption for `fail(...)` must not
overwrite the original API failure.

Application labels, prompts, progress, errors and diagnostic captions belong in
this table. Windows service/class names, registry paths, interface GUIDs, backup
XML field/enum names, command switches and test data stay in source because
their values are technical contracts. Windows-supplied error descriptions remain
provided by the OS. Minimal errors reporting missing/malformed resources and the
noexcept `Error::what()` fallback stay in code because resource loading itself
has failed or allocating a translated string would break the exception contract.
Developer-only test assertions are also kept in the test source.

The table currently uses contiguous IDs, checked by BackupTests. When adding a
new entry, assign the next ID and update `IDS_LAST_STRING`; avoid renumbering
existing entries. Changes to text require rebuilding the executables.

## Recovery backups

Snapshots use a versioned XML schema and a SHA-256 filename derived from the
uppercase UTF-8 device instance ID. The supported USB instance IDs are ASCII, so
the native hash uppercases ASCII explicitly. XML text supports Unicode,
escaping, empty data and the distinction between absent/existing values. Windows
XmlLite reads the snapshots with DTDs prohibited and limits/checks for size,
structure, version, identity, registration names and types.

The location remains `%ProgramData%\MBE-Easimap-ARM-Setup`, including Logs and
Backups. Baselines are written to a temporary file, flushed and renamed; repair
attempts reuse an existing original baseline. A failed operation leaves that
baseline available for recovery. These steps support recovery but do not claim
transactional driver changes or verified power-loss recovery.

## Build and debugging

Open the root solution in Visual Studio 2026 with its ARM64 C++ tools and
Windows SDK installed. The application is the first project in the solution.
Debug and Release outputs and each project's intermediates are isolated under
`artifacts/`. PDB files support stepping through source and matching symbols to
crash reports. See [TESTING.md](TESTING.md) for startup projects, breakpoints
and test commands.

An installed utility executable is not overwritten by a solution build. Recovery
backups remain valid across native builds; preserve them while a cable still
needs uninstall or recovery.
