#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <set>
#include <cstring>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstdlib>

// 调试宏，发布版本注释关闭校验层与调试回调
#define DEBUG

// ===================== 全局常量 =====================
constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t VULKAN_API_VER = VK_API_VERSION_1_2;
constexpr const char* VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";
constexpr const char* SWAPCHAIN_DEV_EXT = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
constexpr const char* DEBUG_UTILS_EXT = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

#ifdef DEBUG
const std::vector<const char*> VALIDATION_LAYERS = { VALIDATION_LAYER_NAME };
#else
const std::vector<const char*> VALIDATION_LAYERS = {};
#endif

const std::vector<const char*> DEVICE_EXTENSIONS = { SWAPCHAIN_DEV_EXT };

// ===================== 日志分级宏 =====================
#define LOG_INFO(msg)  std::cout << "[INFO] " << msg << "\n"
#define LOG_WARN(msg)  std::cout << "[WARN] " << msg << "\n"
#define LOG_ERR(msg)   std::cerr << "[ERROR] " << msg << "\n"

// ===================== 通用校验宏（兼容Debug/Release） =====================
#ifdef DEBUG
#define VK_CHECK(result, msg) \
do { \
    VkResult res = (result); \
    if (res != VK_SUCCESS) { \
        LOG_ERR(msg << " | Code: " << VkResultToString(res)); \
        assert(false); \
    } \
} while(0)
#else
#define VK_CHECK(result, msg) \
do { \
    VkResult res = (result); \
    if (res != VK_SUCCESS) { \
        LOG_ERR(msg << " | Code: " << VkResultToString(res)); \
        cleanUp(window); \
        exit(EXIT_FAILURE); \
    } \
} while(0)
#endif

// ===================== 交换链封装（自带销毁） =====================
struct SwapchainData
{
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat imageFormat;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    void destroy(VkDevice dev)
    {
        for (auto iv : imageViews)
            vkDestroyImageView(dev, iv, nullptr);
        imageViews.clear();
        images.clear();

        if (handle != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(dev, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    }
} swapchainData;

// 渲染链路
VkRenderPass renderPass = VK_NULL_HANDLE;
std::vector<VkFramebuffer> framebuffers;

// 命令池&同步信号量（渲染循环必备）
VkCommandPool commandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> commandBuffers;
std::vector<VkSemaphore> imageAvailableSemaphores;
std::vector<VkSemaphore> renderFinishedSemaphores;
std::vector<VkFence> inFlightFences;
uint32_t currentFrame = 0;

// Vulkan上下文句柄
VkInstance instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice logicalDevice = VK_NULL_HANDLE;
VkSurfaceKHR windowSurface = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;

uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
uint32_t presentQueueFamilyIndex = UINT32_MAX;
bool framebufferResized = false;

// ===================== 工具函数 =====================
const char* VkResultToString(VkResult res)
{
    switch (res)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    default: return "UNKNOWN_VK_ERROR";
    }
}

// Debug回调
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[Vulkan Debug] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

bool setupDebugMessenger()
{
    if (VALIDATION_LAYERS.empty()) return true;

    auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT)
    {
        LOG_WARN("Failed load debug utils create function");
        return false;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger), "Create debug messenger");
    return true;
}

void destroyDebugMessenger()
{
    if (!debugMessenger || instance == VK_NULL_HANDLE) return;
    auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (vkDestroyDebugUtilsMessengerEXT)
    {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
}

// 校验层支持
bool checkValidationLayerSupport()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    std::set<std::string> layerSet;
    for (auto& prop : availableLayers) layerSet.insert(prop.layerName);

    for (const char* layerName : VALIDATION_LAYERS)
    {
        if (!layerSet.count(layerName))
        {
            LOG_WARN("Missing validation layer: " << layerName);
            return false;
        }
    }
    return true;
}

// 实例扩展校验
bool checkInstanceExtensionSupport(const std::vector<const char*>& required)
{
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> avail(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, avail.data());

    std::set<std::string> availSet;
    for (auto& e : avail) availSet.insert(e.extensionName);

    for (const char* req : required)
    {
        if (!availSet.count(req))
        {
            LOG_ERR("Missing instance extension: " << req);
            return false;
        }
    }
    return true;
}

// 设备扩展校验
bool checkDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& requiredExtensions)
{
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

    std::set<std::string> avail;
    for (auto& e : exts) avail.insert(e.extensionName);
    for (auto req : requiredExtensions)
        if (!avail.count(req)) return false;
    return true;
}

// 获取实例需要的扩展
std::vector<const char*> getRequiredInstanceExtensions(bool enableDebugExt)
{
    uint32_t glfwExtCnt = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCnt);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCnt);
    if (enableDebugExt) extensions.push_back(DEBUG_UTILS_EXT);
    return extensions;
}

// 窗口缩放回调
void framebufferResizeCallback(GLFWwindow* win, int width, int height)
{
    framebufferResized = true;
}

