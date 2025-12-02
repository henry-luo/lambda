#!/usr/bin/env node
/**
 * Layout Comparison Tool
 *
 * Runs Radiant layout engine and compares output with browser reference data.
 *
 * Usage:
 *   node compare-layout.js --run --test <test-name> --category <category> [options]
 *   node compare-layout.js --test <test-name> --category <category> [options]
 *   node compare-layout.js <radiant-json> <browser-json> [options]
 *
 * Examples:
 *   node compare-layout.js --run --test sample3.html --category page   # Run layout then compare
 *   node compare-layout.js --test sample3.html --category page         # Compare existing output
 *   node compare-layout.js --run --test sample3.html --category page --threshold 1.0
 *   node compare-layout.js --run --test sample3.html --category page --verbose
 *
 * Options:
 *   --run               Run Radiant layout first before comparing
 *   --test <name>       Test file name (e.g., sample3.html)
 *   --category <cat>    Test category (e.g., page, flex, grid, position, baseline, basic)
 *   --threshold <n>     Difference threshold for reporting (default: 0.5)
 *   --verbose           Show all comparisons, not just differences
 *   --json              Output as JSON
 *   --elements <list>   Comma-separated list of elements to compare (e.g., "nav,header,footer")
 */

const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

// ANSI color codes
const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
};

class LayoutComparator {
  constructor(options = {}) {
    this.threshold = options.threshold || 0.5;
    this.verbose = options.verbose || false;
    this.jsonOutput = options.jsonOutput || false;
    this.elementsFilter = options.elementsFilter || null;
    this.runLayout = options.runLayout || false;
    this.projectRoot = this.findProjectRoot();
    this.lambdaExe = path.join(this.projectRoot, 'lambda.exe');
    this.viewportWidth = 1200;
    this.viewportHeight = 800;
  }

  findProjectRoot() {
    let dir = __dirname;
    while (dir !== '/') {
      if (fs.existsSync(path.join(dir, 'Makefile')) &&
          fs.existsSync(path.join(dir, 'radiant'))) {
        return dir;
      }
      dir = path.dirname(dir);
    }
    return process.cwd();
  }

  loadJson(filePath) {
    try {
      const content = fs.readFileSync(filePath, 'utf8');
      return JSON.parse(content);
    } catch (error) {
      throw new Error(`Failed to load ${filePath}: ${error.message}`);
    }
  }

  /**
   * Find HTML test file in standard locations
   */
  findHtmlFile(testName, category) {
    const baseName = testName.replace(/\.html?$/, '');
    const extensions = ['.html', '.htm'];

    // Search paths based on category
    const searchDirs = [
      path.join(this.projectRoot, 'test', 'layout', 'data', category),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'page'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'basic'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'baseline'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'flex'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'grid'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'position'),
      path.join(this.projectRoot, 'test', 'layout', 'data', 'css2.1'),
    ];

    for (const dir of searchDirs) {
      for (const ext of extensions) {
        const filePath = path.join(dir, baseName + ext);
        if (fs.existsSync(filePath)) {
          return filePath;
        }
      }
    }

