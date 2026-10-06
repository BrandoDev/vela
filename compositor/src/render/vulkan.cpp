// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "render/vulkan.hpp"

#include <drm_fourcc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/sysmacros.h>
#include <xf86drm.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace vela::render {

namespace {

// Il minimo per scambiare buffer con il kernel e con le app (§7.1).
constexpr const char* requiredExtensions[] = {
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
};

bool envFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value && std::strcmp(value, "0") != 0;
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
{
    return std::any_of(list.begin(), list.end(),
        [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    const wlr_log_importance level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? WLR_ERROR
        : severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT                        ? WLR_INFO
                                                                                              : WLR_DEBUG;
    wlr_log(level, "Vulkan: %s", data->pMessage);
    return VK_FALSE;
}

} // namespace

std::unique_ptr<VulkanDevice> VulkanDevice::create(int backendDrmFd)
{
    std::unique_ptr<VulkanDevice> vk(new VulkanDevice);
    if (!vk->init(backendDrmFd)) {
        return nullptr;
    }
    return vk;
}

VulkanDevice::~VulkanDevice()
{
    wlr_drm_format_set_finish(&renderFormats);
    wlr_drm_format_set_finish(&textureFormats);
    if (renderFd >= 0) {
        close(renderFd);
    }
    if (device) {
        vkDestroyDevice(device, nullptr);
    }
    if (m_messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance, m_messenger, nullptr);
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
    }
}

bool VulkanDevice::init(int backendDrmFd)
{
    uint32_t version = 0;
    if (vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_4) {
        wlr_log(WLR_ERROR, "Vela needs Vulkan 1.4: the system offers at most %u.%u",
            VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return false;
    }

    // Validation layer su richiesta (VELA_VULKAN_VALIDATION=1), se installati.
    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    if (envFlag("VELA_VULKAN_VALIDATION")) {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        const bool found = std::any_of(available.begin(), available.end(), [](const VkLayerProperties& l) {
            return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (found) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            wlr_log(WLR_ERROR, "VELA_VULKAN_VALIDATION: validation layers not installed "
                               "(package vulkan-validation-layers)");
        }
    }

    const VkApplicationInfo app {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vela-compositor",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "vela",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };
    const VkInstanceCreateInfo instanceInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "Can't create the Vulkan instance");
        return false;
    }

    if (!layers.empty()) {
        const VkDebugUtilsMessengerCreateInfoEXT messengerInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = onDebugMessage,
        };
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        create(instance, &messengerInfo, nullptr, &m_messenger);
        wlr_log(WLR_INFO, "Vulkan validation layers enabled");
    }

    if (!pickPhysicalDevice(backendDrmFd) || !createDevice() || !openRenderNode()) {
        return false;
    }
    queryFormats();
    if (renderFormats.len == 0) {
        wlr_log(WLR_ERROR, "%s: no format usable for drawing to outputs", name.c_str());
        return false;
    }
    return true;
}

bool VulkanDevice::pickPhysicalDevice(int backendDrmFd)
{
    dev_t backendDev = 0;
    if (backendDrmFd >= 0) {
        struct stat st {};
        if (fstat(backendDrmFd, &st) == 0) {
            backendDev = st.st_rdev;
        }
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(candidate, &props);

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, exts.data());

        const char* missing = nullptr;
        for (const char* ext : requiredExtensions) {
            if (!hasExtension(exts, ext)) {
                missing = ext;
                break;
            }
        }
        if (props.apiVersion < VK_API_VERSION_1_4 || missing) {
            wlr_log(WLR_INFO, "GPU %s skipped: %s", props.deviceName,
                missing ? missing : "Vulkan 1.4 not supported");
            continue;
        }

        VkPhysicalDeviceDrmPropertiesEXT drm { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT };
        VkPhysicalDeviceProperties2 props2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drm };
        vkGetPhysicalDeviceProperties2(candidate, &props2);
        if (!drm.hasRender) {
            continue;
        }

