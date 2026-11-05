# Architecture

Status: design for Milestones 2–9, with the Milestone 1 skeleton implemented in
source. This document describes intent; it is not a claim that the components
below exist.

---

## 1. Goals and non-goals

### Goals

- A native Windows kernel-mode EXT4 file system driver for Windows 11 x64.
- Correct interaction with the Windows I/O manager, cache manager, memory
  manager, and Mount Manager.
- Read-only support first, with a complete and correct read path.
- Controlled write support only after journal replay, checksum, and crash
  testing are proven.
- A portable EXT4 engine that can be unit-tested and fuzzed outside the kernel.
- Explicit refusal of unsupported or unapproved volumes.
- Reproducible cloud builds and honest release engineering.

### Non-goals

- A user-mode file system, FUSE port, WinFsp/Dokany wrapper, or Linux VM.
- A file-copy or sync tool that pretends to be a file system.
- Formatting, repairing, resizing, or partitioning disks.
- Mounting arbitrary EXT4 volumes automatically.
- Supporting every EXT4 feature in the first release.
- 32-bit Windows, ARM64, or Windows 10 before x64 Windows 11 is stable.
- Encryption, verity, casefold, bigalloc, inline data, or metadata block groups
  in early milestones.

---

## 2. System context

```text
+-------------------------------------------------------------+
| User mode                                                   |
|                                                             |
|  Explorer / games / Steam        stgr-ext4 CLI / service    |
|            |                              |                 |
|            | Win32 file APIs              | configuration,  |
|            |                              | status, mount   |
+------------|------------------------------|-----------------+
|            v                              v                 |
|      Windows I/O manager          Service Control Manager   |
|            |                              |                 |
|            v                              |                 |
|  +---------------------+                  |                 |
|  | STGRExt4.sys (FSD)  |<-----------------+                 |
|  |                     |  registry config / control device  |
|  |  Windows front end  |                                    |
|  |  +--------------+   |                                    |
|  |  | EXT4 engine  |   |                                    |
|  |  +--------------+   |                                    |
|  +----------|----------+                                    |
|             | IRP_MJ_READ / WRITE / FLUSH to volume device   |
|             v                                               |
|      Volume device -> disk class driver -> port driver      |
+-------------------------------------------------------------+
| Kernel mode                                                 |
+-------------------------------------------------------------+
```

The driver is a **WDM file system driver**, not a filter, minifilter, or KMDF
device driver. It owns the file system contract directly.

---

## 3. Layering

### 3.1 Windows front end (`driver/src`, `driver/include`)

Responsibilities:

- `DriverEntry`, unload, file system registration.
- Device objects: one FSDO, one VDO per mounted volume.
- VPB creation and lifecycle; Mount Manager interaction through the
  user-mode service.
- IRP dispatch, IRP contexts, pending returns, cancellation.
- FCB, CCB, and SCB lifecycles.
- Cache Manager callbacks, lazy read/write, fast I/O.
- Byte-range locks, oplocks, security descriptors, share access.
- Flush, shutdown, dismount, surprise removal.
- Approved-volume policy enforcement at mount time.
- Translation between Windows semantics and the engine's POSIX/EXT4 model.

The front end contains no EXT4 on-disk knowledge. It asks the engine for
inodes, directory entries, extents, xattrs, and journal state.

### 3.2 Portable EXT4 engine (`driver/ext4`, Milestone 2+)

Responsibilities:

- Superblock and group descriptor parsing and validation.
- Bitmap interpretation.
- Inode decoding.
- Extent tree traversal and mutation.
- Directory entry and HTREE traversal and mutation.
- Extended attributes.
- JBD2 journal parsing and replay.
- Allocation policy.
- Metadata checksum computation and verification.
- All bounds, overflow, and consistency checks.

Constraints:

- Freestanding C: no kernel headers, no libc, no floating point, no
  `setjmp`/exceptions.
- All disk access through a callback table.
- All memory through a callback table, with explicit caps.
- No global mutable state; an instance context is passed explicitly.
- Deterministic behaviour for the same input and callback responses.
- Every function documents its IRQL/threading assumption (the engine itself is
  callable at PASSIVE_LEVEL only).

### 3.3 Callback boundary

```c
typedef struct _STGR_BLOCK_IO
{
    NTSTATUS (*Read)(void *Context, UINT64 Offset, void *Buffer,
                     UINT32 Length, UINT32 *Transferred);
    NTSTATUS (*Write)(void *Context, UINT64 Offset, const void *Buffer,
                      UINT32 Length, UINT32 *Transferred);
    NTSTATUS (*Flush)(void *Context);
    UINT64   VolumeSizeBytes;
    UINT32   LogicalBlockSize;
} STGR_BLOCK_IO;

typedef struct _STGR_ALLOCATOR
{
    void *(*Allocate)(void *Context, SIZE_T Size);
    void  (*Free)(void *Context, void *Memory);
    void *Context;
} STGR_ALLOCATOR;
```

Final signatures will be fixed in Milestone 2 after the host harness exists.
The important properties are: no implicit global state, explicit length and
transfer counts, 64-bit offsets, and a flush primitive the journal can use.

