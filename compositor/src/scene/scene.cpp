#include "scene/scene.hpp"

#include "scene/frame.hpp"
#include "scene/surface.hpp"

#include <algorithm>
#include <cmath>

namespace vela::scene {

Scene* Scene::s_instance = nullptr;

// ------------------------------------------------------------------ Node --

Node::Node(Type type, Tree* parent)
    : m_type(type)
{
    if (parent) {
        m_parent = parent;
        parent->m_children.push_back(this);
    }
    Scene::changed();
}

Node::~Node()
{
    detach();
    Scene::changed();
}

void Node::detach()
{
    if (m_parent) {
        std::erase(m_parent->m_children, this);
        m_parent = nullptr;
    }
}

void Node::setPosition(double x, double y)
{
    if (x == m_x && y == m_y) {
        return;
    }
    m_x = x;
    m_y = y;
    Scene::changed();
}

void Node::coords(double& lx, double& ly) const
{
    lx = 0.0;
    ly = 0.0;
    for (const Node* node = this; node; node = node->m_parent) {
        lx += node->m_x;
        ly += node->m_y;
    }
}

void Node::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    Scene::changed();
}

bool Node::visibleInTree() const
{
    const Node* node = this;
    for (; node->m_parent; node = node->m_parent) {
        if (!node->m_enabled) {
            return false;
        }
    }
    return node->m_enabled && Scene::instance() && node == &Scene::instance()->root();
}

void Node::setOpacity(float opacity)
{
    if (opacity == m_opacity) {
        return;
    }
    m_opacity = opacity;
    Scene::changed();
}

void Node::raiseToTop()
{
    if (!m_parent || m_parent->m_children.back() == this) {
        return;
    }
    Tree* parent = m_parent;
    std::erase(parent->m_children, this);
    parent->m_children.push_back(this);
    Scene::changed();
}

void Node::placeAbove(Node* sibling)
{
    if (sibling == this || !m_parent || sibling->m_parent != m_parent) {
        return;
    }
    auto& children = m_parent->m_children;
    std::erase(children, this);
    children.insert(std::find(children.begin(), children.end(), sibling) + 1, this);
    Scene::changed();
}

void Node::placeBelow(Node* sibling)
{
    if (sibling == this || !m_parent || sibling->m_parent != m_parent) {
        return;
    }
    auto& children = m_parent->m_children;
    std::erase(children, this);
    children.insert(std::find(children.begin(), children.end(), sibling), this);
    Scene::changed();
}

void Node::reparent(Tree* parent)
{
    if (parent == m_parent) {
        return;
    }
    detach();
    if (parent) {
        m_parent = parent;
        parent->m_children.push_back(this);
    }
    Scene::changed();
}

// ------------------------------------------------------------ sottotipi --

Tree::Tree(Tree* parent)
    : Node(Type::Tree, parent)
{
}

Tree::~Tree()
{
    for (Node* child : m_children) {
        child->m_parent = nullptr;
    }
    m_children.clear();
}

SurfaceNode::SurfaceNode(Tree* parent, wlr_surface* surface)
    : Node(Type::Surface, parent)
    , m_surface(surface)
{
}

RectNode::RectNode(Tree* parent, double width, double height, const wlr_render_color& color)
    : Node(Type::Rect, parent)
    , m_width(width)
    , m_height(height)
    , m_color(color)
{
}

void RectNode::setSize(double width, double height)
{
    if (width == m_width && height == m_height) {
        return;
    }
    m_width = width;
    m_height = height;
    Scene::changed();
}

void RectNode::setColor(const wlr_render_color& color)
{
    m_color = color;
    Scene::changed();
}

BufferNode::BufferNode(Tree* parent, wlr_buffer* buffer, wlr_texture* texture, const wlr_fbox& src,
    wl_output_transform transform, double width, double height)
    : Node(Type::Buffer, parent)
    , m_buffer(wlr_buffer_lock(buffer))
    , m_texture(texture)
    , m_src(src)
    , m_transform(transform)
    , m_width(width)
    , m_height(height)
{
}

BufferNode::~BufferNode()
{
    wlr_buffer_unlock(m_buffer);
}

void BufferNode::setSize(double width, double height)
{
    if (width == m_width && height == m_height) {
        return;
    }
    m_width = width;
    m_height = height;
    Scene::changed();
}

// ------------------------------------------------------------ superfici --

