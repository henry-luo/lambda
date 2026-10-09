// Rendered acceptance: independent SVG/PDF rasterizers must preserve the native map's geometry.
const fs = require('fs');
const path = require('path');
const {spawnSync} = require('child_process');
const {PNG} = require('../render/node_modules/pngjs');

const binary = path.resolve(process.env.LAMBDA_BIN || './lambda.exe');
const output = path.resolve('temp/map-export-check');
fs.mkdirSync(output, {recursive: true});
function run(command, args) {
    const result = spawnSync(command, args, {encoding: 'utf8', maxBuffer: 4 * 1024 * 1024});
    if (result.error || result.status !== 0) {
        throw new Error(`${command} ${args.join(' ')}\n${result.error || result.stderr || result.stdout}`);
    }
}
const cases = {
    offline: [[156,124,255,0,0], [176,124,238,243,246], [206,124,0,170,0],
        [156,167,0,0,255], [24,167,255,255,255], [310,10,0,255,0]],
    dateline: [[139,96,255,0,0], [118,96,238,243,246], [88,96,0,170,0]],
    opacity: [[128,96,128,85,0], [185,96,0,0,255]],
    stroke: [[132,100,0,0,255], [135,103,255,255,255], [39,96,255,255,255], [128,35,255,255,255], [128,154,255,255,255]],
    expressions: [[64,96,255,0,0], [70,96,255,0,0], [75,96,238,243,246],
        [192,96,119,121,251], [128,96,238,243,246]],
};
for (const [name, samples] of Object.entries(cases)) {
    const base = path.join(output, name);
    for (const format of ['png', 'svg', 'pdf']) {
        run(binary, ['render', `test/map/${name}.ls`, '-vw', '360', '-vh', '260', '-o', `${base}.${format}`]);
    }
    if (/<(?:image)\b/.test(fs.readFileSync(`${base}.svg`, 'utf8')) ||
        /\/Subtype\s*\/Image\b/.test(fs.readFileSync(`${base}.pdf`, 'latin1'))) {
        throw new Error(`${name}: vector map export unexpectedly contains a raster image`);
    }
    run('rsvg-convert', [`${base}.svg`, '-o', `${base}-svg.png`]);
    run('pdftocairo', ['-png', '-singlefile', '-r', '72', `${base}.pdf`, `${base}-pdf`]);
    for (const suffix of ['', '-svg', '-pdf']) {
        const image = PNG.sync.read(fs.readFileSync(`${base}${suffix}.png`));
        // document vector export can add page margins; authored map coordinates stay fixed.
        if (!suffix && (image.width !== 360 || image.height !== 260)) throw new Error(`${name}: wrong raster viewport`);
        for (const [x,y,...expected] of samples) {
            if (x >= image.width || y >= image.height) throw new Error(`${name}${suffix}: missing map extent`);
            for (let channel = 0; channel < 3; channel++) {
                const actual = image.data[(y * image.width + x) * 4 + channel];
                if (Math.abs(actual - expected[channel]) > 3) {
                    throw new Error(`${name}${suffix}: pixel ${x},${y} channel ${channel}: expected ${expected[channel]}, got ${actual}`);
                }
            }
        }
    }
    console.log(`${name}: native PNG, SVG and PDF pixels passed`);
}
