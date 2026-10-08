"""Regenerate the tiny, CC0 asset-loading fixtures with Python's standard library."""
import base64, json, math, pathlib, struct, zlib
root = pathlib.Path(__file__).parent

def png(alpha=(255,255,255,255)):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b'\0' + bytes([255, 255, 255, alpha[0], 40, 100, 255, alpha[1]]) + b'\0' + bytes([255, 120, 40, alpha[2], 255, 255, 255, alpha[3]])
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
image = png()
(root / 'textures/checker.png').write_bytes(image)
(root / 'materials/palette.mtl').write_text('newmtl painted\nKd 1 1 1\nd 1\nmap_Kd ../textures/checker.png\n')
(root / 'concave.obj').write_text('''# CC0: concave textured arrow; negative indices and independent UV/normal indices
mtllib materials/palette.mtl
o Arrow
g 007
v -.9 -.6 0
v .25 -.6 0
v .25 -.9 0
v .95 0 0
v .25 .9 0
v .25 .6 0
v -.9 .6 0
vt 0 0
vt .6 0
vt .6 0
vt 1 .5
vt .6 1
vt .6 1
vt 0 1
vn 0 0 1
s 1
usemtl painted
f -7/1/1 -6/2/1 -5/3/1 -4/4/1 -3/5/1 -2/6/1 -1/7/1
''')
positions = [-.8,-.8,0, .1,-.8,0, .1,0,0, -.8,0,0, -.8,0,0, .1,0,0, .1,.8,0, -.8,.8,0]
blob = bytearray()
views, accessors = [], []
def view(data, stride=None):
    while len(blob) % 4: blob.append(0)
    v = {'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)}
    if stride: v['byteStride'] = stride
    views.append(v); blob.extend(data)
    return len(views)-1

def accessor(values, kind, ct=5126, normalized=False, stride=False):
    count = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[kind]
    fmt = {5126:'f',5123:'H',5121:'B'}[ct]
    if stride:
        data = b''.join(struct.pack('<'+fmt*count, *values[i:i+count])+b'\0'*4 for i in range(0,len(values),count))
    else: data = struct.pack('<'+fmt*len(values), *values)
    item = {'bufferView': view(data, count*4+4 if stride else None), 'componentType':ct, 'count':len(values)//count, 'type':kind}
    if normalized: item['normalized'] = True
    accessors.append(item); return len(accessors)-1
p = accessor(positions,'VEC3',stride=True)
n = accessor([0,0,1]*8,'VEC3')
uv = accessor([0,0,1,0,1,1,0,1]*2,'VEC2')
joints = accessor([1,0,0,0]*4+[0,0,0,0]*4,'VEC4',5121)
weights = accessor([255,0,0,0]*8,'VEC4',5121,True)
indices = accessor([0,1,2,0,2,3,4,5,6,4,6,7],'SCALAR',5123)
colors = accessor([100,150,255]*4+[255,175,100]*4,'VEC3',5121,True)
ib = []
for y in [-.3,0]: ib += [1,0,0,0,0,1,0,0,0,0,1,0,-.2,y,0,1]
bind = accessor(ib,'MAT4')
sparse_index = view(bytes([6]))
sparse_value = view(struct.pack('<fff', .5,0,0))
accessors.append({'componentType':5126,'count':8,'type':'VEC3','sparse':{'count':1,'indices':{'bufferView':sparse_index,'componentType':5121},'values':{'bufferView':sparse_value}}})
morph0 = len(accessors)-1
morph1 = accessor([0,.15,0]*8,'VEC3')
time = accessor([0,1,2],'SCALAR')
rot = accessor([0,0,0,1, 0,0,math.sin(.4),math.cos(.4), 0,0,0,1],'VEC4')
cubic_time = accessor([0,2],'SCALAR')
cubic = accessor([0,0,0, 0,.3,0, 1.2,0,0, -1.2,0,0, 0,.3,0, 0,0,0], 'VEC3')
morph = accessor([0,0,1,.5,0,0],'SCALAR')
triangle = accessor([-.2,-.2,0,.2,-.2,0,0,.2,0],'VEC3')
# glTF's default OPAQUE mode must preserve RGB even when the image has alpha.
picture = view(png((0,64,128,192)))
for index in [p,triangle]:
    values = positions if index == p else [-.2,-.2,0,.2,-.2,0,0,.2,0]
    accessors[index]['min'] = [min(values[c::3]) for c in range(3)]
    accessors[index]['max'] = [max(values[c::3]) for c in range(3)]
gltf = {
 'asset':{'version':'2.0','generator':'Lambda CC0 asset fixture'},
 'extensionsUsed':['KHR_materials_unlit'], 'extensionsRequired':['KHR_materials_unlit'],
 'buffers':[{'uri':'articulated.bin','byteLength':len(blob)}], 'bufferViews':views,'accessors':accessors,
 'images':[{'bufferView':picture,'mimeType':'image/png'}], 'textures':[{'source':0}],
 'materials':[{'extensions':{'KHR_materials_unlit':{}},'pbrMetallicRoughness':{'baseColorTexture':{'index':0}},'doubleSided':True},
              {'extensions':{'KHR_materials_unlit':{}},'pbrMetallicRoughness':{'baseColorFactor':[.05,.8,.3,1]}}],
 'meshes':[{'weights':[0,0],'primitives':[{'attributes':{'POSITION':p,'NORMAL':n,'TEXCOORD_0':uv,'JOINTS_0':joints,'WEIGHTS_0':weights,'COLOR_0':colors},'indices':indices,'material':0,'targets':[{'POSITION':morph0},{'POSITION':morph1}]}]},
           {'primitives':[{'attributes':{'POSITION':triangle},'material':1}]}],
 'nodes':[{'name':'Base','translation':[.2,0,0],'children':[1]}, {'name':'Tip','translation':[0,.3,0]}, {'name':'Cloth','mesh':0,'skin':0,'translation':[.4,-.2,0]},
          {'name':'Matrix marker','mesh':1,'matrix':[1,0,0,0,0,1,0,0,0,0,1,0,.8,.65,0,1]},
          {'name':'Quaternion marker','mesh':1,'translation':[.8,-.65,0],'rotation':[0,0,math.sin(.35),math.cos(.35)]}],
 'skins':[{'joints':[1,0],'inverseBindMatrices':bind}],
 'animations':[{'name':'Wave','samplers':[{'input':time,'output':rot,'interpolation':'LINEAR'}, {'input':cubic_time,'output':cubic,'interpolation':'CUBICSPLINE'}, {'input':time,'output':morph,'interpolation':'STEP'}],
                'channels':[{'sampler':0,'target':{'node':0,'path':'rotation'}}, {'sampler':1,'target':{'node':1,'path':'translation'}}, {'sampler':2,'target':{'node':2,'path':'weights'}}]}],
 'scenes':[{'nodes':[0,2,3,4]}], 'scene':0
}
(root / 'articulated.bin').write_bytes(blob)
(root / 'articulated.gltf').write_text(json.dumps(gltf,indent=2)+'\n')
# The same accessor fixture with a data URI exercises embedded buffers independently.
gltf['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(blob).decode()
(root / 'embedded.gltf').write_text(json.dumps(gltf, separators=(',',':'))+'\n')
(root / 'puppet.a3d').write_text('''3dmodel 1.0
Articulated pennant
CC0
Lambda
Two bones with persistent partial frame overrides

Vertex
-.8 -.8 0 1 #ff6496ff 0
.1 -.8 0 1 #FF6496FF 0
.1 0 0 1 #FF6496FF 0
-.8 0 0 1 #FF6496FF 0
-.8 0 0 1 #FFFFAF64 1
.1 0 0 1 #FFFFAF64 1
.1 .8 0 1 #FFFFAF64 1
-.8 .8 0 1 #FFFFAF64 1
.2 0 0 1
0 0 0 1
0 .3 0 1
.6 .3 0 1
0 0 .3894183423 .9210609940

Bones
8 9 Base
/10 9 Tip

Material white
Kd #FFFFFFFF

Mesh pennant
use white
0 1 2 3
4 5 6 7

Action 2000 Wave
frame 0
0 8 9
1 10 9
frame 500
0 8 12
frame 1000
1 11 9
frame 1500
0 8 9
frame 2000
1 10 9

End
''')
