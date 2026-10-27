// Renders one standard-material case description (written by _dump.ls) with
// the pinned Three.js r186, as the reference the native renderer is compared with.
import * as THREE from 'three';
import {RoomEnvironment} from '../../demo/scene3d/vendor/three/addons/environments/RoomEnvironment.js';

const params = new URLSearchParams(location.search);
const d = await (await fetch(params.get('data'))).json();

const renderer = new THREE.WebGLRenderer({canvas: document.getElementById('view'), antialias: true});
renderer.setPixelRatio(1);
renderer.setSize(d.width, d.height);
renderer.toneMapping = {none: THREE.NoToneMapping, neutral: THREE.NeutralToneMapping, aces: THREE.ACESFilmicToneMapping}[d.tone];
renderer.toneMappingExposure = d.exposure;

const scene = new THREE.Scene();
scene.background = new THREE.Color(d.background);
const camera = new THREE.PerspectiveCamera(d.camera.fov, d.width / d.height, 0.1, 1000);
camera.position.fromArray(d.camera.position);
camera.lookAt(new THREE.Vector3().fromArray(d.camera.target));

// native intensities carry the physical convention's π (SP1); Three.js lights are physical
if (d.ambient > 0) scene.add(new THREE.AmbientLight(0xffffff, d.ambient * Math.PI));
const casting = [];
for (const l of d.lights) {
  const light = l.type === 'point' ? new THREE.PointLight(l.color, l.intensity * Math.PI, 0, 2)
    : l.type === 'spot' ? new THREE.SpotLight(l.color, l.intensity * Math.PI, l.distance, l.angle, l.penumbra, 2)
    : new THREE.DirectionalLight(l.color, l.intensity * Math.PI);
  light.position.fromArray(l.position);
  scene.add(light);
  if (light.target) scene.add(light.target);
  if (l.shadow) {
    light.castShadow = true;
    light.shadow.mapSize.set(l.shadow.size, l.shadow.size);
    light.shadow.bias = l.shadow.bias;
    light.shadow.normalBias = l.shadow.normal_bias;
    light.shadow.radius = l.shadow.softness;
    casting.push({light, l});
  }
}
renderer.shadowMap.enabled = casting.length > 0;
if (d.hemisphere) scene.add(new THREE.HemisphereLight(d.hemisphere.sky, d.hemisphere.ground, d.hemisphere.intensity * Math.PI));
// the native room is a port of this RoomEnvironment, captured and prefiltered without blur
if (d.environment === 'room') scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0).texture;
// An image environment, decoded here as r186's RGBELoader decodes it (no RGBELoader is vendored):
// adaptive run-length scanlines, mantissa / 255 * 2^(exponent - 128), half floats, flipped rows.
function decodeRgbe(bytes) {
  let offset = 0, line = '';
  const next = () => { line = ''; while (bytes[offset] !== 10) line += String.fromCharCode(bytes[offset++]); offset++; return line; };
  while (next() !== '');
  const [, height, width] = next().match(/^-Y (\d+) \+X (\d+)$/).map(Number);
  const rgbe = new Uint8Array(width * height * 4), scan = new Uint8Array(width * 4);
  for (let y = 0; y < height; y++) {
    offset += 4;
    for (let c = 0; c < 4; c++) for (let x = 0; x < width;) {
      let count = bytes[offset++];
      if (count > 128) { count -= 128; const v = bytes[offset++]; while (count--) scan[(x++) * 4 + c] = v; }
      else while (count--) scan[(x++) * 4 + c] = bytes[offset++];
    }
    rgbe.set(scan, y * width * 4);
  }
  const data = new Uint16Array(width * height * 4);
  for (let i = 0; i < width * height; i++) {
    const e = rgbe[i * 4 + 3], scale = e ? Math.pow(2.0, e - 128.0) / 255.0 : 0;
    for (let c = 0; c < 3; c++) data[i * 4 + c] = THREE.DataUtils.toHalfFloat(rgbe[i * 4 + c] * scale);
    data[i * 4 + 3] = THREE.DataUtils.toHalfFloat(1);
  }
  const texture = new THREE.DataTexture(data, width, height, THREE.RGBAFormat, THREE.HalfFloatType);
  texture.colorSpace = THREE.LinearSRGBColorSpace; texture.minFilter = texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = false; texture.flipY = true; texture.mapping = THREE.EquirectangularReflectionMapping;
  texture.needsUpdate = true;
  return texture;
}
if (d.environment_src) {
  const bytes = new Uint8Array(await (await fetch('../' + d.environment_src)).arrayBuffer());
  scene.environment = new THREE.PMREMGenerator(renderer).fromEquirectangular(decodeRgbe(bytes)).texture;
}

