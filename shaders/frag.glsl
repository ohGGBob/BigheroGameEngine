#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertColor;

layout(location = 0) out vec4 outColor;

// set1 binding0：光照参数；binding1：漫反射纹理
layout(set = 1, binding = 0, std140) uniform LightUBO {
    vec3 lightDir;
    vec3 lightColor;
    vec3 cameraPos;
    float ambientFactor;
    float specPower;
    float specStrength;
} lightUbo;

layout(set = 1, binding = 1) uniform sampler2D albedoTex;

void main()
{
    // 法线零向量安全容错
    vec3 normal = inNormal;
    if (length(normal) < 0.0001)
        normal = vec3(0.0, 1.0, 0.0);
    vec3 N = normalize(normal);

    vec3 L = normalize(-lightUbo.lightDir);
    vec3 V = normalize(lightUbo.cameraPos - inWorldPos);

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

    // 顶点色x纹理；纹理为SRGB格式，硬件采样时已转线性
    vec3 albedo = inVertColor * texture(albedoTex, inUV).rgb;

    vec3 linearColor = albedo * (ambient + diffuse + specular);
    linearColor = min(linearColor, vec3(3.0));

    // 交换链为SRGB格式，硬件负责线性->sRGB编码，着色器只输出线性值
    outColor = vec4(linearColor, 1.0);
}
