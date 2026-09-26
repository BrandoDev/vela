#pragma once

#include "render/frame_clock.hpp"
#include "render/vulkan.hpp"

#include <array>
#include <memory>
#include <vector>

namespace vela::render {

// Disegna uno schermo con il renderer di Vela (docs/renderer.md §6).
// Tappa S0: la scena di prova (s0.frag) su una swapchain nostra, con i
// buffer importati in Vulkan e la sincronizzazione implicita con il kernel.
class OutputRenderer {
public:
    static std::unique_ptr<OutputRenderer> create(VulkanDevice& vk, wlr_allocator* allocator, wlr_output* output);
    ~OutputRenderer();

    OutputRenderer(const OutputRenderer&) = delete;
    OutputRenderer& operator=(const OutputRenderer&) = delete;

    // Disegna il frame che verrà mostrato all'istante `presentNs` e lo mette
    // in `state` (da passare a wlr_output_commit_state).
    bool render(wlr_output_state* state, int64_t presentNs);

private:
    // Un buffer della swapchain importato in Vulkan; vive quanto il buffer
    // (legato con un wlr_addon).
    struct Target {
        OutputRenderer* owner;
        wlr_addon addon;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        int dmabufFd = -1; // piano 0, per la sincronizzazione implicita (non nostro)
    };
    // Risorse di un frame in volo.
    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquire = VK_NULL_HANDLE; // attesa di chi usava ancora il buffer
        VkSemaphore release = VK_NULL_HANDLE; // fine del disegno, esportato come sync_file
    };

    OutputRenderer(VulkanDevice& vk, wlr_allocator* allocator, wlr_output* output);
    bool init();
    bool ensureSwapchain();
    bool ensurePipeline(VkFormat format);
    Target* targetFor(wlr_buffer* buffer);
    static void destroyTarget(wlr_addon* addon);
    static const wlr_addon_interface s_targetAddon;
    bool waitForReaders(const Target& target, Frame& frame);
    void publishFence(const Target& target, Frame& frame);

    VulkanDevice& m_vk;
    wlr_allocator* m_allocator;
    wlr_output* m_output;

    wlr_swapchain* m_swapchain = nullptr;
    const FormatInfo* m_format = nullptr;
    // Tutti i buffer importati, anche di swapchain già sostituite ma ancora
    // mostrati dallo schermo.
    std::vector<Target*> m_targets;

    VkCommandPool m_pool = VK_NULL_HANDLE;
    std::array<Frame, 3> m_frames {};
    size_t m_frameIndex = 0;

    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkFormat m_pipelineFormat = VK_FORMAT_UNDEFINED;
};

} // namespace vela::render
