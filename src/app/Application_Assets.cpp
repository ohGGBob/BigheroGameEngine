// Application 资产装配翻译单元：网格/纹理登记（bighero:: 资产面板数据源）、
// glTF 模型加载与逐材质批次绘制辅助（MakeGltfPush / SortedGltfBlendPrims / DrawGltfPrims）。
// 从 Application.cpp 拆出（2026-09 复评收尾：Application 主文件 ≤1200 行）。
#include "app/Application.h"

#include "scene/GltfLoader.h"

#include <algorithm>
#include <filesystem>

namespace BigHero
{

void Application::RegisterMeshAsset(const std::string& name, const std::string& path,
                                    const std::vector<Scene::Vertex>& verts,
                                    const std::vector<uint32_t>& indices,
                                    bighero::AssetMetadata::LoadState state, uint64_t fileSize)
{
    bighero::AssetRegistry::Entry e;
    e.name = name;
    e.path = path;
    e.type = static_cast<uint32_t>(bighero::AssetMetadata::AssetType::Mesh);
    e.state = static_cast<uint32_t>(state);
    e.size = fileSize;
    assetRegistry_.Add(e);

    bighero::MeshResource res(name, static_cast<uint32_t>(verts.size()),
                              static_cast<uint32_t>(indices.size()));
    res.SetPrimitiveCount(static_cast<uint32_t>(indices.size() / 3));
    if (!verts.empty())
    {
        glm::vec3 lo = verts[0].pos, hi = verts[0].pos;
        for (const Scene::Vertex& v : verts)
        {
            lo = glm::min(lo, v.pos);
            hi = glm::max(hi, v.pos);
        }
        res.SetBounds(lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
    }
    meshResources_[name] = std::move(res);
}

const bighero::MeshResource* Application::FindMeshResource(const std::string& name) const
{
    const auto it = meshResources_.find(name);
    return it != meshResources_.end() ? &it->second : nullptr;
}

void Application::RegisterTextureAsset(const std::string& name, const std::string& path,
                                       bighero::AssetMetadata::LoadState state, uint64_t fileSize)
{
    bighero::AssetRegistry::Entry e;
    e.name = name;
    e.path = path;
    e.type = static_cast<uint32_t>(bighero::AssetMetadata::AssetType::Texture);
    e.state = static_cast<uint32_t>(state);
    e.size = fileSize;
    assetRegistry_.Add(e);
}

uint32_t Application::LoadTextureSlot(const std::string& baseDir, const std::string& uri, bool sRGB,
                                      uint32_t fallbackSlot, const std::string& debugName)
{
    if (uri.empty() || nextTextureSlot_ >= Render::kObjectTextureSlots)
        return fallbackSlot;

    const std::filesystem::path p = std::filesystem::path(baseDir) / uri;
    if (!std::filesystem::exists(p))
    {
        LOG_WARN("glTF 贴图未找到: " << p.string() << "（" << debugName << " 使用回退槽 " << fallbackSlot << "）");
        RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Failed, 0);
        return fallbackSlot;
    }

    // 经 AssetManager 缓存工厂加载（键名含 normal/_mr 按线性，其余 sRGB）；sRGB 参数保留语义校验用
    auto tex = assetManager_.Load<Texture>(p.string());
    const auto fileSize = static_cast<uint64_t>(std::filesystem::file_size(p));
    if (!tex)
    {
        LOG_WARN("glTF 贴图加载失败: " << p.string() << "（" << debugName << " 使用回退槽 " << fallbackSlot << "）");
        RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Failed, fileSize);
        return fallbackSlot;
    }

    texturePool_[nextTextureSlot_] = std::move(tex);
    RegisterTextureAsset(debugName, p.string(), bighero::AssetMetadata::LoadState::Loaded, fileSize);
    LOG_INFO("glTF 贴图入池: 槽" << nextTextureSlot_ << " <- " << p.string());
    return nextTextureSlot_++;
}

