#!/usr/bin/env python3
"""Compile the checked-in Julia algorithms into native Java/Erlang functions.

This is a development tool. Generated sources execute directly in their host
runtime; no Julia syntax tree or foreign runtime is used by a benchmark process.
"""
import json
from pathlib import Path
import re
import subprocess
import hashlib
import sys
from collections import Counter

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'test/benchmark'
AST = ROOT / 'temp/native-ports/ast'

def sym(x):
    if isinstance(x,dict):
        if 's' in x: return x['s']
        if 'q' in x: return sym(x['q'])
        if x.get('h') == 'curly': return sym(x['a'][0])
        if x.get('h') == '::': return sym(x['a'][0])
        if x.get('h') == '.':
            left=sym(x['a'][0]);return left+'.'+sym(x['a'][1]) if left else None
    return None

def node(h,*a): return {'h':h,'a':list(a)}

def type_name(x):
    """Retain union members instead of erasing them to the constructor name."""
    if isinstance(x,dict) and x.get('h')=='curly' and sym(x['a'][0])=='Union':
        return '|'.join(type_name(member) for member in x['a'][1:])
    name=sym(x)
    if name is None: raise ValueError(('unsupported type',x))
    return name
def walk(x):
    yield x
    if isinstance(x,dict):
        for a in x.get('a',[]): yield from walk(a)

def value_node(x):
    while isinstance(x,dict) and x.get('h')=='block' and len(x['a'])==1:x=x['a'][0]
    return x

def assigned_names(body):
    """Count lexical writes; writes through an object do not rebind its owner."""
    names = Counter()
    def target(x):
        if not isinstance(x,dict): return
        if 's' in x: names[x['s']] += 1
        elif x.get('h') in ('tuple','::'):
            for child in x['a'][:1] if x['h']=='::' else x['a']: target(child)
    for x in walk(body):
        if not isinstance(x,dict): continue
        h,a=x.get('h'),x.get('a',[])
        if h in ('=','+=','-=','*=','/=','%=','>>=','÷='): target(a[0])
        elif h=='local':
            for child in a:
                if isinstance(child,dict) and 's' in child: target(child)
    return names

def immutable_prefix(body):
    """Initial, single-assignment locals can use native lexical variables."""
    statements=flatten(body)
    writes=assigned_names(body)
    prefix=[]
    for statement in statements[:-1]:
        if not isinstance(statement,dict) or statement.get('h')!='=': break
        lhs,rhs=statement['a']
        if not isinstance(lhs,dict) or 's' not in lhs or writes[lhs['s']]!=1: break
        prefix.append((lhs['s'],rhs))
    return prefix,node('block',*statements[len(prefix):])

def readonly_scalar_names(trees):
    """Conservatively identify scalar const names with no competing bindings."""
    constants=Counter();writes=Counter();shadowed=set()
    def binding(x):
        while isinstance(x,dict) and x.get('h') in ('::','kw','...'):
            x=x['a'][0]
        if isinstance(x,dict) and 's' in x:shadowed.add(x['s'])
    for tree in trees:
        writes.update(assigned_names(tree))
        for x in walk(tree):
            if not isinstance(x,dict) or 'h' not in x:continue
            h,a=x['h'],x['a']
            if h=='const' and a[0].get('h')=='=':
                target,value=a[0]['a']
                if 's' in target and (value is None or isinstance(value,(bool,int,float,str))):constants[target['s']]+=1
            if h=='function' or (h=='=' and isinstance(a[0],dict) and a[0].get('h')=='call'):
                signature=a[0]
                if signature.get('h')=='call':
                    binding(signature['a'][0])
                    for param in signature['a'][1:]:
                        if isinstance(param,dict) and param.get('h')=='parameters':
                            for keyword in param['a']:binding(keyword)
                        else:binding(param)
            if h=='->':
                params=a[0]['a'] if isinstance(a[0],dict) and a[0].get('h')=='tuple' else [a[0]]
                for param in params:binding(param)
    return {name for name,count in constants.items() if writes[name]==count and name not in shadowed}

def source_scalar_names():
    return readonly_scalar_names(json.loads(path.read_text()) for path in sorted(AST.rglob('*.json')))

DIRECT_BUILTINS=set('truth0 get0 set0! slice0 int0 ord0 chr0 range0 enumerate0 add0 mul0 format0 jget jset! jcat jor jrescue jint jord jchr jslice jstring jparse with_node length isempty abs min max sqrt sin cos zeros ones fill Int Int32 Float64 UInt32 UInt8 Char String string Symbol parse read joinpath eachindex collect first push! pop! println print'.split())

