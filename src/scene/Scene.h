#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace BigHero::Scene
{
    // 场景物体定义（共用网格资源，模型矩阵+材质由实例决定）
    struct SceneObject
    {
        glm::vec3 position;    // 物体中心（世界空间，y=0.5*scale时底面贴地）
        float scale;           // 均匀缩放
        glm::vec3 tint;        // 反照率乘数（经推送常量下传）
        float spinSpeed;       // 绕Y轴自转速度（度/秒）
        float phase;           // 初始相位（度）
        uint32_t meshId = 0;   // 0=共享立方体网格 1=外部加载模型（assets/models/torus.obj）
        float metallic = 0.0f; // PBR金属度 0电介质~1金属
        float roughness = 0.5f;// PBR粗糙度 0镜面~1粗糙
        glm::vec3 rotation{ 0.0f }; // Gizmo 手动旋转（欧拉 XYZ，度），与自转叠加
    };

    // 默认演示场景：PBR材质展示（电介质/金属/粗糙度渐变）+ 悬浮金属圆环
    inline std::vector<SceneObject> BuildDefaultScene()
    {
        return {
            { { 0.0f, 0.5f,  0.0f}, 1.0f, {1.0f, 1.0f, 1.0f},  30.0f,   0.0f, 0, 0.0f, 0.15f },
            { { 2.2f, 0.75f, -0.8f}, 1.5f, {0.60f, 0.78f, 1.0f}, -18.0f,  40.0f, 0, 0.0f, 0.55f },
            { {-2.4f, 0.35f,  1.2f}, 0.7f, {1.0f, 0.60f, 0.40f},  55.0f, 120.0f, 0, 1.0f, 0.25f },
            { { 1.6f, 0.35f,  2.1f}, 0.7f, {0.60f, 1.0f, 0.68f},  42.0f, 200.0f, 0, 0.0f, 0.85f },
            { {-1.6f, 1.1f, -2.3f}, 2.2f, {0.88f, 0.60f, 0.98f},  10.0f, 300.0f, 0, 0.3f, 0.40f },
            { { 0.0f, 2.6f,  0.9f}, 0.9f, {1.0f, 0.77f, 0.34f},  24.0f,  60.0f, 1, 1.0f, 0.20f }
        };
    }
}
