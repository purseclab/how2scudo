#pragma once

#include <stddef.h>
#include <stdint.h>

// Constants derived from the unstripped AArch64 libc whose GNU build ID is
// c001f2c6f6eddca1f38c99bd7c52019d. Do not reuse them for another libc.
#define HOW2SCUDO_TARGET_ANDROID_API 34U
#define HOW2SCUDO_TARGET_ABI "arm64-v8a"
#define HOW2SCUDO_TARGET_LIBC_BUILD_ID "c001f2c6f6eddca1f38c99bd7c52019d"
#define HOW2SCUDO_TARGET_LIBC_SHA256                                         \
    "6fba4f6460cc6145eb9cf849eea5d1e929081530e8f2937c987cbce454da0057"
#define HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET 0xdd480ULL
#define HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE 0x10U
#define HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE 0x40U
#define HOW2SCUDO_TARGET_SECONDARY_USER_DELTA 0x50U
#define HOW2SCUDO_TARGET_ADDRESS_MASK 0x00ffffffffffffffULL
#define HOW2SCUDO_TARGET_SCUDO_HEADER_TAG 0x0200000000000000ULL

// AndroidSizeClassMap properties used by house of spirit. The binary loads a
// 32-bit compact pointer and reconstructs it with a four-bit left shift.
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID 1U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE 0x10U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_BITS 32U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_SCALE_LOG 4U
