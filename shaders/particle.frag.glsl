#version 450
// 粒子片段着色器：以到中心的距离生成柔和圆形 alpha，供 Alpha 混合。
layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float d = length(inUV);
    if (d > 1.0)
        discard;
    float alpha = smoothstep(1.0, 0.0, d);
    outColor = vec4(inColor, alpha);
}
