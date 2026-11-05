/*++
Module Name:
    stgr_super.c

Abstract:
    Primary superblock read, decode, and validation.

    The rule for this file: refuse rather than guess. Every rejection has a
    distinct status so tests can assert the reason, and no field is trusted
    because another field looks plausible. Backup superblocks are never used
    as a fallback; an old backup can resurrect stale metadata.

Environment:
    Host tests and kernel context, PASSIVE_LEVEL only.

License:
    SPDX-License-Identifier: MIT
--*/

#include "stgr_ext4.h"
#include "stgr_ext4_le.h"

//
// Field offsets in the 1024-byte superblock, from the public on-disk layout.
// docs/EXT4-FORMAT.md documents the same table.
//
#define STGR_SB_OFF_INODES_COUNT 0x00u
#define STGR_SB_OFF_BLOCKS_COUNT_LO 0x04u
#define STGR_SB_OFF_FREE_BLOCKS_LO 0x0Cu
#define STGR_SB_OFF_FREE_INODES 0x10u
#define STGR_SB_OFF_FIRST_DATA_BLOCK 0x14u
#define STGR_SB_OFF_LOG_BLOCK_SIZE 0x18u
#define STGR_SB_OFF_BLOCKS_PER_GROUP 0x20u
#define STGR_SB_OFF_INODES_PER_GROUP 0x28u
#define STGR_SB_OFF_MAGIC 0x38u
#define STGR_SB_OFF_STATE 0x3Au
#define STGR_SB_OFF_ERRORS 0x3Cu
#define STGR_SB_OFF_REV_LEVEL 0x4Cu
#define STGR_SB_OFF_INODE_SIZE 0x58u
#define STGR_SB_OFF_COMPAT 0x5Cu
#define STGR_SB_OFF_INCOMPAT 0x60u
#define STGR_SB_OFF_RO_COMPAT 0x64u
#define STGR_SB_OFF_UUID 0x68u
#define STGR_SB_OFF_VOLUME_NAME 0x78u
#define STGR_SB_OFF_DESC_SIZE 0xFEu
#define STGR_SB_OFF_BLOCKS_COUNT_HI 0x150u
#define STGR_SB_OFF_FREE_BLOCKS_HI 0x158u
#define STGR_SB_OFF_CHECKSUM_TYPE 0x175u

#define STGR_SB_MIN_BLOCK_SIZE 1024u
#define STGR_SB_MAX_LOG_BLOCK_SIZE 6u
#define STGR_SB_BASE_INODE_SIZE 128u
#define STGR_SB_UUID_LENGTH 16u
#define STGR_SB_LABEL_LENGTH 16u
#define STGR_SB_DESC_SIZE_LEGACY 32u
#define STGR_SB_DESC_SIZE_MIN_64BIT 64u

//
// Feature policy masks. These encode docs/FILESYSTEM-COMPATIBILITY.md §2 and
// must change with it, in the same commit.
//
// Compatible: safe to read, no layout change the reader must understand here.
//
#define STGR_COMPAT_SUPPORTED_MASK                              \
    (STGR_FEATURE_COMPAT_DIR_PREALLOC |                         \
     STGR_FEATURE_COMPAT_HAS_JOURNAL |                          \
     STGR_FEATURE_COMPAT_EXT_ATTR |                             \
     STGR_FEATURE_COMPAT_RESIZE_INODE |                         \
     STGR_FEATURE_COMPAT_DIR_INDEX |                            \
     STGR_FEATURE_COMPAT_SPARSE_SUPER2 |                        \
     STGR_FEATURE_COMPAT_ORPHAN_FILE)

//
// Incompatible: EXTENTS and FILETYPE are required, not optional. The initial
// engine has no legacy block-map or name-length reader, so a volume without
// them cannot be read correctly.
//
#define STGR_INCOMPAT_SUPPORTED_MASK                            \
    (STGR_FEATURE_INCOMPAT_FILETYPE |                           \
     STGR_FEATURE_INCOMPAT_EXTENTS |                            \
     STGR_FEATURE_INCOMPAT_64BIT |                              \
     STGR_FEATURE_INCOMPAT_FLEX_BG |                            \
     STGR_FEATURE_INCOMPAT_CSUM_SEED)

