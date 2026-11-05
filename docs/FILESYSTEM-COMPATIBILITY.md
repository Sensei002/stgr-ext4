# File system and feature compatibility

Status: **policy and design. Nothing has been tested.** The driver does not
parse EXT4 yet, so the practical compatibility of the current build is: it
mounts nothing.

This document is the contract for which volumes the project intends to accept,
which it refuses, and how EXT4 semantics map to Windows. The engine policy
table is authoritative; when it changes, this table must change in the same
commit.

---

## 1. Compatibility verdicts

Every feature gets one of four verdicts:

| Verdict | Meaning |
| --- | --- |
| **Read + write** | Supported for read and, once write support ships, write |
| **Read only** | Safe to read; any write mount is refused |
| **Refuse** | No mount, read or write |
| **Later** | Planned, but not in the current milestone |

A read-only-compatible feature is not automatically write-safe. The two
verdicts are tracked separately.

---

## 2. EXT4 feature matrix

### 2.1 Core target set (read-only first)

| Feature | Flag | Read | Write | Notes |
| --- | --- | --- | --- | --- |
| Extents | `INCOMPAT_EXTENTS` | Yes | Later | Mandatory for regular files in the initial target |
| 64-bit | `INCOMPAT_64BIT` | Yes | Later | 64-bit block numbers and descriptor fields |
| Flexible block groups | `INCOMPAT_FLEX_BG` | Yes | Later | Metadata placement across groups |
| Directory index | `COMPAT_DIR_INDEX` | Yes | Later | HTREE traversal required |
| File types | `INCOMPAT_FILETYPE` | Yes | Later | Required; legacy name-length encoding is refused |
| Sparse super | `RO_COMPAT_SPARSE_SUPER` | Yes | Later | Backup superblock placement |
| Sparse super 2 | `COMPAT_SPARSE_SUPER2` | Yes | Later | `s_backup_bgs` |
| Large file | `RO_COMPAT_LARGE_FILE` | Yes | Later | 64-bit `i_size` |
| Huge file | `RO_COMPAT_HUGE_FILE` | Yes | Later | `i_blocks` in fs blocks |
| Directory link count | `RO_COMPAT_DIR_NLINK` | Yes | Later | `i_links_count` semantics |
| Extra inode size | `RO_COMPAT_EXTRA_ISIZE` | Yes | Later | Extra timestamp/version fields |
| Metadata checksums | `RO_COMPAT_METADATA_CSUM` | Yes | Later | CRC32C on all metadata |
| Checksum seed | `INCOMPAT_CSUM_SEED` | Yes | Later | `s_checksum_seed` |
| Extended attributes | `COMPAT_EXT_ATTR` | Yes | Later | Read; ADS is not a mapping |
| Journal | `COMPAT_HAS_JOURNAL` | Yes | Later | Read-only until replay is proven |
| Resize inode | `COMPAT_RESIZE_INODE` | Yes | Never resize | Reserved GDT blocks are read |
| Directory preallocation | `COMPAT_DIR_PREALLOC` | Yes | Later | Preallocated directory blocks |
| Orphan file | `COMPAT_ORPHAN_FILE` | Conditional | Refuse | Read-only only when `orphan_present` is clear |
| Quota | `RO_COMPAT_QUOTA` | Read only | Refuse | No quota enforcement |
| Project quota | `RO_COMPAT_PROJECT` | Read only | Refuse | Project IDs are metadata only |
| Read-only flag | `RO_COMPAT_READONLY` | Read only | Refuse | The volume says it is read-only |
| Legacy GDT checksum | `RO_COMPAT_GDT_CSUM` | Yes | Later | CRC16 group descriptor checksum |
| B-tree directory | `RO_COMPAT_BTREE_DIR` | Ignore | Ignore | Historical bit; not used by kernel/e2fsprogs |

### 2.2 Refused features

