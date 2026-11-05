# EXT4 on-disk format reference

This document records the subset of the EXT4 format the engine must implement.
It is written from the public kernel.org EXT4 on-disk specification. No Linux
kernel source is copied.

All multi-byte fields are **little-endian** on disk. Nothing in this document
is a licence to cast a disk buffer to a C structure: the engine decodes fields
explicitly, validates them, and never trusts a length or offset.

---

## 1. Volume layout

```text
byte 0
  +--------------------------------------------------+
  | boot block / 1024 bytes (not file system data)   |
  +--------------------------------------------------+
  | superblock (1024 bytes) at byte 1024             |
  +--------------------------------------------------+
  | block group descriptor table                     |
  +--------------------------------------------------+
  | reserved GDT blocks (resize_inode)               |
  +--------------------------------------------------+
  | data blocks, block and inode bitmaps, inode      |
  | tables, directories, file data, journal          |
  +--------------------------------------------------+
```

The block size is `1024 << s_log_block_size`, valid values 1024, 2048, 4096,
8192, 16384, 32768, and 65536 (the last only on architectures that support it;
Windows x64 can address it but the project will validate practical limits).
`s_first_data_block` is 1 for 1 KiB blocks (the superblock occupies byte 1024,
inside block 1) and 0 for larger block sizes.

Group count:

```text
groups = ceil((s_blocks_count - s_first_data_block) / s_blocks_per_group)
```

All arithmetic uses 64-bit values and checks for overflow.

---

## 2. Superblock

Located at byte offset 1024 (block 0 offset 1024, or block 1 for 1 KiB blocks).
Size 1024 bytes. Magic `0xEF53` at offset `0x38`.

