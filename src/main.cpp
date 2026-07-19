#define VK_ENABLE_BETA_EXTENSIONS
#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#define SKIP_VK_MISS_FEATURES 1
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <set>
#include <cstring>
#include <string>
#include <fstream>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <array>
#include <cstddef>
#include "render/shader_loader.h"
#include "render/ubo_structs.h"
#include "render/ubo_buffer.h"
#include "render/descriptor_set.h"
#include "render/pipeline.h"
#include <optional>

// 调试宏，发布版本注释关闭校验层与调试回调
#define DEBUG

// ===================== 全局常量 =====================
static constexpr uint32_t WINDOW_WIDTH = 1280;
static constexpr uint32_t WINDOW_HEIGHT = 720;
static constexpr uint32_t VULKAN_API_VER = VK_API_VERSION_1_3;
static constexpr const char* VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";
static constexpr const char* SWAPCHAIN_DEV_EXT = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
static constexpr const char* DEBUG_UTILS_EXT = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
static constexpr const char* SHADER_VERT_PATH = "shaders/vert.spv";
static constexpr const char* SHADER_FRAG_PATH = "shaders/frag.spv";

// ===================== 全部函数前置声明 =====================
const char* VkResultToString(VkResult res);
void cleanUp(GLFWwindow* window);
void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags targetProperties);
void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps, VkBuffer& outBuffer, VkDeviceMemory& outMem);
void copyBufferData(VkDeviceMemory bufferMem, const void* srcData, VkDeviceSize size);
std::vector<uint32_t> readShaderFile(const std::string& filePath);
bool setupDebugMessenger();
void destroyDebugMessenger();
void destroySyncObjects();
bool checkValidationLayerSupport();
bool checkInstanceExtensionSupport(const std::vector<const char*>& required);
bool checkDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& requiredExtensions);
std::vector<const char*> getRequiredInstanceExtensions(bool enableDebugExt);
void framebufferResizeCallback(GLFWwindow* win, int width, int height);
bool findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface, uint32_t& outGraphics, uint32_t& outPresent);
bool pickPhysicalDevice(VkSurfaceKHR surface);
bool createVulkanInstance();
bool createLogicalDevice();
bool createSurface(GLFWwindow* win);
std::vector<VkSurfaceFormatKHR> getSurfaceFormats();
std::vector<VkPresentModeKHR> getSurfacePresentModes();
VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* win);
void createRenderPass();
void createFramebuffers();
void destroyRenderResources();
void destroyGraphicsPipelineResources();
void createSwapchain(GLFWwindow* win);
void createSyncObjects();
void createCommandPool();
void createCommandBuffers();
void createVertexBuffer();
void createGraphicsPipeline();
void createUboAndDescriptorResources();
void updateSceneUboData();

