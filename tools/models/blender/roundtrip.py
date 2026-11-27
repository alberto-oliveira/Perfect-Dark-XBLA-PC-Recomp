# Blender (headless) round trip for gun meshes: import a glTF, optionally make an edit,
# export glTF again for glb2xbla.py.
#
#   blender --background --factory-startup --python roundtrip.py -- <in.glb> <out.glb>
#           [--stretch AXIS SIGN CUT FACTOR]
#
# --stretch: every mesh vertex past CUT along the glTF axis AXIS (x, y or z) in direction
#   SIGN (+1 or -1) moves FACTOR times as far past it: e.g. "z +1 9 1.3" makes everything
#   in front of z = 9 30% longer, with UVs untouched (the texture stretches with it). It is
#   done in glTF space (Blender's import turns glTF Y-up into its Z-up: glTF (x, y, z) =
#   Blender (x, -z, y)... see to_gltf/from_gltf below).
import sys

import bpy


def args():
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    return a


# glTF -> Blender: (x, y, z) -> (x, -z, y); and back.
def to_gltf(v): return (v[0], v[2], -v[1])
def from_gltf(v): return (v[0], -v[2], v[1])


def stretch(axis, sign, cut, factor):
    k = 'xyz'.index(axis)
    moved = 0
    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        world = obj.matrix_world
        inverse = world.inverted()
        for vert in obj.data.vertices:
            g = list(to_gltf(world @ vert.co))
            past = (g[k] - cut) * sign
            if past > 0:
                g[k] = cut + sign * past * factor
                vert.co = inverse @ __import__('mathutils').Vector(from_gltf(g))
                moved += 1
        obj.data.update()
    print(f'[roundtrip] stretch: {moved} vertices past {axis} {cut:+} moved, factor {factor}')


def export(path):
    # The exporter's option names change between Blender versions: try the newer first.
    base = dict(filepath=path, export_format='GLB', export_skins=True, export_materials='EXPORT',
                export_yup=True, export_apply=False)
    for extra in (dict(export_vertex_color='ACTIVE', export_all_vertex_colors=True),
                  dict(export_vertex_color='ACTIVE'),
                  dict(export_colors=True),
                  dict()):
        try:
            bpy.ops.export_scene.gltf(**base, **extra)
            print('[roundtrip] exported with', extra or 'default options')
            return
        except TypeError as e:
            print('[roundtrip] exporter rejected', extra, ':', e)
    raise SystemExit('export failed')


def main():
    a = args()
    src, dst = a[0], a[1]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    print('[roundtrip] imported:', [(o.name, o.type) for o in bpy.data.objects])
    # The importer adds a mesh it draws bones with ("Icosphere"); it is no part of the gun.
    shapes = {b.custom_shape for o in bpy.data.objects if o.type == 'ARMATURE' for b in o.pose.bones if b.custom_shape}
    for o in list(bpy.data.objects):
        if o in shapes or (o.type == 'MESH' and o.name.startswith('Icosphere')):
            bpy.data.objects.remove(o, do_unlink=True)
    if '--stretch' in a:
        i = a.index('--stretch')
        stretch(a[i + 1], float(a[i + 2]), float(a[i + 3]), float(a[i + 4]))
    export(dst)


main()
