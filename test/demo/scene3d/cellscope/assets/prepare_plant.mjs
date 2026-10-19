// Rebuild the bundled glTF from the upstream plant GLB; never change the color texture.
import { createRequire } from 'node:module';
import { resolve, join } from 'node:path';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';

const [source, destination, toolsDirectory] = process.argv.slice(2);
if (!source || !destination || !toolsDirectory) {
    throw new Error('Usage: node prepare_plant.mjs source.glb output-directory tools-directory');
}
const require = createRequire(resolve(toolsDirectory, 'package.json'));
const { NodeIO } = require('@gltf-transform/core');
const { ALL_EXTENSIONS } = require('@gltf-transform/extensions');
const { weld, simplify, quantize, prune, center } = require('@gltf-transform/functions');
const { MeshoptSimplifier } = await import(require.resolve('meshoptimizer'));
await MeshoptSimplifier.ready;

const io = new NodeIO().registerExtensions(ALL_EXTENSIONS);
const document = await io.read(source);
const counts = () => document.getRoot().listMeshes().reduce((sum, mesh) => {
    for (const primitive of mesh.listPrimitives()) {
        sum.vertices += primitive.getAttribute('POSITION').getCount();
        sum.triangles += primitive.getIndices().getCount() / 3;
    }
    return sum;
}, { vertices: 0, triangles: 0 });
const before = counts();
// Quantized storage meets the budget while preserving texture seams and authored normal directions.
const ratio = 0.004, error = 0.01, positionBits = 14, normalBits = 8, uvBits = 16;
const maxBinaryBytes = 250000;
await document.transform(weld(), simplify({ simplifier: MeshoptSimplifier, ratio, error }),
    quantize({ quantizePosition: positionBits, quantizeNormal: normalBits, quantizeTexcoord: uvBits }),
    prune(), center({ pivot: 'center' }));
const after = counts();
if (after.vertices > 262144 || after.triangles > 524288) {
    throw new Error('Reduced model exceeds the native Scene3D per-mesh budget');
}
const texture = document.getRoot().listTextures()[0];
texture.setURI('plant-basecolor.jpg');
await mkdir(destination, { recursive: true });
await io.write(join(destination, 'plant.gltf'), document);
const binaryBytes = (await stat(join(destination, 'plant.bin'))).size;
if (binaryBytes >= maxBinaryBytes) {
    throw new Error(`Reduced plant buffer is ${binaryBytes} bytes; must be below ${maxBinaryBytes}`);
}
const digest = data => createHash('sha256').update(data).digest('hex');
await writeFile(join(destination, 'manifest.json'), JSON.stringify({
    source: 'https://github.com/cclank/cell-architecture-studio',
    revision: '1cab982e7a0f96af854a696430c0724707764358',
    source_file: 'public/models/plant-cell-first001.glb',
    source_sha256: digest(await readFile(source)),
    texture_sha256: digest(texture.getImage()),
    preparation: { gltf_transform: '4.5.0', meshoptimizer: '1.0.1', ratio, error,
        position_bits: positionBits, normal_bits: normalBits, uv_bits: uvBits,
        max_binary_bytes: maxBinaryBytes },
    original: before, reduced: { ...after, binary_bytes: binaryBytes }
}, null, 2) + '\n');
console.log(JSON.stringify({ original: before, reduced: after, binary_bytes: binaryBytes }));