// 顶点结构：坐标+RGB颜色
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;

    constexpr Vertex(glm::vec3 p, glm::vec3 n, glm::vec2 u, glm::vec3 c)
        : pos(p), normal(n), uv(u), color(c) {
    }

    static VkVertexInputBindingDescription getBindingDesc()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttrDesc()
    {
        std::vector<VkVertexInputAttributeDescription> attr(4);

        // location 0 : vec3 pos
        attr[0].binding = 0;
        attr[0].location = 0;
        attr[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attr[0].offset = offsetof(Vertex, pos);

        // location 1 : vec3 normal
        attr[1].binding = 0;
        attr[1].location = 1;
        attr[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attr[1].offset = offsetof(Vertex, normal);

        // location 2 : vec2 uv
        attr[2].binding = 0;
        attr[2].location = 2;
        attr[2].format = VK_FORMAT_R32G32_SFLOAT;
        attr[2].offset = offsetof(Vertex, uv);

        // location 3 : vec3 color
        attr[3].binding = 0;
        attr[3].location = 3;
        attr[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attr[3].offset = offsetof(Vertex, color);

        return attr;
    }
};

// 临时3D平面测试面片（替代旧2D三角形，适配3D着色器）
static constexpr std::array<Vertex, 6> planeVertices = {
    // 三角形 1
    Vertex({-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f,1.0f,1.0f}),
    Vertex({ 1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f,1.0f,1.0f}),
    Vertex({ 1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f,1.0f,1.0f}),
    // 三角形 2
    Vertex({-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f,1.0f,1.0f}),
    Vertex({ 1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f,1.0f,1.0f}),
    Vertex({-1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f,1.0f,1.0f}),
};

// 全局Vulkan句柄
static VkPipeline graphicsPipeline = VK_NULL_HANDLE;
static VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
static VkBuffer vertexBuffer = VK_NULL_HANDLE;
static VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

// ---------------------- 渲染模块全局资源（UBO/描述符） ----------------------
// 简写命名空间 BigHero::Render，简化后续调用
namespace BR = BigHero::Render;

// 描述符管理器：维护set0相机UBO、set1光照UBO的布局/池/集合
static BR::DescriptorManager descManager;

// 相机 Uniform 缓冲（延迟初始化，逻辑设备创建后再构造）
static std::optional<BR::UboBuffer<BR::CameraUBO>> cameraUbo;
// 光照 Uniform 缓冲（延迟初始化，逻辑设备创建后再构造）
static std::optional<BR::UboBuffer<BR::LightUBO>> lightUbo;

// 封装版3D图形管线，与原有基础三角形管线互不冲突，可按需切换使用
static std::optional<BR::GraphicsPipeline> sceneGraphicsPipeline;

#ifdef DEBUG
static const std::vector<const char*> VALIDATION_LAYERS = { VALIDATION_LAYER_NAME };
#else
static const std::vector<const char*> VALIDATION_LAYERS = {};
#endif

static const std::vector<const char*> DEVICE_EXTENSIONS = { SWAPCHAIN_DEV_EXT };

// ===================== 日志分级宏 =====================
#define LOG_INFO(msg)  std::cout << "[INFO] " << msg << "\n"
#define LOG_WARN(msg)  std::cout << "[WARN] " << msg << "\n"
#define LOG_ERR(msg)   std::cerr << "[ERROR] " << msg << "\n"

// ===================== 通用校验宏 =====================
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
        cleanUp(nullptr); \
        exit(EXIT_FAILURE); \
    } \
} while(0)
#endif

// ===================== 交换链封装 =====================
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
        {
            if (iv != VK_NULL_HANDLE)
                vkDestroyImageView(dev, iv, nullptr);
        }
        imageViews.clear();
        images.clear();

        if (handle != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(dev, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    }
} swapchainData{};

static VkRenderPass renderPass = VK_NULL_HANDLE;
static std::vector<VkFramebuffer> framebuffers;

static VkCommandPool commandPool = VK_NULL_HANDLE;
static std::vector<VkCommandBuffer> commandBuffers;
static std::vector<VkSemaphore> imageAvailableSemaphores;
static std::vector<VkSemaphore> renderFinishedSemaphores;
static std::vector<VkFence> inFlightFences;
static uint32_t currentFrame = 0;

static VkInstance instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice logicalDevice = VK_NULL_HANDLE;
static VkSurfaceKHR windowSurface = VK_NULL_HANDLE;
static VkQueue graphicsQueue = VK_NULL_HANDLE;
static VkQueue presentQueue = VK_NULL_HANDLE;

static uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
static uint32_t presentQueueFamilyIndex = UINT32_MAX;
static bool framebufferResized = false;

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

uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags targetProperties)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        const uint32_t typeBit = 1U << i;
        if ((typeFilter & typeBit) && (memProps.memoryTypes[i].propertyFlags & targetProperties) == targetProperties)
        {
            return i;
        }
    }
    LOG_ERR("No matching memory type found");
    return UINT32_MAX;
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
    VkBuffer& outBuffer, VkDeviceMemory& outMem)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(logicalDevice, &bufInfo, nullptr, &outBuffer), "Buffer create failed");

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(logicalDevice, outBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, memProps);

    VK_CHECK(vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &outMem), "Buffer memory allocate failed");
    VK_CHECK(vkBindBufferMemory(logicalDevice, outBuffer, outMem, 0), "Bind buffer memory failed");
}

void copyBufferData(VkDeviceMemory bufferMem, const void* srcData, VkDeviceSize size)
{
    if (srcData == nullptr || size == 0)
    {
        LOG_ERR("copyBufferData: invalid source data or zero size");
        return;
    }

    void* dataPtr = nullptr;
    VK_CHECK(vkMapMemory(logicalDevice, bufferMem, 0, size, 0, &dataPtr), "Map buffer memory");

    std::memcpy(dataPtr, srcData, size);
    vkUnmapMemory(logicalDevice, bufferMem);
}

