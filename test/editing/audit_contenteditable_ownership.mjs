// D7.2.5 ownership guard: package policy must not reappear in native edit
// mechanisms. The audit reports a stable inventory under temp/ for CI review.
import { mkdir, readFile, writeFile } from 'node:fs/promises';

const mechanism_files = [
    'radiant/editing_dom_waist.cpp',
    'lambda/module/radiant/radiant_module.cpp',
    'lambda/module/radiant/radiant_dom_bridge.cpp'
];
const legacy_roots = [
    'radiant',
    'lambda/dom',
    'lambda/module/radiant'
];
const policy_literals = [
    'bold', 'italic', 'underline', 'strikethrough', 'insertParagraph',
    'formatBlock', 'insertOrderedList', 'insertUnorderedList', 'createLink',
    'insertImage'
];
const legacy_channels = [
    'pending_dom_edit',
    'dom_edit_set_pending',
    'dom_edit_clear_pending',
    'dom_edit_prepare_pending',
    'dom_edit_apply_epoch',
    'dom_edit_caret_node',
    'dom_edit_caret_offset',
    's_dom_edit_apply_epoch',
    's_dom_edit_caret_node',
    's_dom_edit_caret_u16'
];

async function text(path) {
    return readFile(path, 'utf8');
}

const policy_hits = [];
for (const source_file of mechanism_files) {
    const source = await text(source_file);
    for (const literal of policy_literals) {
        if (source.includes(`"${literal}"`)) policy_hits.push({ source_file, literal });
    }
}

// Keep the legacy channel search finite and reviewable rather than treating
// generic `undo`/`redo` event labels as contenteditable policy: form controls
// retain their independently owned value history.
const { execFileSync } = await import('node:child_process');
const legacy_hits = [];
for (const channel of legacy_channels) {
    let output = '';
    try {
        output = execFileSync('rg', ['-l', '--glob', '*.{c,cc,cpp,h,hpp}', channel, ...legacy_roots], {
            encoding: 'utf8'
        }).trim();
    } catch (error) {
        if (error.status !== 1) throw error;
    }
    for (const source_file of output ? output.split('\n') : []) {
        legacy_hits.push({ channel, source_file });
    }
}

const report = {
    ruling: 'D7.2.5',
    mechanism_files,
    policy_literals,
    legacy_channels,
    policy_hits,
    legacy_hits,
    pass: policy_hits.length === 0 && legacy_hits.length === 0
};
await mkdir('temp', { recursive: true });
await writeFile('temp/contenteditable-ownership-audit.json', `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify(report));
if (!report.pass) process.exitCode = 1;
