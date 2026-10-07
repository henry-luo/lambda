%% Native document algebra; AST objects remain ordinary decoded JSON maps.
-module(pretty).
-export([run/0]).
txt(V)when is_binary(V)->{text,V};txt(true)->{text,<<"true">>};txt(false)->{text,<<"false">>};txt(null)->{text,<<"null">>};txt(V)when is_integer(V)->{text,integer_to_binary(V)};txt(V)when is_float(V)->{text,float_to_binary(V,[short])}.
cat(Parts)->{concat,Parts}.
indent(D)->{indent,D}.
group(D)->{group,D,infinity}.
group(D,Limit)->{group,D,Limit}.
ifbreak(D)->{ifbreak,D,txt(<<>>)}.
join(_,[])->cat([]);
join(Sep,Parts)->cat(lists:join(Sep,Parts)).
flat({text,V})->byte_size(V);
flat({concat,Parts})->lists:foldl(fun(D,A)->case {flat(D),A}of {infinity,_}->infinity;{_,infinity}->infinity;{N,M}->N+M end end,0,Parts);
flat({indent,D})->flat(D);flat({group,D,_})->flat(D);flat({ifbreak,_,D})->flat(D);flat(line)->1;flat(softline)->0;flat(hardline)->infinity.
render({text,V},Column,_,_)->{V,Column+byte_size(V)};
render({concat,Parts},Column,Indent,Mode)->lists:mapfoldl(fun(D,C)->render(D,C,Indent,Mode)end,Column,Parts);
render({indent,D},Column,Indent,Mode)->render(D,Column,Indent+1,Mode);
render({group,D,Limit},Column,Indent,Mode)->Flat=Mode=:=flat orelse(Limit>=flat(D) andalso flat(D)=<80-Column),render(D,Column,Indent,case Flat of true->flat;false->break end);
render({ifbreak,B,F},Column,Indent,Mode)->render(case Mode of flat->F;break->B end,Column,Indent,Mode);
render(line,Column,_,flat)->{<<" ">>,Column+1};
render(softline,Column,_,flat)->{<<>>,Column};
render(Line,_,Indent,_)when Line=:=line;Line=:=softline;Line=:=hardline->Column=Indent*2,{[$\n,lists:duplicate(Column,$ )],Column}.
obj(null)->#{};obj(undefined)->#{};obj(M)->M.
get(N,K)->maps:get(K,N,undefined).
child(N,K)->obj(get(N,K)).
str(N,K)->case get(N,K)of undefined-> <<>>;V->V end.
flag(N,K)->get(N,K)=:=true.
array(N,K)->case get(N,K)of undefined->[];null->[];L->L end.
docs(Values,Kind)->[case Kind of node->node(obj(V));statement->statement(obj(V))end||V<-Values].
literal(N)->case str(N,<<"type">>)of <<"StringLiteral">>->txt(iolist_to_binary(json:encode(str(N,<<"value">>))));<<"NumericLiteral">>->txt(get(N,<<"value">>));<<"BooleanLiteral">>->txt(get(N,<<"value">>));<<"NullLiteral">>->txt(<<"null">>);<<"RegExpLiteral">>->txt(iolist_to_binary(["/",str(N,<<"pattern">>),"/",str(N,<<"flags">>)]))end.
key(N)->case flag(N,<<"computed">>)of true->cat([txt(<<"[">>),node(child(N,<<"key">>)),txt(<<"]">>)]);false->node(child(N,<<"key">>))end.
delimited(Open,Close,Values,Line,Inside)->Contents=join(cat([txt(<<",">>),line]),docs(Values,node)),case Inside of
    true->group(cat([txt(Open),indent(cat([Line,Contents,ifbreak(txt(<<",">>))])),Line,txt(Close)]));
    false->group(cat([txt(Open),indent(cat([Line,Contents])),ifbreak(txt(<<",">>)),Line,txt(Close)]))end.
params(Values)->delimited(<<"(">>,<<")">>,Values,softline,false).
args([])->txt(<<"()">>);
args(Values)->case lists:any(fun(V)->str(V,<<"type">>)=:= <<"ObjectExpression">>end,Values)of true->cat([txt(<<"(">>),join(txt(<<", ">>),docs(Values,node)),txt(<<")">>)]);false->params(Values)end.
block([],_) ->txt(<<"{}">>);
block(Body,Kind)->cat([txt(<<"{">>),indent(cat([hardline,join(hardline,docs(Body,Kind))])),hardline,txt(<<"}">>)]).
variable(N,Terminator)->D=cat([txt(<<(str(N,<<"kind">>))/binary," ">>),join(cat([txt(<<",">>),line]),docs(array(N,<<"declarations">>),node))]),case Terminator of true->cat([D,txt(<<";">>)]);false->D end.
property(N)->case str(N,<<"type">>)of <<"SpreadElement">>->cat([txt(<<"...">>),node(child(N,<<"argument">>))]);_->K=key(N),case flag(N,<<"shorthand">>)of true->K;false->cat([K,txt(<<": ">>),expression(child(N,<<"value">>),0)])end end.
precedence(N)when map_size(N)=:=0->100;
precedence(N)->case str(N,<<"type">>)of <<"AssignmentExpression">>->1;<<"ArrowFunctionExpression">>->1;<<"LogicalExpression">>->case str(N,<<"operator">>)of <<"&&">>->3;_->2 end;
    <<"BinaryExpression">>->case str(N,<<"operator">>)of O when O=:= <<"*">>;O=:= <<"/">>;O=:= <<"%">>->12;O when O=:= <<"+">>;O=:= <<"-">>->11;
        O when O=:= <<"<">>;O=:= <<"<=">>;O=:= <<">">>;O=:= <<">=">>;O=:= <<"in">>;O=:= <<"instanceof">>->9;
        O when O=:= <<"==">>;O=:= <<"!=">>;O=:= <<"===">>;O=:= <<"!==">>->8;_->7 end;_->20 end.
