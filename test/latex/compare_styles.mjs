#!/usr/bin/env node
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Compare raw HTML for fracs_basic_3
const ml = fs.readFileSync(path.join(__dirname, 'reference/fracs_basic_3.mathlive.html'), 'utf8');
const lambda = fs.readFileSync(path.join(__dirname, '../../temp/analysis/test.html'), 'utf8');

// Extract all style height values
const extractHeights = (html) => {
  const matches = html.match(/height:[0-9.]+em/g) || [];
  return matches.map(m => parseFloat(m.replace('height:', '')));
};

const extractVAligns = (html) => {
  const matches = html.match(/vertical-align:-?[0-9.]+em/g) || [];
  return matches.map(m => parseFloat(m.replace('vertical-align:', '')));
};

const extractTops = (html) => {
  const matches = html.match(/top:-?[0-9.]+em/g) || [];
  return matches.map(m => parseFloat(m.replace('top:', '')));
};

console.log('MathLive heights:', extractHeights(ml));
console.log('Lambda heights:  ', extractHeights(lambda));
console.log();
console.log('MathLive v-aligns:', extractVAligns(ml));
console.log('Lambda v-aligns:  ', extractVAligns(lambda));
console.log();
console.log('MathLive tops:', extractTops(ml));
console.log('Lambda tops:  ', extractTops(lambda));
