#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <optional>
#include <set>
#include <cstring>
#include <string>
#include <algorithm>

// 调试宏，发布版本注释/取消定义关闭所有调试组件
#define DEBUG

// 全局常量区
constexpr uint32_t WINDOW_WIDTH = 1280;
constexpr uint32_t WINDOW_HEIGHT = 720;
constexpr uint32_t VULKAN_API_VER = VK_API_VERSION_1_2;
constexpr const char* VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";
constexpr const char* SWAPCHAIN_DEV_EXT = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
constexpr const char* DEBUG_UTILS_EXT = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

#ifdef DEBUG
const std::vector<const char*> VALIDATION_LAYERS = { VALIDATION_LAYER_NAME };
#else
const std::vector<const char*> VALIDATION_LAYERS = {};
#endif

const std::vector<const char*> DEVICE_EXTENSIONS = { SWAPCHAIN_DEV_EXT };

// Swapchain 数据统一封装结构体，减少全局变量散乱
struct SwapchainData
{
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat imageFormat;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
} swapchainData;

// Vulkan基础上下文全局句柄（初学简化，后期封装类）
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

// ===================== 工具函数：错误码转字符串 =====================
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
    default: return "UNKNOWN_VK_ERROR";
    }
}

// ===================== Debug 调试回调 =====================
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[Vulkan Debug] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

// 创建调试信使
bool setupDebugMessenger()
{
    if (VALIDATION_LAYERS.empty()) return true;

    auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT)
    {
        std::cerr << "Failed load debug utils function\n";
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

    VkResult res = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
    if (res != VK_SUCCESS)
    {
        std::cerr << "Create debug messenger fail: " << VkResultToString(res) << "\n";
        return false;
    }
    return true;
}

void destroyDebugMessenger()
{
    if (!debugMessenger) return;
    auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (vkDestroyDebugUtilsMessengerEXT)
    {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
}

// ===================== 层/扩展校验 =====================
bool checkValidationLayerSupport()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : VALIDATION_LAYERS)
    {
        bool found = false;
        for (const auto& prop : availableLayers)
        {
            if (strcmp(prop.layerName, layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cout << "Warning: Missing validation layer: " << layerName << "\n";
            return false;
        }
    }
    return true;
}

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
            std::cerr << "Missing required instance extension: " << req << "\n";
            return false;
        }
    }
    return true;
}

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

std::vector<const char*> getRequiredInstanceExtensions(bool enableDebug)
{
    uint32_t glfwExtCnt = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCnt);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCnt);

    if (enableDebug)
        extensions.push_back(DEBUG_UTILS_EXT);
    return extensions;
}

// ===================== 窗口回调 =====================
void framebufferResizeCallback(GLFWwindow* win, int width, int height)
{
    framebufferResized = true;
}

// ===================== 队列族一次性查询（合并两个旧函数，消除重复API） =====================
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

// ===================== 物理设备筛选 =====================
bool pickPhysicalDevice(VkSurfaceKHR surface)
{
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());

    // 优先离散独显，无独显兜底集成显卡
    for (VkPhysicalDevice gpu : gpus)
    {
        VkPhysicalDeviceProperties prop{};
        vkGetPhysicalDeviceProperties(gpu, &prop);

        uint32_t gIdx, pIdx;
        bool queueOk = findQueueFamilies(gpu, surface, gIdx, pIdx);
        bool extOk = checkDeviceExtensionSupport(gpu, DEVICE_EXTENSIONS);

        if (queueOk && extOk)
        {
            // 优先独显
            if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                physicalDevice = gpu;
                graphicsQueueFamilyIndex = gIdx;
                presentQueueFamilyIndex = pIdx;
                break;
            }
            // 暂存集显兜底
            if (!physicalDevice)
            {
                physicalDevice = gpu;
                graphicsQueueFamilyIndex = gIdx;
                presentQueueFamilyIndex = pIdx;
            }
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
    {
        std::cerr << "No compatible GPU found!\n";
        return false;
    }

    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(physicalDevice, &prop);
    std::cout << "Selected GPU: " << prop.deviceName << "\nGraphics queue idx: " << graphicsQueueFamilyIndex
        << "\nPresent queue idx: " << presentQueueFamilyIndex << "\n";
    return true;
}

