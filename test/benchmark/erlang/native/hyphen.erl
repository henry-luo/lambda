-module(hyphen).
-export([prepare/0,work/1]).
-record(tables,{root,first,count,code,child,level,offset,length,values,exceptions}).
-record(hyphenator,{tables,words=#{},markers=#{}}).
ints(M,K)->list_to_tuple(maps:get(K,M)).
at(T,I)->element(I+1,T).
load()->{ok,B}=file:read_file("test/benchmark/text/hyphen_tables.json"),M=json:decode(B),Markers=ints(M,<<"exception_markers">>),
    Exceptions=maps:from_list([{Word,[at(Markers,I)||I<-lists:seq(Offset,Offset+Count-1)]}||{Word,Offset,Count}<-lists:zip3(maps:get(<<"exception_words">>,M),maps:get(<<"exception_offsets">>,M),maps:get(<<"exception_counts">>,M))]),
    #tables{root=maps:get(<<"root">>,M),first=ints(M,<<"node_first">>),count=ints(M,<<"node_count">>),code=ints(M,<<"edge_code">>),child=ints(M,<<"edge_child">>),level=ints(M,<<"node_level">>),offset=ints(M,<<"level_offsets">>),length=ints(M,<<"level_lengths">>),values=ints(M,<<"level_values">>),exceptions=Exceptions}.
child(T,Node,Code)->First=at(T#tables.first,Node),child_at(T,Code,First,First+at(T#tables.count,Node)).
child_at(_,_,End,End)->-1;
child_at(T,C,I,End)->case at(T#tables.code,I)=:=C of true->at(T#tables.child,I);false->child_at(T,C,I+1,End)end.
markers(H,Word)->Lower=string:lowercase(Word),T=H#hyphenator.tables,
    case maps:find(Lower,T#tables.exceptions)of {ok,M}->{M,H};error->case maps:find(Lower,H#hyphenator.markers)of {ok,M}->{M,H};error->
        Len=byte_size(Word),Ext= <<".",Lower/binary,".">>,Levels=lists:foldl(fun(Start,A)->Position=case Start of 0->0;_->Start-1 end,scan(T,Ext,Start,Len+2,T#tables.root,Position,A)end,erlang:make_tuple(Len+1,0),lists:seq(0,Len-1)),
        Cleared=setelement(Len+1,setelement(Len,setelement(2,setelement(1,Levels,0),0),0),0),M=[I||I<-lists:seq(0,Len),at(Cleared,I)band 1=/=0],{M,H#hyphenator{markers=(H#hyphenator.markers)#{Lower=>M}}}end end.
scan(_,_,End,End,_,_,Levels)->Levels;
scan(T,Ext,I,End,Node,Position,Levels)->N=child(T,Node,binary:at(Ext,I)),case N<0 of true->Levels;false->L=at(T#tables.level,N),Next=case L<0 of true->Levels;false->Offset=at(T#tables.offset,L),Count=at(T#tables.length,L),lists:foldl(fun(K,A)->Target=Position+K,Value=at(T#tables.values,Offset+K),case Target>=0 andalso Target<tuple_size(A) andalso Value>at(A,Target)of true->setelement(Target+1,A,Value);false->A end end,Levels,lists:seq(0,Count-1))end,scan(T,Ext,I+1,End,N,Position,Next)end.
word(H,Word)->case maps:find(Word,H#hyphenator.words)of {ok,Value}->{Value,H};error->{Result,Next}=case byte_size(Word)<5 orelse binary:match(Word,<<"-">>)=/=nomatch of true->{Word,H};false->{Marks,A}=markers(H,Word),{iolist_to_binary(insert(Word,0,Marks)),A}end,{Result,Next#hyphenator{words=(Next#hyphenator.words)#{Word=>Result}}}end.
insert(<<>>,_,Marks)->lists:duplicate(length(Marks),$-);
insert(<<C,Rest/binary>>,I,[I|Marks])->[$-,C|insert(Rest,I+1,Marks)];
insert(<<C,Rest/binary>>,I,Marks)->[C|insert(Rest,I+1,Marks)].
letter(C)->(C>=$A andalso C=<$Z)orelse(C>=$a andalso C=<$z).
word_char(C)->letter(C)orelse C=:=$'.
word_end(S,I,Start)when I=:=byte_size(S)->I;
word_end(S,I,Start)->C=binary:at(S,I),case word_char(C)orelse(C=:=$- andalso I>Start andalso I+1<byte_size(S) andalso letter(binary:at(S,I+1)))of true->word_end(S,I+1,Start);false->I end.
text(H,S)->text(H,S,0,[]).
text(H,S,I,Acc)when I=:=byte_size(S)->{iolist_to_binary(lists:reverse(Acc)),H};
text(H,S,I,Acc)->C=binary:at(S,I),case C=:=$< andalso I+1<byte_size(S) andalso(letter(binary:at(S,I+1))orelse binary:at(S,I+1)=:=$/)of
    true->End=case binary:match(S,<<">">>,[{scope,{I,byte_size(S)-I}}])of nomatch->byte_size(S);{At,_}->At+1 end,text(H,S,End,[binary:part(S,I,End-I)|Acc]);
    false->case word_char(C)of true->End=word_end(S,I,I),{Result,Next}=word(H,binary:part(S,I,End-I)),text(Next,S,End,[Result|Acc]);false->text(H,S,I+1,[C|Acc])end end.
prepare()->T=load(),{ok,B}=file:read_file("test/benchmark/text/hyphen_cases.json"),Cases=json:decode(B),lists:foldl(fun([Source,Expected],H)->{Result,Next}=text(H,Source),native_bench:check(Result=:=Expected),Next end,#hyphenator{tables=T},Cases),{T,Cases}.
work({T,Cases})->lists:foldl(fun(_,S)->{Sum,_,_}=lists:foldl(fun([Source,_],{A,H,I})->{Result,Next}=text(H,Source),B=(A+byte_size(Result)*29)rem 1000000007,C=case byte_size(Result)of 0->0;N->binary:at(Result,I rem N)end,{(B+C)rem 1000000007,Next,I+1}end,{S,#hyphenator{tables=T},0},Cases),Sum end,0,lists:seq(1,32)).