In kernel mode the callbacks build synchronous IRPs against the volume device.
In user mode they use a file handle, `pread`/`pwrite`-style calls, or an
in-memory image. The engine never knows the difference.

---

## 4. Directory layout and ownership

| Path | Layer | Contents |
| --- | --- | --- |
| `driver/include/` | Kernel | Shared kernel-only definitions |
| `driver/src/` | Kernel | DriverEntry, dispatch, volume, FCB, cache |
| `driver/ext4/` | Portable | On-disk engine, no kernel includes |
| `driver/tests/` | Host | Engine unit tests and fuzz harness (future) |
| `management/` | User | Rust CLI and future service |
| `scripts/` | Build | Build, package, validate |
| `docs/` | Docs | Design and operations |

Dependency rule: `driver/ext4` must never include `ntddk.h`, and `driver/src`
must never reimplement on-disk parsing. A CI check should enforce this when the
engine exists.

---

## 5. Windows object model (planned)

### 5.1 FSDO

Created once in `DriverEntry`:

- name `\STGRExt4`;
- type `FILE_DEVICE_DISK_FILE_SYSTEM`;
- extension `STGR_DEVICE_HEADER` with `Kind = StgrDeviceFileSystem`;
- registered with `IoRegisterFileSystem`;
- one extra object reference held for the registration lifetime.

### 5.2 VDO

Created per mounted volume:

- type `FILE_DEVICE_DISK_FILE_SYSTEM`;
- extension `STGR_VOLUME` (future) containing:
  - back-pointer to the FSDO;
  - target volume device;
  - VPB pointer;
  - engine instance;
  - volume geometry and feature summary;
  - FCB list and lock;
  - mount state, read-only flag, dismount flag;
  - reference count;
  - approved-volume UUID.

The VDO is the target of file IRPs. The FSDO only handles file system control
and driver-wide queries.

### 5.3 VPB

Created when a mount is accepted. The sequence is the documented IFS one:

1. `IoCreateVpb(TargetDevice)`.
2. Set `Vpb->RealDevice = TargetDevice`.
3. Set `Vpb->DeviceObject = Vdo`.
4. Set `Vpb->Flags |= VPB_MOUNTED`.
5. Attach the VPB to the target device.
6. Complete the mount IRP with `Information = 1`.

The VPB is torn down on dismount and on volume removal. A stale VPB must never
be reused for a different volume.

### 5.4 FCB / CCB / SCB

- **FCB** (file control block): one per open file stream. Holds the inode
  number, inode snapshot, extent state, size, timestamps, and cache state.
- **CCB** (context control block): one per open handle. Holds the FCB pointer,
  share access, desired access, and per-handle state.
- **SCB** (stream control block): one per data stream. For EXT4 there is one
  unnamed stream; named streams (ADS) are refused.

Reference counting is explicit. Cleanup happens on the last handle; close
happens when the file object is destroyed; FCB teardown happens when the last
reference is dropped.

---

## 6. Lifecycle

### 6.1 Driver load (implemented in source)

1. Zero globals, store the driver object.
2. Initialize the volume resource and list.
3. Create the FSDO.
4. Install the dispatch table.
5. Register with `IoRegisterFileSystem`.
6. Hold a reference so unload ordering is deterministic.

The current code performs no configuration read and creates no VDO. A mount
probe is declined immediately without touching the volume.

### 6.2 Mount (Milestone 3+)

1. Receive `IRP_MN_MOUNT_VOLUME` with target device and VPB.
2. Acquire the volume lock shared; reject if unloading.
3. Read and validate the superblock through the engine.
4. Check the feature policy.
5. Check the approved-volume allowlist by UUID.
6. Check journal and orphan state.
7. Create the VDO and engine instance.
8. Create and attach the VPB.
9. Complete with success and `Information = 1`.

Every failure before step 8 deletes the VDO and completes with
`STATUS_UNRECOGNIZED_VOLUME` (or a more specific error where appropriate) and
`Information = 0`.

### 6.3 Dismount and unload

1. Stop new opens.
2. Flush or invalidate cached state as appropriate.
3. Close all FCBs.
4. Detach and delete the VPB.
5. Delete the VDO.
6. On driver unload, ensure the volume list is empty, unregister the FSDO,
   drop the registration reference, delete the FSDO, and delete the resource.

Surprise removal follows a separate path: outstanding IRPs are cancelled or
failed, the VDO is marked dead, and teardown is deferred until references drop.

---

## 7. Threading and IRQL model

| Operation | IRQL | Notes |
| --- | --- | --- |
| DriverEntry / unload | PASSIVE_LEVEL | May block |
| Mount / verify / dismount | PASSIVE_LEVEL | May do synchronous I/O |
| Create / cleanup / close | PASSIVE_LEVEL | May block |
| Read / write (cached) | PASSIVE_LEVEL to APC_LEVEL | Cache Manager callbacks |
| Paging I/O | APC_LEVEL or higher | No blocking waits; no user buffers |
| Fast I/O | PASSIVE_LEVEL to APC_LEVEL | Must not block |
| Directory control | PASSIVE_LEVEL | May be large |
| Device control | PASSIVE_LEVEL | Validate buffers |
| DPC / ISR | Not used | The driver has no interrupt path |