const geometries = {};
for (const [name, g] of Object.entries(d.geometries)) {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(g.positions, 3));
  geometry.setAttribute('normal', new THREE.Float32BufferAttribute(g.normals, 3));
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(g.uvs, 2));
  if (g.tangents) geometry.setAttribute('tangent', new THREE.Float32BufferAttribute(g.tangents, 4));
  geometry.setIndex(g.indices);
  geometries[name] = geometry;
}
// sampled as the native renderer samples: linear, no mipmaps, clamped; sRGB only for color
const loader = new THREE.TextureLoader();
const textures = {};
await Promise.all(Object.entries(d.textures).map(async ([name, t]) => {
  const texture = await loader.loadAsync('../' + t.file);
  texture.colorSpace = t.srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
  texture.minFilter = THREE.LinearFilter;
  texture.generateMipmaps = false;
  textures[name] = texture;
}));

const objects = [];
for (const b of d.objects) {
  const m = b.material;
  const material = new THREE.MeshStandardMaterial({color: m.color, metalness: m.metalness ?? 0, roughness: m.roughness ?? 1});
  if (m.map) material.map = textures[m.map];
  if (m.metal_rough) material.metalnessMap = material.roughnessMap = textures[m.metal_rough];
  if (m.normal) { material.normalMap = textures[m.normal]; material.normalScale.set(m.normal_scale, m.normal_scale); }
  if (m.occlusion) { material.aoMap = textures[m.occlusion]; material.aoMapIntensity = m.occlusion_strength; }
  if (m.emissive_map) { material.emissive.set(m.emissive); material.emissiveIntensity = m.emissive_intensity; material.emissiveMap = textures[m.emissive_map]; }
  if (m.mask) material.alphaTest = m.mask;
  if (m.double) material.side = THREE.DoubleSide;
  if (m.back) material.side = THREE.BackSide;
  const mesh = new THREE.Mesh(geometries[b.geometry], material);
  mesh.position.fromArray(b.position);
  if (b.rotation) mesh.rotation.fromArray(b.rotation);
  mesh.scale.setScalar(d.scale);
  // native meshes cast and receive by default (SP3)
  mesh.castShadow = mesh.receiveShadow = true;
  scene.add(mesh);
  objects.push(mesh);
}
// Frame each shadow camera as the native renderer does: depth fitted to the casters and receivers
// with a 1% margin, sideways to them unless the light gives an extent (spot lights: their reach);
// a point light's cube reaches the largest offset component of any corner
scene.updateMatrixWorld(true);
for (const {light, l} of casting) {
  const cam = light.shadow.camera;
  if (l.type === 'point') {
    const origin = new THREE.Vector3().setFromMatrixPosition(light.matrixWorld);
    let reach = 0;
    for (const mesh of objects) {
      mesh.geometry.computeBoundingBox();
      const b = mesh.geometry.boundingBox;
      for (let c = 0; c < 8; c++) {
        const p = new THREE.Vector3(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z);
        p.applyMatrix4(mesh.matrixWorld).sub(origin);
        reach = Math.max(reach, Math.abs(p.x), Math.abs(p.y), Math.abs(p.z));
      }
    }
    cam.far = l.distance > 0 ? l.distance : reach + Math.max(0.01 * reach, 0.01);
    cam.near = Math.max(cam.far * 0.001, 0.001);
    cam.updateProjectionMatrix();
    continue;
  }
  cam.position.setFromMatrixPosition(light.matrixWorld);
  cam.lookAt(new THREE.Vector3().setFromMatrixPosition(light.target.matrixWorld));
  cam.updateMatrixWorld();
  const lo = new THREE.Vector3(Infinity, Infinity, Infinity), hi = new THREE.Vector3(-Infinity, -Infinity, -Infinity);
  for (const mesh of objects) {
    mesh.geometry.computeBoundingBox();
    const b = mesh.geometry.boundingBox;
    for (let c = 0; c < 8; c++) {
      const p = new THREE.Vector3(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z);
      p.applyMatrix4(mesh.matrixWorld).applyMatrix4(cam.matrixWorldInverse);
      lo.min(p); hi.max(p);
    }
  }
  let near = -hi.z, far = -lo.z;
  const margin = Math.max(0.01 * (far - near), 0.01);
  if (l.type === 'spot') {
    far = l.distance > 0 ? l.distance : far + margin;
    cam.near = Math.min(Math.max(near - margin, far * 0.001), far * 0.999);
    cam.far = far;
  } else {
    const e = l.shadow.extent;
    cam.left = e ? -e : lo.x; cam.right = e ? e : hi.x; cam.bottom = e ? -e : lo.y; cam.top = e ? e : hi.y;
    cam.near = near - margin; cam.far = far + margin;
  }
  cam.updateProjectionMatrix();
}
renderer.render(scene, camera);
globalThis.pbrReady = true;
