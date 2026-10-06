// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/texture.hpp"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cstring>

namespace vela::render {

namespace {

bool updateFromBuffer(wlr_texture* wlr, wlr_buffer* buffer, const pixman_region32_t* damage);
bool readPixels(wlr_texture* wlr, const wlr_texture_read_pixels_options* options);
uint32_t preferredReadFormat(wlr_texture* wlr);
void destroyTexture(wlr_texture* wlr);

const wlr_texture_impl textureImpl {
    .update_from_buffer = updateFromBuffer,
    .read_pixels = readPixels,
    .preferred_read_format = preferredReadFormat,
    .destroy = destroyTexture,
};

void handleBufferDestroy(wlr_addon* addon);
const wlr_addon_interface textureAddon {
    .name = "vela-texture",
    .destroy = handleBufferDestroy,
};

Texture* fromAddon(wlr_addon* addon)
{
    return reinterpret_cast<Texture*>(reinterpret_cast<char*>(addon) - offsetof(Texture, addon));
}

// La texture sparisce: le risorse Vulkan quando la GPU avrà finito di
// usarle, la struttura subito.
void retire(Texture* texture)
{
    if (!texture->renderer) {
        delete texture; // il renderer è già chiuso: non resta nulla da liberare
        return;
    }
    Renderer& renderer = *texture->renderer;
    if (texture->buffer) {
        wlr_addon_finish(&texture->addon);
        texture->buffer = nullptr;
    }
    VkDevice device = renderer.vk().device;
    const VkImageView view = texture->view;
    const VkImage image = texture->image;
    const VkDeviceMemory memory = texture->memory;
    if (image) {
        renderer.defer([device, view, image, memory] {
            vkDestroyImageView(device, view, nullptr);
            vkDestroyImage(device, image, nullptr);
            vkFreeMemory(device, memory, nullptr);
        });
    }
    renderer.trackTexture(texture, false);
    delete texture;
}

void handleBufferDestroy(wlr_addon* addon)
{
    retire(fromAddon(addon));
}

void destroyTexture(wlr_texture* wlr)
{
    Texture* texture = toTexture(wlr);
    --texture->refs;
    if (texture->buffer) {
        // Resta legata al buffer, pronta se l'app lo riusa: sparirà con lui.
        // Sbloccarlo può distruggerlo (e con lui la texture): ultima cosa.
        wlr_buffer_unlock(texture->buffer);
        return;
    }
    retire(texture);
}

bool createView(Texture* texture)
{
    const bool alpha = texture->format->alpha;
    const VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = texture->format->unorm,
        // I formati X... hanno il quarto canale indefinito: vale 1.
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            alpha ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_ONE,
        },
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    return vkCreateImageView(texture->renderer->vk().device, &viewInfo, nullptr, &texture->view) == VK_SUCCESS;
}

Texture* newTexture(Renderer& renderer, const PixelFormat* format, int width, int height)
{
    auto* texture = new Texture {};
    wlr_texture_init(&texture->base, renderer.wlr(), &textureImpl, uint32_t(width), uint32_t(height));
    texture->renderer = &renderer;
    texture->format = format;
    texture->dmabufFd = -1;
    texture->refs = 1;
    renderer.trackTexture(texture, true);
    return texture;
}

// ------------------------------------------------------------- dmabuf --

wlr_texture* importDmabuf(Renderer& renderer, wlr_buffer* buffer, const wlr_dmabuf_attributes& dmabuf)
{
    // Già importato: le app riusano sempre gli stessi buffer.
    if (wlr_addon* addon = wlr_addon_find(&buffer->addons, &renderer, &textureAddon)) {
        Texture* texture = fromAddon(addon);
        ++texture->refs;
        wlr_buffer_lock(buffer);
        return &texture->base;
    }

    VulkanDevice& vk = renderer.vk();
    const PixelFormat* format = pixelFormat(dmabuf.format);
    if (!format || !wlr_drm_format_set_has(&vk.textureFormats, dmabuf.format, dmabuf.modifier)) {
        wlr_log(WLR_DEBUG, "Texture: dmabuf 0x%08x (modifier 0x%" PRIx64 ") non supportato", dmabuf.format,
            dmabuf.modifier);
        return nullptr;
    }
    Texture* texture = newTexture(renderer, format, dmabuf.width, dmabuf.height);
    if (!vk.importDmabuf(dmabuf, format->unorm, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            texture->image, texture->memory)
        || !createView(texture)) {
        wlr_log(WLR_ERROR, "Texture: impossibile importare il dmabuf");
        retire(texture);
        return nullptr;
    }
    texture->foreign = true;
    texture->dmabufFd = dmabuf.fd[0];
    texture->buffer = wlr_buffer_lock(buffer);
    wlr_addon_init(&texture->addon, &buffer->addons, &renderer, &textureAddon);
    return &texture->base;
}

// ------------------------------------------------ memoria condivisa --

// Copia nell'immagine le zone `rects` dei pixel `data`.
bool upload(Texture* texture, const uint8_t* data, size_t stride, const pixman_box32_t* rects, int count)
{
    Renderer& renderer = *texture->renderer;
    const uint32_t bpp = texture->format->bytesPerPixel;
    VkCommandBuffer cmd = renderer.uploadCommands();

    const VkImageMemoryBarrier2 before {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = texture->uploaded ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo beforeDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &before,
    };
    vkCmdPipelineBarrier2(cmd, &beforeDep);

    bool ok = true;
    for (int i = 0; i < count; ++i) {
        const pixman_box32_t& r = rects[i];
        const uint32_t width = uint32_t(r.x2 - r.x1);
        const uint32_t height = uint32_t(r.y2 - r.y1);
        if (width == 0 || height == 0) {
            continue;
        }
        const size_t rowBytes = size_t(width) * bpp;
        Renderer::Staging staging {};
        if (!renderer.stage(rowBytes * height, staging)) {
            ok = false;
            break;
        }
        const uint8_t* src = data + size_t(r.y1) * stride + size_t(r.x1) * bpp;
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(staging.data + y * rowBytes, src + y * stride, rowBytes);
        }
        const VkBufferImageCopy copy {
            .bufferOffset = staging.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { r.x1, r.y1, 0 },
            .imageExtent = { width, height, 1 },
        };
        vkCmdCopyBufferToImage(cmd, staging.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    const VkImageMemoryBarrier2 after {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo afterDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &after,
    };
    vkCmdPipelineBarrier2(cmd, &afterDep);
    texture->uploaded = true;
    return ok;
}

wlr_texture* uploadBuffer(Renderer& renderer, wlr_buffer* buffer)
{
    void* data = nullptr;
    uint32_t drmFormat = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &drmFormat, &stride)) {
        return nullptr;
    }
    const PixelFormat* format = pixelFormat(drmFormat);
    const auto& shm = renderer.vk().shmFormats;
    if (!format || std::find(shm.begin(), shm.end(), drmFormat) == shm.end()) {
        wlr_buffer_end_data_ptr_access(buffer);
        wlr_log(WLR_DEBUG, "Texture: formato 0x%08x non supportato", drmFormat);
        return nullptr;
    }