| Feature | Flag | Verdict | Reason |
| --- | --- | --- | --- |
| Compression | `INCOMPAT_COMPRESSION` | Refuse | Not implemented; layout changes |
| Journal device | `INCOMPAT_JOURNAL_DEV` | Refuse | External journal not supported |
| Metadata block groups | `INCOMPAT_META_BG` | Refuse | Descriptor placement differs |
| MMP | `INCOMPAT_MMP` | Refuse | Multi-mount protection not implemented |
| EA inode | `INCOMPAT_EA_INODE` | Refuse (initial) | Shared xattr values outside the inode |
| Directory data | `INCOMPAT_DIRDATA` | Refuse | Data stored with directory entries |
| Large directory | `INCOMPAT_LARGEDIR` | Refuse (initial) | HTREE depth/size handling not proven |
| Inline data | `INCOMPAT_INLINE_DATA` | Refuse | Data in inode/iblock |
| Encryption | `INCOMPAT_ENCRYPT` | Refuse | Windows has no equivalent; ciphertext would be exposed |
| Casefold | `INCOMPAT_CASEFOLD` | Refuse | Name semantics differ |
| Bigalloc | `RO_COMPAT_BIGALLOC` | Refuse | Cluster allocation changes every bitmap |
| Replica | `RO_COMPAT_REPLICA` | Refuse | Replication semantics |
| Shared blocks | `RO_COMPAT_SHARED_BLOCKS` | Refuse | Non-upstream shared-block images; ownership is ambiguous |
| Verity | `RO_COMPAT_VERITY` | Refuse | Merkle tree verification not implemented |
| Orphan present | `RO_COMPAT_ORPHAN_PRESENT` | Refuse | Pending orphan work |
| Fast commit | `COMPAT_FAST_COMMIT` | Refuse (initial) | Additional journal format |
| Stable inodes | `COMPAT_STABLE_INODES` | Refuse (initial) | Not yet understood |
| Imagic inodes | `COMPAT_IMAGIC_INODES` | Refuse | Obsolete/implementation-specific |
| Lazy block groups | `COMPAT_LAZY_BG` | Refuse | Not used by Linux/e2fsprogs |
| Exclude inode | `COMPAT_EXCLUDE_INODE` | Refuse | Not used |
| Exclude bitmap | `COMPAT_EXCLUDE_BITMAP` | Refuse | Not used |
| Snapshot | `RO_COMPAT_HAS_SNAPSHOT` | Refuse | Snapshot semantics not supported |

### 2.3 Unknown bits

- Unknown **incompatible** bit: refuse. Always.
- Unknown **read-only-compatible** bit: refuse a write mount; refuse a read
  mount unless the bit is documented as safe for reading.
- Unknown **compatible** bit: refuse unless the bit is documented as not
  changing layout or semantics.

The safe default is refusal. "It mounted on my machine" is not evidence.

---

## 3. Block size and volume size

Initial target:

| Property | Initial support | Notes |
| --- | --- | --- |
| Block size 1024 | Yes | `s_first_data_block = 1` |
| Block size 2048 | Yes | |
| Block size 4096 | Yes | Most common |
| Block size 8192+ | Later | Needs validation against Windows memory/page assumptions |
| 16-bit `i_blocks` | Yes | |
| 64-bit block numbers | Yes | With `64BIT` |
| Volume > 2 TiB | Yes, with `64BIT` | Practical limits validated per test image |
| File > 4 GiB | Yes, with `LARGE_FILE` | |
| File > 16 TiB | Later | Depends on block size and extent limits |

Windows uses 4 KiB pages. A 1 KiB block size means multiple file system blocks
per page; the cache manager integration must handle that correctly. It is a
supported target, not an afterthought, but it needs dedicated tests.

---

## 4. ext2, ext3, and ext4

| File system | Initial support | Notes |
| --- | --- | --- |
| ext4 | Target | The feature matrix above applies |
| ext3 | Not initially | Journal format is JBD, not JBD2; legacy block maps may appear |
| ext2 | Not initially | No journal, but legacy block maps are not in the initial read path |

The project name is EXT4, and the initial engine assumes extents. A volume
without `EXTENTS` is refused rather than read through an untested legacy block
map. Supporting ext2/ext3 later is possible but is a separate compatibility
claim with its own tests.

---

## 5. Windows semantic mapping

These are the hard gaps. None of this is implemented yet.

### 5.1 Names

| Aspect | EXT4 | Windows | Planned policy |
| --- | --- | --- | --- |
| Encoding | Arbitrary bytes, conventionally UTF-8 | UTF-16 | Strict UTF-8 decode; reversible escape for invalid bytes |
| Case | Case-sensitive | Case-insensitive by default | Detect collisions at mount; refuse write if a lossless mapping is impossible |
| Separators | `/` forbidden; NUL forbidden | `\` and `/`; many characters forbidden | Map `/` to `\`; reject or escape forbidden characters |
| Reserved names | None | `CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `LPT1..9` | Escape with a documented prefix; never silently rename |
| Length | 255 bytes | 255 UTF-16 units, path limits | Reject writes that cannot round-trip; expose long paths |
| Trailing dot/space | Legal | Illegal or stripped | Escape; do not strip silently |
| Unicode normalization | Not enforced | Not enforced | Do not normalize; preserve bytes |