### 2.1 Key fields

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| `0x00` | 4 | `s_inodes_count` | Total inodes |
| `0x04` | 4 | `s_blocks_count_lo` | Low 32 bits of block count |
| `0x08` | 4 | `s_r_blocks_count_lo` | Reserved blocks |
| `0x0C` | 4 | `s_free_blocks_count_lo` | Free blocks |
| `0x10` | 4 | `s_free_inodes_count` | Free inodes |
| `0x14` | 4 | `s_first_data_block` | 0 or 1 |
| `0x18` | 4 | `s_log_block_size` | Block size shift |
| `0x1C` | 4 | `s_log_cluster_size` | Cluster shift (bigalloc) |
| `0x20` | 4 | `s_blocks_per_group` | Blocks per group |
| `0x24` | 4 | `s_clusters_per_group` | Clusters per group |
| `0x28` | 4 | `s_inodes_per_group` | Inodes per group |
| `0x34` | 2 | `s_mnt_count` | Mount count |
| `0x36` | 2 | `s_max_mnt_count` | Max mount count |
| `0x38` | 2 | `s_magic` | `0xEF53` |
| `0x3A` | 2 | `s_state` | Clean/error/orphan bits |
| `0x3C` | 2 | `s_errors` | Error behaviour |
| `0x3E` | 2 | `s_minor_rev_level` | Minor revision |
| `0x40` | 4 | `s_lastcheck` | Last fsck time |
| `0x44` | 4 | `s_checkinterval` | fsck interval |
| `0x48` | 4 | `s_creator_os` | 0 Linux, 1 Hurd, 2 Masix, 3 FreeBSD, 4 Lites |
| `0x4C` | 4 | `s_rev_level` | 0 original, 1 dynamic |
| `0x50` | 2 | `s_def_resuid` | Default reserved uid |
| `0x52` | 2 | `s_def_resgid` | Default reserved gid |
| `0x54` | 4 | `s_first_ino` | First non-reserved inode |
| `0x58` | 2 | `s_inode_size` | Inode size (128, 256, …) |
| `0x5A` | 2 | `s_block_group_nr` | Group of this superblock copy |
| `0x5C` | 4 | `s_feature_compat` | Compatible features |
| `0x60` | 4 | `s_feature_incompat` | Incompatible features |
| `0x64` | 4 | `s_feature_ro_compat` | Read-only-compatible features |
| `0x68` | 16 | `s_uuid` | Volume UUID |
| `0x78` | 16 | `s_volume_name` | Label, NUL-padded |
| `0x88` | 64 | `s_last_mounted` | Last mount path |
| `0xC8` | 4 | `s_algorithm_usage_bitmap` | Compression |
| `0xCC` | 1 | `s_prealloc_blocks` | Preallocation |
| `0xCD` | 1 | `s_prealloc_dir_blocks` | Directory preallocation |
| `0xCE` | 2 | `s_reserved_gdt_blocks` | Reserved GDT blocks |
| `0xD0` | 16 | `s_journal_uuid` | Journal UUID |
| `0xE0` | 4 | `s_journal_inum` | Internal journal inode |
| `0xE4` | 4 | `s_journal_dev` | External journal device |
| `0xE8` | 4 | `s_last_orphan` | Orphan inode list head |
| `0xEC` | 16 | `s_hash_seed` | HTREE hash seed |
| `0xFC` | 1 | `s_def_hash_version` | HTREE hash version |
| `0xFD` | 1 | `s_jnl_backup_type` | Journal backup type |
| `0xFE` | 2 | `s_desc_size` | Group descriptor size |
| `0x100` | 4 | `s_default_mount_opts` | Default mount options |
| `0x104` | 4 | `s_first_meta_bg` | First meta block group |
| `0x108` | 4 | `s_mkfs_time` | Creation time |
| `0x10C` | 68 | `s_jnl_blocks` | Journal backup |
| `0x150` | 4 | `s_blocks_count_hi` | High 32 bits |
| `0x154` | 4 | `s_r_blocks_count_hi` | High 32 bits |
| `0x158` | 4 | `s_free_blocks_count_hi` | High 32 bits |
| `0x15C` | 2 | `s_min_extra_isize` | Minimum extra inode size |
| `0x15E` | 2 | `s_want_extra_isize` | Wanted extra inode size |
| `0x160` | 4 | `s_flags` | Filesystem flags |
| `0x164` | 2 | `s_raid_stride` | RAID stride |
| `0x166` | 2 | `s_mmp_interval` | MMP interval |
| `0x168` | 8 | `s_mmp_block` | MMP block |
| `0x170` | 4 | `s_raid_stripe_width` | RAID stripe width |
| `0x174` | 1 | `s_log_groups_per_flex` | Flex group shift |
| `0x175` | 1 | `s_checksum_type` | 1 = crc32c |
| `0x176` | 1 | `s_encryption_level` | Encryption |
| `0x178` | 8 | `s_kbytes_written` | Lifetime write counter |
| `0x194` | 4 | `s_error_count` | Error count |
| `0x200` | 64 | `s_mount_opts` | Last mount options |
| `0x240` | 4 | `s_usr_quota_inum` | User quota inode |
| `0x244` | 4 | `s_grp_quota_inum` | Group quota inode |
| `0x248` | 4 | `s_overhead_blocks` | Overhead blocks |
| `0x24C` | 8 | `s_backup_bgs[2]` | Sparse superblock 2 backups |
| `0x254` | 4 | `s_encrypt_algos[4]` | Encryption algorithms |
| `0x258` | 16 | `s_encrypt_pw_salt` | Encryption salt |
| `0x268` | 4 | `s_lpf_ino` | Lost+found inode |
| `0x26C` | 4 | `s_prj_quota_inum` | Project quota inode |
| `0x270` | 4 | `s_checksum_seed` | Metadata checksum seed |
| `0x274` | 1 | `s_wtime_hi` | Write time high bits |
| `0x275` | 1 | `s_mtime_hi` | Mount time high bits |
| `0x276` | 1 | `s_mkfs_time_hi` | Creation time high bits |
| `0x277` | 1 | `s_lastcheck_hi` | Last check high bits |
| `0x278` | 1 | `s_first_error_time_hi` | First error time high bits |
| `0x279` | 1 | `s_last_error_time_hi` | Last error time high bits |
| `0x27A` | 1 | `s_first_error_errcode` | First error code |
| `0x27B` | 1 | `s_last_error_errcode` | Last error code |
| `0x27C` | 2 | `s_encoding` | Casefold encoding |
| `0x27E` | 2 | `s_encoding_flags` | Casefold flags |
| `0x280` | 4 | `s_orphan_file_inum` | Orphan file inode |
| `0x284` | 376 | `s_reserved[94]` | Reserved |
| `0x3FC` | 4 | `s_checksum` | Superblock crc32c |

The table is a navigation aid, not a complete structure definition. Reserved
areas and newer fields must be read from the specification for the feature set
being implemented.

### 2.2 State and revision

`s_state` bits:

| Bit | Name | Meaning |
| --- | --- | --- |
| `0x0001` | `EXT4_VALID_FS` | Cleanly unmounted |
| `0x0002` | `EXT4_ERROR_FS` | Errors detected |
| `0x0004` | `EXT4_ORPHAN_FS` | Orphan processing needed |

