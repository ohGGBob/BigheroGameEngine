# 房间展示场景资产生成器（Blender 后台运行工具，不在引擎构建链内）。
#
# 运行方式（需要 Blender 3.x+ 的 Python 环境，本机验证用 Blender 5.2）：
#     blender --background --python tools/gen_room.py
#
# 输出：assets/models/room/cozy_room.glb（输出路径相对本脚本自身解析，
#       与调用时的工作目录无关，可从任意目录运行）。
#
# 与旧版 assets/models/room/create_room.py 的差异（2026-09 重修）：
#   1. 轴向修正：Blender 为 Z 上（X 右 / Y 纵深 / Z 高度）。旧版把墙高与家具"高度"
#      写在了 Y（纵深）轴上——导出 glTF 经 Y-up 转换后墙变成贴地横躺的板、家具悬空
#      或埋进地里，任何运行期整体旋转都无法修复。本版所有物体高度一律走 Z 轴、
#      薄边（墙厚/板厚）走在横向，导出 Y-up 后即引擎空间（x=左右, y=高, z=纵深），
#      运行期无需任何旋转/位移修正。
#   2. 烘焙物体变换：引擎的 GltfLoader 不烘焙 glTF 节点 TRS（仅取蒙皮/动画用），
#      静态网格必须把逐物体变换烘焙进顶点数据（与 samples/assets 现有 model.gltf
#      的"单节点烘焙几何"同构）。脚本先把各物体自身 TRS 烘焙，再整体施加
#      Z-up -> Y-up 的 -90° X 旋转并二次烘焙，最后以 export_yup=False 导出，
#      保证输出 glb 的节点为单位变换、顶点直接是引擎世界坐标。
#   3. 空间约定：房间内腔 x∈[-3,3]（宽 6m）、高 3m、深 5m（引擎 z∈[-5,0]）；
#      地板顶面在 y=0.006（引擎世界 y 方向；比引擎地平面上浮 6mm 防止与程序化
#      地面平面 z-fighting），门洞在前墙（引擎 z=-5 一侧）中央 x∈[-1.5,1.5]。
#
# 说明：本脚本是资产工具，只在需要重新生成 cozy_room.glb 时手工运行，
# 不参与引擎构建，也不在 CI 中执行。

import math
import os

import bpy
from mathutils import Matrix

# ---- 房间尺寸（Blender：X 宽 / Y 纵深 / Z 高，单位米） ----
W, D, H = 6.0, 5.0, 3.0


def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)


def make_material(name, color, roughness=0.8):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if bsdf:
        bsdf.inputs['Base Color'].default_value = (*color, 1.0)
        bsdf.inputs['Roughness'].default_value = roughness
        bsdf.inputs['Metallic'].default_value = 0.0
    return mat


def add_primitive(name, kind, args, location=(0, 0, 0), scale=(1, 1, 1), rotation=(0, 0, 0),
                  color=(0.8, 0.8, 0.8), roughness=0.8, color_material=None):
    """创建基本体并把原点放到底部或中心：统一约定 location 为其几何中心。"""
    if kind == 'cube':
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    elif kind == 'plane':
        bpy.ops.mesh.primitive_plane_add(size=1.0, location=location)
    elif kind == 'cylinder':
        bpy.ops.mesh.primitive_cylinder_add(radius=args[0], depth=args[1], location=location)
    else:
        raise ValueError(kind)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = scale
    obj.rotation_euler = rotation
    if color_material is None:
        color_material = color
    mat = make_material(name + '_mat', color_material, roughness)
    obj.data.materials.append(mat)
    return obj


