#!/usr/bin/env node
/**
 * convert_math_fixtures.js - Convert JSON math fixtures to .tex files
 * 
 * Converts test/math/fixtures/*.json to test/latex/fixtures/math/mathlive/*.tex
 * Each test case becomes a standalone .tex file with a minimal document structure.
 * 
 * Usage:
 *   node utils/convert_math_fixtures.js [options]
 * 
 * Options:
 *   --dry-run    Show what would be done without doing it
 *   --verbose    Show detailed output
 *   --clean      Remove existing converted files first
 */

const fs = require('fs');
const path = require('path');

// Configuration
const PROJECT_ROOT = path.resolve(__dirname, '..');
const MATH_FIXTURES_DIR = path.join(PROJECT_ROOT, 'test', 'math', 'fixtures');
const OUTPUT_DIR = path.join(PROJECT_ROOT, 'test', 'latex', 'fixtures', 'math', 'mathlive');

// Parse arguments
const args = process.argv.slice(2);
const options = {
    dryRun: args.includes('--dry-run'),
    verbose: args.includes('--verbose'),
    clean: args.includes('--clean')
};

/**
 * Unescape LaTeX string from JSON (double backslashes -> single)
 */
function unescapeLatex(latex) {
    return latex.replace(/\\\\/g, '\\');
}

/**
 * Generate a safe filename from category and test id
 */
function makeFilename(category, id, index) {
    // Use index for uniqueness since id can repeat across categories
    return `${category}_${String(index).padStart(3, '0')}.tex`;
}

/**
 * Wrap a math expression in a minimal LaTeX document
 */
function wrapInDocument(latex, description, source) {
    // Determine if this is display math or inline
    const isDisplay = latex.includes('\\[') || latex.includes('\\begin{');
    
    // Clean up the latex - remove outer \[ \] if present since we'll add our own
    let mathContent = latex.trim();
    if (mathContent.startsWith('\\[') && mathContent.endsWith('\\]')) {
        mathContent = mathContent.slice(2, -2).trim();
    }
    
    // Build document
    const lines = [
        `% ${description}`,
        `% Source: ${source}`,
        `\\documentclass{article}`,
        `\\usepackage{amsmath}`,
        `\\usepackage{amssymb}`,
        `\\pagestyle{empty}`,
        `\\begin{document}`,
        isDisplay ? `\\[${mathContent}\\]` : `$${mathContent}$`,
        `\\end{document}`,
        ''
    ];
    
    return lines.join('\n');
}

/**
 * Load and parse a JSON fixture file
 */
function loadFixture(filepath) {
    try {
        const content = fs.readFileSync(filepath, 'utf8');
        return JSON.parse(content);
    } catch (err) {
        console.error(`Error loading ${filepath}: ${err.message}`);
        return null;
    }
}

/**
 * Process a single JSON fixture file
 */
function processFixtureFile(filepath) {
    const fixture = loadFixture(filepath);
    if (!fixture) return { created: 0, skipped: 0, errors: 0 };
    
    const category = fixture.category || path.basename(filepath, '.json');
    const tests = fixture.tests || [];
    
    let created = 0, skipped = 0, errors = 0;
    
    tests.forEach((test, index) => {
        const filename = makeFilename(category, test.id, index);
        const outputPath = path.join(OUTPUT_DIR, filename);
        
        // Skip if already exists and not forcing
        if (fs.existsSync(outputPath) && !options.clean) {
            if (options.verbose) {
                console.log(`  [skip] ${filename} (exists)`);
            }
            skipped++;
            return;
        }
        
        try {
            const latex = unescapeLatex(test.latex);
            const content = wrapInDocument(
                latex,
                test.description || `${category} test ${index}`,
                test.source || 'mathlive'
            );
            
            if (options.dryRun) {
                console.log(`  [would create] ${filename}`);
            } else {
                fs.writeFileSync(outputPath, content, 'utf8');
                if (options.verbose) {
                    console.log(`  [created] ${filename}`);
                }
            }
            created++;
        } catch (err) {
            console.error(`  [error] ${filename}: ${err.message}`);
            errors++;
        }
    });
    
    return { created, skipped, errors };
}

