import java.util.ArrayList;
import java.util.Objects;
import static java.util.List.of;

final class MicroDiff {
    record Rich(String kind,Object value){}
    record Difference(String type,ArrayList<Object> path,Object oldValue,Object value){}
    static Object snapshot(boolean v){return JsonData.obj(
        "document",JsonData.obj("title",v?"Text benchmark — revised":"Text benchmark","sections",of(
            JsonData.obj("id","intro","blocks",of(JsonData.obj("type","paragraph","text","A short paragraph of source text."),JsonData.obj("type","code","language","js","lines",v?18:12))),
            JsonData.obj("id","body","blocks",of(JsonData.obj("type","heading","level",v?2:1,"text","Algorithms"),JsonData.obj("type","list","items",v?of("diff","snapshot","hyphen"):of("diff","snapshot")))))),
        "options",JsonData.obj("theme",v?"dark":"light","flags",JsonData.obj("trackChanges",v,"preserveWhitespace",true)),
        "tags",v?of("text","benchmark","updated"):of("text","benchmark"),"updated",new Rich("Date",v?1700000001000L:1700000000000L),
        "pattern",new Rich("RegExp",v?"/source|text|diff/gi":"/source|text/g"),"value",v?42:41);}
    static java.util.List<Object> keys(Object v){if(v instanceof java.util.Map<?,?> m)return new ArrayList<>(m.keySet());ArrayList<Object> a=new ArrayList<>();for(int i=0;i<JsonData.array(v).size();i++)a.add(i);return a;}
    static boolean has(Object v,Object k){return v instanceof java.util.Map<?,?> m?m.containsKey(k):(Integer)k<JsonData.array(v).size();}
    static Object get(Object v,Object k){return v instanceof java.util.Map<?,?> m?m.get(k):JsonData.array(v).get((Integer)k);}
    static int kind(Object v){return v instanceof java.util.Map?1:v instanceof java.util.List?2:v instanceof Rich?3:0;}
    static Difference change(String kind,Object key,Object old,Object value){return new Difference(kind,new ArrayList<>(of(key)),old,value);}
    static ArrayList<Difference> diff(Object old,Object value,java.util.List<Object> ancestors){ArrayList<Difference> out=new ArrayList<>();
        for(Object key:keys(old)){Object before=get(old,key);if(!has(value,key)){out.add(change("REMOVE",key,before,null));continue;}Object after=get(value,key);int k=kind(before);
            boolean cycle=false;for(Object parent:ancestors)if(parent==before){cycle=true;break;}
            if(k!=0&&k==kind(after)&&k!=3&&!cycle){ArrayList<Object> stack=new ArrayList<>(ancestors);stack.add(before);
                for(Difference d:diff(before,after,stack)){d.path.add(0,key);out.add(d);}}
            else if(!Objects.equals(before,after))out.add(change("CHANGE",key,before,after));}
        for(Object key:keys(value))if(!has(old,key))out.add(change("CREATE",key,null,get(value,key)));return out;}
    static long work(Object[][] pairs){long checksum=0;for(int round=0;round<512;round++)for(Object[] pair:pairs){ArrayList<Difference> ds=diff(pair[0],pair[1],of());checksum=(checksum+ds.size()*19)%1000000007;for(Difference d:ds)checksum=(checksum+d.type.length()*23+d.path.size())%1000000007;}return checksum;}
    static Object[][] prepare(){Object[][] pairs=new Object[4][2];for(int i=0;i<4;i++){pairs[i][0]=snapshot(i%2==0);pairs[i][1]=snapshot(i%2!=0);}return pairs;}
}
