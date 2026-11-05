# Changelog

All notable changes to STGR EXT4 are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
intends to use [Semantic Versioning](https://semver.org/spec/v2.0.0.html) once
it publishes usable releases.

## Verification status

**No release in this file has been built, loaded, or tested.** The repository
was authored in a workspace without MSVC, the WDK, Rust, Git, or a Windows VM.
Two static checks have since been run locally: `scripts/Test-Repository.ps1`
passed, and the Rust workspace was clean under `cargo fmt --check` and
`cargo clippy -D warnings` on toolchain 1.83.0. No Rust binary was linked and no
Rust test was executed. Entries describe source-level work only. Claims of
compilation, CI success, driver loading, mounting, or data integrity would be
false and are therefore not made.

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

### Changed

- Nothing yet.

### Fixed

- Nothing yet.

### Security

- Repository hygiene check rejects private key material, committed binaries,
  and user-mode filesystem dependencies in code.
- No signing keys are present in the repository.
- Release artifacts are explicitly unsigned development builds.

### Known limitations

- The driver has never been compiled.
- The driver has never been loaded.
- No EXT4 volume can be mounted.
- No file can be read or written.
- There is no volume device object, VPB, drive letter, cache integration, or
  journal support.
- The management CLI's `status` command only queries the service control
  manager; it does not contact a driver control interface.
- The GitHub Actions definitions are unverified; YAML validity does not imply
  a successful build.
- Test signing and Secure Boot constraints make the unsigned artifact
  unloadable on a normal Windows 11 system.

## [0.1.0] — unreleased

The version `0.1.0.0` is reserved for the Milestone 1 skeleton. It must not be
published as a usable release. When it is eventually built and verified, the
entry will record the exact CI run and VM evidence.

No comparison or release links are published until the repository has a public
upstream URL and a real tag. Placeholder links are deliberately omitted rather
than pointing at a domain that does not exist.
