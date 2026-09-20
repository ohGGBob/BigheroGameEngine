#pragma once
// 后处理处理器：Bloom（亮部提取 → 高斯模糊 → 合成）+ ACES 色调映射。
//
// 架构：
// - 场景由 Renderer 渲染到离屏缓冲（PostProcessor 提供颜色/解析图，Renderer 提供深度图）
// - PostProcessor 管理后处理中间缓冲、渲染通道、管线、描述符集
// - RecordBloom() 录制完整后处理链并输出到交换链
// - 复用现有场景渲染通道，避免重建场景管线

#include "render/Image.h"
#include "render/pipeline.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

namespace BigHero
{
class Context;
}

namespace BigHero::Render
{
class PostProcessor
{
  public:
    PostProcessor() = default;
    ~PostProcessor() { Destroy(); }

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void Init(const Context& ctx, VkExtent2D extent, VkFormat colorFormat, VkSampleCountFlagBits samples,
              const std::vector<VkImageView>& swapchainViews);

    void Destroy();

    // 窗口尺寸变化时重建所有尺寸相关资源
    void Recreate(const Context& ctx, VkExtent2D extent, const std::vector<VkImageView>& swapchainViews);

    // 录制完整 Bloom 后处理链：深度线性化 → 景深 → 亮部提取 → 水平模糊 → 垂直模糊 → 合成到交换链
    void RecordBloom(VkCommandBuffer cmd, uint32_t swapchainIndex, VkExtent2D extent, float camNear, float camFar);

    [[nodiscard]] bool IsValid() const noexcept { return initialized_; }

    // 离屏缓冲视图（供 Renderer 创建离屏帧缓冲）
    [[nodiscard]] VkImageView OffscreenMsaaColorView() const noexcept { return offscreenMsaaColor_.View(); }
    [[nodiscard]] VkImageView OffscreenResolveView() const noexcept { return offscreenResolve_.View(); }
    // 渲染图：离屏场景颜色图像（MSAA / 解析）
    [[nodiscard]] VkImage OffscreenMsaaColorImage() const noexcept { return offscreenMsaaColor_.Get(); }
    [[nodiscard]] VkImage OffscreenResolveImage() const noexcept { return offscreenResolve_.Get(); }
    [[nodiscard]] bool UseMsaa() const noexcept { return samples_ != VK_SAMPLE_COUNT_1_BIT; }

    // 升级 22：场景深度图视图（MSAA 深度），供景深 Pass 采样还原线性深度
    void SetSceneDepth(VkImageView depthView, VkImage depthImage, bool msaa) noexcept
    {
        sceneDepthView_ = depthView;
        sceneDepthImage_ = depthImage;
        sceneDepthIsMsaa_ = msaa;
        RefreshDepthDescriptors();
    }
    // 升级 22：每帧相机近/远平面（线性深度还原需要）
    void SetCamera(float nearPlane, float farPlane) noexcept
    {
        camNear_ = nearPlane;
        camFar_ = farPlane;
    }

    // 可调参数（编辑器实时修改）
    float bloomThreshold = 0.8f;
    float bloomSoftKnee = 0.5f;
    float bloomStrength = 0.6f;
    float exposure = 1.0f;

    // 升级 21：色调分级（Color Grading）参数，作用于合成阶段（ACES 之后）。
    // 默认值均为"无操作"，编辑器不改时画面不变。映射见 render/ColorGrading.h。
    float gradeSaturation = 1.0f; // 饱和度：1=不变，0=全灰，>1 更艳
    float gradeContrast = 1.0f;   // 对比度：以中灰(0.5)为中心，1=不变
    float gradeLift = 0.0f;       // 加性偏移（暗部提升），三通道统一
    float gradeGain = 1.0f;       // 乘性缩放（整体亮度），1=不变
    float gradeGamma = 1.0f;      // 幂次（<1 提亮中间调，>1 压暗），1=不变

    // 升级 22：景深（DoF）参数，作用于独立景深 Pass（场景颜色 → 虚化结果）。
    // 默认 enabled=false，开启后处理时不改变画面，由编辑器勾选启用。
    bool dofEnabled = false;       // 景深开关（默认关闭，避免无预警虚化）
    float dofFocusDistance = 7.0f; // 对焦距离（世界单位，与线性深度同量纲）
    float dofAperture = 0.03f;     // 弥散圆强度（光圈/焦距，越大越虚）
    float dofMaxBlur = 0.020f;     // 最大模糊半径（UV 空间）

