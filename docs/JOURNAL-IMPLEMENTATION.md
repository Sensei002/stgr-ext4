# Journal implementation plan (JBD2)

Status: **not implemented**. This document defines the design and the safety
rules for the JBD2 journal. No journal code exists, no replay has been run, and
no write support is enabled.

An EXT4 write bug can destroy a Linux installation. The journal is the
mechanism that makes crash consistency possible, and it is also the easiest
place to corrupt a volume if it is misunderstood. The project therefore treats
journal support as a gate, not a feature checkbox.

---

## 1. Why the journal is mandatory before writes

Without journal replay, a volume that was not cleanly unmounted contains
metadata in an intermediate state. Writing new metadata on top of that state
compounds the inconsistency. There is no safe "write a little" mode.

Rules:

1. **No write mount** on a volume with `INCOMPAT_RECOVER`,
   `s_state & EXT4_ERROR_FS`, a non-empty journal, or `orphan_present`.
2. **No read-only mount** on a volume whose consistency cannot be established,
   unless the user explicitly chooses a forensic read-only mode that is clearly
   labelled and still validates every structure it touches.
3. **Never clear a journal without replaying it.** Clearing `needs_recovery`
   without applying committed transactions loses data.
4. **Never replay on the original device until replay has passed on a scratch
   copy** and `e2fsck -fn` reports clean.
5. **Never write while another OS may have the volume mounted.** Fast Startup,
   hibernation, and Linux mounts violate exclusive access. The driver cannot
   detect every case, so the documentation and the approval workflow must.

---

## 2. JBD2 on-disk structures

The journal is either an internal inode (`s_journal_inum`, usually inode 8) or
an external device (`JOURNAL_DEV` / `s_journal_dev`). The initial policy
implements internal journals only and rejects external journal devices.

Journal blocks are addressed relative to the journal, not the file system. The
first journal block contains the JBD2 superblock.

### 2.1 Block header

Every journal block starts with a 12-byte header:

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `h_magic` = `0xC03B3998` |
| `0x04` | 4 | `h_blocktype` |
| `0x08` | 4 | `h_sequence` |

Block types:

| Value | Type |
| --- | --- |
| 1 | Descriptor |
| 2 | Commit |
| 3 | Revoke v1 |
| 4 | Superblock v2 |
| 5 | Revoke v2 |

### 2.2 Journal superblock (type 4)

Key fields after the header:

| Offset | Size | Field |
| --- | --- | --- |
| `0x0C` | 4 | `s_blocksize` |
| `0x10` | 4 | `s_maxlen` (journal length in blocks) |
| `0x14` | 4 | `s_first` (first usable journal block) |
| `0x18` | 4 | `s_sequence` |
| `0x1C` | 4 | `s_start` (first block to replay; 0 = empty) |
| `0x20` | 4 | `s_errno` |
| `0x24` | 4 | `s_feature_compat` |
| `0x28` | 4 | `s_feature_incompat` |
| `0x2C` | 4 | `s_feature_ro_compat` |
| `0x30` | 16 | `s_uuid` |
| `0x40` | 4 | `s_nr_users` |
| `0x44` | 4 | `s_dynsuper` |
| `0x48` | 4 | `s_max_transaction` |
| `0x4C` | 4 | `s_max_trans_data` |
| `0x50` | 1 | `s_checksum_type` |
| `0x54` | 4 | `s_checksum` |
| `0x58` | 64 | `s_users[16]` |

The exact tail layout depends on the journal feature set and must be read from
the specification for the version being implemented. The engine validates
`h_magic`, `h_blocktype`, `s_blocksize` (must match the file system block
size), `s_maxlen`, `s_first`, `s_start < s_maxlen`, and the checksum.

`INCOMPAT_RECOVER` in the **file system** superblock is the flag that says the
journal needs replay. The journal superblock's `s_start` says where replay
begins. Both must be consistent; a mismatch is treated as corruption.

### 2.3 Descriptor block (type 1)

A descriptor block contains a header followed by one or more tags. Each tag
maps a journal data block to a file system block.

Tag fields (v2/v3 depending on feature set):

| Field | Meaning |
| --- | --- |
| `t_blocknr` | Low 32 bits of the target file system block |
| `t_blocknr_high` | High 32 bits (64-bit journals) |
| `t_flags` | `ESCAPE` (0x1), `SAME_UUID` (0x2), `DELETED` (0x4), `LAST_TAG` (0x8) |
| `t_checksum` | Tag checksum (csum v2/v3) |

