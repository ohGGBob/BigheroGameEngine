// 资产加载（MTL 材质解析 / glTF 2.0 加载器）单元测试。
// 2026-09-04 测试工程化重构：由单体 test_main.cpp 拆分而来，每个原分区封装为独立 TEST_CASE。
#include "framework/test_common.h"
#include "scene/GltfLoader.h"
#include "scene/MtlMaterial.h"

using namespace BigHero;

TEST_CASE("Assets.MtlMaterial")
{
    // ---- Wavefront .mtl 材质解析（纯CPU） ----
    {
        using namespace Scene;

        const std::string mtl = "# 测试材质库\n"
                                "newmtl Gold\n"
                                "Ka 0.1 0.1 0.1\n"
                                "Kd 1.0 0.8 0.3\n"
                                "Ks 0.6 0.5 0.2\n"
                                "Ns 128\n"
                                "d 1.0\n"
                                "illum 2\n"
                                "map_Kd gold_albedo.png\n"
                                "\n"
                                "newmtl Matte\n"
                                "Kd 0.5 0.5 0.5\n"
                                "Ns 4\n"
                                "Tr 0.4\n";

        const std::vector<MtlMaterial> mats = ParseMtl(mtl);
        CHECK(mats.size() == 2);
        CHECK(mats[0].name == "Gold");
        CHECK(glm::distance(mats[0].diffuse, glm::vec3(1.0f, 0.8f, 0.3f)) < 1e-4f);
        CHECK(glm::distance(mats[0].specular, glm::vec3(0.6f, 0.5f, 0.2f)) < 1e-4f);
        CHECK(std::fabs(mats[0].shininess - 128.0f) < 1e-4f);
        CHECK(mats[0].opacity == 1.0f);
        CHECK(mats[0].mapKd == "gold_albedo.png");
        CHECK(mats[0].HasMapKd());

        CHECK(mats[1].name == "Matte");
        CHECK(std::fabs(mats[1].diffuse.r - 0.5f) < 1e-4f);
        // Tr 0.4 -> opacity = 0.6
        CHECK(std::fabs(mats[1].opacity - 0.6f) < 1e-4f);

        // 空/纯注释文件不抛异常，返回空
        CHECK(ParseMtl("").empty());
        CHECK(ParseMtl("# only a comment\n").empty());

        // usemtl 前面聚合子网格：faceMaterials 每 3 索引一条三角形
        const std::vector<std::string> faceMats = {"Gold", "Gold", "Matte", "Matte", "Gold"};
        const auto sub = GroupFacesByMaterial(faceMats, mats);
        // Gold(2三角) -> Matte(2三角) -> Gold(1三角)：3 个子网格
        CHECK(sub.size() == 3);
        CHECK(sub[0].materialIndex == 0);
        CHECK(sub[0].firstIndex == 0);
        CHECK(sub[0].indexCount == 6); // 2 三角形
        CHECK(sub[1].materialIndex == 1);
        CHECK(sub[1].firstIndex == 6);
        CHECK(sub[1].indexCount == 6);
        CHECK(sub[2].materialIndex == 0);
        CHECK(sub[2].firstIndex == 12);
        CHECK(sub[2].indexCount == 3);

        // 未知名材质 -> materialIndex == -1（视为未指定）
        const std::vector<std::string> unknownFace = {"Nope"};
        const auto u = GroupFacesByMaterial(unknownFace, mats);
        CHECK(u.size() == 1 && u[0].materialIndex == -1);
    }
}

