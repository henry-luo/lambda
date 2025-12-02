#!/usr/bin/env node

/**
 * Web Page Downloader
 *
 * Downloads a static web page along with its CSS, images, and web fonts.
 * Rewrites the HTML to point to local resources.
 *
 * Usage: node download-page.js <url> <output-dir> [options]
 *
 * Options:
 *   --prefix <prefix>   Add prefix to all resource filenames (useful for shared res dirs)
 *   --name <name>       Custom name for the HTML file (default: index.html)
 *
 * Example: node download-page.js https://example.com ./output --prefix example --name example.html
 */

const https = require('https');
const http = require('http');
const fs = require('fs');
const path = require('path');
const { URL } = require('url');
const crypto = require('crypto');

// resource mapping: original URL -> local filename
const resourceMap = new Map();

// prefix for resource filenames (set via CLI)
let resourcePrefix = '';

/**
 * Convert a URL to a safe ASCII filename
 */
function toSafeFilename(urlStr, extension) {
    const url = new URL(urlStr);
    // use pathname and query as basis
    let name = url.pathname + (url.search || '');

    // remove leading slash
    name = name.replace(/^\//, '');

    // if empty, use hash of full URL
    if (!name || name === '/') {
        name = crypto.createHash('md5').update(urlStr).digest('hex').slice(0, 12);
    }

    // replace unsafe chars with underscore
    name = name.replace(/[^a-zA-Z0-9._-]/g, '_');

    // collapse multiple underscores
    name = name.replace(/_+/g, '_');

    // truncate if too long (keep extension room)
    if (name.length > 100) {
        const hash = crypto.createHash('md5').update(urlStr).digest('hex').slice(0, 8);
        name = name.slice(0, 80) + '_' + hash;
    }

    // ensure proper extension
    if (extension) {
        const extLower = extension.toLowerCase();
        if (!name.toLowerCase().endsWith(extLower)) {
            name = name.replace(/\.[^.]*$/, '') + extLower;
        }
    }

    // add prefix if specified
    if (resourcePrefix) {
        name = resourcePrefix + '_' + name;
    }

    return name;
}

/**
 * Fetch a URL and return the response as a buffer
 */
function fetch(urlStr, options = {}) {
    return new Promise((resolve, reject) => {
        const url = new URL(urlStr);
        const protocol = url.protocol === 'https:' ? https : http;

        const reqOptions = {
            hostname: url.hostname,
            port: url.port || (url.protocol === 'https:' ? 443 : 80),
            path: url.pathname + url.search,
            method: 'GET',
            headers: {
                'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36',
                'Accept': options.accept || '*/*',
                ...options.headers
            }
        };

        const req = protocol.request(reqOptions, (res) => {
            // handle redirects
            if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
                const redirectUrl = new URL(res.headers.location, urlStr).href;
                console.log(`  Redirecting to: ${redirectUrl}`);
                resolve(fetch(redirectUrl, options));
                return;
            }

            if (res.statusCode !== 200) {
                reject(new Error(`HTTP ${res.statusCode} for ${urlStr}`));
                return;
            }

            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('end', () => {
                resolve({
                    buffer: Buffer.concat(chunks),
                    contentType: res.headers['content-type'] || '',
                    url: urlStr
                });
            });
        });

        req.on('error', reject);
        req.setTimeout(30000, () => {
            req.destroy();
            reject(new Error(`Timeout fetching ${urlStr}`));
        });
        req.end();
    });
}

/**
 * Determine file extension from content type or URL
 */
