#pragma once

// Il device Vulkan di Vela (docs/renderer.md §7). Uno per la GPU che pilota
// gli schermi; tutto il disegno del renderer di Vela passa da qui.

#include "wlr.hpp"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

namespace vela::render {

// Formati DRM che sappiamo usare, con il formato Vulkan corrispondente. Le
// viste sono _SRGB: gli shader lavorano in spazio lineare e la GPU converte
// scrivendo (§7.5).
struct FormatInfo {
    uint32_t drm;
    VkFormat vk;
};
const FormatInfo* formatInfo(uint32_t drmFormat);

class VulkanDevice {
public:
    // backendDrmFd: il device DRM del backend (-1 se non ce l'ha, es.
    // headless): si sceglie la GPU corrispondente. nullptr se Vulkan 1.3 o
    // le estensioni necessarie mancano; il motivo è già nel log.
    static std::unique_ptr<VulkanDevice> create(int backendDrmFd);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const;
    VkShaderModule createShader(const uint32_t* code, size_t bytes) const;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    std::string name;

    // Il render node della stessa GPU, aperto da noi: serve all'allocatore.
    int renderFd = -1;
    // (formato, modifier) importabili come dmabuf e usabili come
    // destinazione di rendering.
    wlr_drm_format_set renderFormats {};
    // La GPU sa esportare e importare semafori come sync_file
    // (sincronizzazione implicita con il kernel, §7.3).
    bool syncFile = false;

    PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProperties = nullptr;
    PFN_vkGetSemaphoreFdKHR getSemaphoreFd = nullptr;
    PFN_vkImportSemaphoreFdKHR importSemaphoreFd = nullptr;

private:
    VulkanDevice() = default;
    bool init(int backendDrmFd);
    bool pickPhysicalDevice(int backendDrmFd);
    bool createDevice();
    bool openRenderNode();
    void queryRenderFormats();

    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDeviceDrmPropertiesEXT m_drm {};
};

} // namespace vela::render