    // 升级 23：相机运动模糊（Motion Blur）参数，作用于独立运动模糊 Pass（DoF 输出 → 带拖尾结果）。
    // 默认 enabled=false，开启后处理时不改变画面（直通），由编辑器勾选启用。
    bool mbEnabled = false;     // 运动模糊开关（默认关闭）
    float mbStrength = 0.5f;    // 拖尾强度（速度向量缩放 [0,1]）
    float mbMaxBlur = 0.02f;    // 速度向量长度上限（UV 空间，限速防全屏涂抹）
    float mbMaxSamples = 16.0f; // 沿轨迹采样数（越多越平滑、越贵）

    // 升级 25：体积雾（Volumetric Fog）参数，作用于合成 Pass 光线步进。
    // 默认 enabled=false，需开启后处理（前向 + MSAA 路径有线性深度图）才可见。
    bool fogEnabled = false;               // 体积雾开关
    float fogDensity = 0.045f;             // 基准高度处雾密度（越大越浓）
    float fogHeightFalloff = 0.14f;        // 高度指数衰减率（越大雾越贴地）
    float fogBaseHeight = 0.0f;            // 基准高度（米），其下密度饱和
    float fogScatter = 0.6f;               // 阳光前向散射强度 [0,1]（逆光发亮）
    glm::vec3 fogTint{0.72f, 0.82f, 1.0f}; // 雾散射染色

    // 升级 27：雾效阴影采样（God Rays）——步进点投影 CSM 图集，遮挡体在雾中投出光柱
    bool fogShadowEnabled = true; // 雾中投影开关（需 SetFogShadowResources 每帧提供资源）
    int fogSteps = 32;            // 光线步进数（16/32/64，越大光柱越平滑，开销线性增长）

    // 升级 28：TAA 时间抗锯齿（仅 MSAA 路径）——Halton 抖动累积 + 深度重投影历史混合
    bool taaEnabled = false;   // TAA 开关（需 CPU 端每帧 SetTaaCamera/SetTaaJitter 配合抖动投影）
    float taaFeedback = 0.90f; // 历史权重 [0,0.95]，越高越平滑（过大易拖影）

    // 每帧设置 TAA 重投影矩阵（双方均为带抖动的 VP），在 RecordBloom 之前调用
    void SetTaaCamera(const glm::mat4& prevVP, const glm::mat4& currVP) noexcept
    {
        taaReproj_ = prevVP * glm::inverse(currVP);
    }

    // 每帧设置当前帧裁剪空间抖动量（重投影射线还原用）
    void SetTaaJitter(float x, float y) noexcept { taaJitter_ = glm::vec2(x, y); }

    // 重置 TAA 历史（开关切换/历史失效时调用，下一帧直通重建）
    void ResetTaa() noexcept { taaFrameCounter_ = 0; }

    // 升级 27：每帧设置雾阴影资源（场景级联 UBO 缓冲 + CSM 深度图集视图/采样器，同源复用）
    void SetFogShadowResources(VkBuffer lightUbo, VkImageView shadowView, VkSampler shadowSampler) noexcept
    {
        fogShadowLightUbo_ = lightUbo;
        fogShadowView_ = shadowView;
        fogShadowSampler_ = shadowSampler;
    }

    // 升级 26：自动曝光（Eye Adaptation）+ 电影化（暗角/胶片颗粒），作用于合成 Pass。
    // 亮度链每帧常跑（64² → 8² → 1x1 → 适应 ping-pong，开销可忽略），开关仅作用于合成端。
    bool autoExposure = false;      // 自动曝光开关（手动模式 exposure 直用）
    float exposureKeyValue = 0.18f; // 中灰键值：画面平均亮度映射到的目标（18% 灰基准）
    float adaptationSpeed = 1.5f;   // 亮度适应速度（越大收敛越快，模拟人眼延迟）
    float vignetteIntensity = 0.0f; // 暗角强度 [0,1]（0=关闭）
    float vignetteRadius = 0.55f;   // 暗角起始半径 [0,1]（越小越早开始变暗）
    float filmGrain = 0.0f;         // 胶片颗粒强度（显示参考空间加性噪声）