function getExtension(urlStr, contentType) {
    const contentTypeMap = {
        'text/css': '.css',
        'text/html': '.html',
        'image/png': '.png',
        'image/jpeg': '.jpg',
        'image/gif': '.gif',
        'image/svg+xml': '.svg',
        'image/webp': '.webp',
        'image/avif': '.avif',
        'image/x-icon': '.ico',
        'image/vnd.microsoft.icon': '.ico',
        'font/woff': '.woff',
        'font/woff2': '.woff2',
        'font/ttf': '.ttf',
        'font/otf': '.otf',
        'application/font-woff': '.woff',
        'application/font-woff2': '.woff2',
        'application/x-font-ttf': '.ttf',
        'application/x-font-otf': '.otf',
        'application/vnd.ms-fontobject': '.eot',
    };

    // try content type first
    if (contentType) {
        const ct = contentType.split(';')[0].trim().toLowerCase();
        if (contentTypeMap[ct]) {
            return contentTypeMap[ct];
        }
    }

    // fall back to URL extension
    const url = new URL(urlStr);
    const pathname = url.pathname;
    const match = pathname.match(/\.([a-zA-Z0-9]+)$/);
    if (match) {
        return '.' + match[1].toLowerCase();
    }

    return '';
}

/**
 * Check if a URL is a font resource
 */
function isFontUrl(urlStr, contentType) {
    const fontExtensions = ['.woff', '.woff2', '.ttf', '.otf', '.eot'];
    const ext = getExtension(urlStr, contentType);
    return fontExtensions.includes(ext);
}

/**
 * Check if a URL is an image resource
 */
function isImageUrl(urlStr, contentType) {
    const imageExtensions = ['.png', '.jpg', '.jpeg', '.gif', '.svg', '.webp', '.avif', '.ico', '.bmp'];
    const ext = getExtension(urlStr, contentType);
    return imageExtensions.includes(ext);
}

/**
 * Extract URLs from CSS content
 */
