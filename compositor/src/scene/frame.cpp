#include "scene/frame.hpp"

#include "scene/surface.hpp"

#include <algorithm>
#include <cmath>

namespace vela::scene {

namespace {

bool sameBox(const wlr_box& a, const wlr_box& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool sameFbox(const wlr_fbox& a, const wlr_fbox& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool sameColor(const wlr_render_color& a, const wlr_render_color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int64_t regionArea(const pixman_region32_t* region)
{
    int count = 0;
    const pixman_box32_t* rects = pixman_region32_rectangles(region, &count);
    int64_t area = 0;
    for (int i = 0; i < count; ++i) {
        area += int64_t(rects[i].x2 - rects[i].x1) * (rects[i].y2 - rects[i].y1);
    }
    return area;
}

// Un rettangolo logico in pixel: si arrotondano i bordi, non posizione e
// dimensione (§3.2), così due rettangoli adiacenti restano adiacenti.
wlr_box toPixels(double x, double y, double width, double height, const BuildParams& p)
{
    const int x1 = int(std::lround((x - p.originX) * p.scale));
    const int y1 = int(std::lround((y - p.originY) * p.scale));
    const int x2 = int(std::lround((x + width - p.originX) * p.scale));
    const int y2 = int(std::lround((y + height - p.originY) * p.scale));
    return { x1, y1, x2 - x1, y2 - y1 };
}

bool onScreen(const wlr_box& box, const BuildParams& p)
{
    wlr_box clipped {};
    return box.width > 0 && box.height > 0 && wlr_box_intersection(&clipped, &box, &p.bounds);
}

// Copia 1:1 (nessun ricampionamento) quando un pixel del buffer cade
// esattamente su un pixel dello schermo (§3.3).
bool isOneToOne(const wlr_fbox& src, const wlr_box& box, wl_output_transform transform)
{
    const bool swapped = transform & WL_OUTPUT_TRANSFORM_90;
    const double width = swapped ? src.height : src.width;
    const double height = swapped ? src.width : src.height;
    return src.x == std::floor(src.x) && src.y == std::floor(src.y) && width == double(box.width)
        && height == double(box.height);
}

void addSurface(wlr_surface* surface, double lx, double ly, float opacity, const BuildParams& p,
    std::vector<Element>& out)
{
    wlr_texture* texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return; // nessun buffer: non c'è niente da mostrare
    }
    const double width = surface->current.width;
    const double height = surface->current.height;
    wlr_fbox src {};
    wlr_surface_get_buffer_source_box(surface, &src);
    const wl_output_transform transform = wlr_output_transform_invert(surface->current.transform);
    wlr_box box = toPixels(lx, ly, width, height, p);

    // Nitidezza (§3.3, §3.4): se l'app ha disegnato alla scala di questo
    // schermo (buffer grande quanto la sua area fisica, a meno
    // dell'arrotondamento), la superficie si aggancia a un pixel fisico e
    // occupa esattamente i pixel del buffer: copia 1:1, senza filtri, anche
    // se la posizione logica cade a metà di un pixel.
    const bool swapped = transform & WL_OUTPUT_TRANSFORM_90;
    const double bufferWidth = swapped ? src.height : src.width;
    const double bufferHeight = swapped ? src.width : src.height;
    const bool whole = src.x == std::floor(src.x) && src.y == std::floor(src.y)
        && bufferWidth == std::floor(bufferWidth) && bufferHeight == std::floor(bufferHeight);
    if (whole && std::abs(bufferWidth - width * p.scale) < 1.0 && std::abs(bufferHeight - height * p.scale) < 1.0) {
        box.width = int(bufferWidth);
        box.height = int(bufferHeight);
    }
    if (!onScreen(box, p)) {
        return;
    }
    Element e {};
    e.key = surface;
    e.surface = surface;
    e.texture = texture;
    e.src = src;
    e.transform = transform;
    e.box = box;
    e.opacity = opacity;
    e.linear = !isOneToOne(e.src, box, e.transform);
    e.originX = (lx - p.originX) * p.scale;
    e.originY = (ly - p.originY) * p.scale;
    e.scaleX = width > 0 ? box.width / width : p.scale;
    e.scaleY = height > 0 ? box.height / height : p.scale;
    out.push_back(e);
}

void visit(Node* node, double lx, double ly, float opacity, const BuildParams& p, std::vector<Element>& out,
    bool isRoot)
{
    const bool captured = isRoot && p.captureRoot;
    if (!node->enabled() && !captured) {
        return;
    }
    lx += node->x();
    ly += node->y();
    if (!captured) {
        opacity *= node->opacity();
    }
    if (opacity <= 0.0f) {
        return;
    }

    switch (node->type()) {
    case Node::Type::Tree:
        for (Node* child : static_cast<Tree*>(node)->children()) {
            visit(child, lx, ly, opacity, p, out, false);
        }
        break;
    case Node::Type::Surface: {
        wlr_surface* root = static_cast<SurfaceNode*>(node)->surface();
        if (!root->mapped) {
            break;
        }
        forEachSurface(root, [&](wlr_surface* surface, int sx, int sy) {
            addSurface(surface, lx + sx, ly + sy, opacity, p, out);
        });
        break;
    }
    case Node::Type::Rect: {
        auto* rect = static_cast<RectNode*>(node);
        const wlr_box box = toPixels(lx, ly, rect->width(), rect->height(), p);
        if (!onScreen(box, p) || rect->color().a <= 0.0f) {
            break;
        }
        Element e {};
        e.key = node;
        e.color = rect->color();
        e.color.r *= opacity;
        e.color.g *= opacity;
        e.color.b *= opacity;
        e.color.a *= opacity;
        e.box = box;
        e.opacity = 1.0f;
        out.push_back(e);
        break;
    }
    case Node::Type::Buffer: {
        auto* buffer = static_cast<BufferNode*>(node);
        const wlr_box box = toPixels(lx, ly, buffer->width(), buffer->height(), p);
        if (!onScreen(box, p) || !buffer->texture()) {
            break;
        }
        Element e {};
        e.key = node;
        e.texture = buffer->texture();
        e.src = buffer->src();
        e.transform = buffer->transform();
        e.box = box;
        e.opacity = opacity;
        e.linear = !isOneToOne(e.src, box, e.transform);
        out.push_back(e);
        break;
    }
    }
}

// La parte certamente opaca di un elemento, in pixel.
void opaqueRegion(const Element& e, pixman_region32_t* out)
{
    pixman_region32_clear(out);
    if (e.opacity < 1.0f) {
        return;
    }
    if (!e.texture) {
        if (e.color.a >= 1.0f) {
            pixman_region32_union_rect(out, out, e.box.x, e.box.y, uint32_t(e.box.width), uint32_t(e.box.height));
        }
        return;
    }
    if (!e.surface) {
        const render::Texture* texture = render::toTexture(e.texture);
        if (texture && !texture->format->alpha) {
            pixman_region32_union_rect(out, out, e.box.x, e.box.y, uint32_t(e.box.width), uint32_t(e.box.height));
        }
        return;
    }
    // Regione opaca dichiarata dall'app, in coordinate della superficie:
    // la si restringe verso l'interno, per non coprire mai troppo.
    int count = 0;
    const pixman_box32_t* rects = pixman_region32_rectangles(&e.surface->opaque_region, &count);
    for (int i = 0; i < count; ++i) {
        const int x1 = int(std::ceil(e.originX + rects[i].x1 * e.scaleX));
        const int y1 = int(std::ceil(e.originY + rects[i].y1 * e.scaleY));
        const int x2 = int(std::floor(e.originX + rects[i].x2 * e.scaleX));
        const int y2 = int(std::floor(e.originY + rects[i].y2 * e.scaleY));
        if (x2 > x1 && y2 > y1) {
            pixman_region32_union_rect(out, out, x1, y1, uint32_t(x2 - x1), uint32_t(y2 - y1));
        }
    }
    pixman_region32_intersect_rect(out, out, e.box.x, e.box.y, uint32_t(e.box.width), uint32_t(e.box.height));
}

} // namespace

bool Element::sameLook(const Element& o) const
{
    // Il contenuto delle superfici cambia con i commit, che portano il loro
    // danno preciso: qui conta solo dove e come si disegnano. Per i nodi
    // nostri anche la texture.
    const bool sameTexture = surface ? true : texture == o.texture;
    return surface == o.surface && sameTexture && sameBox(box, o.box) && sameFbox(src, o.src)
        && transform == o.transform && opacity == o.opacity && linear == o.linear && sameColor(color, o.color);
}

void buildElements(Node* root, const BuildParams& params, std::vector<Element>& out)
{
    double lx = 0.0;
    double ly = 0.0;
    if (root->parent()) {
        root->parent()->coords(lx, ly);
    }
    visit(root, lx, ly, 1.0f, params, out, true);
}

void cullOccluded(std::vector<Element>& elements)
{
    pixman_region32_t covered;
    pixman_region32_t visible;
    pixman_region32_t opaque;
    pixman_region32_init(&covered);
    pixman_region32_init(&visible);
    pixman_region32_init(&opaque);
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        Element& e = *it;
        pixman_region32_init_rect(&visible, e.box.x, e.box.y, uint32_t(e.box.width), uint32_t(e.box.height));
        pixman_region32_subtract(&visible, &visible, &covered);
        e.visible = pixman_region32_not_empty(&visible);
        e.visibleArea = e.visible ? regionArea(&visible) : 0;
        if (e.visible) {
            opaqueRegion(e, &opaque);
            pixman_region32_union(&covered, &covered, &opaque);
        }
        pixman_region32_fini(&visible);
    }
    pixman_region32_fini(&covered);
    pixman_region32_fini(&opaque);
}

void drawElements(render::Pass& pass, const std::vector<Element>& elements, const pixman_region32_t* clip,
    wl_output_transform outputTransform, int width, int height)
{
    const wl_output_transform toBuffer = wlr_output_transform_invert(outputTransform);
    for (const Element& e : elements) {
        if (!e.visible) {
            continue;
        }
        wlr_box dst {};
        wlr_box_transform(&dst, &e.box, toBuffer, width, height);
        if (!e.texture) {
            pass.addRect(dst, e.color, clip, true);
            continue;
        }
        render::Texture* texture = render::toTexture(e.texture);
        if (!texture) {
            continue;
        }
        render::Pass::TextureDraw draw {
            .texture = texture,
            .src = e.src,
            .dst = dst,
            .transform = wlr_output_transform_compose(e.transform, outputTransform),
            .alpha = e.opacity,
            .linear = e.linear,
            .blend = true,
            .clip = clip,
        };
        // Sincronizzazione esplicita: il buffer è pronto quando scatta il
        // punto di acquisizione dell'app.
        if (e.surface) {
            const auto* sync = wlr_linux_drm_syncobj_v1_get_surface_state(e.surface);
            if (sync && sync->acquire_timeline) {
                draw.waitTimeline = sync->acquire_timeline;
                draw.waitPoint = sync->acquire_point;
            }
        }
        pass.addTexture(draw);
    }
}

void addReleasePoints(const std::vector<Element>& elements, render::Renderer& renderer, uint64_t syncPoint)
{
    Scene* scene = Scene::instance();
    if (!renderer.syncTimeline() || syncPoint == 0 || !scene || !scene->eventLoop) {
        return;
    }
    for (const Element& e : elements) {
        if (!e.visible || !e.surface) {
            continue;
        }
        if (auto* sync = wlr_linux_drm_syncobj_v1_get_surface_state(e.surface)) {
            wlr_linux_drm_syncobj_v1_state_add_release_point(sync, renderer.syncTimeline(), syncPoint,
                scene->eventLoop);
        }
    }
}

// ----------------------------------------------------------- OutputFrame --

OutputFrame::OutputFrame(Scene& scene, render::Renderer& renderer, wlr_output* output)
    : m_scene(scene)
    , m_renderer(renderer)
    , m_output(output)
{
    wlr_damage_ring_init(&m_ring);
    scene.frames.push_back(this);

    // Il cursore disegnato da noi (quando non c'è quello hardware) e chi
    // chiede un frame (cursore hardware, catture) passano da qui.
    m_damage.notify = [](wl_listener* listener, void* data) {
        OutputFrame* self = wl_container_of(listener, self, m_damage);
        self->damage(static_cast<wlr_output_event_damage*>(data)->damage);
    };
    wl_signal_add(&output->events.damage, &m_damage);
    m_needsFrame.notify = [](wl_listener* listener, void*) {
        OutputFrame* self = wl_container_of(listener, self, m_needsFrame);
        if (self->scheduleFrame) {
            self->scheduleFrame();
        }
    };
    wl_signal_add(&output->events.needs_frame, &m_needsFrame);

    // Scanout di buffer con sincronizzazione esplicita: serve un backend
    // che sappia aspettare e far scattare timeline (DRM, annidato in un
    // ospite con linux-drm-syncobj). Quello headless dice di saperlo fare,
    // ma poi rifiuta i commit con le timeline.
    if (output->backend->features.timeline && renderer.syncTimeline() && !wlr_output_is_headless(output)) {
        m_scanoutTimeline = wlr_drm_syncobj_timeline_create(renderer.vk().renderFd);
    }
}

OutputFrame::~OutputFrame()
{
    wl_list_remove(&m_damage.link);
    wl_list_remove(&m_needsFrame.link);
    std::erase(m_scene.frames, this);
    for (const auto& [key, element] : m_last) {
        if (element.surface) {
            forgetSurface(element.surface, m_output);
        }
    }
    if (m_feedbackSurface) {
        sendFeedback(m_feedbackSurface, false);
    }
    if (m_scanoutTimeline) {
        wlr_drm_syncobj_timeline_unref(m_scanoutTimeline);
    }
    wlr_damage_ring_finish(&m_ring);
}

void OutputFrame::damage(const pixman_region32_t* region)
{
    wlr_damage_ring_add(&m_ring, region);
    if (scheduleFrame) {
        scheduleFrame();
    }
}

void OutputFrame::damageWhole()
{
    int width = 0;
    int height = 0;
    wlr_output_transformed_resolution(m_output, &width, &height);
    const wlr_box whole { 0, 0, width, height };
    wlr_damage_ring_add_box(&m_ring, &whole);
    if (scheduleFrame) {
        scheduleFrame();
    }
}

void OutputFrame::surfaceCommitted(wlr_surface* surface)
{
    auto it = m_last.find(surface);
    if (it == m_last.end()) {
        return;
    }
    const Element& e = it->second;
    pixman_region32_t region;
    pixman_region32_init(&region);
    wlr_surface_get_effective_damage(surface, &region);
    if (pixman_region32_not_empty(&region)) {
        const bool exact = e.scaleX == std::floor(e.scaleX) && e.scaleY == std::floor(e.scaleY)
            && e.originX == std::floor(e.originX) && e.originY == std::floor(e.originY);
        wlr_region_scale_xy(&region, &region, float(e.scaleX), float(e.scaleY));
        pixman_region32_translate(&region, int(std::floor(e.originX)), int(std::floor(e.originY)));
        if (!exact) {
            wlr_region_expand(&region, &region, 1); // il filtro tocca anche i vicini
        }
        pixman_region32_intersect_rect(&region, &region, e.box.x, e.box.y, uint32_t(e.box.width),
            uint32_t(e.box.height));
        wlr_damage_ring_add(&m_ring, &region);
    }
    pixman_region32_fini(&region);
    // Anche senza danno: l'app aspetta il suo frame callback.
    if (scheduleFrame) {
        scheduleFrame();
    }
}

void OutputFrame::surfaceDestroyed(wlr_surface* surface)
{
    if (surface == m_feedbackSurface) {
        m_feedbackSurface = nullptr;
        m_feedbackDebounce = 0;
    }
    auto it = m_last.find(surface);
    if (it == m_last.end()) {
        return;
    }
    wlr_damage_ring_add_box(&m_ring, &it->second.box);
    m_last.erase(it);
    std::erase(m_visibleSurfaces, surface);
    if (scheduleFrame) {
        scheduleFrame();
    }
}

void OutputFrame::updateSurfaces(const std::vector<Element>& elements)
{
    m_visibleSurfaces.clear();
    for (const Element& e : elements) {
        if (!e.surface) {
            continue;
        }
        wlr_box onOutput {};
        const wlr_box bounds { 0, 0, m_width, m_height };
        wlr_box_intersection(&onOutput, &e.box, &bounds);
        reportSurface(e.surface, m_output, int64_t(onOutput.width) * onOutput.height, e.visibleArea);
        if (e.visible) {
            m_visibleSurfaces.push_back(e.surface);
        }
    }
}

void OutputFrame::resetDamage()
{
    // Tutti i buffer ripartono da capo: il prossimo frame li ridisegna interi.
    wlr_damage_ring_finish(&m_ring);
    wlr_damage_ring_init(&m_ring);
    m_width = 0;
    m_height = 0;
    if (scheduleFrame) {
        scheduleFrame();
    }
}

bool OutputFrame::render(double lx, double ly, wlr_output_state* pending)
{
    if (!m_output->enabled) {
        return false;
    }
    // Dimensione e scala del frame: quelle nuove, se il commit cambia modo.
    int width = m_output->width;
    int height = m_output->height;
    float scale = m_output->scale;
    wl_output_transform transform = m_output->transform;
    if (pending && (pending->committed & WLR_OUTPUT_STATE_MODE)) {
        if (pending->mode_type == WLR_OUTPUT_STATE_MODE_FIXED && pending->mode) {
            width = pending->mode->width;
            height = pending->mode->height;
        } else {
            width = pending->custom_mode.width;
            height = pending->custom_mode.height;
        }
    }
    if (pending && (pending->committed & WLR_OUTPUT_STATE_SCALE)) {
        scale = pending->scale;
    }
    if (pending && (pending->committed & WLR_OUTPUT_STATE_TRANSFORM)) {
        transform = pending->transform;
    }
    if (transform & WL_OUTPUT_TRANSFORM_90) {
        std::swap(width, height);
    }
    if (width != m_width || height != m_height || scale != m_scale) {
        wlr_damage_ring_finish(&m_ring);
        wlr_damage_ring_init(&m_ring);
        m_width = width;
        m_height = height;
        m_scale = scale;
        const wlr_box whole { 0, 0, width, height };
        wlr_damage_ring_add_box(&m_ring, &whole);
    }

    // 1. La scena appiattita, in pixel di questo schermo.
    std::vector<Element> elements;
    const BuildParams params {
        .originX = lx,
        .originY = ly,
        .scale = scale,
        .bounds = { 0, 0, width, height },
    };
    buildElements(&m_scene.root(), params, elements);
    cullOccluded(elements);

    // 2. Il danno: ciò che è apparso, sparito, cambiato o spostato rispetto
    //    al frame precedente (il contenuto delle superfici arriva dai commit).
    pixman_region32_t changed;
    pixman_region32_init(&changed);
    auto addBox = [&](const wlr_box& box) {
        pixman_region32_union_rect(&changed, &changed, box.x, box.y, uint32_t(std::max(box.width, 0)),
            uint32_t(std::max(box.height, 0)));
    };
    std::unordered_map<const void*, Element> current;
    current.reserve(elements.size());
    size_t highestOldOrder = 0;
    for (size_t i = 0; i < elements.size(); ++i) {
        Element& e = elements[i];
        e.order = i;
        auto it = m_last.find(e.key);
        if (it == m_last.end()) {
            addBox(e.box);
        } else {
            const Element& old = it->second;
            if (!e.sameLook(old)) {
                addBox(old.box);
                addBox(e.box);
            } else if (old.order < highestOldOrder) {
                addBox(e.box); // è salito sopra altri elementi
            }
            highestOldOrder = std::max(highestOldOrder, old.order);
        }
        current.emplace(e.key, e);
    }
    for (const auto& [key, old] : m_last) {
        if (!current.contains(key)) {
            addBox(old.box);
            if (old.surface) {
                forgetSurface(old.surface, m_output);
            }
        }
    }
    wlr_damage_ring_add(&m_ring, &changed);
    pixman_region32_fini(&changed);
    // Diagnosi: VELA_DEBUG_DAMAGE=1 ridisegna tutto a ogni frame.
    static const bool fullDamage = std::getenv("VELA_DEBUG_DAMAGE") && *std::getenv("VELA_DEBUG_DAMAGE") == '1';
    if (fullDamage && pixman_region32_not_empty(&m_ring.current)) {
        wlr_damage_ring_finish(&m_ring);
        wlr_damage_ring_init(&m_ring);
        const wlr_box whole { 0, 0, width, height };
        wlr_damage_ring_add_box(&m_ring, &whole);
    }
    m_last = std::move(current);
    updateSurfaces(elements);

    // Un'app a schermo intero che si può mostrare così com'è.
    const Element* candidate = pending ? nullptr : scanoutCandidate(elements, transform);
    updateFeedback(candidate);

    const bool damaged = pixman_region32_not_empty(&m_ring.current);
    if (!damaged && !m_output->needs_frame && !pending) {
        return false;
    }
    m_delivered = {};

    // Lo stato del commit: quello del chiamante (cambio di modo, disegnato
    // subito alla nuova dimensione) o uno nostro.
    wlr_output_state own;
    wlr_output_state_init(&own);
    wlr_output_state& state = pending ? *pending : own;
    // Anche quando c'è solo da spostare il cursore hardware si consegna un
    // buffer (con danno vuoto il disegno non costa quasi nulla). Con DRM un
    // commit senza buffer è bloccante: fermerebbe il compositor fino al
    // vblank di questo schermo, e intanto gli altri schermi perderebbero i
    // loro (con un 75 Hz accanto, il 180 Hz scendeva sotto i 60 fps).

    // Scanout diretto: niente disegno, il buffer dell'app va sullo schermo.
    if (candidate && tryScanout(*candidate, state)) {
        wlr_output_state_finish(&own);
        // Lo schermo è a posto: il danno accumulato non serve più (i
        // nostri buffer invece sono rimasti indietro, vedi sotto).
        pixman_region32_clear(&m_ring.current);
        if (!m_scanout) {
            wlr_log(WLR_DEBUG, "%s: scanout diretto attivo", m_output->name);
        }
        m_scanout = true;
        m_delivered.scanout = true;
        return true;
    }
    if (m_scanout) {
        // Si torna a comporre: i nostri buffer non hanno visto i frame
        // dello scanout, si ridisegnano interi.
        wlr_log(WLR_DEBUG, "%s: scanout diretto finito", m_output->name);
        m_scanout = false;
        const wlr_box whole { 0, 0, width, height };
        wlr_damage_ring_add_box(&m_ring, &whole);
    }

    if (!wlr_output_configure_primary_swapchain(m_output, &state, &m_output->swapchain)) {
        wlr_output_state_finish(&own);
        return false;
    }
    wlr_buffer* buffer = wlr_swapchain_acquire(m_output->swapchain);
    if (!buffer) {
        wlr_output_state_finish(&own);
        return false;
    }

    // Il danno di questo frame (per il backend) e quello del buffer che
    // stiamo per riusare (per l'età del buffer: ciò che gli manca rispetto
    // a ora).
    pixman_region32_t frameDamage;
    pixman_region32_t bufferDamage;
    pixman_region32_init(&frameDamage);
    pixman_region32_init(&bufferDamage);
    pixman_region32_copy(&frameDamage, &m_ring.current);
    wlr_damage_ring_rotate_buffer(&m_ring, buffer, &bufferDamage);

    const wl_output_transform toBuffer = wlr_output_transform_invert(transform);
    wlr_region_transform(&bufferDamage, &bufferDamage, toBuffer, width, height);
    wlr_region_transform(&frameDamage, &frameDamage, toBuffer, width, height);

    bool ok = false;
    if (std::unique_ptr<render::Pass> pass = m_renderer.beginPass(buffer)) {
        // Sotto tutto, il nero (lo sfondo del desktop di solito lo copre).
        pass->addRect({ 0, 0, buffer->width, buffer->height }, { 0.0f, 0.0f, 0.0f, 1.0f }, &bufferDamage, false);
        drawElements(*pass, elements, &bufferDamage, transform, width, height);
        wlr_output_add_software_cursors_to_render_pass(m_output, pass->wlr(), &bufferDamage);
        pass->measure();
        ok = pass->submit();
        if (ok) {
            m_delivered.point = pass->point();
            m_delivered.timingSlot = pass->timingSlot();
            addReleasePoints(elements, m_renderer, pass->syncPoint());
        }
    }

    const int bufferWidth = buffer->width;
    const int bufferHeight = buffer->height;
    if (ok) {
        wlr_output_state_set_buffer(&state, buffer);
        wlr_output_state_set_damage(&state, &frameDamage);
        for (wlr_surface* surface : m_visibleSurfaces) {
            const SurfaceState* surfaceInfo = surfaceState(surface);
            if (!surfaceInfo || surfaceInfo->pacing == m_output) {
                wlr_presentation_surface_textured_on_output(surface, m_output);
            }
        }
        ok = wlr_output_commit_state(m_output, &state);
    }
    wlr_buffer_unlock(buffer);
    wlr_output_state_finish(&own);
    if (!ok) {
        // Il frame non è arrivato allo schermo: il suo danno va ridisegnato.
        wlr_region_transform(&frameDamage, &frameDamage, transform, bufferWidth, bufferHeight);
        wlr_damage_ring_add(&m_ring, &frameDamage);
    }
    pixman_region32_fini(&frameDamage);
    pixman_region32_fini(&bufferDamage);
    return ok;
}

const Element* OutputFrame::scanoutCandidate(const std::vector<Element>& elements,
    wl_output_transform transform) const
{
    // VELA_SCANOUT=0: sempre composizione (diagnosi, confronti).
    static const bool enabled = !(std::getenv("VELA_SCANOUT") && std::strcmp(std::getenv("VELA_SCANOUT"), "0") == 0);
    if (!enabled) {
        return nullptr;
    }
    const Element* found = nullptr;
    for (const Element& e : elements) {
        if (!e.visible) {
            continue;
        }
        if (found) {
            return nullptr; // si vede anche altro
        }
        found = &e;
    }
    if (!found || !found->surface || !found->texture) {
        return nullptr;
    }
    const Element& e = *found;
    const wlr_box whole { 0, 0, m_width, m_height };
    if (e.opacity < 1.0f || e.linear || !sameBox(e.box, whole) || e.transform != transform) {
        return nullptr;
    }
    // Opaca: formato senza alfa, o regione opaca dichiarata su tutto.
    const render::Texture* texture = render::toTexture(e.texture);
    bool opaque = texture && !texture->format->alpha;
    if (!opaque) {
        pixman_box32_t all { 0, 0, e.surface->current.width, e.surface->current.height };
        opaque = pixman_region32_contains_rectangle(&e.surface->opaque_region, &all) == PIXMAN_REGION_IN;
    }
    return opaque ? found : nullptr;
}

bool OutputFrame::tryScanout(const Element& e, wlr_output_state& state)
{
    if (!wlr_output_is_direct_scanout_allowed(m_output)) {
        return false; // cursore disegnato da noi, cattura in corso
    }
    wlr_surface* surface = e.surface;
    wlr_client_buffer* client = surface->buffer;
    if (!client) {
        return false;
    }
    wlr_buffer* buffer = &client->base;
    if (client->source && client->source->n_locks > 0) {
        buffer = client->source;
    }
    wlr_dmabuf_attributes dmabuf {};
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        return false; // il piano dello schermo legge solo dmabuf
    }
    if (e.src.x != 0.0 || e.src.y != 0.0 || e.src.width != double(buffer->width)
        || e.src.height != double(buffer->height)) {
        return false; // l'app ne mostra solo una parte
    }

