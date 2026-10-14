import { operate } from './_control.ls';
import * as THREE from '../scene3d/vendor/three/three.module.js';
import { OrbitControls } from '../scene3d/vendor/three/addons/controls/OrbitControls.js';

const byId = id => document.getElementById(id);
const viewport = byId('world');
const kind = viewport.getAttribute('data-demo');
const initialCamera = JSON.parse(viewport.getAttribute('data-camera'));
const camera = new THREE.PerspectiveCamera(initialCamera.fov, 1040 / 500, 0.1, 1000);
camera.position.fromArray(initialCamera.position);
const controls = new OrbitControls(camera, viewport);
controls.target.fromArray(initialCamera.target);
controls.minDistance = 5;
controls.maxDistance = 45;
controls.minPolarAngle = 0.08;
controls.maxPolarAngle = Math.PI / 2 + 0.1;
controls.screenSpacePanning = true;
controls.update();
controls.saveState();
controls.addEventListener('change', () => {
    // camera edits leave native body poses and the document-owned world intact.
    byId('main').setAttribute('position', camera.position.toArray().join(' '));
    byId('main').setAttribute('target', controls.target.toArray().join(' '));
});

let playing = viewport.getAttribute('data-playing') === 'true';
let contacts = 0;
let pending = 0;
let queue = Promise.resolve();
let timer = null;
let disposed = false;
const bins = [new Set(), new Set(), new Set()];
const counted = new Set();
const demo = { kind, state: null, bins, counted, camera, controls, ready: false, request, restart, action, topView };
globalThis.physicsDemo = demo;

function report(error) {
    byId('status').textContent = error.message || String(error);
    byId('status').setAttribute('class', 'error');
}

function updateMetrics() {
    demo.playing = playing;
    const state = demo.state;
    if (!state || !state.bodies) return;
    let bodies = 0, fallen = 0, airborne = 0, fastest = 0, highest = 0, spin = 0, energy = 0;
    for (let i = 0; i < state.bodies.length; i++) {
        const body = state.bodies[i];
        if (!/^(tile-|cargo-|marble-|ball-)/.test(body.id)) continue;
        const speed = Math.hypot(body.velocity[0], body.velocity[1], body.velocity[2]);
        bodies++;
        if (body.position[1] < 0.5) fallen++;
        if (body.position[1] > 0.44) airborne++;
        fastest = Math.max(fastest, speed);
        highest = Math.max(highest, body.position[1]);
        spin += Math.hypot(body.angular_velocity[0], body.angular_velocity[1], body.angular_velocity[2]);
        energy += 0.5 * body.mass * speed * speed;
    }
    let values;
    if (kind === 'domino') values = [fallen + ' / ' + bodies, String(contacts), fastest.toFixed(2) + ' m/s'];
    else if (kind === 'salvage') values = [String(bodies), (spin / Math.max(bodies, 1)).toFixed(2) + ' rad/s', energy.toFixed(2) + ' J'];
    else if (kind === 'sorter') values = bins.map(bin => String(bin.size));
    else values = [airborne + ' / ' + bodies, highest.toFixed(2) + ' m', energy.toFixed(2) + ' J'];
    for (let i = 0; i < 3; i++) byId(['metric-primary', 'metric-secondary', 'metric-tertiary'][i]).textContent = values[i];
    byId('clock').textContent = state.time.toFixed(2) + ' s';
    byId('play-toggle').textContent = playing ? 'Pause' : 'Play';
    byId('play-toggle').setAttribute('aria-pressed', String(playing));
    byId('status').textContent = kind === 'sorter'
        ? (playing ? 'Running' : 'Paused') + ' · ' + counted.size + ' of 18 marbles counted'
        : (playing ? 'Running' : 'Paused') + ' · tick ' + state.tick;
    byId('status').setAttribute('class', '');
}

function clearStatistics() {
    contacts = 0;
    counted.clear();
    for (const bin of bins) bin.clear();
}

function request(message) {
    pending++;
    // native procedures return Promises; serialize controls and telemetry so reset never races a read.
    const result = queue.then(() => {
        if (message.operation === 'reset') clearStatistics();
        return operate(viewport, message);
    }).then(value => {
        if (message.operation === 'play') playing = true;
        if (['pause', 'reset', 'step'].includes(message.operation)) playing = false;
        if (value && value.bodies) demo.state = value;
        pending--;
        updateMetrics();
        return value;
    }, error => {
        pending--;
        report(error);
        throw error;
    });
    queue = result.catch(() => {});
    return result;
}

function restart(resume = false) {
    return request({ operation: 'reset' }).then(value => resume ? request({ operation: 'play' }) : value);
}

function action(id, resume = true) {
    const button = byId(id);
    if (!button || !button.hasAttribute('data-request')) return Promise.reject(new Error('Unknown experiment action'));
    const before = button.getAttribute('data-restart') === 'true' ? restart() : Promise.resolve();
    return before.then(() => request(JSON.parse(button.getAttribute('data-request')))).then(value =>
        resume && button.getAttribute('data-play-after') === 'true' ? request({ operation: 'play' }) : value);
}

function topView() {
    const offset = camera.position.clone().sub(controls.target);
    camera.position.copy(controls.target).add(new THREE.Vector3(0.01, offset.length(), 0.01));
    controls.update();
}

viewport.addEventListener('physicscontactstart', () => { contacts++; updateMetrics(); });
viewport.addEventListener('physicssensorenter', event => {
    if (kind !== 'sorter') return;
    const e = event.detail;
    const gate = e.body_a.startsWith('bin-') ? e.body_a : e.body_b.startsWith('bin-') ? e.body_b : null;
    const marble = e.body_a.startsWith('marble-') ? e.body_a : e.body_b;
    if (!gate || !marble.startsWith('marble-') || counted.has(marble)) return;
    const index = Number(gate.slice(4));
    if (index >= 0 && index < bins.length) { counted.add(marble); bins[index].add(marble); updateMetrics(); }
});
viewport.addEventListener('pointerdown', () => viewport.focus());
viewport.addEventListener('keydown', event => {
    if (event.key === ' ') { event.preventDefault(); request({ operation: playing ? 'pause' : 'play' }).catch(report); }
});

const buttons = document.querySelectorAll('[data-request]');
for (let i = 0; i < buttons.length; i++) {
    const button = buttons[i];
    button.addEventListener('click', () => action(button.id).catch(report));
}
byId('play-toggle').addEventListener('click', () => request({ operation: playing ? 'pause' : 'play' }).catch(report));
byId('step').addEventListener('click', () => request({ operation: 'step', ticks: 2 }).catch(report));
byId('reset').addEventListener('click', () => restart().catch(report));
byId('camera-home').addEventListener('click', () => controls.reset());
byId('camera-top').addEventListener('click', topView);

// telemetry reads completed poses; the native animation clock owns all simulation pacing.
function poll() {
    if (disposed) return;
    if (playing && pending === 0) request({ operation: 'read' }).catch(report);
    timer = setTimeout(poll, 150);
}
window.addEventListener('resize', () => {
    const bounds = viewport.getBoundingClientRect();
    if (bounds.height > 0) camera.aspect = bounds.width / bounds.height;
    camera.updateProjectionMatrix();
});
window.addEventListener('pagehide', () => {
    disposed = true;
    clearTimeout(timer);
    controls.dispose();
});
request({ operation: 'read' }).then(() => {
    demo.ready = true;
    viewport.setAttribute('data-ready', 'true');
    poll();
}).catch(report);
