import fs from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import {parse} from 'acorn';
import * as walk from 'acorn-walk';

const referenceDir = path.dirname(new URL(import.meta.url).pathname);
const suiteDir = path.resolve(referenceDir, '..');
const config = JSON.parse(await fs.readFile(path.join(referenceDir, 'corpus.json'), 'utf8'));
const mode = process.argv.includes('--write') ? 'write' : 'check';
const upstreamArg = process.argv.indexOf('--upstream');
const upstream = path.resolve(upstreamArg >= 0 ? process.argv[upstreamArg + 1]
  : path.join(suiteDir, '../../../../temp/mermaid-upstream'));

function callName(node) {
  return node?.callee?.type === 'Identifier' ? node.callee.name : null;
}

function literalText(node) {
  if (!node) return null;
  if (node.type === 'Literal' && typeof node.value === 'string') return node.value;
  if (node.type === 'TemplateLiteral' && node.expressions.length === 0) {
    return node.quasis[0].value.cooked;
  }
  if (node.type === 'CallExpression' && callName(node) === 'cleanupComments') {
    return literalText(node.arguments[0]);
  }
  return null;
}

function parsedSource(testNode) {
  let source = null;
  walk.simple(testNode.arguments[1], {
    CallExpression(node) {
      if (source !== null || node.callee?.type !== 'MemberExpression') return;
      const property = node.callee.property;
      if ((property?.name ?? property?.value) === 'parse') source = literalText(node.arguments[0]);
    },
  });
  return source;
}

function adaptedSource(source) {
  return `${source.split(/\r?\n/).map((line) => line.trim()).join('\n').trim()}\n`;
}

const sourceCache = new Map();
let failures = 0;
for (const entry of config.cases) {
  const sourceFile = path.join(upstream, entry.file);
  let tests = sourceCache.get(sourceFile);
  if (!tests) {
    const source = await fs.readFile(sourceFile, 'utf8');
    const ast = parse(source, {ecmaVersion: 'latest', sourceType: 'module'});
    tests = [];
    walk.simple(ast, {
      CallExpression(node) {
        if (!['it', 'test'].includes(callName(node))) return;
        const name = literalText(node.arguments[0]);
        if (name !== null) tests.push({name, source: parsedSource(node)});
      },
    });
    sourceCache.set(sourceFile, tests);
  }
  const matches = tests.filter((test) => test.name === entry.test);
  const selected = matches[entry.occurrence ?? 0];
  if (!selected?.source) {
    console.error(`missing upstream test source: ${entry.file} :: ${entry.test}`);
    failures++;
    continue;
  }
  const adapted = adaptedSource(selected.source);
  const destination = path.join(suiteDir, entry.destination);
  if (mode === 'write') {
    await fs.mkdir(path.dirname(destination), {recursive: true});
    await fs.writeFile(destination, adapted);
  } else {
    const checkedIn = await fs.readFile(destination, 'utf8').catch(() => null);
    if (checkedIn !== adapted) {
      console.error(`adapted case drift: ${entry.destination}`);
      failures++;
    }
  }
}

if (failures > 0) process.exitCode = 1;
else console.log(`${mode === 'write' ? 'wrote' : 'verified'} ${config.cases.length} pinned Mermaid cases`);