def flatten(x): return x.get('a',[]) if isinstance(x,dict) and x.get('h') in ('block','toplevel') else [x]

class Java:
    def __init__(self,readonly=()): self.seq=0; self.functions=[]; self.file=''; self.errors=[];self.aliases={};self.readonly=set(readonly)
    def constant_reads(self,body,excluded):
        used={x['s'] for x in walk(body) if isinstance(x,dict) and 's' in x}
        return sorted((used & self.readonly)-set(excluded))
    def q(self,x): return json.dumps(x,ensure_ascii=True)
    def fresh(self): self.seq+=1; return 'x'+str(self.seq)
    def expr(self,x,e='e'):
        q=self.q
        if x is None:return 'null'
        if isinstance(x,bool):return str(x).lower()
        if isinstance(x,str):return q(x)
        if isinstance(x,int):return str(x)+'L'
        if isinstance(x,float):return repr(x)
        if 's'in x:return self.aliases.get(x['s'],f'get({e},{q(x["s"])})')
        if 'q'in x:return 'new Atom('+q(sym(x))+')' if sym(x) else self.expr(x['q'],e)
        if 'c'in x:return f'new Ch({q(x["c"])})'
        h,a=x['h'],x['a']
        if h=='let' and a[0].get('h')=='=' and sym(a[0]['a'][0])=='_bool_value':
            val=self.expr(a[0]['a'][1],e);tmp=self.fresh();old=self.aliases.copy();self.aliases['_bool_value']=tmp
            result=self.expr(value_node(a[1]),e);self.aliases=old
            return f'select({val},{tmp}->'+result+')'
        if h in ('if','elseif') and len(a)==3 and all(not any(isinstance(n,dict)and n.get('h')in ('return','break','continue','while','for','function')for n in walk(branch))for branch in a[1:]):
            return '(truth('+self.expr(a[0],e)+')?'+self.expr(value_node(a[1]),e)+':'+self.expr(value_node(a[2]),e)+')'
        if h in ('block','let','if','elseif','try') or h in ('=','+=','-=','*=','/=','%=', '>>=','÷='):
            z=self.fresh();helper='eval' if h=='let'else'evalSame';return f'{helper}({e}, {z} -> {{'+self.body(x,z,True)+'})'
        if h=='::':return self.expr(a[0],e)
        if h=='.':
            if isinstance(a[1],dict)and a[1].get('h')=='tuple':return f'broadcast({e},{self.expr(a[0],e)},new Object[]{{'+','.join(self.expr(y,e)for y in a[1]['a'])+'})'
            return f'field({self.expr(a[0],e)},{q(sym(a[1]))})'
        if h=='ref':
            obj=self.expr(a[0],e)
            parts=[self.expr(y,e).replace(f'get({e},"end")',f'len({obj})')for y in a[1:]]
            return f'index({obj},new Object[]{{'+','.join(parts)+'})'
        if h in ('vect','tuple'):
            return ('arr' if h=='vect' else 'tuple')+'('+','.join(self.expr(y,e) for y in a)+')'
        if h=='curly':
            if sym(a[0])=='Union':return 'new TypeTag('+q(type_name(x))+')'
            return self.expr(a[0],e)
        if h=='comparison':
            return '('+' && '.join('truth('+self.callop(sym(a[i]),[a[i-1],a[i+1]],e)+')' for i in range(1,len(a),2))+')'
        if h in ('&&','||'):
            left=self.expr(a[0],e); right=self.expr(a[1],e)
            return ('(truth('+left+') ? '+right+' : false)') if h=='&&' else ('(truth('+left+') ? true : '+right+')')
        if h=='string':return 'strcat('+','.join(self.expr(y,e) for y in a)+')'
        if h=='...':return f'new Spread({self.expr(a[0],e)})'
        if h=='kw':return f'pair({q(sym(a[0]))},{self.expr(a[1],e)})'
        if h=='parameters':return 'keywords('+','.join(self.expr(y,e) for y in a)+')'
        if h=='->':
            args = a[0]['a'] if isinstance(a[0],dict) and a[0].get('h')=='tuple' else [a[0]]
            fn=self.makefn('',args,a[1],e)
            return fn
        if h in ('comprehension','typed_comprehension'):return self.expr(a[-1],e)
        if h=='generator':
            bindings=a[1:]; out=self.fresh();z=self.fresh()
            loop=node('for',bindings[0] if len(bindings)==1 else node('block',*bindings),node('call',{'s':'push!'}, {'s':out},a[0]))
            return f'eval({e},{z} -> {{set({z},{q(out)},arr());'+self.body(loop,z)+f'return get({z},{q(out)});'+'})'
        if h=='macrocall':
            name=sym(a[0])
            if name in ('@__DIR__',):return q(str(Path('test/benchmark/julia')/Path(self.file).parent))
            if name in ('@inline','Base.@kwdef'):return self.expr(a[-1],e)
            if name=='@sprintf':return 'format('+','.join(self.expr(y,e)for y in a[1:])+')'
            if name=='@raw_str':return self.expr(a[1],e)
            if name=='@r_str':return 'new Rx('+self.expr(a[1],e)+')'
            if name=='@assert':return f'check({self.expr(a[1],e)})'
            if name=='@printf':return f'builtin({e},"printf",new Object[]{{'+','.join(self.expr(y,e)for y in a[1:])+'})'
        if h=='call':
            f,*args=a; name=sym(f)
            # Julia syntax stores keyword parameters first; native calls bind positional arguments first.
            args.sort(key=lambda arg: isinstance(arg,dict) and arg.get("h")=="parameters")
            if name=='isa':return f'isa({e},{self.expr(args[0],e)},{q(type_name(args[1]))})'
            if name in ('+','-','*','/','÷','%','mod','fld','div','<<','>>','&','|','xor','^','==','!=','===','!==','<','>','<=','>=','!','~','=>',':','isa','.*','.+'):
                return self.callop(name,args,e)
            if name in DIRECT_BUILTINS:return f'builtin({e},{q(name)},new Object[]{{'+','.join(self.expr(y,e)for y in args)+'})'
            if name=='include':return 'null'
            if name in ('Base.length','Base.join','Base.pairs','Base.keys'):return f'builtin({e},{q(name.split(".")[-1])},new Object[]{{'+','.join(self.expr(y,e)for y in args)+'})'
            target=f'get({e},{q(name)})' if name and '.' not in name else self.expr(f,e)
            return f'call({e},{target},new Object[]{{'+','.join(self.expr(y,e)for y in args)+'})'
        if h=='do':return self.expr(node('call',a[0]['a'][0],a[1],*a[0]['a'][1:]),e)
        raise ValueError((self.file,'expression',x))
    def callop(self,name,args,e):
        return 'op('+self.q(name)+',new Object[]{'+','.join(self.expr(y,e)for y in args)+'})'
    def assign(self,t,v,e):
        if isinstance(t,dict) and t.get('h')=='::':t=t['a'][0]
        if sym(t) and 's' in t:return f'set({e},{self.q(sym(t))},{v});'
        h,a=t['h'],t['a']
        if h=='tuple':
            tmp=self.fresh();return f'Object {tmp}={v};'+''.join(self.assign(y,f'nth({tmp},{i})',e)for i,y in enumerate(a))
        if h=='ref':return f'putIndex({self.expr(a[0],e)},new Object[]{{'+','.join(self.expr(y,e)for y in a[1:])+f'}},{v});'
        if h=='.':return f'putField({self.expr(a[0],e)},{self.q(sym(a[1]))},{v});'
        raise ValueError(('assignment',t))
    def body(self,x,e='e',last=False):
        if not isinstance(x,dict) or 'h' not in x:
            v=self.expr(x,e);return 'return '+v+';' if last else f'discard({v});'
        h,a=x['h'],x['a']
        if h=='let':
            bindings=flatten(a[0]);names=[sym(y['a'][0])for y in bindings if isinstance(y,dict)and y.get('h')=='='and's'in y['a'][0]]
            return ''.join(f'bind({e},{self.q(n)},null);'for n in names)+''.join(self.body(y,e,last and i==len(a)-1)for i,y in enumerate(a))
        if h in ('block','toplevel'):
            # let bindings live in a child scope; generated temporary names are unique.
            return ''.join(self.body(y,e,last and i==len(a)-1) for i,y in enumerate(a)) or ('return null;' if last else '')
        if h in ('if','elseif'):
            return f'if(truth({self.expr(a[0],e)})){{'+self.body(a[1],e,last)+'}else{'+(self.body(a[2],e,last)if len(a)>2 else ('return null;'if last else ''))+'}'
        if h=='return':return 'if(always())throw new Ret('+ (self.expr(a[0],e)if a else 'null')+');'+('return null;' if last else '')
        if h=='break':return 'break;'
        if h=='continue':return 'continue;'
        if h in ('&&','||') and any(isinstance(y,dict) and y.get('h') in ('return','break','continue') for y in walk(a[1])):
            return f'if({"!" if h=="||"else ""}truth({self.expr(a[0],e)})){{'+self.body(a[1],e)+'}'+('return null;'if last else '')
        if h=='while':return f'while(truth({self.expr(a[0],e)})){{'+self.body(a[1],e)+'}'+('return null;'if last else '')
        if h=='for':
            binds=flatten(a[0]); b=a[1]
            code=self.body(b,e)
            for binding in reversed(binds):
                if binding.get('h')=='filter':
                    cond,*bs=binding['a']; code=f'if(truth({self.expr(cond,e)})){{'+code+'}'; binding=bs[0]
                target,values=binding['a'];tmp=self.fresh()
                code=f'for(Object {tmp}: iterable({self.expr(values,e)})){{'+self.assign(target,tmp,e)+code+'}'
            return code+('return null;'if last else '')
        if h=='local':return ''.join(self.body(y,e) if isinstance(y,dict)and y.get('h')=='=' else f'bind({e},{self.q(sym(y))},null);'for y in a)+('return null;'if last else '')
        if h=='function' or (h=='=' and isinstance(a[0],dict)and a[0].get('h')=='call'):
            sig,b=a;name=sym(sig['a'][0]); args=sig['a'][1:]
            return f'define({e},{self.q(name)},{self.makefn(name,args,b,e)});'+('return null;'if last else '')
        if h in ('=','+=','-=','*=','/=','>>=','%=','÷=','.*='):
            target,value=a
            if h!='=':value=node('call',{'s':h[:-1]},target,value)
            return self.assign(target,self.expr(value,e),e)+('return '+self.expr(target,e)+';'if last else '')
        if h=='try':
            return 'try{'+self.body(a[0],e,last)+'}catch(Ret r){throw r;}catch(RuntimeException ex){'+self.body(a[2],e,last)+'}'
        if h=='const':return self.body(a[0],e,last)
        if h=='macrocall' and sym(a[0]) in ('@inline','Base.@kwdef'):
            return self.body(a[-1],e,last)
        if h in ('abstract','struct','using','module'):return 'return null;' if last else ''
        value=self.expr(x,e)
        return 'return '+value+';' if last else 'discard('+value+');'
    def makefn(self,name,args,b,capture):
        ident='fn'+self.fresh();params=[]; defaults=[]; types=[];vararg=None;kwargs=[]
        for arg in args:
            if isinstance(arg,dict)and arg.get('h')=='parameters':kwargs=arg['a'];continue
            if isinstance(arg,dict)and arg.get('h')=='...':vararg=sym(arg['a'][0]);continue
            default=None
            if isinstance(arg,dict)and arg.get('h')=='kw':arg,default=arg['a']
            typ='Any'
            if isinstance(arg,dict)and arg.get('h')=='::':arg,t=arg['a'];typ=sym(t) or 'Any'
            params.append(sym(arg)); defaults.append(default); types.append(typ)
        outer_aliases=self.aliases;self.aliases={};writes=assigned_names(b)
        setup=''
        def bind_arg(name,value):
            if writes[name]: return f'bind(e,{self.q(name)},{value});'
            local=self.fresh();self.aliases[name]=local
            return f'final Object {local}={value};bind(e,{self.q(name)},{local});'
        for i,(p,d)in enumerate(zip(params,defaults)):
            setup+=bind_arg(p,f'a.length>{i}?a[{i}]:'+ (self.expr(d)if d is not None else 'null'))
        if vararg: setup+=bind_arg(vararg,f'arrSlice(a,{len(params)})')
        for kw in kwargs:
            target,default=kw['a'] if kw.get('h')=='kw' else(kw,None)
            setup+=bind_arg(sym(target),f'kwarg(a,{self.q(sym(target))},()->'+self.expr(default)+')')
        prefix,remaining=immutable_prefix(b);code=''
        # Resolve proven immutable scalar bindings once, outside repeated dispatch.
        for name in self.constant_reads(b,set(params)|set(writes)|set(self.aliases)):
            local=self.fresh();code+=f'final Object {local}=get(e,{self.q(name)});';self.aliases[name]=local
        for name,rhs in prefix:
            local=self.fresh();value=self.expr(rhs);self.aliases[name]=local
            code+=f'final Object {local}={value};set(e,{self.q(name)},{local});'
        code+=self.body(remaining,last=True)
        self.aliases=outer_aliases
        self.functions.append(f'static Object {ident}(Env e,Object[] a){{{setup} try{{{code}}}catch(Ret r){{return r.value;}}}}')
        minimum=sum(d is None for d in defaults)
        return f'new Fn({capture},Ports::{ident},new String[]{{'+','.join(self.q(t)for t in types)+f'}},{minimum},{-1 if vararg else len(params)})'

    def filecode(self,path):
        self.file=path
        x=json.loads((AST/(path+'.json')).read_text()); code=[]
        def install(x,e='e'):
            if not isinstance(x,dict) or 'h'not in x:return self.body(x,e)
            h,a=x['h'],x['a']
            if h in ('toplevel','block'):return ''.join(install(y,e)for y in a)
            if h=='call' and sym(a[0])=='include':
                p=str((Path(path).parent/a[1]))
                import posixpath
                return f'load({e},{self.q(posixpath.normpath(p))});'
            if h=='module':
                mod=sym(a[1]); return f'{{Env module=new Env({e});set({e},{self.q(mod)},module);'+install(a[2],'module')+'}'
            if h=='macrocall'and sym(a[0])in('Base.@kwdef','@inline'):return install(a[-1],e)
            if h=='abstract':
                decl=a[0];n=sym(decl['a'][0])if decl.get('h')=='<:'else sym(decl)
                parent=sym(decl['a'][1])if decl.get('h')=='<:'else 'Any'
                return f'defType({e},{self.q(n)},{self.q(parent)},new String[]{{}},new Object[]{{}},true);'
            if h=='struct':
                _,decl,fields=a; n=sym(decl['a'][0])if decl.get('h')=='<:'else sym(decl);parent=sym(decl['a'][1])if decl.get('h')=='<:'else 'Any'
                names=[];vals=[]
                for f in flatten(fields):
                    default=None
                    if isinstance(f,dict)and f.get('h')=='=':f,default=f['a']
                    if isinstance(f,dict)and 's' in f:names.append(sym(f));vals.append(self.expr(default,e)if default is not None else 'null')
                    if isinstance(f,dict)and f.get('h')=='::':
                        names.append(sym(f['a'][0]));t=sym(f['a'][1]); vals.append(self.expr(default,e)if default is not None else ('false'if t=='Bool'else '0.0'if t=='Float64'else '0L'if t in('Int','Int32','UInt32')else 'null'))
                return f'defType({e},{self.q(n)},{self.q(parent)},new String[]{{'+','.join(self.q(n)for n in names)+'},new Object[]{'+','.join(vals)+'},false);'
            # entry main calls execute only in the selected source, after includes finish.
            if h=='function' and sym(a[0]['a'][0]) in ('run_benchmark','checked_benchmark'):return ''
            return self.body(x,e)
        return install(x)

