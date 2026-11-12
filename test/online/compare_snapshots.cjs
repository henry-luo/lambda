#!/usr/bin/env node

// Rank broad visual differences; live content and animation still need review.
const fs = require('fs');
const path = require('path');
const {createRequire} = require('module');
const {readPageCatalog, readCsvObjects, serializeCsv} = require('../../utils/online_pages.cjs');

function pngLibrary(root) {
    try { return require('pngjs').PNG; } catch (error) {
        if (error.code !== 'MODULE_NOT_FOUND') throw error;
    }
    const render = path.join(root, 'test/render/test_radiant_render.js');
    if (!fs.existsSync(render)) throw new Error('Install pngjs or set up the test/render corpus dependencies.');
    return createRequire(fs.realpathSync(render))('pngjs').PNG;
}

function coarseImage(image, columns = 120, rows = 80) {
    const values = new Float64Array(columns * rows * 3);
    for (let y = 0; y < rows; y++) for (let x = 0; x < columns; x++) {
        const x0 = Math.floor(x * image.width / columns);
        const x1 = Math.max(x0 + 1, Math.floor((x + 1) * image.width / columns));
        const y0 = Math.floor(y * image.height / rows);
        const y1 = Math.max(y0 + 1, Math.floor((y + 1) * image.height / rows));
        const target = (y * columns + x) * 3;
        for (let sy = y0; sy < y1; sy++) for (let sx = x0; sx < x1; sx++) {
            const offset = (sy * image.width + sx) * 4;
            const alpha = image.data[offset + 3] / 255;
            for (let c = 0; c < 3; c++) values[target + c] +=
                (image.data[offset + c] * alpha + 255 * (1 - alpha)) / 255;
        }
        const count = (x1 - x0) * (y1 - y0);
        for (let c = 0; c < 3; c++) values[target + c] /= count;
    }
    return values;
}

function compareImages(left, right) {
    const a = coarseImage(left), b = coarseImage(right);
    const variation = (values) => {
        const means = [0, 0, 0];
        for (let i = 0; i < values.length; i++) means[i % 3] += values[i] / (values.length / 3);
        let square = 0;
        for (let i = 0; i < values.length; i++) square += (values[i] - means[i % 3]) ** 2;
        return Math.sqrt(square / values.length);
    };
    const leftVariation = variation(a), rightVariation = variation(b);
    let error = 0, changed = 0;
    for (let i = 0; i < a.length; i += 3) {
        let square = 0;
        for (let c = 0; c < 3; c++) {
            const delta = a[i + c] - b[i + c];
            error += Math.abs(delta);
            square += delta * delta;
        }
        if (Math.sqrt(square / 3) > 0.12) changed++;
    }
    const meanError = error / a.length;
    const changedArea = changed / (a.length / 3);
    const blankMismatch = Math.min(leftVariation, rightVariation) < 0.001 &&
        Math.max(leftVariation, rightVariation) > 0.01 && changedArea >= 0.12 && meanError >= 0.02;
    return {mean_error: meanError, changed_area: changedArea,
        blank_mismatch: blankMismatch,
        major_candidate: blankMismatch || (meanError >= 0.12 && changedArea >= 0.35),
        dimensions_match: left.width === right.width && left.height === right.height};
}

const escapeHtml = (value) => String(value ?? '').replace(/[&<>"']/g,
    (character) => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[character]));

function reviewHtml(rows, output, snapshots, generatedAt) {
    const image = (file) => escapeHtml(path.relative(output, path.join(snapshots, file)));
    const severityCounts = Object.fromEntries(['critical', 'high', 'low'].map((severity) =>
        [severity, rows.filter((row) => row.severity === severity).length]));
    return `<!doctype html><meta charset="utf-8"><title>Online snapshot comparison</title>
<style>body{font:16px system-ui;margin:24px;background:#eee}article{background:white;padding:12px;margin:16px 0}
.pair{display:flex;gap:12px}.pair a{width:50%}img{width:100%;border:1px solid #aaa}h2{font-size:18px}
input{padding:8px;width:360px}.hidden{display:none}small{display:block}.severity{font-weight:700;padding:2px 7px;border-radius:4px}
.critical{background:#8b0000;color:white}.high{background:#b54708;color:white}.low{background:#e5e7eb;color:#222}</style>
<h1>Online snapshot comparison</h1><p>Generated ${escapeHtml(generatedAt)}. ${rows.length} catalog pages, ordered by severity: ${severityCounts.critical} critical, ${severityCounts.high} high and ${severityCounts.low} low. Critical means the comparison is blocked by a capture or image problem; high means an automated major difference or a major omission retained in visual review; low means below the major triage threshold. Coarse 120 × 80 RGB averages suppress fine text noise.
Major candidate: mean channel error ≥12% and changed area ≥35% (RGB distance &gt;12%),
or a nearly uniform image against content with mean error ≥2% and changed area ≥12%.
These are review candidates, not confirmed engine defects. Live content, animation, consent and capture dates can differ.</p>
<input id="filter" placeholder="Filter label or URL"><label><input id="high" type="checkbox" style="width:auto"> Critical and high severity only</label>
${rows.map((row) => `<article data-search="${escapeHtml(row.label + ' ' + row.url)}" data-severity="${row.severity}">
<h2><span class="severity ${row.severity}">${row.severity.toUpperCase()}</span> ${escapeHtml(row.label)} — ${row.status === 'compared' ?
    `${(row.mean_error * 100).toFixed(1)}% mean error; ${(row.changed_area * 100).toFixed(1)}% area${row.major_candidate ? ' · MAJOR CANDIDATE' : ''}` : escapeHtml(row.status)}</h2>
<a href="${escapeHtml(row.url)}">${escapeHtml(row.url)}</a><small>${escapeHtml(row.error)}</small>
${row.severity_reason ? `<small>${escapeHtml(row.severity_reason)}</small>` : ''}
<small>Lambda ${row.lambda_width || '?'} × ${row.lambda_height || '?'} · ${escapeHtml(row.lambda_attempt_utc || 'capture date not recorded')}
 / Chromium ${row.browser_width || '?'} × ${row.browser_height || '?'} · ${escapeHtml(row.browser_attempt_utc || 'capture date not recorded')}</small>
${row.status === 'compared' ? `<div class="pair"><a href="${image(row.lambda_snapshot)}">Lambda<img loading="lazy" src="${image(row.lambda_snapshot)}"></a>
<a href="${image(row.browser_snapshot)}">Chromium<img loading="lazy" src="${image(row.browser_snapshot)}"></a></div>` : ''}</article>`).join('\n')}
<script>function filter(){for(const a of document.querySelectorAll('article'))a.classList.toggle('hidden',
!a.dataset.search.toLowerCase().includes(document.querySelector('#filter').value.toLowerCase())||
(document.querySelector('#high').checked&&a.dataset.severity==='low'))}
document.querySelector('#filter').oninput=filter;document.querySelector('#high').onchange=filter;</script>`;
}