    // 升级 26：每帧帧时长（秒），供亮度适应指数趋近；在 RecordBloom 之前调用
    void SetDeltaTime(float dt) noexcept { frameDelta_ = glm::clamp(dt, 1e-4f, 0.1f); }

    // 每帧设置重投影矩阵（prevVP × inverse(currVP)），在 RecordBloom 之前调用
    void SetMotionBlurCamera(const glm::mat4& prevVP, const glm::mat4& currVP) noexcept
    {
        mbReproj_ = prevVP * glm::inverse(currVP);
    }

    // 升级 25：每帧设置体积雾相机环境（相机位置/前向 + 指向太阳单位向量 + 投影参数），在 RecordBloom 之前调用
    void SetFogCamera(const glm::vec3& pos, const glm::vec3& fwd, const glm::vec3& sunL, float tanHalfFov,
                      float aspect) noexcept
    {
        fogCamPos_ = pos;
        fogCamFwd_ = fwd;
        fogSunL_ = sunL;
        fogTanHalfFov_ = tanHalfFov;
        fogAspect_ = aspect;
    }

  private:
    void CreateImages(const Context& ctx);
    void CreateFramebuffers(const std::vector<VkImageView>& swapchainViews);
    void CreateRenderPasses();
    void CreatePipelines(const Context& ctx);
    void CreateDescriptorResources(const Context& ctx);
    void UpdateDescriptorSets();
    // SetSceneDepth 之后刷新依赖深度视图的描述符（depthLinearize/mb/taa）：
    // Init 阶段 UpdateDescriptorSets 时深度视图尚未注入（为 NULL），须在注入后补写。
    void RefreshDepthDescriptors();
    void DestroyFramebuffers();
    void DestroyPipelines();
    // 升级 26：自适应亮度 ping-pong 图一次性初始化（清为 log(0.18)，转 SHADER_READ_ONLY）
    void InitAdaptImages(const Context& ctx);

    VkDevice device_ = VK_NULL_HANDLE;
    bool initialized_ = false;

    VkExtent2D extent_{0, 0};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    // 离屏场景渲染：MSAA 颜色 + 解析（可采样）。深度图由 Renderer 提供。
    Image offscreenMsaaColor_;
    Image offscreenResolve_;

    // 后处理中间缓冲（半分辨率）
    VkExtent2D halfExtent_{0, 0};
    Image brightImage_;
    Image blurImageA_;
    Image blurImageB_;

    // 后处理渲染通道（单颜色附件，最终布局 SHADER_READ_ONLY）
    VkRenderPass postRenderPass_ = VK_NULL_HANDLE;
    // 升级 22：深度线性化专用渲染通道（单 R32F 颜色附件，匹配线性深度图格式）
    VkRenderPass linearizeRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer brightFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer blurAFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer blurBFramebuffer_ = VK_NULL_HANDLE;

    // 输出渲染通道（写入交换链，最终布局 COLOR_ATTACHMENT_OPTIMAL）
    VkRenderPass outputRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> outputFramebuffers_;

    // 全屏管线
    std::unique_ptr<GraphicsPipeline> brightPipeline_;
    std::unique_ptr<GraphicsPipeline> blurPipeline_;
    std::unique_ptr<GraphicsPipeline> compositePipeline_;

    // 描述符资源
    VkDescriptorSetLayout descSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet brightDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurHDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurVDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeDescSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // 升级 22：景深资源（仅 MSAA 路径使用）
    VkImageView sceneDepthView_ = VK_NULL_HANDLE;
    VkImage sceneDepthImage_ = VK_NULL_HANDLE;
    bool sceneDepthIsMsaa_ = true;
    float camNear_ = 0.1f;
    float camFar_ = 500.0f;

    Image linearDepthImage_; // R32F 线性深度（景深 CoC 计算用）
    Image dofImage_;         // 景深输出（bloom 链改从此图读取）

    VkFramebuffer depthLinearizeFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer dofFramebuffer_ = VK_NULL_HANDLE;

    std::unique_ptr<GraphicsPipeline> depthLinearizePipeline_;
    std::unique_ptr<GraphicsPipeline> dofPipeline_;

    VkDescriptorSet depthLinearizeDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet dofDescSet_ = VK_NULL_HANDLE;

