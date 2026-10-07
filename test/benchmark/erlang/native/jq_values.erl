%% Native jq values: tuples for indexed arrays, persistent maps with key order.
-module(jq_values).
-export([decode/1,type/1,type_name/1,truth/1,size/1,keys/2,compare/2,index/2,slice/3,bin/3,with/3,native/3,json/1,number/1,getpath/2,empty_object/0]).
-compile({no_auto_import,[size/1]}).
-record(object,{order=[],values=#{}}).
empty_object()->#object{}.
error_value(V)->throw({jq_error,V}).
decode(B)->
    %% Decoder callbacks retain source key order and build indexed arrays directly.
    {Value,ok,Rest}=json:decode(B,ok,#{
        array_finish=>fun(Rev,Old)->{list_to_tuple(lists:reverse(Rev)),Old}end,
        object_start=>fun(_)->#object{}end,
        object_push=>fun(K,V,O)->with(O,K,V)end,
        object_finish=>fun(O,Old)->{O,Old}end}),
    case string:trim(Rest)of <<>>->Value;_->error_value(<<"trailing JSON data">>)end.
type(null)->0;type(false)->1;type(true)->2;type(V)when is_number(V)->3;type(V)when is_binary(V)->4;type(#object{})->6;type(V)when is_tuple(V)->5.
type_name(V)->element(type(V)+1,{<<"null">>,<<"boolean">>,<<"boolean">>,<<"number">>,<<"string">>,<<"array">>,<<"object">>}).
truth(null)->false;truth(false)->false;truth(_)->true.
size(null)->0;size(V)when is_binary(V)->length(unicode:characters_to_list(V));size(#object{values=M})->map_size(M);size(V)when is_tuple(V)->tuple_size(V).
array(V)when is_tuple(V),element(1,V)=:=object->error_value(<<"array required">>);
array(V)when is_tuple(V)->V;
array(_)->error_value(<<"array required">>).
number(V)when is_integer(V)->integer_to_binary(V);
number(V)when is_float(V)->case V==trunc(V)of true->integer_to_binary(trunc(V));false->float_to_binary(V,[short])end.
num(V)when is_number(V)->V;num(_)->error_value(<<"number required">>).
keys(#object{order=Order},Sorted)->L=lists:reverse(Order),list_to_tuple(case Sorted of true->lists:sort(L);false->L end);
keys(V,_)when is_tuple(V)->list_to_tuple(lists:seq(0,tuple_size(V)-1));
keys(_,_) ->error_value(<<"value has no keys">>).
cmp_scalar(A,B)->if A<B->-1;A>B->1;true->0 end.
compare(A,B)->Ta=type(A),Tb=type(B),case Ta=:=Tb of false->cmp_scalar(Ta,Tb);true->case Ta of 0->0;1->0;2->0;3->cmp_scalar(A,B);4->cmp_scalar(A,B);5->compare_arrays(A,B,1);6->Ka=keys(A,true),Kb=keys(B,true),case compare(Ka,Kb)of 0->compare_fields(A,B,tuple_to_list(Ka));C->C end end end.
compare_arrays(A,B,I)when I>tuple_size(A);I>tuple_size(B)->cmp_scalar(tuple_size(A),tuple_size(B));
compare_arrays(A,B,I)->case compare(element(I,A),element(I,B))of 0->compare_arrays(A,B,I+1);C->C end.
compare_fields(_,_,[])->0;
compare_fields(A,B,[K|Rest])->case compare(index(A,K),index(B,K))of 0->compare_fields(A,B,Rest);C->C end.
sort(V)->list_to_tuple(merge_sort(tuple_to_list(array(V)),fun(A,B)->compare(A,B)=<0 end)).
merge_sort(L,Cmp)->merge_pass(L,1,length(L),Cmp).
merge_pass(L,W,N,_)when W>=N->L;
merge_pass(L,W,N,Cmp)->merge_pass(merge_runs(L,W,Cmp,[]),W*2,N,Cmp).
take(0,L,Acc)->{lists:reverse(Acc),L};take(_,[],Acc)->{lists:reverse(Acc),[]};take(N,[X|Xs],Acc)->take(N-1,Xs,[X|Acc]).
merge_runs([],_,_,Acc)->lists:reverse(Acc);
merge_runs(L,W,Cmp,Acc)->{A,Rest}=take(W,L,[]),{B,Tail}=take(W,Rest,[]),merge_runs(Tail,W,Cmp,lists:reverse(merge(A,B,Cmp),Acc)).
merge([],B,_)->B;merge(A,[],_)->A;
merge([A|As]=Left,[B|Bs]=Right,Cmp)->case Cmp(A,B)of true->[A|merge(As,Right,Cmp)];false->[B|merge(Left,Bs,Cmp)]end.
with(#object{order=Order,values=M},K,V)when is_binary(K)->NewOrder=case maps:is_key(K,M)of true->Order;false->[K|Order]end,#object{order=NewOrder,values=M#{K=>V}};
with(_,_,_)->error_value(<<"object and string key required">>).
index(null,K)when is_binary(K);is_number(K);K=:=null->null;
index(#object{values=M},K)when is_binary(K)->maps:get(K,M,null);
index(T,K)when is_tuple(T),is_number(K)->case type(T)of 5->I0=floor(K),I=case I0<0 of true->I0+tuple_size(T);false->I0 end,case I>=0 andalso I<tuple_size(T)of true->element(I+1,T);false->null end;_->error_value(<<"Cannot index object with number">>)end;
index(_,_) ->error_value(<<"Cannot index value">>).
slice(null,_,_)->null;
slice(T,From,Upto)->N=size(T),S0=case From of null->0;_->num(From)end,E0=case Upto of null->N;_->num(Upto)end,S1=case S0<0 of true->S0+N;false->S0 end,E1=case E0<0 of true->E0+N;false->E0 end,S=min(N,max(0,floor(S1))),E=max(S,min(N,ceil(E1))),
    case type(T)of 4->unicode:characters_to_binary(lists:sublist(unicode:characters_to_list(T),S+1,E-S));5->list_to_tuple([element(I+1,T)||I<-lists:seq(S,E-1)]);_->error_value(<<"value cannot be sliced">>)end.
bin(1,null,R)->R;bin(1,L,null)->L;
bin(1,L,R)when is_number(L),is_number(R)->L+R;
bin(1,L,R)when is_binary(L),is_binary(R)-> <<L/binary,R/binary>>;
bin(1,L=#object{},R=#object{order=Order,values=M})->lists:foldl(fun(K,O)->with(O,K,maps:get(K,M))end,L,lists:reverse(Order));
bin(1,L,R)when is_tuple(L),is_tuple(R)->list_to_tuple(tuple_to_list(array(L))++tuple_to_list(array(R)));
bin(2,L,R)when is_tuple(L),is_tuple(R)->list_to_tuple([X||X<-tuple_to_list(array(L)),not lists:any(fun(Y)->compare(X,Y)=:=0 end,tuple_to_list(array(R)))]);
bin(3,L,R)when is_binary(L),is_number(R)->repeat_string(L,R);
bin(3,L,R)when is_number(L),is_binary(R)->repeat_string(R,L);
bin(Op,L,R)when Op>=2,Op=<5->A=num(L),B=num(R),case (Op=:=4 orelse Op=:=5)andalso B==0 of true->error_value(<<"division by zero">>);false->case Op of 2->A-B;3->A*B;4->A/B;5->trunc(A)rem abs(trunc(B))end end;
bin(Op,L,R)when Op>=6,Op=<11->C=compare(L,R),case Op of 6->C=:=0;7->C=/=0;8->C<0;9->C=<0;10->C>0;11->C>=0 end;
bin(_,_,_)->error_value(<<"incompatible jq operands">>).
repeat_string(_,N)when N=<0->null;
repeat_string(S,N)->binary:copy(S,max(1,trunc(N))).
contains(A=#object{values=M},#object{values=N})->maps:fold(fun(K,V,Acc)->Acc andalso maps:is_key(K,M)andalso contains(index(A,K),V)end,true,N);
contains(A,B)when is_tuple(A),is_tuple(B)->lists:all(fun(Y)->lists:any(fun(X)->contains(X,Y)end,tuple_to_list(A))end,tuple_to_list(B));
contains(A,B)when is_binary(A),is_binary(B)->binary:match(A,B)=/=nomatch;
contains(A,B)->compare(A,B)=:=0.
json(null)-> <<"null">>;json(true)-> <<"true">>;json(false)-> <<"false">>;json(V)when is_number(V)->number(V);json(V)when is_binary(V)->iolist_to_binary(json:encode(V));
json(O=#object{order=Order})->iolist_to_binary(["{",lists:join(",",[[json(K),":",json(index(O,K))]||K<-lists:reverse(Order)]),"}"]);
json(A)when is_tuple(A)->iolist_to_binary(["[",lists:join(",",[json(V)||V<-tuple_to_list(A)]),"]"]).
tostring(V)when is_binary(V)->V;tostring(V)->json(V).
getpath(V,Path)->lists:foldl(fun(K,Value)->index(Value,K)end,V,tuple_to_list(array(Path))).
setpath(_,P,I,V)when I=:=tuple_size(P)->V;
setpath(T,P,I,V)->K=element(I+1,P),case is_binary(K)of true->Base=case T of null->#object{};_->T end,Child=setpath(index(Base,K),P,I+1,V),with(Base,K,Child);
    false when is_number(K)->Base=case T of null->{};_->array(T)end,I0=floor(K),At=case I0<0 of true->I0+tuple_size(Base);false->I0 end,case At<0 of true->error_value(<<"Out of bounds negative array index">>);false->Old=case At<tuple_size(Base)of true->element(At+1,Base);false->null end,Child=setpath(Old,P,I+1,V),Grown=case At<tuple_size(Base)of true->Base;false->list_to_tuple(tuple_to_list(Base)++lists:duplicate(At+1-tuple_size(Base),null))end,setelement(At+1,Grown,Child)end;
    false->error_value(<<"Invalid path component">>)end.
delpath(null,_,_)->null;
delpath(O=#object{values=M,order=Order},P,I)->K=element(I+1,P),case maps:find(K,M)of error->O;{ok,Old}->case I=:=tuple_size(P)-1 of true->O#object{values=maps:remove(K,M),order=lists:delete(K,Order)};false->with(O,K,delpath(Old,P,I+1))end end;
delpath(A,P,I)when is_tuple(A)->K=element(I+1,P),I0=floor(num(K)),At=case I0<0 of true->I0+tuple_size(A);false->I0 end,case At<0 orelse At>=tuple_size(A)of true->A;false->case I=:=tuple_size(P)-1 of true->erlang:delete_element(At+1,A);false->setelement(At+1,A,delpath(element(At+1,A),P,I+1))end end.
delpaths(T,Paths)->lists:foldl(fun(P,Cur)->case tuple_size(array(P))of 0->null;_->delpath(Cur,P,0)end end,T,lists:reverse(tuple_to_list(sort(Paths)))).
flatten(A,Acc)->lists:foldl(fun(X,S)->case type(X)of 5->flatten(X,S);_->[X|S]end end,Acc,tuple_to_list(array(A))).
add_all(null)->null;add_all({})->null;
add_all(A)->[First|Rest]=tuple_to_list(array(A)),add_fold(Rest,First,normal).
add_fold([],Acc,normal)->Acc;add_fold([],Chunks,array)->list_to_tuple(lists:append(lists:reverse(Chunks)));
add_fold([X|Xs],Acc,array)->case type(X)of 5->add_fold(Xs,[tuple_to_list(X)|Acc],array);_->add_fold([X|Xs],list_to_tuple(lists:append(lists:reverse(Acc))),normal)end;
add_fold([X|Xs],Acc,normal)->case {type(Acc),type(X)}of {5,5}->add_fold(Xs,[tuple_to_list(X),tuple_to_list(Acc)],array);_->add_fold(Xs,bin(1,Acc,X),normal)end.
unique([])->[];unique([X|Xs])->[X|unique_tail(X,Xs)].
unique_tail(_,[])->[];
unique_tail(Prev,[X|Xs])->case compare(Prev,X)of 0->unique_tail(Prev,Xs);_->[X|unique_tail(X,Xs)]end.
native(1,Input,_)->case type(Input)of 0->0;3->abs(Input);T when T>=4->size(Input);_->error_value(<<"boolean has no length">>)end;
native(2,Input,_)->not truth(Input);
native(3,Input,_)->type_name(Input);
native(4,Input,_)->keys(Input,true);native(5,Input,_)->keys(Input,false);
native(6,#object{values=M},[K])when is_binary(K)->maps:is_key(K,M);
native(6,A,[K])when is_tuple(A),is_number(K)->K>=0 andalso K<tuple_size(A);
native(6,_,_)->error_value(<<"cannot check key">>);
native(7,Input,[Arg])->T=type(Input),U=type(Arg),case T=:=U orelse((T=:=1 orelse T=:=2)andalso(U=:=1 orelse U=:=2))of true->contains(Input,Arg);false->error_value(<<"cannot check containment">>)end;
native(8,Input,_)->tostring(Input);native(9,Input,_)->json(Input);
native(10,Input,_)when is_binary(Input)->try decode(Input)catch _:_ ->error_value(<<"Invalid JSON text: ",Input/binary>>)end;
native(11,Input,_)when is_number(Input)->Input;
native(11,Input,_)when is_binary(Input)->Value=try json:decode(Input)catch _:_ ->null end,case is_number(Value)of true->Value;false->error_value(<<"Cannot parse number">>)end;
native(Id,Input,_)when (Id=:=12 orelse Id=:=13),is_binary(Input)->unicode:characters_to_binary([case C of _ when Id=:=12,C>=$a,C=<$z->C-32;_ when Id=:=13,C>=$A,C=<$Z->C+32;_->C end||C<-unicode:characters_to_list(Input)]);
native(14,Input,_)when is_binary(Input)->list_to_tuple(unicode:characters_to_list(Input));
native(15,Input,_)->unicode:characters_to_binary([trunc(num(C))||C<-tuple_to_list(array(Input))]);
native(16,Input,[Separator])when is_binary(Input),is_binary(Separator)->case Input of <<>>->{};_->case Separator of <<>>->list_to_tuple([unicode:characters_to_binary([C])||C<-unicode:characters_to_list(Input)]);_->list_to_tuple(binary:split(Input,Separator,[global]))end end;
native(17,Input,[Arg])->Sep=case is_binary(Arg)of true->Arg;false-> <<>>end,iolist_to_binary(lists:join(Sep,[case X of null-> <<>>;_->tostring(X)end||X<-tuple_to_list(array(Input))]));
native(18,Input,_)->add_all(Input);
native(19,Input,_)->list_to_tuple(lists:reverse(flatten(Input,[])));
native(20,Input,_)->floor(num(Input));native(21,Input,_)->ceil(num(Input));
native(Id,Input,_)when Id=:=22;Id=:=23->case tuple_to_list(array(Input))of []->null;[First|Rest]->lists:foldl(fun(X,Best)->C=compare(X,Best),case (Id=:=22 andalso C<0)orelse(Id=:=23 andalso C>=0)of true->X;false->Best end end,First,Rest)end;
native(24,Input,_)->list_to_tuple(unique(tuple_to_list(sort(Input))));native(25,Input,_)->sort(Input);
native(26,null,_)->{};
native(26,Input,_)when is_binary(Input)->unicode:characters_to_binary(lists:reverse(unicode:characters_to_list(Input)));
native(26,Input,_)->list_to_tuple(lists:reverse(tuple_to_list(array(Input))));
native(27,Input,[Path])->getpath(Input,Path);
native(28,Input,[Path,Value])->setpath(Input,array(Path),0,Value);
native(29,Input,[Paths])->delpaths(Input,Paths);
native(30,Input,_)->lists:foldl(fun(#object{values=M},Out)->Key=entry_key(M,[<<"key">>,<<"k">>,<<"name">>,<<"Name">>,<<"K">>,<<"Key">>]),Name=case is_binary(Key)of true->Key;false->json(Key)end,with(Out,Name,maps:get(<<"value">>,M,maps:get(<<"v">>,M,null)))end,#object{},tuple_to_list(array(Input)));
native(Id,Input,[Keys])when Id=:=31;Id=:=32->array(Input),array(Keys),case tuple_size(Input)=:=tuple_size(Keys)of false->error_value(<<"sort key count">>);true->Indices=lists:seq(1,tuple_size(Keys)),Order=merge_sort(Indices,fun(A,B)->compare(element(A,Keys),element(B,Keys))=<0 end),case Id of 31->list_to_tuple([element(I,Input)||I<-Order]);32->groups(Order,Input,Keys,[],[],none)end end;
native(33,Input,_)->error_value(Input);native(34,_,[Arg])->error_value(Arg);
native(_,_,_)->error_value(<<"invalid jq native arguments">>).
entry_key(_,[])->null;
entry_key(M,[K|Ks])->V=maps:get(K,M,null),case truth(V)of true->V;false->entry_key(M,Ks)end.
groups([],_,_,Acc,[],_)->list_to_tuple(lists:reverse(Acc));
groups([],_,_,Acc,Group,_)->list_to_tuple(lists:reverse([list_to_tuple(lists:reverse(Group))|Acc]));
groups([I|Is],Input,Keys,Acc,Group,Previous)->Key=element(I,Keys),case Group=/=[] andalso compare(Key,Previous)=/=0 of true->groups(Is,Input,Keys,[list_to_tuple(lists:reverse(Group))|Acc],[element(I,Input)],Key);false->groups(Is,Input,Keys,Acc,[element(I,Input)|Group],Key)end.
