# STGR EXT4

A native Windows kernel-mode EXT4 file system driver, written in C against the
Windows Driver Kit (WDK) and the Installable File System (IFS) contract.

STGR EXT4 is intended to let Windows 11 x64 read, and later carefully write, an
EXT4 volume directly through the Windows I/O manager. There is no WinFsp,
Dokany, FUSE, user-mode proxy, Linux virtual machine, or file-copy layer in the
data path. The long-term goal is a real file system driver with correct caching,
locking, paging I/O, crash consistency, and drive-letter integration.

> **Project status: Milestone 1 source, not yet built or tested.**
>
> The driver in this repository registers itself as a file system and safely
> declines every volume. It does not parse EXT4, does not mount anything, and
> has no read or write support. The driver source has been authored but **no
> driver compilation, driver load, or CI run has been performed in this
> workspace**. The Rust workspace is `cargo fmt`- and Clippy-clean (Rust
> 1.83.0) but has not been linked or executed; no linker is installed here.
> Nothing here is installable on a normal Windows 11 machine. See
> [`ROADMAP.md`](ROADMAP.md) and [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md).

---

## Why this exists

Linux EXT4 volumes are common in dual-boot systems, NAS drives, external disks,
and game libraries. Windows has no in-box EXT4 driver. Existing options are
usually user-mode file systems or ext2/3-era tools with limited feature
support.

STGR EXT4 takes the slow, testable path:

- a real WDM file system driver registered with `IoRegisterFileSystem`;
- a portable, freestanding EXT4 engine that can be fuzzed on the host;
- a staged feature policy that refuses anything it cannot handle correctly;
- an explicit approved-volume allowlist so no random disk is ever mounted;
- read-only support first, write support only after journal replay, checksum,
  and crash-consistency work is complete.

## Safety rules

These are project invariants, not aspirations:

1. **Never mount an arbitrary EXT4 volume.** Only volumes explicitly approved
   by UUID in the driver configuration may be mounted.
2. **Never format, repair, resize, or modify a partition table or bootloader.**
3. **Never silently write to a file system that is dirty or needs journal
   replay.** Refuse the mount instead.
4. **Never claim success that did not happen.** No fake mounts, no stubbed
   operations returning success, no invented test results.
5. **Never treat an unsigned development build as production-signed.**
6. **The user's real disks are never automated test targets.** Destructive
   testing happens on disposable virtual disks only.

Read [`docs/DATA-INTEGRITY.md`](docs/DATA-INTEGRITY.md) before pointing any
build at a disk that contains data.

## Current state (Milestone 1)

| Area | State |
| --- | --- |
| Feasibility and architecture | Written: [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) |
| Driver entry, FSDO, registration, unload | Implemented in source |
| Mount probe handling | Implemented: declines with `STATUS_UNRECOGNIZED_VOLUME` |
| EXT4 superblock parsing | Not implemented |
| Volume device objects / VPB / drive letters | Not implemented |
| File reads and writes | Not implemented |
| Journal replay | Not implemented |
| Management CLI | `version`, `help`, `status` only; other commands exit 70 |
| Compiler verification | **Not performed** for the driver; Rust workspace is fmt/Clippy-clean only |
| GitHub Actions verification | **Not performed** (workflows authored, never run) |
| Kernel/VM testing | **Not performed**; no test VM exists |

The management CLI reports `filesystem mounting: not implemented` and never
prints a fake success. The driver has no code path that completes a mount IRP
with success.

## Architecture at a glance

```text
Win32 / Explorer / Steam / games
              |
              v
      Windows I/O manager
              |
              v
   STGRExt4.sys  (WDM, IFS file system)
      |            \
      |             +-- Windows front end: FSDO, VDO, VPB, IRP dispatch,
      |                 FCB/CCB/SCB, cache manager, locks, security
      |
      +-- portable EXT4 engine (future): superblock, group descriptors,
          inodes, extents, directories, xattrs, JBD2 journal
              |
              v
      Volume device -> disk driver -> storage
```

The engine is designed to compile in two environments: kernel mode with
callback-based block I/O, and user mode with a file-backed device for tests.
That split lets corrupt-image and fuzz testing run without a kernel.

