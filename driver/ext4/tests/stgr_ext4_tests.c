/*++
Module Name:
    stgr_ext4_tests.c

Abstract:
    Host unit tests for the portable EXT4 superblock parser.

    These tests run on the build machine, not in the kernel. They build
    superblock images in memory, corrupt one thing at a time, and assert the
    exact status and the exact decoded values. A test that only checks "did
    not crash" is not a test.

Environment:
    Host, any C11 compiler.

License:
    SPDX-License-Identifier: MIT
--*/

#include <stdio.h>
#include <string.h>

#include "stgr_ext4.h"
#include "stgr_ext4_le.h"

//
// Superblock field offsets used by the tests. Kept separate from the engine
// on purpose: a wrong offset in the engine must fail a test, not agree with it.
//
#define SB_OFF_INODES_COUNT 0x00u
#define SB_OFF_BLOCKS_COUNT_LO 0x04u
#define SB_OFF_FREE_BLOCKS_LO 0x0Cu
#define SB_OFF_FREE_INODES 0x10u
#define SB_OFF_FIRST_DATA_BLOCK 0x14u
#define SB_OFF_LOG_BLOCK_SIZE 0x18u
#define SB_OFF_BLOCKS_PER_GROUP 0x20u
#define SB_OFF_INODES_PER_GROUP 0x28u
#define SB_OFF_MAGIC 0x38u
#define SB_OFF_STATE 0x3Au
#define SB_OFF_ERRORS 0x3Cu
#define SB_OFF_REV_LEVEL 0x4Cu
#define SB_OFF_INODE_SIZE 0x58u
#define SB_OFF_COMPAT 0x5Cu
#define SB_OFF_INCOMPAT 0x60u
#define SB_OFF_RO_COMPAT 0x64u
#define SB_OFF_UUID 0x68u
#define SB_OFF_VOLUME_NAME 0x78u
#define SB_OFF_DESC_SIZE 0xFEu
#define SB_OFF_CHECKSUM_TYPE 0x175u

#define TEST_BLOCKS 64u
#define TEST_BLOCK_SIZE 4096u
#define TEST_DEVICE_BYTES ((uint64_t)TEST_BLOCKS * TEST_BLOCK_SIZE)

static int g_failures;
static int g_checks;

static void PutLe16(uint8_t *Buffer, uint32_t Offset, uint16_t Value)
{
    Buffer[Offset + 0u] = (uint8_t)(Value & 0xFFu);
    Buffer[Offset + 1u] = (uint8_t)(Value >> 8);
}

static void PutLe32(uint8_t *Buffer, uint32_t Offset, uint32_t Value)
{
    Buffer[Offset + 0u] = (uint8_t)(Value & 0xFFu);
    Buffer[Offset + 1u] = (uint8_t)((Value >> 8) & 0xFFu);
    Buffer[Offset + 2u] = (uint8_t)((Value >> 16) & 0xFFu);
    Buffer[Offset + 3u] = (uint8_t)(Value >> 24);
}

static void ExpectInt(const char *Name, long Expected, long Actual)
{
    g_checks++;
    if (Expected != Actual)
    {
        printf("FAIL %-46s expected %ld, got %ld\n", Name, Expected, Actual);
        g_failures++;
    }
}

static void ExpectStatus(const char *Name, STGR_STATUS Expected, STGR_STATUS Actual)
{
    ExpectInt(Name, (long)Expected, (long)Actual);
}

static void ExpectTrue(const char *Name, int Condition)
{
    g_checks++;
    if (Condition == 0)
    {
        printf("FAIL %-46s condition was false\n", Name);
        g_failures++;
    }
}