// 查询队列族
bool findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface, uint32_t& outGraphics, uint32_t& outPresent)
{
    uint32_t queueCnt = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueCnt, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCnt);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueCnt, queues.data());

    bool foundGraphics = false;
    bool foundPresent = false;
    for (uint32_t i = 0; i < queueCnt; i++)
    {
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !foundGraphics)
        {
            outGraphics = i;
            foundGraphics = true;
        }
        VkBool32 canPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &canPresent);
        if (canPresent && !foundPresent)
        {
            outPresent = i;
            foundPresent = true;
        }
        if (foundGraphics && foundPresent) break;
    }
    return foundGraphics && foundPresent;
}

// 挑选物理设备
bool pickPhysicalDevice(VkSurfaceKHR surface)
{
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    uint32_t bestGfxIdx = UINT32_MAX, bestPreIdx = UINT32_MAX;

    for (VkPhysicalDevice gpu : gpus)
    {
        VkPhysicalDeviceProperties prop{};
        vkGetPhysicalDeviceProperties(gpu, &prop);

        uint32_t gIdx, pIdx;
        bool queueOk = findQueueFamilies(gpu, surface, gIdx, pIdx);
        bool extOk = checkDeviceExtensionSupport(gpu, DEVICE_EXTENSIONS);
        if (!queueOk || !extOk) continue;

        // 优先离散显卡
        if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            bestDevice = gpu;
            bestGfxIdx = gIdx;
            bestPreIdx = pIdx;
            break;
        }
        if (bestDevice == VK_NULL_HANDLE)
        {
            bestDevice = gpu;
            bestGfxIdx = gIdx;
            bestPreIdx = pIdx;
        }
    }

    if (bestDevice == VK_NULL_HANDLE)
    {
        LOG_ERR("No compatible GPU found!");
        return false;
    }
    physicalDevice = bestDevice;
    graphicsQueueFamilyIndex = bestGfxIdx;
    presentQueueFamilyIndex = bestPreIdx;

    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(physicalDevice, &prop);
    LOG_INFO("Selected GPU: " << prop.deviceName);
    LOG_INFO("Graphics queue idx: " << graphicsQueueFamilyIndex);
    LOG_INFO("Present queue idx: " << presentQueueFamilyIndex);
    return true;
}

// 创建Vulkan实例
bool createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BigheroGameEngine Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "BigheroEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VULKAN_API_VER;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;

    bool layersOk = checkValidationLayerSupport();
    std::vector<const char*> instanceExts = getRequiredInstanceExtensions(layersOk);
    if (!checkInstanceExtensionSupport(instanceExts)) return false;

    info.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    info.ppEnabledExtensionNames = instanceExts.data();
    if (layersOk)
    {
        info.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }
    else
    {
        LOG_WARN("Validation layers missing, run without debug");
        info.enabledLayerCount = 0;
        info.ppEnabledLayerNames = nullptr;
    }

    VK_CHECK(vkCreateInstance(&info, nullptr, &instance), "Create Vulkan Instance");
    LOG_INFO("Vulkan instance created");
    if (layersOk) setupDebugMessenger();
    return true;
}

// 创建逻辑设备
bool createLogicalDevice()
{
    std::set<uint32_t> uniqueQueues = { graphicsQueueFamilyIndex, presentQueueFamilyIndex };
    std::vector<VkDeviceQueueCreateInfo> qInfos;
    float prio = 1.0f;
    for (uint32_t idx : uniqueQueues)
    {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = idx;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        qInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
    VkPhysicalDeviceFeatures deviceFeatures{};
    if (supportedFeatures.samplerAnisotropy)
        deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo devInfo{};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = static_cast<uint32_t>(qInfos.size());
    devInfo.pQueueCreateInfos = qInfos.data();
    devInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
    devInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
    devInfo.pEnabledFeatures = &deviceFeatures;

    VK_CHECK(vkCreateDevice(physicalDevice, &devInfo, nullptr, &logicalDevice), "Create Logical Device");
    vkGetDeviceQueue(logicalDevice, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(logicalDevice, presentQueueFamilyIndex, 0, &presentQueue);
    LOG_INFO("Logical device created, queues acquired");
    return true;
}

// 创建窗口Surface
bool createSurface(GLFWwindow* win)
{
    VK_CHECK(glfwCreateWindowSurface(instance, win, nullptr, &windowSurface), "Create Window Surface");
    LOG_INFO("Window surface created");
    return true;
}

// Swapchain辅助工具
std::vector<VkSurfaceFormatKHR> getSurfaceFormats()
{
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, windowSurface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, windowSurface, &formatCount, formats.data());
    return formats;
}

std::vector<VkPresentModeKHR> getSurfacePresentModes()
{
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, windowSurface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, windowSurface, &modeCount, modes.data());
    return modes;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return fmt;
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes)
{
    for (VkPresentModeKHR m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* win)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    w = std::max(w, 1);
    h = std::max(h, 1);
    VkExtent2D actualExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    actualExtent.width = std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actualExtent;
}

// 创建渲染通道
void createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainData.imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass), "Create RenderPass");
    LOG_INFO("RenderPass created");
}

