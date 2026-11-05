/*++
Module Name:
    stgr_ext4.h

Abstract:
    Portable EXT4 engine: superblock recognition and feature policy.

    Milestone 2 scope: read and validate the primary superblock of a supported,
    approved volume and refuse everything else. This header deliberately has no
    notion of mounting, inodes, directories, or caching.

Environment:
    Host tests and kernel context, PASSIVE_LEVEL only.

License:
    SPDX-License-Identifier: MIT
--*/

#ifndef STGR_EXT4_H
#define STGR_EXT4_H

#include "stgr_ext4_types.h"

#define STGR_EXT4_SUPERBLOCK_OFFSET 1024u
#define STGR_EXT4_SUPERBLOCK_SIZE 1024u
#define STGR_EXT4_MAGIC 0xEF53u

//
// Feature bits. Names follow the kernel on-disk specification; the policy in
// StgrExt4CheckFeaturePolicy is the authority, and
// docs/FILESYSTEM-COMPATIBILITY.md must be updated in the same commit as any
// change here.
//
#define STGR_FEATURE_COMPAT_DIR_PREALLOC 0x0001u
#define STGR_FEATURE_COMPAT_IMAGIC_INODES 0x0002u
#define STGR_FEATURE_COMPAT_HAS_JOURNAL 0x0004u
#define STGR_FEATURE_COMPAT_EXT_ATTR 0x0008u
#define STGR_FEATURE_COMPAT_RESIZE_INODE 0x0010u
#define STGR_FEATURE_COMPAT_DIR_INDEX 0x0020u
#define STGR_FEATURE_COMPAT_SPARSE_SUPER2 0x0200u
#define STGR_FEATURE_COMPAT_FAST_COMMIT 0x0400u
#define STGR_FEATURE_COMPAT_STABLE_INODES 0x0800u
#define STGR_FEATURE_COMPAT_ORPHAN_FILE 0x1000u

#define STGR_FEATURE_INCOMPAT_COMPRESSION 0x0001u
#define STGR_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define STGR_FEATURE_INCOMPAT_RECOVER 0x0004u
#define STGR_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008u
#define STGR_FEATURE_INCOMPAT_META_BG 0x0010u
#define STGR_FEATURE_INCOMPAT_EXTENTS 0x0040u
#define STGR_FEATURE_INCOMPAT_64BIT 0x0080u
#define STGR_FEATURE_INCOMPAT_MMP 0x0100u
#define STGR_FEATURE_INCOMPAT_FLEX_BG 0x0200u
#define STGR_FEATURE_INCOMPAT_EA_INODE 0x0400u
#define STGR_FEATURE_INCOMPAT_DIRDATA 0x1000u
#define STGR_FEATURE_INCOMPAT_CSUM_SEED 0x2000u
#define STGR_FEATURE_INCOMPAT_LARGEDIR 0x4000u
#define STGR_FEATURE_INCOMPAT_INLINE_DATA 0x8000u
#define STGR_FEATURE_INCOMPAT_ENCRYPT 0x10000u
#define STGR_FEATURE_INCOMPAT_CASEFOLD 0x20000u

#define STGR_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001u
#define STGR_FEATURE_RO_COMPAT_LARGE_FILE 0x0002u
#define STGR_FEATURE_RO_COMPAT_BTREE_DIR 0x0004u
#define STGR_FEATURE_RO_COMPAT_HUGE_FILE 0x0008u
#define STGR_FEATURE_RO_COMPAT_GDT_CSUM 0x0010u
#define STGR_FEATURE_RO_COMPAT_DIR_NLINK 0x0020u
#define STGR_FEATURE_RO_COMPAT_EXTRA_ISIZE 0x0040u
#define STGR_FEATURE_RO_COMPAT_QUOTA 0x0100u
#define STGR_FEATURE_RO_COMPAT_BIGALLOC 0x0200u
#define STGR_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400u
#define STGR_FEATURE_RO_COMPAT_REPLICA 0x0800u
#define STGR_FEATURE_RO_COMPAT_READONLY 0x1000u
#define STGR_FEATURE_RO_COMPAT_PROJECT 0x2000u
#define STGR_FEATURE_RO_COMPAT_SHARED_BLOCKS 0x4000u
#define STGR_FEATURE_RO_COMPAT_VERITY 0x8000u
#define STGR_FEATURE_RO_COMPAT_ORPHAN_PRESENT 0x10000u