    VulkanDevice& vk = renderer.vk();
    Texture* texture = newTexture(renderer, format, buffer->width, buffer->height);
    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format->unorm,
        .extent = { uint32_t(buffer->width), uint32_t(buffer->height), 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    bool ok = vkCreateImage(vk.device, &imageInfo, nullptr, &texture->image) == VK_SUCCESS;
    if (ok) {
        VkMemoryRequirements requirements {};
        vkGetImageMemoryRequirements(vk.device, texture->image, &requirements);
        const VkMemoryAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = vk.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        ok = allocInfo.memoryTypeIndex != UINT32_MAX
            && vkAllocateMemory(vk.device, &allocInfo, nullptr, &texture->memory) == VK_SUCCESS
            && vkBindImageMemory(vk.device, texture->image, texture->memory, 0) == VK_SUCCESS && createView(texture);
    }
    if (ok) {
        const pixman_box32_t whole { 0, 0, buffer->width, buffer->height };
        ok = upload(texture, static_cast<const uint8_t*>(data), stride, &whole, 1);
    }
    wlr_buffer_end_data_ptr_access(buffer);
    if (!ok) {
        wlr_log(WLR_ERROR, "Texture: impossibile caricare un buffer %dx%d", buffer->width, buffer->height);
        if (!texture->memory && texture->image) {
            vkDestroyImage(vk.device, texture->image, nullptr);
            texture->image = VK_NULL_HANDLE;
        }
        retire(texture);
        return nullptr;
    }
    return &texture->base;
}

// L'app ha disegnato di nuovo (in questo buffer o in un altro dello stesso
// formato e dimensione): si ricopia solo la parte cambiata.
bool updateFromBuffer(wlr_texture* wlr, wlr_buffer* buffer, const pixman_region32_t* damage)
{
    Texture* texture = toTexture(wlr);
    if (texture->foreign || buffer->width != int(wlr->width) || buffer->height != int(wlr->height)) {
        return false;
    }
    void* data = nullptr;
    uint32_t drmFormat = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &drmFormat, &stride)) {
        return false;
    }
    bool ok = drmFormat == texture->format->drm;
    if (ok) {
        pixman_region32_t region;
        pixman_region32_init(&region);
        pixman_region32_intersect_rect(&region, damage, 0, 0, buffer->width, buffer->height);
        int count = 0;
        const pixman_box32_t* rects = pixman_region32_rectangles(&region, &count);
        // Troppi pezzetti: meglio un'unica copia del rettangolo che li contiene.
        if (count > 16) {
            rects = pixman_region32_extents(&region);
            count = 1;
        }
        ok = upload(texture, static_cast<const uint8_t*>(data), stride, rects, count);
        pixman_region32_fini(&region);
    }
    wlr_buffer_end_data_ptr_access(buffer);
    return ok;
}

