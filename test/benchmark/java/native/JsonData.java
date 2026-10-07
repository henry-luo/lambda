import java.util.LinkedHashMap;
import java.util.ArrayList;
import java.nio.file.Files;
import java.nio.file.Path;

/** Fixture JSON represented by ordinary Java maps, lists and scalar values. */
final class JsonData {
    final String text;int at;
    JsonData(String t){text=t;}
    static Object load(String name){try{return parse(Files.readString(Path.of("test/benchmark/text/"+name)));}catch(Exception e){throw new RuntimeException(e);}}
    static Object parse(String text){JsonData p=new JsonData(text);Object v=p.value();p.space();if(p.at!=text.length())throw new IllegalArgumentException("trailing JSON");return v;}
    void space(){while(at<text.length()&&Character.isWhitespace(text.charAt(at)))at++;}
    char take(){return text.charAt(at++);}
    void require(char c){space();if(take()!=c)throw new IllegalArgumentException("JSON token");}
    Object value(){space();char c=text.charAt(at);
        if(c=='"')return string();
        if(c=='{'){at++;LinkedHashMap<String,Object> m=new LinkedHashMap<>();space();if(text.charAt(at)=='}'){at++;return m;}
            do{space();String k=string();require(':');m.put(k,value());space();c=take();}while(c==',');if(c!='}')throw new IllegalArgumentException("JSON object");return m;}
        if(c=='['){at++;ArrayList<Object> a=new ArrayList<>();space();if(text.charAt(at)==']'){at++;return a;}
            do{a.add(value());space();c=take();}while(c==',');if(c!=']')throw new IllegalArgumentException("JSON array");return a;}
        if(text.startsWith("true",at)){at+=4;return true;}if(text.startsWith("false",at)){at+=5;return false;}if(text.startsWith("null",at)){at+=4;return null;}
        int start=at;while(at<text.length()&&"-+0123456789.eE".indexOf(text.charAt(at))>=0)at++;String n=text.substring(start,at);
        if(n.indexOf('.')>=0||n.indexOf('e')>=0||n.indexOf('E')>=0)return Double.parseDouble(n);return Long.parseLong(n);
    }
    String string(){if(take()!='"')throw new IllegalArgumentException("JSON string");StringBuilder b=new StringBuilder();
        for(char c=take();c!='"';c=take()){if(c=='\\'){c=take();switch(c){case 'b'->c='\b';case 'f'->c='\f';case 'n'->c='\n';case 'r'->c='\r';case 't'->c='\t';case 'u'->{c=(char)Integer.parseInt(text.substring(at,at+4),16);at+=4;}case '"','\\','/'->{}default->throw new IllegalArgumentException("JSON escape");}}b.append(c);}return b.toString();}
    @SuppressWarnings("unchecked") static java.util.Map<String,Object> object(Object v){return v==null?java.util.Map.of():(java.util.Map<String,Object>)v;}
    @SuppressWarnings("unchecked") static java.util.List<Object> array(Object v){return v==null?java.util.List.of():(java.util.List<Object>)v;}
    static String string(Object v){return v==null?"":v.toString();}
    static java.util.Map<String,Object> obj(Object... fields){LinkedHashMap<String,Object> m=new LinkedHashMap<>();for(int i=0;i<fields.length;i+=2)m.put((String)fields[i],fields[i+1]);return m;}
    static String quote(String s){StringBuilder b=new StringBuilder("\"");for(int i=0;i<s.length();i++){char c=s.charAt(i);switch(c){case '"'->b.append("\\\"");case '\\'->b.append("\\\\");case '\n'->b.append("\\n");case '\r'->b.append("\\r");case '\t'->b.append("\\t");default->{if(c<32)b.append(String.format("\\u%04x",(int)c));else b.append(c);}}}return b.append('"').toString();}
}