`s_errors`: 1 = continue, 2 = remount read-only, 3 = panic.

`s_rev_level`: 0 = original (fixed 128-byte inodes), 1 = dynamic. Modern
filesystems use 1.

### 2.3 Validation

The engine rejects the superblock if any of the following is true:

- magic is not `0xEF53`;
- `s_log_block_size > 6`, or the resulting block size exceeds the configured
  maximum;
- block size is smaller than 1024;
- `s_blocks_count == 0` or exceeds the device size;
- `s_inodes_count == 0` or exceeds a sane cap for the volume;
- `s_blocks_per_group == 0` or `s_inodes_per_group == 0`;
- `s_inode_size < 128`, not a power of two, or larger than the block size;
- `s_desc_size` is invalid for the selected feature set;
- a 64-bit field overflows when combined with its low half;
- the group count computed from geometry exceeds a configured cap;
- the checksum (when `metadata_csum` is set) does not match;
- the feature policy rejects any incompatible or read-only-compatible bit.

The primary superblock is authoritative. Backup superblocks are not used as a
silent fallback: choosing an old backup can resurrect stale metadata. A future
recovery path may use a backup only with an explicit user action and a full
explanation.

---

## 3. Feature flags

This is the initial policy. The authoritative implementation is the engine's
policy table; this document and
[`FILESYSTEM-COMPATIBILITY.md`](FILESYSTEM-COMPATIBILITY.md) must stay in sync.

### 3.1 Compatible (`s_feature_compat`)

| Bit | Name | Initial policy |
| --- | --- | --- |
| `0x0001` | `DIR_PREALLOC` | Read; write support later |
| `0x0002` | `IMAGIC_INODES` | Reject |
| `0x0004` | `HAS_JOURNAL` | Read; required policy for write |
| `0x0008` | `EXT_ATTR` | Read |
| `0x0010` | `RESIZE_INODE` | Read; never resize |
| `0x0020` | `DIR_INDEX` | Read (HTREE) |
| `0x0040` | `LAZY_BG` | Reject (not used by Linux/e2fsprogs) |
| `0x0080` | `EXCLUDE_INODE` | Reject (not used) |
| `0x0100` | `EXCLUDE_BITMAP` | Reject (not used) |
| `0x0200` | `SPARSE_SUPER2` | Read; validate backup groups |
| `0x0400` | `FAST_COMMIT` | Reject until understood |
| `0x0800` | `STABLE_INODES` | Reject until understood |
| `0x1000` | `ORPHAN_FILE` | Read-only only if `orphan_present` is clear; never write |

Unknown compatible bits are ignored **only** if their semantics are documented
not to change the layout. In practice, the policy is allowlist-based: an
unknown bit is a refusal unless it is explicitly known to be safe.

### 3.2 Incompatible (`s_feature_incompat`)

| Bit | Name | Initial policy |
| --- | --- | --- |
| `0x0001` | `COMPRESSION` | Reject |
| `0x0002` | `FILETYPE` | Support |
| `0x0004` | `RECOVER` | Reject mount until journal replay is proven |
| `0x0008` | `JOURNAL_DEV` | Reject |
| `0x0010` | `META_BG` | Reject |
| `0x0040` | `EXTENTS` | Support |
| `0x0080` | `64BIT` | Support |
| `0x0100` | `MMP` | Reject |
| `0x0200` | `FLEX_BG` | Support |
| `0x0400` | `EA_INODE` | Reject initial read-only |
| `0x1000` | `DIRDATA` | Reject |
| `0x2000` | `CSUM_SEED` | Support |
| `0x4000` | `LARGEDIR` | Reject until HTREE depth handling is proven |
| `0x8000` | `INLINE_DATA` | Reject |
| `0x10000` | `ENCRYPT` | Reject |
| `0x20000` | `CASEFOLD` | Reject |

Unknown incompatible bits **always** cause refusal. This is non-negotiable: an
unknown incompatible feature can change the meaning of every block on the
volume.

### 3.3 Read-only compatible (`s_feature_ro_compat`)

