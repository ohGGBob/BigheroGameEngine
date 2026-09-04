// 动画系统（glTF 动画插值 / 骨骼蒙皮端到端管线 / 动画状态机）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
#include "framework/test_common.h"
#include "framework/test_gltf_helpers.h"
#include "render/Skinning.h"
#include "scene/Animation.h"
#include "scene/AnimationStateMachine.h"
#include "scene/GltfLoader.h"
#include "scene/Skeleton.h"
#include "scene/SkinnedMesh.h"

using namespace BigHero;

TEST_CASE("Anim.GltfAnimation")
{
    // ---- glTF 动画系统（纯CPU，LINEAR/STEP 插值） ----
    {
        using namespace BigHero::Scene;

        // 构造：1 节点，2 条采样器（rotation + translation），1 条动画。
        // 缓冲布局：
        //   [0..32)   rotation 关键帧 2 个（VEC4 quat，各16字节）: [1,0,0,0] 静止, [sin45,sin45,0,0] 绕X转90°
        //   [32..56)  translation 关键帧 2 个（VEC3，各12字节）: (0,0,0) -> (2,0,0)
        //   [56..64)  input 时间戳 2 个（SCALAR float）: 0.0, 1.0
        std::vector<unsigned char> abin;
        const auto appendF = [](std::vector<unsigned char>& v, float x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 4);
        };
        const auto b64enc = [](const std::vector<unsigned char>& bytes) -> std::string
        {
            static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((bytes.size() + 2) / 3) * 4);
            for (size_t i = 0; i < bytes.size(); i += 3)
            {
                const unsigned a = bytes[i];
                const unsigned b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
                const unsigned c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
                out += tbl[a >> 2];
                out += tbl[((a & 3) << 4) | (b >> 4)];
                out += (i + 1 < bytes.size()) ? tbl[((b & 0xF) << 2) | (c >> 6)] : '=';
                out += (i + 2 < bytes.size()) ? tbl[c & 0x3F] : '=';
            }
            return out;
        };
        // rotation 关键帧（glTF 存储 (x,y,z,w)）：静止 [0,0,0,1] 与 绕X转90° [s2,0,0,s2]
        const float s2 = std::sqrt(2.0f) * 0.5f;
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        appendF(abin, 1.0f);
        appendF(abin, s2);
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        appendF(abin, s2);
        // translation 关键帧：(0,0,0) -> (2,0,0)
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        appendF(abin, 2.0f);
        appendF(abin, 0.0f);
        appendF(abin, 0.0f);
        // input 时间戳：0.0, 1.0（两个采样器共用）
        appendF(abin, 0.0f);
        appendF(abin, 1.0f);

        const std::string aUri = "data:application/octet-stream;base64," + b64enc(abin);
        const std::string aGltf = std::string("{") + "\"asset\":{\"version\":\"2.0\"}," + "\"buffers\":[{\"uri\":\"" +
                                  aUri + "\",\"byteLength\":" + std::to_string(abin.size()) + "}]," +
                                  "\"bufferViews\":[" + "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":32}," +
                                  "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":24}," +
                                  "{\"buffer\":0,\"byteOffset\":56,\"byteLength\":8}" + "]," + "\"accessors\":[" +
                                  "{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}," +
                                  "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}," +
                                  "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}" + "]," +
                                  "\"nodes\":[{\"translation\":[0,0,0],\"rotation\":[0,0,0,1],\"scale\":[1,1,1]}]," +
                                  "\"animations\":[{" + "\"name\":\"TestAnim\"," + "\"samplers\":[" +
                                  "{\"input\":2,\"output\":0,\"interpolation\":\"LINEAR\"}," +
                                  "{\"input\":2,\"output\":1,\"interpolation\":\"LINEAR\"}" + "]," + "\"channels\":[" +
                                  "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}}," +
                                  "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"translation\"}}" + "]" + "}]," +
                                  "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":4}]}]" + "}";

        const GltfModel am = LoadGltfFromMemory(aGltf);
        CHECK(am.animations.size() == 1);
        CHECK(am.animations[0].name == "TestAnim");
        CHECK(am.animations[0].channels.size() == 2);
        CHECK(am.animations[0].samplers.size() == 2);
        // 采样器输入时间与输出值已解析
        CHECK(am.animations[0].samplers[0].times.size() == 2);
        CHECK(std::fabs(am.animations[0].samplers[0].times[1] - 1.0f) < 1e-5f);
        CHECK(am.animations[0].samplers[0].values.size() == 2);
        CHECK(glm::distance(am.animations[0].samplers[0].values[1], glm::vec4(s2, 0, 0, s2)) < 1e-5f);

        const AnimationPlayer player(am);
        CHECK(player.IsValid());
        CHECK(player.AnimationCount() == 1);
        CHECK(std::fabs(player.Duration() - 1.0f) < 1e-5f);

        std::vector<glm::vec3> T;
        std::vector<glm::quat> R;
        std::vector<glm::vec3> S;

        // t=0：静止旋转，平移 (0,0,0)
        player.Sample(0.0f, false, T, R, S);
        CHECK(T.size() == 1);
        CHECK(glm::distance(T[0], glm::vec3(0, 0, 0)) < 1e-4f);
        CHECK(std::fabs(R[0].w - 1.0f) < 1e-4f && std::fabs(R[0].x) < 1e-4f);

        // t=1：绕X转90°（w=cos45, x=sin45），平移 (2,0,0)
        player.Sample(1.0f, false, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(2, 0, 0)) < 1e-4f);
        CHECK(std::fabs(R[0].w - s2) < 1e-4f);
        CHECK(std::fabs(R[0].x - s2) < 1e-4f);
        CHECK(std::fabs(R[0].y) < 1e-4f);

        // t=0.5：slerp 到 45°（绕X转45°：w=cos22.5, x=sin22.5），平移 lerp 到 (1,0,0)
        player.Sample(0.5f, false, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(1, 0, 0)) < 1e-4f);
        const float c225 = std::cos(glm::radians(22.5f));
        const float s225 = std::sin(glm::radians(22.5f));
        CHECK(std::fabs(R[0].w - c225) < 1e-3f);
        CHECK(std::fabs(R[0].x - s225) < 1e-3f);
        CHECK(std::fabs(R[0].y) < 1e-3f);

        // loop=true：t=1.5 回绕到 0.5，平移 (1,0,0)
        player.Sample(1.5f, true, T, R, S);
        CHECK(glm::distance(T[0], glm::vec3(1, 0, 0)) < 1e-4f);

        // 越界动画索引 -> IsValid()==false，Sample 保持模型默认值
        const AnimationPlayer bad(am, 99);
        CHECK(!bad.IsValid());
        std::vector<glm::vec3> bT, bS;
        std::vector<glm::quat> bR;
        bad.Sample(0.0f, false, bT, bR, bS);
        CHECK(bT.size() == 1);
        CHECK(glm::distance(bT[0], glm::vec3(0, 0, 0)) < 1e-4f); // 默认平移
    }
}

