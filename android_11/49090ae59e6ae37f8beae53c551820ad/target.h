#pragma once

#include <stddef.h>
#include <stdint.h>

// Constants derived from the unstripped AArch64 libc whose GNU build ID is
// 49090ae59e6ae37f8beae53c551820ad. Do not reuse them for another libc.
#define HOW2SCUDO_TARGET_ANDROID_API 30U
#define HOW2SCUDO_TARGET_ABI "arm64-v8a"
#define HOW2SCUDO_TARGET_LIBC_BUILD_ID "49090ae59e6ae37f8beae53c551820ad"
#define HOW2SCUDO_TARGET_LIBC_SHA256                                         \
    "a93ab0117c60661e4c1eb9c8fac90855c465821ff0bae94a4299c9f51fe5ec0a"
#define HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET 0xc96c0ULL
#define HOW2SCUDO_TARGET_SCUDO_COOKIE_MEMBER_OFFSET 0x13f00ULL
#define HOW2SCUDO_TARGET_SCUDO_COOKIE_OFFSET 0xdd5c0ULL
// Android 11's _ZL9Allocator symbol names the full allocator object. The
// deallocate disassembly loads the CRC seed at Allocator + 0x13f00.
#if HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET +                              \
        HOW2SCUDO_TARGET_SCUDO_COOKIE_MEMBER_OFFSET !=                    \
    HOW2SCUDO_TARGET_SCUDO_COOKIE_OFFSET
#error "inconsistent Scudo cookie address"
#endif
#define HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE 0x10U
#define HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE 0x30U
#define HOW2SCUDO_TARGET_SECONDARY_USER_DELTA 0x40U
#define HOW2SCUDO_TARGET_ADDRESS_MASK 0x00ffffffffffffffULL
#define HOW2SCUDO_TARGET_SCUDO_HEADER_TAG 0x0000000000000000ULL

// The Android 11 local cache stores full 64-bit primary block pointers.
// Disassembly indexes 0xf0-byte PerClass objects at eight-byte scale and loads
// the selected entry directly into x0 without a decompaction step.
#define HOW2SCUDO_TARGET_PER_CLASS_STRIDE 0xf0U
#define HOW2SCUDO_TARGET_PER_CLASS_CHUNKS_OFFSET 0x10U
#define HOW2SCUDO_TARGET_TLS_SLOT_OFFSET 0x30U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID 1U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE 0x10U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_BLOCK_SIZE 0x20U
#define HOW2SCUDO_TARGET_PRIMARY_ENTRY_BITS 64U
