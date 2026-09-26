// vela-shot: cattura lo schermo di Vela in PNG (protocollo wlr-screencopy).
//
// Uso: vela-shot FILE.png [X Y LARGHEZZA ALTEZZA]
//
// Cattura il primo schermo; con le coordinate, solo quella zona (in pixel
// dello schermo). Usa la sessione indicata da WAYLAND_DISPLAY.

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <zlib.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

static struct wl_shm* shm;
static struct wl_output* output;
static struct zwlr_screencopy_manager_v1* manager;

static struct {
    uint32_t format, width, height, stride;
    int haveBuffer, ready, failed, yInvert;
} frame;

static void registryGlobal(void* data, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version)
{
    if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, wl_output_interface.name) && !output) {
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    } else if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name)) {
        manager = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    }
}

static void registryRemove(void* data, struct wl_registry* registry, uint32_t name) { }

static const struct wl_registry_listener registryListener = { registryGlobal, registryRemove };

static void frameBuffer(void* data, struct zwlr_screencopy_frame_v1* f, uint32_t format,
    uint32_t width, uint32_t height, uint32_t stride)
{
    frame.format = format;
    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.haveBuffer = 1;
}

static void frameFlags(void* data, struct zwlr_screencopy_frame_v1* f, uint32_t flags)
{
    frame.yInvert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}

static void frameReady(void* data, struct zwlr_screencopy_frame_v1* f, uint32_t hi, uint32_t lo, uint32_t ns)
{
    frame.ready = 1;
}

static void frameFailed(void* data, struct zwlr_screencopy_frame_v1* f)
{
    frame.failed = 1;
}

static const struct zwlr_screencopy_frame_v1_listener frameListener = {
    .buffer = frameBuffer,
    .flags = frameFlags,
    .ready = frameReady,
    .failed = frameFailed,
};

// ------------------------------------------------------------------ PNG --

static void put32(unsigned char* p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void writeChunk(FILE* file, const char* type, const unsigned char* data, uint32_t length)
{
    unsigned char header[8];
    put32(header, length);
    memcpy(header + 4, type, 4);
    fwrite(header, 1, 8, file);
    if (length) {
        fwrite(data, 1, length, file);
    }
    uLong crc = crc32(0, (const Bytef*)type, 4);
    crc = crc32(crc, data, length);
    unsigned char trailer[4];
    put32(trailer, (uint32_t)crc);
    fwrite(trailer, 1, 4, file);
}

// Pixel dello schermo -> PNG RGB a 8 bit.
static int writePng(const char* path, const unsigned char* pixels, int x, int y, int w, int h)
{
    // In memoria: XRGB/ARGB8888 = B,G,R,A; XBGR/ABGR8888 = R,G,B,A.
    const int bgr = frame.format == WL_SHM_FORMAT_XRGB8888 || frame.format == WL_SHM_FORMAT_ARGB8888;
    const size_t rowSize = 1 + (size_t)w * 3;
    unsigned char* raw = malloc(rowSize * h);
    for (int row = 0; row < h; ++row) {
        const int srcRow = frame.yInvert ? (int)frame.height - 1 - (y + row) : y + row;
        const unsigned char* src = pixels + (size_t)srcRow * frame.stride + (size_t)x * 4;
        unsigned char* dst = raw + row * rowSize;
        *dst++ = 0; // filtro: nessuno
        for (int col = 0; col < w; ++col, src += 4) {
            *dst++ = bgr ? src[2] : src[0];
            *dst++ = src[1];
            *dst++ = bgr ? src[0] : src[2];
        }
    }
    uLongf packedSize = compressBound(rowSize * h);
    unsigned char* packed = malloc(packedSize);
    compress2(packed, &packedSize, raw, rowSize * h, 6);

    FILE* file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return 0;
    }
    static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    fwrite(signature, 1, 8, file);
    unsigned char ihdr[13];
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8; // bit per canale
    ihdr[9] = 2; // RGB
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    writeChunk(file, "IHDR", ihdr, sizeof(ihdr));
    writeChunk(file, "IDAT", packed, packedSize);
    writeChunk(file, "IEND", NULL, 0);
    fclose(file);
    free(raw);
    free(packed);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 6) {
        fprintf(stderr, "Uso: %s FILE.png [X Y LARGHEZZA ALTEZZA]\n", argv[0]);
        return 2;
    }
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "vela-shot: nessuna sessione Wayland (WAYLAND_DISPLAY)\n");
        return 1;
    }
    wl_registry_add_listener(wl_display_get_registry(display), &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!manager || !shm || !output) {
        fprintf(stderr, "vela-shot: il compositor non offre wlr-screencopy\n");
        return 1;
    }

    struct zwlr_screencopy_frame_v1* capture = zwlr_screencopy_manager_v1_capture_output(manager, 0, output);
    zwlr_screencopy_frame_v1_add_listener(capture, &frameListener, NULL);
    while (!frame.haveBuffer && !frame.failed && wl_display_dispatch(display) != -1) { }
    if (frame.failed) {
        fprintf(stderr, "vela-shot: cattura rifiutata\n");
        return 1;
    }

    const size_t size = (size_t)frame.stride * frame.height;
    const int fd = memfd_create("vela-shot", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        perror("vela-shot");
        return 1;
    }
    unsigned char* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, frame.width, frame.height,
        frame.stride, frame.format);
    zwlr_screencopy_frame_v1_copy(capture, buffer);
    while (!frame.ready && !frame.failed && wl_display_dispatch(display) != -1) { }
    if (frame.failed) {
        fprintf(stderr, "vela-shot: copia fallita\n");
        return 1;
    }

    int x = 0, y = 0, w = frame.width, h = frame.height;
    if (argc == 6) {
        x = atoi(argv[2]);
        y = atoi(argv[3]);
        w = atoi(argv[4]);
        h = atoi(argv[5]);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > (int)frame.width || y + h > (int)frame.height) {
            fprintf(stderr, "vela-shot: zona fuori dallo schermo (%ux%u)\n", frame.width, frame.height);
            return 2;
        }
    }
    const int ok = writePng(argv[1], pixels, x, y, w, h);
    wl_display_disconnect(display);
    return ok ? 0 : 1;
}
