import {createRequire} from 'node:module';
import {readFile, readdir, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, '../../../..');
const source = path.dirname(require.resolve('@ant-design/icons-svg/package.json'));
const metadata = JSON.parse(await readFile(path.join(source, 'package.json')));
const records = {};
const inputs = {};
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
function normalize(node) {
    // two upstream definitions contain empty defs/style wrappers with no paint effect.
    const children = (node.children || []).map(normalize).filter(Boolean);
    if ((node.tag === 'defs' || node.tag === 'style') && !children.length && !Object.keys(node.attrs).length)
        return null;
    if (!['svg', 'path', 'g'].includes(node.tag)) throw new Error(`unsupported icon node ${node.tag}`);
    return {tag: node.tag, attrs: node.attrs, ...(children.length ? {children} : {})};
}
for (const filename of (await readdir(path.join(source, 'lib/asn'))).filter(name => name.endsWith('.js')).sort()) {
    const filepath = path.join(source, 'lib/asn', filename);
    inputs[filename] = sha256(await readFile(filepath));
    const definition = require(filepath).default;
    const icon = typeof definition.icon === 'function'
        ? definition.icon('__dtna_primary__', '__dtna_secondary__') : definition.icon;
    (records[definition.name] ||= {})[definition.theme === 'twotone' ? 'two-tone' : definition.theme] = normalize(icon);
}
const destination = path.join(root, 'lmd/package/ui/dtna/icon_assets.json');
const data = JSON.stringify(records) + '\n';
await writeFile(destination, data);
await writeFile(path.join(root, 'lmd/package/ui/dtna/icon_assets.LICENSE'),
    await readFile(path.join(path.dirname(require.resolve('@ant-design/icons/package.json')), 'LICENSE')));
await writeFile(path.join(directory, '../icons.manifest'), JSON.stringify({
    schema: 1, package: metadata.name, version: metadata.version,
    source: 'https://github.com/ant-design/ant-design-icons',
    lockfile: 'test/ui/dtna_reference/app/package-lock.json',
    generated: 'lmd/package/ui/dtna/icon_assets.json', sha256: sha256(data),
    names: Object.keys(records).length, variants: Object.keys(inputs).length, inputs,
}, null, 2) + '\n');
console.log(`dtna icons: froze ${Object.keys(inputs).length} variants of ${Object.keys(records).length} names`);