namespace {

void forEachSurfaceAt(wlr_surface* surface, int x, int y, const std::function<void(wlr_surface*, int, int)>& fn)
{
    wlr_subsurface* subsurface;
    wl_list_for_each(subsurface, &surface->current.subsurfaces_below, current.link)
    {
        if (subsurface->surface->mapped) {
            forEachSurfaceAt(subsurface->surface, x + subsurface->current.x, y + subsurface->current.y, fn);
        }
    }
    fn(surface, x, y);
    wl_list_for_each(subsurface, &surface->current.subsurfaces_above, current.link)
    {
        if (subsurface->surface->mapped) {
            forEachSurfaceAt(subsurface->surface, x + subsurface->current.x, y + subsurface->current.y, fn);
        }
    }
}

} // namespace

void forEachSurface(wlr_surface* root, const std::function<void(wlr_surface*, int, int)>& fn)
{
    forEachSurfaceAt(root, 0, 0, fn);
}

void popupPosition(wlr_xdg_popup* popup, double& x, double& y)
{
    x = popup->current.geometry.x - popup->base->geometry.x;
    y = popup->current.geometry.y - popup->base->geometry.y;
    // Rispetto alla finestra (geometria) del genitore, che può avere un
    // margine per l'ombra; un pannello della shell non ce l'ha.
    if (wlr_xdg_surface* parent = popup->parent ? wlr_xdg_surface_try_from_wlr_surface(popup->parent) : nullptr) {
        x += parent->geometry.x;
        y += parent->geometry.y;
    }
}

// Stato per superficie: dal commit al danno degli schermi che la mostrano.
namespace {

struct Watched;

// Parte "C" dello stato, con i membri che wlroots conosce: dal puntatore
// all'addon o al listener si risale qui con un cast.
struct StateShim {
    wlr_addon addon;
    wl_listener commit;
    Watched* owner;
};

struct Watched {
    SurfaceState state;
    StateShim shim;
};

void destroyState(wlr_addon* addon);
const wlr_addon_interface stateAddon {
    .name = "vela-surface-state",
    .destroy = destroyState,
};

void onCommit(wl_listener* listener, void*)
{
    StateShim* shim = wl_container_of(listener, shim, commit);
    wlr_surface* surface = shim->owner->state.surface;
    bool shown = false;
    for (OutputFrame* frame : Scene::instance()->frames) {
        if (frame->shows(surface)) {
            frame->surfaceCommitted(surface);
            shown = true;
        }
    }
    // Una superficie nuova o finora nascosta: forse ora si vede.
    if (!shown) {
        Scene::changed();
    }
}

void destroyState(wlr_addon* addon)
{
    auto* shim = reinterpret_cast<StateShim*>(addon);
    Watched* watched = shim->owner;
    if (Scene* scene = Scene::instance()) {
        for (OutputFrame* frame : scene->frames) {
            frame->surfaceDestroyed(watched->state.surface);
        }
    }
    wl_list_remove(&shim->commit.link);
    wlr_addon_finish(addon);
    delete watched;
}

void refresh(SurfaceState& state)
{
    wlr_output* primary = nullptr;
    wlr_output* pacing = nullptr;
    int64_t bestOverlap = 0;
    int64_t bestVisible = 0;
    for (const SurfaceState::OnOutput& on : state.outputs) {
        if (on.overlap > bestOverlap) {
            bestOverlap = on.overlap;
            primary = on.output;
        }
        if (on.visible > bestVisible
            || (on.visible > 0 && on.visible == bestVisible && pacing && on.output->refresh > pacing->refresh)) {
            bestVisible = on.visible;
            pacing = on.output;
        }
    }
    state.pacing = pacing;
    if (!primary) {
        // Non si vede da nessuna parte (ridotta a icona, fuori schermo):
        // niente leave, così ricomparendo sullo stesso schermo non cambia
        // nulla per l'app.
        return;
    }
    state.primary = primary;

    wlr_surface* surface = state.surface;
    wlr_surface_output* entered;
    wlr_surface_output* tmp;
    wl_list_for_each_safe(entered, tmp, &surface->current_outputs, link)
    {
        const bool still = std::any_of(state.outputs.begin(), state.outputs.end(),
            [&](const SurfaceState::OnOutput& on) { return on.output == entered->output && on.overlap > 0; });
        if (!still) {
            wlr_surface_send_leave(surface, entered->output);
        }
    }
    for (const SurfaceState::OnOutput& on : state.outputs) {
        if (on.overlap > 0) {
            wlr_surface_send_enter(surface, on.output);
        }
    }
    // La scala dello schermo che ne mostra la parte maggiore (§3.6).
    const double scale = primary->scale;
    wlr_fractional_scale_v1_notify_scale(surface, scale);
    wlr_surface_set_preferred_buffer_scale(surface, int32_t(std::ceil(scale)));
}

} // namespace

