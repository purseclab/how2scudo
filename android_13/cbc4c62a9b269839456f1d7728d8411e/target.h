#pragma once

#include <stddef.h>
#include <stdint.h>

// Constants derived from the unstripped AArch64 libc whose GNU build ID is
// cbc4c62a9b269839456f1d7728d8411e. Do not reuse them for another libc.
#define HOW2SCUDO_TARGET_ANDROID_API 33U
#define HOW2SCUDO_TARGET_ABI "arm64-v8a"
#define HOW2SCUDO_TARGET_LIBC_BUILD_ID "cbc4c62a9b269839456f1d7728d8411e"
#define HOW2SCUDO_TARGET_LIBC_SHA256                                         \
    "7eb78bcfd12f5f1617d136cd4c2916b97b8be94588c4f1279d86539821e9b8ca"
#define HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET 0xd7440ULL
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
