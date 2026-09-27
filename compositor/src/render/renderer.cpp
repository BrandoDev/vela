#include "render/renderer.hpp"

#include "render/pass.hpp"
#include "render/texture.hpp"

#include <poll.h>
#include <xf86drm.h>

namespace vela::render {

namespace {

const uint32_t quadVert[] = {
#include "quad.vert.spv.inc"
};
const uint32_t textureFrag[] = {
#include "texture.frag.spv.inc"
};
const uint32_t rectFrag[] = {
#include "rect.frag.spv.inc"
};

constexpr VkDeviceSize stagingChunkSize = 8 * 1024 * 1024;
constexpr size_t idleStagingChunksKept = 2;

// ------------------------------------------------------- wlr_renderer --

const wlr_drm_format_set* getTextureFormats(wlr_renderer* wlr, uint32_t caps)
{
    Renderer* renderer = Renderer::from(wlr);
    if (caps == WLR_BUFFER_CAP_DMABUF) {
        return &renderer->vk().textureFormats;
    }
    if (caps == WLR_BUFFER_CAP_DATA_PTR) {
        return renderer->shmFormatSet();
    }
    return nullptr;
}

const wlr_drm_format_set* getRenderFormats(wlr_renderer* wlr)
{
    return &Renderer::from(wlr)->vk().renderFormats;
}

void destroyRenderer(wlr_renderer* wlr)
{
    delete Renderer::from(wlr);
}

int getDrmFd(wlr_renderer* wlr)
{
    return Renderer::from(wlr)->vk().renderFd;
}

wlr_texture* textureFromBuffer(wlr_renderer* wlr, wlr_buffer* buffer)
{
    return createTexture(*Renderer::from(wlr), buffer);
}

wlr_render_pass* beginBufferPass(wlr_renderer* wlr, wlr_buffer* buffer, const wlr_buffer_pass_options* options)
{
    std::unique_ptr<Pass> pass = Renderer::from(wlr)->beginPass(buffer);
    if (pass && options && options->signal_timeline) {
        pass->signalOnDone(options->signal_timeline, options->signal_point);
    }
    return pass ? pass.release()->wlr() : nullptr;
}

const wlr_renderer_impl rendererImpl {
    .get_texture_formats = getTextureFormats,
    .get_render_formats = getRenderFormats,
    .destroy = destroyRenderer,
    .get_drm_fd = getDrmFd,
    .texture_from_buffer = textureFromBuffer,
    .begin_buffer_pass = beginBufferPass,
    .render_timer_create = nullptr,
};

} // namespace

const wlr_addon_interface Renderer::s_targetAddon {
    .name = "vela-render-target",
    .destroy = Renderer::destroyTarget,
};

Renderer* Renderer::create(VulkanDevice& vk)
{
    auto* renderer = new Renderer(vk);
    if (!renderer->init()) {
        delete renderer;
        return nullptr;
    }
    return renderer;
}

Renderer* Renderer::from(wlr_renderer* renderer)
{
    return reinterpret_cast<Shim*>(renderer)->self;
}

Renderer::Renderer(VulkanDevice& vk)
    : m_vk(vk)
{
    wlr_renderer_init(&m_shim.base, &rendererImpl, WLR_BUFFER_CAP_DMABUF);
    m_shim.self = this;
    for (uint32_t format : vk.shmFormats) {
        wlr_drm_format_set_add(&m_shmFormats, format, DRM_FORMAT_MOD_LINEAR);
    }
}

bool Renderer::init()
{
    const VkCommandPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_vk.queueFamily,
    };
    if (vkCreateCommandPool(m_vk.device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        return false;
    }

    const VkSemaphoreTypeCreateInfo timelineType {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    const VkSemaphoreCreateInfo timelineInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timelineType,
    };
    if (vkCreateSemaphore(m_vk.device, &timelineInfo, nullptr, &m_timeline) != VK_SUCCESS) {
        return false;
    }

    // Sincronizzazione esplicita con le app (linux-drm-syncobj-v1): serve una
    // timeline del kernel su cui far scattare i punti di rilascio, e i
    // sync_file per passare le fence tra Vulkan e il kernel.
    uint64_t syncobjTimeline = 0;
    if (m_vk.syncFile && drmGetCap(m_vk.renderFd, DRM_CAP_SYNCOBJ_TIMELINE, &syncobjTimeline) == 0
        && syncobjTimeline) {
        m_syncTimeline = wlr_drm_syncobj_timeline_create(m_vk.renderFd);
    }
    m_shim.base.features.timeline = m_syncTimeline != nullptr;
    if (!m_syncTimeline) {
        wlr_log(WLR_INFO, "Renderer: niente timeline syncobj, niente sincronizzazione esplicita con le app");
    }

    // Timestamp della GPU: due per disegno misurato (inizio e fine).
    if (m_vk.timestampPeriod > 0.0f) {
        const VkQueryPoolCreateInfo queryInfo {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = timingSlots * 2,
        };
        if (vkCreateQueryPool(m_vk.device, &queryInfo, nullptr, &m_queryPool) != VK_SUCCESS) {
            m_queryPool = VK_NULL_HANDLE;
        }
    }

    // Nearest per le copie 1:1 (nitidezza esatta, §3.3); bilineare per il
    // resto finché non arrivano i filtri di qualità (tappa S2).
    for (const bool linear : { false, true }) {
        const VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 0.25f,
        };
        if (vkCreateSampler(m_vk.device, &samplerInfo, nullptr, linear ? &m_linear : &m_nearest) != VK_SUCCESS) {
            return false;
        }
    }