// ------------------------------------------------------ lettura pixel --

// Per le catture (screencopy, anteprime): copia una zona della texture
// nella memoria indicata. La CPU aspetta la GPU.
bool readPixels(wlr_texture* wlr, const wlr_texture_read_pixels_options* options)
{
    Texture* texture = toTexture(wlr);
    Renderer& renderer = *texture->renderer;
    VulkanDevice& vk = renderer.vk();

    const PixelFormat* wanted = pixelFormat(options->format);
    if (!wanted || wanted->unorm != texture->format->unorm) {
        wlr_log(WLR_ERROR, "Texture: lettura nel formato 0x%08x non supportata", options->format);
        return false;
    }
    // Da X... ad A...: il quarto canale va messo a "opaco".
    const bool forceOpaque = wanted->alpha && !texture->format->alpha && wanted->bytesPerPixel == 4
        && (wanted->unorm == VK_FORMAT_B8G8R8A8_UNORM || wanted->unorm == VK_FORMAT_R8G8B8A8_UNORM);

    wlr_box box {};
    wlr_texture_read_pixels_options_get_src_box(options, wlr, &box);
    const uint32_t bpp = texture->format->bytesPerPixel;
    const size_t rowBytes = size_t(box.width) * bpp;
    Renderer::Staging staging {};
    if (box.width <= 0 || box.height <= 0 || !renderer.stage(rowBytes * size_t(box.height), staging)) {
        return false;
    }

    VkCommandBuffer cmd = renderer.beginCommands();
    const VkImageLayout restLayout
        = texture->foreign ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const uint32_t foreignFamily = texture->foreign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED;
    const uint32_t ownFamily = texture->foreign ? vk.queueFamily : VK_QUEUE_FAMILY_IGNORED;
    const VkImageMemoryBarrier2 before {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = texture->foreign ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = texture->foreign ? VK_ACCESS_2_NONE : VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = restLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = foreignFamily,
        .dstQueueFamilyIndex = ownFamily,
        .image = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo beforeDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &before,
    };
    vkCmdPipelineBarrier2(cmd, &beforeDep);
    const VkBufferImageCopy copy {
        .bufferOffset = staging.offset,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { box.x, box.y, 0 },
        .imageExtent = { uint32_t(box.width), uint32_t(box.height), 1 },
    };
    vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
    const VkImageMemoryBarrier2 after {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = texture->foreign ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = restLayout,
        .srcQueueFamilyIndex = ownFamily,
        .dstQueueFamilyIndex = foreignFamily,
        .image = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    // La copia verso il buffer deve essere visibile alla CPU.
    const VkBufferMemoryBarrier2 hostBarrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = staging.buffer,
        .offset = staging.offset,
        .size = rowBytes * size_t(box.height),
    };
    const VkDependencyInfo afterDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &hostBarrier,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &after,
    };
    vkCmdPipelineBarrier2(cmd, &afterDep);

    // Un dmabuf: prima si aspetta chi ci sta scrivendo; dopo, chi vorrà
    // scriverci aspetterà la nostra lettura.
    std::vector<int> waits;
    if (texture->foreign && vk.syncFile) {
        dma_buf_export_sync_file exportReq { .flags = DMA_BUF_SYNC_READ, .fd = -1 };
        if (ioctl(texture->dmabufFd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exportReq) == 0 && exportReq.fd >= 0) {
            waits.push_back(exportReq.fd);
        }
    }
    int releaseFd = -1;
    const uint64_t point = renderer.submit(cmd, waits, texture->foreign ? &releaseFd : nullptr);
    if (point == 0) {
        return false;
    }
    if (releaseFd >= 0) {
        dma_buf_import_sync_file importReq { .flags = DMA_BUF_SYNC_READ, .fd = releaseFd };
        ioctl(texture->dmabufFd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &importReq);
        close(releaseFd);
    }
    renderer.waitFor(point);

    auto* dst = static_cast<uint8_t*>(wlr_texture_read_pixel_options_get_data(options));
    for (int y = 0; y < box.height; ++y) {
        uint8_t* row = dst + size_t(y) * options->stride;
        std::memcpy(row, staging.data + size_t(y) * rowBytes, rowBytes);
        if (forceOpaque) {
            for (size_t x = 3; x < rowBytes; x += 4) {
                row[x] = 0xff;
            }
        }
    }
    return true;
}