/**
 * Process the combined all_tests.json file
 */
function processAllTestsFile(filepath) {
    const data = loadFixture(filepath);
    if (!data || !data.categories) return { created: 0, skipped: 0, errors: 0 };
    
    let totalCreated = 0, totalSkipped = 0, totalErrors = 0;
    
    for (const [category, tests] of Object.entries(data.categories)) {
        if (options.verbose) {
            console.log(`Processing category: ${category} (${tests.length} tests)`);
        }
        
        tests.forEach((test, index) => {
            const filename = makeFilename(category, test.id, index);
            const outputPath = path.join(OUTPUT_DIR, filename);
            
            // Skip if already exists and not forcing
            if (fs.existsSync(outputPath) && !options.clean) {
                if (options.verbose) {
                    console.log(`  [skip] ${filename} (exists)`);
                }
                totalSkipped++;
                return;
            }
            
            try {
                const latex = unescapeLatex(test.latex);
                const content = wrapInDocument(
                    latex,
                    test.description || `${category} test ${index}`,
                    test.source || 'mathlive'
                );
                
                if (options.dryRun) {
                    console.log(`  [would create] ${filename}`);
                } else {
                    fs.writeFileSync(outputPath, content, 'utf8');
                    if (options.verbose) {
                        console.log(`  [created] ${filename}`);
                    }
                }
                totalCreated++;
            } catch (err) {
                console.error(`  [error] ${filename}: ${err.message}`);
                totalErrors++;
            }
        });
    }
    
    return { created: totalCreated, skipped: totalSkipped, errors: totalErrors };
}

/**
 * Main entry point
 */
function main() {
    console.log('Converting math JSON fixtures to .tex files...');
    console.log(`  Source: ${MATH_FIXTURES_DIR}`);
    console.log(`  Output: ${OUTPUT_DIR}`);
    if (options.dryRun) {
        console.log('  Mode: DRY RUN');
    }
    console.log('');
    
    // Create output directory
    if (!options.dryRun) {
        fs.mkdirSync(OUTPUT_DIR, { recursive: true });
    }
    
    // Clean if requested
    if (options.clean && !options.dryRun) {
        const existingFiles = fs.readdirSync(OUTPUT_DIR).filter(f => f.endsWith('.tex'));
        for (const file of existingFiles) {
            fs.unlinkSync(path.join(OUTPUT_DIR, file));
        }
        console.log(`Cleaned ${existingFiles.length} existing files\n`);
    }
    
    let totalCreated = 0, totalSkipped = 0, totalErrors = 0;
    
    // Check for combined file first
    const allTestsPath = path.join(MATH_FIXTURES_DIR, 'all_tests.json');
    if (fs.existsSync(allTestsPath)) {
        console.log('Processing all_tests.json (combined fixture)...');
        const result = processAllTestsFile(allTestsPath);
        totalCreated += result.created;
        totalSkipped += result.skipped;
        totalErrors += result.errors;
    } else {
        // Process individual fixture files
        const files = fs.readdirSync(MATH_FIXTURES_DIR)
            .filter(f => f.endsWith('.json') && f !== 'all_tests.json');
        
        for (const file of files) {
            const filepath = path.join(MATH_FIXTURES_DIR, file);
            console.log(`Processing ${file}...`);
            const result = processFixtureFile(filepath);
            totalCreated += result.created;
            totalSkipped += result.skipped;
            totalErrors += result.errors;
        }
    }
    
    console.log('');
    console.log('Summary:');
    console.log(`  Created: ${totalCreated}`);
    console.log(`  Skipped: ${totalSkipped}`);
    console.log(`  Errors: ${totalErrors}`);
    
    if (totalCreated > 0 && !options.dryRun) {
        console.log('');
        console.log('Next steps:');
        console.log('  1. Generate DVI references:');
        console.log('     node utils/generate_latex_refs.js --output-format=dvi --test=mathlive');
        console.log('  2. Run tests:');
        console.log('     ./test/test_latex_dvi_compare_gtest.exe --gtest_filter="*Mathlive*"');
    }
    
    return totalErrors === 0 ? 0 : 1;
}

process.exit(main());