//
// A superblock for a 4 KiB-block, 256 KiB, one-group EXT4 volume with the
// initial target feature set and no metadata_csum.
//
static void BuildValidSuperblock(uint8_t *Buffer)
{
    static const uint8_t uuid[16] = {
        0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u,
        0x99u, 0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu, 0x00u
    };
    uint32_t index;

    memset(Buffer, 0, STGR_EXT4_SUPERBLOCK_SIZE);

    PutLe32(Buffer, SB_OFF_INODES_COUNT, 64u);
    PutLe32(Buffer, SB_OFF_BLOCKS_COUNT_LO, TEST_BLOCKS);
    PutLe32(Buffer, SB_OFF_FREE_BLOCKS_LO, 32u);
    PutLe32(Buffer, SB_OFF_FREE_INODES, 32u);
    PutLe32(Buffer, SB_OFF_FIRST_DATA_BLOCK, 0u);
    PutLe32(Buffer, SB_OFF_LOG_BLOCK_SIZE, 2u);
    PutLe32(Buffer, SB_OFF_BLOCKS_PER_GROUP, TEST_BLOCKS);
    PutLe32(Buffer, SB_OFF_INODES_PER_GROUP, 64u);
    PutLe16(Buffer, SB_OFF_MAGIC, (uint16_t)STGR_EXT4_MAGIC);
    PutLe16(Buffer, SB_OFF_STATE, (uint16_t)STGR_SUPER_STATE_VALID_FS);
    PutLe16(Buffer, SB_OFF_ERRORS, 1u);
    PutLe32(Buffer, SB_OFF_REV_LEVEL, 1u);
    PutLe16(Buffer, SB_OFF_INODE_SIZE, 256u);
    PutLe32(Buffer, SB_OFF_COMPAT,
            STGR_FEATURE_COMPAT_HAS_JOURNAL |
            STGR_FEATURE_COMPAT_EXT_ATTR |
            STGR_FEATURE_COMPAT_DIR_INDEX);
    PutLe32(Buffer, SB_OFF_INCOMPAT,
            STGR_FEATURE_INCOMPAT_EXTENTS |
            STGR_FEATURE_INCOMPAT_64BIT |
            STGR_FEATURE_INCOMPAT_FILETYPE);
    PutLe32(Buffer, SB_OFF_RO_COMPAT,
            STGR_FEATURE_RO_COMPAT_SPARSE_SUPER |
            STGR_FEATURE_RO_COMPAT_LARGE_FILE |
            STGR_FEATURE_RO_COMPAT_HUGE_FILE |
            STGR_FEATURE_RO_COMPAT_DIR_NLINK |
            STGR_FEATURE_RO_COMPAT_EXTRA_ISIZE);
    PutLe16(Buffer, SB_OFF_DESC_SIZE, 64u);

    for (index = 0u; index < 16u; index++)
    {
        Buffer[SB_OFF_UUID + index] = uuid[index];
    }

    memcpy(Buffer + SB_OFF_VOLUME_NAME, "STGRTEST", 8u);
}

//
// Block device used by the read-path tests.
//
typedef struct _TEST_DEVICE
{
    const uint8_t *Image;
    uint32_t ImageBytes;
    int ShortRead;
} TEST_DEVICE;

static STGR_STATUS TestRead(void *Context, uint64_t OffsetBytes, void *Buffer, uint32_t LengthBytes)
{
    TEST_DEVICE *device = (TEST_DEVICE *)Context;

    if (device->ShortRead != 0)
    {
        return StgrStatusIoError;
    }

    if (OffsetBytes > (uint64_t)device->ImageBytes ||
        LengthBytes > ((uint32_t)device->ImageBytes - (uint32_t)OffsetBytes))
    {
        return StgrStatusIoError;
    }

    memcpy(Buffer, device->Image + (uint32_t)OffsetBytes, LengthBytes);
    return StgrStatusSuccess;
}

static void TestValidSuperblock(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);

    ExpectStatus("valid 4KiB superblock accepted",
                 StgrStatusSuccess,
                 StgrExt4ParseSuperblock(superblock,
                                         STGR_EXT4_SUPERBLOCK_SIZE,
                                         TEST_DEVICE_BYTES,
                                         NULL,
                                         &decoded));
    ExpectInt("block size decoded", 4096, (long)decoded.BlockSize);
    ExpectInt("block count decoded", (long)TEST_BLOCKS, (long)decoded.BlocksCount);
    ExpectInt("inode count decoded", 64, (long)decoded.InodesCount);
    ExpectInt("group count computed", 1, (long)decoded.GroupCount);
    ExpectInt("inode size decoded", 256, (long)decoded.InodeSize);
    ExpectInt("descriptor size decoded", 64, (long)decoded.DescriptorSize);
    ExpectInt("revision decoded", 1, (long)decoded.RevisionLevel);
    ExpectInt("first data block decoded", 0, (long)decoded.FirstDataBlock);
    ExpectTrue("uuid copied", decoded.Uuid[0] == 0x11u && decoded.Uuid[15] == 0x00u);
    ExpectTrue("label copied and terminated",
               strcmp(decoded.VolumeLabel, "STGRTEST") == 0 &&
               decoded.VolumeLabel[16] == '\0');

    //
    // Group count must round up: 65 blocks in 64-block groups is two groups.
    //
    PutLe32(superblock, SB_OFF_BLOCKS_COUNT_LO, 65u);
    ExpectStatus("ceil() group count",
                 StgrStatusSuccess,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, 0u, NULL, &decoded));
    ExpectInt("ceil() group count value", 2, (long)decoded.GroupCount);
}

