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
for (const l of d.lights) {
  const light = l.type === 'point' ? new THREE.PointLight(l.color, l.intensity * Math.PI, 0, 2)
    : new THREE.DirectionalLight(l.color, l.intensity * Math.PI);
  light.position.fromArray(l.position);
  scene.add(light);
}
if (d.hemisphere) scene.add(new THREE.HemisphereLight(d.hemisphere.sky, d.hemisphere.ground, d.hemisphere.intensity * Math.PI));
// the native room is a port of this RoomEnvironment, captured and prefiltered without blur
if (d.environment === 'room') scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0).texture;

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

for (const b of d.spheres) {
  const m = b.material;
  const material = new THREE.MeshStandardMaterial({color: m.color, metalness: m.metalness ?? 0, roughness: m.roughness ?? 1});
  if (m.map) material.map = textures[m.map];
  if (m.metal_rough) material.metalnessMap = material.roughnessMap = textures[m.metal_rough];
  if (m.normal) { material.normalMap = textures[m.normal]; material.normalScale.set(m.normal_scale, m.normal_scale); }
  if (m.occlusion) { material.aoMap = textures[m.occlusion]; material.aoMapIntensity = m.occlusion_strength; }
  if (m.emissive_map) { material.emissive.set(m.emissive); material.emissiveIntensity = m.emissive_intensity; material.emissiveMap = textures[m.emissive_map]; }
  if (m.mask) material.alphaTest = m.mask;
  if (m.double) material.side = THREE.DoubleSide;
  const mesh = new THREE.Mesh(geometries[b.geometry], material);
  mesh.position.fromArray(b.position);
  mesh.scale.setScalar(d.scale);
  scene.add(mesh);
}
renderer.render(scene, camera);
globalThis.pbrReady = true;
