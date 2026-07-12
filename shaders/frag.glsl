#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 outWorldPos;
layout(location = 1) in vec3 outNormal;
layout(location = 2) in nonperspective vec2 outUV;
layout(location = 3) in vec3 outVertColor;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0, std140) uniform LightUBO {
    vec3 lightDir;
    vec3 lightColor;
    vec3 cameraPos;
    float ambientFactor;
    float specPower;
    float specStrength;
} lightUbo;

void main()
{
    // 法线零向量安全容错
    vec3 normal = outNormal;
    float nLen = length(normal);
    if(nLen < 0.0001)
        normal = vec3(0.0, 1.0, 0.0);
    vec3 N = normalize(normal);

    vec3 L = normalize(-lightUbo.lightDir);
    vec3 V = normalize(lightUbo.cameraPos - outWorldPos);

    // 朗伯漫反射
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * lightUbo.lightColor;

    // Blinn-Phong高光
    vec3 H = normalize(L + V);
    float specDot = max(dot(N, H), 0.0);
    float spec = pow(specDot, lightUbo.specPower);
    vec3 specular = spec * lightUbo.lightColor * lightUbo.specStrength;

    // 环境光
    vec3 ambient = lightUbo.lightColor * lightUbo.ambientFactor;

    vec3 albedo = outVertColor;
    vec3 finalLinear = albedo * (ambient + diffuse + specular);

    // 简单防曝光
    finalLinear = min(finalLinear, vec3(3.0));

    // sRGB伽马编码
    vec3 finalGamma = pow(finalLinear, vec3(1.0 / 2.2));

    outColor = vec4(finalGamma, 1.0);
}