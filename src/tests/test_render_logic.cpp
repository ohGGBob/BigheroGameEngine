// 渲染纯逻辑（UBO 布局 / 视锥剔除 / 实例化 / HDR / GPU 分配器 / 描述符索引 / 色调分级 / 渲染图 / 瞬态内存池）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
#include "framework/test_common.h"
#include "render/ColorGrading.h"
#include "render/Frustum.h"
#include "render/GpuAllocator.h"
#include "render/HdrImage.h"
#include "render/InstanceBuffer.h"
#include "render/RenderGraph.h"
#include "render/TransientMemoryPool.h"
#include "render/descriptor_set.h"
#include "render/ubo_structs.h"

using namespace BigHero;

TEST_CASE("Render.UboLayout")
{
    // ---- UBO 布局（std140 严格对齐） ----
    CHECK(sizeof(Render::CameraUBO) == 128);
    // GpuPointLight 必须为 16 的倍数：std140 规则要求"结构体数组"步长=大小向上取整到16，
    // 故元素取 48 字节（位置/强度/颜色/半径/阴影标志 + 填充），CPU 数组步长=GPU 步长。
    CHECK(sizeof(Render::GpuPointLight) == 48);
    CHECK(offsetof(Render::LightUBO, lightSpaceMatrix) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights) % 16 == 0);
    CHECK(offsetof(Render::LightUBO, lights[1]) - offsetof(Render::LightUBO, lights[0]) == 48);
    // 点光源立方体阴影 UBO：6 个 mat4 紧密数组，每 mat4 64 字节
    CHECK(sizeof(Render::PointShadowUBO) == 6 * 64);
    CHECK(offsetof(Render::PointShadowUBO, faceMatrices[1]) - offsetof(Render::PointShadowUBO, faceMatrices[0]) == 64);
}

TEST_CASE("Render.FrustumCulling")
{
    // ---- 视锥剔除（纯数学，无 GPU 依赖） ----
    // 单位立方体裁剪盒（VP=identity）：可见区为 x,y,z ∈ [-1,1]
    {
        const Render::Frustum idF = Render::Frustum::FromViewProj(glm::mat4(1.0f));
        CHECK(idF.IntersectsSphere(glm::vec3(0.0f), 0.1f));              // 中心在内
        CHECK(idF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 0.5f), 0.1f));  // 偏内
        CHECK(!idF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 5.0f), 0.1f)); // 远处被远平面剔除
        CHECK(!idF.IntersectsSphere(glm::vec3(5.0f, 0.0f, 0.0f), 0.1f)); // 右侧被右平面剔除
    }
    // 透视相机（Vulkan NDC z∈[0,1]）：相机位于 (0,0,5) 看向原点
    {
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const Render::Frustum camF = Render::Frustum::FromViewProj(proj * view);
        CHECK(camF.IntersectsSphere(glm::vec3(0.0f), 1.0f));                // 原点在相机前方
        CHECK(camF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 4.9f), 0.5f));    // 贴近近平面
        CHECK(!camF.IntersectsSphere(glm::vec3(0.0f, 0.0f, 20.0f), 1.0f));  // 相机后方
        CHECK(!camF.IntersectsSphere(glm::vec3(0.0f, 100.0f, 0.0f), 1.0f)); // 视野上方之外
    }
    // 立方体局部包围球半径 ≈ 0.5*sqrt(3)
    CHECK(std::fabs(Scene::kCubeBoundingRadius - 0.8660254f) < 1e-4f);
}

TEST_CASE("Render.InstanceLayout")
{
    // ---- 实例化布局（逐实例输入 std140） ----
    // 实例步长须为 16 的倍数，且模型矩阵 4 行各按 16 字节对齐。
    // model(mat4,64) + tint(vec4,16) + metallic(4) + roughness(4) + pad[2](8) = 96 = 6*16。
    CHECK(sizeof(Render::InstanceData) % 16 == 0);
    CHECK(sizeof(Render::InstanceData) == 96);
    CHECK(offsetof(Render::InstanceData, tint) == 64);     // model 64 字节之后
    CHECK(offsetof(Render::InstanceData, metallic) == 80); // tint 16 字节之后
    CHECK(offsetof(Render::InstanceData, roughness) == 84);
    {
        const VkVertexInputBindingDescription binding = Render::InstanceBuffer::GetBindingDesc();
        CHECK(binding.binding == 1);
        CHECK(binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE);
        CHECK(binding.stride == sizeof(Render::InstanceData));
        const auto attrs = Render::InstanceBuffer::GetAttrDesc();
        CHECK(attrs.size() == 6);       // 4 行 model + tint + metallic/roughness，无空槽
        CHECK(attrs[0].location == 5);  // model[0] 行
        CHECK(attrs[3].location == 8);  // model[3] 行
        CHECK(attrs[4].location == 9);  // tint
        CHECK(attrs[5].location == 10); // metallic/roughness
        for (const auto& a : attrs)
            CHECK(a.binding == 1); // 全部走实例 binding
    }
}