    // Una texture per disegno, legata con i push descriptor (Vulkan 1.4):
    // niente pool di descrittori da gestire.
    const VkDescriptorSetLayoutBinding binding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo setInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(m_vk.device, &setInfo, nullptr, &m_setLayout) != VK_SUCCESS) {
        return false;
    }
    const VkPushConstantRange push {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(QuadPush),
    };
    const VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    if (vkCreatePipelineLayout(m_vk.device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        return false;
    }

    m_vert = m_vk.createShader(quadVert, sizeof(quadVert));
    m_textureFrag = m_vk.createShader(textureFrag, sizeof(textureFrag));
    m_rectFrag = m_vk.createShader(rectFrag, sizeof(rectFrag));
    return m_vert && m_textureFrag && m_rectFrag;
}

Renderer::~Renderer()
{
    if (m_vk.device) {
        vkDeviceWaitIdle(m_vk.device);
    }
    collect();

    // Buffer ancora vivi (es. dello schermo): ci stacchiamo da tutti. (Il
    // renderer si distrugge solo con VELA_VULKAN_VALIDATION: vedi
    // Server::shutdown.)
    const std::vector<RenderTarget*> targets = m_targets;
    for (RenderTarget* target : targets) {
        destroyTarget(&target->addon);
    }
    releaseTextures();
    collect();

    for (StagingChunk& chunk : m_staging) {
        vkDestroyBuffer(m_vk.device, chunk.buffer, nullptr);
        vkFreeMemory(m_vk.device, chunk.memory, nullptr);
    }
    for (VkSemaphore semaphore : m_freeWaitSemaphores) {
        vkDestroySemaphore(m_vk.device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_freeReleaseSemaphores) {
        vkDestroySemaphore(m_vk.device, semaphore, nullptr);
    }
    for (auto& [key, pipeline] : m_pipelines) {
        vkDestroyPipeline(m_vk.device, pipeline, nullptr);
    }
    vkDestroyShaderModule(m_vk.device, m_vert, nullptr);
    vkDestroyShaderModule(m_vk.device, m_textureFrag, nullptr);
    vkDestroyShaderModule(m_vk.device, m_rectFrag, nullptr);
    vkDestroyPipelineLayout(m_vk.device, m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_vk.device, m_setLayout, nullptr);
    vkDestroySampler(m_vk.device, m_nearest, nullptr);
    vkDestroySampler(m_vk.device, m_linear, nullptr);
    vkDestroySemaphore(m_vk.device, m_timeline, nullptr);
    if (m_queryPool) {
        vkDestroyQueryPool(m_vk.device, m_queryPool, nullptr);
    }
    if (m_syncTimeline) {
        // Nessuno deve restare ad aspettare un nostro punto.
        wlr_drm_syncobj_timeline_signal(m_syncTimeline, UINT64_MAX);
        wlr_drm_syncobj_timeline_unref(m_syncTimeline);
    }
    vkDestroyCommandPool(m_vk.device, m_pool, nullptr);
    wlr_drm_format_set_finish(&m_shmFormats);
}

std::unique_ptr<Pass> Renderer::beginPass(wlr_buffer* buffer)
{
    RenderTarget* target = targetFor(buffer);
    if (!target) {
        return nullptr;
    }
    return std::make_unique<Pass>(*this, target, buffer);
}

// ------------------------------------------ sincronizzazione esplicita --

uint64_t Renderer::signalSyncPoint(int syncFile)
{
    if (!m_syncTimeline) {
        return 0;
    }
    const uint64_t point = ++m_syncPoint;
    const bool ok = syncFile >= 0 ? wlr_drm_syncobj_timeline_import_sync_file(m_syncTimeline, point, syncFile)
                                  : wlr_drm_syncobj_timeline_signal(m_syncTimeline, point);
    if (!ok) {
        // Meglio un rilascio anticipato che un'app bloccata per sempre: la
        // CPU aspetta la GPU e il punto scatta subito.
        waitFor(m_lastPoint);
        wlr_drm_syncobj_timeline_signal(m_syncTimeline, point);
    }
    return point;
}

// ------------------------------------------------------ tempi della GPU --

int Renderer::timingSlot()
{
    if (!m_queryPool) {
        return -1;
    }
    const uint64_t done = completed();
    for (int i = 0; i < timingSlots; ++i) {
        const int slot = (m_nextTiming + i) % timingSlots;
        if (m_timing[slot] <= done) {
            m_timing[slot] = UINT64_MAX; // in registrazione
            m_nextTiming = (slot + 1) % timingSlots;
            return slot;
        }
    }
    return -1;
}

void Renderer::writeTimestamp(VkCommandBuffer cmd, int slot, bool end)
{
    if (slot < 0) {
        return;
    }
    if (!end) {
        vkCmdResetQueryPool(cmd, m_queryPool, uint32_t(slot * 2), 2);
    }
    vkCmdWriteTimestamp2(cmd, end ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        m_queryPool, uint32_t(slot * 2 + (end ? 1 : 0)));
}

void Renderer::timingSubmitted(int slot, uint64_t point)
{
    if (slot >= 0) {
        m_timing[slot] = point; // 0: invio fallito, slot di nuovo libero
    }
}

bool Renderer::readTiming(int slot, uint64_t point, GpuTiming& out)
{
    // Lo slot potrebbe essere già stato riusato da un disegno successivo.
    if (slot < 0 || point == 0 || m_timing[slot] != point || point > completed()) {
        return false;
    }
    uint64_t ticks[2] {};
    if (vkGetQueryPoolResults(m_vk.device, m_queryPool, uint32_t(slot * 2), 2, sizeof(ticks), ticks,
            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT)
        != VK_SUCCESS) {
        return false;
    }
    const uint64_t mask = m_vk.timestampMask;
    if (m_vk.getCalibratedTimestamps) {
        out.startNs = gpuToMonotonic(ticks[0] & mask);
        out.endNs = gpuToMonotonic(ticks[1] & mask);
        out.absolute = true;
    } else {
        out.startNs = 0;
        out.endNs = int64_t(double((ticks[1] - ticks[0]) & mask) * m_vk.timestampPeriod);
        out.absolute = false;
    }
    return true;
}

// Dai tick della GPU a CLOCK_MONOTONIC. I due orologi derivano un poco:
// la calibrazione si rifà ogni secondo.
int64_t Renderer::gpuToMonotonic(uint64_t ticks)
{
    const uint64_t mask = m_vk.timestampMask;
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t nowNs = int64_t(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
    if (m_calibratedAt == 0 || nowNs - m_calibratedAt > 1'000'000'000) {
        const VkCalibratedTimestampInfoKHR infos[2] {
            { .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR, .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR },
            { .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR },
        };
        uint64_t values[2] {};
        uint64_t deviation = 0;
        if (m_vk.getCalibratedTimestamps(m_vk.device, 2, infos, values, &deviation) == VK_SUCCESS) {
            m_calibrationTicks = values[0] & mask;
            m_calibrationNs = int64_t(values[1]);
            m_calibratedAt = nowNs;
        }
    }
    // Differenza con segno, anche se il contatore ha meno di 64 bit.
    uint64_t diff = (ticks - m_calibrationTicks) & mask;
    int64_t signedDiff = int64_t(diff);
    if (mask != UINT64_MAX && diff > mask / 2) {
        signedDiff = int64_t(diff) - int64_t(mask) - 1;
    }
    return m_calibrationNs + int64_t(double(signedDiff) * m_vk.timestampPeriod);
}

// ------------------------------------------------------------ texture --

void Renderer::trackTexture(Texture* texture, bool alive)
{
    if (alive) {
        m_textures.insert(texture);
    } else {
        m_textures.erase(texture);
    }
}

// Alla chiusura: le texture ancora vive perdono le risorse Vulkan. Quelle
// che wlroots non usa più (restano solo legate al buffer) spariscono del
// tutto; le altre restano vuote, e verranno distrutte da wlroots.
void Renderer::releaseTextures()
{
    const std::vector<Texture*> textures(m_textures.begin(), m_textures.end());
    for (Texture* texture : textures) {
        releaseTexture(texture);
    }
    m_textures.clear();
}

// ------------------------------------------------------------ targets --

RenderTarget* Renderer::targetFor(wlr_buffer* buffer)
{
    if (wlr_addon* addon = wlr_addon_find(&buffer->addons, this, &s_targetAddon)) {
        return reinterpret_cast<RenderTarget*>(reinterpret_cast<char*>(addon) - offsetof(RenderTarget, addon));
    }

    wlr_dmabuf_attributes dmabuf {};
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        wlr_log(WLR_ERROR, "Renderer: si può disegnare solo su buffer dmabuf");
        return nullptr;
    }
    const PixelFormat* format = pixelFormat(dmabuf.format);
    if (!format || format->srgb == VK_FORMAT_UNDEFINED
        || !wlr_drm_format_set_has(&m_vk.renderFormats, dmabuf.format, dmabuf.modifier)) {
        wlr_log(WLR_ERROR, "Renderer: formato 0x%08x (modifier 0x%" PRIx64 ") non disegnabile", dmabuf.format,
            dmabuf.modifier);
        return nullptr;
    }

    auto* target = new RenderTarget {};
    target->owner = this;
    target->format = format->srgb;
    target->dmabufFd = dmabuf.fd[0];
    target->width = uint32_t(dmabuf.width);
    target->height = uint32_t(dmabuf.height);
    if (!m_vk.importDmabuf(dmabuf, format->srgb, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, target->image,
            target->memory)) {
        wlr_log(WLR_ERROR, "Renderer: impossibile importare il buffer di destinazione");
        delete target;
        return nullptr;
    }
    const VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format->srgb,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCreateImageView(m_vk.device, &viewInfo, nullptr, &target->view);

    wlr_addon_init(&target->addon, &buffer->addons, this, &s_targetAddon);
    m_targets.push_back(target);
    return target;
}

void Renderer::destroyTarget(wlr_addon* addon)
{
    auto* target = reinterpret_cast<RenderTarget*>(reinterpret_cast<char*>(addon) - offsetof(RenderTarget, addon));
    Renderer* renderer = target->owner;
    std::erase(renderer->m_targets, target);
    wlr_addon_finish(&target->addon);
    // La GPU potrebbe starci ancora disegnando.
    VkDevice device = renderer->m_vk.device;
    const VkImageView view = target->view;
    const VkImage image = target->image;
    const VkDeviceMemory memory = target->memory;
    renderer->defer([device, view, image, memory] {
        vkDestroyImageView(device, view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
    });
    delete target;
}

// ----------------------------------------------------- command buffer --

VkCommandBuffer Renderer::beginCommands()
{
    const uint64_t done = completed();
    Commands* free = nullptr;
    for (Commands& entry : m_commands) {
        if (!entry.busy && entry.point <= done) {
            free = &entry;
            break;
        }
    }
    if (!free) {
        const VkCommandBufferAllocateInfo info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_vk.device, &info, &cmd) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        free = &m_commands.emplace_back(Commands { cmd, 0, false });
    }
    free->busy = true;
    free->point = 0;
    vkResetCommandBuffer(free->cmd, 0);
    const VkCommandBufferBeginInfo begin {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(free->cmd, &begin);
    return free->cmd;
}

VkCommandBuffer Renderer::uploadCommands()
{
    if (!m_upload) {
        m_upload = beginCommands();
    }
    return m_upload;
}

bool Renderer::stage(VkDeviceSize size, Staging& out)
{
    size = (size + 15) & ~VkDeviceSize(15);
    const uint64_t done = completed();

    StagingChunk* chosen = nullptr;
    // Prima il blocco che stiamo già riempiendo, poi uno che la GPU ha
    // finito di leggere, altrimenti uno nuovo.
    for (StagingChunk& chunk : m_staging) {
        if (chunk.point == UINT64_MAX && chunk.size - chunk.used >= size) {
            chosen = &chunk;
            break;
        }
    }
    if (!chosen) {
        for (StagingChunk& chunk : m_staging) {
            if (chunk.point != UINT64_MAX && chunk.point <= done && chunk.size >= size) {
                chunk.used = 0;
                chosen = &chunk;
                break;
            }
        }
    }
    if (!chosen) {
        StagingChunk chunk {};
        chunk.size = std::max(stagingChunkSize, size);
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = chunk.size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        if (vkCreateBuffer(m_vk.device, &bufferInfo, nullptr, &chunk.buffer) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(m_vk.device, chunk.buffer, &requirements);
        const uint32_t type = m_vk.findMemoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        const VkMemoryAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = type,
        };
        void* mapped = nullptr;
        if (type == UINT32_MAX || vkAllocateMemory(m_vk.device, &allocInfo, nullptr, &chunk.memory) != VK_SUCCESS) {
            vkDestroyBuffer(m_vk.device, chunk.buffer, nullptr);
            return false;
        }
        vkBindBufferMemory(m_vk.device, chunk.buffer, chunk.memory, 0);
        vkMapMemory(m_vk.device, chunk.memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        chunk.data = static_cast<uint8_t*>(mapped);
        chosen = &m_staging.emplace_back(chunk);
    }

    chosen->point = UINT64_MAX; // in uso da comandi non ancora inviati
    out = { chosen->buffer, chosen->used, chosen->data + chosen->used };
    chosen->used += size;
    return true;
}

// ------------------------------------------------------------- invio --

uint64_t Renderer::submit(VkCommandBuffer cmd, std::vector<int>& waitSyncFiles, int* releaseFd)
{
    if (releaseFd) {
        *releaseFd = -1;
    }
    vkEndCommandBuffer(cmd);

    std::vector<VkCommandBufferSubmitInfo> cmds;
    if (m_upload) {
        vkEndCommandBuffer(m_upload);
        cmds.push_back({ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = m_upload });
    }
    cmds.push_back({ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd });

    // Le fence del kernel (sync_file) diventano semafori che la GPU aspetta.
    std::vector<VkSemaphore> waits;
    std::vector<VkSemaphoreSubmitInfo> waitInfos;
    for (int fd : waitSyncFiles) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (!m_freeWaitSemaphores.empty()) {
            semaphore = m_freeWaitSemaphores.back();
            m_freeWaitSemaphores.pop_back();
        } else {
            const VkSemaphoreCreateInfo info { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCreateSemaphore(m_vk.device, &info, nullptr, &semaphore);
        }
        const VkImportSemaphoreFdInfoKHR importInfo {
            .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
            .semaphore = semaphore,
            .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            .fd = fd,
        };
        if (!semaphore || m_vk.importSemaphoreFd(m_vk.device, &importInfo) != VK_SUCCESS) {
            // Ripiego: aspetta la CPU.
            pollfd pfd { .fd = fd, .events = POLLIN, .revents = 0 };
            poll(&pfd, 1, -1);
            close(fd);
            if (semaphore) {
                m_freeWaitSemaphores.push_back(semaphore);
            }
            continue;
        }
        waits.push_back(semaphore);
        waitInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = semaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
    }
    waitSyncFiles.clear();

    const uint64_t point = m_lastPoint + 1;
    std::vector<VkSemaphoreSubmitInfo> signals {
        { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = m_timeline,
            .value = point,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT },
    };
    VkSemaphore release = VK_NULL_HANDLE;
    if (releaseFd && m_vk.syncFile) {
        if (!m_freeReleaseSemaphores.empty()) {
            release = m_freeReleaseSemaphores.back();
            m_freeReleaseSemaphores.pop_back();
        } else {
            const VkExportSemaphoreCreateInfo exportInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            };
            const VkSemaphoreCreateInfo info { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &exportInfo };
            vkCreateSemaphore(m_vk.device, &info, nullptr, &release);
        }
        if (release) {
            signals.push_back({
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = release,
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            });
        }
    }

    const VkSubmitInfo2 submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = uint32_t(waitInfos.size()),
        .pWaitSemaphoreInfos = waitInfos.data(),
        .commandBufferInfoCount = uint32_t(cmds.size()),
        .pCommandBufferInfos = cmds.data(),
        .signalSemaphoreInfoCount = uint32_t(signals.size()),
        .pSignalSemaphoreInfos = signals.data(),
    };
    const bool ok = vkQueueSubmit2(m_vk.queue, 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS;

    // I command buffer tornano disponibili quando la GPU li ha eseguiti.
    const VkCommandBuffer upload = m_upload;
    m_upload = VK_NULL_HANDLE;
    for (Commands& entry : m_commands) {
        if (entry.cmd == cmd || entry.cmd == upload) {
            entry.busy = false;
            entry.point = ok ? point : 0;
        }
    }

    if (!ok) {
        wlr_log(WLR_ERROR, "Renderer: invio alla GPU fallito");
        // Semafori in uno stato incerto: meglio buttarli.
        for (VkSemaphore semaphore : waits) {
            vkDestroySemaphore(m_vk.device, semaphore, nullptr);
        }
        if (release) {
            vkDestroySemaphore(m_vk.device, release, nullptr);
        }
        for (StagingChunk& chunk : m_staging) {
            if (chunk.point == UINT64_MAX) {
                chunk.point = m_lastPoint;
            }
        }
        return 0;
    }

    m_lastPoint = point;
    for (StagingChunk& chunk : m_staging) {
        if (chunk.point == UINT64_MAX) {
            chunk.point = point;
        }
    }
    defer([this, waits] {
        m_freeWaitSemaphores.insert(m_freeWaitSemaphores.end(), waits.begin(), waits.end());
    });

    if (release) {
        const VkSemaphoreGetFdInfoKHR getInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            .semaphore = release,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };
        if (m_vk.getSemaphoreFd(m_vk.device, &getInfo, releaseFd) != VK_SUCCESS) {
            *releaseFd = -1;
        }
        defer([this, release] { m_freeReleaseSemaphores.push_back(release); });
    }
    // Diagnosi: VELA_DEBUG_SYNC=1 fa aspettare la GPU alla CPU a ogni invio,
    // così chi legge i nostri buffer li trova certamente finiti.
    static const bool cpuSync = std::getenv("VELA_DEBUG_SYNC") && *std::getenv("VELA_DEBUG_SYNC") == '1';
    if ((releaseFd && *releaseFd < 0) || cpuSync) {
        waitFor(point); // senza sync_file: la CPU aspetta la GPU
    }
    return point;
}

uint64_t Renderer::completed()
{
    uint64_t value = 0;
    if (vkGetSemaphoreCounterValue(m_vk.device, m_timeline, &value) == VK_SUCCESS) {
        m_completed = value;
    }
    return m_completed;
}

void Renderer::waitFor(uint64_t point)
{
    if (point == 0 || point <= m_completed) {
        return;
    }
    const VkSemaphoreWaitInfo info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &m_timeline,
        .pValues = &point,
    };
    vkWaitSemaphores(m_vk.device, &info, UINT64_MAX);
    completed();
}