    wlr_output_state attempt;
    wlr_output_state_init(&attempt);
    if (!wlr_output_state_copy(&attempt, &state)) {
        return false;
    }
    wlr_output_state_set_buffer(&attempt, buffer);
    auto* sync = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
    if (sync && sync->acquire_timeline) {
        if (!m_scanoutTimeline) {
            wlr_output_state_finish(&attempt);
            return false;
        }
        wlr_output_state_set_wait_timeline(&attempt, sync->acquire_timeline, sync->acquire_point);
        wlr_output_state_set_signal_timeline(&attempt, m_scanoutTimeline, m_scanoutPoint + 1);
    }
    bool ok = wlr_output_test_state(m_output, &attempt);
    if (ok) {
        const SurfaceState* info = surfaceState(surface);
        if (!info || info->pacing == m_output) {
            wlr_presentation_surface_scanned_out_on_output(surface, m_output);
        }
        ok = wlr_output_commit_state(m_output, &attempt);
    }
    if (ok && sync && sync->acquire_timeline) {
        // Il backend fa scattare il punto quando smette di mostrare il buffer.
        ++m_scanoutPoint;
        wlr_linux_drm_syncobj_v1_state_add_release_point(sync, m_scanoutTimeline, m_scanoutPoint,
            m_output->event_loop);
    }
    wlr_output_state_finish(&attempt);
    return ok;
}

void OutputFrame::updateFeedback(const Element* candidate)
{
    // Come wlr_scene: un'app diventa (o smette di essere) candidata allo
    // scanout per un po' di frame di fila prima di cambiarle i formati
    // consigliati, perché ricreare i buffer costa.
    constexpr int debounceFrames = 30;
    if (!m_scene.linuxDmabuf) {
        return;
    }
    wlr_surface* target = candidate ? candidate->surface : nullptr;
    if (target && target == m_feedbackSurface) {
        m_feedbackDebounce = debounceFrames;
        return;
    }
    if (target) {
        if (++m_feedbackDebounce < debounceFrames) {
            return;
        }
        if (m_feedbackSurface) {
            sendFeedback(m_feedbackSurface, false);
        }
        sendFeedback(target, true);
        m_feedbackSurface = target;
        m_feedbackDebounce = debounceFrames;
        return;
    }
    if (!m_feedbackSurface) {
        m_feedbackDebounce = 0;
        return;
    }
    if (--m_feedbackDebounce > 0) {
        return;
    }
    sendFeedback(m_feedbackSurface, false);
    m_feedbackSurface = nullptr;
    m_feedbackDebounce = 0;
}

void OutputFrame::sendFeedback(wlr_surface* surface, bool scanout)
{
    SurfaceState* info = surfaceState(surface);
    if (!scanout) {
        wlr_linux_dmabuf_v1_set_surface_feedback(m_scene.linuxDmabuf, surface, nullptr);
        if (info) {
            info->scanoutFeedback = nullptr;
        }
        wlr_log(WLR_DEBUG, "%s: feedback dmabuf predefinito a una superficie", m_output->name);
        return;
    }
    const wlr_linux_dmabuf_feedback_v1_init_options options {
        .main_renderer = m_renderer.wlr(),
        .scanout_primary_output = m_output,
        .output_layer_feedback_event = nullptr,
    };
    wlr_linux_dmabuf_feedback_v1 feedback {};
    if (!wlr_linux_dmabuf_feedback_v1_init_with_options(&feedback, &options)) {
        return; // lo schermo non dice quali formati legge il piano primario
    }
    wlr_linux_dmabuf_v1_set_surface_feedback(m_scene.linuxDmabuf, surface, &feedback);
    wlr_linux_dmabuf_feedback_v1_finish(&feedback);
    if (info) {
        info->scanoutFeedback = m_output;
    }
    wlr_log(WLR_DEBUG, "%s: feedback dmabuf con la tranche di scanout a una superficie", m_output->name);
}

void OutputFrame::sendFrameDone(const timespec& when)
{
    for (wlr_surface* surface : m_visibleSurfaces) {
        const SurfaceState* state = surfaceState(surface);
        if (!state || state->pacing == m_output) {
            wlr_surface_send_frame_done(surface, &when);
        }
    }
}

} // namespace vela::scene
