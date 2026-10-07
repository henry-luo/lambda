%% Native value adapters. Mutable benchmark objects have process-local handles;
%% only generated native functions execute the workload, never an AST evaluator.
-module(pr).
-compile(export_all).
-compile(nowarn_export_all).
-compile({no_auto_import,[get/1,put/2,length/1]}).

new(Kind,Data)-> charge(Kind,Data), I=erlang:get(next_id),erlang:put(next_id,I+1),erlang:put({h,I},{Kind,Data}),
    case erlang:get(temps) of [T|Rest]->erlang:put(temps,[[{h,I}|T]|Rest]);_->ok end,{h,I}.
val({h,I})->case erlang:get({h,I}) of undefined->error({dead_handle,I});V->V end.
store({h,I},Kind,Data)->erlang:put({h,I},{Kind,Data}),ok.
env(Parent)->new(env,#{parent=>Parent,values=>#{},loaded=>#{}}).
getv(_E,<<"nothing">>)->nil;
getv(_E,<<"undef">>)->nil;
getv(_E,<<"true">>)->true;
getv(_E,<<"false">>)->false;
getv(_E,<<"Inf">>)->pos_infinity;
getv(nil,<<"NaN">>)->nan;
getv(nil,<<"pi">>)->math:pi();
getv(nil,N)->{builtin,N};
getv(E,N)->{env,D}=val(E),case maps:find(N,maps:get(values,D))of {ok,V}->V;error->getv(maps:get(parent,D),N)end.
bind(E,N,V)->{env,D}=val(E),store(E,env,D#{values:=maps:put(N,V,maps:get(values,D))}),V.
setv(E,N,V)->case owner(E,N)of nil->bind(E,N,V);At->bind(At,N,V)end.
owner(nil,_)->nil;
owner(E,N)->{env,D}=val(E),case maps:is_key(N,maps:get(values,D))of true->E;false->owner(maps:get(parent,D),N)end.
define(E,N,F)->Old=getv(E,N),G=case Old of {group,_,T,Fs}->{group,N,T,Fs++[F]};_->{group,N,nil,[F]}end,bind(E,N,G).
deftype(E,N,P,Fields,Defaults)->Type={type,N,P,Fields,Defaults},bind(E,<<"@type:",N/binary>>,Type),bind(E,N,{group,N,Type,[]}).
arr(A)->new(arr,array:from_list(expand(A))).
tup(A)->{tuple,expand(A)}.
keywords(A)->{kw,maps:from_list([list_to_tuple(elements(V))||V<-A])}.
expand(A)->lists:append([case V of {spread,X}->elements(X);_ ->[V]end||V<-A]).
arg(A,I,Default)->case erlang:length(A)>I of true->lists:nth(I+1,A);false->Default()end.
kwarg(A,N,Default)->case [maps:get(N,K)||{kw,K}<-A,maps:is_key(N,K)]of [V|_]->V;_->Default()end.
%% Ordinary positional calls need neither expansion nor keyword reordering.
normalize(A)->case lists:any(fun({spread,_})->true;({kw,_})->true;(_)->false end,A)of
    false->A;
    true->{Values,Kws}=lists:partition(fun(X)->not is_kw(X)end,expand(A)),Values++Kws
end.
is_kw({kw,_})->true;is_kw(_)->false.
call(E,{builtin,N},A)->builtin(E,N,normalize(A));
call(_E,{fn,C,F,_Ts,_Min,_Max},A)->invoke(C,F,normalize(A));
call(E,{group,Name,T,Fs},A0)->A=normalize(A0),Count=erlang:length([X||X<-A,not is_kw(X)]),
    Candidates=[{score(Ts),F}||F={fn,C,_,Ts,Min,Max}<-Fs,Count>=Min,(Max<0 orelse Count=<Max),match_types(C,A,Ts)],
    case {T,A,Candidates}of
        {{type,_,_,_,_},[{atom,<<"raw">>}],_}->object(T,[]);
        %% The group's arguments are already normalized; enter its body once.
        {_,_,[_|_]}->{_,{fn,C,F,_,_,_}}=lists:last(lists:keysort(1,Candidates)),invoke(C,F,A);
        {{type,_,_,_,_},_,_}->object(T,A);
        _->builtin(E,Name,A)
    end;
call(_,F,A)->error({not_callable,F,A}).
score(Ts)->erlang:length([T||T<-Ts,T=/=<<"Any">>]).
match_types(_,_,[])->true;
match_types(_,[],_)->true;
match_types(E,[A|As],[T|Ts])->isa(E,A,T)andalso match_types(E,As,Ts).
invoke(C,F,A)->with_frame(env(C),F,A).
with_frame(E,F,A)->Roots=erlang:get(roots),Temps=erlang:get(temps),erlang:put(roots,[E|Roots]),erlang:put(temps,[[]|Temps]),
    %% Recursive workloads need collection even when they contain no loop boundary.
    %% Retain the returned value in its caller before reclaiming expired frames.
    try F(E,A) of V -> retain(V,Temps),maybe_collect(),V catch C:R:S->erlang:put(temps,Temps),erlang:raise(C,R,S) after erlang:put(roots,Roots) end.
retain(V,[])->erlang:put(temps,[]),V;
retain(V,[T|Ts])->erlang:put(temps,[[V|T]|Ts]),V.
%% Expression blocks share their lexical scope and only need temporary roots.
eval_same(E,F)->with_frame(E,fun(Scope,_)->F(Scope)end,[]).
eval(E,F)->invoke(E,fun(Child,_)->F(Child)end,[]).
object({type,N,P,Fields,Defaults},A)->D=maps:from_list(lists:zip(Fields,[clone_default(V)||V<-Defaults])),
    {Vals,Kws}=lists:partition(fun(X)->not is_kw(X)end,A),Base=maps:merge(D,maps:from_list(lists:zip(lists:sublist(Fields,erlang:length(Vals)),Vals))),
    All=lists:foldl(fun({kw,K},Acc)->maps:merge(Acc,K)end,Base,Kws),new(obj,{N,P,All}).
clone_default({h,_}=H)->case val(H)of {arr,A}->new(arr,A);{dict,M}->new(dict,M);_->H end;clone_default(V)->V.
isa(_E,_X,<<"Any">>)->true;
isa(E,X,T)->case binary:split(T,<<"|">>,[global])of [_]->isa1(E,X,T);Ts->lists:any(fun(S)->isa(E,X,S)end,Ts)end.
isa1(_,nil,<<"Nothing">>)->true;
isa1(_,X,<<"Bool">>)->is_boolean(X);
isa1(_,X,T)when T=:=<<"Number">>;T=:=<<"Real">>->is_number(X)orelse is_boolean(X);
isa1(_,X,T)when T=:=<<"Integer">>;T=:=<<"Int">>;T=:=<<"Int32">>;T=:=<<"UInt32">>;T=:=<<"UInt8">>->is_integer(X);
isa1(_,X,T)when T=:=<<"Float64">>;T=:=<<"AbstractFloat">>->is_float(X);
isa1(_,X,T)when T=:=<<"String">>;T=:=<<"AbstractString">>->is_binary(X);
isa1(_,{atom,_},<<"Symbol">>)->true;
isa1(_,{char,_},<<"Char">>)->true;
isa1(_,{tuple,_},<<"Tuple">>)->true;
isa1(_,X,T)when T=:=<<"AbstractArray">>;T=:=<<"AbstractVector">>;T=:=<<"Vector">>->kind(X)=:=arr;
isa1(_,X,T)when T=:=<<"AbstractDict">>;T=:=<<"Dict">>->kind(X)=:=dict;
isa1(_,X,<<"Function">>)->is_tuple(X)andalso lists:member(element(1,X),[fn,group,builtin]);
isa1(E,X,T)->case kind(X)of obj->{obj,{N,P,_}}=val(X),type_is(E,N,P,T);_->false end.
type_is(_,N,_,N)->true;
type_is(_,_,<<"Any">>,_)->false;
type_is(E,_,P,T)->case getv(E,<<"@type:",P/binary>>)of {type,N,Next,_,_}->type_is(E,N,Next,T);_->P=:=T end.
kind({h,_}=H)->element(1,val(H));kind(_)->none.
truth(nil)->false;truth(false)->false;truth(_)->true.
truth0(X)->truth(X)andalso X=/=0 andalso X=/=0.0 andalso X=/=<<>> andalso case kind(X)of arr->len(X)>0;dict->len(X)>0;_->true end.
check(X)->case truth(X)of true->nil;false->error(benchmark_check_failed)end.
int({u32,N})->N;int(true)->1;int(false)->0;int({char,B})->hd(unicode:characters_to_list(B));int(N)when is_integer(N)->N;int(N)when is_float(N)->trunc(N);int(B)when is_binary(B)->binary_to_integer(B).
num({u32,N})->N;num(true)->1;num(false)->0;num(X)->X.
text(nil)-><<"nothing">>;text(true)-><<"true">>;text(false)-><<"false">>;text({char,B})->B;text({atom,B})->B;
text({ascii,B})->B;
text(B)when is_binary(B)->B;text(N)when is_integer(N)->integer_to_binary(N);text(N)when is_float(N)->float_to_binary(N,[short]);text(V)->iolist_to_binary(io_lib:format("~p",[V])).
strcat(A)->iolist_to_binary([text(X)||X<-A]).
elements({ascii,B})->elements(B);
elements(B)when is_binary(B)->[{char,unicode:characters_to_binary([C])}||C<-unicode:characters_to_list(B)];
elements({tuple,A})->A;
elements({range,S,End,Step})->seq(S,End,Step,[]);
elements({h,_}=H)->case val(H)of {arr,A}->array:to_list(A);{dict,M}->[tup([K,V])||{K,V}<-maps:to_list(M)]end.
seq(S,End,Step,Acc)when (Step>0 andalso S>=End)orelse(Step<0 andalso S=<End)->lists:reverse(Acc);
seq(S,End,Step,Acc)->seq(S+Step,End,Step,[S|Acc]).
len({ascii,B})->byte_size(B);
len(B)when is_binary(B)->erlang:length(unicode:characters_to_list(B));
len({tuple,A})->erlang:length(A);
len({h,_}=H)->case val(H)of {arr,A}->array:size(A);{dict,M}->maps:size(M);{buffer,B}->iolist_size(B)end.
nth({tuple,A},I)->lists:nth(I+1,A);nth(H,I)->index(H,[I+1]).
index({type,_},I)->arr(I);
index({builtin,_},I)->arr(I);
index({group,_,_,_},I)->arr(I);
index(X,[])->index(X,[1]);
index({ascii,B},[I])when is_integer(I)->{char,<< (binary:at(B,I-1)) >>};
index(B,[I])when is_binary(B),is_integer(I)->{char,unicode:characters_to_binary([lists:nth(I,unicode:characters_to_list(B))])};
index(B,[R={range,_,_,_}])when is_binary(B)->strcat([index(B,[I])||I<-elements(R)]);
index({tuple,A},[I])when is_integer(I)->lists:nth(I,A);
index({tuple,A},[R])->tup([lists:nth(I,A)||I<-elements(R)]);
index({h,_}=H,[I])->case val(H)of {dict,M}->maps:get(I,M,nil);{arr,A}->case is_integer(I)of true->array:get(I-1,A);false->arr([array:get(N-1,A)||N<-elements(I)])end end.
putindex(H,[],V)->putindex(H,[1],V);
putindex(H,[I],V)->case val(H)of {dict,M}->store(H,dict,maps:put(I,V,M));{arr,A}->store(H,arr,array:set(I-1,V,A))end,V.
field({h,_}=H,N)->case val(H)of {obj,{_,_,M}}->maps:get(N,M,nil);{env,_}->getv(H,N)end;
field({builtin,B},N)->{builtin,<<B/binary,".",N/binary>>};
field(X,<<"first">>)->nth(X,0);field(X,<<"second">>)->nth(X,1).
putfield(H,N,V)->{obj,{T,P,M}}=val(H),store(H,obj,{T,P,maps:put(N,V,M)}),V.
equal(nan,_)->false;equal(_,nan)->false;
equal(A,B)when is_number(A),is_number(B)->A==B;
equal({h,_}=A,{h,_}=B)->case {val(A),val(B)}of {{arr,X},{arr,Y}}->equal_lists(array:to_list(X),array:to_list(Y));{{dict,X},{dict,Y}}->maps:keys(X)=:=maps:keys(Y)andalso lists:all(fun(K)->equal(maps:get(K,X),maps:get(K,Y))end,maps:keys(X));_->A=:=B end;
equal({tuple,A},{tuple,B})->equal_lists(A,B);equal(A,B)->A=:=B.
equal_lists([],[])->true;equal_lists([A|As],[B|Bs])->equal(A,B)andalso equal_lists(As,Bs);equal_lists(_,_)->false.
cmp(pos_infinity,pos_infinity)->0;cmp(neg_infinity,neg_infinity)->0;cmp(pos_infinity,_)->1;cmp(_,pos_infinity)->-1;cmp(neg_infinity,_)->-1;cmp(_,neg_infinity)->1;
cmp(A,B)when is_number(A),is_number(B)->if A<B ->-1;A>B->1;true->0 end;
cmp(A,B)->X=case A of {char,C}->C;_->A end,Y=case B of {char,D}->D;_->B end,if X<Y->-1;X>Y->1;true->0 end.
%% Erlang rejects IEEE non-finite values; preserve them symbolically for rays
%% whose omitted bounds are NaN, and for parallel intersection divisions.
op(N,[A,B])when (A=:=nan orelse B=:=nan),(N=:=<<"<">> orelse N=:=<<">">> orelse N=:=<<"<=">> orelse N=:=<<">=">>)->false;
op(<<"-">>,[nan])->nan;
op(<<"-">>,[H={h,_}])->arr([op(<<"-">>,[V])||V<-elements(H)]);
op(N,[A,B])when (A=:=nan orelse B=:=nan),(N=:=<<"+">> orelse N=:=<<"-">> orelse N=:=<<"*">> orelse N=:=<<"/">>)->nan;
op(<<"/">>,[A,B])when is_number(A),B==0->case A==0 of true->nan;false->infinity(signbit(A)bxor signbit(B))end;
op(N,[A,B])when A=:=pos_infinity;A=:=neg_infinity;B=:=pos_infinity;B=:=neg_infinity->infinite_op(N,A,B);
op(<<"%">>,[A,{builtin,<<"UInt32">>}])->{u32,int(A)band 16#ffffffff};
op(N,[{u32,A},{u32,B}])->{u32,int(op(N,[A,B]))band 16#ffffffff};
op(N,[{u32,A},B])when N=:=<<"<<">>;N=:=<<">>">>->{u32,int(op(N,[A,B]))band 16#ffffffff};
op(N,[A,B,C|Rest])when N=:=<<"+">>;N=:=<<"*">>->lists:foldl(fun(V,R)->op(N,[R,V])end,A,[B,C|Rest]);
op(<<"!">>,[A])->not truth(A);op(<<"~">>,[A])->bnot int(A);
op(<<"==">>,[A,B])->equal(A,B);op(<<"!=">>,[A,B])->not equal(A,B);
op(<<"===">>,[A,B])->A=:=B;op(<<"!==">>,[A,B])->A=/=B;
op(<<"<">>,[A,B])->cmp(A,B)<0;op(<<">">>,[A,B])->cmp(A,B)>0;op(<<"<=">>,[A,B])->cmp(A,B)=<0;op(<<">=">>,[A,B])->cmp(A,B)>=0;
op(<<"=>">>,[A,B])->tup([A,B]);
op(<<":">>,[S,End])->{range,int(S),int(End)+1,1};op(<<":">>,[S,Step,End])->{range,int(S),int(End)+case Step>0 of true->1;false->-1 end,int(Step)};
op(<<"&">>,[A,B])->int(A)band int(B);op(<<"|">>,[A,B])->int(A)bor int(B);op(<<"xor">>,[A,B])->int(A)bxor int(B);op(<<"<<">>,[A,B])->int(A)bsl int(B);op(<<">>">>,[A,B])->int(A)bsr int(B);
op(<<"+">>,[A,B])->num(A)+num(B);op(<<"-">>,[pos_infinity])->neg_infinity;op(<<"-">>,[neg_infinity])->pos_infinity;op(<<"-">>,[A])->-num(A);op(<<"-">>,[A,B])->num(A)-num(B);
op(<<"*">>,[A,B])when is_binary(A);is_binary(B)->strcat([A,B]);op(<<"*">>,[{char,_}=A,B])->strcat([A,B]);op(<<"*">>,[A,{char,_}=B])->strcat([A,B]);op(<<"*">>,[A,B])->num(A)*num(B);
op(<<"/">>,[A,B])->num(A)/num(B);op(<<"^">>,[A,B])->math:pow(num(A),num(B));
op(<<"%">>,[A,B])when is_integer(A),is_integer(B)->A rem B;op(<<"%">>,[A,B])->math:fmod(A,B);
op(<<"mod">>,[A,B])->A-floor(A/B)*B;op(<<"fld">>,[A,B])->case is_integer(A)andalso is_integer(B)of true->Q=A div B,case (A rem B)/=0 andalso ((A<0)xor(B<0))of true->Q-1;false->Q end;false->floor(A/B)end;
op(N,[A,B])when N=:=<<"div">>;N=:=<<"÷"/utf8>>->case is_integer(A)andalso is_integer(B)of true->A div B;false->trunc(A/B)end;
op(<<".*">>,[A,B])->arr([op(<<"*">>,[V,B])||V<-elements(A)]);op(<<".+">>,[A,B])->arr([op(<<"+">>,[V,B])||V<-elements(A)]).
while(E,Cond,Body)->checkpoint(E),case truth(Cond())of false->nil;true->try Body()of _->while(E,Cond,Body)catch throw:continue_loop->while(E,Cond,Body);throw:break_loop->nil end end.
foreach(E,{range,S,End,Step},F)->for_range(E,S,End,Step,F);
foreach(E,X,F)->Old=erlang:get(loop_roots),erlang:put(loop_roots,[X|Old]),try for_list(E,elements(X),F)after erlang:put(loop_roots,Old)end.
for_range(_,S,End,Step,_)when(Step>0 andalso S>=End)orelse(Step<0 andalso S=<End)->nil;
for_range(E,S,End,Step,F)->checkpoint(E),try F(S)of _->for_range(E,S+Step,End,Step,F)catch throw:continue_loop->for_range(E,S+Step,End,Step,F);throw:break_loop->nil end.
for_list(_,[],_)->nil;
for_list(E,[X|Xs],F)->checkpoint(E),try F(X)of _->for_list(E,Xs,F)catch throw:continue_loop->for_list(E,Xs,F);throw:break_loop->nil end.
%% Collect only at loop boundaries, when generated locals and active call frames
%% own all live handles. Parent expression temporaries remain protected.
checkpoint(_E)->case erlang:get(temps)of [_|Rest]->erlang:put(temps,[[]|Rest]);_->ok end,
    maybe_collect().
maybe_collect()->case erlang:get(alloc_weight)>1000000 of true->collect();false->ok end.
collect()->Roots=erlang:get(roots)++erlang:get(bench_roots)++erlang:get(loop_roots)++lists:append(erlang:get(temps)),Live=mark(Roots,#{}),
    lists:foreach(fun({{h,I},_})->case maps:is_key(I,Live)of true->ok;false->erlang:erase({h,I})end;(_)->ok end,erlang:get()),erlang:put(last_gc,erlang:get(next_id)),erlang:put(alloc_weight,0),ok.
mark([],M)->M;
mark([{h,I}|Rest],M)->case maps:is_key(I,M)of true->mark(Rest,M);false->mark([erlang:get({h,I})|Rest],maps:put(I,true,M))end;
mark([X|Rest],M)when is_map(X)->mark(maps:keys(X)++maps:values(X)++Rest,M);
mark([X|Rest],M)when is_tuple(X)->mark(tuple_to_list(X)++Rest,M);
mark([X|Rest],M)when is_list(X)->mark(X++Rest,M);
mark([_|Rest],M)->mark(Rest,M).
load(E,F)->{env,D}=val(E),L=maps:get(loaded,D),case maps:is_key(F,L)of true->nil;false->store(E,env,D#{loaded:=maps:put(F,true,L)}),ports:install(E,F)end.
broadcast(E,F,[A])->arr([call(E,F,[V])||V<-elements(A)]).

builtin(E,N,A)->X=arg(A,0,fun()->nil end),Y=arg(A,1,fun()->nil end),Z=arg(A,2,fun()->nil end),
    case N of
        <<"Sys.iswindows">>->is_windows(os:type());
        <<"json_string">>->json_quote(text(X));
        <<"identity">>->X;
        <<"truth0">>->truth0(X);
        <<"length">>->len(X);<<"sizeof">>->byte_size(text(X));<<"ncodeunits">>->byte_size(text(X));<<"isempty">>->len(X)=:=0;
        <<"Int">>->int(X);<<"Int32">>->int(X);<<"UInt8">>->int(X);<<"int0">>->int(X);<<"jint">>->int(X);<<"UInt32">>->{u32,int(X)band 16#ffffffff};
        <<"Float64">>->case is_binary(X)of true->binary_to_float(X);false->float(num(X))end;
        <<"String">>->text(X);<<"string">>->strcat(A);<<"Symbol">>->{atom,strcat(A)};
        <<"Char">>->{char,unicode:characters_to_binary([int(X)])};<<"chr0">>->unicode:characters_to_binary([int(X)]);<<"jchr">>->unicode:characters_to_binary([int(X)]);
        <<"ord0">>->hd(unicode:characters_to_list(text(X)));<<"jord">>->hd(unicode:characters_to_list(text(X)));
        <<"AsciiText">>->{ascii,text(X)};<<"Val">>->X;<<"big">>->int(X);
        <<"zero">>->case is_float(X)of true->0.0;false->0 end;<<"one">>->case is_float(X)of true->1.0;false->1 end;
        <<"sqrt">>->math:sqrt(num(X));<<"sin">>->math:sin(num(X));<<"cos">>->math:cos(num(X));<<"abs">>->abs(num(X));<<"sign">>->case cmp(X,0)of -1->-1;0->0;1->1 end;
        <<"min">>->lists:foldl(fun(V,R)->case cmp(V,R)<0 of true->V;false->R end end,X,A);
        <<"max">>->lists:foldl(fun(V,R)->case cmp(V,R)>0 of true->V;false->R end end,X,A);
        <<"clamp">>->case cmp(X,Y)<0 of true->Y;false->case cmp(X,Z)>0 of true->Z;false->X end end;
        <<"floor">>->floor(lists:last(A));<<"ceil">>->ceil(lists:last(A));<<"trunc">>->trunc(lists:last(A));
        <<"parse">>->case X of {builtin,<<"Float64">>}->binary_to_float(Y);_->binary_to_integer(Y)end;
        <<"range0">>->case A of [Stop]->{range,0,int(Stop),1};[Start,Stop]->{range,int(Start),int(Stop),1};[Start,Stop,Step]->{range,int(Start),int(Stop),int(Step)}end;
        <<"eachindex">>->{range,1,len(X)+1,1};
        <<"enumerate">>->arr(enumerate(elements(X),1));<<"enumerate0">>->arr(enumerate(elements(X),0));
        <<"zip">>->arr(zip_many([elements(V)||V<-A]));
        <<"collect">>->arr(elements(X));
        <<"fill">>->arr(lists:duplicate(int(Y),X));
        <<"zeros">>->arr(lists:duplicate(int(lists:last(A)),case A of [_]->0.0;_->0 end));
        <<"ones">>->arr(lists:duplicate(int(lists:last(A)),case A of [_]->1.0;_->1 end));
        <<"Vector">>->arr(lists:duplicate(int(lists:last(A)),nil));
        <<"Ref">>->arr([X]);
        <<"Dict">>->Pairs=case A of [V]->case V of {tuple,[_,_]}->[V];_->elements(V)end;_->A end,new(dict,maps:from_list([list_to_tuple(elements(V))||V<-Pairs]));
        <<"get0">>->get0(X,Y,false);<<"jget">>->get0(X,Y,true);
        <<"set0!">>->set0(X,Y,Z);<<"jset!">>->set0(X,Y,Z);
        <<"push!">>->lists:foreach(fun(V)->push(X,V)end,tl(A)),X;<<"m_append">>->push(X,Y);
        <<"append!">>->lists:foreach(fun(V)->push(X,V)end,elements(Y)),X;<<"m_extend">>->lists:foreach(fun(V)->push(X,V)end,elements(Y)),nil;
        <<"pop!">>->pop(X,len(X)-1);<<"m_pop">>->pop(X,case A of [_]->len(X)-1;_->case Y<0 of true->len(X)+Y;false->Y end end);
        <<"insert!">>->insert(X,int(Y)-1,Z);<<"m_insert">>->insert(X,int(Y),Z);
        <<"m_get">>->dict_get(X,Y,Z);<<"get">>->dict_get(X,Y,Z);<<"haskey">>->{dict,M}=val(X),maps:is_key(Y,M);
        <<"in">>->contains(Y,X);<<"in0">>->contains(Y,X);
        <<"pairs">>->arr(elements(X));<<"m_items">>->arr(elements(X));
        <<"keys">>->{dict,M}=val(X),arr(maps:keys(M));<<"values">>->{dict,M}=val(X),arr(maps:values(M));
        <<"first">>->case A of [_]->hd(elements(X));_->slice(X,0,int(Y),1)end;
        <<"only">>->check(len(X)=:=1),nth(X,0);
        <<"add0">>->case {is_binary(X),kind(X),X}of {true,_,_}->strcat([X,Y]);{_,arr,_}->arr(elements(X)++elements(Y));{_,_,{tuple,_}}->tup(elements(X)++elements(Y));_->op(<<"+">>,A)end;
        <<"mul0">>->case is_number(X)orelse is_boolean(X)of true->op(<<"*">>,A);false->repeat(X,Y)end;<<"repeat">>->repeat(X,Y);
        <<"vcat">>->arr(lists:append([elements(V)||V<-A]));<<"jcat">>->case is_binary(X)of true->strcat(A);false->arr(lists:append([elements(V)||V<-A]))end;
        <<"slice0">>->slice(X,Y,Z,arg(A,3,fun()->nil end));<<"jslice">>->slice(X,Y,Z,1);
        <<"setslice0!">>->Lo=case Y of nil->0;_->int(Y)end,Hi=case Z of nil->len(X);_->int(Z)end,Items=elements(X),Replacement=elements(lists:nth(4,A)),store(X,arr,array:from_list(lists:sublist(Items,Lo)++Replacement++lists:nthtail(Hi,Items))),lists:nth(4,A);
        <<"copy">>->clone_default(X);<<"deepcopy">>->deep(X);
        <<"fill!">>->store(X,arr,array:from_list(lists:duplicate(len(X),Y))),X;
        <<"join">>->join(elements(X),case Y of nil-><<>>;_->text(Y)end);<<"m_join">>->join(elements(Y),text(X));
        <<"split">>->arr(binary:split(text(X),text(Y),[global]));<<"m_split">>->arr(binary:split(text(X),text(Y),[global]));
        <<"startswith">>->starts(text(X),text(Y));<<"m_startswith">>->starts(text(X),text(Y));<<"endswith">>->ends(text(X),text(Y));<<"m_endswith">>->ends(text(X),text(Y));
        <<"occursin">>->case X of {regex,R}->re:run(text(Y),R,[unicode])=/=nomatch;_->binary:match(text(Y),text(X))=/=nomatch end;
        <<"occursin_reverse">>->binary:match(text(X),text(Y))=/=nomatch;
        <<"uppercase">>->unicode:characters_to_binary(string:uppercase(unicode:characters_to_list(text(X))));<<"lowercase">>->lower(X);<<"m_lower">>->lower(X);
        <<"uppercasefirst">>->[First|Rest]=unicode:characters_to_list(text(X)),unicode:characters_to_binary(string:uppercase([First])++Rest);
        <<"isascii">>->is_ascii(X);<<"m_isascii">>->is_ascii(X);
        <<"isletter">>->is_letter(hd(unicode:characters_to_list(text(X))));<<"isdigit">>->C=hd(unicode:characters_to_list(text(X))),C>=$0 andalso C=<$9;
        <<"isspace">>->is_space(X);<<"m_isspace">>->is_space(X);<<"m_isalnum">>->Cs=unicode:characters_to_list(text(X)),Cs=/=[]andalso lists:all(fun(C)->is_letter(C)orelse(C>=$0 andalso C=<$9)end,Cs);
        <<"m_find">>->find(X,Y,case Z of nil->0;_->int(Z)end);
        <<"replace">>->lists:foldl(fun(P,Acc)->[From,To]=elements(P),case From of {regex,R}->re:replace(Acc,R,text(To),[global,unicode,{return,binary}]);_->binary:replace(Acc,text(From),text(To),[global])end end,text(X),tl(A));
        <<"Regex">>->{regex,text(X)};
        <<"eachmatch">>->{regex,R}=X,case re:run(text(Y),R,[global,unicode,{capture,first,binary}])of nomatch->arr([]);{match,Ms}->arr([hd(M)||M<-Ms])end;
        <<"re_search">>->regex_match(text(X),text(Y));<<"re_match">>->regex_match(<<"^(?:",(text(X))/binary,")">>,text(Y));
        <<"match">>->{regex,R}=X,regex_match(R,text(Y));
        <<"format0">>->case text(Y)of <<"02d">>->pad(text(X),2,<<"0">>,left);<<".3f">>->float_to_binary(float(num(X)),[{decimals,3}])end;
        <<"reverse">>->case is_binary(X)of true->unicode:characters_to_binary(lists:reverse(unicode:characters_to_list(X)));false->arr(lists:reverse(elements(X)))end;
        <<"rpad">>->pad(text(X),int(Y),case Z of nil-><<" ">>;_->text(Z)end,right);<<"lpad">>->pad(text(X),int(Y),case Z of nil-><<" ">>;_->text(Z)end,left);
        <<"sort">>->sort_values(E,X,A,false);<<"sort!">>->sort_values(E,X,A,true);
        <<"issorted">>->L=elements(X),L=:=lists:sort(L);
        <<"all">>->Vs=case A of [_]->elements(X);_->[call(E,X,[V])||V<-elements(Y)]end,lists:all(fun truth/1,Vs);
        <<"any">>->Vs=case A of [_]->elements(X);_->[call(E,X,[V])||V<-elements(Y)]end,lists:any(fun truth/1,Vs);
        <<"foldl">>->lists:foldl(fun(V,Acc)->call(E,X,[Acc,V])end,maps:get(<<"init">>,kwmap(A)),elements(Y));
        <<"sum">>->lists:sum([num(V)||V<-elements(X)]);
        <<"objectid">>->case X of {h,I}->I;_->erlang:phash2(X)end;
        <<"typeof">>->case X of {h,_}->kind(X);_->if is_float(X)->float;is_integer(X)->integer;is_binary(X)->string;true->X end end;
        <<"time_ns">>->erlang:monotonic_time(nanosecond);
        <<"read">>->{ok,B}=file:read_file(text(X)),B;
        <<"joinpath">>->unicode:characters_to_binary(filename:join([text(V)||V<-A]));
        <<"take!">>->{buffer,B}=val(X),store(X,buffer,[]),iolist_to_binary(B);
        <<"IOBuffer">>->new(buffer,[]);
        <<"print">>->output(A,false);<<"println">>->output(A,true);<<"printf">>->output_format(A);<<"format">>->format(A);
        <<"open">>->{ok,Sink}=file:open(text(Y),[write,raw,binary]),try call(E,X,[{sink,Sink}])after file:close(Sink)end;
        <<"write">>->{sink,Sink}=X,B=text(Y),ok=file:write(Sink,B),byte_size(B);
        <<"ErrorException">>->{error_value,text(X)};<<"error">>->error(X);<<"throw">>->error(X);
        <<"reinterpret">>->V=int(Y)band 16#ffffffff,case V>=16#80000000 of true->V-16#100000000;false->V end;
        <<"isfinite">>->is_number(X);<<"isinteger">>->is_integer(X)orelse(is_float(X)andalso X=:=float(trunc(X)));
        <<"jor">>->V=call(E,X,[]),case V=:=nil orelse V=:=false of true->call(E,Y,[]);false->V end;
        <<"jrescue">>->try call(E,X,[])catch throw:{return,_}=R->throw(R);_:_->call(E,Y,[])end;
        <<"jstring">>->case is_float(X)andalso X=:=float(trunc(X))of true->integer_to_binary(trunc(X));false->text(X)end;
        <<"jparse">>->call(E,getv(E,<<"fixture_value">>),[call(E,getv(E,<<"m_parse">>),[call(E,getv(E,<<"C__Parser">>),[text(X)])])]);
        <<"with_node">>->{obj,{T,P,M}}=val(X),new(obj,{T,P,maps:merge(M,kwmap(A))});
        <<"run_benchmark">>->run_benchmark(E,A);
        <<"checked_benchmark">>->Prep=maps:get(<<"prepare">>,kwmap(A),{builtin,<<"nothing_prepare">>}),
            Work={fn,E,fun(_,Args)->call(E,Y,[lists:nth(2,Args)])end,[<<"Any">>,<<"Any">>],2,2},
            Verify={fn,E,fun(_,[V])->equal(V,Z)end,[<<"Any">>],1,1},
            Report={fn,E,fun(_,_)->output([X,<<": PASS">>],true)end,[<<"Any">>],1,1},run_benchmark(E,[Prep,Work,Verify,Report]);
        <<"nothing_prepare">>->nil;
        <<"codeunit">>->binary:at(text(X),int(Y)-1);<<"nextind">>->int(Y)+case Z of nil->1;_->int(Z)end;<<"prevind">>->int(Y)-1;
        <<"SubString">>->Cs=unicode:characters_to_list(text(X)),Start=int(Y),Stop=case Z of nil->erlang:length(Cs);_->int(Z)end,unicode:characters_to_binary(lists:sublist(lists:nthtail(Start-1,Cs),max(0,Stop-Start+1)));
        _->error({missing_adapter,N,A})
    end.
is_windows({win32,_})->true;
is_windows(_)->false.
kwmap(A)->lists:foldl(fun({kw,M},Acc)->maps:merge(Acc,M);(_,Acc)->Acc end,#{},A).
enumerate([],_) ->[];enumerate([X|Xs],I)->[tup([I,X])|enumerate(Xs,I+1)].
get0(H,K,Safe)->case kind(H)of dict->dict_get(H,K,nil);_->N=len(H),I=case Safe orelse K>=0 of true->int(K);false->N+int(K)end,case Safe andalso(I<0 orelse I>=N)of true->nil;false->case index(H,[I+1])of {char,B}->B;V->V end end end.
set0(H,K,V)->case kind(H)of dict->putindex(H,[K],V);_->putindex(H,[case K<0 of true->len(H)+K+1;false->K+1 end],V)end.
push(H,V)->{arr,A}=val(H),store(H,arr,array:set(array:size(A),V,A)),H.
pop(H,I)->L=elements(H),V=lists:nth(I+1,L),store(H,arr,array:from_list(lists:sublist(L,I)++lists:nthtail(I+1,L))),V.
insert(H,I,V)->L=elements(H),store(H,arr,array:from_list(lists:sublist(L,I)++[V|lists:nthtail(I,L)])),H.
dict_get(H,K,Default)->{dict,M}=val(H),maps:get(K,M,Default).
contains(B,X)when is_binary(B)->binary:match(B,text(X))=/=nomatch;
contains(H,X)->case kind(H)of dict->{dict,M}=val(H),maps:is_key(X,M);_->lists:any(fun(V)->equal(V,X)end,elements(H))end.
repeat(X,N)->case is_binary(X)of true->binary:copy(X,int(N));false->arr(lists:append(lists:duplicate(int(N),elements(X))))end.
slice({ascii,B},Lo,Hi,Stride)when Stride=:=nil;Stride=:=1->N=byte_size(B),L=case Lo of nil->0;_ when Lo<0->N+Lo;_->Lo end,H=case Hi of nil->N;_ when Hi<0->N+Hi;_->Hi end,Start=max(0,min(N,L)),Stop=max(0,min(N,H)),binary:part(B,Start,max(0,Stop-Start));
slice(X,Lo,Hi,Stride)->N=len(X),Step=case Stride of nil->1;_->int(Stride)end,
    Start0=case Lo of nil->case Step>0 of true->0;false->N-1 end;_->case Lo<0 of true->N+int(Lo);false->int(Lo)end end,
    Stop0=case Hi of nil->case Step>0 of true->N;false->-1 end;_->case Hi<0 of true->N+int(Hi);false->int(Hi)end end,
    Low=case Step>0 of true->0;false->-1 end,High=case Step>0 of true->N;false->N-1 end,Start=max(Low,min(High,Start0)),Stop=max(Low,min(High,Stop0)),
    Values=[index(X,[I+1])||I<-elements({range,Start,Stop,Step})],case is_binary(X)of true->strcat(Values);false->arr(Values)end.
deep(H={h,_})->case val(H)of {arr,A}->arr([deep(V)||V<-array:to_list(A)]);{dict,M}->new(dict,maps:map(fun(_,V)->deep(V)end,M));_->H end;deep(X)->X.
join([],_) -> <<>>;join([X|Xs],Sep)->iolist_to_binary([text(X)|[[Sep,text(V)]||V<-Xs]]).
starts(B,P)->byte_size(B)>=byte_size(P)andalso binary:part(B,0,byte_size(P))=:=P.
ends(B,P)->byte_size(B)>=byte_size(P)andalso binary:part(B,byte_size(B)-byte_size(P),byte_size(P))=:=P.
lower(X)->unicode:characters_to_binary(string:lowercase(unicode:characters_to_list(text(X)))).
is_ascii(X)->lists:all(fun(C)->C<128 end,unicode:characters_to_list(text(X))).
is_letter(C)->(C>=$a andalso C=<$z)orelse(C>=$A andalso C=<$Z)orelse(C>127).
is_space(X)->Cs=unicode:characters_to_list(text(X)),Cs=/=[]andalso lists:all(fun(C)->lists:member(C,[9,10,11,12,13,32])end,Cs).
find(X,Y,Start)->Cs=unicode:characters_to_list(text(X)),case Start>erlang:length(Cs)of true->-1;false->Tail=unicode:characters_to_binary(lists:nthtail(Start,Cs)),case binary:match(Tail,text(Y))of nomatch->-1;{At,_}->Start+erlang:length(unicode:characters_to_list(binary:part(Tail,0,At)))end end.
regex_match(R,B)->case re:run(B,R,[unicode,{capture,first,binary}])of nomatch->nil;{match,[V]}->V end.
pad(B,N,P,Side)->Rest=binary:copy(P,max(0,N-len(B))),case Side of left-><<Rest/binary,B/binary>>;right-><<B/binary,Rest/binary>>end.
sort_values(E,X,A,Mutate)->Key=maps:get(<<"by">>,kwmap(A),nil),Sorted=lists:sort(fun(L,R)->cmp(case Key of nil->L;_->call(E,Key,[L])end,case Key of nil->R;_->call(E,Key,[R])end)=<0 end,elements(X)),case Mutate of true->store(X,arr,array:from_list(Sorted)),X;false->arr(Sorted)end.
output(A,Newline)->{Sink,Values}=case A of [H={h,_}|Rest]->case kind(H)of buffer->{H,Rest};_->{nil,A}end;_->{nil,A}end,
    B=strcat(Values++case Newline of true->[<<"\n">>];false->[]end),emit(Sink,B).
emit(nil,B)->io:put_chars(B),nil;emit(Sink,B)->{buffer,Old}=val(Sink),store(Sink,buffer,[Old,B]),nil.
output_format(A)->case A of [H={h,_}|Rest]->case kind(H)of buffer->emit(H,format(Rest));_->emit(nil,format(A))end;_->emit(nil,format(A))end.
format([Fmt|Values])->format_parts(binary_to_list(text(Fmt)),Values,[]).
format_parts([],_,Acc)->iolist_to_binary(lists:reverse(Acc));
format_parts([$%,$s|Rest],[V|Vs],Acc)->format_parts(Rest,Vs,[text(V)|Acc]);
format_parts([$%,$.,$3,$f|Rest],[V|Vs],Acc)->format_parts(Rest,Vs,[float_to_binary(float(num(V)),[{decimals,3}])|Acc]);
format_parts([$%,$.,$9,$f|Rest],[V|Vs],Acc)->format_parts(Rest,Vs,[float_to_binary(float(num(V)),[{decimals,9}])|Acc]);
format_parts([C|Rest],Vs,Acc)->format_parts(Rest,Vs,[<<C>>|Acc]).
run_benchmark(E,[Prepare,Work,Verify,Report])->Buffer=new(buffer,[]),Warm=case os:getenv("NATIVE_BENCH_WARMUP")of "0"->0;_->1 end,
    %% Pin preparation/result temporaries across callbacks that contain loops.
    erlang:put(bench_roots,[Buffer|erlang:get(bench_roots)]),
    case Warm of 1->S=call(E,Prepare,[]),erlang:put(bench_roots,[S|erlang:get(bench_roots)]),R=call(E,Work,[Buffer,S]),check(call(E,Verify,[R]));_->ok end,
    store(Buffer,buffer,[]),State=call(E,Prepare,[]),erlang:put(bench_roots,[State|erlang:get(bench_roots)]),Start=erlang:monotonic_time(nanosecond),Result=call(E,Work,[Buffer,State]),Elapsed=(erlang:monotonic_time(nanosecond)-Start)/1.0e6,
    check(call(E,Verify,[Result])),{buffer,Out}=val(Buffer),io:put_chars(Out),call(E,Report,[Result]),io:format("__TIMING__:~p~n",[Elapsed]),Result.
adapters(E,<<"class_support.jl">>)->nil;
adapters(E,<<"jetstream/support.jl">>)->load(E,<<"common.jl">>);
adapters(E,<<"text/support.jl">>)->load(E,<<"common.jl">>),load(E,<<"fixtures.jl">>);
adapters(E,<<"text/jq_values.jl">>)->load(E,<<"fixtures.jl">>),
    %% Qualified calls retain the module owning the parser definitions.
    bind(E,<<"jparse">>,{fn,E,fun(Scope,Args)->builtin(Scope,<<"jparse">>,Args)end,[<<"Any">>,<<"Any">>],2,2}),
    deftype(E,<<"JResult">>,<<"Any">>,[<<"ok">>,<<"value">>],[false,nil]),
    deftype(E,<<"Node">>,<<"Any">>,[<<"kind">>,<<"op">>,<<"a">>,<<"b">>,<<"c">>,<<"d">>,<<"list">>,<<"name">>,<<"value">>,<<"pvar">>],[0,0,nil,nil,nil,nil,arr([]),<<>>,nil,arr([])]);
adapters(_,File)->error({missing_source,File}).
init()->erlang:put(alloc_weight,0),erlang:put(next_id,0),erlang:put(last_gc,0),erlang:put(roots,[]),erlang:put(temps,[[]]),erlang:put(bench_roots,[]),erlang:put(loop_roots,[]),
    Root=env(nil),erlang:put(roots,[Root]),Root.
main()->[Entry|Args]=init:get_plain_arguments(),Root=init(),bind(Root,<<"ARGS">>,arr([unicode:characters_to_binary(X)||X<-Args])),
    try case Entry of "text/text_search"->text_search:run(Root);_->load(Root,<<(unicode:characters_to_binary(Entry))/binary,".jl">>)end of _->halt(0)catch Class:Reason:Stack->io:format(standard_error,"~p:~p~n~p~n",[Class,Reason,Stack]),halt(1)end.

zip_many(Lists)->case lists:any(fun(L)->L=:=[]end,Lists)of true->[];false->[tup([hd(L)||L<-Lists])|zip_many([tl(L)||L<-Lists])]end.
json_quote(B)->Cs=unicode:characters_to_list(B),unicode:characters_to_binary([$"|lists:append([case C of 34->[92,34];92->[92,92];10->[92,$n];13->[92,$r];9->[92,$t];_->[C]end||C<-Cs])]++[$"]).

charge(Kind,Data)->Weight=case Kind of arr->array:size(Data)+1;env->32;dict->maps:size(Data)*2+1;obj->{_,_,M}=Data,maps:size(M)*2+1;_->8 end,erlang:put(alloc_weight,erlang:get(alloc_weight)+Weight).

signbit(pos_infinity)->0;signbit(neg_infinity)->1;signbit(X)-><<S:1,_:63>>= <<(float(X)):64/float>>,S.
infinity(0)->pos_infinity;infinity(1)->neg_infinity.
is_infinite(X)->X=:=pos_infinity orelse X=:=neg_infinity.
infinite_op(N,A,B)when N=:=<<"<">>;N=:=<<">">>;N=:=<<"<=">>;N=:=<<">=">>;N=:=<<"==">>;N=:=<<"!=">>;N=:=<<"===">>;N=:=<<"!==">>->C=cmp(A,B),case N of <<"<">>->C<0;<<">">>->C>0;<<"<=">>->C=<0;<<">=">>->C>=0;<<"==">>->C=:=0;<<"===">>->C=:=0;_->C=/=0 end;
infinite_op(<<"+">>,A,B)->case is_infinite(A)andalso is_infinite(B)andalso A=/=B of true->nan;false->case is_infinite(A)of true->A;false->B end end;
infinite_op(<<"-">>,A,B)->infinite_op(<<"+">>,A,op(<<"-">>,[B]));
infinite_op(<<"*">>,A,B)->case A==0 orelse B==0 of true->nan;false->infinity(signbit(A)bxor signbit(B))end;
infinite_op(<<"/">>,A,B)->case {is_infinite(A),is_infinite(B)}of {true,true}->nan;{true,false}->infinity(signbit(A)bxor signbit(B));{false,true}->case signbit(A)bxor signbit(B)of 0->0.0;1->-0.0 end end.
