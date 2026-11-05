# Changelog

All notable changes to STGR EXT4 are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
intends to use [Semantic Versioning](https://semver.org/spec/v2.0.0.html) once
it publishes usable releases.

## Verification status

**No release in this file has been built, loaded, or tested on a real system.**
The repository was authored in a workspace without MSVC, the WDK, Rust, Git, or
a Windows VM. It now builds in GitHub Actions: CI run 8 on commit `55a1236`
completed green with hygiene, Debug and Release x64 driver builds, INF catalog
generation, native PE verification, a format/Clippy/test/link pass of the Rust
CLI, and the host build and execution of the portable EXT4 parser (71 checks,
0 failures). **No driver has been loaded, no volume mounted, no file read, and
no VM test executed**, so no artifact is installable or usable. Entries
describe source-level and build-level work only. Claims of driver loading,
mounting, data integrity, or game compatibility would be false and are
therefore not made.

## [Unreleased]

### Added

- Initial repository foundation:
  - MIT license, third-party provenance record, editor and line-ending policy.
  - Pinned Rust toolchain (1.83.0) and pinned WDK/SDK NuGet content
    (10.0.26100.6584).
- Feasibility report covering architecture, toolchain, EXT4 strategy,
  WinBtrfs/FastFAT lessons, CI limits, VM testing, signing, integrity risks,
  semantic gaps, and effort estimates.
- Milestone 1 driver source:
  - `DriverEntry` that creates a `FILE_DEVICE_DISK_FILE_SYSTEM` device object
    and registers it with `IoRegisterFileSystem`.
  - Unload that unregisters, dereferences, and deletes the file system device
    object.
  - Dispatch table that declines `IRP_MN_MOUNT_VOLUME` with
    `STATUS_UNRECOGNIZED_VOLUME` and `Information = 0`.
  - `IRP_MN_VERIFY_VOLUME` and all other IRPs declined without a false success.
  - Version resource, INF, WDK project, unsigned-package notice.
- Management CLI scaffold:
  - `version`, `help`, and `status`.
  - `scan`, `list`, `mount`, `unmount`, `configure`, and `diagnostics` exit
    with code 70 and print that they are not implemented.
  - Process-level tests assert that `mount` is not a fake success.
- Build and verification scripts:
  - `Build-Driver.ps1`, `Stage-DriverPackage.ps1`, `Invoke-Inf2Cat.ps1`,
    `Test-DriverImage.ps1`, `Test-Repository.ps1`.
- GitHub Actions workflow definitions for CI, driver build, management build,
  security, and release. All actions are pinned to commit SHAs. The workflows
  have **not** been executed.
- Documentation set: architecture, IFS integration, EXT4 format, journal plan,
  configuration, compatibility matrix, data integrity, gaming compatibility,
  testing, installation, signing, CI operation, troubleshooting, roadmap,
  contributing, and security policy.
- Milestone 2 engine foundation under `driver/ext4`, freestanding C with no
  kernel headers:
  - Little-endian decoders and overflow-checked multiply/add helpers.
  - Block-I/O callback boundary; the engine never dereferences a device.
  - Bounded 1024-byte superblock read with short-read and failure detection.
  - Superblock validation: magic, revision, block size, geometry, inode size,
    descriptor size, feature sets, state and error flags.
  - Feature policy that refuses required-but-missing and unknown incompatible
    bits, reporting which set and which bit was refused.
  - `metadata_csum` volumes are refused with `StgrStatusChecksumNotVerified`
    instead of trusted: CRC-32C is deliberately not implemented yet.
  - Host unit tests (71 checks) and `scripts/Test-Engine.ps1`, wired into a new
    `Engine host tests` CI job.

### Changed

- Nothing yet.

### Fixed

- Driver build failures exposed by the first CI runs:
  - 64-bit MSBuild so the x64 `InfVerif.dll` from the WDK package loads.
  - `rc.exe` resolved from the SDK NuGet package instead of the runner image.
  - INF adjusted to satisfy universal driver validation rules.
  - Driver image check accepts dumpbin's actual `subsystem (Native)` spelling;
    it still rejects every user-mode subsystem.
  - Driver builds pinned to the `windows-2022` image, because the pinned
    10.0.26100 WDK package ships 17.0 MSBuild task assemblies.
- Engine host test failures found by the first two engine job runs:
  - `%d` used with `long` arguments replaced by `%ld` (`C4477` under `/WX`).
  - The 1 KiB geometry case no longer leaves `INCOMPAT_64BIT` set, so it
    asserts the 32-byte descriptor size the test names.
  - Unused `decoded` variable removed from the feature-policy test (`C4101`).

### Security

- Repository hygiene check rejects private key material, committed binaries,
  and user-mode filesystem dependencies in code.
- No signing keys are present in the repository.
- Release artifacts are explicitly unsigned development builds.

### Known limitations

- The driver compiles and packages in CI (runs 7 and 8) but has never been
  loaded or unloaded.
- The EXT4 parser has only been run against synthetic images built in memory;
  no image created by `mke2fs` has been parsed.
- No EXT4 volume can be mounted.
- No file can be read or written.
- There is no volume device object, VPB, drive letter, cache integration, or
  journal support.
- The management CLI's `status` command only queries the service control
  manager; it does not contact a driver control interface.
- CI proves compilation and packaging only. It does not load the driver, mount
  a volume, or exercise any kernel path, and it does not sign anything.
- Test signing and Secure Boot constraints make the unsigned artifact
  unloadable on a normal Windows 11 system.

## [0.1.0] — unreleased

The version `0.1.0.0` is reserved for the Milestone 1 skeleton. It must not be
published as a usable release. When it is eventually built and verified, the
entry will record the exact CI run and VM evidence.

No comparison or release links are published until the repository has a public
upstream URL and a real tag. Placeholder links are deliberately omitted rather
than pointing at a domain that does not exist.
