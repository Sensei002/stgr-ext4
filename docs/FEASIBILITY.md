# STGR EXT4 — Technical feasibility report

Status: **Milestone 0 deliverable**
Date: 2026-11-05
Scope: native Windows kernel-mode EXT4 filesystem driver, Windows 11 x64

---

## 0. Repository inspection (first task)

The workspace supplied for this task (`C:\Users\Admin\Desktop\project\stgr-ext4`)
was inspected before any file was created.

| Question | Finding |
| --- | --- |
| Is the repository empty? | Yes. Zero files, zero directories. |
| Is it a Git repository? | No `.git` directory. `git` is not installed on this machine. |
| Existing workflows? | None. |
| Existing license? | None. |
| Existing source structure? | None. |
| Existing dependencies? | None. |
| Earlier WinFsp/Dokany/FUSE implementation to replace? | None. |
| Can the current repository support the native WDK architecture? | Yes, because it is empty; there is no legacy userspace design to unwind. |
| Is a GitHub remote configured? | No. No `gh` CLI, no credentials, no remote URL. |
| Local compiler toolchain? | None initially. No MSVC, no WDK, no CMake, no Git. A temporary Rust 1.83.0 toolchain was later installed under the temp directory (not on `PATH`) solely for `cargo fmt`/`cargo clippy`; it has no linker, so nothing was linked. |

**Consequence:** this environment can author the repository and its CI
definitions, but it cannot compile the driver, load it, or push to GitHub.
Nothing in this report claims a successful compilation or a green workflow.
The first real verification happens when the tree is pushed to GitHub Actions
(see §7) and, later, when a driver is loaded in a disposable VM (see §8).

This is a **feasibility report plus Milestone 0/1 code**, not a working
filesystem. At the end of Milestone 1 the driver registers itself as a file
system and safely refuses every volume. It does not mount anything.

---

## 1. Native Windows filesystem driver architecture

### 1.1 Position in the storage stack

```text
Win32 / Explorer / Steam / games
        |
        v
I/O Manager  (IRP_MJ_CREATE, READ, WRITE, DIRECTORY_CONTROL, ...)
        |
        v
Cache Manager / Memory Manager  (Cc*, Mm*, paging I/O)
        |
        v
STGRExt4.sys  (IFS file system driver, WDM)
        |
        |  IRP_MJ_READ / IRP_MJ_WRITE to the volume device
        v
Volume device  ->  disk class driver  ->  port driver  ->  HDD/SSD
```

The driver is a **WDM file system driver**, not a filter driver, not a
minifilter, not KMDF, and not a user-mode filesystem. It registers with
`IoRegisterFileSystem`, receives `IRP_MN_MOUNT_VOLUME` from the I/O manager,
creates volume device objects (VDOs), and serves file IRPs from those VDOs.
There is no WinFsp, Dokany, FUSE, proxy process, or Linux VM in the data path.

### 1.2 Layering

The implementation is split into two hard layers:

1. **Windows front end** (`driver/src`, `driver/include`)
   - `DriverEntry`, unload, filesystem registration.
   - Device objects, VPB, volume lifecycle.
   - IRP dispatch, IRQL, cancellation, reference counting.
   - FCB/CCB/SCB, file locks, oplocks, security, cache manager callbacks.
   - Mount Manager interaction is done by the user-mode service, not by the
     filesystem driver itself.

2. **Portable EXT4 engine** (`driver/ext4` from Milestone 2 onward)
   - Pure C, freestanding: no kernel headers, no libc, no floating point.
   - All disk access through a small `stgr_block_io` callback table.
   - All allocation through a callback table.
   - All on-disk fields decoded with explicit little-endian helpers and
     bounds checks.
   - The same engine is compiled into a user-mode parser/unit-test harness,
     so corrupted-image tests do not require a kernel or a VM.

This split is the single most important design decision for safety and
testability. A parser bug can be reproduced on the host with a disk image,
ASan/UBSan and a deterministic block device before it ever sees kernel memory.

### 1.3 Planned mount flow (Milestone 3+)

1. Driver loads as a `SERVICE_FILE_SYSTEM_DRIVER`, calls
   `IoCreateDevice(FILE_DEVICE_DISK_FILE_SYSTEM)` and
   `IoRegisterFileSystem`.
