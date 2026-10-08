import * as THREE from './vendor/three/three.module.js';
import { OrbitControls } from './vendor/three/addons/controls/OrbitControls.js';

const viewport = document.getElementById('ringworld');
const nativeCamera = document.getElementById('main');
const camera = new THREE.PerspectiveCamera(42, 1040 / 570, 0.1, 1000);
camera.position.set(7.5, 4.8, 11);
const controls = new OrbitControls(camera, viewport);
controls.minDistance = 7;
controls.maxDistance = 38;
controls.minPolarAngle = 0.08;
controls.maxPolarAngle = Math.PI - 0.08;
controls.maxTargetRadius = 8;
controls.screenSpacePanning = true;
controls.listenToKeyEvents(viewport);
controls.update();
controls.saveState();

// Three supplies camera/input math; the existing native scene owns every draw.
function syncCamera() {
    nativeCamera.setAttribute('position', camera.position.toArray().join(' '));
    nativeCamera.setAttribute('target', controls.target.toArray().join(' '));
}
controls.addEventListener('change', syncCamera);

function orbit(horizontal, vertical) {
    const offset = camera.position.clone().sub(controls.target);
    const spherical = new THREE.Spherical().setFromVector3(offset);
    spherical.theta += horizontal;
    spherical.phi = THREE.MathUtils.clamp(spherical.phi + vertical, controls.minPolarAngle, controls.maxPolarAngle);
    camera.position.copy(controls.target).add(new THREE.Vector3().setFromSpherical(spherical));
    controls.update();
}
function zoom(factor) {
    const offset = camera.position.clone().sub(controls.target);
    const distance = THREE.MathUtils.clamp(offset.length() * factor, controls.minDistance, controls.maxDistance);
    camera.position.copy(controls.target).add(offset.setLength(distance));
    controls.update();
}
function mode(pan) {
    controls.mouseButtons.LEFT = pan ? THREE.MOUSE.PAN : THREE.MOUSE.ROTATE;
    document.getElementById('orbit-mode').setAttribute('aria-pressed', String(!pan));
    document.getElementById('pan-mode').setAttribute('aria-pressed', String(pan));
}
function reset() { controls.reset(); mode(false); }
function preset(polar) {
    controls.target.set(0, 0, 0);
    camera.position.setFromSpherical(new THREE.Spherical(15, polar, 0.6));
    controls.update();
}

const host = new RadiantAnimationHost(viewport);
const play = document.getElementById('play');
let playing = true;
function playback(enabled) {
    host.operate(12, enabled ? 2 : 0, 'orbits');
    playing = enabled;
    play.textContent = playing ? 'Pause' : 'Play';
    play.setAttribute('aria-pressed', String(playing));
}
function rewind() {
    host.operate(12, 1, 0);
    if (playing) host.operate(12, 2, 'orbits');
}
const actions = {
    play: () => playback(!playing), rewind,
    'orbit-mode': () => mode(false), 'pan-mode': () => mode(true),
    'orbit-left': () => orbit(-0.16, 0), 'orbit-right': () => orbit(0.16, 0),
    'tilt-up': () => orbit(0, -0.12), 'tilt-down': () => orbit(0, 0.12),
    'zoom-in': () => zoom(0.85), 'zoom-out': () => zoom(1 / 0.85), reset,
    top: () => preset(0.16), side: () => preset(Math.PI / 2)
};
for (const id of Object.keys(actions)) document.getElementById(id).addEventListener('click', actions[id]);
// OrbitControls prevents the pointer default, so claim keyboard focus explicitly.
viewport.addEventListener('pointerdown', () => viewport.focus());
viewport.addEventListener('keydown', event => {
    const key = event.key.toLowerCase();
    if (key === '+' || key === '=') zoom(0.85);
    else if (key === '-' || key === '_') zoom(1 / 0.85);
    else if (key === 'r') reset();
    else if (key === ' ') playback(!playing);
    else return;
    event.preventDefault();
});
window.addEventListener('resize', () => {
    const bounds = viewport.getBoundingClientRect();
    if (bounds.height > 0) camera.aspect = bounds.width / bounds.height;
    camera.updateProjectionMatrix();
});
window.addEventListener('pagehide', () => { controls.dispose(); host.operate(11); });

globalThis.ringworld = { camera, controls, orbit, zoom, reset, playback, rewind };
viewport.setAttribute('data-ready', 'true');
