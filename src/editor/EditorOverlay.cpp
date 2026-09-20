#include "editor/EditorOverlay.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/Swapchain.h"

#include "backends/imgui_impl_vulkan.h"
// 平台后端：桌面=GLFW，Android=NativeActivity（imgui_impl_android）
#ifdef __ANDROID__
#include "backends/imgui_impl_android.h"
#include <android/native_window.h>
#else
#include "backends/imgui_impl_glfw.h"
#endif
#include "imgui.h"

#include <filesystem>
#include <stdexcept>

namespace BigHero
{
namespace
{
void ImGuiCheckVkResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("ImGui Vulkan后端错误: VkResult " << static_cast<int>(result));
        throw std::runtime_error("ImGui Vulkan backend error");
    }
}
} // namespace

void EditorOverlay::Init(const Context& ctx, const Window& window, const Swapchain& swapchain)
{
    if (initialized_)
        return;
    ctx_ = &ctx;

    // ---- UI渲染通道：加载场景通道遗留的颜色（LOAD），绘制UI后转为呈现布局 ----
    VkAttachmentDescription color{};
    color.format = swapchain.Format();
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &color;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(ctx.Device(), &passInfo, nullptr, &overlayPass_), "创建UI渲染通道");

    // ---- ImGui上下文、字体与后端 ----
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // 不落盘布局配置，保持输出目录干净

    // 加载系统中文字体：显式烘焙常用简体字形范围（2500常用字+ASCII），保证图集包含中文。
    // 跨平台回退链：打包字体（随引擎 assets 分发）→ Windows 系统字体
    bool fontLoaded = false;
    const char* kFontCandidates[] = {
        "assets/fonts/wqy-microhei.ttc",                  // 打包的开源中文字体（文泉驿微米黑，随仓库分发）
        "C:/Windows/Fonts/msyh.ttc",                      // Windows：微软雅黑
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", // Linux 发行版常见路径
        "/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc",
    };
    for (const char* path : kFontCandidates)
    {
        if (std::filesystem::exists(path))
        {
            if (io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) !=
                nullptr)
            {
                LOG_INFO("中文字体加载: " << path);
                fontLoaded = true;
                break;
            }
        }
    }
    if (!fontLoaded)
        LOG_WARN("未找到可用中文字体，中文界面可能显示为问号");

#ifdef __ANDROID__
    ImGui_ImplAndroid_Init(static_cast<ANativeWindow*>(window.NativeWindowHandle()));
#else
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window.NativeWindowHandle()), true);
#endif

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = ctx.Instance();
    info.PhysicalDevice = ctx.PhysicalDevice();
    info.Device = ctx.Device();
    info.QueueFamily = ctx.GraphicsFamily();
    info.Queue = ctx.GraphicsQueue();
    info.DescriptorPoolSize = 16; // 让后端自动创建描述符池
    info.MinImageCount = swapchain.ImageCount();
    info.ImageCount = swapchain.ImageCount();
    info.PipelineInfoMain.RenderPass = overlayPass_;
    info.CheckVkResultFn = ImGuiCheckVkResult;
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("ImGui Vulkan后端初始化失败");

    createFramebuffers(swapchain);
    initialized_ = true;
    LOG_INFO("编辑器覆盖层初始化完成");
}

void EditorOverlay::RecreateFramebuffers(const Swapchain& swapchain)
{
    if (!initialized_)
        return;
    destroyFramebuffers();
    createFramebuffers(swapchain);
    ImGui_ImplVulkan_SetMinImageCount(swapchain.ImageCount());
}

void EditorOverlay::Shutdown()
{
    if (!initialized_)
        return;

    ImGui_ImplVulkan_Shutdown();
#ifdef __ANDROID__
    ImGui_ImplAndroid_Shutdown();
#else
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();

    destroyFramebuffers();
    if (overlayPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx_->Device(), overlayPass_, nullptr);
        overlayPass_ = VK_NULL_HANDLE;
    }
    ctx_ = nullptr;
    initialized_ = false;
}

void EditorOverlay::NewFrame()
{
    ImGui_ImplVulkan_NewFrame();
#ifdef __ANDROID__
    ImGui_ImplAndroid_NewFrame();
#else
    ImGui_ImplGlfw_NewFrame();
#endif
    ImGui::NewFrame();
}

void EditorOverlay::Render(VkCommandBuffer cmd, uint32_t imageIndex)
{
    ImGui::Render();

    // 防御：UI 帧缓冲与交换链图像数失配（重建时序异常）时跳过本帧 UI，避免裸下标触发
    // 匿名 vector 断言。画面仅损失一帧 UI 叠加，主场景照常呈现。
    if (imageIndex >= framebuffers_.size() || framebuffers_[imageIndex] == VK_NULL_HANDLE)
    {
        static bool sWarned = false;
        if (!sWarned)
        {
            sWarned = true;
            LOG_WARN("编辑器覆盖层：imageIndex " << imageIndex << " 超出 UI 帧缓冲数量 " << framebuffers_.size()
                                                 << "，本帧 UI 跳过（后续同类告警已抑制）");
        }
        return;
    }

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = overlayPass_;
    passInfo.framebuffer = framebuffers_[imageIndex];
    passInfo.renderArea.offset = {0, 0};
    const ImVec2 uiSize = ImGui::GetMainViewport()->WorkSize;
    passInfo.renderArea.extent = {static_cast<uint32_t>(uiSize.x), static_cast<uint32_t>(uiSize.y)};
    const VkClearValue clear{}; // loadOp=LOAD时清除值被忽略
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
}

void EditorOverlay::createFramebuffers(const Swapchain& swapchain)
{
    // 幂等：先销毁旧帧缓冲再按当前图像数重建。resize 直接截断会静默泄漏被丢弃的句柄，
    // 且旧帧缓冲绑定的是已销毁交换链的图像视图（重建时序异常时的静默失效源）。
    destroyFramebuffers();
    framebuffers_.resize(swapchain.ImageCount());
    for (uint32_t i = 0; i < swapchain.ImageCount(); ++i)
    {
        const VkImageView attachment = swapchain.Views()[i];
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = overlayPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = swapchain.Extent().width;
        fbInfo.height = swapchain.Extent().height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_->Device(), &fbInfo, nullptr, &framebuffers_[i]), "创建UI帧缓冲");
    }
}

void EditorOverlay::destroyFramebuffers()
{
    if (ctx_ == nullptr)
        return;
    for (VkFramebuffer fb : framebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_->Device(), fb, nullptr);
    framebuffers_.clear();
}
} // namespace BigHero
