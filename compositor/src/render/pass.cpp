#include "render/pass.hpp"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cmath>

namespace vela::render {

namespace {

bool passSubmit(wlr_render_pass* wlr)
{
    Pass* pass = Pass::from(wlr);
    const bool ok = pass->submit();
    delete pass; // wlroots non lo userà più
    return ok;
}

void passAddTexture(wlr_render_pass* wlr, const wlr_render_texture_options* options)
{
    Texture* texture = toTexture(options->texture);
    if (!texture) {
        return;
    }
    Pass::TextureDraw draw { .texture = texture };
    wlr_render_texture_options_get_src_box(options, &draw.src);
    wlr_render_texture_options_get_dst_box(options, &draw.dst);
    draw.transform = options->transform;
    draw.alpha = wlr_render_texture_options_get_alpha(options);
    draw.linear = options->filter_mode == WLR_SCALE_FILTER_BILINEAR;
    draw.blend = options->blend_mode == WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
    draw.clip = options->clip;
    draw.waitTimeline = options->wait_timeline;
    draw.waitPoint = options->wait_point;
    Pass::from(wlr)->addTexture(draw);
}

void passAddRect(wlr_render_pass* wlr, const wlr_render_rect_options* options)
{
    Pass* pass = Pass::from(wlr);
    wlr_box box {};
    const wlr_box whole { 0, 0, pass->width(), pass->height() };
    box = wlr_box_empty(&options->box) ? whole : options->box;
    pass->addRect(box, options->color, options->clip, options->blend_mode == WLR_RENDER_BLEND_MODE_PREMULTIPLIED);
}

const wlr_render_pass_impl passImpl {
    .submit = passSubmit,
    .add_texture = passAddTexture,
    .add_rect = passAddRect,
};

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Le matrici di wl_output_transform (come in wlroots), righe [a b; d e]:
// la texture ruotata/specchiata nel quadrato unitario.
struct Rotation {
    float a, b, d, e;
};
Rotation rotation(wl_output_transform transform)
{
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: return { 1, 0, 0, 1 };
    case WL_OUTPUT_TRANSFORM_90: return { 0, 1, -1, 0 };
    case WL_OUTPUT_TRANSFORM_180: return { -1, 0, 0, -1 };
    case WL_OUTPUT_TRANSFORM_270: return { 0, -1, 1, 0 };
    case WL_OUTPUT_TRANSFORM_FLIPPED: return { -1, 0, 0, 1 };
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: return { 0, 1, 1, 0 };
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return { 1, 0, 0, -1 };
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return { 0, -1, -1, 0 };
    }
    return { 1, 0, 0, 1 };
}

} // namespace

Pass::Pass(Renderer& renderer, RenderTarget* target, wlr_buffer* buffer)
    : m_renderer(renderer)
    , m_target(target)
    , m_buffer(wlr_buffer_lock(buffer))
{
    wlr_render_pass_init(&m_shim.base, &passImpl);
    m_shim.self = this;
}

Pass::~Pass()
{
    for (Wait& wait : m_waits) {
        wlr_drm_syncobj_timeline_unref(wait.timeline);
    }
    if (m_signalTimeline) {
        if (!m_submitted) {
            // Chi aspetta questo punto non deve restare bloccato.
            wlr_drm_syncobj_timeline_signal(m_signalTimeline, m_signalPoint);
        }
        wlr_drm_syncobj_timeline_unref(m_signalTimeline);
    }
    if (m_timingSlot >= 0 && !m_submitted) {
        m_renderer.timingSubmitted(m_timingSlot, 0);
    }
    wlr_buffer_unlock(m_buffer);
}

void Pass::measure()
{
    if (m_timingSlot < 0 && !m_submitted) {
        m_timingSlot = m_renderer.timingSlot();
    }
}

void Pass::signalOnDone(wlr_drm_syncobj_timeline* timeline, uint64_t point)
{
    if (m_signalTimeline) {
        wlr_drm_syncobj_timeline_unref(m_signalTimeline);
    }
    m_signalTimeline = wlr_drm_syncobj_timeline_ref(timeline);
    m_signalPoint = point;
}