2. The I/O manager probes the driver with `IRP_MN_MOUNT_VOLUME` for a volume.
3. Driver reads the 1024-byte superblock through the volume device with a
   synchronous `IRP_MJ_READ` built by the driver.
4. Driver validates magic, geometry, feature flags, checksum and state.
5. Driver checks the **approved-volume allowlist** (EXT4 UUID) loaded from its
   service registry key. Unknown EXT4 volumes are refused with
   `STATUS_UNRECOGNIZED_VOLUME`; they are never mounted automatically.
6. Driver creates a VDO, creates a VPB, links `Vpb->RealDevice`,
   `Vpb->DeviceObject`, sets `VPB_MOUNTED`, and completes the mount IRP with
   `Information = 1`.
7. The Mount Manager and the STGR service assign the persisted drive letter.

At Milestone 1 steps 3–7 are absent. The mount handler returns
`STATUS_UNRECOGNIZED_VOLUME` with `Information = 0`, which is the correct,
safe way to decline a volume. This is not a stub that pretends to succeed.

### 1.4 Why not a user-mode filesystem

The requirements are explicit and the architecture agrees: a user-mode
filesystem cannot provide correct Windows cache coherency, paging I/O,
oplocks, byte-range locks, memory-mapped files, or crash-consistent flush
semantics at the level a game library needs. WinFsp/Dokany would also add a
second failure domain and an IPC round trip per operation. They are rejected
for this project.

---

## 2. WDK and Windows SDK versions

### 2.1 Selected combination

| Component | Version | Source |
| --- | --- | --- |
| Runner (driver) | `windows-2022` | GitHub-hosted |
| Runner (Rust, hygiene) | `windows-2025` | GitHub-hosted |
| Visual Studio (driver) | 2022 (17.x) | `windows-2022` image |
| Platform toolset | `WindowsKernelModeDriver10.0` | WDK VS extension in the runner image (10.0.26100.16) |
| WDK content | `Microsoft.Windows.WDK.x64` **10.0.26100.6584** | NuGet |
| Windows SDK content | `Microsoft.Windows.SDK.CPP` / `.CPP.x64` **10.0.26100.6584** | NuGet |
| Target platform version | `10.0.26100.0` | SDK package props |
| Target OS | Windows 11 x64 (`TargetVersion=Windows10`) | Project |
| C language mode | MSVC default C mode, C89-compatible source | Project |

The hosted `windows-2022` image contains the **WDK Visual Studio extension**
(the toolset integration) but not the standalone WDK content. Restoring the
content from NuGet is Microsoft's documented CI path (and the path used by
Microsoft's own `Windows-driver-samples` CI). The driver project therefore
imports the NuGet props from `Directory.Build.props`.

Driver builds deliberately stay on `windows-2022`. The pinned 10.0.26100 WDK
NuGet package ships MSBuild task assemblies named
`Microsoft.DriverKit.Build.Tasks.17.0.dll`; the `windows-2025` label now
resolves to Visual Studio 2026 (18.x), which looks for an `18.0` assembly and
fails with `MSB4062`. Rust and repository-hygiene jobs do not load WDK tasks and
run on `windows-2025`. Moving the driver to `windows-2025` requires upgrading
the WDK/SDK pin to a release that ships 18.0 task assemblies, then re-verifying
the build.

### 2.2 Reproducibility gaps

- The `windows-2022` and `windows-2025` runner labels are moving images. The
  WDK/SDK content is pinned; **MSVC itself is not**. The CI prints `cl.exe` and
  `link.exe` versions into the build log. For bit-reproducible releases, use a
  pinned container image or a self-hosted image and record its digest.
- NuGet restore is not locked with a hash. The versions are exact, but the
  package source is nuget.org. A future improvement is to vendor the nupkg
  checksums in the repository and verify them after restore.
- The Rust toolchain is pinned to 1.83.0 by `rust-toolchain.toml`.

### 2.3 Why not KMDF

KMDF is a driver framework for PnP device drivers. A file system driver must
implement the IFS contract directly (VPB, FCB, cache callbacks, mount IRPs).
FastFAT and CDFS are WDM drivers and are the correct references. Using KMDF
would add abstraction without removing any of the IFS obligations.

