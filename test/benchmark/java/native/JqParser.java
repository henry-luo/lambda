import java.util.ArrayList;

/** Recursive-descent parser for the shared jq benchmark filters. */
final class JqParser {
    static final class Node {
        final int kind;int op;Node a,b,c,d;String name="";Object value;
        ArrayList<Node> list=new ArrayList<>();ArrayList<Boolean> pvar=new ArrayList<>();
        Node(int k,Node x,Node y){kind=k;a=x;b=y;}
    }
    static Node mk(int k,Node a,Node b){return new Node(k,a,b);}
    static Node lit(Object v){Node n=mk(3,null,null);n.value=v;return n;}
    static Node named(int k,String name,Node a,Node b){Node n=mk(k,a,b);n.name=name;return n;}
    static Node bin(int op,Node a,Node b){Node n=mk(11,a,b);n.op=op;return n;}
    static Node call(String name,Node... args){Node n=named(26,name,null,null);n.list.addAll(java.util.Arrays.asList(args));return n;}
    final String src;int pos;
    JqParser(String text){src=text;}
    RuntimeException fail(String text){return new IllegalArgumentException("jq parse: "+text+" at "+pos);}
    void space(){while(pos<src.length()){char c=src.charAt(pos);if(Character.isWhitespace(c))pos++;else if(c=='#'){while(pos<src.length()&&src.charAt(pos)!='\n')pos++;}else break;}}
    boolean peek(String s){space();return src.startsWith(s,pos);}
    boolean accept(String s){if(!peek(s))return false;pos+=s.length();return true;}
    void expect(String s){if(!accept(s))throw fail("expected "+s);}
    static boolean identStart(char c){return c=='_'||c>='a'&&c<='z'||c>='A'&&c<='Z';}
    static boolean identChar(char c){return identStart(c)||c>='0'&&c<='9';}
    String ident(){space();int start=pos;if(pos>=src.length()||!identStart(src.charAt(pos)))throw fail("identifier");while(pos<src.length()&&identChar(src.charAt(pos)))pos++;return src.substring(start,pos);}
    String variable(){expect("$");return ident();}
    boolean keyword(String word){if(!peek(word))return false;int end=pos+word.length();if(end<src.length()&&identChar(src.charAt(end)))return false;pos=end;return true;}
    Node string(){expect("\"");StringBuilder chars=new StringBuilder();Node result=null;
        while(pos<src.length()){char c=src.charAt(pos++);if(c=='"'){Node tail=lit(chars.toString());return result==null?tail:bin(1,result,tail);}if(c!='\\'){chars.append(c);continue;}
            char e=src.charAt(pos++);if(e=='('){Node piece=lit(chars.toString());chars.setLength(0);Node expr=pipe();expect(")");Node joined=bin(1,piece,mk(19,expr,call("tostring")));result=result==null?joined:bin(1,result,joined);}
            else if(e=='u'){chars.append((char)Integer.parseInt(src.substring(pos,pos+4),16));pos+=4;}else chars.append(switch(e){case 'n'->'\n';case 'r'->'\r';case 't'->'\t';case 'b'->'\b';case 'f'->'\f';default->e;});}
        throw fail("unterminated string");}
    Node number(){space();int start=pos;while(pos<src.length()){char c=src.charAt(pos);if(c>='0'&&c<='9'||c=='.')pos++;else if(c=='e'||c=='E'){pos++;if(pos<src.length()&&(src.charAt(pos)=='+'||src.charAt(pos)=='-'))pos++;}else break;}return lit(JsonData.parse(src.substring(start,pos)));}
    Node objectValue(){if(accept("-"))return mk(10,objectValue(),null);Node v=postfix(false);if(!peek("|=")&&accept("|"))return mk(19,v,objectValue());return v;}
    Node object(){Node n=mk(9,null,null);if(accept("}"))return n;
        do{Node key,value;if(peek("$")){String name=variable();key=lit(name);value=named(27,name,null,null);}else{
                if(peek("\""))key=string();else if(accept("(")){key=pipe();expect(")");}else key=lit(ident());
                value=accept(":")?objectValue():mk(4,mk(1,null,null),key);}
            n.list.add(key);n.list.add(value);}while(accept(","));expect("}");return n;}
    Node primary(){space();if(pos>=src.length())throw fail("unexpected end");char c=src.charAt(pos);
        if(c=='.'){if(accept(".."))return mk(2,null,null);pos++;if(pos<src.length()&&identStart(src.charAt(pos)))return mk(4,mk(1,null,null),lit(ident()));if(pos<src.length()&&src.charAt(pos)=='"')return mk(4,mk(1,null,null),string());return mk(1,null,null);}
        if(c>='0'&&c<='9')return number();if(c=='"')return string();
        if(accept("(")){Node n=pipe();expect(")");return n;}
        if(accept("[")){if(accept("]"))return mk(8,null,null);Node n=mk(8,pipe(),null);expect("]");return n;}
        if(accept("{"))return object();if(c=='$')return named(27,variable(),null,null);
        if(keyword("if"))return conditional();
        if(keyword("try")){Node n=mk(7,postfix(false),null);if(keyword("catch"))n.b=postfix(false);return n;}
        boolean foreach=keyword("foreach");if(foreach||keyword("reduce")){Node source=postfix(false);if(!keyword("as"))throw fail("expected as");String name=variable();expect("(");Node init=pipe();expect(";");Node update=pipe(),extract=foreach&&accept(";")?pipe():null;expect(")");Node n=named(foreach?23:22,name,source,init);n.c=update;n.d=extract;return n;}
        if(keyword("label")){String name=variable();expect("|");return named(28,name,pipe(),null);}
        if(keyword("break"))return named(29,variable(),null,null);
        if(identStart(c)){String name=ident();Node n=call(name);if(accept("(")){do{n.list.add(pipe());}while(accept(";"));expect(")");}return n;}
        throw fail("unexpected character");}
    Node conditional(){Node n=mk(24,pipe(),null);if(!keyword("then"))throw fail("expected then");n.b=pipe();if(keyword("elif")){n.c=conditional();return n;}if(keyword("else"))n.c=pipe();if(!keyword("end"))throw fail("expected end");return n;}
    Node postfix(boolean allowAs){Node term=primary();while(true){space();char c=pos<src.length()?src.charAt(pos):0,next=pos+1<src.length()?src.charAt(pos+1):0;
        if(c=='.'&&(identStart(next)||next=='"'||next=='[')){pos++;if(next=='"')term=mk(4,term,string());else if(next!='[')term=mk(4,term,lit(ident()));}
        else if(accept("[")){if(accept("]"))term=mk(5,term,null);else if(accept(":")){Node n=mk(6,term,null);n.c=peek("]")?null:pipe();expect("]");term=n;}else{Node idx=pipe();if(accept(":")){Node n=mk(6,term,idx);n.c=peek("]")?null:pipe();expect("]");term=n;}else{expect("]");term=mk(4,term,idx);}}}
        else if(!peek("?//")&&accept("?"))term=mk(7,term,null);else break;}
        if(allowAs&&keyword("as")){String name=variable();expect("|");return named(21,name,term,pipe());}return term;}
    Node unary(){if(!peek("-=")&&accept("-"))return mk(10,postfix(true),null);return postfix(true);}
    Node mul(){Node n=unary();while(true){if(peek("*=")||peek("/=")||peek("%="))break;if(accept("*"))n=bin(3,n,unary());else if(!peek("//")&&accept("/"))n=bin(4,n,unary());else if(accept("%"))n=bin(5,n,unary());else break;}return n;}
    Node add(){Node n=mul();while(true){if(peek("+=")||peek("-="))break;if(accept("+"))n=bin(1,n,mul());else if(accept("-"))n=bin(2,n,mul());else break;}return n;}
    Node cmp(){Node n=add();String[] ops={"==","!=","<","<=",">",">="};for(int i:new int[]{0,1,3,5,2,4})if(accept(ops[i]))return bin(6+i,n,add());return n;}
    Node and(){Node n=cmp();while(keyword("and"))n=mk(12,n,cmp());return n;}
    Node or(){Node n=and();while(keyword("or"))n=mk(13,n,and());return n;}
    Node assign(){Node n=or();int kind=0,op=0;if(accept("|="))kind=16;else if(accept("+=")){kind=17;op=1;}else if(accept("-=")){kind=17;op=2;}else if(accept("*=")){kind=17;op=3;}else if(accept("/=")){kind=17;op=4;}else if(accept("%=")){kind=17;op=5;}else if(accept("//="))kind=18;else if(!peek("==")&&accept("="))kind=15;if(kind==0)return n;Node out=mk(kind,n,alt());out.op=op;return out;}
    Node alt(){Node n=assign();return !peek("//=")&&accept("//")?mk(14,n,alt()):n;}
    Node comma(){Node n=alt();while(accept(","))n=mk(20,n,alt());return n;}
    Node definition(){String name=ident();Node n=named(25,name,null,null);if(accept("(")){do{boolean variable=peek("$");n.list.add(named(27,variable?variable():ident(),null,null));n.pvar.add(variable);}while(accept(";"));expect(")");}expect(":");n.a=pipe();expect(";");n.b=pipe();return n;}
    Node pipe(){if(keyword("def"))return definition();Node n=comma();return !peek("|=")&&accept("|")?mk(19,n,pipe()):n;}
    Node parse(){Node n=pipe();space();if(pos!=src.length())throw fail("trailing source");return n;}
}
