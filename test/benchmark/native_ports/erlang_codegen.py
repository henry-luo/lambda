"""Erlang native-function backend for generate.py's syntax extraction."""
import json
from generate import Java, sym, node, walk, flatten, AST, BASE, value_node, DIRECT_BUILTINS, assigned_names, immutable_prefix, type_name, source_scalar_names
from pathlib import Path
import posixpath

class Erlang(Java):
    def q(self,x): return '<<'+json.dumps(x,ensure_ascii=False)+'/utf8>>'
    def fresh(self): self.seq+=1;return 'X'+str(self.seq)
    def expr(self,x,e='E'):
        q=self.q
        if x is None:return 'nil'
        if isinstance(x,bool):return str(x).lower()
        if isinstance(x,str):return q(x)
        if isinstance(x,int):return repr(x)
        if isinstance(x,float):
            v=repr(x);return v.replace('e','.0e')if 'e'in v and '.'not in v.split('e')[0]else v
        if 's'in x:return self.aliases.get(x['s'],f'pr:getv({e},{q(x["s"])})')
        if 'q'in x:return '{atom,'+q(sym(x))+'}' if sym(x) else self.expr(x['q'],e)
        if 'c'in x:return '{char,'+q(x['c'])+'}'
        h,a=x['h'],x['a']
        if h=='let' and a[0].get('h')=='=' and sym(a[0]['a'][0])=='_bool_value':
            val=self.expr(a[0]['a'][1],e);tmp=self.fresh();old=self.aliases.copy();self.aliases['_bool_value']=tmp
            result=self.expr(value_node(a[1]),e);self.aliases=old
            return f'(fun({tmp})->'+result+' end)('+val+')'
        if h in ('if','elseif') and len(a)==3 and all(not any(isinstance(n,dict)and n.get('h')in ('return','break','continue','while','for','function')for n in walk(branch))for branch in a[1:]):
            return 'case pr:truth('+self.expr(a[0],e)+')of true->'+self.expr(value_node(a[1]),e)+';false->'+self.expr(value_node(a[2]),e)+' end'
        if h in ('block','let','if','elseif','try','=','+=','-=','*=','/=','%=', '>>=','÷='):
            z=self.fresh();helper='eval'if h=='let'else'eval_same';return f'pr:{helper}({e},fun({z})->'+self.body(x,z,True)+' end)'
        if h=='::':return self.expr(a[0],e)
        if h=='.':
            if isinstance(a[1],dict)and a[1].get('h')=='tuple':return f'pr:broadcast({e},{self.expr(a[0],e)},['+','.join(self.expr(y,e)for y in a[1]['a'])+'])'
            return f'pr:field({self.expr(a[0],e)},{q(sym(a[1]))})'
        if h=='ref':
            obj=self.expr(a[0],e);parts=[self.expr(y,e).replace(f'pr:getv({e},{q("end")})',f'pr:len({obj})')for y in a[1:]]
            return f'pr:index({obj},['+','.join(parts)+'])'
        if h in ('vect','tuple'):return ('pr:arr'if h=='vect'else'pr:tup')+'(['+','.join(self.expr(y,e)for y in a)+'])'
        if h=='curly':return '{type,'+q(type_name(x))+'}' if sym(a[0])=='Union'else self.expr(a[0],e)
        if h=='comparison':return '('+' andalso '.join('pr:truth('+self.callop(sym(a[i]),[a[i-1],a[i+1]],e)+')'for i in range(1,len(a),2))+')'
        if h in ('&&','||'):
            return f'case pr:truth({self.expr(a[0],e)}) of true -> '+(self.expr(a[1],e)if h=='&&'else'true')+'; false -> '+('false'if h=='&&'else self.expr(a[1],e))+' end'
        if h=='string':return 'pr:strcat(['+','.join(self.expr(y,e)for y in a)+'])'
        if h=='...':return '{spread,'+self.expr(a[0],e)+'}'
        if h=='kw':return 'pr:tup(['+q(sym(a[0]))+','+self.expr(a[1],e)+'])'
        if h=='parameters':return 'pr:keywords(['+','.join(self.expr(y,e)for y in a)+'])'
        if h=='->':
            args=a[0]['a']if isinstance(a[0],dict)and a[0].get('h')=='tuple'else[a[0]]
            return self.makefn('',args,a[1],e)
        if h in ('comprehension','typed_comprehension'):return self.expr(a[-1],e)
        if h=='generator':
            bindings=a[1:];out=self.fresh();z=self.fresh()
            loop=node('for',bindings[0]if len(bindings)==1 else node('block',*bindings),node('call',{'s':'push!'},{'s':out},a[0]))
            return f'pr:eval({e},fun({z})->pr:setv({z},{q(out)},pr:arr([])), '+self.body(loop,z)+f',pr:getv({z},{q(out)}) end)'
        if h=='macrocall':
            n=sym(a[0])
            if n=='@__DIR__':return q(str(Path('test/benchmark/julia')/Path(self.file).parent))
            if n in ('@inline','Base.@kwdef'):return self.expr(a[-1],e)
            if n=='@raw_str':return self.expr(a[1],e)
            if n=='@r_str':return '{regex,'+self.expr(a[1],e)+'}'
            if n=='@assert':return 'pr:check('+self.expr(a[1],e)+')'
            if n in ('@sprintf','@printf'):return f'pr:builtin({e},{q("format"if n=="@sprintf"else"printf")},['+','.join(self.expr(y,e)for y in a[1:])+'])'
        if h=='call':
            f,*args=a;name=sym(f)
            # Julia syntax stores keyword parameters first; native calls bind positional arguments first.
            args.sort(key=lambda arg: isinstance(arg,dict) and arg.get("h")=="parameters")
            if name=='isa':return f'pr:isa({e},{self.expr(args[0],e)},{q(type_name(args[1]))})'
            if name in ('+','-','*','/','÷','%','mod','fld','div','<<','>>','&','|','xor','^','==','!=','===','!==','<','>','<=','>=','!','~','=>',':','.*','.+'):return self.callop(name,args,e)
            if name in DIRECT_BUILTINS:return f'pr:builtin({e},{q(name)},['+','.join(self.expr(y,e)for y in args)+'])'
            if name=='include':return'nil'
            if name in ('Base.length','Base.join','Base.pairs','Base.keys'):return f'pr:builtin({e},{q(name.split(".")[-1])},['+','.join(self.expr(y,e)for y in args)+'])'
            target=f'pr:getv({e},{q(name)})'if name and'.'not in name else self.expr(f,e)
            return f'pr:call({e},{target},['+','.join(self.expr(y,e)for y in args)+'])'
        if h=='do':return self.expr(node('call',a[0]['a'][0],a[1],*a[0]['a'][1:]),e)
        raise ValueError((self.file,'expression',x))
    def callop(self,n,args,e):return 'pr:op('+self.q(n)+',['+','.join(self.expr(y,e)for y in args)+'])'
    def assign(self,t,v,e):
        if isinstance(t,dict)and t.get('h')=='::':t=t['a'][0]
        if 's'in t:return f'pr:setv({e},{self.q(sym(t))},{v})'
        h,a=t['h'],t['a']
        if h=='tuple':
            tmp=self.fresh();return f'{tmp}={v},'+','.join(self.assign(y,f'pr:nth({tmp},{i})',e)for i,y in enumerate(a))
        if h=='ref':return f'pr:putindex({self.expr(a[0],e)},['+','.join(self.expr(y,e)for y in a[1:])+f'],{v})'
        if h=='.':return f'pr:putfield({self.expr(a[0],e)},{self.q(sym(a[1]))},{v})'
        raise ValueError(t)
    def body(self,x,e='E',last=False):
        if not isinstance(x,dict)or'h'not in x:return self.expr(x,e)
        h,a=x['h'],x['a']
        if h=='let':
            bindings=flatten(a[0]);names=[sym(y['a'][0])for y in bindings if isinstance(y,dict)and y.get('h')=='='and's'in y['a'][0]]
            return ','.join([f'pr:bind({e},{self.q(n)},nil)'for n in names]+[self.body(y,e,last and i==len(a)-1)for i,y in enumerate(a)])
        if h in ('block','toplevel'):return ',\n'.join(self.body(y,e,last and i==len(a)-1)for i,y in enumerate(a))or'nil'
        if h in ('if','elseif'):return 'case pr:truth('+self.expr(a[0],e)+') of true -> '+self.body(a[1],e,last)+'; false -> '+(self.body(a[2],e,last)if len(a)>2 else'nil')+' end'
        if h=='return':return 'throw({return,'+(self.expr(a[0],e)if a else'nil')+'})'
        if h=='break':return 'throw(break_loop)'
        if h=='continue':return 'throw(continue_loop)'
        if h in ('&&','||')and any(isinstance(y,dict)and y.get('h')in('return','break','continue')for y in walk(a[1])):
            return 'case pr:truth('+self.expr(a[0],e)+') of '+('false'if h=='||'else'true')+' -> '+self.body(a[1],e)+'; _ -> nil end'
        if h=='while':return 'pr:while('+e+',fun()->'+self.expr(a[0],e)+' end,fun()->'+self.body(a[1],e)+' end)'
        if h=='for':
            code=self.body(a[1],e)
            for binding in reversed(flatten(a[0])):
                if binding.get('h')=='filter':
                    cond,*bs=binding['a'];code='case pr:truth('+self.expr(cond,e)+') of true -> '+code+'; false -> nil end';binding=bs[0]
                target,values=binding['a'];tmp=self.fresh()
                code='pr:foreach('+e+','+self.expr(values,e)+',fun('+tmp+')->'+self.assign(target,tmp,e)+','+code+' end)'
            return code
        if h=='local':return ','.join(self.body(y,e)if isinstance(y,dict)and y.get('h')=='='else f'pr:bind({e},{self.q(sym(y))},nil)'for y in a)or'nil'
        if h=='function'or(h=='='and isinstance(a[0],dict)and a[0].get('h')=='call'):
            sig,b=a;name=sym(sig['a'][0]);return f'pr:define({e},{self.q(name)},{self.makefn(name,sig["a"][1:],b,e)})'
        if h in ('=','+=','-=','*=','/=','>>=','%=','÷=','.*='):
            target,value=a
            if h!='=':value=node('call',{'s':h[:-1]},target,value)
            return self.assign(target,self.expr(value,e),e)
        if h=='try':return 'try '+self.body(a[0],e,last)+' catch throw:{return,_}=R -> throw(R); _:_ -> '+self.body(a[2],e,last)+' end'
        if h=='const':return self.body(a[0],e,last)
        if h=='macrocall'and sym(a[0])in('@inline','Base.@kwdef'):return self.body(a[-1],e,last)
        if h in ('abstract','struct','using','module'):return'nil'
        return self.expr(x,e)
    def makefn(self,name,args,b,capture):
        ident='fn'+self.fresh().lower();params=[];defaults=[];types=[];vararg=None;kwargs=[]
        for arg in args:
            if isinstance(arg,dict)and arg.get('h')=='parameters':kwargs=arg['a'];continue
            if isinstance(arg,dict)and arg.get('h')=='...':vararg=sym(arg['a'][0]);continue
            default=None
            if isinstance(arg,dict)and arg.get('h')=='kw':arg,default=arg['a']
            typ='Any'
            if isinstance(arg,dict)and arg.get('h')=='::':arg,t=arg['a'];typ=sym(t)or'Any'
            params.append(sym(arg));defaults.append(default);types.append(typ)
        outer_aliases=self.aliases;self.aliases={};writes=assigned_names(b);setup=[]
        def bind_arg(name,value):
            if writes[name]: return f'pr:bind(E,{self.q(name)},{value})'
            local=self.fresh();self.aliases[name]=local
            return f'{local} = {value},pr:bind(E,{self.q(name)},{local})'
        for i,(p,d)in enumerate(zip(params,defaults)):setup.append(bind_arg(p,f'pr:arg(A,{i},fun()->'+self.expr(d)+' end)'))
        if vararg:setup.append(bind_arg(vararg,f'pr:arr(lists:nthtail({len(params)},A))'))
        for kw in kwargs:
            target,default=kw['a']if kw.get('h')=='kw'else(kw,None)
            setup.append(bind_arg(sym(target),f'pr:kwarg(A,{self.q(sym(target))},fun()->'+self.expr(default)+' end)'))
        prefix,remaining=immutable_prefix(b);code=[]
        for name in self.constant_reads(b,set(params)|set(writes)|set(self.aliases)):
            local=self.fresh();code.append(f'{local} = pr:getv(E,{self.q(name)})');self.aliases[name]=local
        for name,rhs in prefix:
            local=self.fresh();value=self.expr(rhs);self.aliases[name]=local
            code.append(f'{local} = {value},pr:setv(E,{self.q(name)},{local})')
        code.append(self.body(remaining,last=True))
        self.functions.append(ident+'(E,A)->'+','.join(setup+['try '+','.join(code)+' catch throw:{return,V}->V end'])+'.')
        self.aliases=outer_aliases
        minimum=sum(d is None for d in defaults)
        return '{fn,'+capture+',fun ?MODULE:'+ident+'/2,['+','.join(self.q(t)for t in types)+f'],{minimum},{-1 if vararg else len(params)}'+'}'
    def filecode(self,path):
        self.file=path;x=json.loads((AST/(path+'.json')).read_text())
        def install(x,e='E'):
            if not isinstance(x,dict)or'h'not in x:return self.body(x,e)
            h,a=x['h'],x['a']
            if h in ('toplevel','block'):return ',\n'.join(install(y,e)for y in a)or'nil'
            if h=='call'and sym(a[0])=='include':return f'pr:load({e},{self.q(posixpath.normpath(str(Path(path).parent/a[1])))})'
            if h=='module':
                mod=self.fresh();return f'{mod}=pr:env({e}),pr:bind({e},{self.q(sym(a[1]))},{mod}),'+install(a[2],mod)
            if h=='macrocall'and sym(a[0])in('Base.@kwdef','@inline'):return install(a[-1],e)
            if h in ('struct','abstract'):
                decl=a[1]if h=='struct'else a[0];n=sym(decl['a'][0])if decl.get('h')=='<:'else sym(decl);parent=sym(decl['a'][1])if decl.get('h')=='<:'else'Any';names=[];vals=[]
                if h=='struct':
                    for f in flatten(a[2]):
                        default=None
                        if isinstance(f,dict)and f.get('h')=='=':f,default=f['a']
                        if isinstance(f,dict)and's'in f:names.append(sym(f));vals.append(self.expr(default,e)if default is not None else'nil')
                        if isinstance(f,dict)and f.get('h')=='::':
                            names.append(sym(f['a'][0]));t=sym(f['a'][1]);vals.append(self.expr(default,e)if default is not None else'false'if t=='Bool'else'0.0'if t=='Float64'else'0'if t in('Int','Int32','UInt32')else'nil')
                return f'pr:deftype({e},{self.q(n)},{self.q(parent)},['+','.join(self.q(n)for n in names)+'],['+','.join(vals)+'])'
            if h=='function'and sym(a[0]['a'][0])in('run_benchmark','checked_benchmark'):return'nil'
            return self.body(x,e)
        return install(x)

def generate():
    j=Erlang(source_scalar_names());loads=[];skip={'class_support.jl','text/jq_values.jl','text/support.jl','jetstream/support.jl','test_jq_vm.jl'}
    for p in sorted(AST.rglob('*.json')):
        rel=str(p.relative_to(AST))[:-5]
        if rel in skip:continue
        try:code=j.filecode(rel)
        except Exception as ex:raise RuntimeError(rel)from ex
        loads.append('install(E,'+j.q(rel)+')->'+code+';')
    # Binary literals are legal patterns; function calls are not.
    import re
    header='%% Generated native functions. Source notices: ../native_ports/LICENSE.md.\n-module(ports).\n-compile(export_all).\n-compile(nowarn_export_all).\n'
    source=header+'\n'.join(loads)+'\ninstall(E,F)->pr:adapters(E,F).\n'+'\n'.join(j.functions)+'\n'
    source=re.sub(r'install\(E,unicode:characters_to_binary\(("(?:[^"\\]|\\.)*")\)\)',lambda m:'install(E,<<'+m[1]+'/utf8>>)',source)
    dest=BASE/'erlang';dest.mkdir(exist_ok=True);(dest/'ports.erl').write_text(source)

if __name__=='__main__':generate()
