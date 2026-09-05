#include "render/Context.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "platform/Window.h"

#include <set>
#include <stdexcept>
#include <vector>

namespace BigHero
{
namespace
{
constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char* kDebugUtilsExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
constexpr const char* kSwapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

bool checkValidationLayerSupport()
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    std::set<std::string> layerSet;
    for (const auto& prop : available)
        layerSet.insert(prop.layerName);
    return layerSet.count(kValidationLayer) > 0;
}

std::vector<const char*> getRequiredInstanceExtensions(bool enableDebugExt)
{
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);
    if (enableDebugExt)
        extensions.push_back(kDebugUtilsExt);
    return extensions;
}

bool checkInstanceExtensionSupport(const std::vector<const char*>& required)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    std::set<std::string> availSet;
    for (const auto& ext : available)
        availSet.insert(ext.extensionName);
    for (const char* req : required)
        if (availSet.count(req) == 0)
            return false;
    return true;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice gpu, const std::vector<const char*>& required)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, available.data());

    std::set<std::string> availSet;
    for (const auto& ext : available)
        availSet.insert(ext.extensionName);
    for (const char* req : required)
        if (availSet.count(req) == 0)
            return false;
    return true;
}

bool findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface, uint32_t& outGraphics, uint32_t& outPresent)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues.data());

    bool foundGraphics = false;
    bool foundPresent = false;
    for (uint32_t i = 0; i < count; ++i)
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

        if (foundGraphics && foundPresent)
            break;
    }
    return foundGraphics && foundPresent;
}
} // namespace

Context::Context(Window& window, bool enableValidation)
{
    createInstance(enableValidation);
    if (enableValidation)
        setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();

    // 一次性命令池：初始化期间staging上传等使用
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &transferPool_), "创建一次性命令池");

LOG_INFO("Vulkan上下文初始化完成");
}

Context::Context(bool enableValidation)
    : headless_(true)
{
    printf("Creating headless Context...\n");
    fflush(stdout);
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("GLFW 初始化失败 (headless)");
    printf("GLFW initialized\n");
    fflush(stdout);
    createInstance(enableValidation);
    printf("Instance created\n");
    fflush(stdout);
    if (enableValidation)
        setupDebugMessenger();
    printf("Debug messenger setup\n");
    fflush(stdout);
    // Skip surface creation in headless mode
    pickPhysicalDevice();
    printf("Physical device picked\n");
    fflush(stdout);
    createLogicalDevice();
    printf("createLogicalDevice returned to constructor\n");
    fflush(stdout);
    printf("Logical device created\n");
    fflush(stdout);
    printf("About to create command pool...\n");
    fflush(stdout);
    // 一次性命令池：初始化期间staging上传等使用
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    printf("  Calling vkCreateCommandPool...\n");
    fflush(stdout);
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &transferPool_), "创建一次性命令池");
    printf("  vkCreateCommandPool succeeded\n");
    fflush(stdout);
    printf("Command pool created\n");
    fflush(stdout);

    LOG_INFO("Vulkan上下文初始化完成 (headless)");
}

Context::~Context()
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        if (transferPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, transferPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (!headless_ && surface_ != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_ != VK_NULL_HANDLE)
    {
        const auto destroyFunc = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFunc)
            destroyFunc(instance_, debugMessenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE)
        vkDestroyInstance(instance_, nullptr);
    if (headless_)
        glfwTerminate();
}

void Context::createInstance(bool enableValidation)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BigheroGameEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "BigHeroEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = kApiVersion;

    const bool layersOk = enableValidation && checkValidationLayerSupport();
    if (enableValidation && !layersOk)
        LOG_WARN("未找到校验层 " << kValidationLayer << "，将以无校验模式运行");

    const std::vector<const char*> instanceExts = getRequiredInstanceExtensions(layersOk);
    if (!checkInstanceExtensionSupport(instanceExts))
        throw std::runtime_error("缺少必需的实例扩展");

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(instanceExts.size());
    info.ppEnabledExtensionNames = instanceExts.data();
    if (layersOk)
    {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &kValidationLayer;
    }

    VK_CHECK(vkCreateInstance(&info, nullptr, &instance_), "创建Vulkan实例");
    LOG_INFO("Vulkan实例创建成功" << (layersOk ? "（校验层已启用）" : ""));
}

VKAPI_ATTR VkBool32 VKAPI_CALL Context::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
{
    (void)type;
    (void)userData;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("[Vulkan校验] " << data->pMessage);
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN("[Vulkan校验] " << data->pMessage);
    else
        LOG_INFO("[Vulkan校验] " << data->pMessage);
    return VK_FALSE;
}