---

## 3. C toolchain requirements

| Requirement | Decision |
| --- | --- |
| Compiler | MSVC 19.4x from VS 2022 (`cl.exe`), x64 |
| Language | C (`.c`), compiled as C, not C++ |
| Warning level | `/W4` and `/WX` (warnings are errors) |
| Static analysis | MSVC `/analyze` (PREfast) plus WDK Code Analysis rules; advisory in Milestone 1, blocking from Milestone 2 once the baseline is clean |
| Runtime | Kernel CRT subset supplied by WDK (`ntoskrnl.lib`, `BufferOverflowFastFailK.lib`), no user-mode CRT |
| Exceptions | Disabled (`ExceptionHandling` empty) |
| Security | `/GS`, `/DYNAMICBASE` disabled by WDK for kernel images, DEP settings provided by WDK |
| Packing/alignment | On-disk structures are never `#pragma pack`ed and never cast directly from untrusted buffers; a decoder copies fields with explicit little-endian reads |
| Sanitizers | Kernel Address Sanitizer (KASAN) is not generally available for production Windows drivers; user-mode engine tests use ASan/UBSan on Linux and MSVC `/fsanitize=address` where practical |
| Verification | Driver Verifier, Special Pool, Pool Tracking, I/O Verification, IRQL Checking, Deadlock Detection in a disposable VM |

The kernel code will avoid compiler-specific tricks, VLAs, recursion over
untrusted depth, floating point, and unbounded allocations.

---

## 4. EXT4 implementation strategy

Reference: the kernel.org EXT4 on-disk specification. No Linux kernel source
is copied (GPL-2.0); the engine is written from the format specification.

### 4.1 Parsing order

1. **Superblock** at byte 1024 (block 0 offset 1024, or block 1 for 1 KiB
   filesystems): magic `0xEF53`, revision, block size, geometry, UUID, label,
   feature sets, inode size, descriptor size, checksum type/seed, state flags.
2. **Block group descriptors**: 32-byte legacy or 64-byte `64bit` descriptors,
   with `meta_bg`/`flex_bg` placement rules and `metadata_csum` tails.
3. **Bitmaps**: block and inode allocation bitmaps, including uninitialised
   groups (`EXT4_BG_*_UNINIT`), with checksum verification.
4. **Inodes**: 128-byte legacy or `s_inode_size` (typically 256) with
   `extra_isize` bounds, nanosecond timestamps, `i_blocks` semantics
   (`huge_file`), inline data detection.
5. **Extent trees**: depth ≤ 5, `ee_len` unwritten bit, 48-bit physical
   blocks, checksum tail, cycle and bounds detection.
6. **Directories**: `ext4_dir_entry_2` records, file type, inode bounds,
   htree (`dir_index`) root/interior/leaf traversal, `dx_tail` checksums,
   `large_dir` depth.
7. **Extended attributes**: in-inode and external block xattrs, name/value
   bounds, `ea_inode` detection.
8. **Journal**: JBD2 superblock, descriptor/data/commit/revoke blocks,
   checksums, replay (Milestone 5).
9. **Allocation and write path** (Milestone 6): block/inode bitmaps, flex_bg
   locality, extent insertion/split, directory insertion/split, journal
   transaction, ordered write barriers.

### 4.2 Validation rules (non-negotiable)

- Every offset is checked against the volume size before an IRP is built.
- Every block number is checked against `s_blocks_count` before use.
- Every inode number is checked against `s_inodes_count`.
- Every directory record length is checked for alignment, minimum size,
  inode range, name length, and remaining-block space.
- Every extent is checked for overflow, overlap with metadata, and depth.
- Every multiplication/addition is checked for overflow.
- Every checksum is verified before the structure is trusted.
- Unknown incompatible features cause mount refusal, never "best effort".
- A checksum mismatch is corruption, not a warning. It causes I/O errors, not
  silent data exposure.

### 4.3 Testability

The engine is compiled twice:

- into `STGRExt4.sys` with kernel callbacks;
- into a host test binary with a file-backed block device and a fault-injecting
  block device (short reads, EIO, torn writes, reordered writes, lost flushes).

