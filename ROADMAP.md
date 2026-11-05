# Roadmap

This roadmap is deliberately conservative. A milestone is complete only when
its exit gate has been executed and the evidence is recorded. Source code that
has never been compiled does not satisfy a milestone.

Status legend: **done**, **in progress**, **not started**, **blocked**.

Current position: **Milestone 1 source built in CI, no driver load.** GitHub
Actions CI run 7 (`164c359`, 2026-11-05) completed green: hygiene, Debug and
Release x64 driver builds with INF catalog and native PE verification, and the
Rust CLI. The `Security` workflow passed on the same commit. Earlier runs 1–6
failed while the build was brought up and are superseded. No driver has been
loaded in a VM, no volume has been mounted, and no toolset version has been
transcribed from the run log yet.

---

## Milestone summary

| # | Milestone | Estimate | Status |
| --- | --- | --- | --- |
| 0 | Research, architecture, CI foundation | 0.5–1 month | **In progress** (CI green on run 7; toolset versions not yet transcribed) |
| 1 | Driver skeleton, registration, dispatch, package | 1–2 months | **In progress** (built and packaged in CI; never loaded or VM-tested) |
| 2 | Superblock, geometry, feature validation, parser tests | 1–2 months | Not started |
| 3 | Read-only mount: VDO/VPB, Mount Manager, drive letter | 2–4 months | Not started |
| 4 | Full read path: inodes, directories, extents, xattrs, cache | 3–6 months | Not started |
| 5 | Journal parsing and recovery | 4–8 months | Not started |
| 6 | Write support: allocation, transactions, rename, fsync | 8–16 months | Not started |
| 7 | Real-device hardening: hotplug, Verifier, VM matrix | 3–6 months | Not started |
| 8 | Gaming compatibility, performance, stress | 2–5 months | Not started |
| 9 | Installer, signing, security review, release | 2–4 months | Not started |

Estimates are planning ranges for one experienced Windows kernel/filesystem
engineer working full time with a test VM and Linux image tooling. They are not
promises. See [`docs/FEASIBILITY.md`](docs/FEASIBILITY.md) §11 for assumptions
and risks.

---

## Milestone 0 — Research, architecture, CI foundation

**Goal:** decide whether the project is feasible and establish a repository
that can build in the cloud without a local toolchain.

Deliverables:

- [x] Empty-repository inspection and constraint inventory.
- [x] Feasibility report with architecture, risks, and estimates.
- [x] License and third-party provenance review.
- [x] WDK/SDK version selection and NuGet pinning.
- [x] Repository hygiene script.
- [x] GitHub Actions workflow definitions.
- [x] First successful `ci.yml` run on GitHub — run 7 on `164c359`,
      all four jobs green.
- [ ] Recorded compiler and toolset versions from that run.

**Exit gate:** all five workflows parse, `ci.yml` completes green on a pushed
commit, and the log records the MSVC, WDK content, SDK content, and Rust
versions. Until then Milestone 0 is not closed.

**Remaining gap:** the five workflow files are registered and `ci.yml` is green,
so the parse and green-run parts of the gate are met. The versions the gate asks
for are printed by the `Record tool versions` and `Record resolved WDK tool
paths` steps, which succeeded in run 7. The pinned values (WDK/SDK content
`10.0.26100.6584`, Rust `1.83.0`) are already recorded in
[`docs/GITHUB-ACTIONS.md`](docs/GITHUB-ACTIONS.md). The moving MSVC version is
**not** recorded: anonymous users cannot download Actions logs, so a signed-in
account must open the run and transcribe it. Milestone 0 stays open until that
happens.

---

## Milestone 1 — Driver skeleton

**Goal:** a driver that compiles, registers as a file system, and unloads
without leaking or crashing, while refusing every volume.

Deliverables:

- [x] `DriverEntry`, FSDO creation, `IoRegisterFileSystem`.
- [x] Unload path that unregisters, dereferences, and deletes the FSDO.
- [x] Dispatch table with safe defaults.
- [x] `IRP_MN_MOUNT_VOLUME` declined with `STATUS_UNRECOGNIZED_VOLUME`,
      `Information = 0`.
- [x] INF, version resource, WDK project, packaging scripts.
- [x] Unsigned-package notice and catalog generation script.
- [x] Successful Debug and Release x64 builds in GitHub Actions — run 7,
      Debug and Release packages plus PDBs uploaded as artifacts.
