#!/usr/bin/env python3
"""Ruling index for the two formal specs (Doc_Convention §3).

Usage:
  python3 utils/formal_index.py --write        # regenerate doc/Lambda_Formal_Index.md
  python3 utils/formal_index.py --check        # exit 1 when the index is stale
  python3 utils/formal_index.py D7.3.6 [S4.5]  # print rulings / sections with file:line

Make targets: `make formal-index`, `make check-formal-index` (also run by `make lint`).
"""

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SPECS = [
    ('Semantics', 'doc/Lambda_Formal_Semantics.md'),
    ('Design', 'doc/Lambda_Formal_Design.md'),
]
INDEX = 'doc/Lambda_Formal_Index.md'

ID_PAT = r'(?:SI|DI|SO|DO|S|D)\d+(?:\.\d+)*'
# a ruling bullet: `- **S4.5.3v2*** text`, `- **D1.2v2* — Title.** text`, `- **SO7** text`
BULLET_RE = re.compile(r'^- \*\*(?P<id>' + ID_PAT + r')(?P<ver>v\d+)?(?P<star>\*)?'
                       r'(?: — (?P<title>.+?))?\*\*\s*(?P<rest>.*)$', re.S)
HEADING_RE = re.compile(r'^(#{2,4}) (.*)$')
SECTION_ID_RE = re.compile(r'^(' + ID_PAT + r')\b')
TITLE_MAX = 110


class Entry:
    def __init__(self, kind, rid, label, line, title, text):
        self.kind, self.rid, self.label = kind, rid, label   # rid: bare ID, label: with vN / *
        self.line, self.title, self.text = line, title, text


def read_lines(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read().split('\n')


def bullet_block(lines, i):
    """Return the source lines of the bullet starting at lines[i] (continuations are indented)."""
    end = i + 1
    while end < len(lines) and lines[end].startswith('  ') and lines[end].strip():
        end += 1
    return lines[i:end]


def short_title(m):
    """Use the bullet's explicit title (`— Title.` or a leading **bold** clause), else its first clause."""
    if m.group('title'):
        title = m.group('title')
    else:
        rest = m.group('rest')
        bold = re.match(r'\*\*(.+?)\*\*', rest, re.S)
        if bold:
            title = bold.group(1)
        else:
            title = re.split(r'(?<=[.;])\s|\s—\s', rest, maxsplit=1)[0]
    title = ' '.join(title.split()).rstrip('.:')
    if len(title) > TITLE_MAX:
        title = title[:TITLE_MAX].rsplit(' ', 1)[0] + ' …'
    return title.replace('|', '\\|')


def parse_spec(rel):
    lines = read_lines(rel)
    entries, appendix = [], ''
    for i, line in enumerate(lines):
        h = HEADING_RE.match(line)
        if h:
            text = h.group(2).strip()
            if len(h.group(1)) == 2:
                appendix = text if text.startswith('Appendix') else ''
            sid = SECTION_ID_RE.match(text)
            if sid and not appendix:
                entries.append(Entry('section', sid.group(1), sid.group(1), i + 1,
                                     text[len(sid.group(1)):].strip().replace('|', '\\|'), [line]))
            continue
        if appendix.startswith('Appendix A') or appendix.startswith('Appendix C'):
            continue  # footnotes and the record index cite IDs; they do not define them
        if not line.startswith('- **'):
            continue
        block = bullet_block(lines, i)
        m = BULLET_RE.match(' '.join(s.strip() for s in block))
        if not m:
            continue
        label = m.group('id') + (m.group('ver') or '') + ('*' if m.group('star') else '')
        kind = 'open' if appendix.startswith('Appendix B') else 'ruling'
        entries.append(Entry(kind, m.group('id'), label, i + 1, short_title(m), block))
    return lines, entries


def render():
    out = [
        '# Lambda Formal Spec Index',
        '',
        '> **Generated** by `utils/formal_index.py` — do not edit. After editing either',
        '> formal spec run `make formal-index`; `make check-formal-index` (part of',
        '> `make lint`) fails while this file is stale. Not normative: the specs win.',
        '',
        'One row per ruling (`S#`/`D#`), invariant (`SI#`/`DI#`) and open issue',
        '(`SO#`/`DO#`), with the spec line where it starts. `*` marks a ruling that is',
        'not or only partly implemented (see the spec\'s Appendix A). To read a ruling',
        'in full — including lines too long for a viewer — run',
        '`python3 utils/formal_index.py D7.3.6` (an ID or a section such as `S4.5`).',
        '',
    ]
    for name, rel in SPECS:
        lines, entries = parse_spec(rel)
        # a reused ID is a spec defect to escalate (Doc_Convention §2), not to renumber here
        seen = {}
        for e in entries:
            if e.kind != 'section' and e.rid in seen:
                print('formal-index: warning: %s defines %s twice (lines %d and %d)'
                      % (rel, e.rid, seen[e.rid], e.line), file=sys.stderr)
            seen.setdefault(e.rid, e.line)
        version = next((l for l in lines[:10] if l.startswith('**Spec version:**')), '')
        version = version.replace('**Spec version:**', '').strip()
        out += ['## %s — [`%s`](%s) %s' % (name, os.path.basename(rel), os.path.basename(rel),
                                           ('v' + version) if version else ''), '',
                '| ID | Line | Title |', '|---|---|---|']
        for e in entries:
            if e.kind == 'section':
                out.append('| **§%s** | %d | *%s* |' % (e.rid, e.line, e.title))
            else:
                out.append('| %s | %d | %s |' % (e.label, e.line, e.title))
        out.append('')
    return '\n'.join(out)


def lookup(query):
    found = False
    for _, rel in SPECS:
        lines, entries = parse_spec(rel)
        for e in entries:
            if e.rid != query:
                continue
            found = True
            if e.kind == 'section':
                inner = [x.label for x in entries
                         if x.kind != 'section' and x.rid.startswith(query + '.')]
                print('%s:%d  %s' % (rel, e.line, e.text[0]))
                print('  rulings: ' + (', '.join(inner) if inner else '(none)'))
            else:
                print('%s:%d' % (rel, e.line))
                print('\n'.join(e.text))
            print()
    return found


def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__.strip())
        return 0
    index_path = os.path.join(ROOT, INDEX)
    if argv[0] == '--write':
        with open(index_path, 'w', encoding='utf-8') as f:
            f.write(render())
        print('formal-index: wrote %s' % INDEX)
        return 0
    if argv[0] == '--check':
        try:
            with open(index_path, encoding='utf-8') as f:
                current = f.read()
        except FileNotFoundError:
            current = None
        if current != render():
            print('formal-index: %s is stale — run `make formal-index`' % INDEX, file=sys.stderr)
            return 1
        print('formal-index: %s is up to date' % INDEX)
        return 0
    missing = [q for q in argv if not lookup(re.sub(r'v\d+\*?$|\*$', '', q))]
    for q in missing:
        print('formal-index: no ruling or section %s' % q, file=sys.stderr)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