std::vector<uint32_t> readShaderFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERR("Failed to open shader file: " << filePath);
        return {};
    }

    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0 || fileSize % sizeof(uint32_t) != 0)
    {
        LOG_ERR("Shader file empty or not 4-byte aligned: " << filePath);
        file.close();
        return {};
    }

    std::vector<uint32_t> buffer(static_cast<size_t>(fileSize / sizeof(uint32_t)));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    if (static_cast<std::streamsize>(file.gcount()) != fileSize)
    {
        LOG_ERR("Failed to fully read shader file: " << filePath);
        file.close();
        return {};
    }
    file.close();
    return buffer;
}

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

std::vector<const char*> getRequiredInstanceExtensions(bool enableDebugExt)
{
    uint32_t glfwExtCnt = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCnt);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCnt);
    if (enableDebugExt) extensions.push_back(DEBUG_UTILS_EXT);
    return extensions;
}

void framebufferResizeCallback(GLFWwindow* win, int width, int height)
{
    (void)win; (void)width; (void)height;
    framebufferResized = true;
}

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
    descManager.Init(logicalDevice);
    return true;
}

bool createSurface(GLFWwindow* win)
{
    VK_CHECK(glfwCreateWindowSurface(instance, win, nullptr, &windowSurface), "Create Window Surface");
    LOG_INFO("Window surface created");
    return true;
}

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

void destroyRenderResources()
{
    for (auto fb : framebuffers)
    {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(logicalDevice, fb, nullptr);
    }
    framebuffers.clear();
    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void destroyGraphicsPipelineResources()
{
    if (logicalDevice == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(logicalDevice);

    if (vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(logicalDevice, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }

    if (graphicsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
        graphicsPipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(logicalDevice, pipelineLayout, VK_NULL_HANDLE);
        pipelineLayout = VK_NULL_HANDLE;
    }

    LOG_INFO("Graphics pipeline & vertex buffer resources destroyed");
}

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

void createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool), "Create command pool");
    LOG_INFO("Command pool created");
}

void createCommandBuffers()
{
    if (logicalDevice == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE)
    {
        LOG_ERR("Device or command pool not initialized before alloc cmd buffers");
        return;
    }

    if (!commandBuffers.empty())
    {
        vkFreeCommandBuffers(logicalDevice, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        commandBuffers.clear();
    }

    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()), "Allocate frame command buffers");
    LOG_INFO("Command buffers allocated, frame count: " << commandBuffers.size());
}

void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Begin cmd buffer");

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapchainData.extent;
    VkClearValue clearColor = { {{0.0f, 0.0f, 1.0f}} };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    // ====== 新增：绑定set0、set1两套描述符集 ======
    VkDescriptorSet sets[] = {
    descManager.GetSets()[0],
    descManager.GetSets()[1]
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0, 2, sets,
        0, nullptr);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainData.extent.width);
    viewport.height = static_cast<float>(swapchainData.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchainData.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, static_cast<uint32_t>(planeVertices.size()), 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd), "End cmd buffer");
}

void createVertexBuffer()
{
    const VkDeviceSize bufferSize = planeVertices.size() * sizeof(Vertex);
    if (bufferSize == 0)
    {
        LOG_ERR("Vertex data is empty, skip vertex buffer creation");
        return;
    }
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);
    copyBufferData(stagingBufferMemory, planeVertices.data(), bufferSize);
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer, vertexBufferMemory);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer copyCmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &allocInfo, &copyCmd), "Allocate copy command buffer");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(copyCmd, &beginInfo), "Begin copy command buffer");
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(copyCmd, stagingBuffer, vertexBuffer, 1, &copyRegion);
    VK_CHECK(vkEndCommandBuffer(copyCmd), "End copy command buffer");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &copyCmd;
    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Submit buffer copy command");
    VK_CHECK(vkQueueWaitIdle(graphicsQueue), "Wait copy queue finish");
    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &copyCmd);
    if (stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
    }
    if (stagingBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);
        stagingBufferMemory = VK_NULL_HANDLE;
    }
    LOG_INFO("Vertex buffer created, size: " << bufferSize);
}

