import bpy
import os
import math

def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for mat in bpy.data.materials:
        bpy.data.materials.remove(mat)

def make_material(name, color, roughness=0.8):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if bsdf:
        bsdf.inputs['Base Color'].default_value = (*color, 1.0)
        bsdf.inputs['Roughness'].default_value = roughness
    return mat

def add_mesh(name, verts, faces, location=(0,0,0)):
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj.location = location
    return obj

def add_cube(name, size, location=(0,0,0), color=(0.8,0.8,0.8), roughness=0.8):
    bpy.ops.mesh.primitive_cube_add(size=size, location=location)
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material(name + '_mat', color, roughness)
    obj.data.materials.append(mat)
    return obj

def add_plane(name, size, location=(0,0,0), color=(0.8,0.8,0.8), roughness=0.8):
    bpy.ops.mesh.primitive_plane_add(size=size, location=location)
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material(name + '_mat', color, roughness)
    obj.data.materials.append(mat)
    return obj

def add_cylinder(name, radius, depth, location=(0,0,0), color=(0.8,0.8,0.8), roughness=0.8):
    bpy.ops.mesh.primitive_cylinder_add(radius=radius, depth=depth, location=location)
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material(name + '_mat', color, roughness)
    obj.data.materials.append(mat)
    return obj

def export_gltf(filepath):
    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB',
        export_yup=True,
    )

clear_scene()

# 房间尺寸
W, H, D = 6.0, 3.0, 5.0  # 宽 高 深

# 地板 - 暖木色
floor = add_plane('floor', size=W, location=(0, 0, -D/2), color=(0.72, 0.52, 0.32), roughness=0.6)

# 天花板 - 白色
ceiling = add_plane('ceiling', size=W, location=(0, H, -D/2), color=(0.95, 0.95, 0.92), roughness=0.9)

# 后墙
back_wall = add_cube('back_wall', size=1.0, location=(0, H/2, -D), color=(0.92, 0.88, 0.82), roughness=0.9)
back_wall.scale = (W/2, H/2, 0.05)

# 左墙
left_wall = add_cube('left_wall', size=1.0, location=(-W/2, H/2, -D/2), color=(0.92, 0.88, 0.82), roughness=0.9)
left_wall.scale = (0.05, H/2, D/2)

# 右墙（带窗户挖洞 - 简化为不同颜色的"窗框区域"）
right_wall = add_cube('right_wall', size=1.0, location=(W/2, H/2, -D/2), color=(0.92, 0.88, 0.82), roughness=0.9)
right_wall.scale = (0.05, H/2, D/2)

# 前墙（带门洞 - 简化）
front_wall_l = add_cube('front_wall_l', size=1.0, location=(-W/4, H/2, 0), color=(0.92, 0.88, 0.82), roughness=0.9)
front_wall_l.scale = (W/4, H/2, 0.05)
front_wall_r = add_cube('front_wall_r', size=1.0, location=(W/4, H/2, 0), color=(0.92, 0.88, 0.82), roughness=0.9)
front_wall_r.scale = (W/4, H/2, 0.05)
front_wall_top = add_cube('front_wall_top', size=1.0, location=(0, H*0.75, 0), color=(0.92, 0.88, 0.82), roughness=0.9)
front_wall_top.scale = (W/4, H*0.25, 0.05)

# 窗户玻璃（右墙中部）
window = add_plane('window', size=1.0, location=(W/2, H*0.6, -D*0.3), color=(0.6, 0.85, 0.95), roughness=0.05)
window.scale = (0.02, 0.8, 0.8)
window.rotation_euler = (0, math.radians(90), 0)

# 窗户框
win_frame_v = add_cube('win_frame_v', size=1.0, location=(W/2, H*0.6, -D*0.3), color=(0.4, 0.3, 0.2), roughness=0.6)
win_frame_v.scale = (0.03, 0.9, 0.05)
win_frame_h = add_cube('win_frame_h', size=1.0, location=(W/2, H*0.6, -D*0.3), color=(0.4, 0.3, 0.2), roughness=0.6)
win_frame_h.scale = (0.03, 0.05, 0.9)

# ===== 家具 =====