| Bit | Name | Initial policy |
| --- | --- | --- |
| `0x0001` | `SPARSE_SUPER` | Support |
| `0x0002` | `LARGE_FILE` | Support |
| `0x0004` | `BTREE_DIR` | Ignore (historical; not used by kernel/e2fsprogs) |
| `0x0008` | `HUGE_FILE` | Support |
| `0x0080` | `HAS_SNAPSHOT` | Reject (snapshots not supported) |
| `0x0010` | `GDT_CSUM` | Support (legacy checksum) |
| `0x0020` | `DIR_NLINK` | Support |
| `0x0040` | `EXTRA_ISIZE` | Support |
| `0x0100` | `QUOTA` | Read-only; refuse write until quota is understood |
| `0x0200` | `BIGALLOC` | Reject |
| `0x0400` | `METADATA_CSUM` | Support |
| `0x0800` | `REPLICA` | Reject |
| `0x1000` | `READONLY` | Read-only; never write |
| `0x2000` | `PROJECT` | Read-only; refuse write |
| `0x8000` | `VERITY` | Reject |
| `0x10000` | `ORPHAN_PRESENT` | Refuse even read-only until orphan handling is proven |

A read-only-compatible feature may be safe for a read-only mount but unsafe for
a write mount. The policy therefore has two verdicts per feature:

- `READ_OK`
- `READ_WRITE_OK`

Anything not explicitly `READ_WRITE_OK` forces a read-only mount or refusal.

---

## 4. Block group descriptors

The group descriptor table starts at block `s_first_data_block + 1` for the
primary copy, unless `meta_bg` changes the layout. With `sparse_super`, backup
copies are stored only in selected groups (0, 1, powers of 3, 5, 7, and the
last group, with `sparse_super2` using `s_backup_bgs`).

### 4.1 Legacy 32-byte descriptor

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `bg_block_bitmap_lo` |
| `0x04` | 4 | `bg_inode_bitmap_lo` |
| `0x08` | 4 | `bg_inode_table_lo` |
| `0x0C` | 2 | `bg_free_blocks_count_lo` |
| `0x0E` | 2 | `bg_free_inodes_count_lo` |
| `0x10` | 2 | `bg_used_dirs_count_lo` |
| `0x12` | 2 | `bg_flags` |
| `0x14` | 4 | `bg_exclude_bitmap_lo` |
| `0x16` | 2 | `bg_block_bitmap_csum_lo` |
| `0x18` | 2 | `bg_inode_bitmap_csum_lo` |
| `0x1A` | 2 | `bg_itable_unused_lo` |
| `0x1C` | 2 | `bg_checksum` |

### 4.2 64-bit extension

When `64BIT` is set and `s_desc_size >= 64`:

| Offset | Size | Field |
| --- | --- | --- |
| `0x20` | 4 | `bg_block_bitmap_hi` |
| `0x24` | 4 | `bg_inode_bitmap_hi` |
| `0x28` | 4 | `bg_inode_table_hi` |
| `0x2C` | 2 | `bg_free_blocks_count_hi` |
| `0x2E` | 2 | `bg_free_inodes_count_hi` |
| `0x30` | 2 | `bg_used_dirs_count_hi` |
| `0x32` | 2 | `bg_itable_unused_hi` |
| `0x34` | 4 | `bg_exclude_bitmap_hi` |
| `0x36` | 2 | `bg_block_bitmap_csum_hi` |
| `0x38` | 2 | `bg_inode_bitmap_csum_hi` |
| `0x3A` | 2 | reserved |

`bg_flags` bits include `INODE_UNINIT` (`0x1`), `BLOCK_UNINIT` (`0x2`), and
`INODE_ZEROED` (`0x4`). Uninitialised bitmaps are treated as all-zero unless
the feature and checksum rules say otherwise.

### 4.3 Placement

For each group, the block bitmap, inode bitmap, and inode table block numbers
are formed from low and high halves. The engine checks:

- each block number is within the volume;
- the three regions do not overlap each other or the superblock/GDT;
- the inode table spans `ceil(inodes_per_group * inode_size / block_size)`
  blocks and fits within the volume;
- flex group placement is consistent (`flex_bg`) and does not overlap
  metadata of another group;
- uninitialised groups are handled only when the feature says they are empty.

---

## 5. Inodes

Inode `N` (1-based) lives in group `(N - 1) / s_inodes_per_group`, index
`(N - 1) % s_inodes_per_group`, inside that group's inode table. Inode 0 does
not exist. Inode 1 is the bad-blocks inode, inode 2 is the root directory.

