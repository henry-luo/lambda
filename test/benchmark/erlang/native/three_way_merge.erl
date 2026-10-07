-module(three_way_merge).
-export([prepare/0,work/1]).
variant(Base,Side)->[begin Suffix=case I rem 17 of 0->[" ",Side," edit ",integer_to_binary(I rem 31)," keeps the paragraph useful"];_ when Side=:= <<"left">>,I rem 23=:=0-> <<" left-only annotation">>;_ when Side=:= <<"right">>,I rem 29=:=0-> <<" right-only annotation">>;_-> <<>>end,iolist_to_binary([Line,Suffix])end||{I,Line}<-lists:zip(lists:seq(0,767),Base)].
prepare()->Base=[iolist_to_binary(["section ",integer_to_binary(I)," records the base document with stable words for merging and review"])||I<-lists:seq(0,767)],{Base,variant(Base,<<"left">>),variant(Base,<<"right">>)}.
word([],_) -> <<>>;
word(T,I)when I>tuple_size(T)-> <<>>;
word(T,I)->element(I,T).
merge(_,Same,Same)->Same;
merge(Base,Base,Right)->Right;
merge(Base,Left,Base)->Left;
merge(Base,Left,Right)->B=list_to_tuple(binary:split(Base,<<" ">>,[global])),L=list_to_tuple(binary:split(Left,<<" ">>,[global])),R=list_to_tuple(binary:split(Right,<<" ">>,[global])),
    Words=lists:flatmap(fun(I)->Bv=word(B,I),Lv=word(L,I),Rv=word(R,I),case {Lv,Rv}of {Same,Same}->[Same];{Bv,_}->[Rv];{_,Bv}->[Lv];_->[<<"<<<<<<< LEFT">>,Lv,<<"=======">>,Rv,<<">>>>>>> RIGHT">>]end end,lists:seq(1,max(tuple_size(B),max(tuple_size(L),tuple_size(R))))),iolist_to_binary(lists:join(<<" ">>,Words)).
lines({Base,Left,Right})->iolist_to_binary(lists:join(<<"\n">>,[merge(B,L,R)||{B,L,R}<-lists:zip3(Base,Left,Right)])).
work(State)->lists:foldl(fun(R,S)->Merged=lines(State),(S+byte_size(Merged)*31+binary:at(Merged,(R*37)rem byte_size(Merged)))rem 1000000007 end,0,lists:seq(0,10999)).