    return null;
  }

  /**
   * Run Radiant layout engine on HTML file
   * Returns path to output JSON file
   */
  runRadiantLayout(htmlFile) {
    return new Promise((resolve, reject) => {
      const c = colors;

      if (!this.jsonOutput) {
        console.log(`${c.cyan}Running Radiant layout on:${c.reset} ${htmlFile}`);
      }

      // Check if lambda.exe exists
      if (!fs.existsSync(this.lambdaExe)) {
        reject(new Error(`Lambda executable not found: ${this.lambdaExe}\nRun 'make build' first.`));
        return;
      }

      const args = ['layout', htmlFile, '--width', String(this.viewportWidth), '--height', String(this.viewportHeight)];
      const proc = spawn(this.lambdaExe, args, {
        cwd: this.projectRoot
      });

      let stdout = '';
      let stderr = '';

      proc.stdout.on('data', (data) => {
        stdout += data.toString();
      });

      proc.stderr.on('data', (data) => {
        stderr += data.toString();
      });

      const timeout = setTimeout(() => {
        proc.kill();
        reject(new Error('Lambda execution timeout (30s)'));
      }, 30000);

      proc.on('close', (code) => {
        clearTimeout(timeout);
        if (code === 0) {
          // Lambda outputs to /tmp/view_tree.json
          const outputPath = '/tmp/view_tree.json';
          if (!fs.existsSync(outputPath)) {
            reject(new Error(`Lambda did not generate output at ${outputPath}`));
            return;
          }

          // Copy to test_output directory with proper naming
          const testName = path.basename(htmlFile);
          const destPath = path.join(this.projectRoot, 'test_output', `view_tree_${testName}.json`);

          try {
            fs.copyFileSync(outputPath, destPath);
            if (!this.jsonOutput) {
              console.log(`${c.green}✓${c.reset} Layout generated: ${destPath}`);
              console.log();
            }
            resolve(destPath);
          } catch (err) {
            reject(new Error(`Failed to copy output: ${err.message}`));
          }
        } else {
          reject(new Error(`Lambda failed with exit code ${code}: ${stderr}`));
        }
      });

      proc.on('error', (error) => {
        clearTimeout(timeout);
        reject(error);
      });
    });
  }

  /**
   * Find an element in the tree by tag and optional class
   */
  findElement(tree, tag, className = null) {
    const search = (node, path = '') => {
      const nodeTag = node.tag || node.nodeType || '';
      const classes = node.classes || [];
      const selector = node.selector || '';

      // Check for match
      if (nodeTag === tag) {
        if (className === null ||
            classes.includes(className) ||
            selector.includes(className)) {
          return { node, path: `${path}/${nodeTag}` };
        }
      }

      // Search children
      const children = node.children || [];
      for (let i = 0; i < children.length; i++) {
        const child = children[i];
        if (child && typeof child === 'object') {
          const result = search(child, `${path}/${nodeTag}[${i}]`);
          if (result) return result;
        }
      }
      return null;
    };

    return search(tree);
  }

  /**
   * Find all elements matching given tags
   */
  findAllElements(tree, tags) {
    const results = {};

    const search = (node, path = '') => {
      const nodeTag = node.tag || node.nodeType || '';
      const selector = node.selector || '';

      if (tags.includes(nodeTag)) {
        const key = selector || nodeTag;
        if (!results[key]) {
          results[key] = [];
        }
        results[key].push({
          layout: node.layout || {},
          computed: node.computed || {},
          path
        });
      }

      const children = node.children || [];
      for (let i = 0; i < children.length; i++) {
        const child = children[i];
        if (child && typeof child === 'object') {
          search(child, `${path}/${nodeTag}[${i}]`);
        }
      }
    };

    search(tree);
    return results;
  }

  /**
   * Compare layout values between two nodes
   */
  compareLayout(lambdaLayout, browserLayout) {
    const metrics = ['x', 'y', 'width', 'height'];
    const results = [];

    for (const metric of metrics) {
      const lambdaVal = lambdaLayout[metric];
      const browserVal = browserLayout[metric];

      if (typeof lambdaVal === 'number' && typeof browserVal === 'number') {
        const diff = lambdaVal - browserVal;
        const hasDiff = Math.abs(diff) > this.threshold;

        results.push({
          metric,
          lambda: lambdaVal,
          browser: browserVal,
          diff,
          hasDiff
        });
      } else {
        results.push({
          metric,
          lambda: lambdaVal ?? 'N/A',
          browser: browserVal ?? 'N/A',
          diff: null,
          hasDiff: false
        });
      }
    }

    return results;
  }

  /**
   * Print a horizontal line
   */
  printLine(char = '=', length = 80) {
    console.log(char.repeat(length));
  }

  /**
   * Format a number for display
   */
  formatNum(val, width = 8) {
    if (typeof val === 'number') {
      return val.toFixed(1).padStart(width);
    }
    return String(val).padStart(width);
  }

  /**
   * Compare two layout files
   */
  compare(lambdaPath, browserPath) {
    // Load data
    const lambdaData = this.loadJson(lambdaPath);
    const browserData = this.loadJson(browserPath);

    const lambdaTree = lambdaData.layout_tree;
    const browserTree = browserData.layout_tree;

    if (!lambdaTree || !browserTree) {
      throw new Error('Missing layout_tree in one or both files');
    }

    // Collect all unique elements
    const allTags = new Set();
    const collectTags = (node) => {
      const tag = node.tag || node.nodeType;
      if (tag && !['#text', 'text'].includes(tag)) {
        allTags.add(tag);
      }
      for (const child of (node.children || [])) {
        if (child && typeof child === 'object') {
          collectTags(child);
        }
      }
    };
    collectTags(lambdaTree);
    collectTags(browserTree);

    // Define elements to compare
    let elementsToCompare = [
      { tag: 'html', class: null },
      { tag: 'body', class: null },
      { tag: 'header', class: null },
      { tag: 'nav', class: null },
      { tag: 'main', class: null },
      { tag: 'article', class: null },
      { tag: 'section', class: null },
      { tag: 'aside', class: null },
      { tag: 'footer', class: null },
      { tag: 'div', class: 'container' },
      { tag: 'div', class: 'content' },
      { tag: 'div', class: 'wrapper' },
    ];

    // Filter elements if specified
    if (this.elementsFilter) {
      const filterList = this.elementsFilter.split(',').map(s => s.trim().toLowerCase());
      elementsToCompare = elementsToCompare.filter(e =>
        filterList.includes(e.tag.toLowerCase()) ||
        (e.class && filterList.includes(e.class.toLowerCase()))
      );

      // Also add any specified tags not in default list
      for (const tag of filterList) {
        if (!elementsToCompare.some(e => e.tag === tag)) {
          elementsToCompare.push({ tag, class: null });
        }
      }
    }

    // Results collection
    const comparisons = [];
    const issues = [];

    for (const { tag, class: className } of elementsToCompare) {
      const lambdaResult = this.findElement(lambdaTree, tag, className);
      const browserResult = this.findElement(browserTree, tag, className);

      const elementName = className ? `${tag}.${className}` : tag;

      if (!lambdaResult && !browserResult) {
        continue; // Element not in either tree
      }

      if (!lambdaResult) {
        issues.push({
          element: elementName,
          issue: 'Missing in Lambda output'
        });
        continue;
      }

      if (!browserResult) {
        issues.push({
          element: elementName,
          issue: 'Missing in browser reference'
        });
        continue;
      }

      const layoutComparison = this.compareLayout(
        lambdaResult.node.layout || {},
        browserResult.node.layout || {}
      );

      const hasAnyDiff = layoutComparison.some(c => c.hasDiff);

      comparisons.push({
        element: elementName,
        metrics: layoutComparison,
        hasDiff: hasAnyDiff,
        lambdaComputed: lambdaResult.node.computed || {},
        browserComputed: browserResult.node.computed || {}
      });

      // Check for specific CSS issues
      if (hasAnyDiff) {
        const lambdaComputed = lambdaResult.node.computed || {};
        const lambdaPosition = lambdaComputed.position || {};

        // Check float property
        if (lambdaPosition.float && lambdaPosition.float !== 'none') {
          const widthDiff = layoutComparison.find(c => c.metric === 'width');
          const xDiff = layoutComparison.find(c => c.metric === 'x');

          if (widthDiff && widthDiff.hasDiff && widthDiff.diff > 100) {
            issues.push({
              element: elementName,
              issue: `Float:${lambdaPosition.float} element not shrink-wrapping (width diff: ${widthDiff.diff.toFixed(1)}px)`
            });
          }

          if (xDiff && xDiff.hasDiff && Math.abs(xDiff.diff) > 100) {
            issues.push({
              element: elementName,
              issue: `Float:${lambdaPosition.float} positioning incorrect (x diff: ${xDiff.diff.toFixed(1)}px)`
            });
          }
        }
      }
    }

    return {
      lambdaPath,
      browserPath,
      viewport: browserData.browser_info?.viewport || lambdaData.test_info?.viewport,
      comparisons,
      issues,
      summary: {
        total: comparisons.length,
        withDifferences: comparisons.filter(c => c.hasDiff).length,
        issueCount: issues.length
      }
    };
  }

  /**
   * Print comparison results to console
   */
  printResults(results) {
    if (this.jsonOutput) {
      console.log(JSON.stringify(results, null, 2));
      return;
    }

    const { c } = { c: colors };

    this.printLine('=');
    console.log(`${c.bold}${c.cyan}LAYOUT COMPARISON REPORT${c.reset}`);
    this.printLine('=');
    console.log();

    console.log(`${c.dim}Lambda:${c.reset}  ${results.lambdaPath}`);
    console.log(`${c.dim}Browser:${c.reset} ${results.browserPath}`);
    if (results.viewport) {
      console.log(`${c.dim}Viewport:${c.reset} ${results.viewport.width}×${results.viewport.height}`);
    }
    console.log();

    // Print comparisons
    this.printLine('-');
    console.log(`${c.bold}ELEMENT COMPARISONS${c.reset}`);
    this.printLine('-');
    console.log();

    for (const comp of results.comparisons) {
      if (!this.verbose && !comp.hasDiff) continue;

      const statusIcon = comp.hasDiff ? `${c.red}✗${c.reset}` : `${c.green}✓${c.reset}`;
      console.log(`${statusIcon} ${c.bold}${comp.element}${c.reset}`);

      for (const metric of comp.metrics) {
        let line = `    ${metric.metric.padEnd(8)}: `;
        line += `Lambda=${this.formatNum(metric.lambda)}  `;
        line += `Browser=${this.formatNum(metric.browser)}`;

        if (metric.hasDiff) {
          const diffStr = metric.diff > 0 ? `+${metric.diff.toFixed(1)}` : metric.diff.toFixed(1);
          line += `  ${c.red}*** DIFF: ${diffStr} ***${c.reset}`;
        }

        console.log(line);
      }
      console.log();
    }

    // Print issues
    if (results.issues.length > 0) {
      this.printLine('-');
      console.log(`${c.bold}${c.yellow}ISSUES IDENTIFIED${c.reset}`);
      this.printLine('-');
      console.log();

      for (let i = 0; i < results.issues.length; i++) {
        const issue = results.issues[i];
        console.log(`${c.yellow}${i + 1}. ${issue.element}:${c.reset} ${issue.issue}`);
      }
      console.log();
    }

    // Print summary
    this.printLine('=');
    console.log(`${c.bold}SUMMARY${c.reset}`);
    this.printLine('=');
    console.log(`Total elements compared: ${results.summary.total}`);
    console.log(`Elements with differences: ${c.red}${results.summary.withDifferences}${c.reset}`);
    console.log(`Issues found: ${c.yellow}${results.summary.issueCount}${c.reset}`);
    console.log();
  }

  /**
   * Run comparison from command line arguments
   */
  async run(args) {
    let lambdaPath, browserPath;

    // Parse arguments
    const parsedArgs = {};
    for (let i = 0; i < args.length; i++) {
      if (args[i].startsWith('--')) {
        const key = args[i].substring(2);
        if (args[i + 1] && !args[i + 1].startsWith('--')) {
          parsedArgs[key] = args[++i];
        } else {
          parsedArgs[key] = true;
        }
      } else if (!lambdaPath) {
        lambdaPath = args[i];
      } else if (!browserPath) {
        browserPath = args[i];
      }
    }

    // Show help
    if (parsedArgs.help || parsedArgs.h) {
      this.printUsage();
      process.exit(0);
    }

    // Apply options
    if (parsedArgs.threshold) {
      this.threshold = parseFloat(parsedArgs.threshold);
    }
    if (parsedArgs.verbose) {
      this.verbose = true;
    }
    if (parsedArgs.json) {
      this.jsonOutput = true;
    }
    if (parsedArgs.elements) {
      this.elementsFilter = parsedArgs.elements;
    }
    if (parsedArgs.run) {
      this.runLayout = true;
    }

    // Determine test name and category
    const testName = parsedArgs.test ? parsedArgs.test.replace(/\.html?$/, '') : null;
    const category = parsedArgs.category || 'page';

    // If --run is specified, run Radiant layout first
    if (this.runLayout && testName) {
      const htmlFile = this.findHtmlFile(testName, category);
      if (!htmlFile) {
        console.error(`${colors.red}Error:${colors.reset} HTML test file not found for '${testName}' in category '${category}'`);
        console.error(`Searched in: test/layout/data/${category}/, test/layout/data/page/, etc.`);
        process.exit(2);
      }

      try {
        lambdaPath = await this.runRadiantLayout(htmlFile);
      } catch (error) {
        console.error(`${colors.red}Error running layout:${colors.reset} ${error.message}`);
        process.exit(2);
      }
    }

    // Determine file paths
    if (testName && !lambdaPath) {
      lambdaPath = path.join(this.projectRoot, 'test_output', `view_tree_${testName}.html.json`);
    }
    if (testName) {
      browserPath = path.join(this.projectRoot, 'test', 'layout', 'reference', category, `${testName}.json`);
    }

    if (!lambdaPath || !browserPath) {
      this.printUsage();
      process.exit(1);
    }

    // Resolve paths
    if (!path.isAbsolute(lambdaPath)) {
      lambdaPath = path.join(this.projectRoot, lambdaPath);
    }
    if (!path.isAbsolute(browserPath)) {
      browserPath = path.join(this.projectRoot, browserPath);
    }

    // Run comparison
    try {
      const results = this.compare(lambdaPath, browserPath);
      this.printResults(results);

      // Exit with error code if issues found
      if (results.summary.withDifferences > 0) {
        process.exit(1);
      }
    } catch (error) {
      console.error(`${colors.red}Error:${colors.reset} ${error.message}`);
      process.exit(2);
    }
  }

  printUsage() {
    console.log(`
${colors.bold}Layout Comparison Tool${colors.reset}

Run Radiant layout engine and compare output with browser reference data.

${colors.cyan}Usage:${colors.reset}
  node compare-layout.js --run --test <test-name> [--category <category>] [options]
  node compare-layout.js --test <test-name> --category <category> [options]
  node compare-layout.js <radiant-json> <browser-json> [options]

${colors.cyan}Examples:${colors.reset}
  node compare-layout.js --run --test sample3.html --category page  # Run layout then compare
  node compare-layout.js --test sample3.html --category page        # Compare existing output
  node compare-layout.js --run --test sample3 --category page       # .html is optional
  node compare-layout.js --run --test baseline_301 --category baseline

${colors.cyan}Options:${colors.reset}
  --run               Run Radiant layout first before comparing
  --test <name>       Test file name (e.g., sample3.html or sample3)
  --category <cat>    Test category (default: page)
                      Categories: page, baseline, basic, flex, grid, position, css2.1
  --threshold <n>     Difference threshold for reporting (default: 0.5)
  --verbose           Show all comparisons, not just differences
  --json              Output as JSON
  --elements <list>   Comma-separated list of elements to compare
  --help, -h          Show this help message
`);
  }
}

// Run if executed directly
if (require.main === module) {
  const comparator = new LayoutComparator();
  comparator.run(process.argv.slice(2));
}

module.exports = { LayoutComparator };