TEST_CASE("Anim.SkinnedPipeline")
{
    // ---- 骨骼动画端到端管线（SkinnedMesh：动画采样 -> 皮肤矩阵 -> 蒙皮顶点） ----
    {
        using namespace BigHero::Scene;

        // 3 节点层级：node0(根) -> node1(关节0) -> node2(关节1)，各带 T(0,1,0)。
        // 动画只驱动 node1 的 translation：(0,1,0) -> (0,3,0)；
        // 因层级级联，node2 被父节点带动：(0,2,0) -> (0,4,0)。
        // 蒙皮顶点在原点、权重 (0.5,0.5)：
        //   t=0   -> 0.5*(0,1,0)+0.5*(0,2,0) = (0,1.5,0)
        //   t=1   -> 0.5*(0,3,0)+0.5*(0,4,0) = (0,3.5,0)
        std::vector<unsigned char> bin;
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 0.0f); // POSITION
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 1.0f); // NORMAL
        bin.push_back(0);
        bin.push_back(1);
        bin.push_back(0);
        bin.push_back(0); // JOINTS_0
        AppendFloat(bin, 0.5f);
        AppendFloat(bin, 0.5f);
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 0.0f);     // WEIGHTS_0
        for (int j = 0; j < 2; ++j) // 2 个单位逆绑定矩阵（列主序单位阵）
            for (int k = 0; k < 16; ++k)
                AppendFloat(bin, (k % 5 == 0) ? 1.0f : 0.0f);
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 1.0f); // 动画时间 [0,1]
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 1.0f);
        AppendFloat(bin, 0.0f); // 关键帧0 (0,1,0)
        AppendFloat(bin, 0.0f);
        AppendFloat(bin, 3.0f);
        AppendFloat(bin, 0.0f); // 关键帧1 (0,3,0)

        // bufferViews 偏移：POSITION(0,12) NORMAL(12,12) JOINTS(24,4) WEIGHTS(28,16)
        //                   IBM(44,128) 时间(172,8) 采样值(180,24)
        const std::string uri = "data:application/octet-stream;base64," + B64Encode(bin);
        const std::string gltf = std::string("{") + "\"asset\":{\"version\":\"2.0\"}," + "\"buffers\":[{\"uri\":\"" +
                                 uri + "\",\"byteLength\":" + std::to_string(bin.size()) + "}]," + "\"bufferViews\":[" +
                                 "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}," +
                                 "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":12}," +
                                 "{\"buffer\":0,\"byteOffset\":24,\"byteLength\":4}," +
                                 "{\"buffer\":0,\"byteOffset\":28,\"byteLength\":16}," +
                                 "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":128}," +
                                 "{\"buffer\":0,\"byteOffset\":172,\"byteLength\":8}," +
                                 "{\"buffer\":0,\"byteOffset\":180,\"byteLength\":24}" + "]," + "\"accessors\":[" +
                                 "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}," +
                                 "{\"bufferView\":1,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}," +
                                 "{\"bufferView\":2,\"componentType\":5121,\"count\":1,\"type\":\"VEC4\"}," +
                                 "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"VEC4\"}," +
                                 "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}," +
                                 "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}," +
                                 "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}" + "]," +
                                 "\"nodes\":[" + "{\"mesh\":0,\"children\":[1]}," +
                                 "{\"translation\":[0,1,0],\"children\":[2]}," + "{\"translation\":[0,1,0]}" + "]," +
                                 "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":4}]," + "\"animations\":[{" +
                                 "\"name\":\"Move\"," +
                                 "\"samplers\":[{\"input\":5,\"output\":6,\"interpolation\":\"LINEAR\"}]," +
                                 "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]" +
                                 "}]," + "\"meshes\":[{\"primitives\":[{" +
                                 "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3}," +
                                 "\"mode\":4" + "}]}]" + "}";

        const GltfModel m = LoadGltfFromMemory(gltf);
        CHECK(m.animations.size() == 1);
        CHECK(m.jointNodes.size() == 2);

        const SkinnedMesh skinned(m);
        CHECK(skinned.HasSkeleton());
        CHECK(skinned.AnimationCount() == 1);
        CHECK(skinned.VertexCount() == 1);

        std::vector<glm::vec3> pos, nrm;

        // 绑定姿态（无动画）：(0,1.5,0)
        skinned.EvaluateBind(pos, nrm);
        CHECK(pos.size() == 1);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);

        // t=0：关键帧起点，与绑定姿态一致
        skinned.Evaluate(0, 0.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);

        // t=1：node1 升到 (0,3,0)，连带 node2 到 (0,4,0) -> 顶点 (0,3.5,0)
        skinned.Evaluate(0, 1.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.5f, 0)) < 1e-3f);

        // t=0.5：线性插值 node1=(0,2,0)、node2=(0,3,0) -> 顶点 (0,2.5,0)
        skinned.Evaluate(0, 0.5f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 2.5f, 0)) < 1e-3f);

        // 动画下标越界 -> 回退绑定姿态
        skinned.Evaluate(99, 1.0f, false, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);
        // 法线仍为单位长度（蒙皮后归一化）
        CHECK(std::fabs(glm::length(nrm[0]) - 1.0f) < 1e-3f);

        // ---- AnimationState：时间推进 / 速度 / 暂停 / 重置 ----
        AnimationState st;
        CHECK(st.time == 0.0f);
        st.Advance(0.5f);
        CHECK(std::fabs(st.time - 0.5f) < 1e-6f);
        st.speed = 2.0f;
        st.Advance(0.5f);
        CHECK(std::fabs(st.time - 1.5f) < 1e-6f); // 速度倍率生效
        st.playing = false;
        st.Advance(1.0f);
        CHECK(std::fabs(st.time - 1.5f) < 1e-6f); // 暂停不推进
        st.playing = true;
        st.Reset();
        CHECK(st.time == 0.0f);

        // ---- AnimationBlender：多动画加权混合 ----
        // 同一动画在 t=0 与 t=1 各占 50% 权重，混合后 node1=(0,2,0) -> 顶点 (0,2.5,0)
        AnimationBlender blender(m);
        blender.AddLayer(0, 1.0f, 0.0f);
        blender.AddLayer(0, 1.0f, 1.0f);
        CHECK(blender.LayerCount() == 2);
        std::vector<glm::vec3> bt, bs;
        std::vector<glm::quat> br;
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 2.5f, 0)) < 1e-3f);

        // 权重归一化：单层权重 2.0 等价于权重 1，结果为 t=1 的姿态
        blender.Clear();
        CHECK(blender.LayerCount() == 0);
        blender.AddLayer(0, 2.0f, 1.0f);
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.5f, 0)) < 1e-3f);

        // 非等权混合：t=1 占 3/4、t=0 占 1/4 -> node1=(0,2.5,0) -> 顶点 (0,3,0)
        blender.Clear();
        blender.AddLayer(0, 1.0f, 0.0f);
        blender.AddLayer(0, 3.0f, 1.0f);
        blender.Sample(false, bt, br, bs);
        skinned.EvaluatePose(bt, br, bs, pos, nrm);
        CHECK(glm::distance(pos[0], glm::vec3(0, 3.0f, 0)) < 1e-3f);

        // 越界动画下标 / 非正权重被忽略，不产生层
        AnimationBlender empty(m);
        empty.AddLayer(99, 1.0f, 0.0f);
        empty.AddLayer(0, 0.0f, 0.0f);
        CHECK(empty.LayerCount() == 0);

        // ---- GPU 蒙皮：骨骼调色板布局与蒙皮顶点布局 ----
        // std140：mat4[128] 紧密排布，数组步长 64 字节，可整体 memcpy 上传
        CHECK(sizeof(Render::SkinningUBO) == 128 * 64);
        CHECK(Render::GetUboByteSize<Render::SkinningUBO>() == sizeof(Render::SkinningUBO));
        CHECK(offsetof(Render::SkinningUBO, boneMatrices[1]) - offsetof(Render::SkinningUBO, boneMatrices[0]) == 64);

        // 蒙皮顶点布局：前 5 属性复用基础顶点，权重/关节占 location 11/12
        const auto skAttrs = Render::SkinnedVertex::getAttrDesc();
        CHECK(skAttrs.size() == 7);
        CHECK(Render::SkinnedVertex::getBindingDesc().stride == sizeof(Render::SkinnedVertex));
        CHECK(skAttrs[0].location == 0);
        CHECK(skAttrs[4].location == 4);
        CHECK(skAttrs[5].location == 11);
        CHECK(skAttrs[6].location == 12);
        CHECK(skAttrs[5].format == VK_FORMAT_R32G32B32A32_SFLOAT); // weights
        CHECK(skAttrs[6].format == VK_FORMAT_R8G8B8A8_UINT);       // joints
        // 前几个属性偏移与 Scene::Vertex 完全一致，便于复用同一片段着色器
        CHECK(offsetof(Render::SkinnedVertex, pos) == offsetof(Vertex, pos));
        CHECK(offsetof(Render::SkinnedVertex, normal) == offsetof(Vertex, normal));
        CHECK(offsetof(Render::SkinnedVertex, uv) == offsetof(Vertex, uv));
        CHECK(offsetof(Render::SkinnedVertex, tangent) == offsetof(Vertex, tangent));

        // 调色板：默认全单位矩阵（等价绑定姿态、无变形）
        Render::SkinningPalette palette;
        CHECK(palette.MaxBones() == 128);
        CHECK(palette.Data().boneMatrices[0] == glm::mat4(1.0f));
        CHECK(palette.Data().boneMatrices[127] == glm::mat4(1.0f));

        // SetBone / 越界保护
        const glm::mat4 t2 = glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0));
        CHECK(palette.SetBone(1, t2));
        CHECK(palette.Data().boneMatrices[1] == t2);
        CHECK(!palette.SetBone(128, t2)); // 越界返回 false

        // SetBones 批量 + 超限保护（超限时不修改任何内容）
        CHECK(palette.SetBones(std::vector<glm::mat4>{t2, t2}));
        CHECK(!palette.SetBones(std::vector<glm::mat4>(129, t2)));

        // SetFromMesh：CPU 姿态 -> GPU 调色板（t=1 时关节应已动画到位）
        Render::SkinningPalette meshPal;
        CHECK(meshPal.SetFromMesh(skinned, 0, 1.0f, false));
        // 关节0（node1）全局 = T(0,3,0)；关节1（node2）被父节点带动 = T(0,4,0)
        CHECK(glm::distance(glm::vec3(meshPal.Data().boneMatrices[0][3]), glm::vec3(0, 3, 0)) < 1e-3f);
        CHECK(glm::distance(glm::vec3(meshPal.Data().boneMatrices[1][3]), glm::vec3(0, 4, 0)) < 1e-3f);
        // 未使用的槽位保持单位矩阵
        CHECK(meshPal.Data().boneMatrices[2] == glm::mat4(1.0f));
    }
}