static void TestSignatureAndBlockSize(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_MAGIC, 0x1234u);
    ExpectStatus("wrong magic refused",
                 StgrStatusBadMagic,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_LOG_BLOCK_SIZE, 7u);
    ExpectStatus("log block size 7 refused",
                 StgrStatusBadBlockSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_LOG_BLOCK_SIZE, 3u);
    ExpectStatus("8KiB block refused by default policy",
                 StgrStatusBadBlockSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));
}

static void TestOneKilobyteGeometry(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;
    uint64_t device = (uint64_t)TEST_BLOCKS * 1024u;

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_LOG_BLOCK_SIZE, 0u);
    PutLe32(superblock, SB_OFF_FIRST_DATA_BLOCK, 1u);
    PutLe16(superblock, SB_OFF_INODE_SIZE, 128u);

    ExpectStatus("1KiB volume accepted",
                 StgrStatusSuccess,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, device, NULL, &decoded));
    ExpectInt("1KiB block size", 1024, (long)decoded.BlockSize);
    ExpectInt("1KiB first data block", 1, (long)decoded.FirstDataBlock);
    ExpectInt("1KiB descriptor size", 32, (long)decoded.DescriptorSize);

    PutLe32(superblock, SB_OFF_FIRST_DATA_BLOCK, 0u);
    ExpectStatus("wrong first_data_block refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, device, NULL, &decoded));
}

static void TestGeometryRefusals(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_BLOCKS_COUNT_LO, 0u);
    ExpectStatus("zero block count refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, 0u, NULL, &decoded));

    BuildValidSuperblock(superblock);
    ExpectStatus("volume larger than device refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, 4096u, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_INODES_COUNT, 0u);
    ExpectStatus("zero inode count refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_BLOCKS_PER_GROUP, 0u);
    ExpectStatus("zero blocks per group refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_BLOCKS_PER_GROUP, TEST_BLOCK_SIZE * 8u + 1u);
    ExpectStatus("blocks per group beyond bitmap refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_INODES_PER_GROUP, 0u);
    ExpectStatus("zero inodes per group refused",
                 StgrStatusBadGeometry,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));
}

static void TestInodeAndDescriptorSize(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_INODE_SIZE, 100u);
    ExpectStatus("inode size below 128 refused",
                 StgrStatusBadInodeSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_INODE_SIZE, 300u);
    ExpectStatus("non power-of-two inode size refused",
                 StgrStatusBadInodeSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_INODE_SIZE, 8192u);
    ExpectStatus("inode size above block size refused",
                 StgrStatusBadInodeSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_DESC_SIZE, 32u);
    ExpectStatus("64-bit volume with 32-byte descriptors refused",
                 StgrStatusBadDescriptorSize,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    //
    // Without 64BIT the descriptor size is 32 regardless of the field.
    //
    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_INCOMPAT,
            STGR_FEATURE_INCOMPAT_EXTENTS | STGR_FEATURE_INCOMPAT_FILETYPE);
    PutLe16(superblock, SB_OFF_DESC_SIZE, 0u);
    ExpectStatus("32-bit volume accepted",
                 StgrStatusSuccess,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));
    ExpectInt("32-bit descriptor size", 32, (long)decoded.DescriptorSize);
}

static void TestVolumeState(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_STATE, STGR_SUPER_STATE_ERROR_FS);
    ExpectStatus("error state refused",
                 StgrStatusUncleanVolume,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_STATE, STGR_SUPER_STATE_ORPHAN_FS);
    ExpectStatus("orphan state refused",
                 StgrStatusUncleanVolume,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    BuildValidSuperblock(superblock);
    PutLe16(superblock, SB_OFF_STATE, 0u);
    ExpectStatus("no valid-fs bit refused",
                 StgrStatusUncleanVolume,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));
}