function extractUrlsFromCss(cssContent, baseUrl) {
    const urls = [];

    // match url(...) patterns
    const urlRegex = /url\(\s*(['"]?)([^'"()]+)\1\s*\)/gi;
    let match;

    while ((match = urlRegex.exec(cssContent)) !== null) {
        let url = match[2].trim();

        // skip data URLs and absolute data URIs
        if (url.startsWith('data:')) {
            continue;
        }

        // resolve relative URLs
        try {
            const absoluteUrl = new URL(url, baseUrl).href;
            urls.push({
                original: match[0],
                url: absoluteUrl,
                rawUrl: url
            });
        } catch (e) {
            console.warn(`  Warning: Could not parse URL: ${url}`);
        }
    }

    // match @import url(...) or @import "..."
    const importRegex = /@import\s+(?:url\(\s*)?(['"]?)([^'"();\s]+)\1(?:\s*\))?/gi;

    while ((match = importRegex.exec(cssContent)) !== null) {
        let url = match[2].trim();

        if (url.startsWith('data:')) {
            continue;
        }

        try {
            const absoluteUrl = new URL(url, baseUrl).href;
            urls.push({
                original: match[0],
                url: absoluteUrl,
                rawUrl: url,
                isImport: true
            });
        } catch (e) {
            console.warn(`  Warning: Could not parse @import URL: ${url}`);
        }
    }

    return urls;
}

/**
 * Rewrite CSS content to use local URLs
 */
function rewriteCss(cssContent, baseUrl, resDir) {
    const urls = extractUrlsFromCss(cssContent, baseUrl);
    let rewritten = cssContent;

    for (const { original, url, isImport } of urls) {
        if (resourceMap.has(url)) {
            const localFile = resourceMap.get(url);
            const localPath = `${resDir}/${localFile}`;

            if (isImport) {
                // rewrite @import
                const newImport = original.replace(new RegExp(escapeRegex(url.split('/').pop()), 'g'), localPath);
                rewritten = rewritten.replace(original, `@import url('${localPath}')`);
            } else {
                // rewrite url()
                rewritten = rewritten.replace(original, `url('${localPath}')`);
            }
        }
    }

    return rewritten;
}

/**
 * Escape special regex characters
 */
function escapeRegex(str) {
    return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/**
 * Extract resource URLs from HTML
 */
function extractResourcesFromHtml(html, baseUrl) {
    const resources = {
        css: [],
        images: [],
        fonts: []
    };

    // extract <link rel="stylesheet" href="...">
    const linkRegex = /<link[^>]+rel\s*=\s*["']?stylesheet["']?[^>]*href\s*=\s*["']([^"']+)["'][^>]*>/gi;
    let match;
    while ((match = linkRegex.exec(html)) !== null) {
        try {
            resources.css.push(new URL(match[1], baseUrl).href);
        } catch (e) {
            console.warn(`  Warning: Could not parse CSS URL: ${match[1]}`);
        }
    }

    // also match href before rel
    const linkRegex2 = /<link[^>]+href\s*=\s*["']([^"']+)["'][^>]*rel\s*=\s*["']?stylesheet["']?[^>]*>/gi;
    while ((match = linkRegex2.exec(html)) !== null) {
        try {
            const url = new URL(match[1], baseUrl).href;
            if (!resources.css.includes(url)) {
                resources.css.push(url);
            }
        } catch (e) {
            console.warn(`  Warning: Could not parse CSS URL: ${match[1]}`);
        }
    }

    // extract <img src="...">
    const imgRegex = /<img[^>]+src\s*=\s*["']([^"']+)["'][^>]*>/gi;
    while ((match = imgRegex.exec(html)) !== null) {
        if (!match[1].startsWith('data:')) {
            try {
                resources.images.push(new URL(match[1], baseUrl).href);
            } catch (e) {
                console.warn(`  Warning: Could not parse image URL: ${match[1]}`);
            }
        }
    }

    // extract srcset images
    const srcsetRegex = /srcset\s*=\s*["']([^"']+)["']/gi;
    while ((match = srcsetRegex.exec(html)) !== null) {
        const srcset = match[1];
        const srcsetParts = srcset.split(',');
        for (const part of srcsetParts) {
            const url = part.trim().split(/\s+/)[0];
            if (url && !url.startsWith('data:')) {
                try {
                    resources.images.push(new URL(url, baseUrl).href);
                } catch (e) {
                    console.warn(`  Warning: Could not parse srcset URL: ${url}`);
                }
            }
        }
    }

    // extract background images in inline styles
    const styleRegex = /style\s*=\s*["']([^"']*url\([^)]+\)[^"']*)["']/gi;
    while ((match = styleRegex.exec(html)) !== null) {
        const styleUrls = extractUrlsFromCss(match[1], baseUrl);
        for (const { url } of styleUrls) {
            if (isImageUrl(url, '')) {
                resources.images.push(url);
            }
        }
    }

    // extract <link rel="icon" href="..."> and other icon links
    const iconRegex = /<link[^>]+rel\s*=\s*["']?(?:icon|shortcut icon|apple-touch-icon)[^"']*["']?[^>]*href\s*=\s*["']([^"']+)["'][^>]*>/gi;
    while ((match = iconRegex.exec(html)) !== null) {
        if (!match[1].startsWith('data:')) {
            try {
                resources.images.push(new URL(match[1], baseUrl).href);
            } catch (e) {
                console.warn(`  Warning: Could not parse icon URL: ${match[1]}`);
            }
        }
    }

    // extract <link rel="preload" as="font" href="...">
    const fontPreloadRegex = /<link[^>]+rel\s*=\s*["']?preload["']?[^>]*as\s*=\s*["']?font["']?[^>]*href\s*=\s*["']([^"']+)["'][^>]*>/gi;
    while ((match = fontPreloadRegex.exec(html)) !== null) {
        try {
            resources.fonts.push(new URL(match[1], baseUrl).href);
        } catch (e) {
            console.warn(`  Warning: Could not parse font URL: ${match[1]}`);
        }
    }

    // deduplicate
    resources.css = [...new Set(resources.css)];
    resources.images = [...new Set(resources.images)];
    resources.fonts = [...new Set(resources.fonts)];

    return resources;
}

/**
 * Rewrite HTML to use local resources
 */
function rewriteHtml(html, baseUrl) {
    let rewritten = html;

    // rewrite CSS links
    const linkRegex = /(<link[^>]+href\s*=\s*["'])([^"']+)(["'][^>]*>)/gi;
    rewritten = rewritten.replace(linkRegex, (match, pre, url, post) => {
        try {
            const absoluteUrl = new URL(url, baseUrl).href;
            if (resourceMap.has(absoluteUrl)) {
                return pre + 'res/' + resourceMap.get(absoluteUrl) + post;
            }
        } catch (e) {}
        return match;
    });

    // rewrite img src
    const imgRegex = /(<img[^>]+src\s*=\s*["'])([^"']+)(["'][^>]*>)/gi;
    rewritten = rewritten.replace(imgRegex, (match, pre, url, post) => {
        if (url.startsWith('data:')) return match;
        try {
            const absoluteUrl = new URL(url, baseUrl).href;
            if (resourceMap.has(absoluteUrl)) {
                return pre + 'res/' + resourceMap.get(absoluteUrl) + post;
            }
        } catch (e) {}
        return match;
    });

    // rewrite srcset
    const srcsetRegex = /(srcset\s*=\s*["'])([^"']+)(["'])/gi;
    rewritten = rewritten.replace(srcsetRegex, (match, pre, srcset, post) => {
        const parts = srcset.split(',').map(part => {
            const [url, descriptor] = part.trim().split(/\s+/);
            if (!url || url.startsWith('data:')) return part;
            try {
                const absoluteUrl = new URL(url, baseUrl).href;
                if (resourceMap.has(absoluteUrl)) {
                    return 'res/' + resourceMap.get(absoluteUrl) + (descriptor ? ' ' + descriptor : '');
                }
            } catch (e) {}
            return part;
        });
        return pre + parts.join(', ') + post;
    });

    // rewrite inline style url()
    const styleRegex = /(style\s*=\s*["'])([^"']*)(["'])/gi;
    rewritten = rewritten.replace(styleRegex, (match, pre, style, post) => {
        if (!style.includes('url(')) return match;
        const newStyle = style.replace(/url\(\s*(['"]?)([^'"()]+)\1\s*\)/gi, (urlMatch, quote, url) => {
            if (url.startsWith('data:')) return urlMatch;
            try {
                const absoluteUrl = new URL(url, baseUrl).href;
                if (resourceMap.has(absoluteUrl)) {
                    return `url('res/${resourceMap.get(absoluteUrl)}')`;
                }
            } catch (e) {}
            return urlMatch;
        });
        return pre + newStyle + post;
    });

    return rewritten;
}

/**
 * Download a resource and save it locally
 */
async function downloadResource(url, outputDir, subdir = 'res') {
    if (resourceMap.has(url)) {
        return resourceMap.get(url);
    }

    console.log(`  Downloading: ${url}`);

    try {
        const response = await fetch(url);
        const extension = getExtension(url, response.contentType);
        const filename = toSafeFilename(url, extension);

        const resDir = path.join(outputDir, subdir);
        if (!fs.existsSync(resDir)) {
            fs.mkdirSync(resDir, { recursive: true });
        }

        const filePath = path.join(resDir, filename);
        fs.writeFileSync(filePath, response.buffer);

        resourceMap.set(url, filename);
        console.log(`  Saved: ${subdir}/${filename}`);

        return { filename, buffer: response.buffer, contentType: response.contentType };
    } catch (e) {
        console.error(`  Error downloading ${url}: ${e.message}`);
        return null;
    }
}

/**
 * Process CSS file: download it and extract/download referenced resources
 */
async function processCss(cssUrl, outputDir) {
    const result = await downloadResource(cssUrl, outputDir);
    if (!result) return;

    const cssContent = result.buffer.toString('utf-8');
    const urls = extractUrlsFromCss(cssContent, cssUrl);

    // download resources referenced in CSS
    for (const { url } of urls) {
        if (isFontUrl(url, '') || isImageUrl(url, '')) {
            await downloadResource(url, outputDir);
        } else if (url.endsWith('.css')) {
            // handle @import
            await processCss(url, outputDir);
        }
    }

    // rewrite CSS to use local paths
    const rewrittenCss = rewriteCss(cssContent, cssUrl, '.');
    const filename = resourceMap.get(cssUrl);
    const filePath = path.join(outputDir, 'res', filename);
    fs.writeFileSync(filePath, rewrittenCss);
}

/**
 * Main download function
 */
async function downloadPage(url, outputDir, htmlFilename = 'index.html') {
    console.log(`Downloading page: ${url}`);
    console.log(`Output directory: ${outputDir}`);
    if (resourcePrefix) {
        console.log(`Resource prefix: ${resourcePrefix}`);
    }

    // create output directory
    if (!fs.existsSync(outputDir)) {
        fs.mkdirSync(outputDir, { recursive: true });
    }

    // download main HTML
    console.log('\nFetching HTML...');
    const htmlResponse = await fetch(url, { accept: 'text/html' });
    let html = htmlResponse.buffer.toString('utf-8');

    // extract resources
    console.log('\nExtracting resources...');
    const resources = extractResourcesFromHtml(html, url);

    console.log(`  Found ${resources.css.length} CSS files`);
    console.log(`  Found ${resources.images.length} images`);
    console.log(`  Found ${resources.fonts.length} fonts (preloaded)`);

    // download CSS files (and their referenced resources)
    console.log('\nDownloading CSS files...');
    for (const cssUrl of resources.css) {
        await processCss(cssUrl, outputDir);
    }

    // download images
    console.log('\nDownloading images...');
    for (const imageUrl of resources.images) {
        await downloadResource(imageUrl, outputDir);
    }

    // download fonts
    console.log('\nDownloading fonts...');
    for (const fontUrl of resources.fonts) {
        await downloadResource(fontUrl, outputDir);
    }

    // rewrite HTML
    console.log('\nRewriting HTML...');
    html = rewriteHtml(html, url);

    // save HTML
    const htmlPath = path.join(outputDir, htmlFilename);
    fs.writeFileSync(htmlPath, html);
    console.log(`\nSaved: ${htmlFilename}`);

    console.log(`\nDownload complete! ${resourceMap.size} resources downloaded.`);
    console.log(`Open ${htmlPath} in a browser to view the page.`);
}

// CLI entry point
const args = process.argv.slice(2);

// parse options
let pageUrl = null;
let outputDir = null;
let htmlName = 'index.html';

for (let i = 0; i < args.length; i++) {
    if (args[i] === '--prefix' && args[i + 1]) {
        resourcePrefix = args[i + 1];
        i++;
    } else if (args[i] === '--name' && args[i + 1]) {
        htmlName = args[i + 1];
        i++;
    } else if (!pageUrl) {
        pageUrl = args[i];
    } else if (!outputDir) {
        outputDir = args[i];
    }
}

if (!pageUrl || !outputDir) {
    console.log('Usage: node download-page.js <url> <output-dir> [options]');
    console.log('');
    console.log('Options:');
    console.log('  --prefix <prefix>   Add prefix to all resource filenames');
    console.log('  --name <name>       Custom name for the HTML file (default: index.html)');
    console.log('');
    console.log('Example: node download-page.js https://example.com ./output --prefix example --name example.html');
    process.exit(1);
}

// validate URL
try {
    new URL(pageUrl);
} catch (e) {
    console.error(`Error: Invalid URL: ${pageUrl}`);
    process.exit(1);
}

// run
downloadPage(pageUrl, path.resolve(outputDir), htmlName)
    .catch(e => {
        console.error(`Error: ${e.message}`);
        process.exit(1);
    });