// 创建帧缓冲
void createFramebuffers()
{
    framebuffers.resize(swapchainData.imageViews.size());
    for (size_t i = 0; i < swapchainData.imageViews.size(); i++)
    {
        VkImageView attachments[] = { swapchainData.imageViews[i] };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = swapchainData.extent.width;
        fbInfo.height = swapchainData.extent.height;
        fbInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(logicalDevice, &fbInfo, nullptr, &framebuffers[i]), "Create Framebuffer");
    }
    LOG_INFO("Framebuffers created, count: " << framebuffers.size());
}

// 销毁渲染链路（帧缓冲+渲染通道）
void destroyRenderResources()
{
    for (auto fb : framebuffers)
        vkDestroyFramebuffer(logicalDevice, fb, nullptr);
    framebuffers.clear();
    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

// 创建/重建交换链
void createSwapchain(GLFWwindow* win)
{
    destroyRenderResources();

    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, windowSurface, &surfaceCaps);

    auto formats = getSurfaceFormats();
    auto presentModes = getSurfacePresentModes();
    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    VkExtent2D extent = chooseSwapExtent(surfaceCaps, win);

    uint32_t imageCount = surfaceCaps.minImageCount + 1;
    if (surfaceCaps.maxImageCount > 0 && imageCount > surfaceCaps.maxImageCount)
        imageCount = surfaceCaps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = windowSurface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = surfaceFormat.format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = { graphicsQueueFamilyIndex, presentQueueFamilyIndex };
    if (graphicsQueueFamilyIndex != presentQueueFamilyIndex)
    {
        swapInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapInfo.queueFamilyIndexCount = 2;
        swapInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapInfo.queueFamilyIndexCount = 0;
    }

    swapInfo.preTransform = surfaceCaps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = swapchainData.handle;

    VkSwapchainKHR newSwap;
    VK_CHECK(vkCreateSwapchainKHR(logicalDevice, &swapInfo, nullptr, &newSwap), "Create Swapchain");

    swapchainData.destroy(logicalDevice);
    swapchainData.handle = newSwap;
    swapchainData.imageFormat = surfaceFormat.format;
    swapchainData.extent = extent;

    vkGetSwapchainImagesKHR(logicalDevice, swapchainData.handle, &imageCount, nullptr);
    swapchainData.images.resize(imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapchainData.handle, &imageCount, swapchainData.images.data());

    swapchainData.imageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo ivInfo{};
        ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivInfo.image = swapchainData.images[i];
        ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivInfo.format = swapchainData.imageFormat;
        ivInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivInfo.subresourceRange.baseMipLevel = 0;
        ivInfo.subresourceRange.levelCount = 1;
        ivInfo.subresourceRange.baseArrayLayer = 0;
        ivInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(logicalDevice, &ivInfo, nullptr, &swapchainData.imageViews[i]), "Create ImageView");
    }
    LOG_INFO("Swapchain created, image count: " << imageCount);

    createRenderPass();
    createFramebuffers();
}

// 创建同步信号量、栅栏
void createSyncObjects()
{
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateSemaphore(logicalDevice, &semInfo, nullptr, &imageAvailableSemaphores[i]), "Create image semaphore");
        VK_CHECK(vkCreateSemaphore(logicalDevice, &semInfo, nullptr, &renderFinishedSemaphores[i]), "Create render semaphore");
        VK_CHECK(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]), "Create flight fence");
    }
    LOG_INFO("Sync objects created");
}

// 创建命令池
void createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool), "Create command pool");
    LOG_INFO("Command pool created");
}

// 销毁同步对象
void destroySyncObjects()
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
    }
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
}

// 统一资源释放
void cleanUp(GLFWwindow* window)
{
    if (logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(logicalDevice);
        destroySyncObjects();
        if (commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }
    }

    destroyRenderResources();
    swapchainData.destroy(logicalDevice);
    destroyDebugMessenger();

    if (logicalDevice != VK_NULL_HANDLE)
    {
        vkDestroyDevice(logicalDevice, nullptr);
        logicalDevice = VK_NULL_HANDLE;
    }
    if (windowSurface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, windowSurface, nullptr);
        windowSurface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

int main()
{
    if (!glfwInit())
    {
        LOG_ERR("GLFW init failed");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "BigheroGameEngine Vulkan", nullptr, nullptr);
    if (!window)
    {
        LOG_ERR("Window create failed");
        cleanUp(nullptr);
        return -1;
    }
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    if (!createVulkanInstance()) { cleanUp(window); return -1; }
    if (!createSurface(window)) { cleanUp(window); return -1; }
    if (!pickPhysicalDevice(windowSurface)) { cleanUp(window); return -1; }
    if (!createLogicalDevice()) { cleanUp(window); return -1; }

    createSwapchain(window);
    createCommandPool();
    createSyncObjects();

    LOG_INFO("\n[Success] All base Vulkan objects ready!");

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (framebufferResized)
        {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            if (w <= 0 || h <= 0)
            {
                framebufferResized = false;
                continue;
            }
            createSwapchain(window);
            framebufferResized = false;
        }

        // 此处预留渲染循环绘制逻辑
    }

    cleanUp(window);
    return 0;
}