import {createRequire} from 'node:module';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import React from 'react';
import {renderToStaticMarkup} from 'react-dom/server';

const require = createRequire(import.meta.url);
const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, '../../../..');
const source = path.dirname(require.resolve('antd/package.json'));
const {ConfigProvider} = require('antd');
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const destination = path.join(root, 'lmd/package/ui/dtna/illustrations');
await mkdir(destination, {recursive: true});
const records = {};
for (const [name, module] of Object.entries({
    'empty-default':'empty/empty', 'empty-simple':'empty/simple',
    'result-403':'result/unauthorized', 'result-404':'result/noFound', 'result-500':'result/serverError',
})) {
    const filename = path.join(source, `lib/${module}.js`);
    const Component = require(filename).default;
    const markup = renderToStaticMarkup(React.createElement(ConfigProvider,
        {theme:{token:{motion:false}}}, React.createElement(Component)));
    if (!/^<svg\b/.test(markup) || !markup.endsWith('</svg>')) throw new Error(`non-SVG illustration ${name}`);
    await writeFile(path.join(destination, `${name}.svg`), markup + '\n');
    records[name] = {source:`antd/lib/${module}.js`, source_sha256:sha256(await readFile(filename)), sha256:sha256(markup + '\n')};
}
await writeFile(path.join(destination, 'LICENSE'), await readFile(path.join(source, 'LICENSE')));
await writeFile(path.join(directory, '../illustrations.manifest'), JSON.stringify({
    schema:1, package:'antd', version:JSON.parse(await readFile(path.join(source, 'package.json'))).version,
    scope:'default-light illustrations; originating semantic labels are supplied by the component',
    generated:'lmd/package/ui/dtna/illustrations', records,
}, null, 2) + '\n');
console.log(`dtna illustrations: froze ${Object.keys(records).length} licensed SVG assets`);