    // 升级 23：运动模糊资源（仅 MSAA 路径使用）
    glm::mat4 mbReproj_ = glm::mat4(1.0f); // 重投影矩阵（prevVP × inverse(currVP)）
    Image mbImage_;                        // 运动模糊输出（bloom 链改从此图读取）
    VkFramebuffer mbFramebuffer_ = VK_NULL_HANDLE;
    std::unique_ptr<GraphicsPipeline> mbPipeline_;
    VkDescriptorSet mbDescSet_ = VK_NULL_HANDLE;

    // 升级 25：体积雾相机环境（每帧 SetFogCamera 更新）
    glm::vec3 fogCamPos_{0.0f};
    glm::vec3 fogCamFwd_{0.0f, 0.0f, -1.0f};
    glm::vec3 fogSunL_{0.5f, 0.8f, 0.3f};
    float fogTanHalfFov_ = 0.577f; // tan(30°)，对应 60° FOV
    float fogAspect_ = 1.7778f;

    // 升级 26：自动曝光资源（亮度测量链 + 适应 ping-pong，固定小尺寸）
    Image lum64Image_;  // 64x64 对数亮度（场景区域采样）
    Image lum8Image_;   // 8x8 盒式下采样
    Image lum1Image_;   // 1x1 当前帧平均对数亮度
    Image adaptImageA_; // 适应结果 ping-pong A
    Image adaptImageB_; // 适应结果 ping-pong B

    // 升级 27：雾阴影资源句柄（每帧由 Application 经 SetFogShadowResources 提供，不拥有）
    VkBuffer fogShadowLightUbo_ = VK_NULL_HANDLE;
    VkImageView fogShadowView_ = VK_NULL_HANDLE;
    VkSampler fogShadowSampler_ = VK_NULL_HANDLE;

    // 升级 28：TAA 资源（全分辨率历史 ping-pong + 管线 + 描述符）
    Image taaImageA_; // 历史 ping-pong A
    Image taaImageB_; // 历史 ping-pong B
    VkFramebuffer taaAFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer taaBFramebuffer_ = VK_NULL_HANDLE;
    std::unique_ptr<GraphicsPipeline> taaPipeline_;
    VkDescriptorSet taaDescSetA_ = VK_NULL_HANDLE; // b0=当前, b1=历史B, b2=MSAA深度 → 写 A
    VkDescriptorSet taaDescSetB_ = VK_NULL_HANDLE; // b0=当前, b1=历史A, b2=MSAA深度 → 写 B
    glm::mat4 taaReproj_ = glm::mat4(1.0f);        // prevVP × inverse(currVP)（双方带抖动）
    glm::vec2 taaJitter_{0.0f};                    // 当前帧裁剪空间抖动量
    uint32_t taaIndex_ = 0;                        // 偶数帧写 A 读 B，奇数帧写 B 读 A
    uint32_t taaFrameCounter_ = 0;                 // TAA 帧计数（0 = 首帧直通重建历史）

    VkFramebuffer lum64Framebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer lum8Framebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer lum1Framebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer adaptAFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer adaptBFramebuffer_ = VK_NULL_HANDLE;

    std::unique_ptr<GraphicsPipeline> lumStartPipeline_; // 场景 → 64x64 对数亮度
    std::unique_ptr<GraphicsPipeline> lumDownPipeline_;  // 8x8 盒式下采样（64→8→1 共用）
    std::unique_ptr<GraphicsPipeline> adaptPipeline_;    // 亮度适应（指数趋近）

    VkDescriptorSet lumStartDescSet_ = VK_NULL_HANDLE; // b0=场景颜色
    VkDescriptorSet lumDownDescA_ = VK_NULL_HANDLE;    // b0=lum64
    VkDescriptorSet lumDownDescB_ = VK_NULL_HANDLE;    // b0=lum8
    VkDescriptorSet adaptDescA_ = VK_NULL_HANDLE;      // b0=lum1, b1=adaptB → 写 adaptA
    VkDescriptorSet adaptDescB_ = VK_NULL_HANDLE;      // b0=lum1, b1=adaptA → 写 adaptB
    uint32_t adaptIndex_ = 0;                          // 偶数帧写 A 读 B，奇数帧写 B 读 A
    uint32_t frameCounter_ = 0;                        // 总帧数（首帧 reset + 颗粒动画种子）
    float frameDelta_ = 1.0f / 60.0f;
    float grainTime_ = 0.0f; // 颗粒动画时间（帧时长累加）
};
} // namespace BigHero::Render