### 5.1 Base inode (128 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 2 | `i_mode` |
| `0x02` | 2 | `i_uid` |
| `0x04` | 4 | `i_size_lo` |
| `0x08` | 4 | `i_atime` |
| `0x0C` | 4 | `i_ctime` |
| `0x10` | 4 | `i_mtime` |
| `0x14` | 4 | `i_dtime` |
| `0x16` | 2 | `i_gid` |
| `0x18` | 2 | `i_links_count` |
| `0x1C` | 4 | `i_blocks_lo` (512-byte sectors) |
| `0x20` | 4 | `i_flags` |
| `0x24` | 4 | `i_osd1` |
| `0x28` | 60 | `i_block[15]` |
| `0x64` | 4 | `i_generation` |
| `0x68` | 4 | `i_file_acl_lo` |
| `0x6C` | 4 | `i_size_high` |
| `0x70` | 4 | `i_obso_faddr` |
| `0x74` | 12 | OS-dependent 2 |

The Linux OS-dependent 2 area:

| Offset | Size | Field |
| --- | --- | --- |
| `0x74` | 2 | `l_i_blocks_high` |
| `0x76` | 2 | `l_i_file_acl_high` |
| `0x78` | 2 | `l_i_uid_high` |
| `0x7A` | 2 | `l_i_gid_high` |
| `0x7C` | 2 | `l_i_checksum_lo` |
| `0x7E` | 2 | reserved |

### 5.2 Extra inode fields

When `s_inode_size > 128` and `i_extra_isize` is valid:

| Offset | Size | Field |
| --- | --- | --- |
| `0x80` | 2 | `i_extra_isize` |
| `0x82` | 2 | `i_checksum_hi` |
| `0x84` | 4 | `i_ctime_extra` |
| `0x88` | 4 | `i_mtime_extra` |
| `0x8C` | 4 | `i_atime_extra` |
| `0x90` | 4 | `i_crtime` |
| `0x94` | 4 | `i_crtime_extra` |
| `0x98` | 4 | `i_version_hi` |
| `0x9C` | 4 | `i_projid` |

The extra timestamp fields encode nanoseconds and epoch bits. The engine caps
`i_extra_isize` to the inode size minus 128 and rejects a value that points
outside the inode.

### 5.3 File mode and types

`i_mode` uses the POSIX layout: type bits `0xF000` and permission bits `0x0FFF`.

| Type | Value |
| --- | --- |
| FIFO | `0x1000` |
| Character device | `0x2000` |
| Directory | `0x4000` |
| Block device | `0x6000` |
| Regular file | `0x8000` |
| Symlink | `0xA000` |
| Socket | `0xC000` |

### 5.4 Inode flags

Important `i_flags` bits:

| Bit | Name | Meaning |
| --- | --- | --- |
| `0x00000008` | `SYNC` | Synchronous updates |
| `0x00000010` | `IMMUTABLE` | Cannot change |
| `0x00000020` | `APPEND` | Append-only |
| `0x00000080` | `NOATIME` | Do not update atime |
| `0x00001000` | `INDEX` | Directory has HTREE |
| `0x00010000` | `DIRSYNC` | Directory sync |
| `0x00040000` | `HUGE_FILE` | `i_blocks` is in filesystem blocks |
| `0x00080000` | `EXTENTS` | `i_block` is an extent tree |
| `0x00100000` | `VERITY` | Verity-protected file |
| `0x00200000` | `EA_INODE` | Inode stores a large xattr value |
| `0x00400000` | `EOFBLOCKS` | Blocks past EOF are allocated (deprecated) |
| `0x10000000` | `INLINE_DATA` | Data stored in inode/iblock |
| `0x20000000` | `PROJINHERIT` | Project ID inheritance |
| `0x40000000` | `CASEFOLD` | Case-insensitive directory |

The engine rejects inodes with `INLINE_DATA` or `CASEFOLD` under the initial
policy. It treats `EXTENTS` as mandatory for regular files in the initial
target set; a file without it uses the legacy block map, which is not in the
initial read path and must be refused explicitly rather than misread.

### 5.5 Size and blocks

`i_size` is `(i_size_high << 32) | i_size_lo` for regular files when
`LARGE_FILE` is set. `i_blocks` normally counts 512-byte sectors. When the
`HUGE_FILE` read-only-compatible feature is set, `i_blocks_high` extends the
count. If the inode also has the `HUGE_FILE` inode flag, the value is in
filesystem blocks rather than 512-byte sectors. The engine must decode all
three cases exactly; getting this wrong corrupts size and allocation
accounting.

For directories, `i_size` is the directory data size. A directory whose size is
not a multiple of the block size is suspicious and must be bounded, not
trusted.

---

## 6. Extent trees

When `i_flags & EXT4_EXTENTS_FL`, `i_block[15]` holds an extent tree. A leaf
extent maps a logical block range to a physical block range.

