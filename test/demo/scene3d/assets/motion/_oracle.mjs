// Reference poses for the motion fixtures from independent implementations
// (vibe/Lambda_Pkg_Motion.md §10): the pinned Three.js 0.186.1 BVHLoader,
// FBXLoader and SkeletonUtils, and, when UFBX_DUMP names a build of
// _ufbx_dump.c against pinned ufbx v0.23.1, ufbx. Writes oracle.json, which
// test/lambda/scene3d/motion_oracle.ls compares the Lambda converters with.
//
//   UFBX_DUMP=temp/oracle/bin/ufbx_dump node test/demo/scene3d/assets/motion/_oracle.mjs
//
// With MIXAMO_DIR set it runs the local-only Mixamo check instead (§11): Mixamo
// exports are never committed, so it reads them from that directory and writes
// its oracle.json there, for test/lambda/scene3d/_mixamo_local.ls.
//
// Every position is in character space: metres, Y-up, +Z front.
import { registerHooks } from 'node:module';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const vendor = join(here, '../../vendor/three');
// the vendored addons import 'three' unmodified; resolve it to the pinned build
registerHooks({
  resolve(specifier, context, next) {
    if (specifier === 'three') return { url: pathToFileURL(join(vendor, 'three.module.js')).href, shortCircuit: true };
    return next(specifier, context);
  },
});
// FBXLoader starts image loads for embedded textures and sizes cameras from the
// window; neither affects poses, so inert stand-ins suffice outside a browser
globalThis.window = { innerWidth: 640, innerHeight: 480 };
globalThis.document = { createElementNS: () => ({ addEventListener() {}, removeEventListener() {}, style: {} }) };
globalThis.self = globalThis;

const THREE = await import('three');
const { BVHLoader } = await import(pathToFileURL(join(vendor, 'addons/loaders/BVHLoader.js')).href);
const { FBXLoader } = await import(pathToFileURL(join(vendor, 'addons/loaders/FBXLoader.js')).href);
const SkeletonUtils = await import(pathToFileURL(join(vendor, 'addons/utils/SkeletonUtils.js')).href);

const round = x => Math.round(x * 1e9) / 1e9;
const vec = v => [round(v.x), round(v.y), round(v.z)];
// 10 µm units: dense meshes place distinct vertices under a millimetre apart
const key = (v, s) => [v.x, v.y, v.z].map(c => Math.round(c * s * 100000)).join(',');

// a clip held on its last key, so sampling at its duration does not wrap to the start
function play(mixer, clip) {
  if (!clip) return null;
  const action = mixer.clipAction(clip);
  action.setLoop(THREE.LoopOnce, 1);
  action.clampWhenFinished = true;
  return action.play();
}
// world positions of every named bone (or node) at each time of a clip
function sample(root, clip, times, scale, nodes) {
  const mixer = new THREE.AnimationMixer(root);
  const action = play(mixer, clip);
  const out = {};
  for (const t of times) {
    if (action) mixer.setTime(t);
    root.updateMatrixWorld(true);
    const pose = {};
    root.traverse(o => {
      if (!o.name || (nodes && !nodes(o))) return;
      pose[o.name] = vec(new THREE.Vector3().setFromMatrixPosition(o.matrixWorld).multiplyScalar(scale));
    });
    out[String(t)] = pose;
  }
  if (action) mixer.uncacheRoot(root);
  return out;
}
// skinned vertices keyed by their bind-time position
function skinned(root, clip, times, scale) {
  const meshes = [];
  root.traverse(o => { if (o.isSkinnedMesh) meshes.push(o); });
  root.updateMatrixWorld(true);
  // FBXLoader binds at the mesh's world placement, including the Z-up turn of its root
  const keys = meshes.map(mesh => {
    const positions = mesh.geometry.attributes.position;
    return Array.from({ length: positions.count }, (_, i) =>
      key(new THREE.Vector3().fromBufferAttribute(positions, i).applyMatrix4(mesh.matrixWorld), scale));
  });
  const mixer = new THREE.AnimationMixer(root);
  const action = play(mixer, clip);
  const out = {};
  for (const t of times) {
    if (action) mixer.setTime(t);
    root.updateMatrixWorld(true);
    const posed = {};
    meshes.forEach((mesh, m) => {
      for (let i = 0; i < keys[m].length; i++) {
        const v = mesh.getVertexPosition(i, new THREE.Vector3()).applyMatrix4(mesh.matrixWorld).multiplyScalar(scale);
        posed[keys[m][i]] = vec(v);
      }
    });
    out[String(t)] = posed;
  }
  return out;
}

const mixamo = process.env.MIXAMO_DIR;
const root = mixamo ? resolve(mixamo) : here;
const read = name => readFileSync(join(root, name));
const buffer = name => { const b = read(name); return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength); };
const fbx = name => new FBXLoader().parse(buffer(name), root + '/');
const unit = group => (group.userData.unitScaleFactor ?? 1) / 100;
const bones = o => o.isBone;
const frames = (count, rate) => Array.from({ length: count }, (_, k) => k / rate);

const oracle = { three: { bvh: {}, fbx: {}, skinned: {}, retarget: {} }, ufbx: {} };

