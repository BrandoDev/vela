// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Il device Vulkan di Vela (docs/renderer.md §7). Uno per la GPU che pilota
// gli schermi; tutto il disegno del renderer di Vela passa da qui.

#include "render/formats.hpp"
#include "wlr.hpp"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

namespace vela::render {

class VulkanDevice {
public:
    // backendDrmFd: il device DRM del backend (-1 se non ce l'ha, es.
    // headless): si sceglie la GPU corrispondente. nullptr se Vulkan 1.4 o
    // le estensioni necessarie mancano; il motivo è già nel log.
    static std::unique_ptr<VulkanDevice> create(int backendDrmFd);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const;
    // Un dmabuf di quel formato e modifier si può importare per quell'uso.
    bool supportsDmabuf(VkFormat format, uint64_t modifier, VkImageUsageFlags usage) const;
    // Un dmabuf come VkImage, senza copie (tutti i piani nello stesso dmabuf).
    bool importDmabuf(const wlr_dmabuf_attributes& dmabuf, VkFormat format, VkImageUsageFlags usage,
        VkImage& image, VkDeviceMemory& memory) const;
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
    // (formato, modifier) dei dmabuf che sappiamo leggere come texture:
    // quelli che offriamo alle app (linux-dmabuf).
    wlr_drm_format_set textureFormats {};
    // Formati dei buffer in memoria condivisa (wl_shm) che sappiamo caricare.
    std::vector<uint32_t> shmFormats;
    // La GPU sa esportare e importare semafori come sync_file
    // (sincronizzazione implicita con il kernel, §7.3).
    bool syncFile = false;

    PFN_vkGetMemoryFdPropertiesKHR getMemoryFdProperties = nullptr;
    PFN_vkGetSemaphoreFdKHR getSemaphoreFd = nullptr;
    PFN_vkImportSemaphoreFdKHR importSemaphoreFd = nullptr;

    // Timestamp della GPU (§4.3: quanto costa un frame). timestampPeriod:
    // ns per tick; 0 se la coda non li sa scrivere. Con i timestamp
    // calibrati si sa anche *quando* la GPU ha finito, sull'orologio
    // CLOCK_MONOTONIC.
    float timestampPeriod = 0.0f;
    uint64_t timestampMask = 0;
    PFN_vkGetCalibratedTimestampsKHR getCalibratedTimestamps = nullptr;

private:
    VulkanDevice() = default;
    bool init(int backendDrmFd);
    bool pickPhysicalDevice(int backendDrmFd);
    bool createDevice();
    bool openRenderNode();
    void queryFormats();

    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDeviceDrmPropertiesEXT m_drm {};
};

} // namespace vela::render