static void TestFeaturePolicy(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;
    STGR_FEATURE_SET refusedSet;
    uint32_t refusedBits;

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_INCOMPAT,
            STGR_FEATURE_INCOMPAT_EXTENTS |
            STGR_FEATURE_INCOMPAT_64BIT |
            STGR_FEATURE_INCOMPAT_FILETYPE |
            STGR_FEATURE_INCOMPAT_INLINE_DATA);
    refusedSet = StgrFeatureSetNone;
    refusedBits = 0u;
    ExpectStatus("inline data refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4CheckFeaturePolicy(0u, StgrReadLe32(superblock + SB_OFF_INCOMPAT), 0u,
                                            &refusedSet, &refusedBits));
    ExpectInt("inline data refusal set", 2, (long)refusedSet);
    ExpectInt("inline data bit reported", (long)STGR_FEATURE_INCOMPAT_INLINE_DATA, (long)refusedBits);

    ExpectStatus("unknown incompatible bit refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4CheckFeaturePolicy(0u, STGR_FEATURE_INCOMPAT_EXTENTS |
                                                STGR_FEATURE_INCOMPAT_FILETYPE |
                                                0x80000000u, 0u, NULL, NULL));

    refusedSet = StgrFeatureSetNone;
    refusedBits = 0u;
    ExpectStatus("missing extents refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4CheckFeaturePolicy(0u, STGR_FEATURE_INCOMPAT_FILETYPE, 0u,
                                            &refusedSet, &refusedBits));
    ExpectInt("missing extents refusal set", 2, (long)refusedSet);
    ExpectInt("missing extents bit reported", (long)STGR_FEATURE_INCOMPAT_EXTENTS, (long)refusedBits);

    refusedSet = StgrFeatureSetNone;
    ExpectStatus("bigalloc refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4CheckFeaturePolicy(0u,
                                            STGR_FEATURE_INCOMPAT_EXTENTS | STGR_FEATURE_INCOMPAT_FILETYPE,
                                            STGR_FEATURE_RO_COMPAT_BIGALLOC,
                                            &refusedSet, &refusedBits));
    ExpectInt("bigalloc refusal set", 3, (long)refusedSet);

    refusedSet = StgrFeatureSetNone;
    ExpectStatus("imagic inodes refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4CheckFeaturePolicy(STGR_FEATURE_COMPAT_IMAGIC_INODES,
                                            STGR_FEATURE_INCOMPAT_EXTENTS | STGR_FEATURE_INCOMPAT_FILETYPE,
                                            0u, &refusedSet, &refusedBits));
    ExpectInt("imagic refusal set", 1, (long)refusedSet);

    ExpectStatus("supported set accepted",
                 StgrStatusSuccess,
                 StgrExt4CheckFeaturePolicy(STGR_FEATURE_COMPAT_HAS_JOURNAL,
                                            STGR_FEATURE_INCOMPAT_EXTENTS |
                                            STGR_FEATURE_INCOMPAT_FILETYPE |
                                            STGR_FEATURE_INCOMPAT_FLEX_BG,
                                            STGR_FEATURE_RO_COMPAT_SPARSE_SUPER,
                                            NULL, NULL));
}

static void TestChecksumRefusal(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);
    PutLe32(superblock, SB_OFF_RO_COMPAT,
            StgrReadLe32(superblock + SB_OFF_RO_COMPAT) | STGR_FEATURE_RO_COMPAT_METADATA_CSUM);
    superblock[SB_OFF_CHECKSUM_TYPE] = 1u;
    ExpectStatus("metadata_csum volume refused until verified",
                 StgrStatusChecksumNotVerified,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));

    superblock[SB_OFF_CHECKSUM_TYPE] = 2u;
    ExpectStatus("unknown checksum type refused",
                 StgrStatusUnsupportedFeature,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, TEST_DEVICE_BYTES, NULL, &decoded));
}

