"""Blender script: the front picture of StatTrak (updater/assets/device.webp) from the 3D model.

    blender -b "StatTrak model.blend" --python tools/device_art/render_device.py -- out.png [samples]
    python tools/device_art/finish_render.py out.png updater/assets/device.webp

The picture: an orthographic front view, 30 pixels per mm, 1896 x 1024 pixels = the device
(59.2 x 30.15 mm) plus 2 mm on each side. No floor, a transparent background. The display is
off (black glass): the page draws the pixels on top (js/device.js).

The look: matte black anodised aluminium (not metallic, rough), laser etched text in light grey,
a soft light from above, faint rim lights. The glass and the "LED counter" plane of the model are
removed; a new black tinted glass fills the rounded pocket exactly (the plane of the model has
square corners and lies in front of the glass).
"""
import math
import sys

import bmesh
import bpy
import mathutils

args = sys.argv[sys.argv.index('--') + 1:]
OUT = args[0]
SAMPLES = int(args[1]) if len(args) > 1 else 128

KEY, FILL, RIM, WORLD = 3.6, 0.9, 0.8, 0.03
BASE, ROUGH, SPEC, GRAIN = 0.022, 0.62, 0.45, 0.12
ETCH, TINT = 0.78, 0.05
# Front face coordinates (mm, origin = top left of the device box) -> world (m)
TOP, LEFT = 0.030170, -0.0296
POCKET = (3.475, 3.495, 49.225, 18.595, 0.8)       # the glass pocket of the body, radius 0.8
GLASS_Y = (-0.00606, -0.00405)                     # front and back face of the glass

s = bpy.context.scene
for name in ['Floor', 'Key', 'Fill']:
    bpy.data.objects[name].hide_render = True

# --- remove the glass and the display plane of the model -----------------------------------
ob = bpy.data.objects['StatTrak']
remove = {i for i, slot in enumerate(ob.material_slots) if slot.name in ('Black display glass', 'LED counter')}
bm = bmesh.new(); bm.from_mesh(ob.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in remove], context='FACES')
bm.to_mesh(ob.data); bm.free()


# --- a new glass that fills the pocket ------------------------------------------------------
def rounded_rect(x0, y0, x1, y1, r, seg=10):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(seg + 1):
            a = math.radians(a0 + 90 * i / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


pts = rounded_rect(*POCKET)
bm = bmesh.new()
front = [bm.verts.new((LEFT + x / 1000, GLASS_Y[0], TOP - y / 1000)) for x, y in pts]
back = [bm.verts.new((LEFT + x / 1000, GLASS_Y[1], TOP - y / 1000)) for x, y in pts]
bm.faces.new(front); bm.faces.new(list(reversed(back)))
for i in range(len(pts)):
    j = (i + 1) % len(pts)
    bm.faces.new((front[i], back[i], back[j], front[j]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
mesh = bpy.data.meshes.new('Glass'); bm.to_mesh(mesh); bm.free()
s.collection.objects.link(bpy.data.objects.new('Glass', mesh))
glass = bpy.data.materials.new('Tinted glass'); glass.use_nodes = True
g = glass.node_tree.nodes['Principled BSDF']
g.inputs['Base Color'].default_value = (TINT, TINT, TINT, 1)
g.inputs['Transmission Weight'].default_value = 1.0
g.inputs['Roughness'].default_value = 0.02
g.inputs['IOR'].default_value = 1.5
mesh.materials.append(glass)


def principled(name):
    return bpy.data.materials[name].node_tree.nodes['Principled BSDF']


# --- matte black anodised aluminium: dyed oxide layer, bead blasted -------------------------
m = bpy.data.materials['Black anodised aluminium']
t = m.node_tree
b = principled(m.name)
b.inputs['Base Color'].default_value = (BASE, BASE, BASE * 1.06, 1)
b.inputs['Metallic'].default_value = 0.0
b.inputs['Specular IOR Level'].default_value = SPEC
for n in list(t.nodes):
    if n.type in ('TEX_NOISE', 'BUMP'):
        t.nodes.remove(n)
tc = t.nodes.new('ShaderNodeTexCoord')
nz = t.nodes.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = 9000.0; nz.inputs['Detail'].default_value = 4.0
bp = t.nodes.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = GRAIN; bp.inputs['Distance'].default_value = 0.00002
t.links.new(tc.outputs['Object'], nz.inputs['Vector'])
t.links.new(nz.outputs['Fac'], bp.inputs['Height'])
t.links.new(bp.outputs['Normal'], b.inputs['Normal'])
mr = t.nodes.new('ShaderNodeMapRange')            # a little roughness variation
mr.inputs['To Min'].default_value = ROUGH - 0.05; mr.inputs['To Max'].default_value = ROUGH + 0.05
t.links.new(nz.outputs['Fac'], mr.inputs['Value']); t.links.new(mr.outputs['Result'], b.inputs['Roughness'])

# --- laser etched text: light grey, rough, no gloss ------------------------------------------
p = principled('White print')
p.inputs['Base Color'].default_value = (ETCH, ETCH, ETCH * 1.01, 1)
p.inputs['Roughness'].default_value = 0.9
p.inputs['Specular IOR Level'].default_value = 0.2

# steel ring under the buttons: satin, not mirror
st = principled('Brushed steel')
st.inputs['Roughness'].default_value = 0.5
st.inputs['Base Color'].default_value = (0.35, 0.35, 0.36, 1)

# --- render settings ------------------------------------------------------------------------
s.render.engine = 'CYCLES'; s.cycles.samples = SAMPLES; s.cycles.use_denoising = True
s.cycles.transmission_bounces = 8; s.cycles.max_bounces = 12
s.render.film_transparent = True
s.view_settings.view_transform = 'AgX'; s.view_settings.look = 'AgX - Base Contrast'
bg = s.world.node_tree.nodes.get('Background')
bg.inputs[0].default_value = (WORLD, WORLD, WORLD * 1.05, 1); bg.inputs[1].default_value = 1.0

cam = bpy.data.objects['Camera']
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 0.0632           # 63.2 mm across 1896 pixels
cam.location = (0, -0.3, (0.03217 - 0.001963) / 2); cam.rotation_euler = (math.pi / 2, 0, 0)
s.render.resolution_x = 1896; s.render.resolution_y = 1024; s.render.resolution_percentage = 100


def area(name, loc, size, energy):
    light = bpy.data.lights.new(name, 'AREA'); light.energy = energy
    light.shape = 'RECTANGLE'; light.size = size[0]; light.size_y = size[1]
    o = bpy.data.objects.new(name, light); s.collection.objects.link(o); o.location = loc
    o.rotation_euler = (mathutils.Vector((0, 0, 0.012)) - mathutils.Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    return o


area('Key', (0, -0.22, 0.30), (0.7, 0.5), KEY)                   # soft light from above
fill = area('Fill', (0, -0.45, -0.05), (0.8, 0.4), FILL)          # lifts the matte face
fill.visible_glossy = False     # it is right behind the camera: the glass would mirror it as grey
area('RimL', (-0.35, 0.02, 0.10), (0.1, 0.3), RIM)                # faint edges on a black page
area('RimR', (0.35, 0.02, 0.10), (0.1, 0.3), RIM)

s.render.image_settings.file_format = 'PNG'; s.render.image_settings.color_mode = 'RGBA'
s.render.filepath = OUT
bpy.ops.render.render(write_still=True)
