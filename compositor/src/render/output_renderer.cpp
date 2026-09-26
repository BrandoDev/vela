#include "render/output_renderer.hpp"

#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

namespace vela::render {

namespace {

const uint32_t fullscreenVert[] = {
#include "fullscreen.vert.spv.inc"
};
const uint32_t s0Frag[] = {
#include "s0.frag.spv.inc"
};

// Formati preferiti per lo schermo, nell'ordine.
constexpr uint32_t preferredFormats[] = {
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_ABGR8888,
};

} // namespace

const wlr_addon_interface OutputRenderer::s_targetAddon {
    .name = "vela-render-target",
    .destroy = OutputRenderer::destroyTarget,
};

std::unique_ptr<OutputRenderer> OutputRenderer::create(VulkanDevice& vk, wlr_allocator* allocator, wlr_output* output)
{
    std::unique_ptr<OutputRenderer> renderer(new OutputRenderer(vk, allocator, output));
    if (!renderer->init()) {
        return nullptr;
    }
    return renderer;
}

OutputRenderer::OutputRenderer(VulkanDevice& vk, wlr_allocator* allocator, wlr_output* output)
    : m_vk(vk)
    , m_allocator(allocator)
    , m_output(output)
{
}

bool OutputRenderer::init()
{
    const VkCommandPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_vk.queueFamily,
    };
    if (vkCreateCommandPool(m_vk.device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        return false;
    }
    for (Frame& frame : m_frames) {
        const VkCommandBufferAllocateInfo cmdInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        const VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        const VkExportSemaphoreCreateInfo exportInfo {
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };
        const VkSemaphoreCreateInfo exportable {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &exportInfo,
        };
        const VkSemaphoreCreateInfo plain { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (vkAllocateCommandBuffers(m_vk.device, &cmdInfo, &frame.cmd) != VK_SUCCESS
            || vkCreateFence(m_vk.device, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS) {
            return false;
        }
        if (m_vk.syncFile
            && (vkCreateSemaphore(m_vk.device, &plain, nullptr, &frame.acquire) != VK_SUCCESS
                || vkCreateSemaphore(m_vk.device, &exportable, nullptr, &frame.release) != VK_SUCCESS)) {
            return false;
        }
    }

    const VkPushConstantRange push {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = 4 * sizeof(float),
    };
    const VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    return vkCreatePipelineLayout(m_vk.device, &layoutInfo, nullptr, &m_layout) == VK_SUCCESS;
}

OutputRenderer::~OutputRenderer()
{
    vkDeviceWaitIdle(m_vk.device);

    // I buffer possono sopravvivere al renderer (lo schermo ne mostra ancora
    // uno): ci stacchiamo da tutti prima di sparire.
    const std::vector<Target*> targets = m_targets; // destroyTarget li toglie
    for (Target* target : targets) {
        destroyTarget(&target->addon);
    }
    if (m_swapchain) {
        wlr_swapchain_destroy(m_swapchain);
    }
    if (m_pipeline) {
        vkDestroyPipeline(m_vk.device, m_pipeline, nullptr);
    }
    if (m_layout) {
        vkDestroyPipelineLayout(m_vk.device, m_layout, nullptr);
    }
    for (Frame& frame : m_frames) {
        if (frame.fence) {
            vkDestroyFence(m_vk.device, frame.fence, nullptr);
        }
        if (frame.acquire) {
            vkDestroySemaphore(m_vk.device, frame.acquire, nullptr);
        }
        if (frame.release) {
            vkDestroySemaphore(m_vk.device, frame.release, nullptr);
        }
    }
    if (m_pool) {
        vkDestroyCommandPool(m_vk.device, m_pool, nullptr);
    }
}

// ------------------------------------------------------------- swapchain --

bool OutputRenderer::ensureSwapchain()
{
    const int width = m_output->width;
    const int height = m_output->height;
    if (m_swapchain && m_swapchain->width == width && m_swapchain->height == height) {
        return true;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (m_swapchain) {
        // Cambio di dimensione (es. finestra annidata ridimensionata).
        vkDeviceWaitIdle(m_vk.device);
        wlr_swapchain_destroy(m_swapchain);
        m_swapchain = nullptr;
    }

    // Formati in cui sappiamo disegnare e che lo schermo sa mostrare.
    // NULL = lo schermo accetta tutto (es. headless).
    const wlr_drm_format_set* primary = wlr_output_get_primary_formats(m_output, WLR_BUFFER_CAP_DMABUF);
    wlr_drm_format_set usable {};
    if (primary) {
        wlr_drm_format_set_intersect(&usable, &m_vk.renderFormats, primary);
    } else {
        wlr_drm_format_set_union(&usable, &m_vk.renderFormats, &m_vk.renderFormats);
    }

    const wlr_drm_format* chosen = nullptr;
    for (uint32_t drm : preferredFormats) {
        if ((chosen = wlr_drm_format_set_get(&usable, drm))) {
            break;
        }
    }
    if (chosen) {
        m_swapchain = wlr_swapchain_create(m_allocator, width, height, chosen);
        m_format = formatInfo(chosen->format);
    }
    wlr_drm_format_set_finish(&usable);

    if (!m_swapchain) {
        wlr_log(WLR_ERROR, "%s: nessun formato in comune tra la GPU e lo schermo", m_output->name);
        return false;
    }
    wlr_log(WLR_INFO, "%s: swapchain %dx%d, formato 0x%08x", m_output->name, width, height, m_format->drm);
    return true;
}

// --------------------------------------------------------- import buffer --

OutputRenderer::Target* OutputRenderer::targetFor(wlr_buffer* buffer)
{
    if (wlr_addon* addon = wlr_addon_find(&buffer->addons, this, &s_targetAddon)) {
        return reinterpret_cast<Target*>(reinterpret_cast<char*>(addon) - offsetof(Target, addon));
    }

    wlr_dmabuf_attributes dmabuf {};
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        return nullptr;
    }
    // Tutti i piani devono stare nello stesso dmabuf (niente immagini
    // "disjoint": non servono per i formati RGB).
    struct stat first {};
    fstat(dmabuf.fd[0], &first);
    for (int i = 1; i < dmabuf.n_planes; ++i) {
        struct stat other {};
        fstat(dmabuf.fd[i], &other);
        if (other.st_ino != first.st_ino) {
            wlr_log(WLR_ERROR, "Buffer con piani in dmabuf diversi: non supportato");
            return nullptr;
        }
    }

    VkSubresourceLayout planes[WLR_DMABUF_MAX_PLANES] {};
    for (int i = 0; i < dmabuf.n_planes; ++i) {
        planes[i].offset = dmabuf.offset[i];
        planes[i].rowPitch = dmabuf.stride[i];
    }
    const VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = dmabuf.modifier,
        .drmFormatModifierPlaneCount = uint32_t(dmabuf.n_planes),
        .pPlaneLayouts = planes,
    };
    const VkExternalMemoryImageCreateInfo externalInfo {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = &modifierInfo,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &externalInfo,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_format->vk,
        .extent = { uint32_t(dmabuf.width), uint32_t(dmabuf.height), 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    auto target = std::make_unique<Target>();
    target->owner = this;
    target->dmabufFd = dmabuf.fd[0];
    if (vkCreateImage(m_vk.device, &imageInfo, nullptr, &target->image) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "Impossibile importare il buffer dello schermo in Vulkan");
        return nullptr;
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(m_vk.device, target->image, &requirements);
    VkMemoryFdPropertiesKHR fdProps { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    m_vk.getMemoryFdProperties(m_vk.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dmabuf.fd[0], &fdProps);
    const uint32_t memoryType = m_vk.findMemoryType(requirements.memoryTypeBits & fdProps.memoryTypeBits, 0);

    // Vulkan prende possesso del descrittore: gliene diamo una copia.
    const int fd = fcntl(dmabuf.fd[0], F_DUPFD_CLOEXEC, 0);
    const VkMemoryDedicatedAllocateInfo dedicated {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = target->image,
    };
    const VkImportMemoryFdInfoKHR importInfo {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &dedicated,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = fd,
    };
    const VkMemoryAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importInfo,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memoryType,
    };
    if (fd < 0 || memoryType == UINT32_MAX
        || vkAllocateMemory(m_vk.device, &allocInfo, nullptr, &target->memory) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "Impossibile importare la memoria del buffer dello schermo");
        if (fd >= 0) {
            close(fd);
        }
        vkDestroyImage(m_vk.device, target->image, nullptr);
        return nullptr;
    }
    vkBindImageMemory(m_vk.device, target->image, target->memory, 0);

    const VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_format->vk,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCreateImageView(m_vk.device, &viewInfo, nullptr, &target->view);

    Target* raw = target.release();
    wlr_addon_init(&raw->addon, &buffer->addons, this, &s_targetAddon);
    m_targets.push_back(raw);
    return raw;
}

void OutputRenderer::destroyTarget(wlr_addon* addon)
{
    auto* target = reinterpret_cast<Target*>(reinterpret_cast<char*>(addon) - offsetof(Target, addon));
    std::erase(target->owner->m_targets, target);
    VulkanDevice& vk = target->owner->m_vk;
    vkQueueWaitIdle(vk.queue); // raro: solo quando un buffer sparisce
    vkDestroyImageView(vk.device, target->view, nullptr);
    vkDestroyImage(vk.device, target->image, nullptr);
    vkFreeMemory(vk.device, target->memory, nullptr);
    wlr_addon_finish(&target->addon);
    delete target;
}

// ------------------------------------------------------------- pipeline --

bool OutputRenderer::ensurePipeline(VkFormat format)
{
    if (m_pipeline && m_pipelineFormat == format) {
        return true;
    }
    if (m_pipeline) {
        vkDeviceWaitIdle(m_vk.device);
        vkDestroyPipeline(m_vk.device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    VkShaderModule vert = m_vk.createShader(fullscreenVert, sizeof(fullscreenVert));
    VkShaderModule frag = m_vk.createShader(s0Frag, sizeof(s0Frag));
    const VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };
    const VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo raster {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    const VkPipelineDynamicStateCreateInfo dynamic {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };
    const VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format,
    };
    const VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = m_layout,
    };
    const VkResult result = vkCreateGraphicsPipelines(m_vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_vk.device, vert, nullptr);
    vkDestroyShaderModule(m_vk.device, frag, nullptr);
    if (result != VK_SUCCESS) {
        m_pipeline = VK_NULL_HANDLE;
        return false;
    }
    m_pipelineFormat = format;
    return true;
}

// ------------------------------------------------------ sincronizzazione --

// Chi usa ancora il buffer (lo schermo che lo sta mostrando) ce lo dice il
// dmabuf stesso: ne estraiamo le fence come sync_file e la GPU le aspetta.
// Restituisce true se c'è qualcosa da aspettare.
bool OutputRenderer::waitForReaders(const Target& target, Frame& frame)
{
    if (!m_vk.syncFile) {
        return false;
    }
    dma_buf_export_sync_file exportReq { .flags = DMA_BUF_SYNC_WRITE, .fd = -1 };
    if (ioctl(target.dmabufFd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exportReq) != 0 || exportReq.fd < 0) {
        return false;
    }
    const VkImportSemaphoreFdInfoKHR importInfo {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = frame.acquire,
        .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        .fd = exportReq.fd,
    };
    if (m_vk.importSemaphoreFd(m_vk.device, &importInfo) != VK_SUCCESS) {
        close(exportReq.fd);
        return false;
    }
    return true;
}

// La fine del nostro disegno va nel dmabuf: chi lo legge (il kernel per lo
// scanout, il compositor ospite) aspetterà la GPU senza che aspetti la CPU.
void OutputRenderer::publishFence(const Target& target, Frame& frame)
{
    if (m_vk.syncFile) {
        const VkSemaphoreGetFdInfoKHR getInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            .semaphore = frame.release,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };
        int syncFd = -1;
        if (m_vk.getSemaphoreFd(m_vk.device, &getInfo, &syncFd) == VK_SUCCESS && syncFd >= 0) {
            dma_buf_import_sync_file importReq { .flags = DMA_BUF_SYNC_WRITE, .fd = syncFd };
            const bool ok = ioctl(target.dmabufFd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &importReq) == 0;
            close(syncFd);
            if (ok) {
                return;
            }
        }
    }
    // Ripiego: la CPU aspetta che la GPU abbia finito.
    vkWaitForFences(m_vk.device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
}

// ----------------------------------------------------------------- frame --

bool OutputRenderer::render(wlr_output_state* state, int64_t presentNs)
{
    if (!ensureSwapchain() || !ensurePipeline(m_format->vk)) {
        return false;
    }
    Frame& frame = m_frames[m_frameIndex];
    m_frameIndex = (m_frameIndex + 1) % m_frames.size();
    vkWaitForFences(m_vk.device, 1, &frame.fence, VK_TRUE, UINT64_MAX);

    wlr_buffer* buffer = wlr_swapchain_acquire(m_swapchain);
    if (!buffer) {
        return false;
    }
    Target* target = targetFor(buffer);
    if (!target) {
        wlr_buffer_unlock(buffer);
        return false;
    }
    const uint32_t width = uint32_t(buffer->width);
    const uint32_t height = uint32_t(buffer->height);

    VkCommandBuffer cmd = frame.cmd;
    vkResetCommandBuffer(cmd, 0);
    const VkCommandBufferBeginInfo begin {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    // Il buffer arriva "da fuori" (kernel, compositor ospite): lo prendiamo
    // in carico, e alla fine lo restituiamo.
    const VkImageMemoryBarrier2 acquireBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // lo ridisegniamo tutto
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
        .dstQueueFamilyIndex = m_vk.queueFamily,
        .image = target->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo acquireDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &acquireBarrier,
    };
    vkCmdPipelineBarrier2(cmd, &acquireDep);

    const VkRenderingAttachmentInfo attachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = target->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo rendering {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { width, height } },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    };
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const VkViewport vp { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
    const VkRect2D scissor { { 0, 0 }, { width, height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    // Il tempo va passato già ridotto al ciclo della barra (2 s): in float
    // i secondi dall'avvio del sistema perderebbero precisione.
    const float params[4] = {
        float(double(presentNs % 2'000'000'000) / 1e9),
        float(width),
        float(height),
        0.0f,
    };
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), params);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);

    const VkImageMemoryBarrier2 releaseBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = m_vk.queueFamily,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
        .image = target->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    const VkDependencyInfo releaseDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &releaseBarrier,
    };
    vkCmdPipelineBarrier2(cmd, &releaseDep);
    vkEndCommandBuffer(cmd);

    const bool wait = waitForReaders(*target, frame);
    const VkSemaphoreSubmitInfo waitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.acquire,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkSemaphoreSubmitInfo signalInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.release,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkCommandBufferSubmitInfo cmdInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = wait ? 1u : 0u,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = m_vk.syncFile ? 1u : 0u,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    vkResetFences(m_vk.device, 1, &frame.fence);
    if (vkQueueSubmit2(m_vk.queue, 1, &submit, frame.fence) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "%s: invio del frame alla GPU fallito", m_output->name);
        // La fence non verrà mai segnalata: ne serve una nuova.
        vkDestroyFence(m_vk.device, frame.fence, nullptr);
        const VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vkCreateFence(m_vk.device, &fenceInfo, nullptr, &frame.fence);
        wlr_buffer_unlock(buffer);
        return false;
    }
    publishFence(*target, frame);

    wlr_output_state_set_buffer(state, buffer);
    wlr_buffer_unlock(buffer);
    return true;
}

} // namespace vela::render
