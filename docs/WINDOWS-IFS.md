# Windows IFS integration

This document explains how STGR EXT4 fits the Windows Installable File System
(IFS) contract, what the Milestone 1 code actually does, and what must be built
before a volume can be mounted.

It is a design and reference document. The driver has not been compiled or
loaded; no behaviour below is claimed as verified.

---

## 1. What an IFS driver is

A Windows file system driver is a kernel-mode driver that:

1. creates a **file system device object (FSDO)**;
2. registers itself with `IoRegisterFileSystem`;
3. receives `IRP_MN_MOUNT_VOLUME` probes from the I/O manager;
4. creates a **volume device object (VDO)** and a **volume parameter block
   (VPB)** for a volume it accepts;
5. serves file IRPs (`CREATE`, `READ`, `WRITE`, `DIRECTORY_CONTROL`, …) from
   the VDO;
6. participates in caching, memory-mapped I/O, locks, oplocks, security, flush,
   shutdown, dismount, and surprise removal.

This is not a minifilter and not a PnP function driver. It is a WDM driver that
implements the file system contract directly. FastFAT and CDFS are the canonical
simple examples; WinBtrfs is the closest real-world precedent for a non-Windows
file system.

---

## 2. Object model

```text
DriverObject
   |
   +-- DeviceObject: FSDO  (\STGRExt4, FILE_DEVICE_DISK_FILE_SYSTEM)
   |      |
   |      +-- registered in the file system list
   |
   +-- (per mounted volume) DeviceObject: VDO
          |
          +-- DeviceExtension: STGR_VOLUME
          +-- Vpb -> RealDevice (the disk volume)
          |        -> DeviceObject (the VDO)
          |        -> Flags = VPB_MOUNTED
          |
          +-- FileObject -> FsContext  (FCB)
                          -> FsContext2 (CCB)
```

### FSDO

- Created in `DriverEntry` with `IoCreateDevice`.
- Type `FILE_DEVICE_DISK_FILE_SYSTEM`.
- No `DO_BUFFERED_IO` or `DO_DIRECT_IO`; a file system makes its own buffering
  and caching decisions.
- Registered with `IoRegisterFileSystem`.
- Holds one extra reference so the registration lifetime is explicit.

### VDO

- Created for each accepted volume.
- Type `FILE_DEVICE_DISK_FILE_SYSTEM`.
- Its device extension holds the engine instance, target device, VPB, FCB list,
  mount state, and reference count.
- File IRPs are sent to the VDO, not the FSDO.

### VPB

The VPB ties the VDO to the underlying storage volume. It is created with
`IoCreateVpb`, attached to the target device, and marked `VPB_MOUNTED`. The I/O
manager uses it to recognize that the volume is mounted and by which device.

A VPB must never be reused for a different volume. Dismount and removal must
clear `VPB_MOUNTED` and tear the VPB down in the documented order.

---

## 3. Driver entry and registration

The implemented sequence in `driver/src/driver.c`:

1. Zero `StgrGlobals`, store `DriverObject`.
2. `ExInitializeResourceLite(&StgrGlobals.VolumeLock)`.
3. `InitializeListHead(&StgrGlobals.VolumeList)`.
4. `IoCreateDevice(..., FILE_DEVICE_DISK_FILE_SYSTEM, ...)` for `\STGRExt4`.
5. Fill `STGR_DEVICE_HEADER` with a magic value, kind, self pointer, and driver
   pointer.
6. Set every `MajorFunction` to the default dispatcher, then override
   `IRP_MJ_FILE_SYSTEM_CONTROL` with the mount/verify handler.
7. Set `DriverObject->DriverUnload`.
8. `IoRegisterFileSystem(fsdo)`.
9. `ObReferenceObject(fsdo)` to hold the registration reference.

Failure paths unregister if needed and delete the device. The resource is
deleted only if it was initialized.

### Why the FSDO is created before registration