Disk images are created in a Linux CI job with `mke2fs`/`debugfs`/`e2fsck`
from a pinned e2fsprogs version, then checked into an artifact (not the repo).
The parser must reject every corrupted image in the corpus without crashing.

---

## 5. WinBtrfs architectural lessons

WinBtrfs (LGPL-3.0; studied, not copied) is the closest successful precedent:
a native Windows kernel filesystem driver for a Linux filesystem, used in
production by thousands of people.

Lessons that apply directly:

1. **The driver must own the whole filesystem stack.** WinBtrfs does not
   proxy through user mode. Its user-mode tools configure and diagnose; the
   kernel driver does I/O. This is the only way to get cache coherency and
   crash behaviour right.
2. **Mounting is the hard Windows-specific part, not the on-disk format.**
   Volume recognition, VPB lifecycle, Mount Manager names, drive letters,
   dismount and surprise removal consume a large fraction of the effort.
3. **A clean separation between the on-disk engine and the Windows front end
   pays for itself.** It allows offline image testing, which is the only way
   to build confidence before loading code in kernel mode.
4. **Feature refusal is a feature.** WinBtrfs reports unsupported
   combinations instead of writing optimistically. STGR EXT4 must be stricter:
   an EXT4 image that Windows writes incorrectly can destroy a Linux install.
5. **A management service is necessary but must stay thin.** Drive-letter
   persistence, configuration, and diagnostics belong in user mode; the
   filesystem engine does not.
6. **Packaging and signing are first-class engineering work.** A driver that
   cannot be installed on a default Windows 11 machine is not usable, no
   matter how correct the parser is.
7. **Long-term maintenance is the real cost.** A filesystem driver is a
   security boundary and must be re-validated against every Windows and
   e2fsprogs change.

WinBtrfs took years of sustained work. STGR EXT4 should plan for the same
order of magnitude, not a weekend.

---

## 6. FastFAT reference areas

FastFAT is MIT-licensed and is the canonical minimal IFS filesystem driver.
The following areas are the primary references:

| Area | FastFAT file(s) | What STGR EXT4 takes |
| --- | --- | --- |
| Driver entry, FSDO creation, registration | `fatinit.c` | `IoCreateDevice(FILE_DEVICE_DISK_FILE_SYSTEM)`, `IoRegisterFileSystem`, `ObReferenceObject`, unload discipline |
| Mount/verify | `fsctrl.c`, `fatinit.c` | `IRP_MN_MOUNT_VOLUME`, `IRP_MN_VERIFY_VOLUME`, `IoCreateVpb`, `VPB_MOUNTED`, `Information = 1` |
| IRP dispatch and IRP contexts | `fspdisp.c`, `strucsup.c` | dispatch table, IRP context allocation, top-level IRP handling, pending returns |
| FCB/CCB/SCB lifecycle | `filobsup.c`, `close.c`, `cleanup.c` | file object mapping, share access, cleanup/close, reference counting |
| Directory control | `dirctrl.c`, `dirsup.c` | `IRP_MJ_DIRECTORY_CONTROL`, `FILE_DIRECTORY_INFORMATION`, enumeration restart, buffer overflow semantics |
| File information | `fileinfo.c` | `FILE_BASIC_INFORMATION`, `FILE_STANDARD_INFORMATION`, rename/link/delete information classes |
| Caching and fast I/O | `cachesup.c`, `read.c`, `write.c` | `CcInitializeCacheMap`, `CcCopyRead/Write`, `CcFlushCache`, lazy-write callbacks, fast I/O |
| Locks and oplocks | `lockctrl.c` | `FsRtlProcessFileLock`, oplock break handling, byte-range locks |
| Flush/shutdown | `flush.c`, `shutdown.c` | `IRP_MJ_FLUSH_BUFFERS`, `IoRegisterShutdownNotification` |
| Security | `acchksup.c` | security descriptor synthesis and access checks |
| PnP/surprise removal | `pnp.c` | volume removal, dismount, outstanding IRP handling |

FastFAT is FAT-specific in its on-disk logic. None of its FAT structures are
reused. The IFS control-flow patterns and documented WDK APIs are the value.

---

## 7. GitHub Actions implementation

### 7.1 What hosted CI can do