#define STGR_INCOMPAT_REQUIRED_MASK                             \
    (STGR_FEATURE_INCOMPAT_FILETYPE | STGR_FEATURE_INCOMPAT_EXTENTS)

//
// Read-only-compatible: readable without changing the image. BIGALLOC,
// REPLICA, SHARED_BLOCKS, VERITY, and ORPHAN_PRESENT are deliberately absent.
//
#define STGR_RO_COMPAT_SUPPORTED_MASK                           \
    (STGR_FEATURE_RO_COMPAT_SPARSE_SUPER |                      \
     STGR_FEATURE_RO_COMPAT_LARGE_FILE |                        \
     STGR_FEATURE_RO_COMPAT_BTREE_DIR |                         \
     STGR_FEATURE_RO_COMPAT_HUGE_FILE |                         \
     STGR_FEATURE_RO_COMPAT_GDT_CSUM |                          \
     STGR_FEATURE_RO_COMPAT_DIR_NLINK |                         \
     STGR_FEATURE_RO_COMPAT_EXTRA_ISIZE |                       \
     STGR_FEATURE_RO_COMPAT_QUOTA |                             \
     STGR_FEATURE_RO_COMPAT_METADATA_CSUM |                     \
     STGR_FEATURE_RO_COMPAT_READONLY |                          \
     STGR_FEATURE_RO_COMPAT_PROJECT)

static void
StgrZeroMemory(
    void *Buffer,
    uint32_t LengthBytes
    )
{
    uint8_t *bytes = (uint8_t *)Buffer;
    uint32_t index;

    for (index = 0; index < LengthBytes; index++)
    {
        bytes[index] = (uint8_t)0;
    }
}

static void
StgrReportRefusal(
    STGR_FEATURE_SET *RefusedSet,
    uint32_t *RefusedBits,
    STGR_FEATURE_SET Set,
    uint32_t Bits
    )
{
    if (RefusedSet != NULL)
    {
        *RefusedSet = Set;
    }

    if (RefusedBits != NULL)
    {
        *RefusedBits = Bits;
    }
}

STGR_ENGINE_CONFIG
StgrExt4DefaultConfig(
    void
    )
{
    STGR_ENGINE_CONFIG config;

    //
    // 4 KiB maximum keeps the engine to block sizes with defined Windows
    // page behaviour. 8192 and above are a later milestone with their own
    // tests, so they are refused instead of half-supported.
    //
    config.MaxBlockSize = 4096u;
    config.MaxGroupCount = 65536u;
    config.MaxInodeCount = 0x10000000u;
    return config;
}

STGR_STATUS
StgrExt4CheckFeaturePolicy(
    uint32_t CompatFeatures,
    uint32_t IncompatFeatures,
    uint32_t RoCompatFeatures,
    STGR_FEATURE_SET *RefusedSet,
    uint32_t *RefusedBits
    )
{
    uint32_t refused;

    StgrReportRefusal(RefusedSet, RefusedBits, StgrFeatureSetNone, 0u);

    //
    // Required features first: their absence is a refusal, not a warning.
    //
    refused = STGR_INCOMPAT_REQUIRED_MASK & ~IncompatFeatures;
    if (refused != 0u)
    {
        StgrReportRefusal(RefusedSet, RefusedBits, StgrFeatureSetIncompat, refused & (0u - refused));
        return StgrStatusUnsupportedFeature;
    }

    //
    // Unknown incompatible bits are refused unconditionally: an unread layout
    // change is exactly what must never be guessed.
    //
    refused = IncompatFeatures & ~STGR_INCOMPAT_SUPPORTED_MASK;
    if (refused != 0u)
    {
        StgrReportRefusal(RefusedSet, RefusedBits, StgrFeatureSetIncompat, refused & (0u - refused));
        return StgrStatusUnsupportedFeature;
    }

    refused = RoCompatFeatures & ~STGR_RO_COMPAT_SUPPORTED_MASK;
    if (refused != 0u)
    {
        StgrReportRefusal(RefusedSet, RefusedBits, StgrFeatureSetRoCompat, refused & (0u - refused));
        return StgrStatusUnsupportedFeature;
    }

    refused = CompatFeatures & ~STGR_COMPAT_SUPPORTED_MASK;
    if (refused != 0u)
    {
        StgrReportRefusal(RefusedSet, RefusedBits, StgrFeatureSetCompat, refused & (0u - refused));
        return StgrStatusUnsupportedFeature;
    }

    return StgrStatusSuccess;
}

