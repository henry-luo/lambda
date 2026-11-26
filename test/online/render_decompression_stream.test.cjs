const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const puppeteer = require('puppeteer');
const {renderFixtures, loggedResult, rgb} = require('./render_fixture.cjs');

const line = 'Gallery artwork data across compressed chunks.\n';
const repeat = 1700;
const text = line.repeat(repeat);
const compressed = {
    raw: Array.from(zlib.deflateRawSync(text)),
    zlib: Array.from(zlib.deflateSync(text)),
    gzip: Array.from(zlib.gzipSync(text)),
};
const source = `<!doctype html><style>body {margin:0;background:red}</style><body>
<script>
const compressed = ${JSON.stringify(compressed)};
const bytes = new Uint8Array(compressed.raw);
const expected = ${JSON.stringify(line)}.repeat(${repeat});
async function readAll(reader, first) {
    const parts = [];
    let length = 0;
    for (let record = first || await reader.read(); !record.done; record = await reader.read()) {
        if (!(record.value instanceof Uint8Array)) throw new Error('Expected Uint8Array');
        parts.push(record.value);
        length += record.value.byteLength;
    }
    const data = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) {
        data.set(part, offset);
        offset += part.byteLength;
    }
    return new TextDecoder().decode(data);
}
function errorName(promise) {
    return promise.then(() => 'fulfilled', error => error.name);
}
async function decode(format, input) {
    const stream = new DecompressionStream(format);
    const writer = stream.writable.getWriter();
    const reading = readAll(stream.readable.getReader());
    const [result] = await Promise.all([reading, writer.write(input), writer.close()]);
    return result === expected && stream instanceof DecompressionStream &&
        await errorName(writer.write(new Uint8Array(0))) === 'TypeError';
}
async function rejected(format, chunks) {
    const stream = new DecompressionStream(format);
    const writer = stream.writable.getWriter();
    const reading = errorName(readAll(stream.readable.getReader()));
    const writes = chunks.map(chunk => errorName(writer.write(chunk)));
    const [read, close, ...results] = await Promise.all([reading, errorName(writer.close()), ...writes]);
    return [read, close, results.join(',')];
}
async function inspect() {
    const source = new ReadableStream({start(controller) {controller.enqueue(bytes); controller.close();}});
    const pipe = await readAll(source.pipeThrough(new DecompressionStream('deflate-raw')).getReader());
    const frame = new Uint8Array(bytes.length + 5);
    frame.set(bytes, 3);
    const firstSize = Math.floor(bytes.length / 2);
    const stream = new DecompressionStream({toString() {return 'deflate-raw';}});
    const writer = stream.writable.getWriter();
    const reader = stream.readable.getReader();
    const firstWrite = writer.write(new DataView(frame.buffer, 3, firstSize));
    const first = await reader.read();
    const reading = readAll(reader, first);
    const [fromChunks] = await Promise.all([reading, firstWrite, writer.write(bytes.subarray(firstSize)), writer.close()]);
    let invalidFormat = 'fulfilled', requiresNew = 'fulfilled';
    try { new DecompressionStream('deflate-raw\\0'); } catch (error) { invalidFormat = error.name; }
    try { DecompressionStream('gzip'); } catch (error) { requiresNew = error.name; }
    const corrupt = new Uint8Array(compressed.zlib);
    corrupt[corrupt.length - 1] ^= 1;
    const gzip = new Uint8Array(compressed.gzip);
    const concatenated = new Uint8Array(gzip.length * 2);
    concatenated.set(gzip);
    concatenated.set(gzip, gzip.length);
    return {
        pipe: pipe === expected,
        chunks: !first.done && first.value.byteLength > 0 && fromChunks === expected,
        zlib: await decode('deflate', new Uint8Array(compressed.zlib).buffer),
        gzip: await decode('gzip', gzip),
        invalidFormat, requiresNew,
        truncated: await rejected('deflate-raw', [bytes.subarray(0, 3)]),
        trailing: await rejected('deflate-raw', [bytes, new Uint8Array([0])]),
        nonBinary: await rejected('deflate-raw', ['not a byte view']),
        checksum: await rejected('deflate', [corrupt]),
        multipleMembers: await rejected('gzip', [concatenated]),
    };
}
inspect().then(result => {
    window.decodeResult = result;
    document.body.style.background = 'lime';
    console.log('DECOMPRESSION_RESULT ' + JSON.stringify(result));
}, error => {
    window.decodeResult = {error:error.name, message:error.message};
    console.log('DECOMPRESSION_RESULT ' + JSON.stringify(window.decodeResult));
});
</script>`;

const rejectedOnWrite = ['TypeError', 'TypeError', 'TypeError'];
const expected = {
    pipe:true, chunks:true, zlib:true, gzip:true, invalidFormat:'TypeError', requiresNew:'TypeError',
    truncated:['TypeError', 'TypeError', 'fulfilled'],
    trailing:['TypeError', 'TypeError', 'fulfilled,TypeError'],
    nonBinary:rejectedOnWrite, checksum:rejectedOnWrite, multipleMembers:rejectedOnWrite,
};
const render = renderFixtures('decompression-stream');

test('DecompressionStream inflates ZIP entries incrementally and rejects malformed input on both sides', async () => {
    const {image, log} = await render('compression', source);
    assert.deepEqual(loggedResult(log, 'DECOMPRESSION_RESULT'), expected);
    assert.deepEqual(rgb(image, 20, 20), [0, 255, 0]);
});

test('Chromium agrees on separate writes, wrapped formats and errors', async () => {
    const dir = fs.mkdtempSync(path.resolve(__dirname, '../../temp/browser-decompression-'));
    let browser;
    try {
        browser = await puppeteer.launch({headless:'shell', executablePath:process.env.CHROME_HEADLESS_SHELL,
            userDataDir:path.join(dir, 'profile')});
        const page = await browser.newPage();
        await page.setContent(source);
        await page.waitForFunction(() => window.decodeResult, {timeout:5000});
        assert.deepEqual(await page.evaluate(() => window.decodeResult), expected);
    } finally {
        if (browser) await browser.close();
        fs.rmSync(dir, {recursive:true, force:true});
    }
});