The mapping must be reversible. A name that Windows cannot represent must not
be silently hidden or renamed without a record. At mount, the driver can detect
case collisions and refuse a write mount; a read-only mount may present a
documented escaped view.

### 5.2 Metadata

| EXT4 | Windows | Read-only plan | Write plan |
| --- | --- | --- | --- |
| POSIX mode | ACL/security descriptor | Synthesize a stable SD | Preserve mode; map owner/group |
| uid/gid | SID/owner | Map to a documented principal | Define ownership policy |
| POSIX ACL xattr | Windows ACL | Not exposed initially | Later, only with a proven mapping |
| SELinux xattr | None | Preserve, do not expose | Preserve |
| Timestamps (s + ns) | FILETIME (100 ns) | Clamp; preserve ns where possible | Preserve on write |
| Birth time (`i_crtime`) | Creation time | Map when present | Set when supported |
| Device nodes | No equivalent | Expose inert or refuse | Refuse |
| FIFO/socket | No equivalent | Expose inert or refuse | Refuse |

### 5.3 File semantics

| Feature | Plan |
| --- | --- |
| Hard links | Support; correct link count and file ID |
| Symlinks | Expose as reparse points in read-only mode; never follow across the volume boundary silently |
| Sparse files | Read holes as zero; preserve sparseness on write |
| Alternate data streams | Not supported; `STATUS_NOT_SUPPORTED`, never silently drop |
| Memory-mapped files | Required for games; depends on cache manager and paging I/O |
| Byte-range locks | Support through `FsRtlProcessFileLock` |
| Oplocks | Support; break correctly on conflicting access |
| File IDs | Stable per inode/generation |
| Delete-on-close | Later, journaled |
| Rename | Later, journaled and crash-tested |
| Compression | Not supported |
| Encryption | Not supported |
| Deduplication | Not applicable |

### 5.4 Case collisions

A Linux directory can contain `README` and `readme`. Windows normally treats
them as the same name. The driver must:

- detect collisions during directory enumeration or at mount;
- refuse a write mount when a collision exists and a lossless mapping cannot be
  provided;
- never silently hide one entry;
- document the read-only escape strategy.

A per-directory case-sensitive flag is a possible future feature, but it is not
part of the initial design.

---

## 6. e2fsprogs compatibility

`e2fsprogs` is the reference implementation for generating and checking images.

Planned test corpus:

- images created by a pinned `mke2fs` version;
- images with each supported feature combination;
- images with features that must be refused;
- images created by older `mke2fs` versions that are still common;
- images dirtied by `debugfs`, interrupted writes, or simulated crashes;
- images verified with `e2fsck -fn`.

The exact `e2fsprogs` version used to generate the corpus must be recorded in
the test manifest, because defaults change between releases. A new `mke2fs`
default (for example, `orphan_file`) is a compatibility event: the parser must
recognize it and decide, not silently misread it.

The project does not run `e2fsck` in repair mode as part of normal operation.
`e2fsck -fn` is a verification tool in the test lab. The driver never invokes
it and never repairs a volume.

---

## 7. Windows version compatibility

| Windows | Status |
| --- | --- |
| Windows 11 x64 | Initial target |
| Windows 10 x64 | Not planned before Windows 11 is stable |
| Windows Server | Not planned |
| Windows on ARM64 | Not planned |
| 32-bit Windows | Not planned |

The driver is compiled with `TargetVersion=Windows10` because that is the WDK
target label, not because Windows 10 is supported. The test matrix is Windows
11 x64 only until stated otherwise.

---

## 8. What "compatible" will mean

The project will not use the word "compatible" without evidence. A volume is
compatible when:

1. it is approved by UUID;
2. every enabled feature is in the supported set;
3. the superblock and all metadata checksums verify;
4. the journal is clean or has been replayed successfully;
5. the read tests match the Linux reference hashes;
6. the specific test image is in the corpus and the test run is recorded.

Game compatibility is a separate claim with a separate matrix; see
[`GAMING-COMPATIBILITY.md`](GAMING-COMPATIBILITY.md).

---

## 9. Current status

| Item | Status |
| --- | --- |
| Feature policy table in code | Not implemented |
| Superblock parser | Not implemented |
| Any read support | Not implemented |
| Any write support | Not implemented |
| Compatibility test corpus | Does not exist |
| Any tested volume | None |

The current build refuses every volume. That is the only compatibility claim
that is true today.