void Application::UpdateObjectTextureDescriptors()
{
    using RDS = Render::FrameDescriptorSet;
    std::vector<VkDescriptorImageInfo> infos(Render::kObjectTextureSlots);
    for (uint32_t s = 0; s < Render::kObjectTextureSlots; ++s)
    {
        const Texture* tex = texturePool_[s].get();
        if (!tex || !tex->IsValid())
            tex = texture_.get();
        infos[s].sampler = tex->Sampler();
        infos[s].imageView = tex->View();
        infos[s].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    constexpr uint32_t kFrameCount = Renderer::MaxFramesInFlight();
    for (uint32_t fi = 0; fi < kFrameCount; ++fi)
        descManager_.UpdateSetImageArray(FrameSetIndex(fi, RDS::Light), 9, infos.data(), Render::kObjectTextureSlots);
}

void Application::LoadGltfAsset(uint32_t maxInstances)
{
    if (!std::filesystem::exists(kGltfModelPath))
    {
        LOG_INFO("未找到 " << kGltfModelPath << "，场景不含 glTF 模型");
        return;
    }

    try
    {
        gltfModel_ = Scene::LoadGltf(kGltfModelPath);
        Scene::GltfModel& gltf = gltfModel_;
        gltfMesh_.Create(ctx_, gltf.vertices, gltf.indices);

        RegisterMeshAsset("gltf:model", kGltfModelPath, gltf.vertices, gltf.indices,
                          bighero::AssetMetadata::LoadState::Loaded,
                          static_cast<uint64_t>(std::filesystem::file_size(kGltfModelPath)));

        // ---- 逐 primitive 材质解析：贴图 URI 相对 glTF 文件目录解引用，入纹理池 ----
        const std::string baseDir = std::filesystem::path(kGltfModelPath).parent_path().string();
        gltfPrims_.reserve(gltf.primitives.size());
        for (const Scene::GltfPrimitive& p : gltf.primitives)
        {
            GltfPrimMaterial pm;
            pm.firstIndex = p.firstIndex;
            pm.indexCount = p.indexCount;
            if (p.materialIndex >= 0 && p.materialIndex < static_cast<int32_t>(gltf.materials.size()))
            {
                const Scene::GltfMaterial& m = gltf.materials[static_cast<size_t>(p.materialIndex)];
                pm.baseColorFactor = m.baseColorFactor;
                pm.metallicFactor = m.metallicFactor;
                pm.roughnessFactor = m.roughnessFactor;
                // 无贴图回退：反照率/mr→纯白(2) 因子透传；法线→平坦(1)
                pm.texSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.baseColorTextureUri, true, 2, "gltf:baseColor#" + m.name));
                pm.normalSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.normalTextureUri, false, 1, "gltf:normal#" + m.name));
                pm.mrSlot = static_cast<int32_t>(
                    LoadTextureSlot(baseDir, m.metallicRoughnessTextureUri, false, 2, "gltf:mr#" + m.name));
                // 透明/自发光（glTF 2.0 core）：自发光贴图线性色彩空间，无 URI 保持 -1（仅因子生效）
                pm.alphaMode = m.alphaMode;
                pm.alphaCutoff = m.alphaCutoff;
                pm.emissiveFactor = m.emissiveFactor;
                if (!m.emissiveTextureUri.empty())
                    pm.emissiveSlot = static_cast<int32_t>(
                        LoadTextureSlot(baseDir, m.emissiveTextureUri, false, 2, "gltf:emissive#" + m.name));
            }

            // 批次包围盒中心（网格空间）：透明批次按相机深度排序用
            glm::vec3 bmin(1e9f), bmax(-1e9f);
            for (uint32_t i = 0; i < p.indexCount; ++i)
            {
                const glm::vec3 pos = gltf.vertices[gltf.indices[p.firstIndex + i]].pos;
                bmin = glm::min(bmin, pos);
                bmax = glm::max(bmax, pos);
            }
            pm.boundsCenter = (p.indexCount > 0) ? (bmin + bmax) * 0.5f : glm::vec3(0.0f);
            gltfPrims_.push_back(pm);
        }

        // ---- 逐 primitive 实例缓冲（每材质一次 DrawIndexedInstanced） ----
        gltfPrimInstances_.resize(gltfPrims_.size());
        gltfPrimCounts_.assign(gltfPrims_.size(), 0);
        for (Render::InstanceBuffer& ib : gltfPrimInstances_)
            ib.Create(ctx_, maxInstances);

        hasGltf_ = true;
        LOG_INFO("glTF 模型加载成功: " << gltfPrims_.size() << " 个材质批次 / " << gltf.vertices.size() << " 顶点 / "
                                       << gltf.indices.size() / 3 << " 三角形");

        // ---- 动画绑定（升级 24）：模型常驻 + 状态机绑定模型 + 状态绑到实际动画下标 ----
        if (!gltf.animations.empty())
        {
            animationHost_.Init();
            Scene::AnimationStateMachine& sm = animationHost_.StateMachine();
            sm.BindModel(&gltfModel_);
            const int animCount = static_cast<int>(gltf.animations.size());
            for (size_t i = 0; i < sm.StateCount(); ++i)
                sm.SetStateAnimation(static_cast<int>(i), static_cast<int>(i) % animCount);
            LOG_INFO("glTF 动画绑定: " << animCount << " 条动画 -> 状态机状态（前 "
                                      << std::min<size_t>(sm.StateCount(), gltf.animations.size()) << " 个状态）");
        }
    }
    catch (const std::exception& e)
    {
        hasGltf_ = false;
        gltfModel_ = Scene::GltfModel{};
        gltfPrims_.clear();
        gltfPrimInstances_.clear();
        gltfPrimCounts_.clear();
        LOG_ERROR("glTF 模型加载失败: " << e.what());
        RegisterMeshAsset("gltf:model", kGltfModelPath, {}, {}, bighero::AssetMetadata::LoadState::Failed,
                          std::filesystem::exists(kGltfModelPath)
                              ? static_cast<uint64_t>(std::filesystem::file_size(kGltfModelPath))
                              : 0);
    }
}