// 更新相机与光照UBO数据，每一帧可重复调用
void updateSceneUboData()
{
    if (!cameraUbo.has_value() || !lightUbo.has_value())
    {
        LOG_WARN("UBO resources not initialized, skip UBO update");
        return;
    }

    BR::CameraUBO camData{};
    camData.model = glm::mat4(1.0f);
    // 基础相机视角：相机位置(0,3,10)，看向原点
    camData.view = glm::lookAt(glm::vec3(0.f, 3.f, 10.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    // 透视投影 60° FOV，动态匹配窗口长宽比
    camData.proj = glm::perspective(glm::radians(60.f),
        static_cast<float>(swapchainData.extent.width) / static_cast<float>(swapchainData.extent.height),
        0.1f, 1000.f);
    cameraUbo->Update(camData);

    BR::LightUBO lightData{};
    lightData.lightDir = glm::vec3(0.5f, -1.f, -0.3f);
    lightData.lightColor = glm::vec3(1.f, 0.95f, 0.8f);
    lightData.cameraPos = glm::vec3(0.f, 3.f, 10.f);
    lightData.ambientFactor = 0.12f;
    lightData.specPower = 32.f;
    lightData.specStrength = 1.f;
    lightUbo->Update(lightData);

    // 将缓冲绑定至对应描述符set
    descManager.UpdateSet(0, 0, cameraUbo.value());
    descManager.UpdateSet(1, 0, lightUbo.value());
}

// 初始化UBO缓冲、描述符布局/池/集合
void createUboAndDescriptorResources()
{
    // 初始化描述符管理器，传入已创建的逻辑设备
    descManager.Init(logicalDevice);
    // 构造两套UBO缓冲（CPU可映射主机连贯内存）
    cameraUbo.emplace(logicalDevice, physicalDevice, graphicsQueueFamilyIndex);
    lightUbo.emplace(logicalDevice, physicalDevice, graphicsQueueFamilyIndex);

    // 分配一组描述符集：set0相机UBO、set1光照UBO
    descManager.AllocateSet();

    // 填充初始相机、光照数据并更新描述符绑定
    updateSceneUboData();
    LOG_INFO("UBO & descriptor resources initialized");
}

void createGraphicsPipeline()
{
#if SKIP_VK_MISS_FEATURES
    constexpr VkPrimitiveTopology PRIM_TOPO = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
#else
    constexpr VkPrimitiveTopology PRIM_TOPO = VK_PRIMITIVE_TRIANGLE_LIST;
#endif
    constexpr VkSampleCountFlagBits MSAA_SAMPLES = VK_SAMPLE_COUNT_1_BIT;
    constexpr VkCullModeFlags CULL_MODE = VK_CULL_MODE_BACK_BIT;
    constexpr VkFrontFace FRONT_WINDING = VK_FRONT_FACE_CLOCKWISE;
    constexpr float LINE_WIDTH = 1.0f;
    constexpr uint32_t SUBPASS_INDEX = 0;

    auto vertShaderCode = readShaderFile(SHADER_VERT_PATH);
    auto fragShaderCode = readShaderFile(SHADER_FRAG_PATH);
    if (vertShaderCode.empty() || fragShaderCode.empty())
    {
        LOG_ERR("Vertex or fragment shader file missing");
        return;
    }

    VkShaderModuleCreateInfo vertShaderInfo{};
    vertShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertShaderInfo.codeSize = vertShaderCode.size() * sizeof(uint32_t);
    vertShaderInfo.pCode = vertShaderCode.data();
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkResult vertRes = vkCreateShaderModule(logicalDevice, &vertShaderInfo, nullptr, &vertShaderModule);
    if (vertRes != VK_SUCCESS)
    {
        LOG_ERR("Create vertex shader module failed: " + std::string(VkResultToString(vertRes)));
        return;
    }

    VkShaderModuleCreateInfo fragShaderInfo{};
    fragShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragShaderInfo.codeSize = fragShaderCode.size() * sizeof(uint32_t);
    fragShaderInfo.pCode = fragShaderCode.data();
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    VkResult fragRes = vkCreateShaderModule(logicalDevice, &fragShaderInfo, nullptr, &fragShaderModule);
    if (fragRes != VK_SUCCESS)
    {
        LOG_ERR("Create fragment shader module failed: " + std::string(VkResultToString(fragRes)));
        if (vertShaderModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
        return;
    }

    auto destroyShaderModules = [&]() noexcept
        {
            if (vertShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
            }
            if (fragShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
            }
        };

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShaderModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShaderModule;
    shaderStages[1].pName = "main";

    auto bindingDesc = Vertex::getBindingDesc();
    auto attrDescs = Vertex::getAttrDesc();
    const uint32_t attrCount = static_cast<uint32_t>(attrDescs.size());

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = attrCount;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = PRIM_TOPO;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = LINE_WIDTH;
    rasterizer.cullMode = CULL_MODE;
    rasterizer.frontFace = FRONT_WINDING;
    rasterizer.depthBiasEnable = VK_FALSE;

#if !SKIP_VK_MISS_FEATURES
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = MSAA_SAMPLES;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable = VK_FALSE;
#endif

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    const uint32_t dynamicStateCount = 2u;
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = dynamicStateCount;
    dynamicState.pDynamicStates = dynamicStates;

    vkDeviceWaitIdle(logicalDevice);
    destroyGraphicsPipelineResources();

    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(logicalDevice, pipelineLayout, VK_NULL_HANDLE);
        pipelineLayout = VK_NULL_HANDLE;
    }

    // 【核心修复】绑定两套描述符布局 set0(camera), set1(light)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[] = {
        descManager.layoutCamera,
        descManager.layoutLight
    };
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;

    VkResult layoutRes = vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (layoutRes != VK_SUCCESS)
    {
        LOG_ERR("Create pipeline layout failed: " + std::string(VkResultToString(layoutRes)));
        destroyShaderModules();
        return;
    }

    if (renderPass == VK_NULL_HANDLE)
    {
        LOG_ERR("RenderPass handle is null, cannot create graphics pipeline");
        destroyShaderModules();
        return;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
#if !SKIP_VK_MISS_FEATURES
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
#else
    pipelineInfo.pMultisampleState = nullptr;
    pipelineInfo.pDepthStencilState = nullptr;
#endif
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = SUBPASS_INDEX;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkResult pipeRes = vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
    if (pipeRes != VK_SUCCESS)
    {
        LOG_ERR("Create graphics pipeline failed: " + std::string(VkResultToString(pipeRes)));
        destroyShaderModules();
        return;
    }

    destroyShaderModules();
    LOG_INFO("Graphics pipeline created successfully");
}
void cleanUp(GLFWwindow* window)
{
    vkDeviceWaitIdle(logicalDevice);

    destroySyncObjects();
    vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
    destroyGraphicsPipelineResources();
    destroyRenderResources();
    swapchainData.destroy(logicalDevice);

    if (windowSurface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance, windowSurface, nullptr);

    if (logicalDevice != VK_NULL_HANDLE)
        vkDestroyDevice(logicalDevice, nullptr);

    destroyDebugMessenger();
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, nullptr);

    if (window)
        glfwDestroyWindow(window);
    glfwTerminate();
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Vulkan Render", nullptr, nullptr);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    if (!createVulkanInstance())
    {
        cleanUp(window);
        return EXIT_FAILURE;
    }
    if (!createSurface(window))
    {
        cleanUp(window);
        return EXIT_FAILURE;
    }
    if (!pickPhysicalDevice(windowSurface))
    {
        cleanUp(window);
        return EXIT_FAILURE;
    }
    if (!createLogicalDevice())
    {
        cleanUp(window);
        return EXIT_FAILURE;
    }

    createSwapchain(window);
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createVertexBuffer();
    createUboAndDescriptorResources();
    createGraphicsPipeline();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        VkFence inFlightFence = inFlightFences[currentFrame];
        VK_CHECK(vkWaitForFences(logicalDevice, 1, &inFlightFence, VK_TRUE, UINT64_MAX), "Wait fence");

        uint32_t imageIndex;
        VkResult acquireRes = vkAcquireNextImageKHR(logicalDevice, swapchainData.handle, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR || acquireRes == VK_SUBOPTIMAL_KHR || framebufferResized)
        {
            framebufferResized = false;
            createSwapchain(window);
            continue;
        }
        VK_CHECK(acquireRes, "Acquire next image");

        VK_CHECK(vkResetFences(logicalDevice, 1, &inFlightFence), "Reset fence");
        VK_CHECK(vkResetCommandBuffer(commandBuffers[currentFrame], 0), "Reset cmd buffer");
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence), "Queue submit");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainData.handle;
        presentInfo.pImageIndices = &imageIndex;

        VkResult presentRes = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR || framebufferResized)
        {
            framebufferResized = false;
            createSwapchain(window);
        }
        else
        {
            VK_CHECK(presentRes, "Queue present");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    cleanUp(window);
    return EXIT_SUCCESS;
}