Flags matter:

- `ESCAPE`: the data block's first 4 bytes equal the journal magic, so they were
  escaped by the writer. Replay must unescape them.
- `SAME_UUID`: the target UUID matches the journal UUID; if not set, the tag
  includes its own UUID (64-bit journal format).
- `DELETED`: the target block was deleted (revoked); do not replay it.

Data blocks follow the descriptor tags in order. The last tag carries `LAST_TAG`.

### 2.4 Commit block (type 2)

A commit block has the header and a checksum. A transaction is committed only
when a commit block with the matching sequence is present and both the tags and
the commit checksum verify. A descriptor without a commit is an incomplete
transaction and must not be replayed.

### 2.5 Revoke blocks (types 3 and 5)

Revoke blocks list file system blocks that must not be replayed for the
current transaction, even if an older transaction wrote them. Revoke v2 can
carry 64-bit block numbers and checksums. Replay must build a revoke set per
transaction and apply it before writing the transaction's data blocks.

---

## 3. Feature flags

Journal feature bits the engine must handle:

| Set | Bit | Name | Policy |
| --- | --- | --- | --- |
| compat | `0x1` | `CHECKSUM` | Support |
| incompat | `0x1` | `REVOKE` | Support |
| incompat | `0x2` | `64BIT` | Support |
| incompat | `0x4` | `ASYNC_COMMIT` | Reject initially |
| incompat | `0x8` | `CSUM_V2` | Support |
| incompat | `0x10` | `CSUM_V3` | Support |
| incompat | `0x20` | `FAST_COMMIT` | Not supported; no replay of a journal that sets it |

JBD2 defines **no** `ro_compat` features; the journal superblock's
`ro_compat` mask is expected to be zero. Do not confuse the journal superblock
with the file-system superblock: recovery is signalled by the file system's
`INCOMPAT_RECOVER` bit (`0x4`), not by a journal flag.

Unknown journal features cause refusal. A journal whose checksum version is not
understood cannot be replayed safely.

---

## 4. Replay algorithm

Replay is a read-mostly operation until the final apply. The engine implements
it in two phases:

### Phase A — parse and validate, no writes

1. Read the file system superblock; confirm `HAS_JOURNAL` and an internal
   journal inode.
2. Read the journal inode; map its data blocks (extent tree or legacy map) and
   confirm the journal length.
3. Read and validate the JBD2 superblock.
4. If `s_start == 0`, the journal is empty. If the file system still has
   `INCOMPAT_RECOVER`, that is an inconsistency: refuse and report it.
5. Walk the journal circularly from `s_start`, bounded by `s_maxlen`:
   - read a block header;
   - on a descriptor, record tags and the following data blocks;
   - on a revoke, record revoked blocks;
   - on a commit with the expected sequence, finalize the transaction;
   - stop at a sequence mismatch, an invalid block, or a full loop;
   - never follow a tag outside the file system;
   - never replay a transaction without a valid commit.
6. Verify every checksum before accepting a block.
7. Compute the final write set: for each committed transaction, apply revokes,
   then data blocks, in order. A later transaction overrides an earlier one for
   the same target block.

### Phase B — apply

1. Optionally copy the image to scratch storage and replay there first. This is
   mandatory for the first implementation and for every new journal feature.
2. Write each data block to its target, honoring escape decoding.
3. Flush.
4. Update the file system superblock: clear `INCOMPAT_RECOVER`, set
   `EXT4_VALID_FS` if appropriate, update `s_state`, and recompute checksums.
5. Update the journal superblock: set `s_start = 0`, advance `s_sequence` per
   JBD2 rules, and recompute its checksum.
6. Flush again.
7. Verify with `e2fsck -fn` on the scratch image. Only then consider the
   original device.

The exact order of steps 4 and 5 follows JBD2's recovery protocol; it must be
implemented from the specification and tested, not guessed.

### Replay limits

- Maximum transactions per replay.
- Maximum tags per descriptor.
- Maximum revoke records.
- Maximum total blocks written.
- No recursion; use an explicit loop.
- On any inconsistency, stop and refuse. A partial replay is worse than no
  replay.

---

## 5. Write path and transaction lifecycle

