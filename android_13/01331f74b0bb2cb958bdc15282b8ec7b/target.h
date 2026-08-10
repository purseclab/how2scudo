#pragma once

#include <stddef.h>
#include <stdint.h>

// Constants derived from the unstripped AArch64 libc whose GNU build ID is
// 01331f74b0bb2cb958bdc15282b8ec7b. Do not reuse them for another libc.
#define HOW2SCUDO_TARGET_ANDROID_API 33U
#define HOW2SCUDO_TARGET_ABI "arm64-v8a"
#define HOW2SCUDO_TARGET_LIBC_BUILD_ID "01331f74b0bb2cb958bdc15282b8ec7b"
#define HOW2SCUDO_TARGET_LIBC_SHA256                                         \
    "1b8ddf2e3bb23085626ced2529aee520c26bb71875412b2f598ae2e7c732021a"
#define HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET 0xcc440ULL
#define HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE 0x10U
#define HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE 0x30U
#define HOW2SCUDO_TARGET_SECONDARY_USER_DELTA 0x40U
#define HOW2SCUDO_TARGET_ADDRESS_MASK 0x00ffffffffffffffULL
#define HOW2SCUDO_TARGET_SCUDO_HEADER_TAG 0x0200000000000000ULL

// AndroidSizeClassMap properties used by house of spirit. The binary loads a
// 32-bit compact pointer and reconstructs it with a four-bit left shift.
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID 1U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE 0x10U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_BITS 32U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_SCALE_LOG 4U
