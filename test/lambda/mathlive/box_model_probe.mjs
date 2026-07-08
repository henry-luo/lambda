#!/usr/bin/env node
// Dump the math renderer's root box metadata for Phase A box-model migration.
//
// This intentionally checks the Lambda box object before HTML serialization, so
// ML migrations can assert `model: "ml"` without inferring it from markup.

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');
const DEFAULT_SCRIPT_PATH = path.join(TEMP_DIR, 'box_model_probe.ls');
const DEFAULT_LAMBDA = path.join(PROJECT_ROOT, 'lambda.exe');

function parseArgs(argv) {
  const opts = {
    formula: null,
    display: true,
    lambda: DEFAULT_LAMBDA,
    script: DEFAULT_SCRIPT_PATH,
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--formula') opts.formula = argv[++i] ?? '';
    else if (arg === '--display') opts.display = true;
    else if (arg === '--inline') opts.display = false;
    else if (arg === '--lambda') opts.lambda = path.resolve(argv[++i] ?? '');
    else if (arg === '--script') opts.script = path.resolve(argv[++i] ?? '');
    else if (arg === '--help' || arg === '-h') {
      printHelp();
      process.exit(0);
    } else if (opts.formula == null) {
      opts.formula = arg;
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }

  if (opts.formula == null || opts.formula.length === 0) {
    throw new Error('missing formula; pass --formula "x^2" or a positional formula');
  }

  return opts;
}

function printHelp() {
  console.log(`Usage: node test/lambda/mathlive/box_model_probe.mjs [options] FORMULA

Options:
  --formula TEXT    Formula to parse and render
  --display         Use display context (default)
  --inline          Use text context
  --lambda PATH     Lambda executable path, default ./lambda.exe
  --script PATH     Generated Lambda script path, default ./temp/...
`);
}

function lambdaString(value) {
  return JSON.stringify(value);
}

function buildLambdaScript(opts) {
  const ctxFn = opts.display ? 'display_context' : 'text_context';
  return `import render_pkg: lambda.package.math.render
import ctx: lambda.package.math.context
import opt: lambda.package.math.optimize
import box: lambda.package.math.box

let formula = ${lambdaString(opts.formula)}
let ast^err = parse(formula, {type: "math", flavor: "latex"})
let result = if (^err) {
    {formula: formula, error: string(^err)}
} else {
    let raw_box = render_pkg.render_node(ast, ctx.${ctxFn}())
    let bx = opt.coalesce(raw_box)
    {
        formula: formula,
        error: "no-error",
        model: bx.model,
        is_ml: box.is_ml_box(bx),
        type: bx.type,
        height: bx.height,
        depth: bx.depth,
        width: bx.width,
        has_height_raw: bx.height_raw != null,
        has_depth_raw: bx.depth_raw != null,
        has_render_height: bx.render_height != null,
        has_render_depth: bx.render_depth != null,
        has_render_total: bx.render_total != null,
        has_left_right_render_depth: bx.left_right_render_depth != null,
        has_left_right_render_total: bx.left_right_render_total != null,
        max_font_size: bx.max_font_size
    }
}
format(result, "json")
`;
}

function parseLambdaJson(stdout) {
  const trimmed = stdout.trim();
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    return JSON.parse(trimmed.slice(1, -1));
  }
  return JSON.parse(trimmed);
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  fs.mkdirSync(TEMP_DIR, { recursive: true });
  fs.writeFileSync(opts.script, buildLambdaScript(opts));

  const result = spawnSync(opts.lambda, ['--no-log', opts.script], {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  });

  if (result.status !== 0) {
    process.stderr.write(result.stderr);
    process.exit(result.status ?? 1);
  }

  console.log(JSON.stringify(parseLambdaJson(result.stdout), null, 2));
}

main();