Pass* Pass::from(wlr_render_pass* pass)
{
    return reinterpret_cast<Shim*>(pass)->self;
}

// Un disegno per ogni rettangolo del ritaglio, con lo scissor.
void Pass::addDraw(const Draw& draw, const wlr_box& dst, const pixman_region32_t* clip)
{
    pixman_region32_t region;
    pixman_region32_init_rect(&region, dst.x, dst.y, uint32_t(std::max(dst.width, 0)),
        uint32_t(std::max(dst.height, 0)));
    pixman_region32_intersect_rect(&region, &region, 0, 0, m_target->width, m_target->height);
    if (clip) {
        pixman_region32_intersect(&region, &region, clip);
    }
    int count = 0;
    const pixman_box32_t* rects = pixman_region32_rectangles(&region, &count);
    for (int i = 0; i < count; ++i) {
        Draw piece = draw;
        piece.scissor = {
            { rects[i].x1, rects[i].y1 },
            { uint32_t(rects[i].x2 - rects[i].x1), uint32_t(rects[i].y2 - rects[i].y1) },
        };
        m_draws.push_back(piece);
    }
    pixman_region32_fini(&region);
}

void Pass::addTexture(const TextureDraw& in)
{
    Texture* texture = in.texture;
    if (!texture || !texture->view || in.dst.width <= 0 || in.dst.height <= 0 || in.alpha <= 0.0f) {
        return;
    }
    if (in.waitTimeline) {
        const bool known = std::any_of(m_waits.begin(), m_waits.end(), [&](const Wait& wait) {
            return wait.texture == texture && wait.timeline == in.waitTimeline && wait.point == in.waitPoint;
        });
        if (!known) {
            m_waits.push_back({ texture, wlr_drm_syncobj_timeline_ref(in.waitTimeline), in.waitPoint });
        }
    }
    const float texWidth = float(texture->base.width);
    const float texHeight = float(texture->base.height);
    wlr_fbox src = in.src;
    if (wlr_fbox_empty(&src)) {
        src = { 0, 0, texWidth, texHeight };
    }

    Draw draw {};
    draw.kind = Renderer::PipelineKind::Texture;
    draw.texture = texture;
    draw.linear = in.linear;
    // Un ingrandimento vero (non una copia 1:1 né una riduzione): filtro
    // bicubico invece del bilineare, che ammorbidisce.
    const bool swapped = in.transform & WL_OUTPUT_TRANSFORM_90;
    const double shownWidth = swapped ? in.dst.height : in.dst.width;
    const double shownHeight = swapped ? in.dst.width : in.dst.height;
    if (in.linear && shownWidth > src.width * 1.01 && shownHeight > src.height * 1.01) {
        draw.push.flags |= 1u;
    }
    // Un quad opaco non ha bisogno di fondersi con ciò che c'è sotto.
    draw.blend = in.blend && (texture->format->alpha || in.alpha < 1.0f);

    QuadPush& p = draw.push;
    p.dst[0] = float(in.dst.x);
    p.dst[1] = float(in.dst.y);
    p.dst[2] = float(in.dst.width);
    p.dst[3] = float(in.dst.height);
    p.target[0] = float(m_target->width);
    p.target[1] = float(m_target->height);
    p.alpha = in.alpha;

    // Dall'angolo del quad (u, v in [0,1]) alle coordinate della texture:
    // si annulla la trasformazione nel quadrato unitario (matrice
    // ortogonale: l'inversa è la trasposta), poi si va nella zona src.
    const Rotation r = rotation(in.transform);
    auto uvAt = [&](float u, float v, float out[2]) {
        const float px = 2.0f * u - 1.0f;
        const float py = 2.0f * v - 1.0f;
        const float qx = r.a * px + r.d * py;
        const float qy = r.b * px + r.e * py;
        const float s = (qx + 1.0f) / 2.0f;
        const float t = (qy + 1.0f) / 2.0f;
        out[0] = float((src.x + s * src.width) / texWidth);
        out[1] = float((src.y + t * src.height) / texHeight);
    };
    float origin[2], right[2], down[2];
    uvAt(0, 0, origin);
    uvAt(1, 0, right);
    uvAt(0, 1, down);
    p.uvOrigin[0] = origin[0];
    p.uvOrigin[1] = origin[1];
    p.uvX[0] = right[0] - origin[0];
    p.uvX[1] = right[1] - origin[1];
    p.uvY[0] = down[0] - origin[0];
    p.uvY[1] = down[1] - origin[1];

    addDraw(draw, in.dst, in.clip);
}