### 6.1 Extent header (12 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 2 | `eh_magic` = `0xF30A` |
| `0x02` | 2 | `eh_entries` |
| `0x04` | 2 | `eh_max` |
| `0x06` | 2 | `eh_depth` |
| `0x08` | 4 | `eh_generation` |

`eh_depth` is 0 for a leaf, up to 5 for an index tree. `eh_entries <= eh_max`.
The maximum node capacity is `(block_size - 12) / 12` for both index and leaf
nodes, minus the checksum tail when `metadata_csum` is enabled.

### 6.2 Index node (12 bytes per entry)

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `ei_block` (logical block covered) |
| `0x04` | 4 | `ei_leaf_lo` |
| `0x08` | 2 | `ei_leaf_hi` |
| `0x0A` | 2 | unused |

Child block = `(ei_leaf_hi << 32) | ei_leaf_lo`.

### 6.3 Leaf extent (12 bytes per entry)

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `ee_block` (first logical block) |
| `0x04` | 2 | `ee_len` |
| `0x06` | 2 | `ee_start_hi` |
| `0x08` | 4 | `ee_start_lo` |

Physical start = `(ee_start_hi << 32) | ee_start_lo`.

`ee_len` semantics:

- `0..32768`: initialised extent of that many blocks;
- `32769..65535`: unwritten extent of `ee_len - 32768` blocks;
- `0` is invalid for a real extent.

Reads of unwritten extents return zeroes. The engine must never expose stale
disk contents from an unwritten extent.

### 6.4 Checksum tail

With `metadata_csum`, each extent block ends with a tail:

| Offset | Size | Field |
| --- | --- | --- |
| end-4 | 4 | `eb_checksum` |

The tail occupies space that would otherwise be an entry. `eh_max` and the
checksum tail must agree; a node that claims more entries than fit before the
tail is corrupt.

### 6.5 Validation

- `eh_magic` must be `0xF30A`.
- `eh_depth <= 5`.
- `eh_entries <= eh_max`.
- `eh_max` must be plausible for the block size and checksum mode.
- Index entries must be sorted by `ei_block`; duplicates are corruption.
- Leaf extents must be sorted, non-overlapping, and within `i_size` bounds
  where applicable.
- Every physical block must be within the volume.
- No extent may cover a block that the group descriptors identify as metadata
  for a file that is not allowed to reference it (the allocator must enforce
  this; the read path checks bounds at minimum).
- Traversal tracks visited blocks to detect cycles, with a total-node cap.
- A physical range that wraps or overflows is rejected.

---

## 7. Directories

### 7.1 `ext4_dir_entry_2`

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `inode` |
| `0x04` | 2 | `rec_len` |
| `0x06` | 1 | `name_len` |
| `0x07` | 1 | `file_type` |
| `0x08` | name_len | `name` |

Rules:

- `rec_len >= 8`, multiple of 4, and fits in the remaining block.
- `name_len <= rec_len - 8`.
- `inode` is either 0 (unused entry) or within `s_inodes_count`.
- `name` contains no `/` and no NUL; it is not NUL-terminated on disk.
- File types: 0 unknown, 1 regular, 2 directory, 3 char device, 4 block
  device, 5 FIFO, 6 socket, 7 symlink.
- When `FILETYPE` is not set, the type field is a name-length high byte in the
  legacy format; the initial target set requires `FILETYPE`.

The last entry in a block may have a `rec_len` that extends to the end of the
block even if the name is shorter. This is normal. The engine must still
validate that the record does not cross the block boundary.

### 7.2 HTREE (`dir_index`)

A large directory has `i_flags & EXT4_INDEX_FL` and a hash tree in its first
block:

```text
+----------------------+
| "." dirent           |
| ".." dirent          |
| dx_root info         |
| dx_countlimit        |
| dx_entry[0..count-1] |
| ...                  |
+----------------------+
```

- `dx_root` contains a reserved 32-bit field, hash version, info length,
  indirect levels, and flags.
- `dx_countlimit` contains `limit` and `count` (16-bit each).
- Each `dx_entry` is `hash` (32-bit) and `block` (32-bit).
- Interior nodes repeat a countlimit and entries; leaves contain normal
  directory entries.
- `metadata_csum` adds a `dx_tail` with a checksum.

Initial read support may enumerate the tree, but must also handle a directory
that is marked indexed yet whose tree is inconsistent: refuse the directory
rather than return a partial listing as complete. A linear fallback is allowed
only when it is provably equivalent and is recorded as a recovery path.

