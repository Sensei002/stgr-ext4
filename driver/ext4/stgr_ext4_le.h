/*++
Module Name:
    stgr_ext4_le.h

Abstract:
    Little-endian field decoders and overflow-checked arithmetic.

    Nothing in the engine casts a disk buffer to a structure. Fields are read
    byte by byte through these helpers, and every size computation that could
    wrap is checked instead of assumed.

Environment:
    Host tests and kernel context, PASSIVE_LEVEL only.

License:
    SPDX-License-Identifier: MIT
--*/

#ifndef STGR_EXT4_LE_H
#define STGR_EXT4_LE_H

#include <stdint.h>

//
// The kernel build may compile the engine in the default MSVC C dialect,
// where "inline" is spelled __inline. Keep one spelling for both worlds.
//
#if defined(_MSC_VER) && !defined(__cplusplus)
#define STGR_INLINE static __inline
#else
#define STGR_INLINE static inline
#endif

STGR_INLINE uint16_t
StgrReadLe16(
    const uint8_t *Buffer
    )
{
    return (uint16_t)((uint16_t)Buffer[0] | (uint16_t)((uint16_t)Buffer[1] << 8));
}

STGR_INLINE uint32_t
StgrReadLe32(
    const uint8_t *Buffer
    )
{
    return ((uint32_t)Buffer[0])
         | ((uint32_t)Buffer[1] << 8)
         | ((uint32_t)Buffer[2] << 16)
         | ((uint32_t)Buffer[3] << 24);
}

STGR_INLINE uint64_t
StgrReadLe64(
    const uint8_t *Buffer
    )
{
    return (uint64_t)StgrReadLe32(Buffer) | ((uint64_t)StgrReadLe32(Buffer + 4) << 32);
}

//
// Join the low and high halves of a 64-bit on-disk field. The high half only
// exists when the volume sets the 64BIT incompatible feature.
//
STGR_INLINE uint64_t
StgrCombine64(
    uint32_t Low,
    uint32_t High
    )
{
    return (uint64_t)Low | ((uint64_t)High << 32);
}

//
// Returns 1 and stores the product on success, 0 on overflow.
//
STGR_INLINE int
StgrMulU64(
    uint64_t Left,
    uint64_t Right,
    uint64_t *Result
    )
{
    if (Left != 0u && Right > UINT64_MAX / Left)
    {
        return 0;
    }

    *Result = Left * Right;
    return 1;
}

//
// Returns 1 and stores the sum on success, 0 on overflow.
//
STGR_INLINE int
StgrAddU64(
    uint64_t Left,
    uint64_t Right,
    uint64_t *Result
    )
{
    if (Left > UINT64_MAX - Right)
    {
        return 0;
    }

    *Result = Left + Right;
    return 1;
}

STGR_INLINE int
StgrIsPowerOfTwoU32(
    uint32_t Value
    )
{
    return Value != 0u && (Value & (Value - 1u)) == 0u;
}

#endif // STGR_EXT4_LE_H