TEST_CASE("Render.HdrImage")
{
    // ---- HDR 环境贴图加载器（纯CPU，RGBE解码） ----
    {
        // 手工构造一张 16x2 的 Radiance .hdr：两行，全行 RLE 压缩，各通道恒定值。
        // R=200,G=100,B=50,E=140 => scale=2^(140-128-8)=16 => (3200,1600,800)。
        const std::string header = "#?RADIANCE\n"
                                   "FORMAT=32-bit_rle_rgbe\n"
                                   "\n"
                                   "-Y 2 +X 16\n";
        std::vector<uint8_t> bytes(header.begin(), header.end());
        const std::vector<uint8_t> scanline = {
            2,   2,   0, 16, // RLE 头，跨度=16
            144, 200,        // R 通道：重复16次值200
            144, 100,        // G 通道
            144, 50,         // B 通道
            144, 140         // E 通道
        };
        bytes.insert(bytes.end(), scanline.begin(), scanline.end());
        bytes.insert(bytes.end(), scanline.begin(), scanline.end()); // 第二行

        HdrImage img;
        img.LoadFromMemory(std::move(bytes));
        CHECK(img.IsValid());
        CHECK(img.Width() == 16);
        CHECK(img.Height() == 2);
        CHECK(img.Exposure() == 1.0f); // 未声明 EXPOSURE，默认1
        CHECK(img.Pixels().size() == 32);
        const glm::vec4 p = img.Pixels()[0];
        CHECK(std::fabs(p.r - 3200.0f) < 1e-3f);
        CHECK(std::fabs(p.g - 1600.0f) < 1e-3f);
        CHECK(std::fabs(p.b - 800.0f) < 1e-3f);
        CHECK(std::fabs(p.a - 1.0f) < 1e-6f);
        // 其余像素一致（RLE 展开正确）
        bool allSame = true;
        for (const auto& q : img.Pixels())
            allSame = allSame && glm::distance(q, p) < 1e-4f;
        CHECK(allSame);

        // RGBEToLinear：E=128 时 scale=1/256=0.00390625
        const uint8_t rgbe[4] = {255, 0, 0, 128};
        const glm::vec3 c = HdrImage::RGBEToLinear(rgbe);
        CHECK(std::fabs(c.r - 255.0f / 256.0f) < 1e-4f);
        CHECK(c.g == 0.0f && c.b == 0.0f);
        // E=0 => 纯黑
        const uint8_t black[4] = {255, 255, 255, 0};
        CHECK(HdrImage::RGBEToLinear(black) == glm::vec3(0.0f));
    }
}

