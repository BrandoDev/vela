#include "render/vulkan.hpp"

#include <drm_fourcc.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <xf86drm.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace vela::render {

namespace {

constexpr FormatInfo formats[] = {
    { DRM_FORMAT_XRGB8888, VK_FORMAT_B8G8R8A8_SRGB },
    { DRM_FORMAT_ARGB8888, VK_FORMAT_B8G8R8A8_SRGB },
    { DRM_FORMAT_XBGR8888, VK_FORMAT_R8G8B8A8_SRGB },
    { DRM_FORMAT_ABGR8888, VK_FORMAT_R8G8B8A8_SRGB },
};

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

const FormatInfo* formatInfo(uint32_t drmFormat)
{
    for (const FormatInfo& info : formats) {
        if (info.drm == drmFormat) {
            return &info;
        }
    }
    return nullptr;
}

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
    if (vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3) {
        wlr_log(WLR_ERROR, "Vela richiede Vulkan 1.3: il sistema offre al massimo %u.%u",
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
            wlr_log(WLR_ERROR, "VELA_VULKAN_VALIDATION: validation layer non installati "
                               "(pacchetto vulkan-validation-layers)");
        }
    }

    const VkApplicationInfo app {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vela-compositor",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "vela",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
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
        wlr_log(WLR_ERROR, "Impossibile creare l'istanza Vulkan");
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
        wlr_log(WLR_INFO, "Validation layer di Vulkan attivi");
    }

    if (!pickPhysicalDevice(backendDrmFd) || !createDevice() || !openRenderNode()) {
        return false;
    }
    queryRenderFormats();
    if (renderFormats.len == 0) {
        wlr_log(WLR_ERROR, "%s: nessun formato utilizzabile per disegnare sugli schermi", name.c_str());
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
        if (props.apiVersion < VK_API_VERSION_1_3 || missing) {
            wlr_log(WLR_INFO, "GPU %s scartata: %s", props.deviceName,
                missing ? missing : "Vulkan 1.3 non supportato");
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
        wlr_log(WLR_ERROR, "Nessuna GPU con Vulkan 1.3 e le estensioni per i dmabuf: Vela non può disegnare");
        return false;
    }
    if (backendDev != 0 && bestScore < 100) {
        wlr_log(WLR_ERROR, "La GPU dello schermo non ha Vulkan 1.3: uso %s", name.c_str());
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
        wlr_log(WLR_ERROR, "%s: nessuna coda grafica", name.c_str());
        return false;
    }

    VkPhysicalDeviceVulkan13Features supported13 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 supported { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported13 };
    vkGetPhysicalDeviceFeatures2(physical, &supported);
    if (!supported13.dynamicRendering || !supported13.synchronization2) {
        wlr_log(WLR_ERROR, "%s: mancano dynamic rendering o synchronization2", name.c_str());
        return false;
    }

    VkPhysicalDeviceVulkan13Features enabled13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo deviceInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(std::size(requiredExtensions)),
        .ppEnabledExtensionNames = requiredExtensions,
    };
    if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        wlr_log(WLR_ERROR, "%s: impossibile creare il device Vulkan", name.c_str());
        return false;
    }
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

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
        wlr_log(WLR_INFO, "%s: niente semafori sync_file, la CPU aspetterà la GPU a ogni frame", name.c_str());
    }
    return true;
}

bool VulkanDevice::openRenderNode()
{
    drmDevice* dev = nullptr;
    if (drmGetDeviceFromDevId(makedev(m_drm.renderMajor, m_drm.renderMinor), 0, &dev) != 0) {
        wlr_log(WLR_ERROR, "%s: render node %ld:%ld non trovato", name.c_str(),
            static_cast<long>(m_drm.renderMajor), static_cast<long>(m_drm.renderMinor));
        return false;
    }
    const std::string path = (dev->available_nodes & (1 << DRM_NODE_RENDER)) ? dev->nodes[DRM_NODE_RENDER] : "";
    drmFreeDevice(&dev);
    if (path.empty() || (renderFd = open(path.c_str(), O_RDWR | O_CLOEXEC)) < 0) {
        wlr_log_errno(WLR_ERROR, "%s: impossibile aprire il render node %s", name.c_str(), path.c_str());
        return false;
    }
    wlr_log(WLR_INFO, "Renderer di Vela: %s (%s)", name.c_str(), path.c_str());
    return true;
}

void VulkanDevice::queryRenderFormats()
{
    for (const FormatInfo& info : formats) {
        VkDrmFormatModifierPropertiesListEXT list { .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
        VkFormatProperties2 props { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &list };
        vkGetPhysicalDeviceFormatProperties2(physical, info.vk, &props);
        std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(list.drmFormatModifierCount);
        list.pDrmFormatModifierProperties = modifiers.data();
        vkGetPhysicalDeviceFormatProperties2(physical, info.vk, &props);

        for (const VkDrmFormatModifierPropertiesEXT& mod : modifiers) {
            if (!(mod.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
                continue;
            }
            // Il modifier deve essere anche importabile da un dmabuf.
            const VkPhysicalDeviceImageDrmFormatModifierInfoEXT modInfo {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
                .drmFormatModifier = mod.drmFormatModifier,
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
                .format = info.vk,
                .type = VK_IMAGE_TYPE_2D,
                .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            };
            VkExternalImageFormatProperties externalProps { .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
            VkImageFormatProperties2 imageProps { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, .pNext = &externalProps };
            if (vkGetPhysicalDeviceImageFormatProperties2(physical, &imageInfo, &imageProps) != VK_SUCCESS) {
                continue;
            }
            if (!(externalProps.externalMemoryProperties.externalMemoryFeatures
                    & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
                continue;
            }
            wlr_drm_format_set_add(&renderFormats, info.drm, mod.drmFormatModifier);
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
