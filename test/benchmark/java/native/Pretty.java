import java.util.ArrayList;
import java.util.Map;

/** Native document algebra and AST printer matching prettier_ast.js. */
final class Pretty {
    record Doc(String kind,String value,java.util.List<Doc> parts,Doc content,Doc broken,Doc flat,double threshold){}
    static Doc text(Object v){return new Doc("text",JsonData.string(v),null,null,null,null,0);}
    static Doc concat(Doc... parts){return concat(java.util.Arrays.asList(parts));}
    static Doc concat(java.util.List<Doc> parts){return new Doc("concat",null,parts,null,null,null,0);}
    static Doc wrap(String kind,Doc content){return new Doc(kind,null,null,content,null,null,Double.POSITIVE_INFINITY);}
    static Doc indent(Doc d){return wrap("indent",d);}
    static Doc group(Doc d){return wrap("group",d);}
    static Doc group(Doc d,double threshold){return new Doc("group",null,null,d,null,null,threshold);}
    static Doc ifBreak(Doc b){return new Doc("if-break",null,null,null,b,text(""),0);}
    static final Doc LINE=wrap("line",null),SOFT=wrap("softline",null),HARD=wrap("hardline",null);
    static Doc join(Doc separator,java.util.List<Doc> parts){ArrayList<Doc> result=new ArrayList<>();for(Doc d:parts){if(!result.isEmpty())result.add(separator);result.add(d);}return concat(result);}
    static double flatLength(Doc d){return switch(d.kind){case "text"->d.value.length();case "concat"->{double n=0;for(Doc p:d.parts)n+=flatLength(p);yield n;}case "indent","group"->flatLength(d.content);case "if-break"->flatLength(d.flat);case "line"->1;case "softline"->0;default->Double.POSITIVE_INFINITY;};}
    static final class State {int column,indent;final int width=80;}
    static String render(Doc d,State s,boolean flat){switch(d.kind){
        case "text":s.column+=d.value.length();return d.value;
        case "concat":{StringBuilder out=new StringBuilder();for(Doc p:d.parts)out.append(render(p,s,flat));return out.toString();}
        case "indent":{s.indent++;String out=render(d.content,s,flat);s.indent--;return out;}
        case "group":return render(d.content,s,flat||(d.threshold>=flatLength(d.content)&&flatLength(d.content)<=s.width-s.column));
        case "if-break":return render(flat?d.flat:d.broken,s,flat);
        case "line":if(flat){s.column++;return " ";}break;
        case "softline":if(flat)return "";break;
        case "hardline":break;
        default:throw new IllegalArgumentException(d.kind);
    }s.column=s.indent*2;return "\n"+" ".repeat(s.column);}
    static Map<String,Object> obj(Object value){return JsonData.object(value);}
    static Map<String,Object> child(Map<String,Object> n,String key){return obj(n.get(key));}
    static String str(Map<String,Object> n,String key){return JsonData.string(n.get(key));}
    static boolean flag(Map<String,Object> n,String key){return Boolean.TRUE.equals(n.get(key));}
    static java.util.List<Object> array(Map<String,Object> n,String key){return JsonData.array(n.get(key));}
    static java.util.List<Doc> docs(java.util.List<Object> values,boolean statements){ArrayList<Doc> result=new ArrayList<>();for(Object v:values)result.add(statements?statement(obj(v)):node(obj(v)));return result;}
    static Doc literal(Map<String,Object> n){return switch(str(n,"type")){case "StringLiteral"->text(JsonData.quote(str(n,"value")));case "NumericLiteral","BooleanLiteral"->text(n.get("value"));case "NullLiteral"->text("null");case "RegExpLiteral"->text("/"+str(n,"pattern")+"/"+str(n,"flags"));default->throw new IllegalArgumentException("literal");};}
    static Doc key(Map<String,Object> n){return flag(n,"computed")?concat(text("["),node(child(n,"key")),text("]")):node(child(n,"key"));}
    static Doc delimited(String open,String close,java.util.List<Object> values,Doc line,boolean trailingInside){
        Doc contents=join(concat(text(","),LINE),docs(values,false));
        if(trailingInside)return group(concat(text(open),indent(concat(line,contents,ifBreak(text(",")))),line,text(close)));
        return group(concat(text(open),indent(concat(line,contents)),ifBreak(text(",")),line,text(close)));
    }
    static Doc params(java.util.List<Object> values){return delimited("(",")",values,SOFT,false);}
    static Doc args(java.util.List<Object> values){if(values.isEmpty())return text("()");for(Object value:values)if(str(obj(value),"type").equals("ObjectExpression"))return concat(text("("),join(text(", "),docs(values,false)),text(")"));return params(values);}
    static Doc block(java.util.List<Object> body,boolean statements){return body.isEmpty()?text("{}"):concat(text("{"),indent(concat(HARD,join(HARD,docs(body,statements)))),HARD,text("}"));}
    static Doc variable(Map<String,Object> n,boolean terminator){Doc d=concat(text(str(n,"kind")+" "),join(concat(text(","),LINE),docs(array(n,"declarations"),false)));return terminator?concat(d,text(";")):d;}
    static Doc property(Map<String,Object> n){if(str(n,"type").equals("SpreadElement"))return concat(text("..."),node(child(n,"argument")));Doc k=key(n);return flag(n,"shorthand")?k:concat(k,text(": "),expression(child(n,"value"),0));}
    static int precedence(Map<String,Object> n){if(n.isEmpty())return 100;return switch(str(n,"type")){case "AssignmentExpression","ArrowFunctionExpression"->1;case "LogicalExpression"->str(n,"operator").equals("&&")?3:2;case "BinaryExpression"->switch(str(n,"operator")){case "*","/","%"->12;case "+","-"->11;case "<","<=",">",">=","in","instanceof"->9;case "==","!=","===","!=="->8;default->7;};default->20;};}
    static Doc expression(Map<String,Object> n,int parent){Doc d=node(n);return precedence(n)<parent?concat(text("("),d,text(")")):d;}
    static void additive(Map<String,Object> n,java.util.List<Map<String,Object>> operands){if(str(n,"type").equals("BinaryExpression")&&str(n,"operator").equals("+")){additive(child(n,"left"),operands);operands.add(child(n,"right"));}else operands.add(n);}
    static Doc chain(Map<String,Object> n){ArrayList<Map<String,Object>> operands=new ArrayList<>();additive(n,operands);ArrayList<Doc> tail=new ArrayList<>();for(int i=1;i<operands.size();i++){tail.add(text(" +"));tail.add(LINE);tail.add(expression(operands.get(i),12));}return group(concat(expression(operands.get(0),11),indent(concat(tail))),60);}
    static Doc node(Map<String,Object> n){if(n.isEmpty())return text("");switch(str(n,"type")){
        case "Identifier":return text(n.get("name"));case "ThisExpression":return text("this");
        case "StringLiteral","NumericLiteral","BooleanLiteral","NullLiteral","RegExpLiteral":return literal(n);
        case "ArrayExpression","ArrayPattern":{java.util.List<Object> a=array(n,"elements");return a.isEmpty()?text("[]"):delimited("[","]",a,SOFT,false);}
        case "ObjectExpression":{java.util.List<Object> p=array(n,"properties");return p.isEmpty()?text("{}"):delimited("{","}",p,LINE,true);}
        case "ObjectProperty":return property(n);
        case "VariableDeclarator":return n.get("init")==null?node(child(n,"id")):concat(node(child(n,"id")),text(" = "),expression(child(n,"init"),0));
        case "SpreadElement":return concat(text("..."),node(child(n,"argument")));
        case "AssignmentPattern":return concat(node(child(n,"left")),text(" = "),node(child(n,"right")));
        case "MemberExpression":return flag(n,"computed")?concat(expression(child(n,"object"),20),text("["),expression(child(n,"property"),0),text("]")):concat(expression(child(n,"object"),20),text("."),node(child(n,"property")));
        case "CallExpression":{java.util.List<Object> a=array(n,"arguments");if(a.size()==1&&str(obj(a.get(0)),"type").equals("ArrowFunctionExpression")){Map<String,Object> arrow=obj(a.get(0));return group(concat(expression(child(n,"callee"),20),text("("),params(array(arrow,"params")),text(" =>"),indent(concat(LINE,expression(child(arrow,"body"),0))),ifBreak(text(",")),SOFT,text(")")));}return concat(expression(child(n,"callee"),20),args(a));}
        case "NewExpression":return concat(text("new "),expression(child(n,"callee"),20),args(array(n,"arguments")));
        case "BinaryExpression","LogicalExpression":{int pre=precedence(n);Map<String,Object> left=child(n,"left");String op=str(n,"operator");if(str(n,"type").equals("BinaryExpression")&&op.equals("+")&&str(left,"type").equals("BinaryExpression")&&str(left,"operator").equals("+"))return chain(n);
            int inc=op.equals("&&")||op.equals("||")?1:0;return group(concat(expression(left,pre),text(" "+op),indent(concat(LINE,expression(child(n,"right"),pre+inc)))));}
        case "UnaryExpression":return concat(text(str(n,"operator")+(str(n,"operator").equals("!")?"":" ")),expression(child(n,"argument"),20));
        case "AssignmentExpression":return concat(expression(child(n,"left"),2),text(" "+str(n,"operator")+" "),expression(child(n,"right"),1));
        case "ArrowFunctionExpression":return concat(params(array(n,"params")),text(" => "),expression(child(n,"body"),1));
        case "FunctionDeclaration":return concat(text((flag(n,"async")?"async ":"")+"function "),text(flag(n,"generator")?"*":""),node(child(n,"id")),params(array(n,"params")),text(" "),block(array(child(n,"body"),"body"),true));
        case "ClassDeclaration":return concat(text("class "),node(child(n,"id")),text(" "),node(child(n,"body")));
        case "ClassBody":return block(array(n,"body"),false);
        case "ClassMethod":return concat(text(flag(n,"static")?"static ":""),text(flag(n,"async")?"async ":""),text(flag(n,"generator")?"*":""),key(n),params(array(n,"params")),text(" "),block(array(child(n,"body"),"body"),true));
        default:return statement(n);
    }}
    static Doc statement(Map<String,Object> n){if(n.isEmpty())return text("");return switch(str(n,"type")){
        case "VariableDeclaration"->variable(n,true);
        case "ReturnStatement"->concat(text("return"),n.get("argument")==null?text(""):concat(text(" "),node(child(n,"argument"))),text(";"));
        case "ExpressionStatement"->concat(node(child(n,"expression")),text(";"));
        case "BlockStatement"->block(array(n,"body"),true);
        case "IfStatement"->{Map<String,Object> c=child(n,"consequent"),a=child(n,"alternate");if(str(c,"type").equals("BlockStatement"))yield concat(text("if ("),expression(child(n,"test"),0),text(") "),statement(c),a.isEmpty()?text(""):concat(text(" else "),statement(a)));
            Doc e=a.isEmpty()?text(""):indent(concat(LINE,text("else"),LINE,statement(a)));yield group(concat(text("if ("),expression(child(n,"test"),0),text(")"),indent(concat(LINE,statement(c))),e));}
        case "ForOfStatement"->concat(text("for ("),variable(child(n,"left"),false),text(" of "),node(child(n,"right")),text(") "),statement(child(n,"body")));
        case "FunctionDeclaration","ClassDeclaration"->node(n);
        case "ExportNamedDeclaration"->n.get("declaration")!=null?concat(text("export "),statement(child(n,"declaration"))):concat(text("export { "),join(text(", "),docs(array(n,"specifiers"),false)),text(" };"));
        case "ExportSpecifier"->{Map<String,Object> l=child(n,"local"),e=child(n,"exported");yield str(l,"name").equals(str(e,"name"))?node(l):concat(node(l),text(" as "),node(e));}
        default->throw new IllegalArgumentException("unsupported AST node: "+str(n,"type"));
    };}
    static Map<String,Object> prepare(){return obj(JsonData.load("prettier_ast.json"));}
    static String work(Map<String,Object> n){String output="";for(int i=0;i<256;i++)output=render(concat(join(HARD,docs(array(n,"body"),true)),HARD),new State(),false);return output;}
    static long checksum(String s){long sum=0;for(int i=0;i<s.length();i++)sum=(sum*31+s.charAt(i))%1000000007;return sum;}
    static void run(){NativeBench.prepared(Pretty::prepare,Pretty::work,v->NativeBench.check(checksum(v)==56483873),v->{System.out.print(v);System.out.println("prettier_ast: CHECKSUM:"+checksum(v));});}
}