Write support is Milestone 6. The design:

```text
1. Begin transaction
2. Reserve credits for metadata + data + revokes
3. Modify in-memory buffers (bitmaps, inode, extent, directory)
4. Recompute metadata checksums
5. Write data blocks (ordered mode) and flush
6. Write journal descriptor + data blocks
7. Flush (barrier)
8. Write commit block
9. Flush (barrier)
10. Checkpoint: write metadata to final locations
11. Flush
12. Release journal space
```

### Ordered mode

In ordered mode, file data must reach stable storage before the journal commit
that references its metadata. Otherwise a crash can expose a committed inode
size pointing at uninitialised data.

### Barriers

A barrier is a flush of the device write cache plus a write-order guarantee.
The driver must issue a flush to the underlying volume and wait for completion
before proceeding past the barrier. On Windows this means sending
`IRP_MJ_FLUSH_BUFFERS` to the volume device (and respecting the storage stack's
cache semantics), not merely flushing the Windows cache manager.

If the device or driver does not honor flushes, the journal cannot guarantee
ordering. The driver must detect and report this condition rather than pretend
the barrier worked.

### Checksums

- Recompute the JBD2 block checksum for every descriptor, commit, and revoke.
- Recompute the file system metadata checksum for every mutated structure.
- A checksum failure at write time is a bug; fail the operation and mark the
  volume read-only rather than write inconsistent metadata.

### Transaction credits

Under-reserving credits is a classic journal bug: the transaction commits
partway through a metadata update. The engine's reservation function must
account for the worst case of every operation (extent split, directory split,
bitmap and group descriptor updates, revokes, and checksums) and fail the
operation before modifying anything if the reservation cannot be met.

---

## 6. Crash-consistency testing

The test matrix, on disposable virtual disks only:

| Scenario | Injection point |
| --- | --- |
| Power loss before descriptor | Drop all journal writes after data |
| Power loss after descriptor, before commit | Drop commit |
| Power loss after commit, before checkpoint | Keep journal, drop checkpoint |
| Torn write inside a metadata block | Flip bytes in one sector |
| Reordered writes | Reorder data and commit |
| Lost flush | Pretend flush completed, discard cache |
| Revoke handling | Reuse a block across transactions |
| Orphan processing | Unlink with an open handle, then crash |

After every run:

1. Replay on a copy.
2. Run `e2fsck -fn` and record the exact output.
3. Compare file contents against a known-good manifest.
4. Run the test again with the replayed image as input.
5. Never reuse the original test image without restoring a snapshot.

A test that only checks "the system booted" is not sufficient. The file system
must pass `e2fsck -fn` and the content hashes must match.

---

## 7. Integration with the Windows front end

- `IRP_MJ_FLUSH_BUFFERS` flushes the cache manager and then the journal.
- `IRP_MJ_SHUTDOWN` performs a final flush and, if possible, a clean journal
  stop. It must not hang on a dead device.
- `IRP_MJ_WRITE` in cached mode goes through `CcCopyWrite`; the lazy-write
  callback eventually writes through the engine.
- Paging writes must be journaled like any other metadata change if they change
  allocation or size.
- `STATUS_FILE_CORRUPT_ERROR` is returned when the journal is inconsistent.
  The volume is marked read-only for the rest of the mount.
- A journal failure never triggers an automatic repair or format.

---

## 8. Current status and gates

| Item | Status |
| --- | --- |
| JBD2 superblock parser | Not implemented |
| Descriptor/commit/revoke parser | Not implemented |
| Checksum v2/v3 | Not implemented |
| Replay engine | Not implemented |
| Scratch-image replay | Not implemented |
| Write transactions | Not implemented |
| Barrier/flush integration | Not implemented |
| Crash-injection tests | Not implemented |
| `e2fsck -fn` verification | Never run |

Gates before any write mount:

1. Read-only mount and full read path are stable.
2. Journal replay passes on a corpus of generated unclean images.
3. Crash-injection matrix passes with zero unexplained corruption.
4. Metadata checksums are verified on every structure the write path touches.
5. Driver Verifier is clean.
6. The approval workflow prevents writes to unapproved or dirty volumes.
7. A rollback/forensic path exists and is documented.

Until all seven are true, the driver is read-only. If a deadline pressures the
project to skip a gate, the correct response is to move the deadline.