//
// s_state bits.
//
#define STGR_SUPER_STATE_VALID_FS 0x0001u
#define STGR_SUPER_STATE_ERROR_FS 0x0002u
#define STGR_SUPER_STATE_ORPHAN_FS 0x0004u

//
// Which feature set a refusal referred to, for diagnostics and tests.
//
typedef enum _STGR_FEATURE_SET
{
    StgrFeatureSetNone = 0,
    StgrFeatureSetCompat = 1,
    StgrFeatureSetIncompat = 2,
    StgrFeatureSetRoCompat = 3
} STGR_FEATURE_SET;

//
// Limits applied on top of the on-disk geometry. The defaults support 1 KiB,
// 2 KiB, and 4 KiB block sizes; larger blocks are a later milestone and are
// refused rather than half-supported.
//
typedef struct _STGR_ENGINE_CONFIG
{
    uint32_t MaxBlockSize;
    uint32_t MaxGroupCount;
    uint32_t MaxInodeCount;
} STGR_ENGINE_CONFIG;

//
// Decoded and validated superblock. Everything here has already passed the
// range checks; consumers must still treat the values as untrusted with
// respect to each other (for example, group descriptors on disk remain
// unverified).
//
typedef struct _STGR_SUPERBLOCK
{
    uint32_t InodesCount;
    uint32_t FreeInodesCount;
    uint64_t BlocksCount;
    uint64_t FreeBlocksCount;
    uint32_t FirstDataBlock;
    uint32_t BlockSize;
    uint32_t BlocksPerGroup;
    uint32_t InodesPerGroup;
    uint32_t GroupCount;
    uint32_t InodeSize;
    uint32_t DescriptorSize;
    uint32_t RevisionLevel;
    uint32_t CompatFeatures;
    uint32_t IncompatFeatures;
    uint32_t RoCompatFeatures;
    uint32_t ChecksumSeed;
    uint16_t State;
    uint16_t ErrorsBehavior;
    uint8_t Uuid[16];
    char VolumeLabel[17];
} STGR_SUPERBLOCK;

//
// Defaults for a read-only recognition pass.
//
STGR_ENGINE_CONFIG
StgrExt4DefaultConfig(
    void
    );

//
// Read the primary superblock through the block device and validate it.
//
STGR_STATUS
StgrExt4ReadSuperblock(
    const STGR_BLOCK_IO *Io,
    const STGR_ENGINE_CONFIG *Config,
    STGR_SUPERBLOCK *Superblock
    );

//
// Validate and decode a 1024-byte superblock image. Exposed so host tests can
// drive the parser without a device. DeviceSizeBytes of 0 means "unknown
// size": the size check is then skipped, which tests use for geometry cases
// that are not about volume size.
//
STGR_STATUS
StgrExt4ParseSuperblock(
    const uint8_t *Buffer,
    uint32_t LengthBytes,
    uint64_t DeviceSizeBytes,
    const STGR_ENGINE_CONFIG *Config,
    STGR_SUPERBLOCK *Superblock
    );

//
// Apply the documented feature policy. Returns StgrStatusUnsupportedFeature
// when any bit is refused, and reports which set and which single bit caused
// the refusal. Outputs are optional.
//
STGR_STATUS
StgrExt4CheckFeaturePolicy(
    uint32_t CompatFeatures,
    uint32_t IncompatFeatures,
    uint32_t RoCompatFeatures,
    STGR_FEATURE_SET *RefusedSet,
    uint32_t *RefusedBits
    );

//
// Not implemented yet, on purpose: CRC-32C superblock checksums. A volume
// with the metadata_csum read-only feature is refused with
// StgrStatusChecksumNotVerified until the checksum path is validated against
// images produced by a pinned e2fsprogs. Guessing a CRC convention would be
// worse than refusing: a wrong checksum pass silently trusts corrupt metadata.
//

#endif // STGR_EXT4_H
