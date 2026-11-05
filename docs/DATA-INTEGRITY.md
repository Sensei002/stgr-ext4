# Data integrity policy

Status: **policy only.** No EXT4 read or write path exists. No integrity test
has been run.

An EXT4 volume may contain a Linux root filesystem, years of photographs, or a
game library that takes days to re-download. A bug that writes one wrong block
can destroy it. This document defines the invariants the implementation must
satisfy, the failure modes it must survive, and the evidence required before
write support is enabled.

---

## 1. Non-negotiable invariants

1. **No unapproved write.** A volume that is not explicitly approved for write
   is never written to, not even to "fix" metadata.
2. **No dirty write.** A volume with pending journal work, an error state, or
   orphan work is not written to.
3. **No unjournaled metadata change.** Every metadata mutation is part of a
   transaction that can be replayed or discarded.
4. **No unchecked structure.** Every structure is bounds-checked, overflow-
   checked, and checksum-verified before it is trusted.
5. **No silent repair.** Corruption is reported. It is never "cleaned up"
   behind the user's back.
6. **No format, resize, or partition edit.** Ever, under any command.
7. **No success without completion.** A failed write is reported as failed.
8. **No concurrent multi-OS write.** The driver refuses when the volume may be
   mounted elsewhere.
9. **No partial transaction reported as committed.** A commit requires a
   durable commit record and a completed barrier.
10. **No test on a real disk.** Destructive tests use disposable virtual disks.

---

## 2. Failure modes and required behaviour

| Failure | Required behaviour |
| --- | --- |
| Short read from the volume | Fail the operation; do not use a partial buffer |
| I/O error (EIO) | Propagate; mark the volume in error; stop writes |
| Checksum mismatch | Treat as corruption; refuse the structure and the operation |
| Unknown incompatible feature | Refuse the mount |
| Dirty journal | Refuse write mount; read-only only if consistency is provable |
| Orphan work pending | Refuse write mount |
| Allocation bitmap inconsistency | Refuse the write; do not guess |
| Extent tree cycle or overflow | Refuse the file/inode; do not recurse further |
| Directory record overlap | Refuse the directory operation |
| Device surprise removal | Fail outstanding I/O; no bugcheck; no further writes |
| Power loss during commit | On next mount, replay if committed; discard if not |
| Torn write inside a block | Detect via checksum where available; otherwise treat as corruption |
| Disk full | Return `STATUS_DISK_FULL`; do not partially allocate |
| Quota exceeded | Refuse the write; do not silently overrun |
| Memory allocation failure | Fail the operation before modifying metadata |
| Journal reservation failure | Fail the operation before modifying metadata |
| Shutdown with pending writes | Flush and commit, or refuse the shutdown cleanly |
| Unload with a mounted volume | Refuse to unload; do not free live objects |

---

## 3. Read path integrity

The read path must never expose data from outside the requested file.

- Extent lengths are bounded by the file size and the volume size.
- Unwritten extents and holes return zeroes, never stale disk contents.
- Directory entries never point outside the directory block.
- Symlink targets are bounded by the inode size and the target format.
- Extended attribute values are bounded by the block or inode.
- A failed checksum aborts the read rather than returning the bytes.

Read-only does not mean risk-free: a parser bug can still corrupt kernel memory
or crash the machine. Host fuzzing and Driver Verifier are required before a
read-only release.

---

## 4. Write path integrity

### 4.1 Ordering

For a metadata change that references file data (ordered mode):

```text
data write -> flush -> journal descriptor + data -> flush -> commit -> flush
           -> checkpoint metadata -> flush
```

The commit must not reach stable storage before the data it references. A
barrier that the storage stack ignores is not a barrier; the driver must verify
flush completion and fail the operation if it cannot.

### 4.2 Transactions

- A transaction has a bounded credit reservation computed before any mutation.
- All metadata updates in one logical operation are in one transaction.
- If any step fails, the transaction is aborted and the in-memory state is
  discarded or restored; it is never partially checkpointed.
- Revoked blocks are recorded so replay does not resurrect them.
- The journal is never overwritten while it contains unreplayed commits.

### 4.3 Allocation

- Block and inode allocation happens under an exclusive volume lock.
- Bitmap, group descriptor counters, and checksums are updated together.
- A block is marked allocated before it is referenced by an inode.
- A block is marked free only after the reference is removed and the change is
  committed.
- Double allocation and leaked blocks are integrity bugs, not performance
  issues.
- The allocator never hands out a block that belongs to metadata, the
  superblock, the GDT, the journal, or another file.

### 4.4 Extent and directory mutation

- Extent insertion and splitting are validated against the block bitmap before
  the new extent is visible.
- Index nodes stay sorted and non-overlapping.
- A directory split updates both the old and new blocks and the parent index in
  one transaction.
- HTREE checksums and tails are recomputed.
- A rename updates source and destination in one transaction, including link
  counts and `..` for directory moves.

### 4.5 Checksums

Every mutated structure gets a fresh checksum:

- superblock;
- group descriptor;
- inode;
- extent block tail;
- directory tail;
- block and inode bitmap;
- journal descriptor/commit/revoke.

A missing checksum update is silent corruption. Tests compare against
`e2fsprogs` output, not just against the driver's own view.

---

## 5. Concurrency

### Windows

- Multiple processes can open the same file.
- The cache manager can write back asynchronously.
- Paging I/O can arrive at raised IRQL.
- A flush can race with a write.
- A dismount can race with an open.

