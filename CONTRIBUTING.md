# Contributing

Start with the solution and Debug | ARM64. Keep changes small and describe the
behaviour being changed and how it was checked. Follow the existing C++20 style
and `.clang-format`. Add XML comments for meaningful API contracts and short
implementation comments for surprising Windows behaviour.

Preserve these guarantees:

- Match the exact supported hardware ID and recheck presence before mutation.
- Keep the original backup across repair/retry operations.
- Preserve unrelated interface GUIDs and their registry value types.
- Restore registrations after driver binding; Windows can clear them on a
  switch.
- Verify restoration before archiving the active backup.
- Preserve readable recovery backups across native releases. Changes to the
  persisted format or filename require an explicit, tested migration.
- Keep the executable independent of installed application runtimes. Changes to
  `/MT`, `/MTd`, dependencies or manifests require checking the delivered
  imports.
- Keep window controls on the UI thread and setup operations off it. Closing
  must remain blocked until the worker's completion is handled.

Run BackupTests after backup changes and the appropriate build configurations
before submitting a change. For device behaviour, use the explicit live
procedure in [TESTING.md](docs/TESTING.md), on a development cable disconnected
from an ECU. A successful build does not prove vehicle communication or every
Windows state.

Do not commit `artifacts/`, `.vs/`, personal `.user` settings, binaries, device
logs or recovery backups. Contributions are accepted under this project's GNU
GPL v3.0 licence; see [LICENSE](LICENSE). Include only code and assets you have
the right to contribute under those terms.