Rules:

- Never allocate with `PagedPool` at `DISPATCH_LEVEL`.
- Never touch pageable code or data at `DISPATCH_LEVEL`.
- Never wait on a resource at `DISPATCH_LEVEL` or above.
- Never access a user-mode pointer directly; use `Irp->AssociatedIrp.
  SystemBuffer`, `Irp->MdlAddress`, or `ProbeForRead`/`ProbeForWrite` at the
  correct IRQL.
- All engine calls happen at PASSIVE_LEVEL.
- A lock order is documented before a second lock is introduced. The initial
  order is: volume lock → FCB list lock → FCB lock → cache.

---

## 8. Error model

- Every public function returns `NTSTATUS` or an engine status.
- `STATUS_SUCCESS` means the operation completed as requested.
- A deliberately unsupported operation returns `STATUS_NOT_IMPLEMENTED`,
  `STATUS_NOT_SUPPORTED`, or `STATUS_INVALID_DEVICE_REQUEST`.
- A malformed on-disk structure returns `STATUS_FILE_CORRUPT_ERROR` or
  `STATUS_IO_DEVICE_ERROR`, never success.
- A checksum mismatch is corruption. It is not repaired silently.
- An I/O error is propagated. It is not retried indefinitely.
- A failed mount completes with `Information = 0`.
- A failed write must leave the caller with a clear error and must not report a
  partial success as a full success.

Engine status values are mapped to NTSTATUS in one place, not scattered through
the front end.

---

## 9. Memory and resource policy

- No unbounded allocation based on an on-disk count.
- Pool tags identify every allocation category (`'gTSI'`, `'gTSB'`, etc.).
- Lookaside lists for fixed-size FCBs and IRP contexts once profiling justifies
  them.
- Directory enumeration uses bounded buffers and restart indices.
- Extent traversal has a hard depth cap and a total-node cap.
- Recursion is avoided for attacker-controlled structures; use explicit stacks
  with fixed limits.
- Every error path frees memory and releases locks. A single-exit pattern with
  a cleanup label is preferred.

---

## 10. Configuration and control

The driver does not read a general-purpose configuration file. It reads:

- the approved-volume allowlist from its service parameters;
- a small set of safety switches (for example, read-only enforcement);
- optionally, a diagnostic verbosity value.

The Rust management service writes configuration and asks the driver to
re-evaluate approved volumes. The CLI is a thin client. The driver never
accepts a volume approval from an unauthenticated user-mode caller.

See [`CONFIGURATION.md`](CONFIGURATION.md).

---

## 11. Testing architecture

| Layer | Runs on | What it proves |
| --- | --- | --- |
| Engine unit tests | Host (Windows/Linux) | Parsing, bounds, checksums |
| Corrupt-image corpus | Host | Rejection without crash |
| Fuzz targets | Host + sanitizers | Memory safety of the parser |
| Driver compile | CI | Type/link correctness, warnings |
| Static analysis | CI + local | Defect patterns |
| Driver load/unload | Disposable VM | Registration, lifetime |
| Mount/read tests | Disposable VM | Windows integration, hashes |
| Journal/crash tests | Disposable VM | Consistency |
| Real hardware | Lab machine | Hotplug, power loss |

A host test cannot prove kernel lifetime correctness. A VM test cannot prove
parser memory safety under all inputs. Both are required.

---

## 12. Key design decisions

| Decision | Alternative | Why |
| --- | --- | --- |
| WDM IFS driver | KMDF, minifilter, user mode | File systems must implement the IFS contract directly. |
| Portable engine with callbacks | Kernel-only parser | Enables host fuzzing and deterministic tests. |
| Explicit little-endian decoding | Cast disk buffers to structs | Avoids alignment, endianness, and over-read bugs. |
| Feature allowlist | Best-effort parsing | An unknown feature can change layout; guessing corrupts data. |
| Approved-volume allowlist | Probe every disk | Prevents accidental or malicious mounts. |
| Read-only first | Write-first MVP | Write bugs destroy data; reads are the safe proving ground. |
| Metadata checksums mandatory | Ignore checksums | Checksums detect corruption and some tampering. |
| Refuse dirty volumes | Replay automatically on mount | Automatic replay is a write and needs a proven journal engine. |
| Unsigned artifacts labelled | Pretend CI output is installable | Signing is a separate, verifiable step. |

---

## 13. Current implementation status

| Component | Status |
| --- | --- |
| FSDO creation and registration | Compiles in CI, never loaded |
| Unload | Compiles in CI, never loaded or exercised |
| Dispatch defaults | Compiles in CI, never loaded |
| Mount refusal | Compiles in CI, never loaded |
| VDO / VPB | Not implemented |
| Engine and callbacks | Not implemented |
| Cache / FCB / CCB / SCB | Not implemented |
| Management service | Not implemented |
| CLI scaffold | Formats, lints, tests, and links in CI |
| CI | Green: `ci.yml` run 7 (Debug and Release x64 driver builds) |

Nothing in this document should be read as evidence that a component works.