void Pass::addRect(const wlr_box& box, const wlr_render_color& color, const pixman_region32_t* clip, bool blend)
{
    if (box.width <= 0 || box.height <= 0) {
        return;
    }
    Draw draw {};
    draw.kind = Renderer::PipelineKind::Rect;
    draw.blend = blend && color.a < 1.0f;
    QuadPush& p = draw.push;
    p.dst[0] = float(box.x);
    p.dst[1] = float(box.y);
    p.dst[2] = float(box.width);
    p.dst[3] = float(box.height);
    p.target[0] = float(m_target->width);
    p.target[1] = float(m_target->height);
    p.alpha = 1.0f;
    // Da sRGB premoltiplicato a lineare premoltiplicato.
    const float a = std::clamp(color.a, 0.0f, 1.0f);
    const float channels[3] = { color.r, color.g, color.b };
    for (int i = 0; i < 3; ++i) {
        const float straight = a > 0.0f ? std::clamp(channels[i] / a, 0.0f, 1.0f) : 0.0f;
        p.color[i] = srgbToLinear(straight) * a;
    }
    p.color[3] = a;
    addDraw(draw, box, clip);
}

bool Pass::submit()
{
    if (m_submitted) {
        return false;
    }
    m_submitted = true;
    m_renderer.collect();
    VulkanDevice& vk = m_renderer.vk();

    // Le texture importate (dmabuf) da prendere in carico, una volta sola.
    std::vector<Texture*> foreign;
    for (const Draw& draw : m_draws) {
        if (draw.texture && draw.texture->foreign
            && std::find(foreign.begin(), foreign.end(), draw.texture) == foreign.end()) {
            foreign.push_back(draw.texture);
        }
    }

    VkCommandBuffer cmd = m_renderer.beginCommands();
    if (!cmd) {
        return false;
    }
    m_renderer.writeTimestamp(cmd, m_timingSlot, false);

    std::vector<VkImageMemoryBarrier2> acquire;
    std::vector<VkImageMemoryBarrier2> release;
    acquire.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        // Il contenuto si conserva: si ridisegna solo ciò che è cambiato.
        .oldLayout = m_target->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
        .dstQueueFamilyIndex = vk.queueFamily,
        .image = m_target->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    });
    release.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = vk.queueFamily,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
        .image = m_target->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    });
    for (Texture* texture : foreign) {
        acquire.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .dstQueueFamilyIndex = vk.queueFamily,
            .image = texture->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        });
        release.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = vk.queueFamily,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
            .image = texture->image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        });
    }
    const VkDependencyInfo acquireDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = uint32_t(acquire.size()),
        .pImageMemoryBarriers = acquire.data(),
    };
    vkCmdPipelineBarrier2(cmd, &acquireDep);

    const VkRenderingAttachmentInfo attachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_target->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo rendering {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { { 0, 0 }, { m_target->width, m_target->height } },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    };
    vkCmdBeginRendering(cmd, &rendering);
    const VkViewport viewport { 0.0f, 0.0f, float(m_target->width), float(m_target->height), 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkPipeline bound = VK_NULL_HANDLE;
    const VkPipelineLayout layout = m_renderer.pipelineLayout();
    for (const Draw& draw : m_draws) {
        VkPipeline pipeline = m_renderer.pipeline(m_target->format, draw.kind, draw.blend);
        if (!pipeline) {
            continue;
        }
        if (pipeline != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bound = pipeline;
        }
        if (draw.texture) {
            const VkDescriptorImageInfo image {
                .sampler = m_renderer.sampler(draw.linear),
                .imageView = draw.texture->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            const VkWriteDescriptorSet write {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image,
            };
            vkCmdPushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &write);
        }
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            sizeof(QuadPush), &draw.push);
        vkCmdSetScissor(cmd, 0, 1, &draw.scissor);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);

    const VkDependencyInfo releaseDep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = uint32_t(release.size()),
        .pImageMemoryBarriers = release.data(),
    };
    vkCmdPipelineBarrier2(cmd, &releaseDep);
    m_renderer.writeTimestamp(cmd, m_timingSlot, true);

    // Sincronizzazione esplicita: le app con linux-drm-syncobj-v1 dicono
    // loro quale punto aspettare prima di leggere il buffer.
    std::vector<int> waits;
    auto explicitSync = [&](const Texture* texture) {
        return std::any_of(
            m_waits.begin(), m_waits.end(), [texture](const Wait& wait) { return wait.texture == texture; });
    };
    for (const Wait& wait : m_waits) {
        const int fd = wlr_drm_syncobj_timeline_export_sync_file(wait.timeline, wait.point);
        if (fd >= 0) {
            waits.push_back(fd);
        } else {
            wlr_log(WLR_ERROR, "Pass: impossibile aspettare il punto %" PRIu64 " di un'app", wait.point);
        }
    }

    // Sincronizzazione implicita: si aspetta chi usa ancora la destinazione
    // (lo schermo che la mostra) e chi sta ancora scrivendo le texture (le
    // app); a fine lavoro la nostra fence finisce negli stessi dmabuf.
    auto exportFence = [&](int dmabufFd, uint32_t flags) {
        dma_buf_export_sync_file req { .flags = flags, .fd = -1 };
        if (ioctl(dmabufFd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &req) == 0 && req.fd >= 0) {
            waits.push_back(req.fd);
        }
    };
    if (vk.syncFile) {
        exportFence(m_target->dmabufFd, DMA_BUF_SYNC_WRITE);
        for (Texture* texture : foreign) {
            if (!explicitSync(texture)) {
                exportFence(texture->dmabufFd, DMA_BUF_SYNC_READ);
            }
        }
    }

    int releaseFd = -1;
    const uint64_t point = m_renderer.submit(cmd, waits, &releaseFd);
    m_renderer.timingSubmitted(m_timingSlot, point);
    if (point == 0) {
        return false;
    }
    m_point = point;
    if (releaseFd >= 0) {
        bool ok = true;
        auto importFence = [&](int dmabufFd, uint32_t flags) {
            dma_buf_import_sync_file req { .flags = flags, .fd = releaseFd };
            ok = ioctl(dmabufFd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &req) == 0 && ok;
        };
        importFence(m_target->dmabufFd, DMA_BUF_SYNC_WRITE);
        for (Texture* texture : foreign) {
            if (!explicitSync(texture)) {
                importFence(texture->dmabufFd, DMA_BUF_SYNC_READ);
            }
        }
        if (!ok) {
            m_renderer.waitFor(point); // il kernel non ha preso la fence
        }
    }
    // Fine lavoro anche sulle timeline syncobj: la nostra (rilascio dei
    // buffer delle app) e quella chiesta da wlroots. releaseFd -1: la CPU ha
    // già aspettato, i punti scattano subito.
    m_syncPoint = m_renderer.signalSyncPoint(releaseFd);
    if (m_signalTimeline) {
        const bool signalled = releaseFd >= 0
            ? wlr_drm_syncobj_timeline_import_sync_file(m_signalTimeline, m_signalPoint, releaseFd)
            : wlr_drm_syncobj_timeline_signal(m_signalTimeline, m_signalPoint);
        if (!signalled) {
            m_renderer.waitFor(point);
            wlr_drm_syncobj_timeline_signal(m_signalTimeline, m_signalPoint);
        }
    }
    if (releaseFd >= 0) {
        close(releaseFd);
    }
    m_target->initialized = true;
    m_target->lastUse = point;
    return true;
}

} // namespace vela::render