- Restore the pinned WDK/SDK NuGet packages.
- Build the driver Debug and Release for x64 with MSVC `/W4 /WX`.
- Build, format-check, Clippy and test the Rust management tool.
- Run repository hygiene checks (license headers, no private keys, no
  forbidden user-mode filesystem dependencies).
- Generate the INF catalog with `Inf2Cat` and validate the INF.
- Inspect the PE headers to confirm it is a native kernel image.
- Upload unsigned development artifacts and symbols.
- Publish tagged, **unsigned** pre-releases with SHA-256 sums.

### 7.2 What hosted CI cannot do

- Load the driver. GitHub-hosted Windows runners are ephemeral and are not a
  suitable or permitted place to install an experimental kernel driver,
  enable test signing, reboot, and run Driver Verifier.
- Run a Windows VM with nested virtualization. Hosted runners do not provide
  a stable nested Hyper-V environment.
- Sign with a Microsoft-trusted production certificate.

Therefore the workflows **must not** report kernel integration tests as
passed. They report compile/package results only.

### 7.3 Required repository secrets and settings

| Setting | Purpose | Required |
| --- | --- | --- |
| `GITHUB_TOKEN` | artifact upload, release creation | automatic |
| `vars.STGR_TEST_SIGNING` | opt-in for a future self-hosted test-signing job | no |
| Production signing certificate | Microsoft Trusted Signing / EV certificate + attestation | not configured |

No signing keys are in the repository. The release workflow marks every
artifact as unsigned and pre-release.

### 7.4 Workflow map

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| `ci.yml` | push to `main`, PR, manual | calls driver + management builds, repository hygiene |
| `driver-build.yml` | reusable, manual | Debug/Release x64 driver, INF catalog, unsigned package, symbols |
| `management-build.yml` | reusable, manual | Rust fmt, Clippy, tests, release binary |
| `security.yml` | push, PR, weekly, manual | dependency review, Rust audit, private-key scan, hygiene |
| `release.yml` | tag `v*.*.*` | downloads verified artifacts, creates unsigned pre-release + SHA-256 |

---

## 8. Windows VM testing strategy

Kernel integration testing is separate from CI and is mandatory before any
real-device write support.

### 8.1 Test topology

```text
Host (dedicated test machine, never the user's gaming PC)
  Hyper-V / VMware
    Guest: Windows 11 x64, disposable checkpoint
      - test signing enabled (bcdedit /set testsigning on)
      - Driver Verifier enabled for STGRExt4.sys
      - kernel debugging over named pipe / network
      - system disk + a second virtual disk
    Second disk:
      - plain disk image with one EXT4 partition
      - snapshots before every destructive test
```

The test VM never sees the physical disks of the developer's machine. The
user's gaming HDD is never a test target.

### 8.2 Test ladder

1. Driver installs, loads, registers, unloads cleanly.
2. Mount refusal on non-EXT4 volumes (no false positives).
3. Read-only mount of a known-good image; directory enumeration and file
   reads match `debugfs`/`sha256sum` on Linux.
4. Corrupted images: every mutation is rejected without a bugcheck.
5. Unclean images: read-only mount refuses; write mount refuses until journal
   replay is implemented.
6. Journal replay on a disposable image, verified by `e2fsck -fn`.
7. Write operations, power-cut injection, `e2fsck -fn` after every run.
8. Surprise removal, dismount, shutdown, and Driver Verifier with no reports.
9. Performance and game workloads (Milestone 8).

### 8.3 Automation

- A self-hosted GitHub runner labelled `windows-kernel-test` in a dedicated
  lab network can execute the VM tests on demand.
- Test results are JUnit XML + ETW/WPP logs + crash dumps.
- A failed Driver Verifier run blocks the milestone.
- The VM is reverted to a clean checkpoint after every run.

Until that runner exists, no VM test result may be claimed.

---

## 9. Driver signing limitations

| Mode | What it is | Limitations |
| --- | --- | --- |
| Unsigned build | What CI produces today | Windows 11 x64 refuses to load it in normal mode. Usable only with test signing + a test certificate installed. |
| Test-signed | Self-signed certificate, `bcdedit /set testsigning on` | Desktop watermark, Secure Boot must be off or test signing configured, not for end users, not for production. |
| Attestation signing | Microsoft Partner Center, EV certificate, driver attestation | No WHQL required for many drivers, but still a Microsoft-signed catalog; requires identity verification and a legal entity. |
| WHQL / HCK | Full logo + HLK testing | Highest bar, required for some classes; expensive and slow. |

