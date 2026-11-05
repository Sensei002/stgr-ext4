# Third-party notices and license review

STGR EXT4 is licensed under the MIT License (see `LICENSE`). This file records
the upstream projects studied and the licenses that apply to them. It is a
record of provenance, not a claim that their code is included.

## Code included in this repository

None. The driver, scripts, documentation and Rust management tool are original
work by the STGR EXT4 contributors, except for standard build-system boilerplate
whose form is dictated by MSBuild, NuGet, the WDK and Cargo.

## Projects studied (no source copied)

| Project | License | What was used |
| --- | --- | --- |
| [WinBtrfs](https://github.com/maharmstone/btrfs) | LGPL-3.0 | Architecture, Windows integration strategy, mount-manager and drive-letter behaviour, INF/package conventions. No source code was copied. |
| [Microsoft Windows-driver-samples](https://github.com/microsoft/Windows-driver-samples) (FastFAT, CDFS) | MIT | IFS driver structure, dispatch patterns, driver entry/unload, mount protocol, WDK/MSBuild conventions. The MIT license permits reuse; the files in this repository are nevertheless written independently. |
| [Linux kernel EXT4 on-disk documentation](https://www.kernel.org/doc/html/latest/filesystems/ext4/) | GPL-2.0 (kernel documentation) | The on-disk format specification and feature flags. Facts and interfaces are used; no kernel source code or documentation text is copied. |
| [Windows Driver Kit documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/) | Microsoft terms | Documented kernel APIs and IFS requirements. |

## Build-time dependencies (not redistributed)

| Dependency | Version | License / terms |
| --- | --- | --- |
| `Microsoft.Windows.WDK.x64` NuGet package | 10.0.26100.6584 | Microsoft package license (accepted by NuGet restore). Not committed to this repository. |
| `Microsoft.Windows.SDK.CPP` / `.CPP.x64` NuGet packages | 10.0.26100.6584 | Microsoft package license. Not committed. |
| Visual Studio 2022 Build Tools / MSVC / MSBuild | runner image version | Microsoft license, provided by the GitHub-hosted runner. |
| Rust toolchain | pinned by `rust-toolchain.toml` (1.83.0) | MIT/Apache-2.0. |
| GitHub Actions | pinned by commit SHA | GitHub terms. |

## Deliberate licensing decisions

1. **No Linux kernel code.** The EXT4 engine is implemented from the public
   on-disk specification. Linux `fs/ext4` and `fs/jbd2` are GPL-2.0; copying
   them would force this driver to GPL-2.0 and create a mixed-license kernel
   binary. That is avoidable and is therefore avoided.
2. **No WinBtrfs code.** WinBtrfs is LGPL-3.0. It is an architectural reference
   only. Any future code contribution derived from it must be discussed and the
   project license revisited before merging.
3. **MIT for original code.** MIT is compatible with the WDK headers, the
   Microsoft sample license, and both proprietary and GPL consumers. It keeps
   the management tool usable by third parties without a copyleft obligation.
4. **Revisit before v1.0.** If a compelling reason to adopt Apache-2.0 (patent
   grant) or a copyleft license appears, the change must be made before the
   first public release and recorded in `CHANGELOG.md`.

## Trademarks

Windows, Win32, NTFS and Visual Studio are trademarks of Microsoft Corporation.
Linux and EXT4 refer to the Linux kernel filesystem. STGR EXT4 is not affiliated
with, endorsed by, or sponsored by Microsoft or the Linux kernel community.