### 7.3 Directory semantics

- `.` must refer to the directory itself; `..` must refer to its parent (except
  at the root, where `..` refers to the root).
- Duplicate names in one directory are corruption.
- Names are bytes. Windows conversion is a front-end concern; see
  [`FILESYSTEM-COMPATIBILITY.md`](FILESYSTEM-COMPATIBILITY.md).
- Enumeration must be restartable and must not skip or duplicate entries when
  the caller's buffer is too small.

---

## 8. Extended attributes

Extended attributes may be stored:

1. in the inode's extra space, beginning at `0x80 + i_extra_isize`, when that
   space is large enough and the magic matches;
2. in the block referenced by `i_file_acl` (and `i_file_acl_high`).

### 8.1 Headers

An **in-inode** attribute area starts with a 4-byte header:

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `h_magic` = `0xEA020000` |

Entries begin immediately after that 4-byte header. The value offsets are
relative to the start of the first entry, not to the start of the inode.

An **external block** starts with a 32-byte header:

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 4 | `h_magic` = `0xEA020000` |
| `0x04` | 4 | `h_refcount` |
| `0x08` | 4 | `h_blocks` |
| `0x0C` | 4 | `h_hash` |
| `0x10` | 4 | `h_checksum` (metadata_csum) |
| `0x14` | 12 | reserved (zero) |

The two layouts are not interchangeable. Treating an in-inode area as a
32-byte block header reads past the magic into the first entry.

### 8.2 Entry

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | 1 | `e_name_len` |
| `0x01` | 1 | `e_name_index` |
| `0x02` | 2 | `e_value_offs` |
| `0x04` | 4 | `e_value_inum` |
| `0x08` | 4 | `e_value_size` |
| `0x0C` | 4 | `e_hash` |
| `0x10` | name_len | name |

Value offsets are relative to the start of the entry table for in-inode
attributes, and to the start of the block for external attributes. Values may
also live in the inode identified by `e_value_inum` when `EA_INODE` is enabled;
the initial policy rejects `EA_INODE`.

Name indexes include user (`1`), POSIX ACL access (`2`), POSIX ACL default
(`3`), trusted (`4`), lustre (`5`), security (`6`), system (`7`), and
system rich ACL (`8`). The front end uses `system.posix_acl_*` and
`security.selinux` only for metadata translation; it does not expose raw xattr
editing before write support.

### 8.3 Validation

- magic must match;
- `h_blocks` must be 1 for an external block (the initial policy does not
  support multi-block xattrs);
- `h_refcount` must be sane (shared xattr blocks are an advanced feature);
- every entry offset and value range must stay inside the block;
- names must not contain NUL or `/`;
- duplicate names are corruption;
- checksum must verify when `metadata_csum` is set.

---

## 9. Metadata checksums

`metadata_csum` replaces the legacy group-descriptor checksum with CRC32C
(Castagnoli). The checksum seed is:

```text
if CSUM_SEED:  s_checksum_seed
else:          crc32c(0xFFFFFFFF, s_uuid[0..15])
```

### 9.1 Superblock

`crc32c(seed=0xFFFFFFFF, bytes 0x000..0x3FB)` written to `s_checksum` at
`0x3FC`. The checksum field itself is excluded from the calculation.

### 9.2 Group descriptor

```text
crc = crc32c(seed, group_number as little-endian u32)
crc = crc32c(crc, descriptor bytes with bg_checksum zeroed)
```

The legacy `GDT_CSUM` feature uses CRC16 over the UUID and descriptor instead.
A volume may have one or the other, not both.

### 9.3 Inode

```text
crc = crc32c(seed, inode_number as little-endian u32)
crc = crc32c(crc, i_generation as little-endian u32)
crc = crc32c(crc, inode bytes with i_checksum_lo/hi zeroed)
```

The low 16 bits are stored in `l_i_checksum_lo`; when the inode is large
enough, the high 16 bits are stored in `i_checksum_hi`. The engine must know
whether the high field exists before comparing.

### 9.4 Extent block

```text
crc = crc32c(seed, inode_number)
crc = crc32c(crc, i_generation)
crc = crc32c(crc, bytes before the 4-byte eb_checksum)
```

### 9.5 Directory block

There are two tail formats:

**Linear leaf block** — a 12-byte `ext4_dir_entry_tail` at the end:

