#!/usr/bin/env node

/*
 * Vendor the load-bearing network dependencies of the recorded web-tmpl suite.
 *
 * The templates are file:// fixtures, so an external stylesheet or script makes
 * browser references depend on whatever the host happens to serve today. This
 * tool stores each fetched response under data/web-tmpl/_vendor/ and rewrites
 * only resource-bearing HTML attributes and CSS URL values. Navigation links
 * deliberately remain untouched.
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { fetchResource, getExtension, toSafeFilename } = require('./download-page.js');

const fixtureRoot = path.resolve(__dirname, '../../test/layout/data/web-tmpl');
const vendorRoot = path.join(fixtureRoot, '_vendor');
const manifestPath = path.join(vendorRoot, 'manifest.json');
const writeChanges = process.argv.includes('--write');
const browserUserAgent = 'Mozilla/5.0 (Macintosh; ARM Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36';

const resources = new Map();
const sourceFiles = [];
const failures = [];
const destinationOwners = new Map();

function isFetchableUrl(value) {
    return value && !/^(?:data:|blob:|about:|#)/i.test(value);
}

function normalizeRemoteUrl(value, baseUrl = null) {
    const decoded = value.replace(/&amp;/gi, '&');
    let parsed;
    try {
        // A protocol-relative link in a file fixture otherwise resolves to a
        // file-host URL, even though its author intended a network dependency.
        parsed = decoded.startsWith('//')
            ? new URL(`https:${decoded}`)
            : new URL(decoded, baseUrl || undefined);
    } catch {
        return null;
    }
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return null;
    const fragment = parsed.hash;
    parsed.hash = '';
    return { key: parsed.href, fragment };
}

function readBaselineNames() {
    return fs.readFileSync(path.join(fixtureRoot, 'baseline.txt'), 'utf8')
        .trim().split(/\r?\n/)
        .map(line => line.split(' Elements ')[0]);
}

function walkCssFiles(directory) {
    const entries = fs.readdirSync(directory, { withFileTypes: true });
    for (const entry of entries) {
        const filePath = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            walkCssFiles(filePath);
        } else if (entry.isFile() && entry.name.endsWith('.css')) {
            sourceFiles.push({ type: 'css', path: filePath });
        }
    }
}

function collectSourceFiles() {
    for (const testName of readBaselineNames()) {
        const directory = path.join(fixtureRoot, testName);
        const indexPath = path.join(directory, 'index.html');
        if (!fs.existsSync(indexPath)) {
            throw new Error(`missing recorded fixture: ${indexPath}`);
        }
        sourceFiles.push({ type: 'html', path: indexPath });
        walkCssFiles(directory);
    }
}

function attributeValue(match) {
    return match[2] ?? match[3] ?? match[4] ?? '';
}

function htmlResourceUrls(source) {
    const urls = [];
    const tagPattern = /<(link|script|img|iframe|source|video|audio|embed|object)\b[^>]*>/gi;
    for (const tagMatch of source.matchAll(tagPattern)) {
        const tagName = tagMatch[1].toLowerCase();
        const tag = tagMatch[0];
        const relMatch = /\brel\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i.exec(tag);
        const rel = (relMatch?.[1] ?? relMatch?.[2] ?? relMatch?.[3] ?? '').toLowerCase();
        const linkLoadsResource = tagName !== 'link' ||
            /(?:^|\s)(?:stylesheet|icon|preload|modulepreload|prefetch|manifest)(?:\s|$)/.test(rel);
        if (!linkLoadsResource) continue;
        for (const attribute of tag.matchAll(/\b(href|src)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/gi)) {
            urls.push(attributeValue(attribute));
        }
    }
    return urls;
}

function cssUrls(source) {
    const urls = [];
    const urlPattern = /url\(\s*(?:"([^"]*)"|'([^']*)'|([^\s"')]+))\s*\)/gi;
    const importPattern = /@import\s+(?:"([^"]*)"|'([^']*)')/gi;
    for (const match of source.matchAll(urlPattern)) urls.push(match[1] ?? match[2] ?? match[3] ?? '');
    for (const match of source.matchAll(importPattern)) urls.push(match[1] ?? match[2] ?? '');
    return urls;
}

function destinationFor(url, extension) {
    const parsed = new URL(url);
    const scheme = parsed.protocol.slice(0, -1);
    const host = parsed.host.replace(/[^a-z0-9._-]/gi, '_');
    const filename = toSafeFilename(url, extension || '');
    const relative = path.posix.join(scheme, host, filename);
    const owner = destinationOwners.get(relative);
    if (owner && owner !== url) {
        // A query is part of the response identity, so avoid silently sharing
        // two resources that happen to sanitize to the same filename.
        const suffix = crypto.createHash('sha256').update(url).digest('hex').slice(0, 12);
        return path.posix.join(scheme, host, `${filename}.${suffix}`);
    }
    destinationOwners.set(relative, url);
    return relative;
}

async function ensureResource(url) {
    const normalized = normalizeRemoteUrl(url);
    if (!normalized) return null;
    const existing = resources.get(normalized.key);
    if (existing) return existing.promise;

    const resource = { sourceUrl: normalized.key, promise: null };
    resource.promise = (async () => {
        try {
            const response = await fetchResource(normalized.key, {
                headers: { 'User-Agent': browserUserAgent }
            });
            const extension = getExtension(response.url, response.contentType);
            resource.content = response.buffer;
            resource.contentType = response.contentType;
            resource.responseUrl = response.url;
            resource.relativePath = destinationFor(normalized.key, extension);
            resource.isCss = extension === '.css' || /^text\/css(?:;|$)/i.test(response.contentType);
            if (resource.isCss) {
                const css = resource.content.toString('utf8');
                for (const reference of cssUrls(css)) {
                    if (!isFetchableUrl(reference)) continue;
                    const child = normalizeRemoteUrl(reference, resource.responseUrl);
                    if (child) await ensureResource(child.key);
                }
            }
            return resource;
        } catch (error) {
            resource.error = error.message;
            failures.push({ url: normalized.key, error: error.message });
            return resource;
        }
    })();
    resources.set(normalized.key, resource);
    return resource.promise;
}

function localReference(sourcePath, resource, fragment = '') {
    const target = path.join(vendorRoot, resource.relativePath);
    const relative = path.relative(path.dirname(sourcePath), target).split(path.sep).join('/');
    return `${relative.startsWith('.') ? relative : `./${relative}`}${fragment}`;
}

function replacementFor(value, sourcePath, baseUrl = null) {
    const normalized = normalizeRemoteUrl(value, baseUrl);
    if (!normalized) return value;
    const resource = resources.get(normalized.key);
    if (!resource || resource.error || !resource.relativePath) return value;
    return localReference(sourcePath, resource, normalized.fragment);
}

function rewriteCss(source, sourcePath, baseUrl = null) {
    const urlPattern = /url\(\s*("([^"]*)"|'([^']*)'|([^\s"')]+))\s*\)/gi;
    const importPattern = /@import\s+("([^"]*)"|'([^']*)')/gi;
    let rewritten = source.replace(urlPattern, (match, quoted, doubleQuoted, singleQuoted, bare) => {
        const value = doubleQuoted ?? singleQuoted ?? bare ?? '';
        const replacement = replacementFor(value, sourcePath, baseUrl);
        return replacement === value ? match : `url("${replacement}")`;
    });
    rewritten = rewritten.replace(importPattern, (match, quoted, doubleQuoted, singleQuoted) => {
        const value = doubleQuoted ?? singleQuoted ?? '';
        const replacement = replacementFor(value, sourcePath, baseUrl);
        return replacement === value ? match : `@import "${replacement}"`;
    });
    return rewritten;
}

function rewriteHtml(source, sourcePath) {
    const tagPattern = /<(link|script|img|iframe|source|video|audio|embed|object)\b[^>]*>/gi;
    return source.replace(tagPattern, (tag, tagName) => {
        const lowerTag = tagName.toLowerCase();
        const relMatch = /\brel\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i.exec(tag);
        const rel = (relMatch?.[1] ?? relMatch?.[2] ?? relMatch?.[3] ?? '').toLowerCase();
        const linkLoadsResource = lowerTag !== 'link' ||
            /(?:^|\s)(?:stylesheet|icon|preload|modulepreload|prefetch|manifest)(?:\s|$)/.test(rel);
        if (!linkLoadsResource) return tag;
        return tag.replace(/\b(href|src)\s*=\s*("([^"]*)"|'([^']*)'|([^\s>]+))/gi,
            (attribute, name, quoted, doubleQuoted, singleQuoted, bare) => {
                const value = doubleQuoted ?? singleQuoted ?? bare ?? '';
                const replacement = replacementFor(value, sourcePath);
                return replacement === value ? attribute : `${name}="${replacement}"`;
            });
    });
}

function writeResource(resource) {
    const destination = path.join(vendorRoot, resource.relativePath);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    let content = resource.content;
    if (resource.isCss) {
        // CSS has a remote base URL; rewrite after its complete dependency graph
        // has been fetched so every @font-face and background URL is local.
        content = Buffer.from(rewriteCss(content.toString('utf8'), destination, resource.responseUrl));
    }
    fs.writeFileSync(destination, content);
}

function writeManifest() {
    const entries = [...resources.values()]
        .filter(resource => !resource.error && resource.relativePath)
        .map(resource => ({
            source_url: resource.sourceUrl,
            response_url: resource.responseUrl,
            path: resource.relativePath,
            content_type: resource.contentType
        }))
        .sort((left, right) => left.path.localeCompare(right.path));
    fs.mkdirSync(vendorRoot, { recursive: true });
    fs.writeFileSync(manifestPath, `${JSON.stringify({ resources: entries }, null, 2)}\n`);
}

async function main() {
    collectSourceFiles();
    const sourceContents = new Map(sourceFiles.map(file => [file.path, fs.readFileSync(file.path, 'utf8')]));
    for (const file of sourceFiles) {
        const contents = sourceContents.get(file.path);
        const urls = file.type === 'html' ? htmlResourceUrls(contents) : cssUrls(contents);
        for (const url of urls) {
            if (!isFetchableUrl(url)) continue;
            const normalized = normalizeRemoteUrl(url);
            if (normalized) await ensureResource(normalized.key);
        }
    }

    const successful = [...resources.values()].filter(resource => !resource.error && resource.relativePath);
    const rewrittenFiles = sourceFiles.filter(file => {
        const source = sourceContents.get(file.path);
        const rewritten = file.type === 'html' ? rewriteHtml(source, file.path) : rewriteCss(source, file.path);
        file.rewritten = rewritten;
        return rewritten !== source;
    });

    console.log(`web-tmpl remote resources: ${resources.size} discovered, ${successful.length} fetched, ${failures.length} unavailable`);
    console.log(`web-tmpl source files to rewrite: ${rewrittenFiles.length}`);
    for (const failure of failures) console.log(`unavailable: ${failure.url} (${failure.error})`);
    if (!writeChanges) {
        console.log('dry run only; re-run with --write to vendor successful resources and rewrite their callers.');
        return;
    }

    for (const resource of successful) writeResource(resource);
    writeManifest();
    for (const file of rewrittenFiles) fs.writeFileSync(file.path, file.rewritten);
}

main().catch(error => {
    console.error(error.stack || error.message);
    process.exitCode = 1;
});