def main():
    subprocess.run(['julia','--startup-file=no',str(Path(__file__).with_name('parse_julia.jl')),str(AST)],check=True,cwd=ROOT)
    j=Java(source_scalar_names()); loads=[]
    skip={'class_support.jl','text/jq_values.jl','text/support.jl','jetstream/support.jl'}
    for p in sorted(AST.rglob('*.json')):
        rel=str(p.relative_to(AST))[:-5]
        if rel in skip or rel=='test_jq_vm.jl':continue
        try:code=j.filecode(rel)
        except Exception as ex: raise RuntimeError(rel)from ex
        init='load'+j.fresh();j.functions.append(f'static void {init}(Env e){{'+code+'}');loads.append('case '+j.q(rel)+':'+init+'(e);break;')
    source='// generated by native_ports/generate.py. Source notices: ../native_ports/LICENSE.md.\nimport java.util.*;\nclass Ports extends PortRuntime {\nstatic void install(Env e,String file){switch(file){'+''.join(loads)+'default: adapters(e,file);}}\n'+ '\n'.join(j.functions)+'\n}\n'
    dest=BASE/'java';dest.mkdir(exist_ok=True);(dest/'Ports.java').write_text(re.sub(r'"(?:[^"\\]|\\.)*"|;',lambda m:m[0]+'\n' if m[0]==';' else m[0],source))

if __name__=='__main__':
    main()
    from erlang_codegen import generate
    generate()
    sys.path.insert(0,str(BASE))
    import run_julia_benchmarks as registry
    entries={f"{e['suite']}/{e['name']}":f"julia/{e['suite']}/{e['name']}.jl" for e in registry.benchmark_entries(True)}
    sources={str(p.relative_to(BASE)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((BASE/'julia').rglob('*.jl')) if p.name!='test_jq_vm.jl'}
    (BASE/'native_ports/manifest.json').write_text(json.dumps({'entries':entries,'algorithm_sources_sha256':sources},indent=2)+'\n')