SurfaceState* surfaceState(wlr_surface* surface)
{
    if (wlr_addon* addon = wlr_addon_find(&surface->addons, nullptr, &stateAddon)) {
        return &reinterpret_cast<StateShim*>(addon)->owner->state;
    }
    return nullptr;
}

void reportSurface(wlr_surface* surface, wlr_output* output, int64_t overlap, int64_t visible)
{
    SurfaceState* state = surfaceState(surface);
    if (!state) {
        return;
    }
    auto it = std::find_if(state->outputs.begin(), state->outputs.end(),
        [output](const SurfaceState::OnOutput& on) { return on.output == output; });
    if (it == state->outputs.end()) {
        state->outputs.push_back({ output, overlap, visible });
    } else if (it->overlap == overlap && it->visible == visible) {
        return;
    } else {
        it->overlap = overlap;
        it->visible = visible;
    }
    refresh(*state);
}

void forgetSurface(wlr_surface* surface, wlr_output* output)
{
    SurfaceState* state = surfaceState(surface);
    if (!state) {
        return;
    }
    const size_t before = state->outputs.size();
    std::erase_if(state->outputs, [output](const SurfaceState::OnOutput& on) { return on.output == output; });
    if (state->outputs.size() != before) {
        refresh(*state);
    }
}

// ----------------------------------------------------------------- Scene --

Scene::Scene()
    : m_root(std::make_unique<Tree>(nullptr))
{
    s_instance = this;
}

Scene::~Scene()
{
    if (m_newSurface.link.next) {
        wl_list_remove(&m_newSurface.link);
    }
    m_root.reset();
    s_instance = nullptr;
}

void Scene::changed()
{
    if (!s_instance) {
        return;
    }
    for (OutputFrame* frame : s_instance->frames) {
        if (frame->scheduleFrame) {
            frame->scheduleFrame();
        }
    }
}

void Scene::watch(wlr_compositor* compositor)
{
    m_newSurface.notify = [](wl_listener*, void* data) {
        auto* surface = static_cast<wlr_surface*>(data);
        auto* watched = new Watched {};
        watched->state.surface = surface;
        watched->shim.owner = watched;
        watched->shim.commit.notify = onCommit;
        wl_signal_add(&surface->events.commit, &watched->shim.commit);
        wlr_addon_init(&watched->shim.addon, &surface->addons, nullptr, &stateAddon);
    };
    wl_signal_add(&compositor->events.new_surface, &m_newSurface);
}

namespace {

bool hitNode(const Node* node, double lx, double ly, Scene::Hit& hit)
{
    if (!node->enabled() || node->ignoresInput) {
        return false;
    }
    const double nx = lx - node->x();
    const double ny = ly - node->y();
    bool found = false;
    switch (node->type()) {
    case Node::Type::Tree: {
        const auto& children = static_cast<const Tree*>(node)->children();
        for (auto it = children.rbegin(); it != children.rend() && !found; ++it) {
            found = hitNode(*it, nx, ny, hit);
        }
        break;
    }
    case Node::Type::Surface: {
        wlr_surface* surface = static_cast<const SurfaceNode*>(node)->surface();
        if (surface->mapped) {
            if (wlr_surface* sub = wlr_surface_surface_at(surface, nx, ny, &hit.sx, &hit.sy)) {
                hit.surface = sub;
                found = true;
            }
        }
        break;
    }
    case Node::Type::Rect:
    case Node::Type::Buffer: {
        // Anteprime e istantanee lasciano passare i clic; la barra del
        // titolo no (senza superficie: è del compositor).
        if (!node->hittable) {
            break;
        }
        double width = 0.0;
        double height = 0.0;
        if (node->type() == Node::Type::Rect) {
            width = static_cast<const RectNode*>(node)->width();
            height = static_cast<const RectNode*>(node)->height();
        } else {
            width = static_cast<const BufferNode*>(node)->width();
            height = static_cast<const BufferNode*>(node)->height();
        }
        found = nx >= 0 && ny >= 0 && nx < width && ny < height;
        if (found) {
            hit.surface = nullptr;
        }
        break;
    }
    }
    if (found && !hit.owner && node->data) {
        hit.owner = node->data;
    }
    return found;
}

} // namespace

Scene::Hit Scene::at(double lx, double ly) const
{
    Hit hit;
    hitNode(m_root.get(), lx, ly, hit);
    return hit;
}

} // namespace vela::scene