void Context::setupDebugMessenger()
{
    const auto createFunc = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (createFunc == nullptr)
    {
        LOG_WARN("无法加载 vkCreateDebugUtilsMessengerEXT，跳过调试回调");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;

    VK_CHECK(createFunc(instance_, &info, nullptr, &debugMessenger_), "创建调试回调");
}

void Context::createSurface(Window& window)
{
    VK_CHECK(glfwCreateWindowSurface(instance_, window.Get(), nullptr, &surface_), "创建窗口表面");
    LOG_INFO("窗口表面创建成功");
}

void Context::pickPhysicalDevice()
{
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(instance_, &gpuCount, nullptr);
    if (gpuCount == 0)
        throw std::runtime_error("未检测到支持Vulkan的GPU");
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    vkEnumeratePhysicalDevices(instance_, &gpuCount, gpus.data());

    // 打分选择：独显 > 集显 > 其他
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    uint32_t bestGraphics = UINT32_MAX;
    uint32_t bestPresent = UINT32_MAX;

    for (VkPhysicalDevice gpu : gpus)
    {
        printf("  Checking GPU...\n");
        fflush(stdout);
        uint32_t gfx = UINT32_MAX;
        uint32_t present = UINT32_MAX;
        
        if (headless_)
        {
            // Headless mode: only need graphics queue
            printf("  Headless mode: finding graphics queue...\n");
            fflush(stdout);
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
            printf("  Queue family count: %u\n", count);
            fflush(stdout);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues.data());
            for (uint32_t i = 0; i < count; ++i)
            {
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    gfx = i;
                    present = i;  // Same queue for headless
                    break;
                }
            }
            if (gfx == UINT32_MAX)
            {
                printf("  No graphics queue found, skipping GPU\n");
                fflush(stdout);
                continue;
            }
        }
        else
        {
            // Normal mode: need graphics + present queues with swapchain
            if (!findQueueFamilies(gpu, surface_, gfx, present))
                continue;
            if (!checkDeviceExtensionSupport(gpu, {kSwapchainExt}))
                continue;
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);

        int score = 10;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score = 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score = 100;

        if (score > bestScore)
        {
            bestScore = score;
            best = gpu;
            bestGraphics = gfx;
            bestPresent = present;
        }
    }

    if (best == VK_NULL_HANDLE)
        throw std::runtime_error("没有满足要求的GPU");

    physicalDevice_ = best;
    graphicsFamily_ = bestGraphics;
    presentFamily_ = bestPresent;
    vkGetPhysicalDeviceFeatures(physicalDevice_, &features_);
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);

    LOG_INFO("选择GPU: " << properties_.deviceName
                         << (graphicsFamily_ == presentFamily_ ? "（图形/呈现共用队列族）" : "（图形/呈现分队列族）"));
}

void Context::createLogicalDevice()
{
    printf("  Creating logical device...\n");
    fflush(stdout);
    // Use static arrays to avoid stack issues
    static uint32_t s_uniqueFamiliesArray[2];
    static uint32_t s_uniqueFamiliesCount = 0;
    s_uniqueFamiliesCount = 0;
    s_uniqueFamiliesArray[s_uniqueFamiliesCount++] = graphicsFamily_;
    if (presentFamily_ != graphicsFamily_)
        s_uniqueFamiliesArray[s_uniqueFamiliesCount++] = presentFamily_;

    static VkDeviceQueueCreateInfo s_queueInfos[2];
    float priority = 1.0f;
    uint32_t queueCount = (presentFamily_ != graphicsFamily_ ? 2 : 1);
    for (uint32_t i = 0; i < queueCount; ++i)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = s_uniqueFamiliesArray[i];
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        s_queueInfos[i] = queueInfo;
    }

    // 按设备支持情况启用特性
    VkPhysicalDeviceFeatures enabledFeatures{};
    if (features_.samplerAnisotropy)
        enabledFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo devInfo{};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = (presentFamily_ != graphicsFamily_ ? 2 : 1);
    devInfo.pQueueCreateInfos = s_queueInfos;

    if (headless_)
    {
        // Headless: no swapchain extension needed
        devInfo.enabledExtensionCount = 0;
        devInfo.ppEnabledExtensionNames = nullptr;
    }
    else
    {
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = &kSwapchainExt;
    }
    devInfo.pEnabledFeatures = &enabledFeatures;

    printf("  Calling vkCreateDevice...\n");
    fflush(stdout);
    VK_CHECK(vkCreateDevice(physicalDevice_, &devInfo, nullptr, &device_), "创建逻辑设备");
    printf("  vkCreateDevice succeeded\n");
    fflush(stdout);
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
    printf("  Got queues\n");
    fflush(stdout);
    LOG_INFO("逻辑设备创建成功，队列已获取");
    printf("createLogicalDevice returning\n");
    fflush(stdout);
}

void Context::SubmitOneTime(const std::function<void(VkCommandBuffer)>& record) const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = transferPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, &cmd), "分配一次性命令缓冲");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "开始录制一次性命令");

    record(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd), "结束录制一次性命令");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE), "提交一次性命令");
    VK_CHECK(vkQueueWaitIdle(graphicsQueue_), "等待一次性命令完成");

    vkFreeCommandBuffers(device_, transferPool_, 1, &cmd);
}
} // namespace BigHero

