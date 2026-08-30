#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace BigHero::Scene
{
    // 场景物体定义（共用网格资源，模型矩阵+着色由实例决定）
    struct SceneObject
    {
        glm::vec3 position;   // 物体中心（世界空间，y=0.5*scale时底面贴地）
        float scale;          // 均匀缩放
        glm::vec3 tint;       // 顶点色乘数（经推送常量下传）
        float spinSpeed;      // 绕Y轴自转速度（度/秒）
        float phase;          // 初始相位（度）
        uint32_t meshId = 0;  // 0=共享立方体网格 1=外部加载模型（assets/models/torus.obj）
    };

    // 默认演示场景：中央大立方体 + 四周四个立方体 + 悬浮圆环体（模型文件缺失时由调用方剔除）
    inline std::vector<SceneObject> BuildDefaultScene()
    {
        return {
            { { 0.0f, 0.5f,  0.0f}, 1.0f, {1.0f, 1.0f, 1.0f},  30.0f,   0.0f, 0 },
            { { 2.2f, 0.75f, -0.8f}, 1.5f, {0.60f, 0.78f, 1.0f}, -18.0f,  40.0f, 0 },
            { {-2.4f, 0.35f,  1.2f}, 0.7f, {1.0f, 0.65f, 0.45f},  55.0f, 120.0f, 0 },
            { { 1.6f, 0.35f,  2.1f}, 0.7f, {0.60f, 1.0f, 0.68f},  42.0f, 200.0f, 0 },
            { {-1.6f, 1.1f, -2.3f}, 2.2f, {0.88f, 0.60f, 0.98f},  10.0f, 300.0f, 0 },
            { { 0.0f, 2.6f,  0.9f}, 0.9f, {1.0f, 0.85f, 0.55f},  24.0f,  60.0f, 1 }
        };
    }
}
