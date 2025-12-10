#!/usr/bin/env node

/**
 * Script to merge all reference JSON files from subdirectories
 * directly under ./test/layout/reference/
 *
 * This script:
 * 1. Scans all subdirectories under reference/
 * 2. Copies all JSON files to the reference/ root directory
 * 3. Handles filename conflicts by prefixing with category name if needed
 */

const fs = require('fs').promises;
const path = require('path');

const referenceDir = path.join(__dirname, 'reference');

async function getSubdirectories() {
    const items = await fs.readdir(referenceDir, { withFileTypes: true });
    return items
        .filter(item => item.isDirectory())
        .map(item => item.name)
        .filter(name => !name.startsWith('.'));
}

async function getAllJsonFiles() {
    const subdirs = await getSubdirectories();
    const allFiles = [];

    for (const subdir of subdirs) {
        const subdirPath = path.join(referenceDir, subdir);
        try {
            const files = await fs.readdir(subdirPath);
            const jsonFiles = files.filter(f => f.endsWith('.json'));

            for (const file of jsonFiles) {
                allFiles.push({
                    category: subdir,
                    filename: file,
                    sourcePath: path.join(subdirPath, file)
                });
            }
        } catch (error) {
            console.error(`Error reading ${subdir}: ${error.message}`);
        }
    }

    return allFiles;
}

async function mergeReferences() {
    console.log('🔍 Scanning reference subdirectories...\n');

    const allFiles = await getAllJsonFiles();

    // Group files by filename to detect conflicts
    const filesByName = new Map();
    for (const file of allFiles) {
        if (!filesByName.has(file.filename)) {
            filesByName.set(file.filename, []);
        }
        filesByName.get(file.filename).push(file);
    }

    // Report statistics
    const subdirs = await getSubdirectories();
    console.log(`📂 Found ${subdirs.length} subdirectories:`);
    for (const subdir of subdirs) {
        const count = allFiles.filter(f => f.category === subdir).length;
        console.log(`   ${subdir}: ${count} files`);
    }
    console.log(`\n📄 Total files: ${allFiles.length}`);

    // Check for conflicts (same filename in multiple categories)
    const conflicts = [];
    for (const [filename, files] of filesByName) {
        if (files.length > 1) {
            conflicts.push({ filename, files });
        }
    }

    if (conflicts.length > 0) {
        console.log(`\n⚠️  Found ${conflicts.length} filename conflicts:`);
        for (const conflict of conflicts) {
            console.log(`   ${conflict.filename}:`);
            for (const file of conflict.files) {
                console.log(`      - ${file.category}/`);
            }
        }
    }

    // Copy files to reference root
    console.log('\n📋 Copying files to reference/ root...\n');

    let copied = 0;
    let skipped = 0;

    for (const file of allFiles) {
        let targetFilename = file.filename;

        // Check if this is a conflict - if so, use the first occurrence
        // (files in different categories with same name are likely duplicates)
        const conflictEntry = filesByName.get(file.filename);
        if (conflictEntry && conflictEntry.length > 1) {
            // Only copy the first occurrence, skip others
            if (conflictEntry[0] !== file) {
                console.log(`   ⏭️  Skipping duplicate: ${file.category}/${file.filename}`);
                skipped++;
                continue;
            }
        }

        const targetPath = path.join(referenceDir, targetFilename);

        try {
            // Check if target already exists in root (not from a subdir)
            try {
                await fs.access(targetPath);
                // File exists - check if it's the same content
                const sourceContent = await fs.readFile(file.sourcePath, 'utf8');
                const targetContent = await fs.readFile(targetPath, 'utf8');

                if (sourceContent === targetContent) {
                    console.log(`   ✓ Already exists (identical): ${targetFilename}`);
                    skipped++;
                    continue;
                } else {
                    console.log(`   ⚠️  Overwriting: ${targetFilename} (from ${file.category}/)`);
                }
            } catch (e) {
                // File doesn't exist, proceed with copy
            }

            await fs.copyFile(file.sourcePath, targetPath);
            console.log(`   ✅ Copied: ${file.category}/${file.filename} → ${targetFilename}`);
            copied++;
        } catch (error) {
            console.error(`   ❌ Error copying ${file.filename}: ${error.message}`);
        }
    }

    console.log(`\n✨ Done! Copied ${copied} files, skipped ${skipped} files.`);

    // Optionally remove empty subdirectories
    const removeSubdirs = process.argv.includes('--remove-subdirs');
    if (removeSubdirs) {
        console.log('\n🗑️  Removing subdirectories...');
        for (const subdir of subdirs) {
            const subdirPath = path.join(referenceDir, subdir);
            try {
                await fs.rm(subdirPath, { recursive: true });
                console.log(`   ✅ Removed: ${subdir}/`);
            } catch (error) {
                console.error(`   ❌ Error removing ${subdir}: ${error.message}`);
            }
        }
    } else {
        console.log('\n💡 Tip: Run with --remove-subdirs to remove the subdirectories after merging.');
    }
}

// Main execution
mergeReferences().catch(error => {
    console.error('Error:', error.message);
    process.exit(1);
});
