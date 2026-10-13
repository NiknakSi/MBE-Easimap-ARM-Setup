# Sharing and releases

The native C++ solution, tests, resources and documentation are licensed under
GNU GPL v3.0 only; see [LICENSE](../LICENSE). Easimap and vendor software are
separate products and are not included in this repository or its releases. This
is an independent utility, not an SBD/MBE product.

## Binary release contents

Use GitHub Releases for compiled downloads. A release ZIP should contain:

- The ARM64 Release build of `MBE-Easimap-ARM-Setup.exe`.
- `LICENSE` and the user-facing `README.txt`.
- The release version and a link to the exact corresponding source revision.

Publish the complete corresponding source, including project files and build
scripts, alongside each binary release. A source archive attached to the same
release makes the relationship explicit. Distributed modified versions must meet
the GPL requirements too; consult LICENSE for the complete terms.

Build each release from its tagged source. Record the compiler/SDK version,
executable SHA-256, build results and relevant device/UI validation in its
release notes. Keep matching PDB symbols for troubleshooting. Build output,
personal IDE state, credentials, device logs and recovery backups do not belong
in Git.