Facts that must not be overstated:

- A self-signed catalog is **not** a production signature.
- Signing the `.cat` does not make the driver trustworthy; the certificate
  chain must be trusted by the target machine.
- Secure Boot + test signing are mutually exclusive in the default
  configuration; a user cannot simply "install the unsigned driver".
- The repository contains no private key and will never contain one.
- CI can generate a catalog; an unsigned catalog is a structural artifact,
  not an installable signature.

The installer (Milestone 9) must check the signature state and refuse to
pretend an unsigned driver is production-signed.

---

## 10. Data-integrity risks

This is the highest-risk part of the project. An EXT4 write bug can destroy a
Linux installation or a game library.

### 10.1 Specific risks

1. **Journal state.** Writing metadata while `INCOMPAT_RECOVER` is set, or
   ignoring pending JBD2 transactions, corrupts the filesystem. Mitigation:
   refuse write mount until journal replay is implemented and tested; never
   clear the journal without replaying it.
2. **Metadata checksums.** `metadata_csum` covers superblock, group
   descriptors, inodes, extent tails, directory tails and bitmaps. A write
   that forgets a checksum is silent corruption. Mitigation: one central
   checksum module; every mutation recomputes the covering checksum; tests
   compare against e2fsprogs.
3. **Ordered write barriers.** Data must reach stable storage before the
   journal commit that references it. Windows flush semantics and the
   underlying disk cache must be respected. Mitigation: explicit
   `IRP_MJ_FLUSH_BUFFERS`/`FLUSH` barriers; write-order fault injection.
4. **Allocation bitmap divergence.** Double allocation or leaked blocks are
   slow corruption. Mitigation: allocate under an exclusive volume lock,
   update bitmap and group counters atomically in one transaction, verify
   with `e2fsck -fn` after every test.
5. **Extent tree corruption.** Splitting an extent incorrectly can point a
   file at metadata blocks. Mitigation: bounds-check every physical block
   against the block bitmap and metadata ranges; fuzz the extent code.
6. **Directory index corruption.** HTREE writes can make entries unreachable.
   Mitigation: read-only linear/HTREE verification first; write support only
   after index rebuild tests.
7. **Orphan handling.** Unlinked-but-open files are tracked in the orphan
   list/file. Ignoring it leaks inodes. Mitigation: reject read-write mount if
   `orphan_present` is set; implement orphan processing with the journal.
8. **Concurrent Windows and Linux access.** The user promises not to mount
   both at once, but Windows Fast Startup and Linux hibernation can violate
   that promise. Mitigation: document, and refuse to write if the EXT4 state
   or journal indicates an unclean mount from another OS.
9. **Surprise removal.** A yanked USB/HDD must fail I/O, not bugcheck.
   Mitigation: cancel-safe IRP handling, pending-IRP tracking, VPB teardown.
10. **Malicious images.** A crafted EXT4 image is untrusted input. Mitigation:
    no direct structure casts, integer-overflow checks, allocation caps,
    depth caps, fuzzing, and Driver Verifier.

### 10.2 Hard rules

- Never format, resize, repair, or rewrite a partition table.
- Never run a destructive repair automatically.
- Never mount an unapproved EXT4 volume automatically.
- Never claim a dirty filesystem is clean.
- Never enable write support to hit a deadline.

---

## 11. Estimated complexity by milestone

Estimates assume one experienced Windows kernel/filesystem engineer working
full time, with access to a test VM and Linux image-generation tooling. They
are planning ranges, not promises.