void Renderer::defer(std::function<void()> fn)
{
    // Anche i caricamenti registrati ma non ancora inviati contano: finiranno
    // nel prossimo invio.
    const uint64_t point = m_upload ? m_lastPoint + 1 : m_lastPoint;
    if (point <= completed()) {
        fn();
        return;
    }
    m_deferred.push_back({ point, std::move(fn) });
}

void Renderer::collect()
{
    const uint64_t done = completed();
    std::vector<Deferred> ready;
    std::erase_if(m_deferred, [&](Deferred& entry) {
        if (entry.point <= done) {
            ready.push_back(std::move(entry));
            return true;
        }
        return false;
    });
    for (Deferred& entry : ready) {
        entry.fn();
    }

    // Blocchi di appoggio inutilizzati: se ne tengono pochi.
    size_t idle = 0;
    std::erase_if(m_staging, [&](StagingChunk& chunk) {
        if (chunk.point == UINT64_MAX || chunk.point > done) {
            return false;
        }
        if (++idle <= idleStagingChunksKept) {
            return false;
        }
        vkDestroyBuffer(m_vk.device, chunk.buffer, nullptr);
        vkFreeMemory(m_vk.device, chunk.memory, nullptr);
        return true;
    });
}

// ----------------------------------------------------------- pipeline --

VkPipeline Renderer::pipeline(VkFormat target, PipelineKind kind, bool blend)
{
    const auto key = std::make_tuple(target, kind, blend);
    if (auto it = m_pipelines.find(key); it != m_pipelines.end()) {
        return it->second;
    }

    const VkPipelineShaderStageCreateInfo stages[] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_vert,
            .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = kind == PipelineKind::Texture ? m_textureFrag : m_rectFrag,
            .pName = "main" },
    };
    const VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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
    // Alfa premoltiplicato, fuso in spazio lineare (la vista è _SRGB).
    const VkPipelineColorBlendAttachmentState blendAttachment {
        .blendEnable = blend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blendState {
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
        .pColorAttachmentFormats = &target,
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
        .pColorBlendState = &blendState,
        .pDynamicState = &dynamic,
        .layout = m_layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "Renderer: impossibile creare una pipeline");
        return VK_NULL_HANDLE;
    }
    m_pipelines[key] = pipeline;
    return pipeline;
}

} // namespace vela::render