Details: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/WINDOWS-IFS.md`](docs/WINDOWS-IFS.md).

## Requirements

### To build (CI or a Windows development machine)

- Windows 10/11 x64 with Visual Studio 2022 and the WDK extension, or a
  `windows-2022` GitHub-hosted runner for the driver build.
- Visual Studio 2022 with the **Desktop development with C++** workload and the
  **Windows Driver Kit Visual Studio extension**.
- WDK and Windows SDK content, pinned through NuGet:
  `Microsoft.Windows.WDK.x64` and `Microsoft.Windows.SDK.CPP*` version
  `10.0.26100.6584` (see [`packages.config`](packages.config)).
- `nuget.exe` on `PATH`.
- MSBuild on `PATH` (`microsoft/setup-msbuild` in CI).
- Rust 1.83.0 for the management tool (pinned by
  [`rust-toolchain.toml`](rust-toolchain.toml)). No external crates.

The project is designed so that contributors do not need a local kernel
toolchain: GitHub Actions performs the supported build. Local builds are
optional. The workflows have not yet been executed.

### To install and load (not possible with the current artifact)

- A disposable Windows 11 x64 virtual machine.
- Test signing enabled **and** Secure Boot disabled, or a production-signed
  package. CI produces unsigned artifacts.
- A virtual disk containing a throwaway EXT4 file system.

Never install a development build on a machine with data you care about.

## Building

### GitHub Actions (supported path)

The workflows are in [`.github/workflows/`](.github/workflows/). Once the
repository is pushed to GitHub:

1. `ci.yml` runs on pushes and pull requests.
2. `driver-build.yml` builds Debug and Release x64 packages.
3. `management-build.yml` runs `cargo fmt --check`, Clippy with
   `-D warnings`, and the test suite.
4. `security.yml` checks dependencies and repository hygiene.
5. `release.yml` creates an **unsigned** pre-release for a `v*.*.*` tag.

See [`docs/GITHUB-ACTIONS.md`](docs/GITHUB-ACTIONS.md) for the exact steps,
pinned action revisions, and limitations.

### Local Windows build

```powershell
nuget restore .\packages.config -PackagesDirectory .\packages\
.\scripts\Build-Driver.ps1 -Configuration Release -Platform x64
.\scripts\Invoke-Inf2Cat.ps1 -PackageDir .\artifacts\package\x64\Release
.\scripts\Test-DriverImage.ps1 -DriverPath .\artifacts\package\x64\Release\STGRExt4.sys
```

`Build-Driver.ps1` fails with a clear message if the pinned WDK packages,
MSBuild, or the project are missing. It does not download packages, sign
anything, or install anything.

### Repository hygiene

```powershell
.\scripts\Test-Repository.ps1
```

This checks required files, SPDX identifiers, forbidden user-mode file system
dependencies, private key material, committed binaries, and version
consistency. It needs no compiler.

### Management CLI

```powershell
cd management
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
cargo build --release
```

## Repository layout

```text
.
├── .github/workflows/     CI, driver, management, security, release
├── driver/
│   ├── include/           shared kernel definitions
│   ├── src/               DriverEntry, dispatch
│   ├── STGRExt4.inf       driver package definition
│   └── STGRExt4.vcxproj   WDK/MSBuild project
├── management/            Rust workspace and management CLI
├── scripts/               build, packaging, validation scripts
├── docs/                  architecture, format, safety, operations
├── packages.config        pinned WDK/SDK NuGet content
├── Directory.Build.props  WDK/SDK import and guard
├── ROADMAP.md             milestones and exit gates
├── CHANGELOG.md           honest change history
└── LICENSE                MIT
```

## Documentation

| Document | Contents |
| --- | --- |
| [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) | Technical feasibility, estimates, go/no-go |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Component and layering design |
| [`docs/WINDOWS-IFS.md`](docs/WINDOWS-IFS.md) | IFS/Windows kernel integration |
| [`docs/EXT4-FORMAT.md`](docs/EXT4-FORMAT.md) | On-disk format and parser plan |
| [`docs/JOURNAL-IMPLEMENTATION.md`](docs/JOURNAL-IMPLEMENTATION.md) | JBD2 replay and write ordering |
| [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) | Approved-volume allowlist design |
| [`docs/FILESYSTEM-COMPATIBILITY.md`](docs/FILESYSTEM-COMPATIBILITY.md) | Supported/rejected EXT4 features |
| [`docs/DATA-INTEGRITY.md`](docs/DATA-INTEGRITY.md) | Integrity invariants and failure policy |
| [`docs/GAMING-COMPATIBILITY.md`](docs/GAMING-COMPATIBILITY.md) | Game libraries, Steam, Proton, anti-cheat |
| [`docs/DRIVER-INSTALLATION.md`](docs/DRIVER-INSTALLATION.md) | Test-VM installation and removal |
| [`docs/DRIVER-SIGNING.md`](docs/DRIVER-SIGNING.md) | Signing reality and options |
| [`docs/GITHUB-ACTIONS.md`](docs/GITHUB-ACTIONS.md) | CI design and operation |
| [`docs/TESTING.md`](docs/TESTING.md) | Test ladder and evidence policy |
| [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) | Failure diagnosis |

## License

Original code is licensed under the MIT License. See [`LICENSE`](LICENSE) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

No Linux kernel code is copied. The EXT4 engine is implemented from the public
on-disk specification. WinBtrfs is an architectural reference only and is
LGPL-3.0; no WinBtrfs code is included.

Windows, Win32, NTFS, and Visual Studio are trademarks of Microsoft Corporation.
Linux and EXT4 refer to the Linux kernel file system. STGR EXT4 is not
affiliated with, endorsed by, or sponsored by Microsoft or the Linux kernel
community.
