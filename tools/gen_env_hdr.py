# 生成 assets/env/env_sunset.hdr：等距柱状投影 HDR 天空（Radiance RGBE 格式）
# 天空模型与引擎内程序化生成保持一致（渐变天空 + 太阳盘 + 光晕 + 地面反弹），
# 供 EnvironmentLighting::CreateFromFile 走真实 HDR 资源加载路径。
import math
import struct

W, H = 1024, 512

SUN_DIR = (-0.5, 1.0, 0.35)


def norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return (v[0] / l, v[1] / l, v[2] / l)


def sky(dirv):
    x, y, z = dirv
    zenith = (0.10, 0.22, 0.55)
    horizon = (0.75, 0.80, 0.90)
    ground = (0.28, 0.25, 0.22)
    if y > 0.0:
        t = y ** 0.45
        c = [horizon[i] + (zenith[i] - horizon[i]) * t for i in range(3)]
    else:
        t = (-y) ** 0.35
        c = [horizon[i] + (ground[i] - horizon[i]) * t for i in range(3)]
    cos_sun = sum(dirv[i] * norm(SUN_DIR)[i] for i in range(3))
    disc = max(0.0, min(1.0, (cos_sun - 0.9992) / (0.9997 - 0.9992))) * 60.0
    glow = (max(cos_sun, 0.0) ** 200) * 3.0 + (max(cos_sun, 0.0) ** 16) * 0.35
    for i, w in enumerate((1.0, 0.92, 0.75)):
        c[i] += (disc + glow) * w
    return c


def to_rgbe(r, g, b):
    m = max(r, g, b)
    if m <= 1e-32:
        return (0, 0, 0, 0)
    frac, exp2 = math.frexp(m)          # m = frac * 2^exp2, frac∈[0.5,1)
    scale = math.ldexp(1.0, exp2 + 9)   # 2^(e-8) 的倒数形式
    ir = int(r / scale) & 0xFF
    ig = int(g / scale) & 0xFF
    ib = int(b / scale) & 0xFF
    ie = int(exp2 + 126) & 0xFF
    return (ir, ig, ib, ie)


pixels = bytearray()
for row in range(H):
    # v: 0(顶部, +Y 天顶) → 1(底部)
    v = (row + 0.5) / H
    theta = v * math.pi
    for col in range(W):
        u = (col + 0.5) / W
        phi = (u - 0.5) * 2.0 * math.pi
        y = math.cos(theta)
        s = math.sin(theta)
        x = s * math.cos(phi)
        z = s * math.sin(phi)
        r, g, b = sky((x, y, z))
        pixels.extend(to_rgbe(r, g, b))

hdr = bytearray()
hdr += b"#?RADIANCE\n"
hdr += b"FORMAT=32-bit_rle_rgbe\n"
hdr += b"GAMMA=1\n"
hdr += b"EXPOSURE=1\n"
hdr += b"\n"
hdr += b"-Y %d +X %d\n" % (H, W)
hdr += bytes(pixels)

open("assets/env/env_sunset.hdr", "wb").write(hdr)
print("env_sunset.hdr written:", len(hdr), "bytes")
