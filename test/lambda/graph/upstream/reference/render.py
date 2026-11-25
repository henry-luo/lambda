#!/usr/bin/env python3
"""Generate the independent SVGs from pinned upstream tools."""

from argparse import ArgumentParser
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[5]
SUITE = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent


def run(args, cwd=ROOT):
    subprocess.run(args, cwd=cwd, check=True, timeout=90)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--d2', required=True, type=Path)
    parser.add_argument('--java', default='java')
    parser.add_argument('--structurizr', required=True, type=Path)
    parser.add_argument('--node-tools', required=True, type=Path)
    parser.add_argument('--chrome', required=True, type=Path)
    options = parser.parse_args()
    output = ROOT / 'temp/graph-upstream/reference'
    output.mkdir(parents=True, exist_ok=True)
    os.environ['TMPDIR'] = str(output)
    fixtures = json.loads((SUITE / 'sources.json').read_text())['fixtures']
    structurizr = [SUITE / entry['file'] for entry in fixtures if 'views' in entry]
    workspaces = output / 'structurizr'
    workspaces.mkdir(exist_ok=True)
    run([options.java, '-Djava.io.tmpdir=' + str(output), '-cp',
         str(options.structurizr.resolve() / 'lib/*'), TOOLS / 'StructurizrReference.java',
         workspaces, *structurizr])

    batch = []
    for entry in fixtures:
        source = SUITE / entry['file']
        family = source.parent.name
        target = output / family / (source.stem + '.svg')
        target.parent.mkdir(exist_ok=True)
        if family == 'd2':
            run([options.d2.resolve(), '--layout', 'dagre', source, target])
        elif family == 'structurizr':
            for key in entry['views']:
                dot = target.with_name(source.stem + '--' + key + '.gv')
                batch.append([dot, dot.with_suffix('.svg')])
        elif family == 'tikz':
            tex = target.with_suffix('.tex')
            tex.write_text('\\documentclass{article}\n\\usepackage{tikz}\n'
                           '\\pagestyle{empty}\n\\begin{document}\n' +
                           source.read_text() + '\\end{document}\n')
            run(['pdflatex', '-no-shell-escape', '-interaction=batchmode', '-halt-on-error',
                 tex.name], cwd=target.parent)
            run(['pdftocairo', '-svg', target.with_suffix('.pdf'), target])
        else:
            batch.append([source, target])
    listing = output / 'browser.json'
    listing.write_text(json.dumps(batch, default=str))
    run(['node', TOOLS / 'render.mjs', options.node_tools.resolve(),
         options.chrome.resolve(), listing])
    print(f'Wrote {sum(len(case.get("views", [None])) for case in fixtures)} references to {output}')


if __name__ == '__main__':
    main()