// BVH: the same files the converter reads, scaled from centimetres
for (const [name, times] of mixamo ? [] : [['jump', [0, 8 / 30, 0.5, 1]], ['offsets', [0, 0.5, 1]]]) {
  const result = new BVHLoader().parse(read(name + '.bvh').toString());
  const group = new THREE.Group();
  group.add(result.skeleton.bones[0]);
  oracle.three.bvh[name] = sample(group, result.clip, times, 0.01);
}

// FBX: generated files and the CC0 character
// the Mixamo exports Three.js r186 ships: a character with its dance, and a samba
const mixamoTimes = [0, 1, 5, 10, 16];
const fbxCases = mixamo ? [
  ['mixamo.fbx', null, [0]],
  ['mixamo.fbx', 'mixamo.com', mixamoTimes],
  ['Samba Dancing.fbx', null, [0]],
  ['Samba Dancing.fbx', 'mixamo.com', mixamoTimes],
] : [
  ['jump.fbx', 'jump', [0, 8 / 30, 0.5, 1]],
  ['jump-ascii.fbx', 'jump', [0, 8 / 30, 0.5, 1]],
  ['jump-7500.fbx', 'jump', [0, 8 / 30, 0.5, 1]],
  ['rig.fbx', 'curl', [0, 0.5]],
  ['rig.fbx', 'wave', [0, 0.5]],
  ['rig.fbx', 'sway', [0.3, 0.7, 1.5]],
  ['kenney/characterMedium.fbx', null, [0]],
  ['kenney/jump.fbx', 'Root|Jump', [0, 0.25, 0.5]],
];
for (const [file, clipName, times] of fbxCases) {
  const group = fbx(file);
  const clip = clipName ? group.animations.find(a => a.name === clipName) : null;
  if (clipName && !clip) throw new Error(`${file}: no clip ${clipName}`);
  const key = file + (clipName ? '#' + clipName : '');
  oracle.three.fbx[key] = sample(group, clip, times, unit(group), o => o.isBone || o.type === 'Object3D' || o.isGroup);
  const skin = skinned(group, clip, times, unit(group));
  if (Object.keys(skin[String(times[0])]).length) oracle.three.skinned[key] = skin;
}

// SkeletonUtils.retargetClip with local offsets W_source⁰⁻¹ · W_target⁰, the
// world-change-from-rest transfer of §8.4: the CMU-named jump onto the A-pose
if (!mixamo) {
  const source = new BVHLoader().parse(read('jump.bvh').toString());
  const holder = new THREE.Group();
  holder.add(source.skeleton.bones[0]);
  holder.updateMatrixWorld(true);
  const target = fbx('apose.fbx');
  target.updateMatrixWorld(true);
  const targetBones = [];
  target.traverse(o => { if (o.isBone) targetBones.push(o); });
  const mesh = new THREE.SkinnedMesh(new THREE.BufferGeometry(), new THREE.MeshBasicMaterial());
  mesh.add(targetBones[0]);
  mesh.bind(new THREE.Skeleton(targetBones));
  mesh.updateMatrixWorld(true);
  const rename = { Spine: 'LowerBack', Spine1: 'Spine', Spine2: 'Spine1' };
  const names = {}, localOffsets = {};
  const rotation = o => new THREE.Matrix4().extractRotation(o.matrixWorld);
  for (const bone of targetBones) {
    const base = bone.name.replace(/^mixamorig/, '');
    const sourceName = rename[base] ?? base;
    const from = source.skeleton.getBoneByName(sourceName);
    if (!from) continue;
    names[bone.name] = sourceName;
    localOffsets[bone.name] = rotation(from).invert().multiply(rotation(bone));
  }
  const clip = SkeletonUtils.retargetClip(mesh, source.skeleton, source.clip, { names, localOffsets, hip: 'Hips' });
  oracle.three.retarget['apose#jump'] = sample(mesh, clip, [0, 8 / 30, 0.5, 1], 0.01, bones);
}

// ufbx, when a dump tool is available
const dump = process.env.UFBX_DUMP;
if (dump && existsSync(dump)) {
  for (const [file, clipName, times] of fbxCases) {
    const result = JSON.parse(execFileSync(dump, [join(root, file), ...times.map(String)], { maxBuffer: 1 << 30 }).toString());
    const key = file + (clipName ? '#' + clipName : '');
    // the first stack of that name, as FBXLoader's lookup and the converter's unsuffixed clip take it
    const stack = clipName ? result.stacks.find(s => s.name === clipName) : null;
    oracle.ufbx[key] = stack ? { nodes: Object.fromEntries(Object.entries(stack.samples).map(([t, s]) => [t, s.nodes])),
      skin: Object.fromEntries(Object.entries(stack.samples).map(([t, s]) => [t, s.skin])),
      static: Object.fromEntries(Object.entries(stack.samples).map(([t, s]) => [t, s.static])) }
      : { nodes: { '0': result.rest.nodes }, skin: { '0': result.rest.skin }, static: { '0': result.rest.static } };
  }
}

writeFileSync(join(root, 'oracle.json'), JSON.stringify(oracle) + '\n');
console.log('oracle.json:', Object.keys(oracle.three.fbx).length, 'Three.js FBX cases,', Object.keys(oracle.ufbx).length, 'ufbx cases');
