#include "scene/capture.hpp"

#include "render/pass.hpp"
#include "scene/frame.hpp"

namespace vela::scene {

namespace {

// L'evento "frame" porta con sé il buffer appena disegnato.
struct FrameEvent {
    wlr_ext_image_capture_source_v1_frame_event base;
    wlr_buffer* buffer;
    render::Renderer* renderer;
    timespec when;
};

void requestFrame(wlr_ext_image_capture_source_v1* source, bool)
{
    WindowCapture::from(source)->renderFrame();
}

void copyFrame(wlr_ext_image_capture_source_v1*, wlr_ext_image_copy_capture_frame_v1* frame,
    wlr_ext_image_capture_source_v1_frame_event* baseEvent)
{
    auto* event = reinterpret_cast<FrameEvent*>(baseEvent);
    if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, event->buffer, event->renderer->wlr())) {
        wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &event->when);
    }
}

const wlr_ext_image_capture_source_v1_interface captureImpl {
    .start = nullptr,
    .stop = nullptr,
    .request_frame = requestFrame,
    .copy_frame = copyFrame,
    .get_pointer_cursor = nullptr,
};

} // namespace

WindowCapture::WindowCapture(Node* source, std::function<wlr_box()> frameBox, render::Renderer& renderer,
    wlr_allocator* allocator)
    : m_node(source)
    , m_frameBox(std::move(frameBox))
    , m_renderer(renderer)
    , m_allocator(allocator)
{
    wlr_ext_image_capture_source_v1_init(&m_shim.base, &captureImpl);
    m_shim.self = this;
}

WindowCapture::~WindowCapture()
{
    wlr_ext_image_capture_source_v1_finish(&m_shim.base);
    wlr_swapchain_destroy(m_swapchain);
}

wlr_ext_image_capture_source_v1* WindowCapture::source()
{
    updateConstraints();
    return &m_shim.base;
}

// Un buffer grande quanto la finestra, in un formato che sappiamo sia
// disegnare sia rileggere (per le catture in memoria condivisa).
bool WindowCapture::updateConstraints()
{
    const wlr_box box = m_frameBox();
    if (box.width <= 0 || box.height <= 0) {
        return false;
    }
    if (m_swapchain && m_swapchain->width == box.width && m_swapchain->height == box.height) {
        return true;
    }
    const render::VulkanDevice& vk = m_renderer.vk();
    const wlr_drm_format* renderable = wlr_drm_format_set_get(&vk.renderFormats, DRM_FORMAT_ARGB8888);
    if (!renderable) {
        return false;
    }
    wlr_drm_format_set both {};
    for (size_t i = 0; i < renderable->len; ++i) {
        if (wlr_drm_format_set_has(&vk.textureFormats, DRM_FORMAT_ARGB8888, renderable->modifiers[i])) {
            wlr_drm_format_set_add(&both, DRM_FORMAT_ARGB8888, renderable->modifiers[i]);
        }
    }
    wlr_swapchain_destroy(m_swapchain);
    m_swapchain = nullptr;
    if (const wlr_drm_format* format = wlr_drm_format_set_get(&both, DRM_FORMAT_ARGB8888)) {
        m_swapchain = wlr_swapchain_create(m_allocator, box.width, box.height, format);
    }
    wlr_drm_format_set_finish(&both);
    if (!m_swapchain) {
        return false;
    }
    wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(&m_shim.base, m_swapchain, m_renderer.wlr());
    wl_signal_emit_mutable(&m_shim.base.events.constraints_update, nullptr);
    return true;
}

void WindowCapture::renderFrame()
{
    if (!updateConstraints()) {
        return;
    }
    const wlr_box box = m_frameBox();
    wlr_buffer* buffer = wlr_swapchain_acquire(m_swapchain);
    if (!buffer) {
        return;
    }
    bool ok = false;
    if (std::unique_ptr<render::Pass> pass = m_renderer.beginPass(buffer)) {
        const wlr_box whole { 0, 0, buffer->width, buffer->height };
        pass->addRect(whole, { 0.0f, 0.0f, 0.0f, 0.0f }, nullptr, false);
        std::vector<Element> elements;
        const BuildParams params {
            .originX = double(box.x),
            .originY = double(box.y),
            .scale = 1.0,
            .bounds = whole,
            .captureRoot = true,
        };
        buildElements(m_node, params, elements);
        for (Element& e : elements) {
            e.visible = true;
        }
        drawElements(*pass, elements, nullptr, WL_OUTPUT_TRANSFORM_NORMAL, whole.width, whole.height);
        ok = pass->submit();
        if (ok) {
            addReleasePoints(elements, m_renderer, pass->syncPoint());
        }
    }
    if (ok) {
        pixman_region32_t damage;
        pixman_region32_init_rect(&damage, 0, 0, uint32_t(buffer->width), uint32_t(buffer->height));
        FrameEvent event { { &damage }, buffer, &m_renderer, {} };
        clock_gettime(CLOCK_MONOTONIC, &event.when);
        wl_signal_emit_mutable(&m_shim.base.events.frame, &event.base);
        pixman_region32_fini(&damage);
    }
    wlr_buffer_unlock(buffer);
}

} // namespace vela::scene
