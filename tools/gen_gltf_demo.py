#!/usr/bin/env python3
"""生成 glTF 透明/自发光材质演示资产（assets/models/）。

输出：
  model.gltf      4 个 primitive：棋盘金属(OPAQUE) / 自发光铜(OPAQUE+emissive) /
                  玻璃(BLEND) / 镂空格栅(MASK)，几何为 4 个并排立方体，缓冲内嵌 base64
  basecolor.png   棋盘格反照率（sRGB）
  normal.png      平坦法线（中性）
  mr.png          metallicRoughness（G=粗糙度 B=金属度）
  emissive.png    自发光条纹（线性）
  mask.png        镂空格栅（alpha 0/1 双值，供 MASK 裁剪）

用法：python tools/gen_gltf_demo.py（在仓库根目录执行）
"""

import base64
import json
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "models"

TEX_SIZE = 128


def make_checker_basecolor() -> Image.Image:
    """棋盘格反照率：亮银 / 暗蓝灰 交错。"""
    img = Image.new("RGB", (TEX_SIZE, TEX_SIZE))
    px = img.load()
    cell = TEX_SIZE // 8
    for y in range(TEX_SIZE):
        for x in range(TEX_SIZE):
            on = ((x // cell) + (y // cell)) % 2 == 0
            px[x, y] = (235, 235, 240) if on else (70, 90, 120)
    return img


def make_flat_normal() -> Image.Image:
    """平坦法线：rgb(128,128,255)。"""
    return Image.new("RGB", (TEX_SIZE, TEX_SIZE), (128, 128, 255))


def make_mr(roughness: float, metallic: float) -> Image.Image:
    """metallicRoughness：G=粗糙度 B=金属度（glTF 2.0 约定）。"""
    g = round(roughness * 255)
    b = round(metallic * 255)
    return Image.new("RGB", (TEX_SIZE, TEX_SIZE), (0, g, b))


def make_emissive() -> Image.Image:
    """自发光条纹：暗底 + 亮橙横条（线性 HDR 由材质 emissiveFactor 倍增）。"""
    img = Image.new("RGB", (TEX_SIZE, TEX_SIZE), (15, 8, 4))
    px = img.load()
    band = TEX_SIZE // 8
    for y in range(TEX_SIZE):
        if (y // band) % 2 == 0:
            for x in range(TEX_SIZE):
                px[x, y] = (255, 120, 40)
    return img


def make_mask() -> Image.Image:
    """镂空格栅：alpha 0/1 双值；RGB 为铜色，孔洞处 alpha=0（MASK 裁剪剔除）。"""
    img = Image.new("RGBA", (TEX_SIZE, TEX_SIZE))
    px = img.load()
    cell = TEX_SIZE // 8
    for y in range(TEX_SIZE):
        for x in range(TEX_SIZE):
            # 每格 2/4 为格栅条，其余镂空
            gx, gy = x % cell, y % cell
            bar = gx < cell // 2 and gy < cell // 2 or gx >= cell // 2 and gy >= cell // 2
            px[x, y] = (184, 115, 51, 255) if bar else (184, 115, 51, 0)
    return img


def cube(offset_x: float, half: float = 0.5):
    """单位立方体网格（24 顶点 36 索引），沿 X 平移。返回 (positions, normals, uvs, indices)。"""
    faces = [
        # (法线, 四角顶点相对坐标：-x+y / +x+y / +x-y / -x-y 组合)
        ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
        ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
        ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
        ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
        ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
        ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for n, corners in faces:
        base = len(positions)
        for cx, cy, cz in corners:
            positions.append((offset_x + cx * half, cy * half, cz * half))
            normals.append(n)
        uvs.extend([(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    return positions, normals, uvs, indices


def build_gltf() -> dict:
    # 4 个并排立方体：棋盘金属 / 自发光铜 / 玻璃 / 镂空格栅
    prims_geo = [cube(x, 0.5) for x in (-1.8, -0.6, 0.6, 1.8)]

    buffer_data = bytearray()
    buffer_views = []
    accessors = []

    def add_view(data: bytes, target: int) -> int:
        # 4 字节对齐
        while len(buffer_data) % 4:
            buffer_data.append(0)
        buffer_views.append(
            {"buffer": 0, "byteOffset": len(buffer_data), "byteLength": len(data), "target": target}
        )
        buffer_data.extend(data)
        return len(buffer_views) - 1

    def add_accessor(minv, maxv, comp_type, count, vtype, view, is_index=False):
        accessors.append(
            {
                "bufferView": view,
                "componentType": comp_type,
                "count": count,
                "type": vtype,
                "min": minv,
                "max": maxv,
            }
        )
        return len(accessors) - 1

    primitives = []
    for positions, normals, uvs, indices in prims_geo:
        # POSITION（float3，须 min/max）
        pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
        pos_view = add_view(pos_bytes, 34962)
        pos_acc = add_accessor(
            [min(p[i] for p in positions) for i in range(3)],
            [max(p[i] for p in positions) for i in range(3)],
            5126, len(positions), "VEC3", pos_view,
        )
        # NORMAL
        nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
        nrm_acc = add_accessor(None, None, 5126, len(normals), "VEC3", add_view(nrm_bytes, 34962))
        # TEXCOORD_0
        uv_bytes = b"".join(struct.pack("<2f", *uv) for uv in uvs)
        uv_acc = add_accessor(None, None, 5126, len(uvs), "VEC2", add_view(uv_bytes, 34962))
        # 索引（uint32）
        idx_bytes = b"".join(struct.pack("<I", i) for i in indices)
        idx_acc = add_accessor(None, None, 5125, len(indices), "SCALAR", add_view(idx_bytes, 34963), True)

        primitives.append(
            {"attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc, "TEXCOORD_0": uv_acc}, "indices": idx_acc,
             "material": len(primitives)}
        )

    materials = [
        {
            "name": "CheckerMetal",
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 1.0,
                "roughnessFactor": 0.2,
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 2},
            },
            "normalTexture": {"index": 1},
        },
        {
            "name": "EmissiveCopper",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.45, 0.25, 1.0],
                "metallicFactor": 1.0,
                "roughnessFactor": 0.35,
                "baseColorTexture": {"index": 0},
            },
            "normalTexture": {"index": 1},
            "emissiveFactor": [2.5, 0.9, 0.3],
            "emissiveTexture": {"index": 3},
        },
        {
            "name": "Glass",
            "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.75, 0.95, 0.35], "metallicFactor": 0.0,
                                     "roughnessFactor": 0.1},
            "alphaMode": "BLEND",
        },
        {
            "name": "Grate",
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicFactor": 0.8,
                "roughnessFactor": 0.5,
                "baseColorTexture": {"index": 4},
            },
            "alphaMode": "MASK",
            "alphaCutoff": 0.35,
        },
    ]

    textures = [
        {"source": i, "sampler": 0} for i in range(5)
    ]
    images = [
        {"uri": "basecolor.png"},
        {"uri": "normal.png"},
        {"uri": "mr.png"},
        {"uri": "emissive.png"},
        {"uri": "mask.png"},
    ]

    # 内嵌 base64 缓冲
    while len(buffer_data) % 4:
        buffer_data.append(0)
    b64 = base64.b64encode(bytes(buffer_data)).decode("ascii")

    return {
        "asset": {"version": "2.0", "generator": "BigHero gen_gltf_demo.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "TransparentEmissiveDemo"}],
        "meshes": [{"name": "demo", "primitives": primitives}],
        "materials": materials,
        "textures": textures,
        "images": images,
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "buffers": [{"byteLength": len(buffer_data), "uri": f"data:application/octet-stream;base64,{b64}"}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    make_checker_basecolor().save(OUT / "basecolor.png")
    make_flat_normal().save(OUT / "normal.png")
    make_mr(roughness=0.2, metallic=1.0).save(OUT / "mr.png")
    make_emissive().save(OUT / "emissive.png")
    make_mask().save(OUT / "mask.png")

    gltf = build_gltf()
    (OUT / "model.gltf").write_text(json.dumps(gltf, separators=(",", ":")), encoding="utf-8")

    print(f"已生成演示资产于 {OUT}:")
    for f in ("model.gltf", "basecolor.png", "normal.png", "mr.png", "emissive.png", "mask.png"):
        size = (OUT / f).stat().st_size
        print(f"  {f}  ({size} bytes)")


if __name__ == "__main__":
    main()
