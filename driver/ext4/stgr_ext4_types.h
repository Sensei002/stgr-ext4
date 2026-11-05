/*++
Module Name:
    stgr_ext4_types.h

Abstract:
    Freestanding base types for the portable EXT4 engine.

    This header must stay usable outside the kernel: no WDK headers, no libc,
    no allocation, no floating point. Only <stdint.h> is used, which every
    freestanding C toolkit provides.

Environment:
    Host tests and kernel context, PASSIVE_LEVEL only.

License:
    SPDX-License-Identifier: MIT
--*/

#ifndef STGR_EXT4_TYPES_H
#define STGR_EXT4_TYPES_H

#include <stdint.h>

//
// Every engine routine returns one of these. Zero is success; every failure
// has its own code so a test can assert the exact reason for refusal rather
// than "it failed".
//
typedef enum _STGR_STATUS
{
    StgrStatusSuccess = 0,
    StgrStatusInvalidParameter = 1,
    StgrStatusIoError = 2,
    StgrStatusBufferTooSmall = 3,
    StgrStatusBadMagic = 4,
    StgrStatusBadBlockSize = 5,
    StgrStatusBadGeometry = 6,
    StgrStatusBadInodeSize = 7,
    StgrStatusBadDescriptorSize = 8,
    StgrStatusUncleanVolume = 9,
    StgrStatusBadChecksum = 10,
    StgrStatusUnsupportedFeature = 11,
    StgrStatusOverflow = 12,

    //
    // The volume requires a check the engine does not implement yet. Today
    // this is the metadata_csum superblock checksum: the volume is refused
    // rather than trusted unverified. Refusal is the safe direction.
    //
    StgrStatusChecksumNotVerified = 13
} STGR_STATUS;

//
// Block device interface. The engine never dereferences a device pointer and
// never assumes the device is seekable beyond SizeBytes.
//
// Read must transfer exactly LengthBytes or fail. A short read is an error,
// not a partial success: a parser that silently accepts short data is how
// truncated images turn into out-of-bounds reads.
//
typedef STGR_STATUS (*STGR_READ_BLOCKS)(
    void *Context,
    uint64_t OffsetBytes,
    void *Buffer,
    uint32_t LengthBytes
    );

typedef struct _STGR_BLOCK_IO
{
    void *Context;
    uint64_t SizeBytes;
    STGR_READ_BLOCKS Read;
} STGR_BLOCK_IO;

#endif // STGR_EXT4_TYPES_H
