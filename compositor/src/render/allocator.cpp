#include "render/allocator.hpp"

#include <drm_fourcc.h>
#include <gbm.h>

#include <vector>

namespace vela::render {

namespace {

struct GbmAllocator {
    wlr_allocator base; // primo membro: wlr_allocator* <-> GbmAllocator*
    gbm_device* gbm;
};

struct GbmBuffer {
    wlr_buffer base; // primo membro: wlr_buffer* <-> GbmBuffer*
    gbm_bo* bo;
    wlr_dmabuf_attributes dmabuf;
};

void destroyBuffer(wlr_buffer* wlrBuffer)
{
    auto* buffer = reinterpret_cast<GbmBuffer*>(wlrBuffer);
    for (int i = 0; i < buffer->dmabuf.n_planes; ++i) {
        close(buffer->dmabuf.fd[i]);
    }
    gbm_bo_destroy(buffer->bo);
    delete buffer;
}

bool getDmabuf(wlr_buffer* wlrBuffer, wlr_dmabuf_attributes* attribs)
{
    *attribs = reinterpret_cast<GbmBuffer*>(wlrBuffer)->dmabuf;
    return true;
}

constexpr wlr_buffer_impl bufferImpl {
    .destroy = destroyBuffer,
    .get_dmabuf = getDmabuf,
    .get_shm = nullptr,
    .begin_data_ptr_access = nullptr,
    .end_data_ptr_access = nullptr,
};

wlr_buffer* createBuffer(wlr_allocator* wlrAlloc, int width, int height, const wlr_drm_format* format)
{
    auto* alloc = reinterpret_cast<GbmAllocator*>(wlrAlloc);

    // Solo modifier espliciti: Vulkan deve sapere com'è fatto il buffer.
    std::vector<uint64_t> modifiers;
    for (size_t i = 0; i < format->len; ++i) {
        if (format->modifiers[i] != DRM_FORMAT_MOD_INVALID) {
            modifiers.push_back(format->modifiers[i]);
        }
    }
    if (modifiers.empty()) {
        wlr_log(WLR_ERROR, "Allocatore: nessun modifier esplicito per il formato 0x%08x", format->format);
        return nullptr;
    }

    gbm_bo* bo = gbm_bo_create_with_modifiers2(alloc->gbm, uint32_t(width), uint32_t(height), format->format,
        modifiers.data(), unsigned(modifiers.size()), GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!bo) {
        // Alcuni driver rifiutano SCANOUT su certi modifier (es. headless).
        bo = gbm_bo_create_with_modifiers2(alloc->gbm, uint32_t(width), uint32_t(height), format->format,
            modifiers.data(), unsigned(modifiers.size()), GBM_BO_USE_RENDERING);
    }
    if (!bo) {
        wlr_log_errno(WLR_ERROR, "Allocatore: gbm_bo_create_with_modifiers2 %dx%d", width, height);
        return nullptr;
    }

    auto* buffer = new GbmBuffer {};
    buffer->bo = bo;
    wlr_dmabuf_attributes& d = buffer->dmabuf;
    d.width = width;
    d.height = height;
    d.format = format->format;
    d.modifier = gbm_bo_get_modifier(bo);
    d.n_planes = gbm_bo_get_plane_count(bo);
    for (int i = 0; i < d.n_planes; ++i) {
        d.fd[i] = gbm_bo_get_fd_for_plane(bo, i);
        d.offset[i] = gbm_bo_get_offset(bo, i);
        d.stride[i] = gbm_bo_get_stride_for_plane(bo, i);
        if (d.fd[i] < 0) {
            wlr_log_errno(WLR_ERROR, "Allocatore: esportazione del piano %d", i);
            for (int j = 0; j < i; ++j) {
                close(d.fd[j]);
            }
            gbm_bo_destroy(bo);
            delete buffer;
            return nullptr;
        }
    }
    wlr_buffer_init(&buffer->base, &bufferImpl, width, height);
    return &buffer->base;
}

void destroyAllocator(wlr_allocator* wlrAlloc)
{
    auto* alloc = reinterpret_cast<GbmAllocator*>(wlrAlloc);
    gbm_device_destroy(alloc->gbm);
    delete alloc;
}

constexpr wlr_allocator_interface allocatorImpl {
    .create_buffer = createBuffer,
    .destroy = destroyAllocator,
};

} // namespace

wlr_allocator* createGbmAllocator(int renderFd)
{
    gbm_device* gbm = gbm_create_device(renderFd);
    if (!gbm) {
        wlr_log(WLR_ERROR, "Allocatore: impossibile creare il device GBM");
        return nullptr;
    }
    auto* alloc = new GbmAllocator {};
    alloc->gbm = gbm;
    wlr_allocator_init(&alloc->base, &allocatorImpl, WLR_BUFFER_CAP_DMABUF);
    return &alloc->base;
}

} // namespace vela::render