TEST_CASE("Render.HdrToCubemap")
{
    // ---- HDR 等距柱状投影 -> 立方图（纯CPU采样一致性） ----
    {
        // 简单纬度梯度：上半球(北)亮、下半球(南)暗，经度无关。
        const uint32_t w = 64, h = 32;
        std::vector<glm::vec3> equi(static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(h - 1); // 0..1 (上->下)
            const float light = (v < 0.5f) ? 1.0f : 0.2f;                      // 上半亮下半暗
            for (uint32_t x = 0; x < w; ++x)
                equi[static_cast<size_t>(y) * w + x] = glm::vec3(light);
        }

        // SampleEquirect：+Y 方向（天顶）应落在上半球（亮），-Y 方向（天底）在下半球（暗）
        const glm::vec3 top = HdrImage::SampleEquirect(glm::vec3(0, 1, 0), equi.data(), w, h);
        const glm::vec3 bottom = HdrImage::SampleEquirect(glm::vec3(0, -1, 0), equi.data(), w, h);
        CHECK(top.r > 0.5f);
        CHECK(bottom.r < 0.5f);

        // EquirectToCube：6 面均正确生成，+Y 面（索引2）应整体亮、-Y 面（索引3）应整体暗
        const auto faces = HdrImage::EquirectToCube(equi.data(), w, h, 8);
        CHECK(faces.size() == 6);
        CHECK(faces[0].size() == 64); // 8x8
        float topAvg = 0.0f, bottomAvg = 0.0f;
        for (const auto& px : faces[2])
            topAvg += px.r;
        for (const auto& px : faces[3])
            bottomAvg += px.r;
        topAvg /= static_cast<float>(faces[2].size());
        bottomAvg /= static_cast<float>(faces[3].size());
        CHECK(topAvg > 0.5f);
        CHECK(bottomAvg < 0.5f);

        // 无效输入抛异常
        bool threw = false;
        try
        {
            (void)HdrImage::EquirectToCube(nullptr, w, h, 8);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        CHECK(threw);
    }
}

TEST_CASE("Render.GpuAllocator")
{
    // ---- GPU 显存分配器（VMA 替代：块内子分配 + 多块管理） ----
    {
        using namespace BigHero::Render;

        // 单块子分配器：容量 1024，粒度 256
        GpuBlockAllocator blk(1024, 256);
        CHECK(blk.Capacity() == 1024);
        CHECK(blk.Used() == 0);
        CHECK(blk.AllocationCount() == 0);

        // 分配大小向上取整到粒度 256
        const GpuAllocation a0 = blk.Alloc(100, 256);
        CHECK(a0.valid && a0.offset == 0 && a0.size == 256);
        const GpuAllocation a1 = blk.Alloc(300, 256); // roundUp 512
        CHECK(a1.valid && a1.offset == 256 && a1.size == 512);
        const GpuAllocation a2 = blk.Alloc(256, 256);
        CHECK(a2.valid && a2.offset == 768 && a2.size == 256);
        CHECK(blk.Used() == 1024);
        CHECK(blk.AllocationCount() == 3);

        // 块满 -> 分配失败
        CHECK(!blk.Alloc(1, 256).valid);

        // 释放中间段后非空，可复用
        blk.Free(a1);
        CHECK(blk.Used() == 512);
        CHECK(blk.AllocationCount() == 2);
        const GpuAllocation a1b = blk.Alloc(400, 256); // roundUp 512，落入 [256,768)
        CHECK(a1b.valid && a1b.offset == 256 && a1b.size == 512);
        CHECK(blk.Used() == 1024);
        CHECK(!blk.Alloc(1, 256).valid); // 真正满了

        // 对齐 > 粒度：偏移向上取整到 align
        GpuBlockAllocator blk2(4096, 256);
        blk2.Alloc(100, 256);                               // [0,256)
        const GpuAllocation aligned = blk2.Alloc(100, 512); // free [256,4096) -> off roundUp(256,512)=512
        CHECK(aligned.valid && aligned.offset == 512);

        // 合并：释放全部段后整块回归连续空闲，可再分配满块
        GpuBlockAllocator blk3(1024, 256);
        GpuAllocation x0 = blk3.Alloc(256, 256); // [0,256)
        GpuAllocation x1 = blk3.Alloc(256, 256); // [256,512)
        GpuAllocation x2 = blk3.Alloc(256, 256); // [512,768)
        GpuAllocation x3 = blk3.Alloc(256, 256); // [768,1024)
        blk3.Free(x0);                           // [0,256)
        blk3.Free(x2);                           // [512,768)
        blk3.Free(x1);                           // [256,512) -> 与 [0,256) 合并 [0,512)
        blk3.Free(x3);                           // [768,1024) -> 合并 [0,1024)
        CHECK(blk3.Empty());
        CHECK(blk3.Used() == 0);
        const GpuAllocation whole = blk3.Alloc(1024, 256);
        CHECK(whole.valid && whole.offset == 0 && whole.size == 1024);

        // 多块分配器：假后端只计数，不真正分配显存
        int created = 0;
        GpuAllocator mgr(1024, 256, 4,
                         [&](uint32_t, VkDeviceSize)
                         {
                             ++created;
                             return VK_NULL_HANDLE;
                         });
        CHECK(mgr.BlockCount() == 0);
        CHECK(mgr.BlockSize() == 1024);
        CHECK(mgr.MaxBlocks() == 4);

        std::vector<GpuAllocation> ms;
        for (int i = 0; i < 4; ++i)
        {
            const GpuAllocation x = mgr.Alloc(256, 256);
            CHECK(x.valid);
            ms.push_back(x);
        }
        CHECK(mgr.BlockCount() == 1);
        CHECK(created == 1); // 仅创建块 0
        // 块 0 满 -> 新建块 1
        const GpuAllocation mb = mgr.Alloc(256, 256);
        CHECK(mb.valid && mb.block == 1);
        CHECK(mgr.BlockCount() == 2);
        CHECK(created == 2);

        // 释放块 0 首个分配，后续分配优先复用已有块（块数不再增长）
        mgr.Free(ms[0]);
        const GpuAllocation mc = mgr.Alloc(256, 256);
        CHECK(mc.valid && mc.block == 0);
        CHECK(mgr.BlockCount() == 2);
        CHECK(created == 2);

        // 越界/无效句柄释放安全
        mgr.Free(GpuAllocation{});
        mgr.Free(ms[1]);
        mgr.Free(ms[2]);
        mgr.Free(ms[3]);
        mgr.Free(mb);
        mgr.Free(mc);
        CHECK(mgr.BlockCount() == 2); // 块不主动回收（简单容量管理）
    }
}

TEST_CASE("Render.DescriptorSetIndex")
{
    // ---- 描述符集索引计算（替换 i*3+N 魔法数） ----
    {
        using namespace BigHero::Render;

        // 枚举值与每帧集合数一致
        CHECK(static_cast<uint32_t>(FrameDescriptorSet::Camera) == 0);
        CHECK(static_cast<uint32_t>(FrameDescriptorSet::Light) == 1);
        CHECK(static_cast<uint32_t>(FrameDescriptorSet::PointShadow) == 2);
        CHECK(kDescriptorSetsPerFrame == 3);

        // FrameSetIndex = frame * kDescriptorSetsPerFrame + kind
        CHECK(FrameSetIndex(0, FrameDescriptorSet::Camera) == 0);
        CHECK(FrameSetIndex(0, FrameDescriptorSet::Light) == 1);
        CHECK(FrameSetIndex(0, FrameDescriptorSet::PointShadow) == 2);
        CHECK(FrameSetIndex(1, FrameDescriptorSet::Camera) == 3);
        CHECK(FrameSetIndex(1, FrameDescriptorSet::Light) == 4);
        CHECK(FrameSetIndex(2, FrameDescriptorSet::PointShadow) == 8);
    }
}

TEST_CASE("Render.ColorGrading")
{
    // ---- 色调分级 ColorGrading（纯CPU：gain/lift + gamma + 对比度 + 饱和度） ----
    {
        using namespace BigHero::Render;

        const ColorGradeParams identity{}; // 全默认 = 无操作

        // 1) 默认参数：输出 == 输入（无操作）
        {
            const glm::vec3 c(0.3f, 0.6f, 0.9f);
            const glm::vec3 g = GradeColor(c, identity);
            CHECK(std::fabs(g.r - c.r) < 1e-4f);
            CHECK(std::fabs(g.g - c.g) < 1e-4f);
            CHECK(std::fabs(g.b - c.b) < 1e-4f);
        }

        // 2) gain=2：整体亮度翻倍（其余阶段默认=1）
        {
            const glm::vec3 c(0.3f, 0.0f, 0.0f);
            ColorGradeParams p{};
            p.gain = 2.0f;
            const glm::vec3 g = GradeColor(c, p);
            CHECK(std::fabs(g.r - 0.6f) < 1e-4f);
            CHECK(std::fabs(g.g) < 1e-4f);
            CHECK(std::fabs(g.b) < 1e-4f);
        }

        // 3) saturation=0：去色为灰度（三通道相等 = 亮度）
        {
            const glm::vec3 c(1.0f, 0.5f, 0.2f);
            ColorGradeParams p{};
            p.saturation = 0.0f;
            const glm::vec3 g = GradeColor(c, p);
            CHECK(std::fabs(g.r - g.g) < 1e-4f);
            CHECK(std::fabs(g.g - g.b) < 1e-4f);
            // 亮度 = 1*0.2126 + 0.5*0.7152 + 0.2*0.0722 = 0.58464
            CHECK(std::fabs(g.r - 0.58464f) < 1e-3f);
        }

        // 4) gamma=2.0：中间调压暗（0.25 -> 0.0625）
        {
            const glm::vec3 c(0.25f, 0.0f, 0.0f);
            ColorGradeParams p{};
            p.gamma = 2.0f;
            const glm::vec3 g = GradeColor(c, p);
            CHECK(std::fabs(g.r - 0.0625f) < 1e-4f);
            CHECK(std::fabs(g.g) < 1e-4f);
            CHECK(std::fabs(g.b) < 1e-4f);
        }

        // 5) contrast=1.5：中灰点(0.5)对比度中心，不变
        {
            const glm::vec3 c(0.5f, 0.5f, 0.5f);
            ColorGradeParams p{};
            p.contrast = 1.5f;
            const glm::vec3 g = GradeColor(c, p);
            CHECK(std::fabs(g.r - 0.5f) < 1e-4f);
            CHECK(std::fabs(g.g - 0.5f) < 1e-4f);
            CHECK(std::fabs(g.b - 0.5f) < 1e-4f);
        }

        // 6) lift 提升暗部（黑 -> lift）
        {
            const glm::vec3 c(0.0f, 0.0f, 0.0f);
            ColorGradeParams p{};
            p.lift = 0.1f;
            const glm::vec3 g = GradeColor(c, p);
            CHECK(std::fabs(g.r - 0.1f) < 1e-4f);
            CHECK(std::fabs(g.g - 0.1f) < 1e-4f);
            CHECK(std::fabs(g.b - 0.1f) < 1e-4f);
        }
    }
}

TEST_CASE("Render.RenderGraph")
{
    // ---- 渲染图（Render Graph）：纯逻辑布局推导（不触发真实 Vulkan 调用） ----
    {
        using namespace Render;
        // 伪 VkImage 句柄（仅作标识，Build 不触碰真实 GPU 对象）
        const VkImage imgGBuffer = reinterpret_cast<VkImage>(0x1001);
        const VkImage imgHdr = reinterpret_cast<VkImage>(0x1002);
        const VkImage imgSwap = reinterpret_cast<VkImage>(0x1003);
        const VkImage imgAo = reinterpret_cast<VkImage>(0x1004);

        // 1) 写后采样读：GBuffer 以颜色附件写（finalLayout=SHADER_READ_ONLY，由 endLayout 声明），
        //    后续光照 pass 采样读 → 布局已被 render pass 转换，只需同布局内存 barrier（写后读同步）
        {
            RenderGraph rg;
            rg.AddPass("geometry", [] {},
                       {{imgGBuffer, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.AddPass("lighting", [] {},
                       {{imgGBuffer, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.Build();
            CHECK(rg.PassCount() == 2);
            CHECK(rg.ImageCount() == 1);
            // 首个 barrier：首次使用 UNDEFINED -> COLOR_ATTACHMENT（归属 pass 0）
            CHECK(rg.PlannedBarriers().size() == 2);
            CHECK(rg.PlannedBarriers()[0].newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            CHECK(rg.BarrierPassIndex()[0] == 0);
            // 第二个 barrier：同布局内存同步（写后读），布局保持 SHADER_READ_ONLY
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.oldLayout == b1.newLayout);
            CHECK(b1.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            CHECK(rg.BarrierPassIndex()[1] == 1);
            // 写阶段为 COLOR_ATTACHMENT_OUTPUT，读阶段为 FRAGMENT_SHADER
            CHECK((b1.srcStage & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0);
            CHECK((b1.dstStage & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0);
        }

        // 1b) 若 render pass 不负责布局转换（finalLayout=COLOR_ATTACHMENT，未声明 endLayout），
        //     写后采样应由渲染图显式插入布局转换 barrier
        {
            RenderGraph rg;
            rg.AddPass("geometry", [] {}, {{imgGBuffer, RGUsage::ColorAttachment}});
            rg.AddPass("lighting", [] {}, {{imgGBuffer, RGUsage::SampledRead}});
            rg.Build();
            CHECK(rg.PlannedBarriers().size() == 2);
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            CHECK(b1.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // 2) 深度附件：写深度（finalLayout=DS_READ_ONLY）后采样 → 深度同布局内存同步
        {
            RenderGraph rg;
            rg.AddPass("scene", [] {},
                       {{imgAo, RGUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
            rg.AddPass("dof", [] {},
                       {{imgAo, RGUsage::DepthReadOnly, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
            rg.Build();
            CHECK(rg.PlannedBarriers().size() == 2);
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.oldLayout == b1.newLayout);
            CHECK(b1.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            CHECK((b1.srcStage & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) != 0);
        }

        // 3) 同布局写后写（如两个连续 pass 都写 swapchain）：插入同布局内存 barrier
        {
            RenderGraph rg;
            rg.AddPass("passA", [] {}, {{imgSwap, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
            rg.AddPass("passB", [] {}, {{imgSwap, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}});
            rg.Build();
            CHECK(rg.PlannedBarriers().size() == 2);
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.oldLayout == b1.newLayout); // 同布局内存 barrier
            CHECK(b1.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        // 4) 呈现链：最后 pass 以 PresentSrc 输出 → 目标布局为 PRESENT_SRC
        {
            RenderGraph rg;
            rg.AddPass("composite", [] {}, {{imgSwap, RGUsage::PresentSrc, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}});
            rg.Build();
            CHECK(rg.ImageLayout(0) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        // 5) 资源去重注册：同一 VkImage 多次使用只占一个图资源
        {
            RenderGraph rg;
            rg.AddPass("a", [] {}, {{imgHdr, RGUsage::ColorAttachment}});
            rg.AddPass("b", [] {}, {{imgHdr, RGUsage::SampledRead}});
            rg.Build();
            CHECK(rg.ImageCount() == 1);
        }
    }
}

TEST_CASE("Render.RenderGraphV2")
{
    // ---- 渲染图 v2：精确同步 + 资源生命周期 ----
    {
        using namespace Render;
        const VkImage imgC = reinterpret_cast<VkImage>(0x2001);
        const VkImage imgD = reinterpret_cast<VkImage>(0x2002);
        const VkImage imgL = reinterpret_cast<VkImage>(0x2003);

        // 写后读（含精确 access）：颜色附件写 → 采样读
        {
            RenderGraph rg;
            rg.RegisterImage("g", imgC, VK_IMAGE_LAYOUT_UNDEFINED, 1024 * 1024 * 16);
            rg.AddPass("geo", [] {}, {{imgC, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.AddPass("light", [] {}, {{imgC, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.Build();
            CHECK(rg.PlannedBarriers().size() == 2);
            // 首个 barrier：首次 UNDEFINED→COLOR_ATTACHMENT，srcAccess=0（忽略旧内容），dstAccess=颜色写
            CHECK(rg.PlannedBarriers()[0].srcAccess == 0);
            CHECK(rg.PlannedBarriers()[0].dstAccess == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            // 第二个 barrier：同布局内存同步，srcAccess=颜色写 → dstAccess=着色器读
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.srcAccess == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            CHECK(b1.dstAccess == VK_ACCESS_SHADER_READ_BIT);
            CHECK((b1.srcStage & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0);
            CHECK((b1.dstStage & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0);
            // 生命周期：资源从 pass0 活到 pass1
            const RGLifetime life = rg.ResourceLifetime(0);
            CHECK(life.firstUse == 0 && life.lastUse == 1);
        }

        // 深度写后采样：精确 depth 访问掩码
        {
            RenderGraph rg;
            rg.AddPass("scene", [] {},
                       {{imgD, RGUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
            rg.AddPass("dof", [] {}, {{imgD, RGUsage::DepthReadOnly, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
            rg.Build();
            const auto& b1 = rg.PlannedBarriers()[1];
            CHECK(b1.srcAccess == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            CHECK(b1.dstAccess == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        }

        // 生命周期区间与 Overlaps 判定（供 transient 别名分析）
        {
            // imgL 在 pass0..1 活（区间 [0,1]），与另一个 [2,3] 的资源不重叠 → 可别名
            RenderGraph rg;
            rg.AddPass("a", [] {}, {{imgL, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.AddPass("b", [] {}, {{imgL, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.AddPass("c", [] {}, {{imgC, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.AddPass("d", [] {}, {{imgC, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
            rg.Build();
            const RGLifetime l1 = rg.ResourceLifetime(0); // imgL: [0,1]
            const RGLifetime l2 = rg.ResourceLifetime(1); // imgC: [2,3]
            CHECK(l1.firstUse == 0 && l1.lastUse == 1);
            CHECK(l2.firstUse == 2 && l2.lastUse == 3);
            CHECK(!l1.Overlaps(l2)); // 不重叠 → 可共享显存
            CHECK(l1.Overlaps(l1));  // 自重叠恒真
        }
    }
}

TEST_CASE("Render.TransientMemoryPool")
{
    // ---- 瞬态内存池（transient 别名分配，纯逻辑） ----
    {
        using namespace Render;
        // 1) 基本分配与释放：分配两块、释放后空间可复用
        {
            TransientMemoryPool pool(1024);
            const VkDeviceSize a = pool.Allocate(128, 64);
            CHECK(a == 0);
            const VkDeviceSize b = pool.Allocate(128, 64);
            CHECK(b == 128);
            CHECK(pool.UsedBytes() == 256);
            pool.Free(a);
            pool.Free(b);
            CHECK(pool.UsedBytes() == 0);
            // 释放后整池 1024 可用，仍可分配
            CHECK(pool.Allocate(1024, 64) == 0);
        }

        // 2) 对齐：未对齐的偏移向上对齐，浪费的间隙留在空闲列表
        {
            TransientMemoryPool pool(1000);
            CHECK(pool.Allocate(100, 1) == 0);              // [0,100)
            const VkDeviceSize c = pool.Allocate(100, 256); // 对齐到 256
            CHECK(c == 256);
            pool.Free(0);
            // 释放 [0,100) 后，与 [100,256) 间隙合并 → [0,256) 空闲，可容纳 200 对齐 64
            const VkDeviceSize d = pool.Allocate(200, 64);
            CHECK(d == 384); // 对齐空隙不复用：next free [356,644) align64 -> 384
            pool.Free(c);
            pool.Free(d);
        }

        // 3) 相邻释放自动合并（避免碎片）：两段分配，释放中间段后前后合并成完整区间
        {
            TransientMemoryPool pool(512);
            const VkDeviceSize a = pool.Allocate(128, 1);  // 0
            const VkDeviceSize b = pool.Allocate(128, 1);  // 128
            const VkDeviceSize cc = pool.Allocate(128, 1); // 256
            CHECK(a == 0 && b == 128 && cc == 256);
            pool.Free(b);
            // 释放中间段后 [128,256) 空闲；[0,128)+[128,256) 不相邻（0 未释放），
            // 但 [128,256)+[256,384)=[128,384) 合并 → 可一次性分配 256
            CHECK(pool.Allocate(256, 1) == TransientMemoryPool::kInvalidOffset); // 中间段未合并，空间不足
            pool.Free(cc);
            // 释放第三段后 [128,384) 合并成连续 256 字节空闲 -> 可一次性分配
            const VkDeviceSize e = pool.Allocate(256, 1);
            CHECK(e == 128);
            pool.Free(a);
            pool.Free(e);
            CHECK(pool.UsedBytes() == 0);
        }

        // 4) 空间不足返回 kInvalidOffset，且不破坏既有分配
        {
            TransientMemoryPool pool(64);
            CHECK(pool.Allocate(64, 1) == 0);
            CHECK(pool.Allocate(1, 1) == TransientMemoryPool::kInvalidOffset);
            pool.Reset();
            CHECK(pool.UsedBytes() == 0);
            CHECK(pool.Allocate(64, 1) == 0);
        }
    }
}

TEST_CASE("Render.TransientSlots")
{
    // ---- 渲染图 v2：transient 槽位规划（区间着色，供内存别名） ----
    {
        using namespace Render;
        const VkImage tA = reinterpret_cast<VkImage>(0x3001);
        const VkImage tB = reinterpret_cast<VkImage>(0x3002);
        const VkImage tC = reinterpret_cast<VkImage>(0x3003);

        // A[0,1] 与 B[2,3] 不重叠 -> 共享槽 0；C[1,2] 与两者都重叠 -> 独立槽 1
        RenderGraph rg;
        rg.RegisterImage("A", tA, VK_IMAGE_LAYOUT_UNDEFINED, 16);
        rg.RegisterImage("B", tB, VK_IMAGE_LAYOUT_UNDEFINED, 8);
        rg.RegisterImage("C", tC, VK_IMAGE_LAYOUT_UNDEFINED, 4);
        rg.AddPass("p0", [] {}, {{tA, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        rg.AddPass("p1", [] {},
                   {{tA, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {tC, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        rg.AddPass("p2", [] {},
                   {{tC, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {tB, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        rg.AddPass("p3", [] {}, {{tB, RGUsage::SampledRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        rg.Build();
        const std::vector<int32_t> slots = rg.PlanTransientSlots();
        CHECK(slots.size() == 3);
        CHECK(slots[0] == 0); // A
        CHECK(slots[1] == 0); // B（与 A 共享）
        CHECK(slots[2] == 1); // C（独立）
    }
}