uint32_t preferredReadFormat(wlr_texture* wlr)
{
    return toTexture(wlr)->format->drm;
}

} // namespace

Texture* toTexture(wlr_texture* texture)
{
    if (!texture || texture->impl != &textureImpl) {
        return nullptr;
    }
    return reinterpret_cast<Texture*>(texture);
}

wlr_texture* createTexture(Renderer& renderer, wlr_buffer* buffer)
{
    wlr_dmabuf_attributes dmabuf {};
    if (wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        return importDmabuf(renderer, buffer, dmabuf);
    }
    return uploadBuffer(renderer, buffer);
}

void releaseTexture(Texture* texture)
{
    VkDevice device = texture->renderer->vk().device;
    if (texture->image) {
        vkDestroyImageView(device, texture->view, nullptr);
        vkDestroyImage(device, texture->image, nullptr);
        vkFreeMemory(device, texture->memory, nullptr);
        texture->view = VK_NULL_HANDLE;
        texture->image = VK_NULL_HANDLE;
        texture->memory = VK_NULL_HANDLE;
    }
    if (texture->buffer) {
        wlr_addon_finish(&texture->addon);
        texture->buffer = nullptr;
        if (texture->refs <= 0) {
            delete texture; // la usava solo la cache del buffer
            return;
        }
    }
    texture->renderer = nullptr; // wlroots la distruggerà più tardi
}

} // namespace vela::render
