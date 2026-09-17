#version 450
// 等距柱状投影(HDR) -> 立方图 片段着色器
// 配合 env_conv.vert 使用：顶点着色器通过 push constant 面基矩阵输出世界方向 outDir

layout(location = 0) in vec3 inDir;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec3 direction = normalize(inDir);
    vec2 uv = sampleSphericalMap(direction);
    vec3 color = texture(equirectangularMap, uv).rgb;
    outColor = vec4(color, 1.0);
}
