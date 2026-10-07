import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;

/** jq values use host collections; only published path updates copy containers. */
final class JqValues {
    static final class Failure extends RuntimeException {final Object value;Failure(Object v){super(null,null,false,false);value=v;}}
    static Failure error(Object v){return new Failure(v);}
    static int type(Object v){return v==null?0:v.equals(false)?1:v.equals(true)?2:v instanceof Number?3:v instanceof String?4:v instanceof java.util.List?5:6;}
    static String typeName(Object v){return switch(type(v)){case 0->"null";case 1,2->"boolean";case 3->"number";case 4->"string";case 5->"array";default->"object";};}
    static boolean truth(Object v){return v!=null&&!Boolean.FALSE.equals(v);}
    static double num(Object v){if(v instanceof Number n)return n.doubleValue();throw error("number required");}
    static int size(Object v){return v instanceof String s?s.codePointCount(0,s.length()):v instanceof java.util.List<?> l?l.size():v instanceof Map<?,?> m?m.size():0;}
    static java.util.List<Object> arr(Object v){if(!(v instanceof java.util.List))throw error(typeName(v)+" is not an array");return JsonData.array(v);}
    static Map<String,Object> obj(Object v){if(!(v instanceof Map))throw error(typeName(v)+" is not an object");return JsonData.object(v);}
    static ArrayList<Object> keys(Object v,boolean sorted){ArrayList<Object> keys=new ArrayList<>();if(v instanceof Map<?,?> m){keys.addAll(m.keySet());return sorted?sort(keys):keys;}if(v instanceof java.util.List<?> l){for(int i=0;i<l.size();i++)keys.add(i);return keys;}throw error("value has no keys");}
    static int compare(Object a,Object b){int ta=type(a),tb=type(b);if(ta!=tb)return Integer.compare(ta,tb);switch(ta){case 0,1,2:return 0;case 3:{double x=num(a),y=num(b);return x<y?-1:x>y?1:0;}case 4:return ((String)a).compareTo((String)b);
        case 5:{java.util.List<Object> x=arr(a),y=arr(b);for(int i=0;i<Math.min(x.size(),y.size());i++){int c=compare(x.get(i),y.get(i));if(c!=0)return c;}return Integer.compare(x.size(),y.size());}
        default:{ArrayList<Object> x=keys(a,true),y=keys(b,true);int c=compare(x,y);if(c!=0)return c;for(Object key:x){c=compare(obj(a).get(key),obj(b).get(key));if(c!=0)return c;}return 0;}}}
    static Object[] mergeSort(Object[] values,java.util.Comparator<Object> comparator){Object[] items=values;
        for(int width=1;width<items.length;width*=2){Object[] out=new Object[items.length];for(int lo=0;lo<items.length;lo+=width*2){int mid=Math.min(lo+width,items.length),hi=Math.min(lo+width*2,items.length),i=lo,j=mid,at=lo;
            while(i<mid&&j<hi)out[at++]=comparator.compare(items[j],items[i])<0?items[j++]:items[i++];while(i<mid)out[at++]=items[i++];while(j<hi)out[at++]=items[j++];}items=out;}return items;}
    static ArrayList<Object> sort(java.util.List<Object> values){return new ArrayList<>(java.util.Arrays.asList(mergeSort(values.toArray(),JqValues::compare)));}
    static Object[] order(java.util.List<Object> keys){Object[] indices=new Object[keys.size()];for(int i=0;i<indices.length;i++)indices[i]=i;return mergeSort(indices,(a,b)->compare(keys.get((Integer)a),keys.get((Integer)b)));}
    static Map<String,Object> with(Object object,String key,Object value){Map<String,Object> out=new LinkedHashMap<>(obj(object));out.put(key,value);return out;}
    static Object index(Object input,Object key){if(input==null&&(key==null||key instanceof String||key instanceof Number))return null;
        if(input instanceof Map&&key instanceof String)return obj(input).get(key);
        if(input instanceof java.util.List&&key instanceof Number){int at=(int)Math.floor(num(key));java.util.List<Object> a=arr(input);if(at<0)at+=a.size();return at>=0&&at<a.size()?a.get(at):null;}
        throw error("Cannot index "+typeName(input)+" with "+typeName(key));}
    static Object slice(Object input,Object from,Object upto){if(input==null)return null;int n=size(input);if(!(input instanceof java.util.List)&&!(input instanceof String))throw error("value cannot be sliced");
        double lo=from==null?0:num(from),hi=upto==null?n:num(upto);if(lo<0)lo+=n;if(hi<0)hi+=n;int start=Math.min(n,Math.max(0,(int)Math.floor(lo))),end=Math.max(start,Math.min(n,(int)Math.ceil(hi)));
        if(input instanceof String s)return s.substring(s.offsetByCodePoints(0,start),s.offsetByCodePoints(0,end));return new ArrayList<>(arr(input).subList(start,end));}
    static Object bin(int op,Object l,Object r){int lt=type(l),rt=type(r);
        if(op==1){if(l==null)return r;if(r==null)return l;if(lt==3&&rt==3)return num(l)+num(r);if(lt==4&&rt==4)return (String)l+(String)r;
            if(lt==5&&rt==5){ArrayList<Object> a=new ArrayList<>(arr(l));a.addAll(arr(r));return a;}if(lt==6&&rt==6){Map<String,Object> out=new LinkedHashMap<>(obj(l));out.putAll(obj(r));return out;}throw error("values cannot be added");}
        if(op==2&&lt==5&&rt==5){ArrayList<Object> out=new ArrayList<>();for(Object x:arr(l)){boolean keep=true;for(Object y:arr(r))if(compare(x,y)==0){keep=false;break;}if(keep)out.add(x);}return out;}
        if(op==3&&(lt==4&&rt==3||lt==3&&rt==4)){String s=(String)(lt==4?l:r);double n=num(lt==3?l:r);return n<=0?null:s.repeat(Math.max(1,(int)n));}
        if(op>=2&&op<=5){double a=num(l),b=num(r);if((op==4||op==5)&&b==0)throw error("division by zero");return switch(op){case 2->a-b;case 3->a*b;case 4->a/b;default->(double)((long)a%Math.abs((long)b));};}
        int c=compare(l,r);return switch(op){case 6->c==0;case 7->c!=0;case 8->c<0;case 9->c<=0;case 10->c>0;case 11->c>=0;default->throw error("unknown operator");};}
    static boolean contains(Object a,Object b){if(a instanceof Map&&b instanceof Map){for(var e:obj(b).entrySet())if(!obj(a).containsKey(e.getKey())||!contains(obj(a).get(e.getKey()),e.getValue()))return false;return true;}
        if(a instanceof java.util.List&&b instanceof java.util.List){for(Object y:arr(b)){boolean found=false;for(Object x:arr(a))if(contains(x,y)){found=true;break;}if(!found)return false;}return true;}
        if(a instanceof String x&&b instanceof String y)return x.contains(y);return compare(a,b)==0;}
    static String number(Object v){double n=num(v);return n==(long)n?Long.toString((long)n):Double.toString(n);}
    static String json(Object v){if(v==null)return "null";if(v instanceof Boolean)return v.toString();if(v instanceof Number)return number(v);if(v instanceof String s)return JsonData.quote(s);
        StringBuilder out=new StringBuilder();if(v instanceof java.util.List){out.append('[');boolean first=true;for(Object x:arr(v)){if(!first)out.append(',');out.append(json(x));first=false;}return out.append(']').toString();}
        out.append('{');boolean first=true;for(var e:obj(v).entrySet()){if(!first)out.append(',');out.append(JsonData.quote(e.getKey())).append(':').append(json(e.getValue()));first=false;}return out.append('}').toString();}
    static String tostring(Object v){return v instanceof String s?s:json(v);}
    static Object getpath(Object value,java.util.List<Object> path){for(Object key:path)value=index(value,key);return value;}
    static Object setpath(Object input,java.util.List<Object> path,int at,Object value){if(at==path.size())return value;Object key=path.get(at);
        if(key instanceof String name){Object base=input==null?new LinkedHashMap<String,Object>():input;Object child=setpath(index(base,name),path,at+1,value);return with(base,name,child);}
        if(key instanceof Number){ArrayList<Object> out=input==null?new ArrayList<>():new ArrayList<>(arr(input));int i=(int)Math.floor(num(key));if(i<0)i+=out.size();if(i<0)throw error("Out of bounds negative array index");Object child=setpath(i<out.size()?out.get(i):null,path,at+1,value);while(out.size()<=i)out.add(null);out.set(i,child);return out;}
        throw error("Invalid path component");}
    static Object delpath(Object input,java.util.List<Object> path,int at){if(input==null)return null;Object key=path.get(at);boolean last=at==path.size()-1;
        if(input instanceof Map&&key instanceof String name){Map<String,Object> out=new LinkedHashMap<>(obj(input));if(!out.containsKey(name))return input;if(last)out.remove(name);else out.put(name,delpath(out.get(name),path,at+1));return out;}
        if(input instanceof java.util.List&&key instanceof Number){ArrayList<Object> out=new ArrayList<>(arr(input));int i=(int)Math.floor(num(key));if(i<0)i+=out.size();if(i<0||i>=out.size())return input;if(last)out.remove(i);else out.set(i,delpath(out.get(i),path,at+1));return out;}
        return index(input,key);}
    static Object delpaths(Object input,Object paths){ArrayList<Object> ordered=sort(arr(paths));Object cur=input;for(int i=ordered.size()-1;i>=0;i--){java.util.List<Object> p=arr(ordered.get(i));if(p.isEmpty())return null;cur=delpath(cur,p,0);}return cur;}
    static void flatten(Object input,ArrayList<Object> out){for(Object x:arr(input))if(x instanceof java.util.List)flatten(x,out);else out.add(x);}
    static Object addAll(Object input){if(input==null)return null;java.util.List<Object> a=arr(input);if(a.isEmpty())return null;Object acc=a.get(0);int mode=0;
        for(int i=1;i<a.size();i++){Object x=a.get(i);int t=type(acc),xt=type(x);if(t==5&&xt==5){if(mode!=1){acc=new ArrayList<>(arr(acc));mode=1;}arr(acc).addAll(arr(x));}
            else if(t==6&&xt==6){if(mode!=2){acc=new LinkedHashMap<>(obj(acc));mode=2;}obj(acc).putAll(obj(x));}else{acc=bin(1,acc,x);mode=0;}}return acc;}
    static Object nativeCall(int id,Object input,Object[] args){int t=type(input);Object arg=args.length>0?args[0]:null;
        return switch(id){
            case 1->{if(t==1||t==2)throw error("boolean has no length");yield t==3?Math.abs(num(input)):size(input);}
            case 2->!truth(input);case 3->typeName(input);case 4,5->keys(input,id==4);
            case 6->{if(input instanceof Map&&arg instanceof String)yield obj(input).containsKey(arg);if(input instanceof java.util.List&&arg instanceof Number)yield num(arg)>=0&&num(arg)<size(input);throw error("cannot check key");}
            case 7->{if(type(arg)!=t&&!((t==1||t==2)&&(type(arg)==1||type(arg)==2)))throw error("cannot check containment");yield contains(input,arg);}
            case 8->tostring(input);case 9->json(input);
            case 10,11->{if(id==11&&input instanceof Number)yield input;if(!(input instanceof String s))throw error("string required for JSON parse");Object value;try{value=JsonData.parse(s);}catch(RuntimeException e){throw error("Invalid JSON text: "+s);}if(id==11&&!(value instanceof Number))throw error("number required");yield value;}
            case 12,13->{if(!(input instanceof String s))throw error("string required");StringBuilder out=new StringBuilder();s.codePoints().forEach(c->out.appendCodePoint(id==12&&c>=97&&c<=122?c-32:id==13&&c>=65&&c<=90?c+32:c));yield out.toString();}
            case 14->{if(!(input instanceof String s))throw error("string required");ArrayList<Object> out=new ArrayList<>();s.codePoints().forEach(out::add);yield out;}
            case 15->{StringBuilder out=new StringBuilder();for(Object x:arr(input))out.appendCodePoint((int)num(x));yield out.toString();}
            case 16->{if(!(input instanceof String s)||!(arg instanceof String separator))throw error("split strings required");if(s.isEmpty())yield new ArrayList<>();ArrayList<Object> out=new ArrayList<>();if(separator.isEmpty())s.codePoints().forEach(c->out.add(new String(Character.toChars(c))));else out.addAll(java.util.Arrays.asList(s.split(java.util.regex.Pattern.quote(separator),-1)));yield out;}
            case 17->{String separator=arg instanceof String s?s:"";StringBuilder out=new StringBuilder();boolean first=true;for(Object x:arr(input)){if(!first)out.append(separator);out.append(x==null?"":tostring(x));first=false;}yield out.toString();}
            case 18->addAll(input);case 19->{ArrayList<Object> out=new ArrayList<>();flatten(input,out);yield out;}
            case 20->Math.floor(num(input));case 21->Math.ceil(num(input));
            case 22,23->{java.util.List<Object> a=arr(input);if(a.isEmpty())yield null;Object best=a.get(0);for(int i=1;i<a.size();i++){int c=compare(a.get(i),best);if(id==22?c<0:c>=0)best=a.get(i);}yield best;}
            case 24,25->{ArrayList<Object> a=sort(arr(input));if(id==25)yield a;ArrayList<Object> out=new ArrayList<>();for(Object x:a)if(out.isEmpty()||compare(x,out.get(out.size()-1))!=0)out.add(x);yield out;}
            case 26->{if(input==null)yield new ArrayList<>();if(input instanceof String s){int[] cps=s.codePoints().toArray();StringBuilder out=new StringBuilder();for(int i=cps.length-1;i>=0;i--)out.appendCodePoint(cps[i]);yield out.toString();}ArrayList<Object> a=new ArrayList<>(arr(input));java.util.Collections.reverse(a);yield a;}
            case 27->getpath(input,arr(arg));case 28->setpath(input,arr(arg),0,args[1]);case 29->delpaths(input,arg);
            case 30->{Map<String,Object> out=new LinkedHashMap<>();for(Object entry:arr(input)){Map<String,Object> e=obj(entry);Object key=null;for(String name:new String[]{"key","k","name","Name","K","Key"}){key=e.get(name);if(truth(key))break;}out.put(key instanceof String s?s:json(key),e.containsKey("value")?e.get("value"):e.get("v"));}yield out;}
            case 31,32->{java.util.List<Object> values=arr(input),keys=arr(arg);if(values.size()!=keys.size())throw error("sort key count");Object[] order=order(keys);ArrayList<Object> out=new ArrayList<>(),group=new ArrayList<>();Object previous=null;boolean first=true;
                for(Object raw:order){int i=(Integer)raw;if(id==31){out.add(values.get(i));continue;}if(!first&&compare(keys.get(i),previous)!=0){out.add(group);group=new ArrayList<>();}group.add(values.get(i));previous=keys.get(i);first=false;}if(id==32&&!first)out.add(group);yield out;}
            case 33->throw error(input);case 34->throw error(arg);default->throw error("unknown jq native");
        };}
}