        // La GPU dello schermo vince sempre; senza indicazioni (headless) si
        // preferisce una scheda dedicata.
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2
            : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU    ? 1
                                                                            : 0;
        if (backendDev != 0) {
            const dev_t primary = makedev(drm.primaryMajor, drm.primaryMinor);
            const dev_t render = makedev(drm.renderMajor, drm.renderMinor);
            if ((drm.hasPrimary && primary == backendDev) || render == backendDev) {
                score = 100;
            }
        }
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
            m_drm = drm;
            name = props.deviceName;
        }
    }

    if (!best) {
        wlr_log(WLR_ERROR, "No GPU with Vulkan 1.4 and the dmabuf extensions: Vela can't draw");
        return false;
    }
    if (backendDev != 0 && bestScore < 100) {
        wlr_log(WLR_ERROR, "The output's GPU has no Vulkan 1.4: using %s", name.c_str());
    }
    physical = best;
    return true;
}

bool VulkanDevice::createDevice()
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;
            found = true;
        }
    }
    if (!found) {
        wlr_log(WLR_ERROR, "%s: no graphics queue", name.c_str());
        return false;
    }

    // Cosa usiamo del core: semafori timeline (1.2) per sapere quando la GPU
    // ha finito, dynamic rendering e synchronization2 (1.3), push
    // descriptor (1.4) per legare le texture senza pool di descrittori.
    VkPhysicalDeviceVulkan14Features supported14 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
    VkPhysicalDeviceVulkan13Features supported13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &supported14,
    };
    VkPhysicalDeviceVulkan12Features supported12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &supported13,
    };
    VkPhysicalDeviceFeatures2 supported { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported12 };
    vkGetPhysicalDeviceFeatures2(physical, &supported);
    if (!supported12.timelineSemaphore || !supported13.dynamicRendering || !supported13.synchronization2
        || !supported14.pushDescriptor) {
        wlr_log(WLR_ERROR, "%s: required Vulkan 1.4 features are missing", name.c_str());
        return false;
    }

    VkPhysicalDeviceVulkan14Features enabled14 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pushDescriptor = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features enabled13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabled14,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features enabled12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &enabled13,
        .timelineSemaphore = VK_TRUE,
    };
    // Facoltativi: i timestamp calibrati, per sapere a che ora (sul nostro
    // orologio) la GPU finisce un frame.
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, exts.data());
    std::vector<const char*> extensions(std::begin(requiredExtensions), std::end(requiredExtensions));
    bool calibrated = false;
    if (hasExtension(exts, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
        auto getDomains = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR"));
        uint32_t domainCount = 0;
        std::vector<VkTimeDomainKHR> domains;
        if (getDomains && getDomains(physical, &domainCount, nullptr) == VK_SUCCESS) {
            domains.resize(domainCount);
            getDomains(physical, &domainCount, domains.data());
        }
        const auto has = [&](VkTimeDomainKHR domain) {
            return std::find(domains.begin(), domains.end(), domain) != domains.end();
        };
        if (has(VK_TIME_DOMAIN_DEVICE_KHR) && has(VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR)) {
            extensions.push_back(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            calibrated = true;
        }
    }

    // Coda ad alta priorità (§4.3): i frame del compositor passano davanti
    // al lavoro delle app, anche di un gioco che tiene la GPU al 100%. Il
    // kernel la concede solo a chi ha CAP_SYS_NICE (come kwin_wayland);
    // altrimenti si resta alla priorità normale.
    // Si chiede solo se il processo ha CAP_SYS_NICE (bit 23 di CapEff): un
    // tentativo rifiutato riempirebbe il log di errori del caricatore.
    bool mayRaisePriority = geteuid() == 0;
    if (FILE* status = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), status)) {
            unsigned long long caps = 0;
            if (std::sscanf(line, "CapEff: %llx", &caps) == 1) {
                mayRaisePriority = mayRaisePriority || (caps & (1ULL << 23));
            }
        }
        std::fclose(status);
    }
    const bool globalPriority = mayRaisePriority && hasExtension(exts, VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME);
    if (globalPriority) {
        extensions.push_back(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME);
    }
    const VkDeviceQueueGlobalPriorityCreateInfoKHR highPriority {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR,
        .globalPriority = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
    };
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = globalPriority ? &highPriority : nullptr,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo deviceInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled12,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    VkResult result = vkCreateDevice(physical, &deviceInfo, nullptr, &device);
    if (globalPriority && (result == VK_ERROR_NOT_PERMITTED_KHR || result == VK_ERROR_INITIALIZATION_FAILED)) {
        queueInfo.pNext = nullptr;
        result = vkCreateDevice(physical, &deviceInfo, nullptr, &device);
        wlr_log(WLR_INFO, "%s: normal-priority GPU queue (high priority was refused)", name.c_str());
    } else if (globalPriority && result == VK_SUCCESS) {
        wlr_log(WLR_INFO, "%s: high-priority GPU queue", name.c_str());
    } else if (!mayRaisePriority) {
        wlr_log(WLR_INFO, "%s: normal-priority GPU queue (high priority needs CAP_SYS_NICE)",
            name.c_str());
    }
    if (result != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "%s: can't create the Vulkan device", name.c_str());
        return false;
    }
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> familyProps(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, familyProps.data());
    const uint32_t validBits = familyProps[queueFamily].timestampValidBits;
    if (validBits > 0) {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(physical, &props);
        timestampPeriod = props.limits.timestampPeriod;
        timestampMask = validBits >= 64 ? UINT64_MAX : (uint64_t(1) << validBits) - 1;
    }
    if (calibrated) {
        getCalibratedTimestamps = reinterpret_cast<PFN_vkGetCalibratedTimestampsKHR>(
            vkGetDeviceProcAddr(device, "vkGetCalibratedTimestampsKHR"));
    }
    if (timestampPeriod <= 0.0f) {
        wlr_log(WLR_INFO, "%s: no GPU timestamps, frame cost is estimated from the CPU", name.c_str());
    } else if (!getCalibratedTimestamps) {
        wlr_log(WLR_INFO, "%s: no calibrated timestamps, only the GPU work duration is measured",
            name.c_str());
    }

    getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
    getSemaphoreFd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
    importSemaphoreFd = reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR"));

    const VkPhysicalDeviceExternalSemaphoreInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkExternalSemaphoreProperties semaphoreProps { .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES };
    vkGetPhysicalDeviceExternalSemaphoreProperties(physical, &semaphoreInfo, &semaphoreProps);
    const VkExternalSemaphoreFeatureFlags both
        = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    syncFile = (semaphoreProps.externalSemaphoreFeatures & both) == both;
    if (!syncFile) {
        wlr_log(WLR_INFO, "%s: no sync_file semaphores, the CPU will wait for the GPU every frame", name.c_str());
    }
    return true;
}