def build_room():
    clear_scene()

    # ===== 外壳（薄边全部横向，高度在 Z） =====
    add_primitive('floor', 'plane', None, location=(0, D / 2, 0.006), scale=(W, D, 1.0),
                  color=(0.72, 0.52, 0.32), roughness=0.55)                    # 暖木地板
    add_primitive('ceiling', 'plane', None, location=(0, D / 2, H), scale=(W, D, 1.0),
                  rotation=(math.pi, 0, 0), color=(0.95, 0.95, 0.92), roughness=0.9)  # 朝下，室内可见
    # 后墙（引擎 z=0 侧）
    add_primitive('back_wall', 'cube', (1,), location=(0, 0, H / 2), scale=(W, 0.1, H),
                  color=(0.92, 0.88, 0.82))
    # 左墙 / 右墙
    add_primitive('left_wall', 'cube', (1,), location=(-W / 2, D / 2, H / 2), scale=(0.1, D, H),
                  color=(0.92, 0.88, 0.82))
    add_primitive('right_wall', 'cube', (1,), location=(W / 2, D / 2, H / 2), scale=(0.1, D, H),
                  color=(0.92, 0.88, 0.82))
    # 前墙（引擎 z=-5 侧）中央留门洞：两侧墙段 (1.5 宽) + 门楣
    add_primitive('front_wall_l', 'cube', (1,), location=(-W / 4, D, 1.1), scale=(1.5, 0.1, 2.2),
                  color=(0.92, 0.88, 0.82))
    add_primitive('front_wall_r', 'cube', (1,), location=(W / 4, D, 1.1), scale=(1.5, 0.1, 2.2),
                  color=(0.92, 0.88, 0.82))
    add_primitive('front_wall_top', 'cube', (1,), location=(0, D, 2.6), scale=(3.0, 0.1, 0.4),
                  color=(0.92, 0.88, 0.82))

    # ===== 窗户（右墙中部，朝室内 -X；平面背面即室外不可见 => 近似玻璃色） =====
    add_primitive('window', 'plane', None, location=(W / 2 - 0.05, D / 2, H * 0.55),
                  scale=(1.1, 1.3, 1.0), rotation=(0, math.radians(-90), 0),
                  color=(0.60, 0.82, 0.94), roughness=0.05)
    add_primitive('win_frame_v', 'cube', (1,), location=(W / 2 - 0.04, D / 2, H * 0.55), scale=(0.02, 1.35, 0.05),
                  color=(0.40, 0.30, 0.20))
    add_primitive('win_frame_h', 'cube', (1,), location=(W / 2 - 0.04, D / 2, H * 0.55), scale=(0.02, 0.05, 1.15),
                  color=(0.40, 0.30, 0.20))

    # ===== 家具（靠墙摆放，高度全部 Z 向） =====
    # 双人床（左侧靠后墙）：床架 1.4 宽 × 2.0 深 × 0.4 高
    add_primitive('bed_base', 'cube', (1,), location=(-1.7, D * 0.66, 0.2), scale=(1.4, 2.0, 0.4),
                  color=(0.60, 0.40, 0.25))
    add_primitive('bed_mattress', 'cube', (1,), location=(-1.7, D * 0.66, 0.48), scale=(1.3, 1.9, 0.16),
                  color=(0.95, 0.95, 0.90), roughness=0.9)
    add_primitive('bed_pillow1', 'cube', (1,), location=(-1.7, D * 0.51, 0.68), scale=(0.6, 0.4, 0.12),
                  color=(1.0, 1.0, 0.95), roughness=0.9)
    add_primitive('bed_pillow2', 'cube', (1,), location=(-1.7, D * 0.81, 0.68), scale=(0.6, 0.4, 0.12),
                  color=(1.0, 1.0, 0.95), roughness=0.9)
    add_primitive('bed_blanket', 'cube', (1,), location=(-1.7, D * 0.62, 0.62), scale=(1.35, 1.0, 0.1),
                  color=(0.40, 0.55, 0.70))
    # 床头柜 + 台灯（床右侧，点光源 #2 的对应几何）
    add_primitive('nightstand', 'cube', (1,), location=(-0.35, D * 0.86, 0.3), scale=(0.6, 0.5, 0.6),
                  color=(0.50, 0.35, 0.20))
    add_primitive('lamp_base', 'cylinder', (0.06, 0.03), location=(-0.35, D * 0.86, 0.615),
                  color=(0.30, 0.30, 0.30), roughness=0.3)
    add_primitive('lamp_stem', 'cylinder', (0.015, 0.22), location=(-0.35, D * 0.86, 0.74),
                  color=(0.30, 0.30, 0.30), roughness=0.3)
    add_primitive('lamp_shade', 'cylinder', (0.10, 0.12), location=(-0.35, D * 0.86, 0.90),
                  color=(0.98, 0.90, 0.75), roughness=0.9)
    # 沙发（靠右侧窗边）
    add_primitive('sofa_base', 'cube', (1,), location=(1.6, D * 0.30, 0.2), scale=(1.1, 0.8, 0.4),
                  color=(0.70, 0.45, 0.35))
    add_primitive('sofa_back', 'cube', (1,), location=(1.6, D * 0.24, 0.65), scale=(1.1, 0.1, 0.6),
                  color=(0.70, 0.45, 0.35))
    add_primitive('sofa_arm1', 'cube', (1,), location=(1.02, D * 0.30, 0.45), scale=(0.1, 0.8, 0.25),
                  color=(0.65, 0.40, 0.30))
    add_primitive('sofa_arm2', 'cube', (1,), location=(2.18, D * 0.30, 0.45), scale=(0.1, 0.8, 0.25),
                  color=(0.65, 0.40, 0.30))
    # 小茶几（沙发前）
    add_primitive('table', 'cylinder', (0.30, 0.05), location=(1.6, D * 0.48, 0.33),
                  color=(0.55, 0.40, 0.25))
    add_primitive('table_leg1', 'cylinder', (0.02, 0.32), location=(1.6, D * 0.48, 0.16),
                  color=(0.55, 0.40, 0.25))
    # 地毯
    add_primitive('rug', 'plane', None, location=(0, D * 0.5, 0.012), scale=(2.5, 1.8, 1.0),
                  color=(0.50, 0.30, 0.20), roughness=0.9)
    # 书架（后墙左侧）+ 彩色书
    add_primitive('bookshelf', 'cube', (1,), location=(-2.3, D * 0.91, 0.7), scale=(0.7, 0.16, 1.4),
                  color=(0.45, 0.30, 0.20))
    for i in range(5):
        add_primitive(f'book_{i}', 'cube', (1,), location=(-2.3, D * 0.90, 0.30 + i * 0.26),
                      scale=(0.12, 0.12, 0.24),
                      color=(0.30 + i * 0.12, 0.50 - i * 0.07, 0.60 + i * 0.05))
    # 盆栽（沙发旁）
    add_primitive('pot', 'cylinder', (0.10, 0.18), location=(2.35, D * 0.20, 0.09),
                  color=(0.60, 0.35, 0.20))
    add_primitive('plant_stem', 'cylinder', (0.012, 0.35), location=(2.35, D * 0.20, 0.34),
                  color=(0.20, 0.50, 0.15))
    add_primitive('plant_leaves', 'cylinder', (0.13, 0.12), location=(2.35, D * 0.20, 0.52),
                  color=(0.25, 0.60, 0.20))
    # 衣柜（前墙右侧）
    add_primitive('wardrobe', 'cube', (1,), location=(2.2, D * 0.85, 0.8), scale=(0.6, 0.4, 1.6),
                  color=(0.50, 0.35, 0.25))
    add_primitive('handle1', 'cylinder', (0.012, 0.05), location=(2.1, D * 0.85, 0.8),
                  color=(0.7, 0.7, 0.6), roughness=0.3)
    add_primitive('handle2', 'cylinder', (0.012, 0.05), location=(2.3, D * 0.85, 0.8),
                  color=(0.7, 0.7, 0.6), roughness=0.3)
    # 挂墙画（后墙，面对室内 +Y）
    add_primitive('painting', 'plane', None, location=(1.0, 0.06, 1.5), scale=(0.9, 1.0, 1.0),
                  rotation=(math.radians(-90), 0, 0), color=(0.35, 0.55, 0.80), roughness=0.7)

    # ===== 吊灯（天花板中央偏房深方向；点光源 #1 的对应几何） =====
    add_primitive('light_base', 'cylinder', (0.08, 0.04), location=(0, D * 0.5, H - 0.08),
                  color=(0.30, 0.30, 0.30), roughness=0.4)
    add_primitive('light_shade', 'cylinder', (0.22, 0.14), location=(0, D * 0.5, H - 0.25),
                  color=(0.98, 0.92, 0.80), roughness=0.9)

    # ===== Q 版人物（房间中央，面向 +Y 即门的方向） =====
    cy = D * 0.5
    add_primitive('char_body', 'cylinder', (0.25, 0.45), location=(0, cy, 0.55),
                  color=(0.90, 0.60, 0.40))
    add_primitive('char_head', 'cylinder', (0.22, 0.30), location=(0, cy, 0.93),
                  color=(0.98, 0.85, 0.75), roughness=0.9)
    add_primitive('char_eye1', 'cylinder', (0.04, 0.02), location=(-0.08, cy + 0.20, 0.96),
                  color=(0.10, 0.10, 0.10), roughness=0.3)
    add_primitive('char_eye2', 'cylinder', (0.04, 0.02), location=(0.08, cy + 0.20, 0.96),
                  color=(0.10, 0.10, 0.10), roughness=0.3)
    add_primitive('char_cheek1', 'cylinder', (0.05, 0.01), location=(-0.14, cy + 0.14, 0.90),
                  color=(1.0, 0.5, 0.5), roughness=0.9)
    add_primitive('char_cheek2', 'cylinder', (0.05, 0.01), location=(0.14, cy + 0.14, 0.90),
                  color=(1.0, 0.5, 0.5), roughness=0.9)
    add_primitive('char_arm1', 'cylinder', (0.06, 0.25), location=(-0.32, cy, 0.55),
                  rotation=(0, 0, math.radians(30)), color=(0.90, 0.60, 0.40))
    add_primitive('char_arm2', 'cylinder', (0.06, 0.25), location=(0.32, cy, 0.55),
                  rotation=(0, 0, math.radians(-30)), color=(0.90, 0.60, 0.40))
    add_primitive('char_leg1', 'cylinder', (0.08, 0.25), location=(-0.12, cy, 0.14),
                  color=(0.30, 0.40, 0.60))
    add_primitive('char_leg2', 'cylinder', (0.08, 0.25), location=(0.12, cy, 0.14),
                  color=(0.30, 0.40, 0.60))
    add_primitive('char_shoe1', 'cylinder', (0.09, 0.08), location=(-0.12, cy, 0.035),
                  color=(0.50, 0.20, 0.15))
    add_primitive('char_shoe2', 'cylinder', (0.09, 0.08), location=(0.12, cy, 0.035),
                  color=(0.50, 0.20, 0.15))
    add_primitive('char_hair', 'cylinder', (0.24, 0.12), location=(0, cy, 1.09), scale=(1.0, 1.0, 0.5),
                  color=(0.40, 0.20, 0.10), roughness=0.9)
    add_primitive('char_ahoge', 'cylinder', (0.03, 0.10), location=(0, cy, 1.16),
                  rotation=(math.radians(20), 0, 0), color=(0.40, 0.20, 0.10), roughness=0.9)


def bake_object_transforms():
    for obj in bpy.data.objects:
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def apply_yup_conversion():
    # Z-up(Blender) -> Y-up(glTF/引擎)：绕 X 轴 -90°（(x,y,z) -> (x,z,-y)）。
    # 直接改写世界矩阵再烘焙，导出时 export_yup=False，节点保持单位变换。
    r = Matrix.Rotation(-math.pi / 2, 4, 'X')
    for obj in bpy.data.objects:
        obj.matrix_world = r @ obj.matrix_world


def main():
    build_room()
    bake_object_transforms()
    apply_yup_conversion()
    bake_object_transforms()

    out = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets',
                                        'models', 'room', 'cozy_room.glb'))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=False,
                              export_apply=False, use_selection=False)
    print('[gen_room] exported:', out)


main()