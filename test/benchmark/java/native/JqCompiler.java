import java.util.ArrayList;
import static java.util.Arrays.asList;

/** Resolve jq lexical names into frame slots and native bytecode. */
final class JqCompiler {
    static final String[] NAMES={"length","not","type","keys","keys_unsorted","has","contains","tostring","tojson","fromjson","tonumber","ascii_upcase","ascii_downcase","explode","implode","split","join","add","flatten","floor","ceil","min","max","unique","sort","reverse","getpath","setpath","delpaths","from_entries","_sort_by_impl","_group_by_impl","error","error"};
    static final int[] ARITIES={0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,2,1,0,1,1,0,1};
    static final class Fn {int entry,nparams,nlocals;Fn(int p){nparams=p;}}
    record Program(int[] code,Object[] constants,Fn[] functions){}
    record Scope(int kind,String name,int arity,int index,Scope previous){}
    static final class Context {final Context parent;final int level;final Fn fn;Scope scope;int locals;Context(Context p,Fn f){parent=p;level=p==null?0:p.level+1;fn=f;}}
    final ArrayList<Integer> code=new ArrayList<>();final ArrayList<Object> constants=new ArrayList<>();final ArrayList<Fn> functions=new ArrayList<>();int synth;
    int emit(int... values){int at=code.size();for(int v:values)code.add(v);return at;}
    int constant(Object v){constants.add(v);return constants.size()-1;}
    void patch(int at){code.set(at,code.size());}
    int function(int n){functions.add(new Fn(n));return functions.size()-1;}
    void push(Context c,int kind,String name,int arity,int index){c.scope=new Scope(kind,name,arity,index,c.scope);}
    int[] variable(Context c,String name){for(Context x=c;x!=null;x=x.parent)for(Scope s=x.scope;s!=null;s=s.previous)if(s.kind==1&&s.name.equals(name))return new int[]{c.level-x.level,s.index};throw new IllegalArgumentException("undefined jq variable $"+name);}
    void sub(Context c,JqParser.Node n){emit(6);compile(c,n,false);emit(7);}
    int closure(Context c,JqParser.Node body){emit(13);int jump=emit(0),fn=function(0);Context inner=new Context(c,functions.get(fn));inner.fn.entry=code.size();compile(inner,body,true);emit(32);inner.fn.nlocals=inner.locals;patch(jump);return fn;}
    void call(Context c,JqParser.Node n,boolean tail){String name=n.name;int arity=n.list.size();
        for(Context x=c;x!=null;x=x.parent)for(Scope s=x.scope;s!=null;s=s.previous)if(s.name.equals(name)){
            if(s.kind==3&&arity==0){emit(tail?31:30,c.level-x.level,s.index);return;}
            if(s.kind==2&&s.arity==arity){int[] closures=new int[arity];for(int i=0;i<arity;i++)closures[i]=closure(c,n.list.get(i));emit(tail?29:28,s.index,c.level-x.level,arity);emit(closures);return;}}
        if(arity==0&&name.equals("empty")){emit(17);return;}
        if(arity==0&&(name.equals("true")||name.equals("false")||name.equals("null"))){emit(3,constant(name.equals("true")?Boolean.TRUE:name.equals("false")?Boolean.FALSE:null));return;}
        if(arity==1&&name.equals("path")){emit(25);compile(c,n.list.get(0),false);emit(26);return;}
        if((arity==2||arity==3)&&name.equals("range")){for(JqParser.Node arg:n.list)sub(c,arg);emit(24,arity);return;}
        int id=0;for(int i=0;i<NAMES.length;i++)if(NAMES[i].equals(name)&&ARITIES[i]==arity){id=i+1;break;}if(id==0)throw new IllegalArgumentException("undefined jq function "+name+"/"+arity);
        for(JqParser.Node arg:n.list)sub(c,arg);emit(27,id,arity);
    }
    void definition(Context c,JqParser.Node n,boolean tail){int count=n.list.size(),index=function(count);push(c,2,n.name,count,index);emit(13);int jump=emit(0);Context inner=new Context(c,functions.get(index));inner.fn.entry=code.size();
        for(int i=0;i<count;i++)push(inner,3,n.list.get(i).name,0,i);JqParser.Node body=n.a;
        for(int i=count-1;i>=0;i--)if(n.pvar.get(i))body=JqParser.named(21,n.list.get(i).name,JqParser.call(n.list.get(i).name),body);
        compile(inner,body,true);emit(32);inner.fn.nlocals=inner.locals;patch(jump);compile(c,n.b,tail);
    }
    void compile(Context c,JqParser.Node n,boolean tail){switch(n.kind){
        case 1:return;case 2:call(c,JqParser.call("recurse"),tail);return;
        case 3:emit(3,constant(n.value));return;
        case 27:{int[] v=variable(c,n.name);emit(20,v[0],v[1]);return;}
        case 4:if(n.b.kind==3){compile(c,n.a,false);emit(9,constant(n.b.value));}else{sub(c,n.b);compile(c,n.a,false);emit(8);}return;
        case 5:compile(c,n.a,false);emit(10);return;
        case 6:sub(c,n.b==null?JqParser.lit(null):n.b);sub(c,n.c==null?JqParser.lit(null):n.c);compile(c,n.a,false);emit(11);return;
        case 7:{emit(33);int handler=emit(0);compile(c,n.a,false);emit(34,13);int end=emit(0);patch(handler);if(n.b!=null)compile(c,n.b,tail);else emit(17);patch(end);return;}
        case 8:{if(n.a==null){emit(4);return;}int slot=c.locals++;emit(1,4,18,0,slot,12);int done=emit(0);compile(c,n.a,false);emit(22,0,slot,17);patch(done);emit(21,0,slot);return;}
        case 9:emit(5);for(int i=0;i<n.list.size();i+=2){sub(c,n.list.get(i));sub(c,n.list.get(i+1));emit(23);}emit(2);return;
        case 10:compile(c,n.a,false);emit(38);return;
        case 11:sub(c,n.b);sub(c,n.a);emit(37,n.op);return;
        case 12:{sub(c,n.a);emit(15);int no=emit(0);compile(c,n.b,false);emit(39,13);int end=emit(0);patch(no);emit(3,constant(false));patch(end);return;}
        case 13:{sub(c,n.a);emit(15);int other=emit(0);emit(3,constant(true),13);int end=emit(0);patch(other);compile(c,n.b,false);emit(39);patch(end);return;}
        case 14:{int found=c.locals++;emit(1,3,constant(false),18,0,found,12);int other=emit(0);emit(33);int skip=emit(0);compile(c,n.a,false);emit(34,16);int skip2=emit(0);emit(1,3,constant(true),18,0,found,13);int end=emit(0);patch(skip);patch(skip2);emit(17);patch(other);emit(1,20,0,found,14);int run=emit(0);emit(17);patch(run);compile(c,n.b,tail);patch(end);return;}
        case 15:case 16:call(c,JqParser.call(n.kind==15?"_assign":"_modify",n.a,n.b),tail);return;
        case 17:case 18:{String name="__rhs"+(++synth);JqParser.Node v=JqParser.named(27,name,null,null),update=n.kind==17?JqParser.bin(n.op,JqParser.mk(1,null,null),v):JqParser.mk(14,JqParser.mk(1,null,null),v);
            compile(c,JqParser.named(21,name,n.b,JqParser.call("_modify",n.a,update)),tail);return;}
        case 19:compile(c,n.a,false);compile(c,n.b,tail);return;
        case 20:{emit(12);int second=emit(0);compile(c,n.a,tail);emit(13);int end=emit(0);patch(second);compile(c,n.b,tail);patch(end);return;}
        case 21:{int slot=c.locals++;sub(c,n.a);emit(19,0,slot);Scope saved=c.scope;push(c,1,n.name,0,slot);compile(c,n.b,tail);c.scope=saved;return;}
        case 22:{int acc=c.locals++,x=c.locals++;emit(1);compile(c,n.b,false);emit(18,0,acc,12);int end=emit(0);emit(1);compile(c,n.a,false);emit(18,0,x,21,0,acc);Scope saved=c.scope;push(c,1,n.name,0,x);compile(c,n.c,false);c.scope=saved;emit(18,0,acc,17);patch(end);emit(21,0,acc);return;}
        case 23:{int acc=c.locals++,x=c.locals++;emit(1);compile(c,n.b,false);emit(18,0,acc,1);compile(c,n.a,false);emit(18,0,x,20,0,acc);Scope saved=c.scope;push(c,1,n.name,0,x);compile(c,n.c,false);emit(1,18,0,acc);if(n.d!=null)compile(c,n.d,tail);c.scope=saved;return;}
        case 24:{sub(c,n.a);emit(15);int other=emit(0);compile(c,n.b,tail);emit(13);int end=emit(0);patch(other);if(n.c!=null)compile(c,n.c,tail);patch(end);return;}
        case 25:{Scope saved=c.scope;definition(c,n,tail);c.scope=saved;return;}
        case 26:call(c,n,tail);return;
        case 28:{int slot=c.locals++;emit(35,0,slot);Scope saved=c.scope;push(c,1,n.name,0,slot);compile(c,n.a,false);c.scope=saved;emit(34);return;}
        case 29:{int[] v=variable(c,n.name);emit(36,v[0],v[1]);return;}
        default:throw new IllegalArgumentException("unsupported jq node "+n.kind);
    }}
    static final String PRELUDE="\ndef select(f): if f then . else empty end;\ndef recurse(f): def r: ., (f | r); r;\ndef recurse: recurse(.[]?);\ndef map(f): [.[] | f];\ndef to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];\ndef with_entries(f): to_entries | map(f) | from_entries;\ndef paths: path(..) | select(length > 0);\ndef paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);\ndef del(f): delpaths([path(f)]);\ndef _assign(paths; $value): reduce path(paths) as $p (.; setpath($p; $value));\ndef _modify(paths; update): reduce path(paths) as $p (.; . as $x | label $out | (setpath($p; $x | getpath($p) | update) | ., break $out), delpaths([$p]));\ndef first(f): label $out | f | ., break $out;\ndef last(f): reduce f as $x (null; $x);\ndef limit($n; f): if $n > 0 then label $out | foreach f as $item (0; . + 1; $item, if . >= $n then break $out else empty end) elif $n == 0 then empty else f end;\ndef nth($n; f): if $n < 0 then error(\"Out of bounds negative array index\") else last(limit($n + 1; f)) end;\ndef repeat(f): def _repeat: f, _repeat; _repeat;\ndef until(cond; update): def _until: if cond then . else (update | _until) end; _until;\ndef first: .[0];\ndef last: .[-1];\ndef sort_by(f): _sort_by_impl(map([f]));\ndef group_by(f): _group_by_impl(map([f]));\ndef unique_by(f): [group_by(f)[] | .[0]];\ndef scalars: select(type | . != \"array\" and . != \"object\");\ndef objects: select(type == \"object\");\ndef arrays: select(type == \"array\");\ndef numbers: select(type == \"number\");\ndef strings: select(type == \"string\");\ndef range($x): range(0; $x);\ndef isempty(g): first((g | false), true);\ndef any: reduce .[] as $x (false; . or $x);\ndef all: reduce .[] as $x (true; . and $x);\n";
    static Program program(String user){JqParser.Node ast=new JqParser(PRELUDE+"\n"+user).parse();JqCompiler c=new JqCompiler();int index=c.function(0);Context top=new Context(null,c.functions.get(index));c.compile(top,ast,true);c.emit(32);top.fn.nlocals=top.locals;return new Program(c.code.stream().mapToInt(Integer::intValue).toArray(),c.constants.toArray(),c.functions.toArray(Fn[]::new));}
}
