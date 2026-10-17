import * as THREE from './vendor/three/three.module.js';
import { ImprovedNoise } from './vendor/three/addons/math/ImprovedNoise.js';
import { OrbitControls } from './vendor/three/addons/controls/OrbitControls.js';

const canvas = document.getElementById('garden');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
renderer.setPixelRatio(1);
renderer.setSize(800, 500, false);
renderer.setClearColor(0x080f22, 1);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(42, 800 / 500, 0.1, 100);
camera.position.set(8, 7, 11);
camera.lookAt(0, 0.4, 0);
const controls = new OrbitControls(camera, canvas);
controls.target.set(0, 0.4, 0);
controls.minDistance = 4;
controls.maxDistance = 35;
controls.minPolarAngle = 0.08;
controls.maxPolarAngle = Math.PI * 0.49;
controls.maxTargetRadius = 8;
controls.screenSpacePanning = true;
controls.listenToKeyEvents(canvas);
controls.update();
controls.saveState();
scene.add(new THREE.AmbientLight(0x809fff, 1.4));
const light = new THREE.DirectionalLight(0xffd2b0, 3);
light.position.set(4, 8, 3);
scene.add(light);

const noise = new ImprovedNoise();
const geometry = new THREE.ConeGeometry(0.38, 2.4, 5);
const material = new THREE.MeshLambertMaterial({ color: 0x71e6d0, flatShading: true });
const crystals = new THREE.InstancedMesh(geometry, material, 49);
const matrix = new THREE.Matrix4();
const transform = new THREE.Object3D();
let index = 0;
for (let x = -3; x <= 3; x++) {
    for (let z = -3; z <= 3; z++) {
        const height = 0.4 + (noise.noise(x * 0.55, z * 0.55, 0.3) + 1) * 0.65;
        transform.position.set(x * 0.86, height * 1.2, z * 0.86);
        transform.rotation.set(0.05 * z, x * 0.4, 0.08 * x);
        transform.scale.set(0.8, height, 0.8);
        transform.updateMatrix();
        matrix.copy(transform.matrix);
        crystals.setMatrixAt(index, matrix);
        crystals.setColorAt(index, new THREE.Color().setHSL(0.45 + (x + z + 6) / 70, 0.7, 0.6));
        index++;
    }
}
crystals.instanceMatrix.needsUpdate = true;
crystals.instanceColor.needsUpdate = true;
scene.add(crystals);

const checker = new Uint8Array([
    25, 38, 64, 255, 47, 66, 96, 255,
    47, 66, 96, 255, 25, 38, 64, 255
]);
const texture = new THREE.DataTexture(checker, 2, 2, THREE.RGBAFormat);
texture.colorSpace = THREE.SRGBColorSpace;
texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
texture.repeat.set(12, 12);
texture.magFilter = THREE.NearestFilter;
texture.needsUpdate = true;
const ground = new THREE.Mesh(new THREE.PlaneGeometry(9, 9), new THREE.MeshBasicMaterial({ map: texture }));
ground.rotation.x = -Math.PI / 2;
scene.add(ground);

const ring = new THREE.Mesh(new THREE.TorusGeometry(3.9, 0.045, 8, 96), new THREE.MeshBasicMaterial({ color: 0xffc17a }));
ring.rotation.x = Math.PI / 2;
ring.position.y = 0.15;
scene.add(ring);
const halo = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 16), new THREE.MeshBasicMaterial({ color: 0xaab9ff, transparent: true, opacity: 0.2, depthWrite: false }));
halo.position.set(0, 3.8, 0);
scene.add(halo);
// redraw on camera changes; the static garden needs no recurring frame loop.
controls.addEventListener('change', () => renderer.render(scene, camera));
function mode(pan) {
    controls.mouseButtons.LEFT = pan ? THREE.MOUSE.PAN : THREE.MOUSE.ROTATE;
    document.getElementById('orbit-mode').setAttribute('aria-pressed', String(!pan));
    document.getElementById('pan-mode').setAttribute('aria-pressed', String(pan));
}
function reset() { controls.reset(); mode(false); }
const actions = {
    'orbit-mode': () => mode(false), 'pan-mode': () => mode(true),
    'orbit-left': () => controls.rotateLeft(0.16),
    'orbit-right': () => controls.rotateLeft(-0.16),
    'tilt-up': () => controls.rotateUp(0.12),
    'tilt-down': () => controls.rotateUp(-0.12),
    'zoom-in': () => controls.dollyIn(0.85),
    'zoom-out': () => controls.dollyOut(0.85),
    reset
};
for (const id of Object.keys(actions)) document.getElementById(id).addEventListener('click', actions[id]);
// OrbitControls consumes pointer defaults, so explicitly focus the keyboard target.
canvas.addEventListener('pointerdown', () => canvas.focus());
canvas.addEventListener('keydown', event => {
    const key = event.key.toLowerCase();
    if (key === '+' || key === '=') actions['zoom-in']();
    else if (key === '-' || key === '_') actions['zoom-out']();
    else if (key === 'r') reset();
    else return;
    event.preventDefault();
});
window.addEventListener('pagehide', () => {
    controls.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
});
renderer.render(scene, camera);
document.getElementById('status').textContent = '49 instanced crystals · Lambert lighting · data texture · transparent halo';
globalThis.threeGallery = { renderer, scene, camera, controls, reset, crystals, texture, geometry, material };
document.getElementById('status').setAttribute('data-ready', 'true');
globalThis.threeGalleryReady = true;