TEST_CASE("Assets.GltfLoader")
{
    // ---- glTF 2.0 加载器（纯CPU，base64 内嵌缓冲） ----
    {
        using namespace BigHero::Scene;

        // base64 编码辅助（构造 data URI 用）
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
        const auto appendF = [](std::vector<unsigned char>& v, float x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 4);
        };
        const auto appendU16 = [](std::vector<unsigned char>& v, uint16_t x)
        {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(&x);
            v.insert(v.end(), p, p + 2);
        };

        // 构造三角形：3 顶点（POSITION/NORMAL VEC3 float，TEXCOORD_0 VEC2 float）+ 3 索引 UINT16
        std::vector<unsigned char> bin;
        // positions
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        appendF(bin, 0.0f);
        // normals
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        // uvs
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        appendF(bin, 0.0f);
        appendF(bin, 0.0f);
        appendF(bin, 1.0f);
        // indices (UINT16)
        appendU16(bin, 0);
        appendU16(bin, 1);
        appendU16(bin, 2);

        const std::string dataUri = "data:application/octet-stream;base64," + b64enc(bin);

        const std::string gltf =
            std::string("{") + "\"asset\":{\"version\":\"2.0\"}," + "\"buffers\":[{\"uri\":\"" + dataUri +
            "\",\"byteLength\":" + std::to_string(bin.size()) + "}]," + "\"bufferViews\":[" +
            "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}," +
            "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36}," +
            "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24}," +
            "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}" + "]," + "\"accessors\":[" +
            "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}," +
            "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}," +
            "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}," +
            "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}" + "]," +
            "\"meshes\":[{\"primitives\":[{" + "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2}," +
            "\"indices\":3,\"mode\":4" + "}]}]," +
            "\"materials\":[{\"name\":\"Red\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}}]," +
            "\"nodes\":[{\"mesh\":0}]" + "}";

        const GltfModel m = LoadGltfFromMemory(gltf);

        // 几何：3 顶点、3 索引、1 子网格
        CHECK(m.vertices.size() == 3);
        CHECK(m.indices.size() == 3);
        CHECK(m.primitives.size() == 1);
        CHECK(m.primitives[0].firstIndex == 0);
        CHECK(m.primitives[0].indexCount == 3);
        CHECK(m.primitives[0].materialIndex == -1); // primitive 未指定 material

        // 顶点位置
        CHECK(glm::distance(m.vertices[0].pos, glm::vec3(0, 0, 0)) < 1e-5f);
        CHECK(glm::distance(m.vertices[1].pos, glm::vec3(1, 0, 0)) < 1e-5f);
        CHECK(glm::distance(m.vertices[2].pos, glm::vec3(0, 1, 0)) < 1e-5f);
        // 法线 +Z
        CHECK(glm::distance(m.vertices[0].normal, glm::vec3(0, 0, 1)) < 1e-5f);
        // UV
        CHECK(glm::distance(m.vertices[2].uv, glm::vec2(0, 1)) < 1e-5f);
        // 缺失顶点色 -> 白
        CHECK(glm::distance(m.vertices[0].color, glm::vec3(1)) < 1e-5f);
        // 索引
        CHECK(m.indices[0] == 0 && m.indices[1] == 1 && m.indices[2] == 2);

        // 材质解析
        CHECK(m.materials.size() == 1);
        CHECK(m.materials[0].name == "Red");
        CHECK(glm::distance(m.materials[0].baseColorFactor, glm::vec4(1, 0, 0, 1)) < 1e-5f);

        // ---- 使用原始几何推导切线：退化为 +X（无 TANGENT、UV 与位置相关） ----
        // 仅验证不崩溃

        // ---- 非法版本应抛异常 ----
        const std::string badVer = std::string("{") + "\"asset\":{\"version\":\"1.0\"},\"meshes\":[]" + "}";
        bool threw = false;
        try
        {
            LoadGltfFromMemory(badVer);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        CHECK(threw);

        // ---- 骨骼蒙皮：2 关节（3 节点）层级 + 1 蒙皮顶点 ----
        {
            // 矩阵逐元素比较辅助（glm 对 mat4 无 distance）
            const auto matClose = [](const glm::mat4& a, const glm::mat4& b, float eps) -> bool
            {
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        if (std::fabs(a[c][r] - b[c][r]) > eps)
                            return false;
                return true;
            };
            // 构建缓冲：
            //   [0..12)   POSITION  (0,0,0)   VEC3 float
            //   [12..24)  NORMAL    (0,0,1)   VEC3 float
            //   [24..28)  JOINTS_0  [0,1,0,0] VEC4 u8
            //   [28..44)  WEIGHTS_0 [0.5,0.5,0,0] VEC4 float
            //   [44..172) inverseBindMatrices 2 个单位 MAT4（128 字节）
            std::vector<unsigned char> sbin;
            appendF(sbin, 0.0f);
            appendF(sbin, 0.0f);
            appendF(sbin, 0.0f); // pos
            appendF(sbin, 0.0f);
            appendF(sbin, 0.0f);
            appendF(sbin, 1.0f); // normal
            sbin.push_back(0);
            sbin.push_back(1);
            sbin.push_back(0);
            sbin.push_back(0); // joints
            appendF(sbin, 0.5f);
            appendF(sbin, 0.5f);
            appendF(sbin, 0.0f);
            appendF(sbin, 0.0f); // weights
            // 2 个单位逆绑定矩阵（列主序单位阵 16 float）
            for (int j = 0; j < 2; ++j)
            {
                for (int k = 0; k < 16; ++k)
                    appendF(sbin, (k % 5 == 0) ? 1.0f : 0.0f); // 对角线 1（k%5==0: 0,5,10,15）
            }

            const std::string sUri = "data:application/octet-stream;base64," + b64enc(sbin);
            const std::string sGltf =
                std::string("{") + "\"asset\":{\"version\":\"2.0\"}," + "\"buffers\":[{\"uri\":\"" + sUri +
                "\",\"byteLength\":" + std::to_string(sbin.size()) + "}]," + "\"bufferViews\":[" +
                "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}," +
                "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":12}," +
                "{\"buffer\":0,\"byteOffset\":24,\"byteLength\":4}," +
                "{\"buffer\":0,\"byteOffset\":28,\"byteLength\":16}," +
                "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":128}" + "]," + "\"accessors\":[" +
                "{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}," +
                "{\"bufferView\":1,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"}," +
                "{\"bufferView\":2,\"componentType\":5121,\"count\":1,\"type\":\"VEC4\"}," +
                "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"VEC4\"}," +
                "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}" + "]," + "\"nodes\":[" +
                "{\"mesh\":0,\"children\":[1]}," + "{\"translation\":[0,1,0],\"children\":[2]}," +
                "{\"translation\":[0,1,0]}" + "]," + "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":4}]," +
                "\"meshes\":[{\"primitives\":[{" +
                "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3}," + "\"mode\":4" + "}]}]" +
                "}";

            const GltfModel sm = LoadGltfFromMemory(sGltf);

            // 节点层级：node1 父=0，node2 父=1
            CHECK(sm.nodeParents.size() == 3);
            CHECK(sm.nodeParents[0] == -1);
            CHECK(sm.nodeParents[1] == 0);
            CHECK(sm.nodeParents[2] == 1);
            // 关节与逆绑定矩阵
            CHECK(sm.jointNodes.size() == 2);
            CHECK(sm.jointNodes[0] == 1 && sm.jointNodes[1] == 2);
            CHECK(sm.inverseBindMatrices.size() == 2);
            // 逆绑定矩阵为单位阵
            CHECK(matClose(sm.inverseBindMatrices[0], glm::mat4(1.0f), 1e-5f));
            // 蒙皮顶点数据
            CHECK(sm.jointIndices.size() == 1);
            CHECK(sm.jointWeights.size() == 1);
            CHECK(sm.jointIndices[0] == glm::u8vec4(0, 1, 0, 0));
            CHECK(glm::distance(sm.jointWeights[0], glm::vec4(0.5f, 0.5f, 0, 0)) < 1e-5f);

            // Skeleton 计算
            const Skeleton skel(sm);
            CHECK(skel.HasSkin());
            CHECK(skel.JointCount() == 2);

            std::vector<glm::mat4> jointGlobal;
            skel.ComputeGlobalJointMatrices(jointGlobal);
            CHECK(jointGlobal.size() == 2);
            // node1 全局 = T(0,1,0)，node2 全局 = T(0,1,0)*T(0,1,0)=T(0,2,0)
            CHECK(matClose(jointGlobal[0], glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)), 1e-4f));
            CHECK(matClose(jointGlobal[1], glm::translate(glm::mat4(1.0f), glm::vec3(0, 2, 0)), 1e-4f));

            // 皮肤矩阵 = 全局 * 逆绑定（单位阵）-> 等于全局
            std::vector<glm::mat4> skinMat;
            skel.ComputeSkinMatrices(skinMat);
            CHECK(skinMat.size() == 2);
            CHECK(matClose(skinMat[0], jointGlobal[0], 1e-4f));

            // CPU 蒙皮：(0,0,0) 顶点，权重 (0.5,0.5) -> (0,1.5,0)
            const std::vector<glm::vec3> pIn = {glm::vec3(0, 0, 0)};
            const std::vector<glm::vec3> nIn = {glm::vec3(0, 0, 1)};
            std::vector<glm::vec3> pOut, nOut;
            skel.SkinVertices(sm.jointIndices, sm.jointWeights, pIn, nIn, pOut, nOut);
            CHECK(glm::distance(pOut[0], glm::vec3(0, 1.5f, 0)) < 1e-3f);
        }
    }
}