- [x] `dumpbin` confirmation of x64 native images — `Verify native x64 PE
      image` passed in run 7. The check accepts dumpbin's actual
      `subsystem (Native)` spelling and still rejects a user-mode subsystem.
- [ ] Test-VM load/unload with Driver Verifier and no bugcheck.
- [ ] Confirmation that no volume is mounted and no mount success is reported.

**Exit gate:** CI produces artifacts, and a disposable VM loads and unloads the
driver cleanly with Driver Verifier enabled. A build alone is not enough.

**Known gap:** the mount handler holds `VolumeLock` shared while declining.
Unload takes it exclusively. That ordering is intentional. The unload path has
not been exercised, so lifetime correctness is unproven.

---

## Milestone 2 — EXT4 recognition and parser foundation

**Goal:** recognize a valid, supported EXT4 volume and reject everything else,
without mounting it.

Deliverables:

- [ ] Freestanding engine skeleton under `driver/ext4/` with no kernel headers.
- [ ] Block-I/O and allocation callback interfaces.
- [ ] Bounded superblock read at byte 1024.
- [ ] Little-endian decoders with overflow-checked arithmetic.
- [ ] Superblock validation: magic, revision, block size, geometry, UUID,
      label, feature sets, inode size, descriptor size, checksum seed/type,
      state and error flags.
- [ ] Feature policy engine implementing the matrix in
      [`docs/FILESYSTEM-COMPATIBILITY.md`](docs/FILESYSTEM-COMPATIBILITY.md).
- [ ] Approved-volume allowlist lookup (design in
      [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md)).
- [ ] Host-side unit tests and a corrupt-image corpus.
- [ ] Mount probe returns success **only** for an approved, supported volume
      (still without creating a VDO until Milestone 3).

**Exit gate:** the host test binary accepts known-good images, rejects every
unsupported feature combination and every corrupted image in the corpus, and
the kernel build passes `/W4 /WX` plus `/analyze` with a clean baseline.
Mount success must still not be reported until the VDO/VPB work lands.

---

## Milestone 3 — Read-only mount

**Goal:** mount an approved volume read-only and expose it through Windows.

Deliverables:

- [ ] VDO creation and `STGR_DEVICE_HEADER` volume extension.
- [ ] VPB creation, `Vpb->RealDevice`, `Vpb->DeviceObject`,
      `VPB_MOUNTED`, `Information = 1`.
- [ ] Mount Manager integration and persisted drive letter via the management
      service.
- [ ] `IRP_MN_VERIFY_VOLUME`, dismount, and volume removal.
- [ ] Root directory open and basic metadata.
- [ ] Refusal of unclean volumes and unsupported features at mount time.
- [ ] Surprise-removal handling with no bugcheck.

**Exit gate:** in a disposable VM, an approved image mounts read-only, appears
as a drive letter, and unmounts cleanly. A non-approved EXT4 image and a
non-EXT4 volume are both refused. Removal of the virtual disk does not
bugcheck.

---

## Milestone 4 — Full read path

**Goal:** read files and directories correctly, including metadata.

Deliverables:

- [ ] Inode decoding, including 64-bit fields, `extra_isize`, nanosecond
      timestamps, and `huge_file`.
- [ ] Extent tree traversal with depth, cycle, and bounds checks.
- [ ] Directory enumeration: linear and HTREE, file types, restart semantics.
- [ ] `IRP_MJ_DIRECTORY_CONTROL` with correct buffer-overflow behaviour.
- [ ] File information classes and basic/standard/name information.
- [ ] Cache Manager integration (`CcInitializeCacheMap`, `CcCopyRead`,
      `CcFlushCache`), lazy-read and fast-I/O paths.
- [ ] File locks, oplocks, cleanup/close, reference counting.
- [ ] Symlinks as reparse points; hard links; sparse file reads.
- [ ] Extended attributes read (in-inode and external blocks), with ADS
      requests refused explicitly.
- [ ] Security descriptor synthesis from POSIX mode/uid/gid.
- [ ] Byte-for-byte comparison against `debugfs`/`sha256sum` on Linux.

**Exit gate:** a corpus of real directory trees read through Windows matches
the Linux reference hashes, including large files, sparse files, symlinks,
hard links, deep trees, long names, and non-ASCII names. No cache coherency
failures under concurrent readers.

---

## Milestone 5 — Journal parsing and recovery

**Goal:** understand JBD2 state and replay a journal safely on a disposable
image.

Deliverables:

- [ ] JBD2 superblock, descriptor, data, commit, and revoke block parsing.
- [ ] Checksum verification (`metadata_csum` / `csum_v2` / `csum_v3`).
- [ ] Replay into a scratch copy first; verify with `e2fsck -fn`.
- [ ] Explicit policy for `INCOMPAT_RECOVER`, `needs_recovery`, and
      `orphan_present`.
- [ ] Refusal to write while replay is incomplete or ambiguous.
- [ ] Power-cut and torn-write injection tests.

**Exit gate:** replay of a generated unclean image produces a file system that
`e2fsck -fn` reports clean, on a disposable virtual disk, with the original
image preserved. No real device is used.

---

## Milestone 6 — Controlled write support

**Goal:** write to an approved, clean, supported volume without corrupting it.

Deliverables:

- [ ] Block and inode allocation with `flex_bg` locality.
- [ ] Extent insertion and splitting.
- [ ] Directory insertion and splitting, HTREE maintenance.
- [ ] Metadata checksum recomputation for every mutated structure.
- [ ] JBD2 transaction creation, commit, checkpoint, and revoke handling.
- [ ] Ordered write barriers through `IRP_MJ_FLUSH_BUFFERS` and device flush.
- [ ] Create, write, truncate, delete, rename, link, unlink, set information.
- [ ] Orphan list/file processing.
- [ ] Quota/project-quota refusal policy.
- [ ] Crash injection after every write phase, followed by `e2fsck -fn`.

**Exit gate:** thousands of randomized write/crash cycles on disposable images,
each followed by `e2fsck -fn`, with zero unexplained corruption. Driver
Verifier clean. No writes to unapproved volumes.

---

## Milestone 7 — Real-device hardening

**Goal:** survive real hardware behaviour and adversarial input.

Deliverables:

- [ ] Real SATA/USB/NVMe devices in a dedicated lab machine.
- [ ] Hotplug, surprise removal, and power-loss testing.
- [ ] Full Driver Verifier: Special Pool, Pool Tracking, I/O Verification,
      IRQL Checking, Deadlock Detection, Force IRQL Checking.
- [ ] Long-running soak tests with concurrent applications.
- [ ] Security review of every parser entry point.
- [ ] Fuzzing with structure-aware mutators and coverage guidance.
- [ ] Performance baseline and regression tracking.

**Exit gate:** no Verifier reports, no bugchecks, no data loss across the
documented test matrix. Results are published with logs, not summarized away.

---

## Milestone 8 — Gaming compatibility

**Goal:** behave correctly for game libraries and launchers.

Deliverables:

- [ ] Steam library discovery and content validation.
- [ ] Proton/Wine prefix behaviour on a Windows-hosted EXT4 volume.
- [ ] Case-collision and long-path handling validated with real titles.
- [ ] Anti-cheat compatibility assessment (documented per title, not assumed).
- [ ] Large-file and sparse-file workloads.
- [ ] Memory-mapped I/O and async I/O behaviour.
- [ ] Performance comparison against NTFS on the same hardware.

**Exit gate:** documented per-title results, including titles that do not work
and why. No claim of universal game compatibility.

---

## Milestone 9 — Installer, signing, release

**Goal:** make installation honest, reversible, and supportable.

Deliverables:

- [ ] Installer that verifies the package signature and refuses unsigned
      packages in production mode.
- [ ] Approval workflow for volumes, with clear warnings.
- [ ] Uninstall that removes the service and driver without touching data.
- [ ] Microsoft-trusted signing (attestation or WHQL as appropriate).
- [ ] Independent security review.
- [ ] Versioned release notes with known limitations.
- [ ] Rollback instructions.

**Exit gate:** a clean Windows 11 machine installs, mounts an approved
read-only volume, and uninstalls cleanly; the signature validates without test
signing; the release notes state exactly what is and is not supported.

---

## Cross-cutting work

These are not separate milestones; they must land with the feature that needs
them:

- **Documentation:** every behaviour change updates the relevant document.
- **Refusal policy:** new EXT4 features are rejected by default until proven.
- **Telemetry:** no silent telemetry; diagnostics are user-invoked.
- **Localization:** not planned before v1.0.
- **32-bit Windows:** not planned.
- **ARM64:** not planned before x64 is stable.

## Evidence policy

A milestone is not done because the code "looks right". It is done when:

1. the change is committed and reviewed;
2. CI builds it with warnings as errors;
3. the relevant host tests pass;
4. the relevant VM tests pass, with logs retained;
5. the result is recorded in [`CHANGELOG.md`](CHANGELOG.md).

If a step could not be run, the roadmap says so. It is never marked complete
by inference.