# 双人床（左侧靠墙）
bed_base = add_cube('bed_base', size=1.0, location=(-W*0.3, 0.35, -D*0.7), color=(0.6, 0.4, 0.25), roughness=0.7)
bed_base.scale = (1.2, 0.35, 1.0)
bed_mattress = add_cube('bed_mattress', size=1.0, location=(-W*0.3, 0.6, -D*0.7), color=(0.95, 0.95, 0.9), roughness=0.9)
bed_mattress.scale = (1.1, 0.15, 0.9)
bed_pillow1 = add_cube('bed_pillow1', size=1.0, location=(-W*0.3, 0.75, -D*0.85), color=(1.0, 1.0, 0.95), roughness=0.9)
bed_pillow1.scale = (0.35, 0.12, 0.25)
bed_pillow2 = add_cube('bed_pillow2', size=1.0, location=(-W*0.3, 0.75, -D*0.55), color=(1.0, 1.0, 0.95), roughness=0.9)
bed_pillow2.scale = (0.35, 0.12, 0.25)
bed_blanket = add_cube('bed_blanket', size=1.0, location=(-W*0.3, 0.7, -D*0.55), color=(0.4, 0.55, 0.7), roughness=0.8)
bed_blanket.scale = (1.15, 0.1, 0.55)

# 床头柜（床右侧）
nightstand = add_cube('nightstand', size=1.0, location=(-W*0.05, 0.3, -D*0.7), color=(0.5, 0.35, 0.2), roughness=0.6)
nightstand.scale = (0.3, 0.3, 0.3)
# 台灯
lamp_base = add_cylinder('lamp_base', radius=0.08, depth=0.02, location=(-W*0.05, 0.48, -D*0.7), color=(0.3, 0.3, 0.3), roughness=0.3)
lamp_stem = add_cylinder('lamp_stem', radius=0.02, depth=0.25, location=(-W*0.05, 0.6, -D*0.7), color=(0.3, 0.3, 0.3), roughness=0.3)
lamp_shade = add_cylinder('lamp_shade', radius=0.12, depth=0.15, location=(-W*0.05, 0.75, -D*0.7), color=(0.95, 0.9, 0.75), roughness=0.9)

# 小沙发（靠窗）
sofa_base = add_cube('sofa_base', size=1.0, location=(W*0.25, 0.35, -D*0.25), color=(0.7, 0.45, 0.35), roughness=0.8)
sofa_base.scale = (0.6, 0.35, 0.5)
sofa_back = add_cube('sofa_back', size=1.0, location=(W*0.25, 0.6, -D*0.45), color=(0.7, 0.45, 0.35), roughness=0.8)
sofa_back.scale = (0.6, 0.25, 0.1)
sofa_arm1 = add_cube('sofa_arm1', size=1.0, location=(W*0.1, 0.45, -D*0.25), color=(0.65, 0.4, 0.3), roughness=0.8)
sofa_arm1.scale = (0.1, 0.2, 0.5)
sofa_arm2 = add_cube('sofa_arm2', size=1.0, location=(W*0.4, 0.45, -D*0.25), color=(0.65, 0.4, 0.3), roughness=0.8)
sofa_arm2.scale = (0.1, 0.2, 0.5)

# 小茶几（沙发前）
table = add_cylinder('table', radius=0.35, depth=0.03, location=(W*0.25, 0.35, -D*0.05), color=(0.55, 0.4, 0.25), roughness=0.6)
table_leg1 = add_cylinder('table_leg1', radius=0.02, depth=0.35, location=(W*0.25, 0.18, -D*0.05), color=(0.55, 0.4, 0.25), roughness=0.6)

# 地毯（房间中央）
rug = add_plane('rug', size=1.0, location=(0, 0.005, -D*0.4), color=(0.5, 0.3, 0.2), roughness=0.9)
rug.scale = (2.5, 1.0, 1.8)

# 书架（后墙左侧）
bookshelf = add_cube('bookshelf', size=1.0, location=(-W*0.35, 0.8, -D*0.85), color=(0.45, 0.3, 0.2), roughness=0.6)
bookshelf.scale = (0.5, 0.8, 0.15)
# 书（彩色小方块）
for i in range(5):
    book = add_cube(f'book_{i}', size=1.0, location=(-W*0.35, 0.35 + i*0.28, -D*0.78), 
                    color=(0.3 + i*0.12, 0.5 - i*0.08, 0.6 + i*0.05), roughness=0.7)
    book.scale = (0.08, 0.12, 0.05)

# 盆栽（沙发旁）
pot = add_cylinder('pot', radius=0.12, depth=0.18, location=(W*0.42, 0.12, -D*0.25), color=(0.6, 0.35, 0.2), roughness=0.7)
plant_stem = add_cylinder('plant_stem', radius=0.015, depth=0.4, location=(W*0.42, 0.4, -D*0.25), color=(0.2, 0.5, 0.15), roughness=0.8)
plant_leaves = add_cylinder('plant_leaves', radius=0.15, depth=0.05, location=(W*0.42, 0.55, -D*0.25), color=(0.25, 0.6, 0.2), roughness=0.8)
plant_leaves.scale = (1.0, 0.3, 1.0)