`IoRegisterFileSystem` inserts the device into the file system registration
list. The device must already be fully formed and its dispatch table installed,
otherwise a probe could arrive with an uninitialized extension.

### Why an extra reference

`IoRegisterFileSystem` does not transfer ownership in a way the driver can rely
on for teardown. Holding an explicit object reference makes the unload order
deterministic: unregister, dereference, delete. This mirrors the pattern in
Microsoft's file system samples.

---

## 4. Mount protocol

### 4.1 The probe

The I/O manager sends `IRP_MJ_FILE_SYSTEM_CONTROL` with minor function
`IRP_MN_MOUNT_VOLUME` to each registered file system. The stack location
contains:

```c
Stack->Parameters.MountVolume.DeviceObject;  // volume to inspect
Stack->Parameters.MountVolume.Vpb;           // VPB supplied by the I/O manager
```

The driver must:

- inspect the volume without disturbing it;
- accept only if it recognizes a supported file system;
- create a VDO and VPB on acceptance;
- complete the IRP with `Information = 1` on success;
- complete with `Information = 0` and an error on refusal.

### 4.2 Milestone 1 behavior

The current handler:

1. Asserts `PASSIVE_LEVEL`.
2. Reads the target device and VPB pointers.
3. If either is null, logs and returns `STATUS_UNRECOGNIZED_VOLUME`.
4. Acquires `VolumeLock` shared.
5. If `Unloading` is set, releases and returns `STATUS_UNRECOGNIZED_VOLUME`.
6. Releases the lock and returns `STATUS_UNRECOGNIZED_VOLUME` **without
   touching the volume**.
7. Completes the IRP with `Information = 0`.

This is a deliberate, truthful refusal. It is not a stub that claims success.

### 4.3 Future acceptance sequence

```c
// Pseudocode; not implemented.
status = StgrEngineReadSuperblock(volume, &sb);
if (!NT_SUCCESS(status)) return STATUS_UNRECOGNIZED_VOLUME;

status = StgrFeaturePolicyCheck(&sb);
if (!NT_SUCCESS(status)) return STATUS_UNRECOGNIZED_VOLUME;

if (!StgrIsVolumeApproved(&sb.uuid)) return STATUS_UNRECOGNIZED_VOLUME;

status = StgrCheckJournalState(&sb);
if (!NT_SUCCESS(status)) return STATUS_UNRECOGNIZED_VOLUME;

vdo = StgrCreateVolumeDevice(fsdo, target, &sb);
if (vdo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

vpb = IoCreateVpb(target);
if (vpb == NULL) { StgrDeleteVolumeDevice(vdo); return STATUS_INSUFFICIENT_RESOURCES; }

vpb->RealDevice = target;
vpb->DeviceObject = vdo;
vpb->Flags |= VPB_MOUNTED;

Irp->IoStatus.Information = 1;
return STATUS_SUCCESS;
```

The order matters: validate everything before creating objects, so a refusal
has no cleanup side effects. The allowlist check must happen before the VPB is
attached.

### 4.4 Verify volume

`IRP_MN_VERIFY_VOLUME` is sent to a mounted file system to confirm that the VPB
still refers to the volume the driver thinks it does. It is answered by the VDO,
not by the FSDO. In Milestone 1 there is no VDO, so the handler returns
`STATUS_UNRECOGNIZED_VOLUME`. It deliberately does **not** return
`STATUS_WRONG_VOLUME`, because that would ask the I/O manager to remount a
volume this driver never mounted.

---

## 5. IRP dispatch

### 5.1 Current table

| Major function | Handler | Behaviour |
| --- | --- | --- |
| `IRP_MJ_FILE_SYSTEM_CONTROL` | `StgrFsdFileSystemControl` | Mount/verify refusal |
| All others | `StgrFsdDispatch` | `STATUS_INVALID_DEVICE_REQUEST` |