STGR_STATUS
StgrExt4ParseSuperblock(
    const uint8_t *Buffer,
    uint32_t LengthBytes,
    uint64_t DeviceSizeBytes,
    const STGR_ENGINE_CONFIG *Config,
    STGR_SUPERBLOCK *Superblock
    )
{
    STGR_ENGINE_CONFIG defaults;
    STGR_STATUS status;
    uint32_t logBlockSize;
    uint32_t blockSize;
    uint32_t firstDataBlock;
    uint32_t inodesPerGroup;
    uint32_t blocksPerGroup;
    uint32_t inodeSize;
    uint32_t descriptorSize;
    uint32_t revisionLevel;
    uint32_t compatFeatures;
    uint32_t incompatFeatures;
    uint32_t roCompatFeatures;
    uint32_t index;
    uint32_t state;
    uint64_t blocksCount;
    uint64_t freeBlocksCount;
    uint64_t volumeBytes;
    uint64_t groupSpan;
    uint64_t groups;

    if (Buffer == NULL || Superblock == NULL)
    {
        return StgrStatusInvalidParameter;
    }

    if (Config == NULL)
    {
        defaults = StgrExt4DefaultConfig();
        Config = &defaults;
    }

    if (Config->MaxBlockSize < STGR_SB_MIN_BLOCK_SIZE)
    {
        return StgrStatusInvalidParameter;
    }

    if (LengthBytes < STGR_EXT4_SUPERBLOCK_SIZE)
    {
        return StgrStatusBufferTooSmall;
    }

    StgrZeroMemory(Superblock, (uint32_t)sizeof(*Superblock));

    if (StgrReadLe16(Buffer + STGR_SB_OFF_MAGIC) != STGR_EXT4_MAGIC)
    {
        return StgrStatusBadMagic;
    }

    revisionLevel = StgrReadLe32(Buffer + STGR_SB_OFF_REV_LEVEL);

    //
    // Block size: 1024 << s_log_block_size, bounded by the configured maximum.
    //
    logBlockSize = StgrReadLe32(Buffer + STGR_SB_OFF_LOG_BLOCK_SIZE);
    if (logBlockSize > STGR_SB_MAX_LOG_BLOCK_SIZE)
    {
        return StgrStatusBadBlockSize;
    }

    blockSize = STGR_SB_MIN_BLOCK_SIZE << logBlockSize;
    if (blockSize > Config->MaxBlockSize)
    {
        return StgrStatusBadBlockSize;
    }

    //
    // s_first_data_block is 1 only for 1 KiB blocks, where the superblock sits
    // inside block 1. Any other value means the geometry is inconsistent.
    //
    firstDataBlock = StgrReadLe32(Buffer + STGR_SB_OFF_FIRST_DATA_BLOCK);
    if (firstDataBlock != ((blockSize == STGR_SB_MIN_BLOCK_SIZE) ? 1u : 0u))
    {
        return StgrStatusBadGeometry;
    }

    incompatFeatures = StgrReadLe32(Buffer + STGR_SB_OFF_INCOMPAT);

    blocksCount = (uint64_t)StgrReadLe32(Buffer + STGR_SB_OFF_BLOCKS_COUNT_LO);
    freeBlocksCount = (uint64_t)StgrReadLe32(Buffer + STGR_SB_OFF_FREE_BLOCKS_LO);

    //
    // The high halves exist only on 64BIT volumes. Reading them on a 32-bit
    // volume would interpret reserved bytes as geometry.
    //
    if ((incompatFeatures & STGR_FEATURE_INCOMPAT_64BIT) != 0u)
    {
        blocksCount = StgrCombine64(StgrReadLe32(Buffer + STGR_SB_OFF_BLOCKS_COUNT_LO),
                                    StgrReadLe32(Buffer + STGR_SB_OFF_BLOCKS_COUNT_HI));
        freeBlocksCount = StgrCombine64(StgrReadLe32(Buffer + STGR_SB_OFF_FREE_BLOCKS_LO),
                                        StgrReadLe32(Buffer + STGR_SB_OFF_FREE_BLOCKS_HI));
    }

    if (blocksCount == 0u)
    {
        return StgrStatusBadGeometry;
    }

    if (!StgrMulU64(blocksCount, (uint64_t)blockSize, &volumeBytes))
    {
        return StgrStatusOverflow;
    }

    if (DeviceSizeBytes != 0u && volumeBytes > DeviceSizeBytes)
    {
        return StgrStatusBadGeometry;
    }

    if (StgrReadLe32(Buffer + STGR_SB_OFF_INODES_COUNT) == 0u ||
        StgrReadLe32(Buffer + STGR_SB_OFF_INODES_COUNT) > Config->MaxInodeCount)
    {
        return StgrStatusBadGeometry;
    }

    blocksPerGroup = StgrReadLe32(Buffer + STGR_SB_OFF_BLOCKS_PER_GROUP);
    inodesPerGroup = StgrReadLe32(Buffer + STGR_SB_OFF_INODES_PER_GROUP);

    //
    // A group bitmap has one bit per block or inode in the group, so a count
    // above 8 bits per byte is impossible, and zero is nonsense.
    //
    if (blocksPerGroup == 0u || blocksPerGroup > (blockSize * 8u) ||
        inodesPerGroup == 0u || inodesPerGroup > (blockSize * 8u))
    {
        return StgrStatusBadGeometry;
    }

    //
    // Inode size: fixed at 128 by revision 0; on revision 1 it must be a power
    // of two between 128 and the block size.
    //
    inodeSize = StgrReadLe16(Buffer + STGR_SB_OFF_INODE_SIZE);
    if (revisionLevel == 0u)
    {
        inodeSize = STGR_SB_BASE_INODE_SIZE;
    }
    else if (inodeSize < STGR_SB_BASE_INODE_SIZE ||
             inodeSize > blockSize ||
             !StgrIsPowerOfTwoU32(inodeSize))
    {
        return StgrStatusBadInodeSize;
    }

    //
    // Group descriptor size: 32 bytes unless 64BIT is set, in which case the
    // on-disk value must be at least 64 and a power of two.
    //
    descriptorSize = STGR_SB_DESC_SIZE_LEGACY;
    if ((incompatFeatures & STGR_FEATURE_INCOMPAT_64BIT) != 0u)
    {
        descriptorSize = StgrReadLe16(Buffer + STGR_SB_OFF_DESC_SIZE);
        if (descriptorSize < STGR_SB_DESC_SIZE_MIN_64BIT ||
            descriptorSize > blockSize ||
            !StgrIsPowerOfTwoU32(descriptorSize))
        {
            return StgrStatusBadDescriptorSize;
        }
    }

    //
    // Group count: ceil((blocks_count - first_data_block) / blocks_per_group),
    // in 64-bit arithmetic with an explicit overflow check.
    //
    if (blocksCount <= (uint64_t)firstDataBlock)
    {
        return StgrStatusBadGeometry;
    }

    groupSpan = blocksCount - (uint64_t)firstDataBlock;
    groups = groupSpan / (uint64_t)blocksPerGroup;
    if ((groupSpan % (uint64_t)blocksPerGroup) != 0u)
    {
        if (!StgrAddU64(groups, 1u, &groups))
        {
            return StgrStatusOverflow;
        }
    }

    if (groups == 0u || groups > (uint64_t)Config->MaxGroupCount ||
        groups > (uint64_t)0xFFFFFFFFu)
    {
        return StgrStatusBadGeometry;
    }

    //
    // An unclean volume is refused before any feature policy runs: a dirty
    // journal means the on-disk metadata is not the final state.
    //
    state = (uint32_t)StgrReadLe16(Buffer + STGR_SB_OFF_STATE);
    if ((state & STGR_SUPER_STATE_VALID_FS) == 0u ||
        (state & (STGR_SUPER_STATE_ERROR_FS | STGR_SUPER_STATE_ORPHAN_FS)) != 0u)
    {
        return StgrStatusUncleanVolume;
    }

    compatFeatures = StgrReadLe32(Buffer + STGR_SB_OFF_COMPAT);
    roCompatFeatures = StgrReadLe32(Buffer + STGR_SB_OFF_RO_COMPAT);

    status = StgrExt4CheckFeaturePolicy(compatFeatures,
                                        incompatFeatures,
                                        roCompatFeatures,
                                        NULL,
                                        NULL);
    if (status != StgrStatusSuccess)
    {
        return status;
    }

    //
    // metadata_csum volumes are refused, not trusted. The feature is in the
    // read policy because the format is understood, but this build cannot
    // verify the checksum yet, so accepting the volume would mean trusting
    // unverified metadata. See the note in stgr_ext4.h.
    //
    if ((roCompatFeatures & STGR_FEATURE_RO_COMPAT_METADATA_CSUM) != 0u)
    {
        if (Buffer[STGR_SB_OFF_CHECKSUM_TYPE] != 1u)
        {
            return StgrStatusUnsupportedFeature;
        }

        return StgrStatusChecksumNotVerified;
    }

    Superblock->InodesCount = StgrReadLe32(Buffer + STGR_SB_OFF_INODES_COUNT);
    Superblock->FreeInodesCount = StgrReadLe32(Buffer + STGR_SB_OFF_FREE_INODES);
    Superblock->BlocksCount = blocksCount;
    Superblock->FreeBlocksCount = freeBlocksCount;
    Superblock->FirstDataBlock = firstDataBlock;
    Superblock->BlockSize = blockSize;
    Superblock->BlocksPerGroup = blocksPerGroup;
    Superblock->InodesPerGroup = inodesPerGroup;
    Superblock->GroupCount = (uint32_t)groups;
    Superblock->InodeSize = inodeSize;
    Superblock->DescriptorSize = descriptorSize;
    Superblock->RevisionLevel = revisionLevel;
    Superblock->CompatFeatures = compatFeatures;
    Superblock->IncompatFeatures = incompatFeatures;
    Superblock->RoCompatFeatures = roCompatFeatures;
    Superblock->State = StgrReadLe16(Buffer + STGR_SB_OFF_STATE);
    Superblock->ErrorsBehavior = StgrReadLe16(Buffer + STGR_SB_OFF_ERRORS);

    for (index = 0u; index < STGR_SB_UUID_LENGTH; index++)
    {
        Superblock->Uuid[index] = Buffer[STGR_SB_OFF_UUID + index];
    }

    for (index = 0u; index < STGR_SB_LABEL_LENGTH; index++)
    {
        Superblock->VolumeLabel[index] = (char)Buffer[STGR_SB_OFF_VOLUME_NAME + index];
    }
    Superblock->VolumeLabel[STGR_SB_LABEL_LENGTH] = '\0';

    return StgrStatusSuccess;
}

STGR_STATUS
StgrExt4ReadSuperblock(
    const STGR_BLOCK_IO *Io,
    const STGR_ENGINE_CONFIG *Config,
    STGR_SUPERBLOCK *Superblock
    )
{
    uint8_t buffer[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_STATUS status;

    if (Io == NULL || Io->Read == NULL || Superblock == NULL)
    {
        return StgrStatusInvalidParameter;
    }

    //
    // The read is bounded: one 1024-byte block at byte 1024. A device that
    // cannot hold both the boot block and the superblock is not usable.
    //
    if (Io->SizeBytes < (uint64_t)(STGR_EXT4_SUPERBLOCK_OFFSET + STGR_EXT4_SUPERBLOCK_SIZE))
    {
        return StgrStatusIoError;
    }

    status = Io->Read(Io->Context,
                      (uint64_t)STGR_EXT4_SUPERBLOCK_OFFSET,
                      buffer,
                      STGR_EXT4_SUPERBLOCK_SIZE);
    if (status != StgrStatusSuccess)
    {
        return status;
    }

    return StgrExt4ParseSuperblock(buffer,
                                   STGR_EXT4_SUPERBLOCK_SIZE,
                                   Io->SizeBytes,
                                   Config,
                                   Superblock);
}
