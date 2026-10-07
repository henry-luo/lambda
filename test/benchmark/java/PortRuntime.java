// Native value adapters for the generated benchmark functions. No AST is evaluated.
import java.util.*;
import java.util.function.*;
import java.math.BigInteger;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.regex.*;
import java.io.*;

class PortRuntime {
    static final class Env {
        final Env parent;
        final Map<String,Object> values = new HashMap<>();
        final Set<String> loaded = new HashSet<>();
        Env(Env parent) { this.parent=parent; }
    }
    interface Body { Object run(Env e,Object[] a); }
    static final class Fn {
        final Env closure; final Body body; final String[] types; final int min,max;
        Fn(Env e,Body b,String[] t,int n,int m){closure=e;body=b;types=t;min=n;max=m;}
    }
    static final class Group {
        final List<Fn> functions=new ArrayList<>(); TypeTag type; String name;
    }
    static class TypeTag {
        final String name; String parent="Any"; String[] fields={}; Object[] defaults={};
        TypeTag(String n){name=n;}
    }
    static final class Obj {
        final TypeTag type; final Map<String,Object> fields=new HashMap<>();
        Obj(TypeTag t){type=t; for(int i=0;i<t.fields.length;i++)fields.put(t.fields[i],cloneDefault(t.defaults[i]));}
    }
    static final class Atom { final String value; Atom(String s){value=s;} public boolean equals(Object o){return o instanceof Atom a&&value.equals(a.value);} public int hashCode(){return value.hashCode();} }
    static final class U32 extends Number {final long value;U32(long v){value=v&0xffffffffL;} public int intValue(){return (int)value;}public long longValue(){return value;}public float floatValue(){return value;}public double doubleValue(){return value;}public String toString(){return Long.toString(value);}}
    static final class Ch { final String text; Ch(String t){text=t;} public boolean equals(Object o){return o instanceof Ch c&&text.equals(c.text);} public int hashCode(){return text.hashCode();} }
    static final class Rx { final Pattern pattern; Rx(String p){pattern=Pattern.compile(p);} }
    static final class Spread { final Object value; Spread(Object v){value=v;} }
    static final class Kw extends LinkedHashMap<String,Object> {}
    static final class Tup extends ArrayList<Object> { Tup(Object...a){super(Arrays.asList(a));} }
    static final class Ret extends RuntimeException { final Object value; Ret(Object v){super(null,null,false,false);value=v;} }
    static final class Builtin {final String name; Builtin(String n){name=n;} public String toString(){return "builtin:"+name;} }
    static Object get(Env e,String n){
        if(n.equals("nothing")||n.equals("undef"))return null;
        if(n.equals("true"))return true;if(n.equals("false"))return false;
        if(n.equals("Inf"))return Double.POSITIVE_INFINITY;
        for(Env at=e;at!=null;at=at.parent)if(at.values.containsKey(n))return at.values.get(n);
        if(n.equals("pi"))return Math.PI;
        if(n.equals("NaN"))return Double.NaN;
        return new Builtin(n);
    }
    static void set(Env e,String n,Object v){
        for(Env at=e;at!=null;at=at.parent)if(at.values.containsKey(n)){at.values.put(n,v);return;}
        e.values.put(n,v);
    }
    static void bind(Env e,String n,Object v){e.values.put(n,v);}
    static void define(Env e,String name,Fn f){
        Object old=e.values.get(name); Group g=old instanceof Group x?x:new Group();g.name=name;g.functions.add(f);e.values.put(name,g);
    }
    static void defType(Env e,String n,String p,String[] fields,Object[] defaults,boolean abs){
        TypeTag t=new TypeTag(n);t.parent=p;t.fields=fields;t.defaults=defaults;
        Group g=new Group();g.type=t;g.name=n;bind(e,n,g);bind(e,"@type:"+n,t);
    }
    static Object cloneDefault(Object v){return v instanceof List<?> l?new ArrayList<>(l):v;}
    static Object evalSame(Env e,Function<Env,Object> f){return f.apply(e);}
    static Object select(Object value,Function<Object,Object> f){return f.apply(value);}
    static Object eval(Env e,Function<Env,Object> f){return f.apply(new Env(e));}
    static Object kwarg(Object[]a,String n,Supplier<Object>d){for(Object v:a)if(v instanceof Kw k&&k.containsKey(n))return k.get(n);return d.get();}
    static Object[] expand(Object[]a){boolean needs=false;for(int i=0;i<a.length;i++)if(a[i]instanceof Spread||(a[i]instanceof Kw&&i<a.length-1))needs=true;if(!needs)return a;ArrayList<Object> out=new ArrayList<>(); for(Object v:a){if(v instanceof Spread s)iterable(s.value).forEach(out::add);else out.add(v);}out.sort((l,r)->Boolean.compare(l instanceof Kw,r instanceof Kw));return out.toArray();}
    static Object call(Env caller,Object target,Object[]original){
        Object[]a=expand(original);
        if(target instanceof Fn f)return invoke(f,a);
        if(target instanceof Group g){
            if(g.type!=null&&a.length==1&&a[0] instanceof Atom at&&at.value.equals("raw"))return new Obj(g.type);
            Fn best=null;int score=-1;int count=a.length;for(Object v:a)if(v instanceof Kw)count--;
            for(Fn f:g.functions){
                if(count<f.min||(f.max>=0&&count>f.max))continue;
                int s=0;boolean match=true;
                for(int i=0;i<Math.min(count,f.types.length);i++){if(!isa(f.closure,a[i],f.types[i])){match=false;break;}if(!f.types[i].equals("Any"))s++;}
                if(match&&s>=score){best=f;score=s;}
            }
            if(best!=null)return invoke(best,a);
            if(g.type!=null){Obj o=new Obj(g.type);int i=0;for(Object v:a){if(v instanceof Kw k)o.fields.putAll(k);else{if(i>=g.type.fields.length)throw new IllegalArgumentException("constructor "+g.type.name);o.fields.put(g.type.fields[i++],v);}}return o;}
            return builtin(caller,g.name,a);
        }
        if(target instanceof Builtin b)return builtin(caller,b.name,a);
        throw new IllegalArgumentException("not callable: "+text(target));
    }
    static Object invoke(Fn f,Object[]a){return f.body.run(new Env(f.closure),a);}
    static boolean isa(Env e,Object x,String t){
        if(t.contains("|")){for(String s:t.split("\\|"))if(isa(e,x,s))return true;return false;}
        if(t.equals("Any"))return true;
        if(t.equals("Nothing"))return x==null;
        if(t.equals("Bool"))return x instanceof Boolean;
        if(t.equals("Number")||t.equals("Real"))return x instanceof Number||x instanceof Boolean;
        if(t.equals("Integer")||t.equals("Int")||t.equals("Int32")||t.equals("UInt32")||t.equals("UInt8"))return x instanceof Long||x instanceof Integer||x instanceof BigInteger||x instanceof U32;
        if(t.equals("Float64")||t.equals("AbstractFloat"))return x instanceof Double;
        if(t.equals("AbstractString")||t.equals("String"))return x instanceof String;
        if(t.equals("Symbol"))return x instanceof Atom;
        if(t.equals("Char"))return x instanceof Ch;
        if(t.equals("Tuple"))return x instanceof Tup;
        if(t.equals("AbstractArray")||t.equals("AbstractVector")||t.equals("Vector"))return x instanceof List<?> && !(x instanceof Tup);
        if(t.equals("AbstractDict")||t.equals("Dict"))return x instanceof Map<?,?>;
        if(t.equals("Function"))return x instanceof Fn||x instanceof Group||x instanceof Builtin;
        if(x instanceof Obj o){String n=o.type.name;while(!n.equals("Any")){if(n.equals(t))return true;Object type=get(e,"@type:"+n);if(!(type instanceof TypeTag tag))break;n=tag.parent;}}
        return false;
    }
    static boolean truth(Object x){return x!=null&&(!(x instanceof Boolean b)||b);}
    static boolean truth0(Object x){return truth(x)&&(!(x instanceof Number n)||n.doubleValue()!=0)&&(!(x instanceof String s)||!s.isEmpty())&&(!(x instanceof Collection<?>c)||!c.isEmpty())&&(!(x instanceof Map<?,?>m)||!m.isEmpty());}
    static boolean always(){return true;} static void discard(Object x){}
    static Object check(Object x){if(!truth(x))throw new AssertionError("benchmark check failed");return null;}
    static long integer(Object x){if(x instanceof Boolean b)return b?1:0;if(x instanceof Ch c)return c.text.codePointAt(0);if(x instanceof Number n)return n.longValue();return Long.parseLong(text(x));}
    static double number(Object x){if(x instanceof Boolean b)return b?1:0;return ((Number)x).doubleValue();}
    static BigInteger big(Object x){return x instanceof BigInteger b?b:BigInteger.valueOf(integer(x));}
    static int cmp(Object a,Object b){
        if(a instanceof Number||a instanceof Boolean){if(b instanceof Number||b instanceof Boolean)return (a instanceof BigInteger||b instanceof BigInteger)?big(a).compareTo(big(b)):Double.compare(number(a),number(b));}
        if(a instanceof Ch c)a=c.text;if(b instanceof Ch c)b=c.text;
        if(a instanceof List<?> l&&b instanceof List<?> r){for(int i=0;i<Math.min(l.size(),r.size());i++){int c=cmp(l.get(i),r.get(i));if(c!=0)return c;}return Integer.compare(l.size(),r.size());}
        return text(a).compareTo(text(b));
    }
    static boolean equal(Object a,Object b){if(a instanceof Double d&&d.isNaN()||b instanceof Double f&&f.isNaN())return false;if(a==b)return true;if(a==null||b==null)return false;if(a instanceof Number&&b instanceof Number)return cmp(a,b)==0;if(a instanceof Ch c&&b instanceof Ch d)return c.text.equals(d.text);if(a instanceof List<?>l&&b instanceof List<?>r){if(l.size()!=r.size())return false;for(int i=0;i<l.size();i++)if(!equal(l.get(i),r.get(i)))return false;return true;}if(a instanceof Map<?,?>m&&b instanceof Map<?,?>n){if(!m.keySet().equals(n.keySet()))return false;for(Object k:m.keySet())if(!equal(m.get(k),n.get(k)))return false;return true;}return a.equals(b);}
    static Object op(String name,Object[]a){
        if(a.length>2&&(name.equals("+")||name.equals("*"))){Object v=a[0];for(int i=1;i<a.length;i++)v=op(name,new Object[]{v,a[i]});return v;}
        Object x=a[0],y=a.length>1?a[1]:null;
        if(Set.of("<",">","<=",">=").contains(name)&&((x instanceof Double d&&d.isNaN())||(y instanceof Double f&&f.isNaN())))return false;
        if(name.equals("-")&&a.length==1&&x instanceof List<?>){ArrayList<Object>out=new ArrayList<>();for(Object v:iterable(x))out.add(op("-",new Object[]{v}));return out;}
        if(name.equals("%")&&y instanceof Builtin b&&b.name.equals("UInt32"))return new U32(integer(x));
        if((x instanceof U32||y instanceof U32)&&Set.of("+","-","*","xor","&","|","<<",">>").contains(name))return new U32(integer(op(name,new Object[]{integer(x),integer(y)})));
        switch(name){
            case "!":return !truth(x); case "~":return ~integer(x);
            case "==":return equal(x,y);case "!=":return !equal(x,y);
            case "===":return x==y||(!(x instanceof Obj||x instanceof List<?>||x instanceof Map<?,?>)&&equal(x,y));
            case "!==":return !truth(op("===",a));
            case "<":return cmp(x,y)<0;case ">":return cmp(x,y)>0;case "<=":return cmp(x,y)<=0;case ">=":return cmp(x,y)>=0;
            case "=>":return pair(x,y);
            case ":":return range(integer(x),integer(a[a.length-1]),a.length==3?integer(y):1,true);
            case "&":return integer(x)&integer(y);case "|":return integer(x)|integer(y);case "xor":return integer(x)^integer(y);case "<<":return integer(x)<<integer(y);case ">>":return integer(x)>>integer(y);
            case "/":return number(x)/number(y);
            case "^":return Math.pow(number(x),number(y));
            case "*":if(x instanceof String||x instanceof Ch||y instanceof String||y instanceof Ch)return text(x)+text(y);break;
            case "isa":throw new IllegalArgumentException("isa requires scope");
        }
        if(name.equals(".*")||name.equals(".+")){ArrayList<Object> out=new ArrayList<>();for(Object v:iterable(x))out.add(op(name.substring(1),new Object[]{v,y}));return out;}
        if(x instanceof BigInteger||y instanceof BigInteger){BigInteger l=big(x),r=y==null?BigInteger.ZERO:big(y);return switch(name){case "+"->l.add(r);case "-"->y==null?l.negate():l.subtract(r);case "*"->l.multiply(r);case "fld","÷","div"->l.divide(r);case "%","mod"->l.remainder(r);default->throw new IllegalArgumentException(name);};}
        boolean fp=x instanceof Double||y instanceof Double;
        if(fp){double l=number(x),r=y==null?0:number(y);return switch(name){case "+"->l+r;case "-"->y==null?-l:l-r;case "*"->l*r;case "%"->l%r;case "mod"->l-Math.floor(l/r)*r;case "fld"->(long)Math.floor(l/r);case "div","÷"->(long)(l/r);default->throw new IllegalArgumentException(name);};}
        long l=integer(x),r=y==null?0:integer(y);return switch(name){case "+"->l+r;case "-"->y==null?-l:l-r;case "*"->l*r;case "%"->l%r;case "mod"->Math.floorMod(l,r);case "fld"->Math.floorDiv(l,r);case "div","÷"->l/r;default->throw new IllegalArgumentException(name);};
    }
    static ArrayList<Object> arr(Object...a){return new ArrayList<>(Arrays.asList(expand(a)));}
    static Tup tuple(Object...a){return new Tup(expand(a));}
    static Object pair(Object a,Object b){return new Tup(a,b);}
    static Kw keywords(Object...a){Kw out=new Kw();for(Object p:a){out.put(text(nth(p,0)),nth(p,1));}return out;}
    static ArrayList<Object> arrSlice(Object[]a,int n){return new ArrayList<>(Arrays.asList(a).subList(n,a.length));}
    static Object nth(Object x,int i){return x instanceof Map.Entry<?,?>p?(i==0?p.getKey():p.getValue()):((List<?>)x).get(i);}
    static long len(Object x){if(x instanceof String s)return s.codePointCount(0,s.length());if(x instanceof Collection<?>l)return l.size();if(x instanceof Map<?,?>m)return m.size();if(x instanceof StringBuilder b)return b.length();throw new IllegalArgumentException("length "+text(x));}
    static Iterable<Object> iterable(Object x){if(x instanceof String s){ArrayList<Object>l=new ArrayList<>();s.codePoints().forEach(c->l.add(new Ch(new String(Character.toChars(c)))));return l;}if(x instanceof Map<?,?>m){ArrayList<Object>l=new ArrayList<>();m.forEach((k,v)->l.add(pair(k,v)));return l;}return (Iterable<Object>)x;}
    static Iterable<Object> range(long start,long stop,long step,boolean inclusive){long bound=inclusive?stop+(step>0?1:-1):stop;return ()->new Iterator<>(){long at=start;public boolean hasNext(){return step>0?at<bound:at>bound;}public Object next(){long v=at;at+=step;return v;}};}
    static Object index(Object x,Object[]i){
        if(x instanceof Builtin b||x instanceof TypeTag||x instanceof Group)return arr(i);
        if(i.length==0)return ((List<?>)x).get(0);
        Object at=i[0];if(x instanceof Map<?,?>m)return m.get(at);
        if(at instanceof Iterable<?>){ArrayList<Object>out=new ArrayList<>();for(Object n:iterable(at))out.add(index(x,new Object[]{n}));if(x instanceof String)return strcat(out.toArray());return out;}
        int k=(int)integer(at)-1;
        if(x instanceof String s){int p=s.offsetByCodePoints(0,k);return new Ch(new String(Character.toChars(s.codePointAt(p))));}
        return ((List<?>)x).get(k);
    }
    static void putIndex(Object x,Object[]i,Object v){if(x instanceof Map m){m.put(i[0],v);return;}List l=(List)x;int at=i.length==0?0:(int)integer(i[0])-1;l.set(at,v);}
    static Object field(Object x,String f){if(x instanceof Builtin b)return new Builtin(b.name+"."+f);if(x instanceof Obj o)return o.fields.get(f);if(x instanceof Env e)return get(e,f);if(f.equals("first"))return nth(x,0);if(f.equals("second"))return nth(x,1);throw new IllegalArgumentException("field "+f+" on "+text(x));}
    static void putField(Object x,String f,Object v){((Obj)x).fields.put(f,v);}
    static String text(Object x){if(x==null)return "nothing";if(x instanceof Ch c)return c.text;if(x instanceof Atom a)return a.value;if(x instanceof Double d&&d==Math.rint(d)&&Math.abs(d)<1e16)return d.toString();return String.valueOf(x);}
    static String strcat(Object...a){StringBuilder b=new StringBuilder();for(Object x:a)b.append(text(x));return b.toString();}
    static Object broadcast(Env e,Object f,Object[]a){ArrayList<Object>out=new ArrayList<>();for(Object v:iterable(a[0]))out.add(call(e,f,new Object[]{v}));return out;}
    static Object fill(Object value,long n){ArrayList<Object>out=new ArrayList<>((int)n);for(long i=0;i<n;i++)out.add(value);return out;}
    static String format(Object...a){Object[]values=new Object[a.length-1];for(int i=1;i<a.length;i++)values[i-1]=a[i] instanceof Ch?text(a[i]):a[i];return String.format(Locale.ROOT,text(a[0]),values);}
    static void load(Env e,String path){if(e.loaded.add(path))Ports.install(e,path);}
    static Object builtin(Env e,String n,Object[]a){
        Object x=a.length>0?a[0]:null,y=a.length>1?a[1]:null,z=a.length>2?a[2]:null;
        switch(n){
            case "jor":{Object v=call(e,x,new Object[]{});return v==null||v==Boolean.FALSE?call(e,y,new Object[]{}):v;}
            case "jrescue":try{return call(e,x,new Object[]{});}catch(Ret r){throw r;}catch(RuntimeException ex){return call(e,y,new Object[]{});}
            case "jstring":return x instanceof Double d&&d==Math.rint(d)&&Math.abs(d)<Long.MAX_VALUE?Long.toString(d.longValue()):text(x);
            case "jparse":return call(e,get(e,"fixture_value"),new Object[]{call(e,get(e,"m_parse"),new Object[]{call(e,get(e,"C__Parser"),new Object[]{text(x)})})});
            case "with_node":{Obj node=(Obj)x;Obj out=new Obj(node.type);out.fields.putAll(node.fields);for(Object v:a)if(v instanceof Kw k)out.fields.putAll(k);return out;}
            case "json_string":return jsonQuote(text(x));
            case "identity":return x;
            case "truth0":return truth0(x);
            case "length","sizeof","ncodeunits":return len(x);
            case "isempty":return len(x)==0;
            case "Int","Int32","UInt8","int0","jint":return integer(x);
            case "UInt32":return new U32(integer(x));
            case "Float64":return x instanceof String?Double.parseDouble(text(x)):number(x);
            case "String","string":return strcat(a);
            case "Symbol":return new Atom(strcat(a));
            case "Char":return new Ch(new String(Character.toChars((int)integer(x))));
            case "ord0","jord":return (long)text(x).codePointAt(0);
            case "chr0","jchr":return new String(Character.toChars((int)integer(x)));
            case "AsciiText":return x;
            case "Val":return x;
            case "zero":if(x instanceof Double)return 0.0;return 0L;case "one":if(x instanceof Double)return 1.0;return 1L;
            case "big":return big(x);
            case "sqrt":return Math.sqrt(number(x));case "sin":return Math.sin(number(x));case "cos":return Math.cos(number(x));
            case "abs":if(x instanceof Double)return Math.abs(number(x));return Math.abs(integer(x));
            case "sign":return (long)Math.signum(number(x));
            case "min","max":{Object v=x;for(Object o:a)if(n.equals("min")?cmp(o,v)<0:cmp(o,v)>0)v=o;return v;}
            case "clamp":return cmp(x,y)<0?y:cmp(x,z)>0?z:x;
            case "floor","ceil","trunc":{Object v=a[a.length-1];double f=n.equals("floor")?Math.floor(number(v)):n.equals("ceil")?Math.ceil(number(v)):(long)number(v);if(a.length==2)return (long)f;return f;}
            case "parse":if(x instanceof Builtin b&&b.name.equals("Float64"))return Double.parseDouble(text(y));return Long.parseLong(text(y));
            case "range0":return a.length==1?range(0,integer(x),1,false):range(integer(x),integer(y),a.length==3?integer(z):1,false);
            case "eachindex":return range(1,len(x),1,true);
            case "enumerate","enumerate0":{ArrayList<Object>out=new ArrayList<>();long i=n.equals("enumerate")?1:0;for(Object v:iterable(x))out.add(pair(i++,v));return out;}
            case "zip":{List<Iterator<Object>> it=new ArrayList<>();for(Object v:a)it.add(iterable(v).iterator());ArrayList<Object>out=new ArrayList<>();while(it.stream().allMatch(Iterator::hasNext)){ArrayList<Object>vs=new ArrayList<>();for(Iterator<Object>i:it)vs.add(i.next());out.add(tuple(vs.toArray()));}return out;}
            case "collect":{ArrayList<Object>out=new ArrayList<>();iterable(x).forEach(out::add);return out;}
            case "fill":return fill(x,integer(y));
            case "zeros","ones":{Object v=n.equals("ones")?Long.valueOf(1):(a.length==1?Double.valueOf(0): (Object)Long.valueOf(0));return fill(v,integer(a[a.length-1]));}
            case "Vector":return fill(null,integer(a[a.length-1]));
            case "codeunit":return (long)text(x).charAt((int)integer(y)-1);
            case "nextind":return integer(y)+(a.length>2?integer(z):1);
            case "prevind":return integer(y)-1;
            case "SubString":return text(x).substring((int)integer(y)-1,a.length>2?(int)integer(z):text(x).length());
            case "Ref":return arr(x);
            case "Dict":{Map<Object,Object>out=new LinkedHashMap<>();if(a.length==1&&!(x instanceof Tup)){for(Object v:iterable(x))out.put(nth(v,0),nth(v,1));}else for(Object v:a)out.put(nth(v,0),nth(v,1));return out;}
            case "get0","jget":{
                if(x instanceof Map<?,?>m)return m.get(y);
                long k=integer(y);if(n.equals("get0")&&k<0)k+=len(x);
                if(n.equals("jget")&&(k<0||k>=len(x)))return null;
                Object v=index(x,new Object[]{k+1});return v instanceof Ch c?c.text:v;
            }
            case "set0!","jset!":{long k=y instanceof Number?integer(y):0;if(x instanceof Map m){m.put(y,z);return z;}if(k<0)k+=len(x);putIndex(x,new Object[]{k+1},z);return z;}
            case "push!","m_append":for(int i=1;i<a.length;i++)((List)x).add(a[i]);return x;
            case "append!","m_extend":iterable(y).forEach(((List)x)::add);return x;
            case "pop!","m_pop":return ((List)x).remove(a.length==1?((List)x).size()-1:(int)(integer(y)<0?len(x)+integer(y):integer(y)));
            case "insert!","m_insert":((List)x).add((int)integer(y)-(n.equals("insert!")?1:0),z);return x;
            case "m_get","get":return ((Map<?,?>)x).containsKey(y)?((Map<?,?>)x).get(y):a.length>2?z:null;
            case "haskey":return ((Map<?,?>)x).containsKey(y);
            case "in","in0":return y instanceof String?text(y).contains(text(x)):y instanceof Map<?,?>m?m.containsKey(x):contains(iterable(y),x);
            case "pairs","m_items":return iterable(x);
            case "keys":return new ArrayList<>(((Map<?,?>)x).keySet());
            case "values":return new ArrayList<>(((Map<?,?>)x).values());
            case "first":if(a.length>1)return slice(x,0L,y,null);return iterable(x).iterator().next();case "only":check(len(x)==1);return nth(x,0);
            case "add0":return x instanceof String?text(x)+text(y):x instanceof List<?>?concatLists(x,y):op("+",a);
            case "mul0":return x instanceof String?text(x).repeat((int)integer(y)):x instanceof List<?>?repeatList(x,integer(y)):op("*",a);
            case "repeat":return x instanceof String?text(x).repeat((int)integer(y)):repeatList(x,integer(y));
            case "vcat","jcat":{if(x instanceof String)return strcat(a);ArrayList<Object>out=new ArrayList<>();for(Object v:a)iterable(v).forEach(out::add);return out;}
            case "slice0","jslice":return slice(x,a.length>1?y:null,a.length>2?z:null,a.length>3?a[3]:null);
            case "setslice0!":{List l=(List)x;int lo=y==null?0:(int)integer(y),hi=z==null?l.size():(int)integer(z);l.subList(lo,hi).clear();ArrayList<Object>values=new ArrayList<>();iterable(a[3]).forEach(values::add);l.addAll(lo,values);return a[3];}
            case "copy":return x instanceof Map<?,?>m?new LinkedHashMap<>(m):x instanceof List<?>l?new ArrayList<>(l):x;
            case "deepcopy":return deep(x);
            case "fill!":Collections.fill((List)x,y);return x;
            case "isfinite":return Double.isFinite(number(x));
            case "Sys.iswindows":return isWindows(System.getProperty("os.name", ""));
            case "isinteger":return number(x)==Math.rint(number(x));
            case "join","m_join":{Object vals=n.equals("m_join")?y:x;String sep=n.equals("m_join")?text(x):a.length>1?text(y):"";StringBuilder out=new StringBuilder();for(Object v:iterable(vals)){if(out.length()>0)out.append(sep);out.append(text(v));}return out.toString();}
            case "split","m_split":return arr((Object[])text(x).split(Pattern.quote(text(y)),-1));
            case "startswith","m_startswith":return text(x).startsWith(text(y));case "endswith","m_endswith":return text(x).endsWith(text(y));
            case "occursin":return x instanceof Rx r?r.pattern.matcher(text(y)).find():text(y).contains(text(x));
            case "occursin_reverse":return text(x).contains(text(y));
            case "uppercase":return text(x).toUpperCase(Locale.ROOT);case "lowercase","m_lower":return text(x).toLowerCase(Locale.ROOT);
            case "uppercasefirst":return text(x).substring(0,1).toUpperCase(Locale.ROOT)+text(x).substring(1);
            case "isascii","m_isascii":return text(x).codePoints().allMatch(c->c<128);
            case "m_isalnum":return !text(x).isEmpty()&&text(x).codePoints().allMatch(Character::isLetterOrDigit);
            case "m_isspace","isspace":return !text(x).isEmpty()&&text(x).codePoints().allMatch(Character::isWhitespace);
            case "isletter":return Character.isLetter(text(x).codePointAt(0));case "isdigit":return Character.isDigit(text(x).codePointAt(0));
            case "m_find":{int start=a.length>2?(int)integer(z):0;return (long)text(x).indexOf(text(y),start);}
            case "replace":{String out=text(x);for(int i=1;i<a.length;i++){Object p=nth(a[i],0),v=nth(a[i],1);out=p instanceof Rx r?r.pattern.matcher(out).replaceAll(Matcher.quoteReplacement(text(v))):out.replace(text(p),text(v));}return out;}
            case "Regex":return new Rx(text(x));
            case "eachmatch":{ArrayList<Object>out=new ArrayList<>();Matcher m=((Rx)x).pattern.matcher(text(y));while(m.find())out.add(m.group());return out;}
            case "re_search","re_match":{Matcher m=Pattern.compile((n.equals("re_match")?"^(?:":"")+text(x)+(n.equals("re_match")?")":"")).matcher(text(y));return m.find()?m.group():null;}
            case "match":{Matcher m=((Rx)x).pattern.matcher(text(y));return m.find()?m.group():null;}
            case "format0":return text(y).equals("02d")?String.format(Locale.ROOT,"%02d",integer(x)):String.format(Locale.ROOT,"%.3f",number(x));
            case "reverse":{if(x instanceof String)return new StringBuilder(text(x)).reverse().toString();ArrayList out=new ArrayList((List)x);Collections.reverse(out);return out;}
            case "rpad","lpad":{String s=text(x),pad=a.length>2?text(z):" ";String rest=pad.repeat(Math.max(0,(int)integer(y)-s.length()));return n.equals("rpad")?s+rest:rest+s;}
            case "sort","sort!":{List<Object>out=n.equals("sort")?new ArrayList<>((List)x):(List)x;Kw kw=null;for(Object v:a)if(v instanceof Kw k)kw=k;Object key=kw==null?null:kw.get("by");final Object keyFn=key;out.sort((l,r)->cmp(keyFn==null?l:call(e,keyFn,new Object[]{l}),keyFn==null?r:call(e,keyFn,new Object[]{r})));return out;}
            case "issorted":{Object prev=null;boolean first=true;for(Object v:iterable(x)){if(!first&&cmp(prev,v)>0)return false;prev=v;first=false;}return true;}
            case "all","any":{boolean result=n.equals("all");Object values=a.length==1?x:y;for(Object v:iterable(values)){boolean t=truth(a.length==1?v:call(e,x,new Object[]{v}));if(n.equals("all")&&!t)return false;if(n.equals("any")&&t)return true;}return result;}
            case "foldl":{Object result=null;for(Object v:a)if(v instanceof Kw k)result=k.get("init");for(Object v:iterable(y))result=call(e,x,new Object[]{result,v});return result;}
            case "sum":{Object result=0L;for(Object v:iterable(x))result=op("+",new Object[]{result,v});return result;}
            case "objectid":return (long)System.identityHashCode(x);
            case "typeof":return x==null?null:x.getClass();
            case "time_ns":return System.nanoTime();
            case "read":try{return Files.readString(Path.of(text(x)));}catch(IOException ex){throw new UncheckedIOException(ex);}
            case "joinpath":{Path p=Path.of(text(x));for(int i=1;i<a.length;i++)p=p.resolve(text(a[i]));return p.normalize().toString();}
            case "take!":{StringBuilder b=(StringBuilder)x;String v=b.toString();b.setLength(0);return v;}
            case "IOBuffer":return new StringBuilder();
            case "print","println","printf":{
                int start=0;StringBuilder sink=null;if(a.length>0&&x instanceof StringBuilder b){sink=b;start=1;}
                String output;if(n.equals("printf"))output=format(Arrays.copyOfRange(a,start,a.length));else output=strcat(Arrays.copyOfRange(a,start,a.length))+(n.equals("println")?"\n":"");
                if(sink!=null)sink.append(output);else System.out.print(output);return null;
            }
            case "open":{try(OutputStream out=new FileOutputStream(text(y))){return call(e,x,new Object[]{out});}catch(IOException ex){throw new UncheckedIOException(ex);}}
            case "write":try{byte[]bytes=text(y).getBytes(StandardCharsets.UTF_8);((OutputStream)x).write(bytes);return (long)bytes.length;}catch(IOException ex){throw new UncheckedIOException(ex);}
            case "ErrorException":return new IllegalArgumentException(text(x));case "error","throw":throw x instanceof RuntimeException r?r:new IllegalArgumentException(text(x));
            case "run_benchmark":return runBenchmark(e,a);
            case "checked_benchmark":{
                Object prep=a.length>3&&a[3]instanceof Kw k&&k.containsKey("prepare")?k.get("prepare"):new Builtin("nothing_prepare");
                Object work=y,expected=z,label=x;
                Body b=(env,args)->call(e,work,new Object[]{args[1]});
                Fn f=new Fn(e,b,new String[]{"Any","Any"},2,2);
                Fn verify=new Fn(e,(env,args)->equal(args[0],expected),new String[]{"Any"},1,1);
                Fn report=new Fn(e,(env,args)->{System.out.println(text(label)+": PASS");return null;},new String[]{"Any"},1,1);
                return runBenchmark(e,new Object[]{prep,f,verify,report});
            }
            case "nothing_prepare":return null;
            case "reinterpret":return (long)(int)integer(y);
            default:throw new IllegalArgumentException("unimplemented adapter: "+n+" "+Arrays.toString(a));
        }
    }
    static boolean isWindows(String name){return name.startsWith("Windows");}
    static String jsonQuote(String input){StringBuilder b=new StringBuilder("\"");for(int c:input.codePoints().toArray()){switch(c){case 34:b.append("\\\"");break;case 92:b.append("\\\\");break;case 10:b.append("\\n");break;case 13:b.append("\\r");break;case 9:b.append("\\t");break;default:if(c<32)b.append(String.format(Locale.ROOT,"\\u%04x",c));else b.appendCodePoint(c);}}return b.append('"').toString();}
    static boolean contains(Iterable<Object>a,Object x){for(Object v:a)if(equal(v,x))return true;return false;}
    static Object concatLists(Object x,Object y){ArrayList<Object>out=x instanceof Tup?new Tup():new ArrayList<>();iterable(x).forEach(out::add);iterable(y).forEach(out::add);return out;}
    static Object repeatList(Object x,long n){ArrayList<Object>out=new ArrayList<>();for(long i=0;i<n;i++)iterable(x).forEach(out::add);return out;}
    static Object slice(Object x,Object lo,Object hi,Object step){long n=len(x),stride=step==null?1:integer(step),start=lo==null?(stride>0?0:n-1):integer(lo),stop=hi==null?(stride>0?n:-1):integer(hi);if(lo!=null&&start<0)start+=n;if(hi!=null&&stop<0)stop+=n;start=Math.max(stride>0?0:-1,Math.min(start,stride>0?n:n-1));stop=Math.max(stride>0?0:-1,Math.min(stop,stride>0?n:n-1));ArrayList<Object>out=new ArrayList<>();for(Object k:range(start,stop,stride,false))out.add(index(x,new Object[]{integer(k)+1}));return x instanceof String?strcat(out.toArray()):out;}
    static Object deep(Object x){if(x instanceof List<?>l){ArrayList<Object>out=new ArrayList<>();for(Object v:l)out.add(deep(v));return out;}if(x instanceof Map<?,?>m){Map<Object,Object>out=new LinkedHashMap<>();m.forEach((k,v)->out.put(k,deep(v)));return out;}return x;}
    static Object runBenchmark(Env e,Object[]a){
        StringBuilder output=new StringBuilder();int warm=Integer.parseInt(System.getenv().getOrDefault("NATIVE_BENCH_WARMUP","1"));
        if(warm==1){Object result=call(e,a[1],new Object[]{output,call(e,a[0],new Object[]{})});if(!truth(call(e,a[2],new Object[]{result})))throw new AssertionError("benchmark result: "+text(result));}
        output.setLength(0);Object state=call(e,a[0],new Object[]{});long start=System.nanoTime();Object result=call(e,a[1],new Object[]{output,state});double elapsed=(System.nanoTime()-start)/1e6;
        if(!truth(call(e,a[2],new Object[]{result})))throw new AssertionError("benchmark result: "+text(result));System.out.print(output);call(e,a[3],new Object[]{result});System.out.println("__TIMING__:"+elapsed);return result;
    }
    static void adapters(Env e,String file){
        switch(file){
            case "class_support.jl":return;
            case "jetstream/support.jl":load(e,"common.jl");return;
            case "text/support.jl":load(e,"common.jl");load(e,"fixtures.jl");return;
            case "fixtures.jl":return;
            case "text/jq_values.jl":
                load(e,"fixtures.jl");
                // Qualified calls must use the module that owns the JSON parser.
                bind(e,"jparse",new Fn(e,(scope,args)->builtin(scope,"jparse",args),new String[]{"Any","Any"},2,2));
                defType(e,"JResult","Any",new String[]{"ok","value"},new Object[]{false,null},false);
                defType(e,"Node","Any",new String[]{"kind","op","a","b","c","d","list","name","value","pvar"},new Object[]{0L,0L,null,null,null,null,arr(),"",null,arr()},false);return;
            default:throw new IllegalArgumentException("missing source "+file);
        }
    }
    public static void main(String[]args){
        if(args.length<1)throw new IllegalArgumentException("usage: Ports suite/name [inner outer]");Env root=new Env(null);bind(root,"ARGS",arr((Object[])Arrays.copyOfRange(args,1,args.length)));if(args[0].equals("text/text_search"))TextSearch.run(root);else load(root,args[0]+".jl");
    }
}