The default handler completes every IRP with `Information = 0`. It does not
queue, pend, or return success.

### 5.2 Planned table

| Major function | Purpose |
| --- | --- |
| `IRP_MJ_CREATE` | Open file/directory, share access, security |
| `IRP_MJ_CLEANUP` | Last handle cleanup, pending writes |
| `IRP_MJ_CLOSE` | Final reference release |
| `IRP_MJ_READ` | Cached and non-cached reads, paging reads |
| `IRP_MJ_WRITE` | Cached and non-cached writes, paging writes |
| `IRP_MJ_QUERY_INFORMATION` | Basic/standard/name/position information |
| `IRP_MJ_SET_INFORMATION` | Rename, delete, size, timestamps, disposition |
| `IRP_MJ_QUERY_EA` / `SET_EA` | Extended attributes; ADS refused |
| `IRP_MJ_DIRECTORY_CONTROL` | Query directory, notify change |
| `IRP_MJ_FILE_SYSTEM_CONTROL` | Mount, verify, lock, unlock, FSCTL |
| `IRP_MJ_DEVICE_CONTROL` | Driver/volume IOCTLs |
| `IRP_MJ_FLUSH_BUFFERS` | Flush cache and journal |
| `IRP_MJ_SHUTDOWN` | Final flush |
| `IRP_MJ_LOCK_CONTROL` | Byte-range locks |
| `IRP_MJ_PNP` | Surprise removal, remove device |
| `IRP_MJ_QUERY_VOLUME_INFORMATION` | Volume size, label, geometry |
| `IRP_MJ_SET_VOLUME_INFORMATION` | Label (write milestone) |

### 5.3 Dispatch rules

- Use `IoGetCurrentIrpStackLocation`, never the previous location, except when
  forwarding an IRP.
- Complete every IRP exactly once.
- Return the same status written to `Irp->IoStatus.Status` unless the IRP was
  pended or completed asynchronously.
- For pended IRPs, mark the stack location pending with
  `IoMarkIrpPending` and return `STATUS_PENDING`.
- Do not touch user buffers after completion.
- Validate `Parameters.*.Length` before use.
- Reject unknown minor functions with `STATUS_INVALID_DEVICE_REQUEST`.

---

## 6. IRP contexts and queuing

A real file system cannot do all work in the dispatch routine. Planned model:

- Allocate an `STGR_IRP_CONTEXT` from a lookaside list.
- Capture the IRP, device object, and relevant parameters.
- Return `STATUS_PENDING` when work must be deferred.
- Queue to a worker thread or use `FsRtl`/`IoQueueWorkItem`.
- Use `IoSetCancelRoutine` and a cancel-safe queue.
- On cancellation, complete with `STATUS_CANCELLED` and release the context.

The context owns references to the VDO and FCB. It must not outlive them.

Milestone 1 has no context and no queue: every request completes inline.

---

## 7. File objects, FCB, CCB, SCB

### FCB

One per file stream. Planned fields:

- inode number and generation;
- inode snapshot (mode, uid/gid, size, timestamps, flags);
- extent tree root or cached extent map;
- file size and allocation size;
- cache state;
- open/cleanup/close state;
- reference count;
- lock and list linkage.

The FCB is stored in `FileObject->FsContext`.

### CCB

One per handle. Planned fields:

- pointer to the FCB;
- desired access and share access;
- create disposition/options;
- per-handle directory enumeration state;
- delete-on-close flag.

The CCB is stored in `FileObject->FsContext2`.

### SCB

One per data stream. EXT4 has one unnamed stream per inode. Alternate data
streams are not mapped. Requests for named streams return
`STATUS_NOT_SUPPORTED`; they are never silently redirected to the unnamed
stream.

### Share access

Use `IoCheckShareAccess` on create and `IoUpdateShareAccess` /
`IoRemoveShareAccess` on cleanup/close. Share modes must be enforced exactly,
including delete sharing.

