#!/usr/bin/env python3
"""Join soft-wrapped prose lines in Markdown so each paragraph/list-item/blockquote-line
is one physical line (Obsidian renders single newlines as breaks). Preserves code fences,
tables, headings, --- rules, list-item boundaries, and intentional **Label:** lines.
Usage: python3 utils/reflow_md.py FILE [FILE ...]"""
import re, sys

FENCE   = re.compile(r'^\s*(```|~~~)')
HEADING = re.compile(r'^\s*#{1,6}\s')
HR      = re.compile(r'^\s*([-*_])\1{2,}\s*$')
TABLE   = re.compile(r'^\s*\|')
LIST    = re.compile(r'^(\s*)([-*+]|\d+\.)\s+(.*)$')
QUOTE   = re.compile(r'^(\s*)>\s?(.*)$')
BOLDLBL = re.compile(r'^\*\*[^*]+:\*\*')

def reflow(text):
    out, buf, in_fence = [], None, False
    def flush():
        nonlocal buf
        if buf is None: return
        out.append(buf['prefix'] + ' '.join(p.strip() for p in buf['parts'] if p.strip() != ''))
        buf = None
    for line in text.split('\n'):
        if in_fence:
            out.append(line)
            if FENCE.match(line): in_fence = False
            continue
        if FENCE.match(line):
            flush(); out.append(line); in_fence = True; continue
        if line.strip() == '':
            flush(); out.append(line); continue
        if HEADING.match(line) or HR.match(line) or TABLE.match(line):
            flush(); out.append(line); continue
        mq = QUOTE.match(line)
        if mq:
            content = mq.group(2)
            if content.strip() == '':
                flush(); out.append('>'); continue
            if buf is not None and buf['kind'] == 'quote' and not BOLDLBL.match(content):
                buf['parts'].append(content)
            else:
                flush(); buf = {'kind':'quote','prefix':'> ','parts':[content]}
            continue
        ml = LIST.match(line)
        if ml:
            flush(); buf = {'kind':'item','prefix':ml.group(1)+ml.group(2)+' ','parts':[ml.group(3)]}
            continue
        if buf is not None and buf['kind'] in ('para','item'):
            buf['parts'].append(line)
        else:
            flush(); buf = {'kind':'para','prefix':'','parts':[line]}
    flush()
    return '\n'.join(out)

for p in sys.argv[1:]:
    orig = open(p).read(); new = reflow(orig); open(p,'w').write(new)
    print(f"{p.split('/')[-1]}: {orig.count(chr(10))} -> {new.count(chr(10))} lines")
