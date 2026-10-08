import * as THREE from './vendor/three/three.module.js';
import { RoomEnvironment } from './vendor/three/addons/environments/RoomEnvironment.js';
import { OrbitControls } from './vendor/three/addons/controls/OrbitControls.js';
import { EffectComposer } from './vendor/three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from './vendor/three/addons/postprocessing/RenderPass.js';
import { ShaderPass } from './vendor/three/addons/postprocessing/ShaderPass.js';
import { OutputPass } from './vendor/three/addons/postprocessing/OutputPass.js';
import { FXAAShader } from './vendor/three/addons/shaders/FXAAShader.js';
import { NativeAnimationMixer } from './native-animation.js';

export function createObservatory(canvas, status, play) {
try {
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, preserveDrawingBuffer: true });
    renderer.setSize(800, 500, false);
    renderer.setPixelRatio(1);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.1;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x07121c);
    const camera = new THREE.PerspectiveCamera(40, 800 / 500, 0.1, 100);
    camera.position.set(8, 6.5, 10);
    const controls = new OrbitControls(camera, canvas);
    controls.target.set(0, 1.25, 0);
    controls.minDistance = 6; controls.maxDistance = 24; controls.maxPolarAngle = Math.PI * 0.47;
    controls.update();
    let environmentTarget;
    function rebuildEnvironment() {
    if (environmentTarget) environmentTarget.dispose();
    const environment = new RoomEnvironment();
    const generator = new THREE.PMREMGenerator(renderer);
    environmentTarget = generator.fromScene(environment, 0.04, 0.1, 100, { size: 64 });
    scene.environment = environmentTarget.texture;
    scene.environmentIntensity = 0.7;
    environment.dispose(); generator.dispose();
    }
    rebuildEnvironment();
    scene.add(new THREE.HemisphereLight(0x89b9db, 0x312817, 0.8));
    const sun = new THREE.DirectionalLight(0xffdda1, 4); sun.name = 'sun';
    sun.position.set(4, 7, 3); sun.castShadow = true;
    sun.shadow.mapSize.set(512, 512);
    Object.assign(sun.shadow.camera, { left: -5, right: 5, top: 5, bottom: -5, near: 0.5, far: 20 });
    sun.shadow.normalBias = 0.04;
    scene.add(sun);
    const root = new THREE.Group(); root.name = 'mechanism'; root.position.y = 1.7; scene.add(root);
    const metal = new THREE.MeshStandardMaterial({ color: 0x67c9ba, metalness: 0.9, roughness: 0.23 });
    const gold = new THREE.MeshStandardMaterial({ color: 0xe1a65c, metalness: 0.82, roughness: 0.22 });
    const ringGeometry = new THREE.TorusGeometry(1.65, 0.085, 12, 80);
    const outer = new THREE.Mesh(ringGeometry, metal); outer.name = 'outer'; outer.castShadow = true;
    outer.rotation.set(0.45, 0.25, 0.45); root.add(outer);
    const inner = new THREE.Mesh(ringGeometry, gold); inner.name = 'inner'; inner.scale.setScalar(0.76);
    inner.rotation.set(-0.8, 0.2, -0.4); inner.castShadow = true; root.add(inner);
    const core = new THREE.Mesh(new THREE.IcosahedronGeometry(0.62, 2), metal);
    core.castShadow = true; core.receiveShadow = true; root.add(core);
    const satellites = [];
    const satelliteGeometry = new THREE.SphereGeometry(0.23, 24, 16);
    for (let i = 0; i < 5; i++) {
        const object = new THREE.Mesh(satelliteGeometry, i % 2 ? metal : gold);
        object.name = 'Satellite ' + (i + 1); object.castShadow = true; object.receiveShadow = true;
        const angle = i * Math.PI * 0.4;
        object.position.set(Math.cos(angle) * 2.7, 0.2 + Math.sin(angle) * 0.55, Math.sin(angle) * 2.7);
        root.add(object); satellites.push(object);
    }
    const stone = new THREE.MeshStandardMaterial({ color: 0xe5d7c0, roughness: 0.86, metalness: 0.05 });
    const platform = new THREE.Mesh(new THREE.CylinderGeometry(3.6, 3.8, 0.28, 80), stone);
    platform.receiveShadow = true; platform.castShadow = true; scene.add(platform);
    const floor = new THREE.Mesh(new THREE.PlaneGeometry(200, 200), new THREE.MeshStandardMaterial({ color: 0x132b32, roughness: 0.8 }));
    floor.rotation.x = -Math.PI / 2; floor.position.y = -0.15; floor.receiveShadow = true; scene.add(floor);
    const rim = new THREE.Mesh(new THREE.TorusGeometry(3.55, 0.028, 8, 96), gold);
    rim.rotation.x = Math.PI / 2; rim.position.y = 0.16; scene.add(rim);
    let composer, fxaa;
    function rebuildComposer() {
        if (composer) { for (const pass of composer.passes) pass.dispose(); composer.dispose(); }
        composer = new EffectComposer(renderer);
        fxaa = new ShaderPass(FXAAShader);
        const size = renderer.getSize(new THREE.Vector2()), density = renderer.getPixelRatio();
        fxaa.uniforms.resolution.value.set(1 / (size.x * density), 1 / (size.y * density));
        composer.addPass(new RenderPass(scene, camera)); composer.addPass(fxaa); composer.addPass(new OutputPass());
    }
    rebuildComposer();
    const nativePlayback = typeof RadiantAnimationHost === 'function';
    const mixer = nativePlayback ? new NativeAnimationMixer(scene, canvas) : new THREE.AnimationMixer(scene);
    const times = [0, 2, 4, 6, 8];
    function rotations(path, eulerAt) {
        const values = [];
        for (const time of times) {
            const quaternion = new THREE.Quaternion().setFromEuler(eulerAt(time));
            values.push(...quaternion.toArray());
        }
        return new THREE.QuaternionKeyframeTrack(path, times, values);
    }
    const lightPositions = times.flatMap(time => [4 * Math.cos(time * Math.PI / 4), 7,
        3 + Math.sin(time * Math.PI / 4) * 3]);
    const clip = new THREE.AnimationClip('orbits', 8, [
        rotations('outer.quaternion', t => new THREE.Euler(0.45, 0.25 + t * Math.PI / 4, 0.45)),
        rotations('inner.quaternion', t => new THREE.Euler(-0.8 + t * Math.PI / 4, 0.2, -0.4)),
        rotations('mechanism.quaternion', t => new THREE.Euler(0, t * Math.PI / 4, 0)),
        new THREE.VectorKeyframeTrack('sun.position', times, lightPositions)
    ]);
    mixer.clipAction(clip).play();
    const raycaster = new THREE.Raycaster();
    let lost = false, disposed = false;
    function render() { if (!lost && !disposed) composer.render(0); }
    function onLost(event) { event.preventDefault(); lost = true; }
    function onRestored() {
        if (disposed) return;
        lost = false;
        if (sun.shadow.map) { sun.shadow.map.dispose(); sun.shadow.map = null; }
        rebuildEnvironment(); rebuildComposer(); render();
        status.setAttribute('data-restored', String(Number(status.getAttribute('data-restored') || 0) + 1));
    }
    canvas.addEventListener('webglcontextlost', onLost);
    canvas.addEventListener('webglcontextrestored', onRestored);
    function sample(seconds) {
        mixer.setTime(seconds); render();
    }
    controls.addEventListener('change', () => {
        status.setAttribute('data-camera', camera.position.toArray().map(n => n.toFixed(5)).join(',')); render();
    });
    for (const name of ['gotpointercapture', 'lostpointercapture', 'pointercancel'])
        canvas.addEventListener(name, () => status.setAttribute('data-' + name, 'true'));
    canvas.addEventListener('click', event => {
        const rect = canvas.getBoundingClientRect();
        raycaster.setFromCamera(new THREE.Vector2((event.clientX - rect.left) / rect.width * 2 - 1,
            1 - (event.clientY - rect.top) / rect.height * 2), camera);
        const hits = raycaster.intersectObjects(satellites);
        status.textContent = hits.length ? hits[0].object.name + ' selected' : 'Drag to orbit · scroll to zoom';
        status.setAttribute('data-selection', hits.length ? hits[0].object.name : 'none');
    });
    let playing = false, previous = 0;
    function frame(now) {
        if (!playing || disposed) return;
        if (!nativePlayback && previous) mixer.update(Math.min((now - previous) / 1000, 0.1));
        previous = now; render(); requestAnimationFrame(frame);
    }
    play.addEventListener('click', () => {
        playing = !playing; previous = 0;
        if (nativePlayback) mixer.setAutomatic(playing);
        if (playing) requestAnimationFrame(frame);
    });
    const loader = new THREE.TextureLoader();
    let loaded = 0;
    function ready() {
        if (++loaded !== 2) return;
        sample(0); status.textContent = 'PBR environment · PNG normal map · JPEG stone · moving shadows · FXAA';
        status.setAttribute('data-ready', 'true');
    }
    loader.load('./assets/brushed-normal.png', texture => {
        metal.normalMap = texture; metal.normalScale.set(0.4, 0.4); metal.needsUpdate = true; ready();
    }, undefined, error => { status.textContent = String(error); });
    loader.load('./assets/stone.jpg', texture => {
        texture.colorSpace = THREE.SRGBColorSpace; texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
        texture.repeat.set(4, 4); stone.map = texture; stone.needsUpdate = true; ready();
    }, undefined, error => { status.textContent = String(error); });
    const api = { renderer, scene, camera, controls, get composer() { return composer; }, mixer, clip, sample, render, satellites, sun,
        setShadows(enabled) {
            renderer.shadowMap.enabled = enabled;
            scene.traverse(object => { if(object.material) object.material.needsUpdate = true; });render();
        },
        dispose() {
            if (disposed) return;
            disposed = true; playing = false;
            if (nativePlayback) mixer.dispose(); else mixer.stopAllAction();
            controls.dispose();
            canvas.removeEventListener('webglcontextlost', onLost);canvas.removeEventListener('webglcontextrestored', onRestored);
            for (const pass of composer.passes) pass.dispose(); composer.dispose(); environmentTarget.dispose();
            const geometries = new Set(), materials = new Set(), textures = new Set();
            scene.traverse(object => { if(object.geometry) geometries.add(object.geometry); if(object.material) materials.add(object.material); });
            for (const material of materials) { if(material.map) textures.add(material.map); if(material.normalMap) textures.add(material.normalMap); material.dispose(); }
            for (const texture of textures) texture.dispose(); for (const geometry of geometries) geometry.dispose();
            if(sun.shadow.map) sun.shadow.map.dispose(); renderer.dispose();
            // this factory owns its canvas context, including renderer-internal fallback textures.
            renderer.forceContextLoss();
        },
        resize(width, height, density = 1) {
            renderer.setPixelRatio(density); renderer.setSize(width, height, false);
            composer.setPixelRatio(density); composer.setSize(width, height);
            camera.aspect = width / height; camera.updateProjectionMatrix();
            fxaa.uniforms.resolution.value.set(1 / (width * density), 1 / (height * density)); render();
        } };
    return api;
} catch (error) {
    status.textContent = String(error); status.setAttribute('data-error', String(error)); throw error;
}

}
const primaryStatus = document.getElementById('status');
globalThis.createObservatory = createObservatory;
globalThis.observatory = createObservatory(document.getElementById('observatory'), primaryStatus, document.getElementById('play'));
Object.defineProperty(globalThis, 'observatoryReady', { get() { return primaryStatus.getAttribute('data-ready') === 'true'; } });