static void TestReadPath(void)
{
    uint8_t image[(4096u * 3u)];
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;
    STGR_BLOCK_IO io;
    TEST_DEVICE device;

    memset(image, 0, sizeof(image));
    BuildValidSuperblock(superblock);
    memcpy(image + STGR_EXT4_SUPERBLOCK_OFFSET, superblock, STGR_EXT4_SUPERBLOCK_SIZE);

    device.Image = image;
    device.ImageBytes = (uint32_t)sizeof(image);
    device.ShortRead = 0;

    io.Context = &device;
    io.SizeBytes = (uint64_t)sizeof(image);
    io.Read = TestRead;

    //
    // The image holds 12288 bytes, but the superblock claims 64 blocks of
    // 4096 bytes. The read succeeds and the geometry check refuses it, which
    // proves the read path and the bound are both exercised.
    //
    ExpectStatus("superblock read then geometry refusal",
                 StgrStatusBadGeometry,
                 StgrExt4ReadSuperblock(&io, NULL, &decoded));

    device.ShortRead = 1;
    ExpectStatus("device error propagated",
                 StgrStatusIoError,
                 StgrExt4ReadSuperblock(&io, NULL, &decoded));

    device.ShortRead = 0;
    io.SizeBytes = 1024u;
    ExpectStatus("device smaller than boot block plus superblock",
                 StgrStatusIoError,
                 StgrExt4ReadSuperblock(&io, NULL, &decoded));

    io.Read = NULL;
    ExpectStatus("missing read callback refused",
                 StgrStatusInvalidParameter,
                 StgrExt4ReadSuperblock(&io, NULL, &decoded));
}

static void TestApiEdges(void)
{
    uint8_t superblock[STGR_EXT4_SUPERBLOCK_SIZE];
    STGR_SUPERBLOCK decoded;

    BuildValidSuperblock(superblock);

    ExpectStatus("short buffer refused",
                 StgrStatusBufferTooSmall,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE - 1u, 0u, NULL, &decoded));

    ExpectStatus("null buffer refused",
                 StgrStatusInvalidParameter,
                 StgrExt4ParseSuperblock(NULL, STGR_EXT4_SUPERBLOCK_SIZE, 0u, NULL, &decoded));

    ExpectStatus("null output refused",
                 StgrStatusInvalidParameter,
                 StgrExt4ParseSuperblock(superblock, STGR_EXT4_SUPERBLOCK_SIZE, 0u, NULL, NULL));
}

static void TestScalarHelpers(void)
{
    static const uint8_t bytes[8] = { 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u };
    uint64_t result;

    ExpectInt("le16 decode", 0x0201, (long)StgrReadLe16(bytes));
    ExpectInt("le32 decode", 0x04030201, (long)StgrReadLe32(bytes));
    ExpectTrue("le64 decode",
               StgrReadLe64(bytes) == 0x0807060504030201u);
    ExpectTrue("combine64",
               StgrCombine64(0x04030201u, 0x08070605u) == 0x0807060504030201u);
    ExpectInt("power of two 256", 1, StgrIsPowerOfTwoU32(256u));
    ExpectInt("power of two 300", 0, StgrIsPowerOfTwoU32(300u));
    ExpectInt("power of two 0", 0, StgrIsPowerOfTwoU32(0u));

    ExpectInt("multiply fits", 1, StgrMulU64(4096u, 4096u, &result));
    ExpectTrue("multiply result", result == 16777216u);
    ExpectInt("multiply overflows", 0, StgrMulU64(0xFFFFFFFFFFFFFFFFu, 2u, &result));
    ExpectInt("multiply by zero", 1, StgrMulU64(0u, 0xFFFFFFFFFFFFFFFFu, &result));

    ExpectInt("add fits", 1, StgrAddU64(1u, 2u, &result));
    ExpectTrue("add result", result == 3u);
    ExpectInt("add overflows", 0, StgrAddU64(0xFFFFFFFFFFFFFFFFu, 1u, &result));
}

int main(void)
{
    TestValidSuperblock();
    TestSignatureAndBlockSize();
    TestOneKilobyteGeometry();
    TestGeometryRefusals();
    TestInodeAndDescriptorSize();
    TestVolumeState();
    TestFeaturePolicy();
    TestChecksumRefusal();
    TestReadPath();
    TestApiEdges();
    TestScalarHelpers();

    printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
