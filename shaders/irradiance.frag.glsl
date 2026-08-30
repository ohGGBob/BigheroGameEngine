#version 450
// 辐照度卷积：对半球均匀采样漫反射积分，输出低频辐照度

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(location = 0) in vec3 inDir;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979;

void main()
{
    const vec3 N = normalize(inDir);
    const vec3 up = (abs(N.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    const vec3 up2 = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    const float sampleDelta = 0.025;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // 球面坐标 -> 切线空间采样方向
            const vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            const vec3 sampleVec = tangentSample.x * right + tangentSample.y * up2 + tangentSample.z * N;

            irradiance += texture(envMap, sampleVec).rgb * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }

    irradiance = PI * irradiance * (1.0 / sampleCount);
    outColor = vec4(irradiance, 1.0);
}