# 衣柜（前墙右侧）
wardrobe = add_cube('wardrobe', size=1.0, location=(W*0.35, 0.8, -0.15), color=(0.5, 0.35, 0.25), roughness=0.6)
wardrobe.scale = (0.6, 0.8, 0.2)
# 衣柜把手
handle1 = add_cylinder('handle1', radius=0.015, depth=0.04, location=(W*0.3, 0.8, -0.05), color=(0.7, 0.7, 0.6), roughness=0.3)
handle2 = add_cylinder('handle2', radius=0.015, depth=0.04, location=(W*0.4, 0.8, -0.05), color=(0.7, 0.7, 0.6), roughness=0.3)

# 挂墙画（后墙）
painting = add_plane('painting', size=1.0, location=(0, H*0.6, -D*0.99), color=(0.3, 0.5, 0.7), roughness=0.7)
painting.scale = (0.6, 1.0, 0.4)

# 吊灯
light_base = add_cylinder('light_base', radius=0.08, depth=0.03, location=(0, H-0.05, -D*0.4), color=(0.8, 0.75, 0.6), roughness=0.4)
light_shade = add_cylinder('light_shade', radius=0.25, depth=0.15, location=(0, H-0.15, -D*0.4), color=(0.95, 0.9, 0.8), roughness=0.9)

# ===== Q版人物 =====
# 身体（大圆）
body = add_cylinder('char_body', radius=0.25, depth=0.45, location=(0, 0.55, -D*0.4), color=(0.9, 0.6, 0.4), roughness=0.8)
# 头（球）
head = add_cylinder('char_head', radius=0.22, depth=0.3, location=(0, 0.95, -D*0.4), color=(0.98, 0.85, 0.75), roughness=0.9)
# 眼睛
eye1 = add_cylinder('char_eye1', radius=0.04, depth=0.02, location=(-0.08, 0.98, -D*0.4 + 0.18), color=(0.1, 0.1, 0.1), roughness=0.3)
eye2 = add_cylinder('char_eye2', radius=0.04, depth=0.02, location=(0.08, 0.98, -D*0.4 + 0.18), color=(0.1, 0.1, 0.1), roughness=0.3)
# 腮红
cheek1 = add_cylinder('char_cheek1', radius=0.05, depth=0.01, location=(-0.14, 0.92, -D*0.4 + 0.16), color=(1.0, 0.5, 0.5), roughness=0.9)
cheek2 = add_cylinder('char_cheek2', radius=0.05, depth=0.01, location=(0.14, 0.92, -D*0.4 + 0.16), color=(1.0, 0.5, 0.5), roughness=0.9)
# 手臂
arm1 = add_cylinder('char_arm1', radius=0.06, depth=0.25, location=(-0.32, 0.55, -D*0.4), color=(0.9, 0.6, 0.4), roughness=0.8)
arm1.rotation_euler = (0, 0, math.radians(30))
arm2 = add_cylinder('char_arm2', radius=0.06, depth=0.25, location=(0.32, 0.55, -D*0.4), color=(0.9, 0.6, 0.4), roughness=0.8)
arm2.rotation_euler = (0, 0, math.radians(-30))
# 腿
leg1 = add_cylinder('char_leg1', radius=0.08, depth=0.25, location=(-0.12, 0.15, -D*0.4), color=(0.3, 0.4, 0.6), roughness=0.8)
leg2 = add_cylinder('char_leg2', radius=0.08, depth=0.25, location=(0.12, 0.15, -D*0.4), color=(0.3, 0.4, 0.6), roughness=0.8)
# 鞋子
shoe1 = add_cylinder('char_shoe1', radius=0.09, depth=0.08, location=(-0.12, 0.04, -D*0.4), color=(0.5, 0.2, 0.15), roughness=0.7)
shoe2 = add_cylinder('char_shoe2', radius=0.09, depth=0.08, location=(0.12, 0.04, -D*0.4), color=(0.5, 0.2, 0.15), roughness=0.7)
# 头发
hair = add_cylinder('char_hair', radius=0.24, depth=0.12, location=(0, 1.08, -D*0.4), color=(0.4, 0.2, 0.1), roughness=0.9)
hair.scale = (1.0, 0.5, 1.0)
# 小呆毛
ahoge = add_cylinder('char_ahoge', radius=0.03, depth=0.1, location=(0, 1.16, -D*0.4), color=(0.4, 0.2, 0.1), roughness=0.9)
ahoge.rotation_euler = (math.radians(20), 0, 0)

# 导出
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cozy_room.glb')
export_gltf(out)
print(f"Exported: {out}")
