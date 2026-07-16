#pragma once
#include <glm/glm.hpp>
#include <cstddef>

namespace BigHero::Render
{
    struct CameraUBO
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;

        CameraUBO() : model(1.0f), view(1.0f), proj(1.0f) {}
    };
    inline constexpr size_t CameraUBO_ByteSize = sizeof(CameraUBO);

    struct LightUBO
    {
        glm::vec3 lightDir;
        float padLightDir;

        glm::vec3 lightColor;
        float padLightColor;

        glm::vec3 cameraPos;
        float ambientFactor;

        float specPower;
        float specStrength;
        float padSpecA;
        float padSpecB;

        LightUBO()
            : lightDir(0.f), padLightDir(0.f),
            lightColor(1.f), padLightColor(0.f),
            cameraPos(0.f), ambientFactor(0.1f),
            specPower(32.f), specStrength(1.f), padSpecA(0.f), padSpecB(0.f)
        {
        }
    };
    inline constexpr size_t LightUBO_ByteSize = sizeof(LightUBO);

    template<typename T>
    constexpr size_t GetUboByteSize();

    template<>
    constexpr size_t GetUboByteSize<CameraUBO>()
    {
        return CameraUBO_ByteSize;
    }

    template<>
    constexpr size_t GetUboByteSize<LightUBO>()
    {
        return LightUBO_ByteSize;
    }
}