expression(N,Parent)->D=node(N),case precedence(N)<Parent of true->cat([txt(<<"(">>),D,txt(<<")">>)]);false->D end.
additive(N)->case str(N,<<"type">>)=:= <<"BinaryExpression">> andalso str(N,<<"operator">>)=:= <<"+">>of true->additive(child(N,<<"left">>))++[child(N,<<"right">>)];false->[N]end.
chain(N)->[First|Rest]=additive(N),group(cat([expression(First,11),indent(cat(lists:flatmap(fun(Operand)->[txt(<<" +">>),line,expression(Operand,12)]end,Rest)))]),60).
node(N)when map_size(N)=:=0->txt(<<>>);
node(N)->case str(N,<<"type">>)of
    <<"Identifier">>->txt(get(N,<<"name">>));<<"ThisExpression">>->txt(<<"this">>);
    T when T=:= <<"StringLiteral">>;T=:= <<"NumericLiteral">>;T=:= <<"BooleanLiteral">>;T=:= <<"NullLiteral">>;T=:= <<"RegExpLiteral">>->literal(N);
    T when T=:= <<"ArrayExpression">>;T=:= <<"ArrayPattern">>->case array(N,<<"elements">>)of []->txt(<<"[]">>);A->delimited(<<"[">>,<<"]">>,A,softline,false)end;
    <<"ObjectExpression">>->case array(N,<<"properties">>)of []->txt(<<"{}">>);A->delimited(<<"{">>,<<"}">>,A,line,true)end;
    <<"ObjectProperty">>->property(N);
    <<"VariableDeclarator">>->case get(N,<<"init">>)of V when V=:=null;V=:=undefined->node(child(N,<<"id">>));_->cat([node(child(N,<<"id">>)),txt(<<" = ">>),expression(child(N,<<"init">>),0)])end;
    <<"SpreadElement">>->cat([txt(<<"...">>),node(child(N,<<"argument">>))]);
    <<"AssignmentPattern">>->cat([node(child(N,<<"left">>)),txt(<<" = ">>),node(child(N,<<"right">>))]);
    <<"MemberExpression">>->case flag(N,<<"computed">>)of true->cat([expression(child(N,<<"object">>),20),txt(<<"[">>),expression(child(N,<<"property">>),0),txt(<<"]">>)]);false->cat([expression(child(N,<<"object">>),20),txt(<<".">>),node(child(N,<<"property">>))])end;
    <<"CallExpression">>->A=array(N,<<"arguments">>),case length(A)=:=1 andalso str(hd(A),<<"type">>)=:= <<"ArrowFunctionExpression">>of true->Arrow=hd(A),group(cat([expression(child(N,<<"callee">>),20),txt(<<"(">>),params(array(Arrow,<<"params">>)),txt(<<" =>">>),indent(cat([line,expression(child(Arrow,<<"body">>),0)])),ifbreak(txt(<<",">>)),softline,txt(<<")">>)]));false->cat([expression(child(N,<<"callee">>),20),args(A)])end;
    <<"NewExpression">>->cat([txt(<<"new ">>),expression(child(N,<<"callee">>),20),args(array(N,<<"arguments">>))]);
    T when T=:= <<"BinaryExpression">>;T=:= <<"LogicalExpression">>->Left=child(N,<<"left">>),O=str(N,<<"operator">>),
        case T=:= <<"BinaryExpression">> andalso O=:= <<"+">> andalso str(Left,<<"type">>)=:= <<"BinaryExpression">> andalso str(Left,<<"operator">>)=:= <<"+">>of true->chain(N);false->P=precedence(N),Inc=case O of <<"&&">>->1;<<"||">>->1;_->0 end,group(cat([expression(Left,P),txt(<<" ",O/binary>>),indent(cat([line,expression(child(N,<<"right">>),P+Inc)]))]))end;
    <<"UnaryExpression">>->O=str(N,<<"operator">>),Suffix=case O of <<"!">>-> <<>>;_-> <<" ">>end,cat([txt(<<O/binary,Suffix/binary>>),expression(child(N,<<"argument">>),20)]);
    <<"AssignmentExpression">>->O=str(N,<<"operator">>),cat([expression(child(N,<<"left">>),2),txt(<<" ",O/binary," ">>),expression(child(N,<<"right">>),1)]);
    <<"ArrowFunctionExpression">>->cat([params(array(N,<<"params">>)),txt(<<" => ">>),expression(child(N,<<"body">>),1)]);
    <<"FunctionDeclaration">>->Prefix=case flag(N,<<"async">>)of true-> <<"async function ">>;false-> <<"function ">>end,Gen=case flag(N,<<"generator">>)of true-> <<"*">>;false-> <<>>end,cat([txt(Prefix),txt(Gen),node(child(N,<<"id">>)),params(array(N,<<"params">>)),txt(<<" ">>),block(array(child(N,<<"body">>),<<"body">>),statement)]);
    <<"ClassDeclaration">>->cat([txt(<<"class ">>),node(child(N,<<"id">>)),txt(<<" ">>),node(child(N,<<"body">>))]);
    <<"ClassBody">>->block(array(N,<<"body">>),node);
    <<"ClassMethod">>->Static=case flag(N,<<"static">>)of true-> <<"static ">>;false-> <<>>end,Async=case flag(N,<<"async">>)of true-> <<"async ">>;false-> <<>>end,Gen=case flag(N,<<"generator">>)of true-> <<"*">>;false-> <<>>end,cat([txt(Static),txt(Async),txt(Gen),key(N),params(array(N,<<"params">>)),txt(<<" ">>),block(array(child(N,<<"body">>),<<"body">>),statement)]);
    _->statement(N)end.