Locks are ordered and documented. The initial order is:

```text
volume lock -> FCB list lock -> FCB lock -> cache manager
```

No path takes them in the reverse order. A lock is never held across a blocking
I/O unless the design explicitly permits it at PASSIVE_LEVEL.

### Linux coexistence

The user may promise not to mount the same volume in Linux and Windows at the
same time. The system can violate that promise:

- Windows Fast Startup leaves NTFS dirty, not EXT4, but dual-boot users can
  still boot Linux and mount the volume.
- Linux hibernation can resume with the volume mounted.
- A USB disk can be moved between machines.
- A virtual machine can share a raw disk.

The driver cannot detect every case. Mitigations:

- refuse write mount when the journal or state indicates an unclean mount;
- document the exclusive-access requirement prominently;
- require an explicit write approval;
- consider a mount-time warning and a "last writer" marker in the future;
- never claim that concurrent access is safe.

A last-writer marker is not a lock. It is a diagnostic, not a safety guarantee.

---

## 6. Surprise removal and power loss

### Surprise removal

1. Mark the VDO dead.
2. Fail new I/O immediately with `STATUS_DEVICE_NOT_CONNECTED` or the
   appropriate status.
3. Cancel or fail outstanding IRPs.
4. Stop the journal; do not attempt to flush to a dead device.
5. Release FCBs and VDO after references drop.
6. Do not bugcheck.

### Power loss

The journal is the recovery mechanism. On the next mount:

- if `INCOMPAT_RECOVER` is set, replay or refuse;
- if the journal is empty and the state is clean, mount normally;
- if the state is inconsistent, refuse and explain;
- never guess.

### Shutdown

`IRP_MJ_SHUTDOWN` flushes cached data and commits or cleanly stops the journal.
It must have a bounded wait. A hung flush must not hang the shutdown forever;
the driver reports the failure and leaves the volume for journal replay.

---

## 7. Malicious images

An EXT4 image is untrusted input. The engine must assume every field is
attacker-controlled.

Required defences:

- no direct casts of disk buffers to structures;
- explicit little-endian decode;
- checked addition, subtraction, multiplication, and shift;
- block and inode numbers validated against geometry;
- directory record lengths validated for alignment and bounds;
- extent depth, entry count, and total node count capped;
- xattr name/value lengths bounded;
- journal transaction count, tag count, and revoke count capped;
- allocation sizes capped and derived from validated counts;
- no recursion on attacker-controlled depth;
- no execution of data;
- no kernel pointer derived from disk data;
- fuzzing with ASan/UBSan on the host;
- Driver Verifier in the VM.

A parser that is "careful" but has one unchecked multiplication is not
acceptable.

---

## 8. Verification with e2fsprogs

`e2fsck -fn` is the external oracle. It is run on disposable images after every
destructive test.

Rules:

- `-n` means "no changes"; the checker must never repair during verification.
- The exact command, version, output, and image hash are recorded.
- A clean `e2fsck` is necessary but not sufficient: file content hashes must
  also match the reference manifest.
- `e2fsck` on the original image is not a substitute for replay on a scratch
  copy.
- A test that produces a clean `e2fsck` but wrong file contents is a failure.

The project does not ship `e2fsck`, invoke it at runtime, or use it to repair a
user's volume.

---

## 9. Backup and rollback

Before any write test or write-capable mount:

1. Snapshot the virtual disk.
2. Record the image hash.
3. Keep a copy that the test cannot modify.
4. Know how to restore it.
5. Verify the restore works before trusting the test.

For a user's real volume, the only safe rollback is a backup made outside the
driver. The project must say this plainly: **this software is not a backup
tool, and a failed write may be unrecoverable without one.**

---

## 10. Incident response

If a build is suspected of corrupting a volume:

1. Stop using the build. Do not mount the volume read-write again.
2. Preserve the disk image or the device before attempting repair.
3. Record the build hash, configuration, feature set, and exact sequence.
4. Run `e2fsck -fn` on a copy and retain the output.
5. Do not run a repair tool on the only copy.
6. Report the issue with a synthetic reproduction if possible.
7. Publish an advisory that states the affected versions and the known-safe
   recovery path.
8. Do not claim the issue is fixed until the fix is tested on a copy and the
   evidence is recorded.

---

## 11. Evidence required before write support

| Gate | Evidence |
| --- | --- |
| Read path stable | Hash comparison against Linux for the corpus |
| Journal replay | Replay on scratch images, `e2fsck -fn` clean |
| Metadata checksums | Every mutated structure verified by e2fsprogs |
| Allocation | No double allocation or leaks across randomized tests |
| Crash consistency | Injection matrix, zero unexplained corruption |
| Concurrency | Multi-threaded stress with Verifier clean |
| Surprise removal | Unplug tests with no bugcheck and no further writes |
| Power loss | Replay succeeds and contents match |
| Security | Fuzzing and review of every parser entry |
| Documentation | User-facing warnings and recovery steps |

No gate may be waived to meet a schedule.

---

## 12. Current status

| Item | Status |
| --- | --- |
| Read path | Not implemented |
| Write path | Not implemented |
| Journal | Not implemented |
| Integrity tests | None |
| `e2fsck` corpus | Does not exist |
| Write approval | Not implemented |

The only integrity property that holds today is that the driver writes nothing
and mounts nothing. That is a consequence of being incomplete, not a
certification of safety.
