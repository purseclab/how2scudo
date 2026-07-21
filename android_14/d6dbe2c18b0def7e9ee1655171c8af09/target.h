#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stddef.h>
#include <stdint.h>
#endif

// Constants derived from the unstripped AArch64 libc whose GNU build ID is
// d6dbe2c18b0def7e9ee1655171c8af09. Do not reuse them for another libc.
#define HOW2SCUDO_TARGET_ANDROID_API 34U
#define HOW2SCUDO_TARGET_ABI "arm64-v8a"
#define HOW2SCUDO_TARGET_LIBC_BUILD_ID "d6dbe2c18b0def7e9ee1655171c8af09"
#define HOW2SCUDO_TARGET_LIBC_SHA256                                         \
    "cc9589528f32b297e027ac5ccbda322587d667a59786d74bab67bf725d05d3ca"
#define HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET 0x107340ULL
#define HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE 0x10U
#define HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE 0x30U
#define HOW2SCUDO_TARGET_SECONDARY_USER_DELTA 0x40U
#define HOW2SCUDO_TARGET_ADDRESS_MASK 0x00ffffffffffffffULL
#define HOW2SCUDO_TARGET_SCUDO_HEADER_TAG 0x0200000000000000ULL

// AndroidSizeClassMap properties used by the house-of-spirit PoC. A 0x10-byte
// request plus Scudo's 0x10-byte header selects the first 0x20-byte class.
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID 1U
#define HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE 0x10U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_BITS 32U
#define HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_SCALE_LOG 4U

#ifdef __cplusplus
namespace how2scudo::target {

inline constexpr unsigned kAndroidApi = HOW2SCUDO_TARGET_ANDROID_API;
inline constexpr char kAbi[] = HOW2SCUDO_TARGET_ABI;
inline constexpr char kLibcBuildId[] = HOW2SCUDO_TARGET_LIBC_BUILD_ID;
inline constexpr char kLibcSha256[] = HOW2SCUDO_TARGET_LIBC_SHA256;

// `_ZL9Allocator` is present at this value in the file's local symbol table.
// For this Scudo build the cookie is the first field of that object.
inline constexpr std::uintptr_t kScudoAllocatorOffset =
    HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET;

// Derived from MapAllocator<AndroidNormalConfig>::allocate/deallocate.
inline constexpr std::size_t kCombinedHeaderSize =
    HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
inline constexpr std::size_t kSecondaryHeaderSize =
    HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE;
inline constexpr std::size_t kSecondaryUserDelta =
    HOW2SCUDO_TARGET_SECONDARY_USER_DELTA;

// AndroidNormalConfig supports memory tagging. Bionic's free() removes its
// process heap tag before calling Scudo. With memory tagging inactive,
// getHeaderTaggedPointer() then applies fixed tag 2 for the header checksum.
inline constexpr std::uintptr_t kAddressMask = HOW2SCUDO_TARGET_ADDRESS_MASK;
inline constexpr std::uintptr_t kScudoHeaderTag =
    HOW2SCUDO_TARGET_SCUDO_HEADER_TAG;

inline constexpr unsigned kSmallestPrimaryClassId =
    HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID;
inline constexpr std::size_t kSmallestPrimaryRequestSize =
    HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE;
inline constexpr unsigned kPrimaryCompactPtrBits =
    HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_BITS;
inline constexpr unsigned kPrimaryCompactPtrScaleLog =
    HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_SCALE_LOG;

}  // namespace how2scudo::target
#endif