statement(N)when map_size(N)=:=0->txt(<<>>);
statement(N)->case str(N,<<"type">>)of
    <<"VariableDeclaration">>->variable(N,true);
    <<"ReturnStatement">>->Argument=case get(N,<<"argument">>)of V when V=:=undefined;V=:=null->txt(<<>>);_->cat([txt(<<" ">>),node(child(N,<<"argument">>))])end,cat([txt(<<"return">>),Argument,txt(<<";">>)]);
    <<"ExpressionStatement">>->cat([node(child(N,<<"expression">>)),txt(<<";">>)]);
    <<"BlockStatement">>->block(array(N,<<"body">>),statement);
    <<"IfStatement">>->C=child(N,<<"consequent">>),A=child(N,<<"alternate">>),case str(C,<<"type">>)of <<"BlockStatement">>->Else=case map_size(A)of 0->txt(<<>>);_->cat([txt(<<" else ">>),statement(A)])end,cat([txt(<<"if (">>),expression(child(N,<<"test">>),0),txt(<<") ">>),statement(C),Else]);
        _->Else=case map_size(A)of 0->txt(<<>>);_->indent(cat([line,txt(<<"else">>),line,statement(A)]))end,group(cat([txt(<<"if (">>),expression(child(N,<<"test">>),0),txt(<<")">>),indent(cat([line,statement(C)])),Else]))end;
    <<"ForOfStatement">>->cat([txt(<<"for (">>),variable(child(N,<<"left">>),false),txt(<<" of ">>),node(child(N,<<"right">>)),txt(<<") ">>),statement(child(N,<<"body">>))]);
    T when T=:= <<"FunctionDeclaration">>;T=:= <<"ClassDeclaration">>->node(N);
    <<"ExportNamedDeclaration">>->case get(N,<<"declaration">>)of V when V=:=null;V=:=undefined->cat([txt(<<"export { ">>),join(txt(<<", ">>),docs(array(N,<<"specifiers">>),node)),txt(<<" };">>)]);_->cat([txt(<<"export ">>),statement(child(N,<<"declaration">>))])end;
    <<"ExportSpecifier">>->L=child(N,<<"local">>),E=child(N,<<"exported">>),case str(L,<<"name">>)=:=str(E,<<"name">>)of true->node(L);false->cat([node(L),txt(<<" as ">>),node(E)])end;
    T->error({unsupported_ast_node,T})end.
prepare()->{ok,B}=file:read_file("test/benchmark/text/prettier_ast.json"),json:decode(B).
work(N)->lists:foldl(fun(_,_) ->{Output,_}=render(cat([join(hardline,docs(array(N,<<"body">>),statement)),hardline]),0,0,break),iolist_to_binary(Output)end,<<>>,lists:seq(1,256)).
checksum(S)->lists:foldl(fun(C,A)->(A*31+C)rem 1000000007 end,0,binary_to_list(S)).
run()->native_bench:prepared(fun prepare/0,fun work/1,fun(S)->native_bench:check(checksum(S)=:=56483873)end,fun(S)->io:put_chars(S),io:format("prettier_ast: CHECKSUM:~B~n",[checksum(S)])end,fun(_)->ok end).
