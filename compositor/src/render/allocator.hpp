#pragma once

#include "wlr.hpp"

namespace vela::render {

// Allocatore dei buffer degli schermi (docs/renderer.md §7.2): GBM sul
// render node della GPU di Vela, buffer esportati come dmabuf con un
// modifier esplicito, adatti sia al disegno Vulkan sia allo scanout.
// Si distrugge con wlr_allocator_destroy(). Non chiude renderFd.
wlr_allocator* createGbmAllocator(int renderFd);

} // namespace vela::render
