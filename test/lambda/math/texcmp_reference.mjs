import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { prepare_reference } from './texcmp_dialect.mjs';

export const reference_preamble = ['texcmp_preamble.tex', 'texcmp_compat.tex']
  .map((name) => fs.readFileSync(new URL(name, import.meta.url), 'utf8')).join('\n');
const LOCAL_TEXMF = fileURLToPath(new URL('../../../temp/mathcmp-texmf', import.meta.url));

function extra_preamble(features) {
  const chunks = [];
  if (features.includes('mathabx')) chunks.push(fs.readFileSync(new URL('./texcmp_mathx.tex', import.meta.url), 'utf8'));
  if (features.includes('integrals')) chunks.push(fs.readFileSync(new URL('./texcmp_integrals.tex', import.meta.url), 'utf8'));
  if (features.includes('accents')) chunks.push('\\usepackage{accents}',
    fs.readFileSync(new URL('./texcmp_accents.tex', import.meta.url), 'utf8'));
  if (features.includes('reactions')) chunks.push(fs.readFileSync(new URL('./texcmp_reactions.tex', import.meta.url), 'utf8'));
  if (features.includes('cyrillic')) chunks.push('\\usepackage[T2A,OT1]{fontenc}');
  if (features.includes('japanese') || features.includes('korean')) chunks.push('\\usepackage{CJKutf8}');
  if (features.includes('korean')) chunks.push('\\pdfmapfile{+nanumfonts.map}');
  if (features.includes('japanese')) chunks.push('\\pdfmapfile{+dmj.map}');
  return chunks.join('\n');
}

export function stage_reference_assets(run) {
  // Match upstream's test/screenshotter-relative paths inside each isolated run.
  fs.mkdirSync(path.join(run, 'cases'), { recursive: true });
  fs.cpSync(new URL('./assets/katex/website/', import.meta.url), path.join(run, 'website'), { recursive: true });
}

export function reference_environment(scratch) {
  const env = { ...process.env, TMPDIR: scratch, TMP: scratch, TEMP: scratch,
    TEXMFOUTPUT: scratch, TEXMFVAR: path.join(scratch, 'texmf-var'),
    TEXMFCONFIG: path.join(scratch, 'texmf-config') };
  // A local user tree keeps BasicTeX additions in ./temp; retain existing user packages.
  if (fs.existsSync(path.join(LOCAL_TEXMF, 'tex'))) {
    const home = spawnSync('kpsewhich', ['-var-value=TEXMFHOME'], { env, encoding: 'utf8' });
    if (home.error || home.status !== 0) throw new Error('cannot resolve TEXMFHOME with kpsewhich');
    env.TEXMFHOME = [LOCAL_TEXMF, home.stdout.trim()].filter(Boolean).join(path.delimiter);
  }
  return env;
}

export function reference_tex(item, definitions = '', preamble = '', prepared = prepare_reference(item.tex, definitions)) {
  // Display environments provide their own delimiters; ordinary display cases need \[...\].
  const ownDisplay = /^\s*\\begin\{(?:equation|align|alignat|flalign|gather|multline)\*?\}/.test(prepared.tex);
  // A final newline terminates TeX comments; an extra blank line would end math mode.
  const formula = prepared.tex.endsWith('\n') ? prepared.tex : prepared.tex + '\n';
  const content = ownDisplay ? formula : item.display ? `\\[{${formula}}\\]` : '${' + formula + '}$';
  const box = item.display || ownDisplay
    ? '\\vbox\\bgroup\\hsize=1200pt\\linewidth=\\hsize\\textwidth=\\hsize'
    : '\\hbox\\bgroup';
  return `\\documentclass[10pt]{article}
${reference_preamble}
${extra_preamble(prepared.features)}
${preamble}
\\newbox\\ReferenceBox
\\begin{document}
${prepared.definitions}
% Primitive groups leave verbatim input unscanned; a named box survives scratch-box macros.
\\setbox\\ReferenceBox=${box}
${content}
\\egroup
\\pdfpagewidth=\\wd\\ReferenceBox \\advance\\pdfpagewidth by 4pt
\\pdfpageheight=\\ht\\ReferenceBox \\advance\\pdfpageheight by \\dp\\ReferenceBox \\advance\\pdfpageheight by 4pt
\\pdfhorigin=0pt \\pdfvorigin=0pt
\\shipout\\vbox{\\kern2pt\\hbox{\\kern2pt\\box\\ReferenceBox\\kern2pt}\\kern2pt}
\\end{document}
`;
}