// ===================== Vulkan实例创建 =====================
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
    if (!checkInstanceExtensionSupport(instanceExts))
    {
        std::cerr << "Instance extension check failed\n";
        return false;
    }

    info.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    info.ppEnabledExtensionNames = instanceExts.data();

    if (layersOk)
    {
        info.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }
    else
    {
        std::cout << "Warning: Validation layers missing, run without debug layer\n";
        info.enabledLayerCount = 0;
        info.ppEnabledLayerNames = nullptr;
    }

    VkResult res = vkCreateInstance(&info, nullptr, &instance);
    if (res != VK_SUCCESS)
    {
        std::cerr << "Instance create failed: " << VkResultToString(res) << "\n";
        return false;
    }
    std::cout << "Vulkan instance created\n";

    if (layersOk)
        setupDebugMessenger();
    return true;
}

// ===================== 逻辑设备创建（修复各向异性过滤硬件兼容） =====================
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

    // 先查询硬件支持再开启各向异性过滤
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

    VkResult res = vkCreateDevice(physicalDevice, &devInfo, nullptr, &logicalDevice);
    if (res != VK_SUCCESS)
    {
        std::cerr << "Logical device create failed: " << VkResultToString(res) << "\n";
        return false;
    }

    vkGetDeviceQueue(logicalDevice, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(logicalDevice, presentQueueFamilyIndex, 0, &presentQueue);
    std::cout << "Logical device created, queues acquired\n";
    return true;
}

// ===================== Window Surface =====================
bool createSurface(GLFWwindow* win)
{
    VkResult res = glfwCreateWindowSurface(instance, win, nullptr, &windowSurface);
    if (res != VK_SUCCESS)
    {
        std::cerr << "Surface create failed: " << VkResultToString(res) << "\n";
        return false;
    }
    std::cout << "Window surface created\n";
    return true;
}

// ===================== Swapchain 工具函数 =====================
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
    VkExtent2D actualExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    actualExtent.width = std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actualExtent;
}

// 创建/重建交换链
void createSwapchain(GLFWwindow* win)
{
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
    VkResult res = vkCreateSwapchainKHR(logicalDevice, &swapInfo, nullptr, &newSwap);
    if (res != VK_SUCCESS)
    {
        std::cerr << "Create swapchain failed: " << VkResultToString(res) << "\n";
        return;
    }

    // 销毁旧交换链资源
    for (auto iv : swapchainData.imageViews)
        vkDestroyImageView(logicalDevice, iv, nullptr);
    if (swapchainData.handle != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(logicalDevice, swapchainData.handle, nullptr);

    // 更新全局交换链数据
    swapchainData.handle = newSwap;
    swapchainData.imageFormat = surfaceFormat.format;
    swapchainData.extent = extent;

    // 获取交换链图像
    vkGetSwapchainImagesKHR(logicalDevice, swapchainData.handle, &imageCount, nullptr);
    swapchainData.images.resize(imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapchainData.handle, &imageCount, swapchainData.images.data());

    // 创建图像视图
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
        vkCreateImageView(logicalDevice, &ivInfo, nullptr, &swapchainData.imageViews[i]);
    }
    std::cout << "Swapchain created, image count: " << imageCount << "\n";
}

// ===================== 统一资源释放 =====================
void cleanUp(GLFWwindow* window)
{
    // 销毁交换链
    for (auto iv : swapchainData.imageViews)
        vkDestroyImageView(logicalDevice, iv, nullptr);
    if (swapchainData.handle != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(logicalDevice, swapchainData.handle, nullptr);
        swapchainData.handle = VK_NULL_HANDLE;
    }

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
    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
}

// ===================== 主函数 =====================
int main()
{
    if (!glfwInit())
    {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "BigheroGameEngine Vulkan", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Window create failed\n";
        cleanUp(nullptr);
        return -1;
    }
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    if (!createVulkanInstance()) { cleanUp(window); return -1; }
    if (!createSurface(window)) { cleanUp(window); return -1; }
    if (!pickPhysicalDevice(windowSurface)) { cleanUp(window); return -1; }
    if (!createLogicalDevice()) { cleanUp(window); return -1; }

    createSwapchain(window);
    std::cout << "All base Vulkan objects ready!\n";

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (framebufferResized)
        {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            if (w == 0 || h == 0)
            {
                framebufferResized = false;
                continue;
            }
            createSwapchain(window);
            framebufferResized = false;
        }
    }

    cleanUp(window);
    return 0;
}