```text
det_reserved_zero1 = 0
det_rec_len        = 12
det_reserved_zero2 = 0
det_reserved_ft    = 0xDE
det_checksum       = crc32c(seed, ino, gen, bytes before the tail)
```

**HTREE index block** — an 8-byte `dx_tail` at the end:

```text
dt_reserved = included in the checksum
dt_checksum = crc32c(seed, ino, gen, block bytes up to dt_checksum,
                     with dt_checksum treated as zero)
```

Do not apply the linear tail formula to an index block or the index formula to
a leaf. The directory's `EXT4_INDEX_FL` and the block's role determine which
one applies.

### 9.6 Extended attribute block

```text
crc = crc32c(seed, block_number as little-endian u64)
crc = crc32c(crc, block bytes with h_checksum treated as zero)
```

In-inode attributes are covered by the inode checksum; they do not have a
separate block checksum.

### 9.7 Bitmaps

```text
block bitmap: crc32c(seed, bitmap, clusters_per_group / 8)
inode bitmap: crc32c(seed, bitmap, inodes_per_group / 8)
```

The low 16 bits go in `bg_*_bitmap_csum_lo`; the high 16 bits go in the 64-bit
descriptor extension when present.

### 9.8 Failure policy

A checksum mismatch is **corruption**, not a warning. The engine:

- refuses to use the structure;
- returns an I/O or corruption status;
- logs the structure and block/inode number;
- never "repairs" it silently;
- never falls back to a stale backup without an explicit user action.

---

## 10. Journal (JBD2)

See [`JOURNAL-IMPLEMENTATION.md`](JOURNAL-IMPLEMENTATION.md) for the full plan.
Key facts:

- The journal is an inode (`s_journal_inum`, usually 8) or an external device.
- Its first block is a JBD2 superblock with magic `0xC03B3998`.
- Blocks are typed as descriptor, commit, revoke, or data.
- `s_start` and `s_sequence` define the replay window.
- `INCOMPAT_RECOVER` / `needs_recovery` mean the journal must be replayed before
  the file system can be trusted.
- `orphan_present` means unlinked-but-open inodes need processing.

The initial driver refuses to write to any volume with pending journal work and
refuses even a read-only mount when the state cannot be proven consistent.

---

## 11. Other structures and features

### 11.1 Symlinks

- Fast symlink: target stored in `i_block` if the target fits and no extent
  flag is set.
- Slow symlink: target stored in data blocks.
- The engine must distinguish them correctly and bound the target length.

### 11.2 Special files

Device nodes, FIFOs, and sockets have no Windows equivalent. The planned
front end exposes them as inert files or refuses access; it never creates a
Windows device from untrusted on-disk major/minor numbers.

### 11.3 Sparse files and holes

A hole is a logical range with no mapped extent. Reads return zeroes. Writes
must allocate blocks and update the extent tree and checksums atomically.

### 11.4 Orphan file / orphan list

Modern `mke2fs` may create an orphan file (`ORPHAN_FILE`, inode
`s_orphan_file_inum`). An unclean volume may also have a non-zero
`s_last_orphan`. The initial driver does not process orphans. It refuses a
write mount when either mechanism indicates pending work.

### 11.5 Quotas and project quotas

Quota inodes and project IDs are parsed for metadata only. The initial driver
does not enforce quotas and refuses write mounts on quota-enabled volumes until
the accounting rules are implemented and tested.

### 11.6 Encryption, verity, casefold

`encrypt`, `verity`, and `casefold` change name or data semantics in ways
Windows cannot represent without a deliberate design. They are rejected at
mount time. The driver never returns ciphertext or raw hashes as if they were
plaintext.

---

## 12. Parser implementation order

1. Fixed-width little-endian readers with overflow checks.
2. Superblock decode and validation.
3. Group descriptor table and geometry.
4. Block and inode bitmap views.
5. Inode decode and checksum.
6. Extent tree traversal.
7. Directory entry decode and linear enumeration.
8. HTREE traversal.
9. Extended attributes.
10. Journal parsing (read-only).
11. Journal replay (scratch image only).
12. Allocation and mutation.

Each step lands with host tests and a corrupt-input corpus before the next
depends on it. A parser that "works on my image" is not accepted.

---

## 13. References

- Linux kernel documentation: `Documentation/filesystems/ext4/` (on-disk
  format). Used as a specification; no source code copied.
- `e2fsprogs` behaviour is the compatibility reference for generated images and
  `e2fsck -fn` verification.
- The engine's policy table is authoritative for which features are accepted.
  This document must be updated when the policy changes.