### Delete and rename

Delete-on-close and rename require careful ordering with open handles. In
read-only mode they return `STATUS_MEDIA_WRITE_PROTECTED` or
`STATUS_ACCESS_DENIED`. In write mode they must be journaled before the
directory entry is changed.

---

## 8. Cache Manager and memory manager

The cache manager is the difference between a toy and a usable file system.

### Required integration

- `CcInitializeCacheMap` when a file is first opened for cached I/O.
- `CcCopyRead` for cached reads.
- `CcCopyWrite` for cached writes.
- `CcSetFileSizes` when the file size changes.
- `CcFlushCache` on flush and close.
- `CcPurgeCacheSection` on invalidation.
- `CcMdlRead` / `CcPrepareMdlWrite` for direct memory-mapped I/O.
- `CcZeroData` for holes and truncation.
- Lazy-write callbacks: `AcquireForLazyWrite`, `ReleaseFromLazyWrite`.
- Read-ahead callbacks: `AcquireForReadAhead`, `ReleaseFromReadAhead`.

### Paging I/O

Paging reads and writes arrive with `IRP_PAGING_IO` set. They must not block on
a resource that a paging operation could itself need, must not touch user
buffers, and must complete promptly. A deadlock here hangs the whole system,
not one application.

### Coherency

The engine must never serve stale data behind the cache manager's back. In
read-only mode this is mostly a matter of invalidating on dismount. In write
mode, metadata changes (size, allocation) must be reflected with
`CcSetFileSizes` and the corresponding flush ordering.

### Fast I/O

A `FAST_IO_DISPATCH` table provides `Read`, `Write`, `QueryBasicInformation`,
`QueryStandardInformation`, `QueryNetworkOpenInformation`, `AcquireForCcFlush`,
`ReleaseForCcFlush`, `AcquireForLazyWrite`, `ReleaseFromLazyWrite`,
`AcquireForReadAhead`, and `ReleaseFromReadAhead`. Fast I/O must be safe at
`APC_LEVEL` and must fall back to the IRP path when it cannot complete.

---

## 9. Locks and oplocks

### Byte-range locks

- `FsRtlInitializeFileLock`
- `FsRtlProcessFileLock`
- `FsRtlUninitializeFileLock`

Lock state is per file object/FCB and must survive cleanup correctly. Lock
requests are not allowed on read-only handles in ways Windows rejects.

### Oplocks

Oplocks let the cache manager and clients optimize access. A file system that
ignores them will still work for many applications but will behave poorly and
may break sharing semantics.

Planned:

- `FsRtlInitializeOplock` / `FsRtlUninitializeOplock` (or the current
  documented API for the target Windows version).
- `FsRtlOplockFsctrl` for oplock FSCTLs.
- `FsRtlOplockIsFastIoPossible` in the fast I/O path.
- Break oplocks before a conflicting open or metadata change.

Oplock correctness is especially important when a Linux guest or another
Windows process may access the same volume. The project policy is to refuse
concurrent multi-OS access rather than pretend to coordinate it.

---

## 10. Security

### On-disk metadata

EXT4 stores a POSIX mode, uid, gid, and possibly ACL xattrs. Windows expects a
security descriptor and an access check.

Read-only plan:

- Synthesize a stable security descriptor from the mode, uid, and gid.
- Map read/write/execute bits to a documented Windows access policy.
- Do not expose POSIX ACL editing.
- Do not let an unprivileged user change the mapping.

Write plan (later):

- Preserve POSIX metadata on create and update.
- Define an explicit ownership mapping policy.
- Refuse operations whose security semantics cannot be represented, rather
  than silently dropping metadata.

### Access checks

Use the documented IFS pattern:

- `SeAccessCheck` against the file's security descriptor;
- `IoCheckShareAccess` for share modes;
- `SeSinglePrivilegeCheck` for privileged operations such as restore or
  backup semantics;