bool VulkanDevice::openRenderNode()
{
    drmDevice* dev = nullptr;
    if (drmGetDeviceFromDevId(makedev(m_drm.renderMajor, m_drm.renderMinor), 0, &dev) != 0) {
        wlr_log(WLR_ERROR, "%s: render node %ld:%ld not found", name.c_str(),
            static_cast<long>(m_drm.renderMajor), static_cast<long>(m_drm.renderMinor));
        return false;
    }
    const std::string path = (dev->available_nodes & (1 << DRM_NODE_RENDER)) ? dev->nodes[DRM_NODE_RENDER] : "";
    drmFreeDevice(&dev);
    if (path.empty() || (renderFd = open(path.c_str(), O_RDWR | O_CLOEXEC)) < 0) {
        wlr_log_errno(WLR_ERROR, "%s: can't open the render node %s", name.c_str(), path.c_str());
        return false;
    }
    wlr_log(WLR_INFO, "Vela renderer: %s (%s)", name.c_str(), path.c_str());
    return true;
}

bool VulkanDevice::supportsDmabuf(VkFormat format, uint64_t modifier, VkImageUsageFlags usage) const
{
    const VkPhysicalDeviceImageDrmFormatModifierInfoEXT modInfo {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
        .drmFormatModifier = modifier,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkPhysicalDeviceExternalImageFormatInfo externalInfo {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = &modInfo,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    const VkPhysicalDeviceImageFormatInfo2 imageInfo {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &externalInfo,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = usage,
    };
    VkExternalImageFormatProperties externalProps { .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
    VkImageFormatProperties2 imageProps { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &externalProps };
    if (vkGetPhysicalDeviceImageFormatProperties2(physical, &imageInfo, &imageProps) != VK_SUCCESS) {
        return false;
    }
    return externalProps.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
}

void VulkanDevice::queryFormats()
{
    for (const PixelFormat& format : pixelFormats()) {
        // dmabuf: per ogni modifier, se si può leggere (texture delle app)
        // e se ci si può disegnare sopra (schermi, catture, cursori).
        VkDrmFormatModifierPropertiesListEXT list { .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
        VkFormatProperties2 props { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &list };
        vkGetPhysicalDeviceFormatProperties2(physical, format.unorm, &props);
        std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(list.drmFormatModifierCount);
        list.pDrmFormatModifierProperties = modifiers.data();
        vkGetPhysicalDeviceFormatProperties2(physical, format.unorm, &props);

        const VkFormatFeatureFlags sampled
            = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        for (const VkDrmFormatModifierPropertiesEXT& mod : modifiers) {
            if ((mod.drmFormatModifierTilingFeatures & sampled) == sampled
                && supportsDmabuf(format.unorm, mod.drmFormatModifier,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                wlr_drm_format_set_add(&textureFormats, format.drm, mod.drmFormatModifier);
            }
        }

        if (format.srgb != VK_FORMAT_UNDEFINED) {
            VkDrmFormatModifierPropertiesListEXT srgbList {
                .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
            };
            VkFormatProperties2 srgbProps { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &srgbList };
            vkGetPhysicalDeviceFormatProperties2(physical, format.srgb, &srgbProps);
            std::vector<VkDrmFormatModifierPropertiesEXT> srgbModifiers(srgbList.drmFormatModifierCount);
            srgbList.pDrmFormatModifierProperties = srgbModifiers.data();
            vkGetPhysicalDeviceFormatProperties2(physical, format.srgb, &srgbProps);
            for (const VkDrmFormatModifierPropertiesEXT& mod : srgbModifiers) {
                if ((mod.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                    && supportsDmabuf(format.srgb, mod.drmFormatModifier, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
                    wlr_drm_format_set_add(&renderFormats, format.drm, mod.drmFormatModifier);
                }
            }
        }

        // Memoria condivisa: la copiamo in un'immagine nostra.
        const VkFormatFeatureFlags shmNeeds = sampled | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((props.formatProperties.optimalTilingFeatures & shmNeeds) == shmNeeds) {
            shmFormats.push_back(format.drm);
        }
    }
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const
{
    VkPhysicalDeviceMemoryProperties props {};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VulkanDevice::importDmabuf(const wlr_dmabuf_attributes& dmabuf, VkFormat format, VkImageUsageFlags usage,
    VkImage& image, VkDeviceMemory& memory) const
{
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    // Niente immagini "disjoint": i formati RGB non ne hanno bisogno.
    struct stat first {};
    fstat(dmabuf.fd[0], &first);
    for (int i = 1; i < dmabuf.n_planes; ++i) {
        struct stat other {};
        fstat(dmabuf.fd[i], &other);
        if (other.st_ino != first.st_ino) {
            wlr_log(WLR_ERROR, "dmabuf with planes in different buffers: not supported");
            return false;
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
        .format = format,
        .extent = { uint32_t(dmabuf.width), uint32_t(dmabuf.height), 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkMemoryFdPropertiesKHR fdProps { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    getMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dmabuf.fd[0], &fdProps);
    const uint32_t memoryType = findMemoryType(requirements.memoryTypeBits & fdProps.memoryTypeBits, 0);

    // Vulkan prende possesso del descrittore: gliene diamo una copia.
    const int fd = fcntl(dmabuf.fd[0], F_DUPFD_CLOEXEC, 0);
    const VkMemoryDedicatedAllocateInfo dedicated {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image,
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
    if (fd < 0 || memoryType == UINT32_MAX || vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        if (fd >= 0) {
            close(fd);
        }
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(device, image, memory, 0);
    return true;
}

VkShaderModule VulkanDevice::createShader(const uint32_t* code, size_t bytes) const
{
    const VkShaderModuleCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes,
        .pCode = code,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

} // namespace vela::render
