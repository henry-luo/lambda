// Verifies a pinned editing-corpus inventory before executing a no-emulation
// runner. This is corpus integrity plumbing only; it does not edit a document.
import { createHash } from 'node:crypto';
import { readdir, readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';

const manifest_path = process.argv[2] || 'test/wpt/contenteditable_manifest.json';
const manifest = JSON.parse(await readFile(manifest_path, 'utf8'));
const checkout_revision = execFileSync('git', ['-C', manifest.root, 'rev-parse', 'HEAD'], {
    encoding: 'utf8'
}).trim();
let corpus_revision = checkout_revision;
if (checkout_revision !== manifest.upstream_revision) {
    try {
        // `editing/` is a linked worktree below a broader reference checkout.
        // Its parent may advance for unrelated reference pages; the pinned
        // corpus is still exact only when this subtree is byte-identical to
        // the manifest revision.
        execFileSync('git', ['-C', manifest.root, 'diff', '--quiet',
                             manifest.upstream_revision, '--', '.']);
        corpus_revision = manifest.upstream_revision;
    } catch {
        throw new Error(`Corpus revision mismatch: expected ${manifest.upstream_revision}, got ${checkout_revision}`);
    }
}
// A pinned corpus can be a linked Git worktree below a larger checkout. Scope
// cleanliness to that corpus directory so generated artifacts elsewhere do
// not mask a modified upstream fixture.
const status = execFileSync('git', ['-C', manifest.root, 'status', '--porcelain', '--', '.'], {
    encoding: 'utf8'
}).trim();
const dirty_selected = status.split('\n').filter((line) => {
    const path = line.slice(3);
    return manifest.entries.some((entry) => path === entry.path || path.endsWith(`/${entry.path}`));
});
if (dirty_selected.length) {
    throw new Error(`Pinned corpus entries are modified: ${dirty_selected.join(', ')}`);
}
if (manifest.require_clean_worktree && status) {
    throw new Error(`Pinned corpus worktree is not clean: ${status}`);
}

async function collect_files(directory) {
    const entries = await readdir(directory, { withFileTypes: true });
    const nested = await Promise.all(entries.map(async (entry) => {
        const path = join(directory, entry.name);
        return entry.isDirectory() ? collect_files(path) : [path];
    }));
    return nested.flat();
}

const manifest_entries = new Map(manifest.entries.map((entry) => [entry.path, entry]));
const source_files = (await readdir(manifest.root, { withFileTypes: true }))
    .filter((entry) => entry.isFile())
    .map((entry) => entry.name)
    .sort();
const listed_files = [...manifest_entries.keys()].sort();
if (manifest.inventory === 'directory' && source_files.join('\n') !== listed_files.join('\n')) {
    throw new Error(`WPT manifest inventory mismatch: source=[${source_files.join(', ')}], manifest=[${listed_files.join(', ')}]`);
}

for (const [source_file, entry] of manifest_entries) {
    const bytes = await readFile(join(manifest.root, source_file));
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    if (sha256 !== entry.sha256) {
        throw new Error(`Corpus hash mismatch for ${source_file}: expected ${entry.sha256}, got ${sha256}`);
    }
}

for (const artifact of manifest.corpus_artifacts || []) {
    const bytes = await readFile(join(manifest.root, artifact.path));
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    if (sha256 !== artifact.sha256) {
        throw new Error(`Corpus artifact hash mismatch for ${artifact.path}: expected ${artifact.sha256}, got ${sha256}`);
    }
}

if (manifest.not_yet_promoted) {
    const corpus_files = await collect_files(manifest.root);
    const html_files = corpus_files.filter((path) => path.endsWith('.html'));
    if (corpus_files.length !== manifest.not_yet_promoted.corpus_files ||
        html_files.length !== manifest.not_yet_promoted.html_files) {
        throw new Error(`Corpus count mismatch: expected ${manifest.not_yet_promoted.corpus_files}/${manifest.not_yet_promoted.html_files} files/html, got ${corpus_files.length}/${html_files.length}`);
    }
}

const automated = manifest.entries.filter((entry) => entry.runner === manifest.runner);
if (automated.length === 0) {
    throw new Error('Corpus manifest has no automated contenteditable cases');
}
const checkout_note = checkout_revision === corpus_revision ? ''
    : ` (checkout ${checkout_revision} has the same pinned corpus subtree)`;
console.log(`Verified ${manifest.entries.length} pinned contenteditable cases at ${corpus_revision}; ${automated.length} run by the no-emulation harness.${checkout_note}`);
