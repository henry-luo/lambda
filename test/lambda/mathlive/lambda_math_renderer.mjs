// Shared driver for rendering one LaTeX formula through Lambda's math package.
// The native `lambda.exe math` CLI was retired; math rendering is a script API.

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const DEFAULT_LAMBDA = path.join(PROJECT_ROOT, 'lambda.exe');
const DEFAULT_SCRIPT = path.join(PROJECT_ROOT, 'temp', 'lambda_math_renderer.ls');
const MATHLIVE_SSR_PATH = path.join(
  PROJECT_ROOT,
  'ref/mathlive/dist/mathlive-ssr.min.mjs'
);
const MATHLIVE_MARKUP_SNAPSHOT = path.join(
  PROJECT_ROOT,
  'ref/mathlive/test/__snapshots__/markup.test.ts.snap'
);

let mathlive_exports = null;

export function render_lambda_math(formula, options = {}) {
  const display = options.display ?? true;
  const lambda = options.lambda ?? DEFAULT_LAMBDA;
  const script_path = options.script ?? DEFAULT_SCRIPT;
  const script = build_lambda_math_script(formula, display);

  fs.mkdirSync(path.dirname(script_path), { recursive: true });
  fs.writeFileSync(script_path, script);

  const result = spawnSync(lambda, ['--no-log', script_path], {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
  });
  if (result.status !== 0) {
    const detail = (result.stderr || result.stdout || '').trim().slice(0, 1000);
    throw new Error(`lambda.exe exited ${result.status ?? result.signal}: ${detail}`);
  }

  const rendered = parse_lambda_json(result.stdout);
  const parseError = find_lambda_parse_error(rendered.ast);
  if (parseError != null) rendered.error = parseError;
  return rendered;
}

export function mathlive_to_lambda_classes(html) {
  return html.replace(/ML__/g, 'lm_');
}

export function lambda_to_mathlive_classes(html) {
  return html.replace(/\blm_/g, 'ML__');
}

export async function render_mathlive_markup(formula, options = {}) {
  if (mathlive_exports == null) {
    mathlive_exports = await import(MATHLIVE_SSR_PATH);
  }
  if (typeof mathlive_exports.convertLatexToMarkup !== 'function') {
    throw new Error('MathLive SSR bundle missing convertLatexToMarkup().');
  }
  const mathstyle = (options.display ?? true) ? 'displaystyle' : 'textstyle';
  return mathlive_exports.convertLatexToMarkup(formula, {
    mathstyle,
    defaultMode: 'math',
  });
}

export function mathlive_expected_error(formula) {
  const snapshot = fs.readFileSync(MATHLIVE_MARKUP_SNAPSHOT, 'utf8');
  const lines = snapshot.split('\n');
  for (const line of lines) {
    const match = line.match(/^exports\[`[^/]+\/ (.*) errors 1`\] = `"([^"]+)"`;$/);
    if (match && unescape_snapshot_latex(match[1]) === formula) return match[2];
  }
  return null;
}

function build_lambda_math_script(formula, display) {
  const formula_literal = JSON.stringify(formula);
  const render_function = display ? 'render_display' : 'render_inline';
  return `import math_pkg: lambda.doc.math.math
import html_ser: lambda.latex.to_html

let formula = ${formula_literal}
let parsed = parse(formula, {type: "math", flavor: "latex"}) ^ { ^ }
let result = if (parsed is error) {
    {formula: formula, error: string(parsed), html: "", ast: null}
} else {
    let rendered = math_pkg.${render_function}(parsed)
    let html = html_ser.to_html(rendered)
    {formula: formula, error: "no-error", html: html, ast: parsed}
}
format(result, "json")
`;
}

function parse_lambda_json(stdout) {
  const output = stdout.trim();
  try {
    return output.startsWith('"') && output.endsWith('"')
      ? JSON.parse(output.slice(1, -1))
      : JSON.parse(output);
  } catch (error) {
    throw new Error(
      `could not parse Lambda JSON output: ${error.message}\n${output.slice(0, 1000)}`
    );
  }
}

function find_lambda_parse_error(node) {
  if (node == null || typeof node !== 'object') return null;
  if (typeof node.error === 'string') return node.error;
  if (Array.isArray(node)) {
    for (const child of node) {
      const error = find_lambda_parse_error(child);
      if (error != null) return error;
    }
  }
  for (const value of Object.values(node)) {
    const error = find_lambda_parse_error(value);
    if (error != null) return error;
  }
  return null;
}

function unescape_snapshot_latex(latex) {
  return latex.replace(/\\\\/g, '\\');
}
