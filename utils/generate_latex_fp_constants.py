#!/usr/bin/env python3
"""Generate expl3 fixed-point power constants from the bundled source."""
from pathlib import Path
import re
import sys

root=next(parent for parent in Path(__file__).resolve().parents if (parent/'lmd/package/doc/latex/tex/expl3-code.tex').is_file())
source=(root/'lmd/package/doc/latex/tex/expl3-code.tex').read_text()
names={'i':1,'ii':2,'iii':3,'iv':4,'vi':6,'vii':7,'viii':8,'ix':9,'x':10}
constants={}
for name,value in re.findall(r'\\tl_const:Nn \\c__fp_ln_([ivx]+)_fixed_tl\s*\{\s*((?:\{\d+\}){6})',source):
    constants[names[name]]=[int(x) for x in re.findall(r'\{(\d+)\}',value)]
if set(constants)!=set(names.values()):
    raise SystemExit('incomplete expl3 logarithm constants')
text=re.search(r'\\intarray_const_from_clist:Nn \\c__fp_exp_intarray\s*\{(.*?)\n  \}',source,re.S).group(1)
raw=[int(re.sub(r'\s+','',part)) for part in text.split(',') if part.strip()]
if len(raw)!=6*9*4:
    raise SystemExit('unexpected expl3 exponential table length')
table=[]
for position in range(6):
    for digit in range(1,10):
        exponent,*words=raw[(position*9+digit-1)*4:(position*9+digit)*4]
        digits=''.join(str(word)[1:] for word in words)
        if len(digits)!=24:
            raise SystemExit('unexpected expl3 exponential mantissa')
        table.append((position,digit,exponent,[int(digits[i:i+4]) for i in range(0,24,4)]))
def limbs(values):
    return '['+','.join(str(x) for x in values)+']'
lines=['// Generated from bundled expl3 constants; run utils/generate_latex_fp_constants.py.',
       'pub let logarithms = [']
items=sorted(constants.items())
lines += ['    {multiplier:'+str(k)+',limbs:'+limbs(v)+'}'+(',' if i+1<len(items) else '') for i,(k,v) in enumerate(items)]
lines += [']','pub let exponentials = [']
lines += ['    {position:'+str(p)+',digit:'+str(d)+',exponent:'+str(e)+',limbs:'+limbs(v)+'}'+(',' if i+1<len(table) else '') for i,(p,d,e,v) in enumerate(table)]
lines += [']']
result='\n'.join(lines)+'\n'
destination=root/'lmd/package/doc/latex/tex_engine/fp_constants.ls'
if sys.argv[1:]==['--check']:
    if not destination.exists() or destination.read_text()!=result:
        raise SystemExit('expl3 fixed-point constants are stale')
elif not sys.argv[1:]:
    destination.write_text(result)
else:
    raise SystemExit('usage: generate_latex_fp_constants.py [--check]')
