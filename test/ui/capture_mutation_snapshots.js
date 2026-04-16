#!/usr/bin/env node

/**
 * Capture reference PNG snapshots from a real browser for Phase 7 mutation tests.
 *
 * Usage:
 *   node test/ui/capture_mutation_snapshots.js [--output-dir <dir>] [--viewport <WxH>] [html_file...]
 *
 * Examples:
 *   # Capture all phase 7 test pages
 *   node test/ui/capture_mutation_snapshots.js
 *
 *   # Capture specific file with custom viewport
 *   node test/ui/capture_mutation_snapshots.js --viewport 800x600 test/ui/test_phase7a_resize_mutation.html
 *
 * For each HTML file, captures multiple snapshots:
 *   - Initial state at default viewport
 *   - After resize mutations (various viewport sizes)
 *   - After scroll mutations
 *   - After animation mutations (pausing at specific times)
 *
 * Output: test/ui/snapshots/<basename>_<state>.png
 */

const puppeteer = require('puppeteer');
const path = require('path');
const fs = require('fs');

// Default test files and their capture configurations
const CAPTURE_CONFIGS = [
    {
        html: 'test/ui/test_phase7a_resize_mutation.html',
        snapshots: [
            { name: 'initial_1024x768', viewport: { width: 1024, height: 768 } },
            { name: 'resized_600x400', viewport: { width: 600, height: 400 } },
            { name: 'resized_320x480', viewport: { width: 320, height: 480 } },
            { name: 'restored_1024x768', viewport: { width: 1024, height: 768 } }
        ]
    },
    {
        html: 'test/ui/test_phase7b_scroll_mutation.html',
        snapshots: [
            { name: 'scroll_top', viewport: { width: 800, height: 600 }, scroll: { x: 0, y: 0 } },
            { name: 'scroll_y400', viewport: { width: 800, height: 600 }, scroll: { x: 0, y: 400 } },
            { name: 'scroll_y1200', viewport: { width: 800, height: 600 }, scroll: { x: 0, y: 1200 } }
        ]
    },
    {
        html: 'test/ui/test_phase7c_animation_mutation.html',
        snapshots: [
            { name: 'initial', viewport: { width: 800, height: 600 } },
            {
                name: 'animation_250ms',
                viewport: { width: 800, height: 600 },
                animation: { pauseAt: 250 }
            },
            {
                name: 'animation_500ms',
                viewport: { width: 800, height: 600 },
                animation: { pauseAt: 500 }
            }
        ]
    }
];

function parseArgs() {
    const args = process.argv.slice(2);
    let outputDir = 'test/ui/snapshots';
    let defaultViewport = { width: 1024, height: 768 };
    const htmlFiles = [];

    for (let i = 0; i < args.length; i++) {
        if (args[i] === '--output-dir' && i + 1 < args.length) {
            outputDir = args[++i];
        } else if (args[i] === '--viewport' && i + 1 < args.length) {
            const parts = args[++i].split('x');
            defaultViewport = { width: parseInt(parts[0]), height: parseInt(parts[1]) };
        } else if (!args[i].startsWith('-')) {
            htmlFiles.push(args[i]);
        }
    }

    return { outputDir, defaultViewport, htmlFiles };
}

async function captureSnapshot(page, config, snapshotConfig, outputDir, basename) {
    const outputName = `${basename}_${snapshotConfig.name}.png`;
    const outputPath = path.join(outputDir, outputName);

    // Set viewport
    const vp = snapshotConfig.viewport || { width: 1024, height: 768 };
    await page.setViewport({ width: vp.width, height: vp.height, deviceScaleFactor: 1 });

    // Reload to ensure clean state (for resize tests, we just resize)
    if (snapshotConfig.scroll) {
        await page.evaluate((x, y) => window.scrollTo(x, y), snapshotConfig.scroll.x, snapshotConfig.scroll.y);
        // Wait for scroll to settle
        await new Promise(r => setTimeout(r, 100));
    }

    // Handle animation snapshots
    if (snapshotConfig.animation) {
        const ms = snapshotConfig.animation.pauseAt;
        await page.evaluate((targetMs) => {
            // Pause all animations and seek to target time
            const animations = document.getAnimations();
            animations.forEach(a => {
                a.pause();
                a.currentTime = targetMs;
            });
            // Also handle CSS transitions via getComputedStyle forcing
        }, ms);
        await new Promise(r => setTimeout(r, 50));
    }

    // Wait for rendering
    await new Promise(r => setTimeout(r, 100));

    // Capture screenshot
    await page.screenshot({ path: outputPath, fullPage: false });
    console.log(`  Saved: ${outputPath} (${vp.width}x${vp.height})`);
}

async function main() {
    const { outputDir, defaultViewport, htmlFiles } = parseArgs();

    // Ensure output directory exists
    fs.mkdirSync(outputDir, { recursive: true });

    // Determine which configs to run
    let configs = CAPTURE_CONFIGS;
    if (htmlFiles.length > 0) {
        configs = htmlFiles.map(f => {
            const existing = CAPTURE_CONFIGS.find(c => c.html === f);
            if (existing) return existing;
            // Default: single snapshot at given viewport
            return {
                html: f,
                snapshots: [{ name: 'default', viewport: defaultViewport }]
            };
        });
    }

    const browser = await puppeteer.launch({
        headless: 'new',
        args: ['--no-sandbox', '--disable-setuid-sandbox']
    });

    try {
        const page = await browser.newPage();

        for (const config of configs) {
            const htmlPath = path.resolve(config.html);
            if (!fs.existsSync(htmlPath)) {
                console.error(`  SKIP: ${config.html} (file not found)`);
                continue;
            }

            const basename = path.basename(config.html, '.html');
            console.log(`Capturing: ${config.html}`);

            // Load the page
            const fileUrl = `file://${htmlPath}`;
            await page.goto(fileUrl, { waitUntil: 'networkidle0' });
            await new Promise(r => setTimeout(r, 200));

            for (const snap of config.snapshots) {
                // For resize tests, we just change viewport (page stays loaded)
                // For scroll tests, we scroll
                // For animation tests, we pause/seek
                await captureSnapshot(page, config, snap, outputDir, basename);
            }
        }

        await page.close();
    } finally {
        await browser.close();
    }

    console.log('\nDone. Reference snapshots saved to:', outputDir);
}

main().catch(err => {
    console.error('Error:', err.message);
    process.exit(1);
});
