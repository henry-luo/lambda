import * as THREE from 'three';
const result = document.getElementById('result');
let checks = 0;
function check(value, message) { if (!value) throw new Error(message); checks++; }
function noError(renderer, stage) { const code=renderer.getContext().getError();check(code === 0, stage + ': GL error ' + code); }
try {
    const first = document.getElementById('first');
    const second = document.getElementById('second');
    const a = new THREE.WebGLRenderer({ canvas: first, antialias: true });
    const b = new THREE.WebGLRenderer({ canvas: second, antialias: false });
    noError(a, 'first renderer startup');noError(b, 'second renderer startup');
    const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
    camera.position.z = 3;
    const scene = new THREE.Scene();
    const geometry = new THREE.BoxGeometry(1, 1, 1);
    const material = new THREE.MeshBasicMaterial({ color: 0xff0000 });
    scene.add(new THREE.Mesh(geometry, material));
    a.setSize(128, 128, false);
    a.setPixelRatio(2);
    a.render(scene, camera);noError(a, 'render');
    check(first.width === 256 && first.height === 256, 'pixel ratio changes bitmap dimensions');
    check(a.getContext().drawingBufferWidth === 256, 'native drawing buffer follows density');
    check(a.info.render.triangles === 12, 'indexed box submitted');
    a.setSize(64, 64, false);
    check(first.width === 128 && first.height === 128, 'resize retains pixel ratio');
    a.render(scene, camera);noError(a, 'render');
    check(a.info.memory.geometries === 1, 'geometry allocation');
    geometry.dispose(); material.dispose();noError(a, 'dispose geometry/material');
    check(a.info.memory.geometries === 0, 'geometry disposal');
    check(a.info.programs.length === 0, 'program disposal');
    a.setClearColor(0xff0000, 1);a.clear();noError(a, 'clear first');
    b.setSize(128, 128, false);b.setClearColor(0x00ff00, 1);b.clear();noError(b, 'clear second');
    check(a.getContext() !== b.getContext(), 'independent renderers');
    check(a.getContext().getError() === 0 && b.getContext().getError() === 0, 'no native errors');
    a.dispose();b.dispose();
    result.setAttribute('data-result', 'passed');result.setAttribute('data-checks', String(checks));
} catch (e) { result.setAttribute('data-result', String(e.stack || e));console.error(e.stack || e); }