TEST_CASE("Anim.StateMachine")
{
    // ---- 动画状态机 ----
    {
        using namespace BigHero::Scene;

        AnimationStateMachine sm;
        const int idle = sm.AddState("Idle", -1, 1.0f, true);
        const int walk = sm.AddState("Walk", -1, 1.0f, true);
        const int jump = sm.AddState("Jump", -1, 1.0f, false);

        CHECK(sm.StateCount() == 3);
        CHECK(sm.CurrentState() == -1);

        sm.SetInitialState(idle);
        CHECK(sm.CurrentState() == idle);
        CHECK(std::string(sm.CurrentStateName()) == "Idle");

        // 参数设置与读取
        sm.SetFloat("Speed", 0.0f);
        sm.SetBool("Grounded", true);
        CHECK(std::fabs(sm.GetFloat("Speed") - 0.0f) < 1e-5f);
        CHECK(sm.GetBool("Grounded") == true);

        // 过渡：Idle -> Walk (Speed > 0.5)
        sm.AddTransition(idle, walk, 0.20f, {{"Speed", AnimConditionType::FloatGreater, 0.5f}});
        sm.AddTransition(walk, idle, 0.20f, {{"Speed", AnimConditionType::FloatLess, 0.5f}});

        // 速度不满足条件，不应过渡
        sm.SetFloat("Speed", 0.3f);
        sm.Update(0.016f);
        CHECK(sm.CurrentState() == idle);
        CHECK(sm.IsTransitioning() == false);

        // 速度满足条件，应开始过渡
        sm.SetFloat("Speed", 2.0f);
        sm.Update(0.016f);
        CHECK(sm.IsTransitioning() == true);

        // 过渡期间不应再次评估新过渡（设 Speed=0，若评估会立即切回 Idle）
        sm.SetFloat("Speed", 0.0f);
        sm.Update(0.016f);
        CHECK(sm.IsTransitioning() == true);

        // 恢复速度，避免过渡完成后立即触发 Walk->Idle
        sm.SetFloat("Speed", 2.0f);
        // 过渡完成后进入 Walk
        sm.Update(1.0f); // 足够长时间完成 0.2s 过渡
        CHECK(sm.IsTransitioning() == false);
        CHECK(sm.CurrentState() == walk);
        CHECK(std::string(sm.CurrentStateName()) == "Walk");

        // Walk -> Idle
        sm.SetFloat("Speed", 0.1f);
        sm.Update(0.016f);
        CHECK(sm.IsTransitioning() == true);
        sm.Update(1.0f);
        CHECK(sm.CurrentState() == idle);

        // Any State 过渡：Jump trigger
        sm.AddTransition(-1, jump, 0.15f, {{"Jump", AnimConditionType::Trigger}});
        sm.SetTrigger("Jump");
        sm.Update(0.016f);
        CHECK(sm.IsTransitioning() == true);
        sm.Update(1.0f);
        CHECK(sm.CurrentState() == jump);

        // Trigger 消费后不应重复触发
        sm.Update(0.016f);
        CHECK(sm.CurrentState() == jump); // 仍在 Jump（无 Jump->Idle 过渡）

        // Bool 条件
        AnimationStateMachine sm2;
        const int a = sm2.AddState("A", -1);
        const int b = sm2.AddState("B", -1);
        sm2.AddTransition(a, b, 0.1f, {{"Flag", AnimConditionType::BoolTrue}});
        sm2.SetInitialState(a);
        sm2.SetBool("Flag", false);
        sm2.Update(0.016f);
        CHECK(sm2.CurrentState() == a);
        sm2.SetBool("Flag", true);
        sm2.Update(0.016f);
        sm2.Update(1.0f);
        CHECK(sm2.CurrentState() == b);

        // 退出时间：未达到退出时间不应过渡
        AnimationStateMachine sm3;
        const int s0 = sm3.AddState("S0", -1, 1.0f, true);
        const int s1 = sm3.AddState("S1", -1, 1.0f, true);
        sm3.AddTransitionWithExit(s0, s1, 0.1f, 0.9f, {});
        sm3.SetInitialState(s0);
        // animationIndex=-1 时 duration=0，normTime=0，永远不满足 exitTime
        sm3.Update(1.0f);
        CHECK(sm3.CurrentState() == s0);
    }
}