| Milestone | Scope | Estimate | Principal risk |
| --- | --- | --- | --- |
| 0 | Research, architecture, CI foundation | 0.5–1 month | CI toolchain (solved in principle) |
| 1 | Driver skeleton, registration, dispatch, package | 1–2 months | Getting unload/registration/lifetime exactly right |
| 2 | Superblock, geometry, features, UUID, parser tests | 1–2 months | Feature matrix completeness |
| 3 | Read-only mount, VDO/VPB, Mount Manager, drive letter | 2–4 months | Windows mount protocol and dismount |
| 4 | Full read: inodes, dirs, extents, xattrs, cache | 3–6 months | Cache Manager/FCB correctness, HTREE, checksums |
| 5 | Journal parsing and recovery | 4–8 months | JBD2 replay, crash consistency |
| 6 | Write support: allocation, journal commits, rename | 8–16 months | Data integrity, concurrency, fsync ordering |
| 7 | Real-device mounting, hotplug, Verifier, VM tests | 3–6 months | Surprise removal, security, long-run stability |
| 8 | Gaming compatibility, performance, stress | 2–5 months | Application semantics, anti-cheat, large files |
| 9 | Installer, signing, security review, release | 2–4 months | Signing identity, installer safety |

**Total to a production-quality read/write driver: roughly 27–55 engineer-months**
(2–4.5 years for one full-time engineer), with the read-only MVP at roughly
7–14 months. This is comparable to the sustained effort behind WinBtrfs.

### Go / no-go

**Go, with staging.** The project is technically feasible and the architecture
is sound, but only if it is developed as a staged, test-driven filesystem
project:

1. Read-only first; do not promise writes.
2. No real-device write mount until journal replay, checksums, crash tests
   and Driver Verifier pass.
3. The user's existing gaming HDD is never an automated test target.
4. Do not claim success without a GitHub Actions run and, later, a VM test
   run that actually happened.

If the goal were "read the game library on Windows within a few weeks", a
native FSD is the wrong tool and a userspace ext4 reader (read-only, no
Windows drive letter) would be safer. The stated goal is a genuine native
filesystem, so the staged kernel path is correct.

---

## Appendix A — Windows/EXT4 semantic gaps to resolve before Milestone 3

| Area | EXT4 | Windows | Planned policy |
| --- | --- | --- | --- |
| Names | bytes, case-sensitive, only `/` and NUL forbidden | UTF-16, case-insensitive by default, many forbidden characters, reserved device names | Strict UTF-8 decode; reversible documented escape for unrepresentable bytes; detect case collisions at mount; refuse read-write if a lossless mapping is impossible |
| Permissions | POSIX mode, uid/gid, ACL xattrs | Windows security descriptor | Read-only: synthesize a stable SD. Write: preserve POSIX metadata, do not expose ACL editing until a mapping is proven |
| Symlinks | fast and slow symlinks | reparse points, privilege required | Expose as reparse points in read-only mode; never silently follow across the volume boundary |
| Hard links | native | native, but different APIs | Support, with correct link count and file-ID behaviour |
| Sparse files | holes, unwritten extents | sparse attribute + valid data length | Read holes as zero; preserve sparseness on write |
| Timestamps | seconds + nanoseconds + epoch bits | FILETIME 100 ns | Clamp out-of-range; preserve nanoseconds where the inode has room |
| Alternate data streams | no direct equivalent | `file:stream` | Not supported initially; return `STATUS_NOT_SUPPORTED`, never silently drop |
| Case sensitivity | per-filesystem/per-directory | mostly case-insensitive | Read-only first; collision detection; no silent shadowing |
| Max name | 255 bytes | 255 UTF-16 units | Reject writes that cannot round-trip; expose long paths through the normal Windows path APIs |
| Encryption/verity | possible | no equivalent | Refuse mount when the feature is present |
| Quotas/project quotas | possible | no equivalent | Read-only ignore with a warning; refuse write until understood |

## Appendix B — Decisions recorded for Milestone 2

- Parse the primary superblock and the documented backup locations; never
  silently fall back to a backup without an explicit, logged reason.
- Verify `metadata_csum` on every structure before trusting it.
- Reject `encrypt`, `verity`, `casefold`, `bigalloc`, `inline_data`, `meta_bg`,
  `mmp`, `journal_dev`, `dirdata`, `compression` at first.
- Modern `mke2fs` defaults include `orphan_file`; recognize it and refuse
  write mount, but allow read-only only when `orphan_present` is clear.
- `extents`, `64bit`, `flex_bg`, `dir_index`, `filetype`, `sparse_super`,
  `large_file`, `huge_file`, `dir_nlink`, `extra_isize`, `metadata_csum`,
  `metadata_csum_seed` are the initial read-only target set.
