%% Compiler state is prepared outside timing; jq names resolve to frame slots.
-module(jq_compile).
-export([program/1]).
-include("jq.hrl").
-record(ctx,{id,parent=-1,level=0,fn,scope=[],locals=0}).
get(T,K)->ets:lookup_element(T,K,2).
set(T,K,V)->ets:insert(T,{K,V}).
context(T,C)->get(T,{ctx,C}).
store(T,C)->set(T,{ctx,C#ctx.id},C).
emit(T,Values)->At=get(T,code_count),lists:foldl(fun(V,I)->set(T,{code,I},V),I+1 end,At,Values),set(T,code_count,At+length(Values)),At.
patch(T,At)->set(T,{code,At},get(T,code_count)).
constant(T,V)->I=get(T,const_count),set(T,{const,I},V),set(T,const_count,I+1),I.
function(T,Params)->I=get(T,fn_count),set(T,{fn,I},#jq_fn{params=Params}),set(T,fn_count,I+1),I.
newctx(T,Parent,Fn)->I=get(T,ctx_count),Level=case Parent of -1->0;_->(context(T,Parent))#ctx.level+1 end,store(T,#ctx{id=I,parent=Parent,level=Level,fn=Fn}),set(T,ctx_count,I+1),I.
local(T,C)->X=context(T,C),I=X#ctx.locals,store(T,X#ctx{locals=I+1}),I.
push(T,C,Kind,Name,Arity,Index)->X=context(T,C),store(T,X#ctx{scope=[{Kind,Name,Arity,Index}|X#ctx.scope]}).
scope(T,C)->(context(T,C))#ctx.scope.
restore_scope(T,C,S)->X=context(T,C),store(T,X#ctx{scope=S}).
lookup(T,C,Name,Kind,Arity)->lookup(T,C,C,Name,Kind,Arity).
lookup(_,_,-1,_,_,_)->none;
lookup(T,Original,C,Name,Kind,Arity)->X=context(T,C),Matches=[{K,I}||{K,N,A,I}<-X#ctx.scope,N=:=Name,((Kind=:=any andalso (K=:=2 orelse K=:=3)) orelse K=:=Kind),((K=:=3 andalso Arity=:=0)orelse A=:=Arity)],
    case Matches of [{K,I}|_]->{K,I,(context(T,Original))#ctx.level-X#ctx.level};_->lookup(T,Original,X#ctx.parent,Name,Kind,Arity)end.
variable(T,C,Name)->case lookup(T,C,Name,1,0)of {1,Slot,Hops}->{Hops,Slot};none->error({undefined_jq_variable,Name})end.
sub(T,C,N)->emit(T,[6]),compile(T,C,N,false),emit(T,[7]),ok.
finish_fn(T,C,Fn)->F=get(T,{fn,Fn}),set(T,{fn,Fn},F#jq_fn{locals=(context(T,C))#ctx.locals}).
entry(T,Fn)->F=get(T,{fn,Fn}),set(T,{fn,Fn},F#jq_fn{entry=get(T,code_count)}).
closure(T,C,Body)->emit(T,[13]),Jump=emit(T,[0]),Fn=function(T,0),Inner=newctx(T,C,Fn),entry(T,Fn),compile(T,Inner,Body,true),emit(T,[32]),finish_fn(T,Inner,Fn),patch(T,Jump),Fn.
call(Name,Args)->#n{kind=26,name=Name,list=Args}.
lit(V)->#n{kind=3,value=V}.
named(K,Name,A,B)->#n{kind=K,name=Name,a=A,b=B}.
choose(true,A,_)->A;choose(false,_,B)->B.
call(T,C,N,Tail)->Name=N#n.name,Args=N#n.list,Arity=length(Args),case lookup(T,C,Name,any,Arity)of
    {3,Index,Hops}->emit(T,[choose(Tail,31,30),Hops,Index]);
    {2,Index,Hops}->Closures=[closure(T,C,A)||A<-Args],emit(T,[choose(Tail,29,28),Index,Hops,Arity]++Closures);
    _->native_call(T,C,Name,Args,Arity,Tail)end,ok.
native_call(T,_,<<"empty">>,[],0,_)->emit(T,[17]);
native_call(T,_,Name,[],0,_)when Name=:= <<"true">>;Name=:= <<"false">>;Name=:= <<"null">>->Value=case Name of <<"true">>->true;<<"false">>->false;_->null end,emit(T,[3,constant(T,Value)]);
native_call(T,C,<<"path">>,[Arg],1,_)->emit(T,[25]),compile(T,C,Arg,false),emit(T,[26]);
native_call(T,C,<<"range">>,Args,Arity,_)when Arity=:=2;Arity=:=3->lists:foreach(fun(A)->sub(T,C,A)end,Args),emit(T,[24,Arity]);
native_call(T,C,Name,Args,Arity,_)->Defs=[{<<"length">>,0},{<<"not">>,0},{<<"type">>,0},{<<"keys">>,0},{<<"keys_unsorted">>,0},{<<"has">>,1},{<<"contains">>,1},{<<"tostring">>,0},{<<"tojson">>,0},{<<"fromjson">>,0},{<<"tonumber">>,0},{<<"ascii_upcase">>,0},{<<"ascii_downcase">>,0},{<<"explode">>,0},{<<"implode">>,0},{<<"split">>,1},{<<"join">>,1},{<<"add">>,0},{<<"flatten">>,0},{<<"floor">>,0},{<<"ceil">>,0},{<<"min">>,0},{<<"max">>,0},{<<"unique">>,0},{<<"sort">>,0},{<<"reverse">>,0},{<<"getpath">>,1},{<<"setpath">>,2},{<<"delpaths">>,1},{<<"from_entries">>,0},{<<"_sort_by_impl">>,1},{<<"_group_by_impl">>,1},{<<"error">>,0},{<<"error">>,1}],
    case [I||{I,D}<-lists:zip(lists:seq(1,length(Defs)),Defs),D=:={Name,Arity}]of []->error({undefined_jq_function,Name,Arity});[Id|_]->lists:foreach(fun(A)->sub(T,C,A)end,Args),emit(T,[27,Id,Arity])end.
definition(T,C,N,Tail)->Params=N#n.list,Count=length(Params),Fn=function(T,Count),push(T,C,2,N#n.name,Count,Fn),emit(T,[13]),Jump=emit(T,[0]),Inner=newctx(T,C,Fn),entry(T,Fn),
    lists:foreach(fun({P,I})->push(T,Inner,3,P#n.name,0,I)end,lists:zip(Params,lists:seq(0,Count-1))),
    Body=lists:foldr(fun({P,IsVar},B)->case IsVar of true->named(21,P#n.name,call(P#n.name,[]),B);false->B end end,N#n.a,lists:zip(Params,N#n.pvar)),
    compile(T,Inner,Body,true),emit(T,[32]),finish_fn(T,Inner,Fn),patch(T,Jump),compile(T,C,N#n.b,Tail).
compile(_,_,#n{kind=1},_)->ok;
compile(T,C,#n{kind=2},Tail)->call(T,C,call(<<"recurse">>,[]),Tail);
compile(T,_,#n{kind=3,value=V},_)->emit(T,[3,constant(T,V)]),ok;
compile(T,C,#n{kind=27,name=Name},_)->{H,S}=variable(T,C,Name),emit(T,[20,H,S]),ok;
compile(T,C,#n{kind=4,a=A,b=#n{kind=3,value=K}},_)->compile(T,C,A,false),emit(T,[9,constant(T,K)]),ok;
compile(T,C,#n{kind=4,a=A,b=B},_)->sub(T,C,B),compile(T,C,A,false),emit(T,[8]),ok;
compile(T,C,#n{kind=5,a=A},_)->compile(T,C,A,false),emit(T,[10]),ok;
compile(T,C,#n{kind=6,a=A,b=B,c=D},_)->sub(T,C,choose(B=:=nil,lit(null),B)),sub(T,C,choose(D=:=nil,lit(null),D)),compile(T,C,A,false),emit(T,[11]),ok;
compile(T,C,#n{kind=7,a=A,b=B},Tail)->emit(T,[33]),Handler=emit(T,[0]),compile(T,C,A,false),emit(T,[34,13]),End=emit(T,[0]),patch(T,Handler),case B of nil->emit(T,[17]);_->compile(T,C,B,Tail)end,patch(T,End),ok;
compile(T,_,#n{kind=8,a=nil},_)->emit(T,[4]),ok;
compile(T,C,#n{kind=8,a=A},_)->Slot=local(T,C),emit(T,[1,4,18,0,Slot,12]),Done=emit(T,[0]),compile(T,C,A,false),emit(T,[22,0,Slot,17]),patch(T,Done),emit(T,[21,0,Slot]),ok;
compile(T,C,#n{kind=9,list=Pairs},_)->emit(T,[5]),object_pairs(T,C,Pairs),emit(T,[2]),ok;
compile(T,C,#n{kind=10,a=A},_)->compile(T,C,A,false),emit(T,[38]),ok;
compile(T,C,#n{kind=11,a=A,b=B,op=Op},_)->sub(T,C,B),sub(T,C,A),emit(T,[37,Op]),ok;
compile(T,C,#n{kind=12,a=A,b=B},_)->sub(T,C,A),emit(T,[15]),No=emit(T,[0]),compile(T,C,B,false),emit(T,[39,13]),End=emit(T,[0]),patch(T,No),emit(T,[3,constant(T,false)]),patch(T,End),ok;
compile(T,C,#n{kind=13,a=A,b=B},_)->sub(T,C,A),emit(T,[15]),Other=emit(T,[0]),emit(T,[3,constant(T,true),13]),End=emit(T,[0]),patch(T,Other),compile(T,C,B,false),emit(T,[39]),patch(T,End),ok;
compile(T,C,#n{kind=14,a=A,b=B},Tail)->Found=local(T,C),emit(T,[1,3,constant(T,false),18,0,Found,12]),Other=emit(T,[0]),emit(T,[33]),Skip=emit(T,[0]),compile(T,C,A,false),emit(T,[34,16]),Skip2=emit(T,[0]),emit(T,[1,3,constant(T,true),18,0,Found,13]),End=emit(T,[0]),patch(T,Skip),patch(T,Skip2),emit(T,[17]),patch(T,Other),emit(T,[1,20,0,Found,14]),Run=emit(T,[0]),emit(T,[17]),patch(T,Run),compile(T,C,B,Tail),patch(T,End),ok;
compile(T,C,#n{kind=Kind,a=A,b=B},Tail)when Kind=:=15;Kind=:=16->call(T,C,call(choose(Kind=:=15,<<"_assign">>,<<"_modify">>),[A,B]),Tail);
compile(T,C,#n{kind=Kind,a=A,b=B,op=Op},Tail)when Kind=:=17;Kind=:=18->Id=get(T,synth)+1,set(T,synth,Id),Name= <<"__rhs",(integer_to_binary(Id))/binary>>,V=named(27,Name,nil,nil),Identity=#n{kind=1},Update=case Kind of 17->#n{kind=11,op=Op,a=Identity,b=V};18->#n{kind=14,a=Identity,b=V}end,compile(T,C,named(21,Name,B,call(<<"_modify">>,[A,Update])),Tail);
compile(T,C,#n{kind=19,a=A,b=B},Tail)->compile(T,C,A,false),compile(T,C,B,Tail);
compile(T,C,#n{kind=20,a=A,b=B},Tail)->emit(T,[12]),Second=emit(T,[0]),compile(T,C,A,Tail),emit(T,[13]),End=emit(T,[0]),patch(T,Second),compile(T,C,B,Tail),patch(T,End),ok;
compile(T,C,#n{kind=21,name=Name,a=A,b=B},Tail)->Slot=local(T,C),sub(T,C,A),emit(T,[19,0,Slot]),Saved=scope(T,C),push(T,C,1,Name,0,Slot),compile(T,C,B,Tail),restore_scope(T,C,Saved);
compile(T,C,#n{kind=22,name=Name,a=A,b=B,c=D},_)->Acc=local(T,C),X=local(T,C),emit(T,[1]),compile(T,C,B,false),emit(T,[18,0,Acc,12]),End=emit(T,[0]),emit(T,[1]),compile(T,C,A,false),emit(T,[18,0,X,21,0,Acc]),Saved=scope(T,C),push(T,C,1,Name,0,X),compile(T,C,D,false),restore_scope(T,C,Saved),emit(T,[18,0,Acc,17]),patch(T,End),emit(T,[21,0,Acc]),ok;
compile(T,C,#n{kind=23,name=Name,a=A,b=B,c=D,d=Extract},Tail)->Acc=local(T,C),X=local(T,C),emit(T,[1]),compile(T,C,B,false),emit(T,[18,0,Acc,1]),compile(T,C,A,false),emit(T,[18,0,X,20,0,Acc]),Saved=scope(T,C),push(T,C,1,Name,0,X),compile(T,C,D,false),emit(T,[1,18,0,Acc]),case Extract of nil->ok;_->compile(T,C,Extract,Tail)end,restore_scope(T,C,Saved);
compile(T,C,#n{kind=24,a=A,b=B,c=D},Tail)->sub(T,C,A),emit(T,[15]),Other=emit(T,[0]),compile(T,C,B,Tail),emit(T,[13]),End=emit(T,[0]),patch(T,Other),case D of nil->ok;_->compile(T,C,D,Tail)end,patch(T,End),ok;
compile(T,C,N=#n{kind=25},Tail)->Saved=scope(T,C),definition(T,C,N,Tail),restore_scope(T,C,Saved);
compile(T,C,N=#n{kind=26},Tail)->call(T,C,N,Tail);
compile(T,C,#n{kind=28,name=Name,a=A},_)->Slot=local(T,C),emit(T,[35,0,Slot]),Saved=scope(T,C),push(T,C,1,Name,0,Slot),compile(T,C,A,false),restore_scope(T,C,Saved),emit(T,[34]),ok;
compile(T,C,#n{kind=29,name=Name},_)->{H,S}=variable(T,C,Name),emit(T,[36,H,S]),ok;
compile(_,_,N,_)->error({unsupported_jq_construct,N}).
object_pairs(_,_,[])->ok;
object_pairs(T,C,[K,V|Rest])->sub(T,C,K),sub(T,C,V),emit(T,[23]),object_pairs(T,C,Rest).
prelude()-> <<"\ndef select(f): if f then . else empty end;\ndef recurse(f): def r: ., (f | r); r;\ndef recurse: recurse(.[]?);\ndef map(f): [.[] | f];\ndef to_entries: [keys_unsorted[] as $k | {key: $k, value: .[$k]}];\ndef with_entries(f): to_entries | map(f) | from_entries;\ndef paths: path(..) | select(length > 0);\ndef paths(node_filter): . as $dot | paths | select(. as $p | $dot | getpath($p) | node_filter);\ndef del(f): delpaths([path(f)]);\ndef _assign(paths; $value): reduce path(paths) as $p (.; setpath($p; $value));\ndef _modify(paths; update): reduce path(paths) as $p (.; . as $x | label $out | (setpath($p; $x | getpath($p) | update) | ., break $out), delpaths([$p]));\ndef first(f): label $out | f | ., break $out;\ndef last(f): reduce f as $x (null; $x);\ndef limit($n; f): if $n > 0 then label $out | foreach f as $item (0; . + 1; $item, if . >= $n then break $out else empty end) elif $n == 0 then empty else f end;\ndef nth($n; f): if $n < 0 then error(\"Out of bounds negative array index\") else last(limit($n + 1; f)) end;\ndef repeat(f): def _repeat: f, _repeat; _repeat;\ndef until(cond; update): def _until: if cond then . else (update | _until) end; _until;\ndef first: .[0];\ndef last: .[-1];\ndef sort_by(f): _sort_by_impl(map([f]));\ndef group_by(f): _group_by_impl(map([f]));\ndef unique_by(f): [group_by(f)[] | .[0]];\ndef scalars: select(type | . != \"array\" and . != \"object\");\ndef objects: select(type == \"object\");\ndef arrays: select(type == \"array\");\ndef numbers: select(type == \"number\");\ndef strings: select(type == \"string\");\ndef range($x): range(0; $x);\ndef isempty(g): first((g | false), true);\ndef any: reduce .[] as $x (false; . or $x);\ndef all: reduce .[] as $x (true; . and $x);\n">>.
program(User)->Text= <<(prelude())/binary,"\n",User/binary>>,Ast=jq_parse:parse(Text),T=ets:new(jq_compiler,[set,private]),
    try lists:foreach(fun(K)->set(T,K,0)end,[code_count,const_count,fn_count,ctx_count,synth]),Fn=function(T,0),Top=newctx(T,-1,Fn),compile(T,Top,Ast,true),emit(T,[32]),finish_fn(T,Top,Fn),
        #program{code=list_to_tuple([get(T,{code,I})||I<-lists:seq(0,get(T,code_count)-1)]),constants=list_to_tuple([get(T,{const,I})||I<-lists:seq(0,get(T,const_count)-1)]),functions=list_to_tuple([get(T,{fn,I})||I<-lists:seq(0,get(T,fn_count)-1)])}
    after ets:delete(T)end.
