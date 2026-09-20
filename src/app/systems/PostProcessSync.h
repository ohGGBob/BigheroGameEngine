#pragma once
// 阶段 3a：后处理参数同步子系统（自 Application 上帝对象拆出）。
// 持有全部"编辑器可调后处理参数 + 渲染路径开关 + TAA 抖动状态 + 视图投影缓存"。
// 职责：① SyncToPostProcessor：每帧把参数同步进 PostProcessor（RecordUi 调用）；
//      ② AdvanceJitter：每帧推进 TAA Halton 抖动并注入相机（UpdateCamera 调用）。
// 纯值状态（不持 GPU 资源）；字段公有（EditorPanel 以指针直写），方法参数传每帧量。

#include "editor/EditorPanel.h" // LightParams

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
class PostProcessor;
}

namespace BigHero
{
class PostProcessSync
{
  public:
    // ---- 渲染路径开关（编辑器勾选；边沿检测驱动 renderer_ 模式切换，Application::UpdateDeferredState 消费） ----
    bool deferred = false;
    bool prevDeferred = false;
    bool postProcess = false; // 后处理总开关（编辑器勾选；驱动 renderer_ 模式切换）
    bool prevPostProcess = false;
    bool ssao = false;
    bool prevSsao = false;
    bool ssr = false;
    bool prevSsr = false;

    // ---- 色调分级（升级 21，作用于 PostProcessor 合成阶段） ----
    float gradeSaturation = 1.0f;
    float gradeContrast = 1.0f;
    float gradeLift = 0.0f;
    float gradeGain = 1.0f;
    float gradeGamma = 1.0f;

    // ---- 景深（升级 22，作用于独立景深 Pass） ----
    bool dofEnabled = false;
    float dofFocusDistance = 7.0f;
    float dofAperture = 0.03f;
    float dofMaxBlur = 0.020f;

    // ---- 相机运动模糊（升级 23，作用于独立运动模糊 Pass） ----
    bool mbEnabled = false;
    float mbStrength = 0.5f;    // 拖尾强度 [0,1]
    float mbMaxBlur = 0.02f;    // 速度向量长度上限（UV 空间）
    float mbMaxSamples = 16.0f; // 沿轨迹采样数

    // ---- 体积雾（升级 25，合成 Pass 光线步进；27 雾中投影 God Rays） ----
    bool fogEnabled = false;
    float fogDensity = 0.045f;             // 基准高度处雾密度
    float fogHeightFalloff = 0.14f;        // 高度指数衰减率
    float fogBaseHeight = 0.0f;            // 基准高度（米）
    float fogScatter = 0.6f;               // 阳光前向散射强度 [0,1]
    glm::vec3 fogTint{0.72f, 0.82f, 1.0f}; // 雾散射染色
    bool fogShadowEnabled = true;          // 雾中投影（God Rays，升级 27）
    int fogSteps = 32;                     // 雾光线步进数（16/32/64）

    // ---- 自动曝光 + 电影化（升级 26，作用于合成 Pass） ----
    bool autoExposure = false;
    float exposureKeyValue = 0.18f; // 中灰键值
    float adaptationSpeed = 1.5f;   // 亮度适应速度
    float vignetteIntensity = 0.0f; // 暗角强度 [0,1]
    float vignetteRadius = 0.55f;   // 暗角起始半径 [0,1]
    float filmGrain = 0.0f;         // 胶片颗粒强度

    // ---- TAA 时间抗锯齿（升级 28，独立 TAA Pass + 场景渲染抖动投影） ----
    bool taaEnabled = false;
    bool prevTaaEnabled = false; // 开关边沿检测（切换时重置 TAA 历史防拖影）
    float taaFeedback = 0.90f;   // 历史权重 [0,0.95]
    float taaJitterX = 0.0f;     // 当前帧 NDC 抖动量（Halton(2,3) 亚像素偏移）
    float taaJitterY = 0.0f;
    int taaJitterPhase = 0; // 抖动相位（8 相位循环）

    // 相机视图投影矩阵：prev=上一帧、curr=当前帧，供运动模糊/TAA 重投影
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    glm::mat4 currViewProj = glm::mat4(1.0f);

    // 每帧把后处理参数同步进 PostProcessor（RecordUi 内调用）。
    // cascadeUbo/View/Sampler：雾阴影同源资源（级联 UBO 缓冲 + CSM 图集视图/采样器）；
    // light：太阳方向与雾相机环境来源；cameraPos/cameraFwd/cameraFov：雾相机环境（活跃相机）；
    // dt：亮度适应步长。
    void SyncToPostProcessor(Render::PostProcessor* pp, VkExtent2D extent, VkBuffer cascadeUbo, VkImageView cascadeView,
                             VkSampler cascadeSampler, const LightParams& light, const glm::vec3& cameraPos,
                             const glm::vec3& cameraFwd, float cameraFov, float dt);

    // 每帧推进 TAA Halton(2,3) 8 相位抖动；返回 NDC 抖动量（[-1,1]）。
    // jitterActive = taaEnabled && PostProcessor 就绪 && MSAA 路径；非激活时返回零。
    // 调用方负责将返回值写入当前活跃相机（OrbitCamera 或 FirstPersonCamera）。
    [[nodiscard]] glm::vec2 AdvanceJitter(bool jitterActive, VkExtent2D frameExtent);
};
} // namespace BigHero
