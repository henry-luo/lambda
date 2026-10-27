#!/usr/bin/env python3
"""Writes the procedural textures of the standard-material fixtures (mod_pbr.ls).

Run from the repository root: python3 test/scene3d/assets/pbr/_generate.py (needs Pillow)
Every map is 128 x 128 and drawn top-down, as images are stored; texture
coordinate v runs up, so image row 0 is v = 1.
"""
import math
from pathlib import Path
from PIL import Image

SIZE = 128
root = Path(__file__).resolve().parent


def write(name, pixel):
    image = Image.new('RGBA', (SIZE, SIZE))
    image.putdata([pixel((x + .5) / SIZE, 1 - (y + .5) / SIZE) for y in range(SIZE) for x in range(SIZE)])
    image.save(root / name, optimize=True)


def byte(v):
    return max(0, min(255, round(v * 255)))


# base color: an 8 x 8 checker of two colors
write('checker.png', lambda u, v: (216, 163, 94, 255) if (int(u * 8) + int(v * 8)) % 2 else (94, 127, 216, 255))

# metalness in B as four stripes across u, roughness in G rising with v (glTF packing; R unused)
write('metal_rough.png', lambda u, v: (255, byte(.1 + .9 * v), 255 if int(u * 4) % 2 else 0, 255))


# tangent-space normals of a 4 x 4 grid of domes: +X along +u, +Y along +v
def dome_height(u, v):
    du, dv = (u * 4) % 1 - .5, (v * 4) % 1 - .5
    r2 = du * du + dv * dv
    return math.sqrt(.16 - r2) if r2 < .16 else 0.0


def normal(u, v):
    e = 1 / SIZE
    hx = (dome_height(u + e, v) - dome_height(u - e, v)) / (2 * e)
    hy = (dome_height(u, v + e) - dome_height(u, v - e)) / (2 * e)
    n = (-hx * .02, -hy * .02, 1.0)
    length = math.sqrt(sum(c * c for c in n))
    return tuple(byte((c / length + 1) / 2) for c in n) + (255,)


write('normal.png', normal)

# occlusion in R: dark rings in the creases between the domes
write('occlusion.png', lambda u, v: (byte(.25 + .75 * min(1, 2.5 * dome_height(u, v) + .2)),) * 3 + (255,))

# emission: warm stripes along v on black
write('emissive.png', lambda u, v: (255, 170, 60, 255) if int(v * 6) % 2 else (0, 0, 0, 255))


# a leaf-like cutout: discs of color, transparent between them, with a soft edge for the cutoff
def leaf(u, v):
    du, dv = (u * 3) % 1 - .5, (v * 3) % 1 - .5
    alpha = max(0, min(1, (.42 - math.sqrt(du * du + dv * dv)) * 20))
    return (78, 150, 70, byte(alpha))


write('leaf.png', leaf)


# pbr.gltf: a quad with tangents and three materials over these maps, for the glTF
# material mapping (test/lambda/scene3d/formats.ls)
def gltf():
    import base64, json, struct
    positions = [-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0]
    normals = [0, 0, 1] * 4
    uvs = [0, 1, 1, 1, 1, 0, 0, 0]  # glTF image UVs start at the top
    tangents = [1, 0, 0, 1] * 4
    indices = [0, 1, 2, 0, 2, 3]
    blob = b''.join(struct.pack('<%df' % len(a), *a) for a in (positions, normals, uvs, tangents)) + struct.pack('<6H', *indices)
    views, offset = [], 0
    for size in (48, 48, 32, 64, 12):
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': size})
        offset += size
    accessors = [{'bufferView': 0, 'componentType': 5126, 'count': 4, 'type': 'VEC3', 'min': [-1, -1, 0], 'max': [1, 1, 0]},
                 {'bufferView': 1, 'componentType': 5126, 'count': 4, 'type': 'VEC3'},
                 {'bufferView': 2, 'componentType': 5126, 'count': 4, 'type': 'VEC2'},
                 {'bufferView': 3, 'componentType': 5126, 'count': 4, 'type': 'VEC4'},
                 {'bufferView': 4, 'componentType': 5123, 'count': 6, 'type': 'SCALAR'}]
    tiled = {'KHR_texture_transform': {'scale': [2, 2]}}
    shifted = {'KHR_texture_transform': {'offset': [.5, 0]}}
    materials = [
        {'name': 'panel', 'doubleSided': True, 'alphaMode': 'MASK', 'alphaCutoff': .3, 'emissiveFactor': [1, .5, .2],
         'pbrMetallicRoughness': {'baseColorTexture': {'index': 0, 'extensions': tiled}, 'metallicFactor': .9, 'roughnessFactor': .7,
                                  'metallicRoughnessTexture': {'index': 1, 'extensions': tiled}},
         'normalTexture': {'index': 2, 'scale': .8, 'extensions': tiled}, 'occlusionTexture': {'index': 3, 'strength': .6, 'extensions': tiled},
         'emissiveTexture': {'index': 4, 'extensions': tiled}},
        {'name': 'plain'},
        {'name': 'mixed', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0, 'extensions': tiled}},
         'normalTexture': {'index': 2, 'extensions': shifted}}]
    attributes = {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2, 'TANGENT': 3}
    doc = {'asset': {'version': '2.0', 'generator': 'test/scene3d/assets/pbr/_generate.py'},
           'extensionsUsed': ['KHR_texture_transform'], 'scene': 0, 'scenes': [{'nodes': [0, 1, 2]}],
           'nodes': [{'mesh': i, 'translation': [i * 2.5 - 2.5, 0, 0]} for i in range(3)],
           'meshes': [{'primitives': [{'attributes': attributes, 'indices': 4, 'material': i}]} for i in range(3)],
           'materials': materials,
           'textures': [{'source': i} for i in range(5)],
           'images': [{'uri': name} for name in ('checker.png', 'metal_rough.png', 'normal.png', 'occlusion.png', 'emissive.png')],
           'buffers': [{'byteLength': len(blob), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode()}],
           'bufferViews': views, 'accessors': accessors}
    (root / 'pbr.gltf').write_text(json.dumps(doc, indent=1) + '\n')


gltf()
print('wrote', sorted(p.name for p in root.glob('*.png')), 'and pbr.gltf')