- `IoAssignSecurity` / `SeSetSecurityDescriptorInfo` for set-security
  requests, once write support exists.

Never trust a user-supplied security descriptor without probing and validating
it.

---

## 11. PnP, shutdown, dismount, surprise removal

### PnP

A volume device receives `IRP_MJ_PNP` requests. The important ones:

- `IRP_MN_SURPRISE_REMOVAL`: the device is gone. Fail new I/O, cancel or fail
  outstanding I/O, mark the VDO dead, do not bugcheck.
- `IRP_MN_REMOVE_DEVICE`: tear down the VDO and VPB after references drop.

The FSDO is not a PnP device and does not receive these.

### Shutdown

Register with `IoRegisterShutdownNotification` and handle `IRP_MJ_SHUTDOWN` by
flushing cached data and, in write mode, committing or cleanly stopping the
journal. A shutdown handler must not hang indefinitely on a dead device.

### Dismount

`IRP_MJ_FILE_SYSTEM_CONTROL` / `IRP_MN_DISMOUNT_VOLUME` (and the user-mode
`FSCTL_DISMOUNT_VOLUME`) must:

1. stop new opens;
2. flush or invalidate;
3. close remaining FCBs;
4. clear `VPB_MOUNTED`;
5. delete the VPB and VDO when references permit.

### Unload

`DriverUnload` is only called when the driver object has no open handles. It
must still handle a non-empty volume list defensively (log and refuse to unload,
or tear down safely). The current code assumes the list is empty and has no
volumes; that assumption must be revisited in Milestone 3.

---

## 12. Mount Manager and drive letters

The Windows Mount Manager (`mountmgr.sys`) owns drive-letter and volume-name
mappings. The planned design keeps this in the user-mode management service:

- the service discovers the mounted VDO through a control interface;
- it asks the Mount Manager to create a persistent drive letter for the volume;
- it stores the mapping in its own configuration;
- the driver does not create symbolic links in `\DosDevices` directly.

Doing this in user mode keeps policy and persistence out of the kernel and
avoids a driver bug corrupting the machine's mount database.

Milestone 1 has no VDO, no control interface, and no drive letter.

---

## 13. Common IFS mistakes this design guards against

| Mistake | Consequence | Guard |
| --- | --- | --- |
| Completing an IRP twice | Bugcheck / use-after-free | Single completion path, context ownership |
| Returning success for an unsupported operation | Silent data loss | Explicit error policy |
| Forgetting `IoMarkIrpPending` | Race, lost completion | Pending path reviewed together |
| Touching a user buffer after completion | Use-after-free | Copy or lock before completing |
| Blocking at `DISPATCH_LEVEL` | Deadlock / bugcheck | IRQL assertions and rules |
| Reusing a VPB | Wrong volume mounted | VPB lifecycle tied to VDO |
| Not handling surprise removal | Bugcheck on unplug | Dead flag and I/O failure path |
| Ignoring share access | Data corruption across processes | `IoCheckShareAccess` on every create |
| Ignoring the cache manager | Stale data, corruption | Cc integration, no bypass |
| Assuming a volume is EXT4 without checking | Mounting a foreign file system | Magic, geometry, feature, checksum checks |
| Trusting on-disk lengths | Pool overflow | Bounds and overflow checks |
| Unload with live volumes | Use-after-free | Volume list and references |

---

## 14. Milestone 1 exit criteria (not yet met)

The current source is intended to demonstrate:

- [x] FSDO creation and registration in source.
- [x] Unload ordering in source.
- [x] Safe mount refusal in source.
- [x] No fake success path.
- [ ] Compiles Debug and Release x64.
- [ ] Loads and unloads in a disposable VM.
- [ ] Driver Verifier reports nothing.
- [ ] Confirmed refusal of every volume with `Information = 0`.

Only the first four are source-level facts. The last four require a build and a
VM and have not been performed.