function main(args = process.argv.slice(2)) {
    const root = path.resolve(__dirname, '../..');
    let output = path.join(root, 'test/online/comparison');
    for (const arg of args) {
        if (arg.startsWith('--output=')) output = path.resolve(arg.slice(9));
        else throw new Error(`unknown option: ${arg}`);
    }
    const PNG = pngLibrary(root);
    const snapshots = path.join(__dirname, 'snapshots');
    const manifest = new Map(readCsvObjects(path.join(snapshots, 'capture_results.csv')).map((row) => [row.label, row]));
    const annotations = path.join(__dirname, 'review_annotations.json');
    const auditPath = fs.existsSync(annotations) ? annotations
        : path.join(snapshots, 'visual_comparison_review_round2.json');
    const reviewAudit = fs.existsSync(auditPath) ? JSON.parse(fs.readFileSync(auditPath, 'utf8')) : null;
    const manualMajor = new Map((reviewAudit?.reviews || [])
        .filter((review) => review.manual_major_remaining)
        .map((review) => [review.label, review.manual_major_remaining]));
    const rows = readPageCatalog(path.join(__dirname, 'pages.csv')).map((page) => {
        const capture = manifest.get(page.label);
        const row = {label: page.label, url: page.url, status: 'compared', error: '',
            lambda_snapshot: capture?.lambda_snapshot || '', browser_snapshot: capture?.browser_snapshot || '',
            lambda_attempt_utc: capture?.lambda_attempt_utc || '', browser_attempt_utc: capture?.browser_attempt_utc || ''};
        if (!capture || capture.url !== page.url || capture.lambda_ok !== 'true' || capture.browser_ok !== 'true') {
            return {...row, status: 'capture_unavailable', error: capture?.lambda_error || capture?.browser_error || 'missing or stale capture'};
        }
        try {
            const left = PNG.sync.read(fs.readFileSync(path.join(snapshots, row.lambda_snapshot)));
            const right = PNG.sync.read(fs.readFileSync(path.join(snapshots, row.browser_snapshot)));
            return {...row, ...compareImages(left, right), lambda_width: left.width, lambda_height: left.height,
                browser_width: right.width, browser_height: right.height};
        } catch (error) { return {...row, status: 'image_error', error: error.message}; }
    });
    for (const row of rows) {
        row.manual_major_remaining = manualMajor.get(row.label) || '';
        if (row.status !== 'compared' || row.dimensions_match === false) {
            row.severity = 'critical';
            row.severity_reason = row.dimensions_match === false ? 'Image dimensions differ.' : 'A complete image comparison is unavailable.';
        } else if (row.major_candidate || row.manual_major_remaining) {
            row.severity = 'high';
            row.severity_reason = row.manual_major_remaining || 'Automated broad-difference threshold reached.';
        } else {
            row.severity = 'low';
            row.severity_reason = 'Below the major triage threshold.';
        }
    }
    const severityRank = {critical: 0, high: 1, low: 2};
    rows.sort((a, b) => severityRank[a.severity] - severityRank[b.severity] ||
        (b.mean_error || 0) - (a.mean_error || 0) ||
        (b.changed_area || 0) - (a.changed_area || 0) || a.label.localeCompare(b.label));
    fs.mkdirSync(output, {recursive: true});
    const generatedAt = new Date().toISOString();
    const summary = {catalog: rows.length, compared: rows.filter((r) => r.status === 'compared').length,
        generated_at: generatedAt,
        major_candidates: rows.filter((r) => r.major_candidate).length,
        manual_major_omissions: rows.filter((r) => r.manual_major_remaining && !r.major_candidate).length,
        severity: Object.fromEntries(['critical', 'high', 'low'].map((severity) =>
            [severity, rows.filter((r) => r.severity === severity).length])),
        unavailable: rows.filter((r) => r.status !== 'compared').length,
        dimension_mismatches: rows.filter((r) => r.dimensions_match === false).length};
    fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({summary, rows}, null, 2) + '\n');
    fs.writeFileSync(path.join(output, 'results.csv'), serializeCsv([...new Set(rows.flatMap(Object.keys))], rows));
    fs.writeFileSync(path.join(output, 'index.html'), reviewHtml(rows, output, snapshots, generatedAt));
    process.stdout.write(JSON.stringify(summary) + '\n');
}

if (require.main === module) main();
module.exports = {coarseImage, compareImages, pngLibrary};
