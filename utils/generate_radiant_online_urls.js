#!/usr/bin/env node

// Refresh the large online smoke-test expansion from SaaS Landing Page's
// externally linked examples while keeping the test's site coverage broad.

const fs = require('fs');
const path = require('path');
const {readPageCatalog, csvField} = require('./online_pages.cjs');

const sourceRoot = 'https://saaslandingpage.com/';
const sourceSitemap = `${sourceRoot}post-sitemap.xml`;
const defaultTarget = 700;
const maxUrlsPerSite = 2;
const requestConcurrency = 12;
const requestBatchSize = 96;
const requestTimeoutMs = 15000;
const requestAttempts = 3;

function optionValue(name, fallback) {
    const index = process.argv.indexOf(name);
    return index >= 0 && index + 1 < process.argv.length
        ? process.argv[index + 1] : fallback;
}

function normalizedSite(url) {
    const labels = url.hostname.toLowerCase().replace(/^www\./, '').split('.');
    return labels.length > 2 ? labels.slice(-2).join('.') : labels.join('.');
}

function canonicalUrl(raw) {
    try {
        const url = new URL(raw.replace(/&amp;/g, '&'));
        if (url.protocol !== 'https:') return null;
        url.hash = '';
        for (const key of [...url.searchParams.keys()]) {
            if (key.startsWith('utm_') || key === 'via') {
                url.searchParams.delete(key);
            }
        }
        if (url.pathname === '/') url.pathname = '';
        return url;
    } catch (_) {
        return null;
    }
}

function externalUrls(html) {
    const urls = [];
    const linkPattern = /<a\b[^>]*\bid="ext-link-\d+"[^>]*\bhref="([^"]+)"/gi;
    for (let match = linkPattern.exec(html); match; match = linkPattern.exec(html)) {
        const url = canonicalUrl(match[1]);
        if (url) urls.push(url);
    }
    return urls;
}

async function fetchPage(url) {
    const response = await fetchSource(url);
    if (!response.ok) throw new Error(`${url} returned HTTP ${response.status}`);
    return response.text();
}

function pause(milliseconds) {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function fetchSource(url) {
    let lastError = null;
    for (let attempt = 1; attempt <= requestAttempts; attempt++) {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), requestTimeoutMs);
        try {
            const response = await fetch(url, {
                headers: {'user-agent': 'lambda-radiant-online-url-generator/1.0'},
                signal: controller.signal,
            });
            if (response.status < 500 || attempt === requestAttempts) return response;
            lastError = new Error(`returned HTTP ${response.status}`);
        } catch (error) {
            lastError = error;
        } finally {
            clearTimeout(timeout);
        }
        await pause(attempt * 250);
    }
    throw lastError;
}

async function fetchCataloguePostUrls() {
    const response = await fetchSource(sourceSitemap);
    if (!response.ok) throw new Error(`${sourceSitemap} returned HTTP ${response.status}`);
    const sitemap = await response.text();
    const urls = [];
    const locationPattern = /<loc>(https:\/\/saaslandingpage\.com\/[^<]+)<\/loc>/g;
    for (let match = locationPattern.exec(sitemap); match;
            match = locationPattern.exec(sitemap)) {
        urls.push(match[1]);
    }
    if (urls.length === 0) throw new Error('catalogue sitemap has no post URLs');
    return urls;
}

async function mapConcurrent(values, work) {
    const results = new Array(values.length);
    let next = 0;
    async function worker() {
        while (next < values.length) {
            const index = next++;
            try {
                results[index] = await work(values[index]);
            } catch (error) {
                process.stderr.write(`generate_radiant_online_urls: skipped ${values[index]}: ${error.message}\n`);
            }
        }
    }
    await Promise.all(Array.from({length: requestConcurrency}, worker));
    return results;
}

function sourcePages(sourcePath) {
    return readPageCatalog(sourcePath).map((page) =>
        [page.label, page.url, page.expect_linked_resources]);
}

function sourceSeedUrls(sourcePath) {
    return sourcePages(sourcePath)
        .filter(([label]) => !label.startsWith('saas_landing_'))
        .map(([, value]) => canonicalUrl(value))
        .filter(Boolean);
}

function renderPages(basePages, urls) {
    const rows = [['label', 'url', 'expect_linked_resources'], ...basePages];
    for (let index = 0; index < urls.length; index++) {
        const label = `saas_landing_${String(index + 1).padStart(3, '0')}`;
        rows.push([label, urls[index], 'true']);
    }
    return `${rows.map((row) => row.map(csvField).join(',')).join('\n')}\n`;
}

async function main() {
    const target = Number(optionValue('--target', String(defaultTarget)));
    const output = optionValue('--output', path.join('test', 'online', 'pages.csv'));
    const seed = optionValue('--seed', path.join('test', 'online', 'pages.csv'));
    if (!Number.isSafeInteger(target) || target <= 0) {
        throw new Error('--target must be a positive integer');
    }

    const urls = [];
    const basePages = sourcePages(seed)
        .filter(([label]) => !label.startsWith('saas_landing_'));
    const seenUrls = new Set();
    const perSite = new Map();
    function add(url) {
        if (urls.length >= target) return;
        const site = normalizedSite(url);
        const text = url.toString();
        if (seenUrls.has(text) || (perSite.get(site) || 0) >= maxUrlsPerSite) return;
        seenUrls.add(text);
        perSite.set(site, (perSite.get(site) || 0) + 1);
        urls.push(text);
    }

    const postUrls = await fetchCataloguePostUrls();
    for (let start = 0; start < postUrls.length && urls.length < target;
            start += requestBatchSize) {
        const pages = await mapConcurrent(postUrls.slice(start,
            start + requestBatchSize), fetchPage);
        for (const page of pages) {
            for (const url of externalUrls(page || '')) add(url);
        }
    }
    // The maintained smoke-test corpus provides stable fallback domains when
    // the public directory does not expose enough entries to reach the target.
    for (const url of sourceSeedUrls(seed)) {
        if (urls.length === target) break;
        add(url);
    }
    if (urls.length !== target) {
        throw new Error(`only found ${urls.length} URLs with at most ${maxUrlsPerSite} per site`);
    }

    fs.mkdirSync(path.dirname(output), {recursive: true});
    fs.writeFileSync(output, renderPages(basePages, urls));
    process.stdout.write(`wrote ${basePages.length + urls.length} pages, including ` +
        `${urls.length} generated URLs across ${perSite.size} sites, to ${output}\n`);
}

main().catch((error) => {
    process.stderr.write(`generate_radiant_online_urls: ${error.message}\n`);
    process.exitCode = 1;
});