// ========================================================================
// glTF 透明/自发光录制辅助
// ========================================================================

Application::PushObject Application::MakeGltfPush(const GltfPrimMaterial& pm, int mode) const
{
    PushObject po;
    po.texIndex = pm.texSlot;
    po.normalIndex = pm.normalSlot;
    po.mrIndex = pm.mrSlot;
    po.emissiveIndex = pm.emissiveSlot;
    po.emissiveFactor = pm.emissiveFactor;
    // 仅 MASK 模式下传阈值（其余模式不裁剪）
    po.alphaCutoff = (pm.alphaMode == 1) ? pm.alphaCutoff : 0.0f;
    po.mode = mode;
    // 色调映射归属：后处理开或延迟链输出线性 HDR，由合成端统一 ACES；直通交换链片元内 ACES
    po.outputTarget = (renderer_.IsPostProcessing() || renderer_.IsDeferred()) ? 1 : 0;
    return po;
}

std::vector<uint32_t> Application::SortedGltfBlendPrims() const
{
    std::vector<uint32_t> order;
    if (!hasGltf_)
        return order;
    for (uint32_t p = 0; p < gltfPrims_.size(); ++p)
        if (gltfPrims_[p].alphaMode == 2 && gltfPrimCounts_[p] > 0)
            order.push_back(p);
    if (order.size() < 2)
        return order;

    // 所有 primitive 批次共享同一实例集合：取首个可见 glTF 实体的模型矩阵（UpdateRenderables
    // 单趟批次化时缓存于 firstGltfModel_，无可见实体时为单位矩阵），
    // 把批次包围中心变换到世界空间后按相机距离从远到近排序（批次级工程折衷）
    const glm::mat4 model = firstGltfModel_;
    const glm::vec3 camPos = camera_.Position();
    std::sort(order.begin(), order.end(),
              [this, &model, &camPos](uint32_t a, uint32_t b)
              {
                  const glm::vec3 wa = glm::vec3(model * glm::vec4(gltfPrims_[a].boundsCenter, 1.0f));
                  const glm::vec3 wb = glm::vec3(model * glm::vec4(gltfPrims_[b].boundsCenter, 1.0f));
                  return glm::distance(wa, camPos) > glm::distance(wb, camPos);
              });
    return order;
}

void Application::DrawGltfPrims(VkCommandBuffer cmd, Render::GraphicsPipeline& pipeline, int filter)
{
    if (!hasGltf_)
        return;

    // 批次顺序：BLEND 按相机深度远到近，其余保持 natural 顺序
    std::vector<uint32_t> order;
    if (filter == 2)
    {
        order = SortedGltfBlendPrims();
    }
    else
    {
        order.resize(gltfPrims_.size());
        for (uint32_t p = 0; p < gltfPrims_.size(); ++p)
            order[p] = p;
    }

    gltfMesh_.Bind(cmd);
    for (const uint32_t p : order)
    {
        const GltfPrimMaterial& pm = gltfPrims_[p];
        if (gltfPrimCounts_[p] == 0)
            continue;
        // filter：0=OPAQUE+MASK 2=BLEND 3=EMISSIVE_ONLY（有自发光的非 BLEND 批次）
        if (filter == 0 && pm.alphaMode > 1)
            continue;
        if (filter == 2 && pm.alphaMode != 2)
            continue;
        if (filter == 3)
        {
            const bool hasEmissive =
                pm.emissiveSlot >= 0 || glm::any(glm::greaterThan(pm.emissiveFactor, glm::vec3(0.0f)));
            if (pm.alphaMode == 2 || !hasEmissive)
                continue; // BLEND 批次随 mode2 全量着色（含自发光），勿重复叠加
        }

        gltfPrimInstances_[p].Bind(cmd);
        const PushObject po = MakeGltfPush(pm, (filter == 3) ? 3 : pm.alphaMode);
        vkCmdPushConstants(cmd, pipeline.GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject), &po);
        gltfMesh_.DrawIndexedInstanced(cmd, pm.indexCount, pm.firstIndex, gltfPrimCounts_[p]);
    }
}

} // namespace BigHero
