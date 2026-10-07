%% Native indexed loops for the ASCII text_search.js corpus. See ../native_ports/LICENSE.md.
-module(text_search).
-export([run/1,naive/3,kmp/3,boyer_moore/3]).

prepare()->
    Corpus=iolist_to_binary(lists:join(<<"\n">>,
        [io_lib:format("record-~B alpha aaaaaaaaaaaaaaaaaaaaaaaa token-~B omega needle-~B",[I,I rem 23,I rem 11])||I<-lists:seq(0,511)])),
    {Corpus,[<<"record-0 alpha">>,<<"record-2048 alpha">>,<<"token-22 omega">>,<<"needle-10">>,<<"omega needle-7">>,
        <<"alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3">>,<<"missing-marker">>,<<"record-2047 omega">>]}.

naive(_,<<>>,Start)->Start;
naive(Text,Pattern,Start)->naive_at(Text,Pattern,Start).
naive_at(Text,Pattern,Position)when Position>byte_size(Text)-byte_size(Pattern)->-1;
naive_at(Text,Pattern,Position)->case matches(Text,Pattern,Position,0)of
    true->Position;false->naive_at(Text,Pattern,Position+1)end.
matches(_,Pattern,_,Offset)when Offset=:=byte_size(Pattern)->true;
matches(Text,Pattern,Position,Offset)->case binary:at(Text,Position+Offset)=:=binary:at(Pattern,Offset)of
    true->matches(Text,Pattern,Position,Offset+1);false->false end.

kmp(_,<<>>,Start)->Start;
kmp(Text,Pattern,Start)->Table=prefix(Pattern,1,0,array:new(byte_size(Pattern),{default,0})),kmp_at(Text,Pattern,Table,Start,0).
prefix(Pattern,Index,_,Table)when Index=:=byte_size(Pattern)->Table;
prefix(Pattern,Index,Length,Table)->case binary:at(Pattern,Index)=:=binary:at(Pattern,Length)of
    true->prefix(Pattern,Index+1,Length+1,array:set(Index,Length+1,Table));
    false when Length>0->prefix(Pattern,Index,array:get(Length-1,Table),Table);
    false->prefix(Pattern,Index+1,Length,Table)end.
kmp_at(Text,_,_,TextIndex,_)when TextIndex=:=byte_size(Text)->-1;
kmp_at(Text,Pattern,Table,TextIndex,PatternIndex)->case binary:at(Text,TextIndex)=:=binary:at(Pattern,PatternIndex)of
    true when PatternIndex+1=:=byte_size(Pattern)->TextIndex+1-byte_size(Pattern);
    true->kmp_at(Text,Pattern,Table,TextIndex+1,PatternIndex+1);
    false when PatternIndex>0->kmp_at(Text,Pattern,Table,TextIndex,array:get(PatternIndex-1,Table));
    false->kmp_at(Text,Pattern,Table,TextIndex+1,0)end.

boyer_moore(_,<<>>,Start)->Start;
boyer_moore(Text,Pattern,Start)->Last=last_table(Pattern,0,array:new(256,{default,-1})),bm_at(Text,Pattern,Last,Start).
last_table(Pattern,Index,Last)when Index=:=byte_size(Pattern)->Last;
last_table(Pattern,Index,Last)->last_table(Pattern,Index+1,array:set(binary:at(Pattern,Index),Index,Last)).
bm_at(Text,Pattern,_,Position)when Position>byte_size(Text)-byte_size(Pattern)->-1;
bm_at(Text,Pattern,Last,Position)->Offset=bm_offset(Text,Pattern,Position,byte_size(Pattern)-1),case Offset<0 of
    true->Position;false->bm_at(Text,Pattern,Last,Position+max(1,Offset-array:get(binary:at(Text,Position+Offset),Last)))end.
bm_offset(_,_,_,Offset)when Offset<0->Offset;
bm_offset(Text,Pattern,Position,Offset)->case binary:at(Text,Position+Offset)=:=binary:at(Pattern,Offset)of
    true->bm_offset(Text,Pattern,Position,Offset-1);false->Offset end.

workload({Text,Patterns})->lists:foldl(fun(Round,Checksum)->
    {Result,_}=lists:foldl(fun(Pattern,{Acc,Index})->Start=(Round*17+Index*13)rem 97,
        N=naive(Text,Pattern,Start),K=kmp(Text,Pattern,Start),B=boyer_moore(Text,Pattern,Start),
        pr:check(N=:=K andalso K=:=B),{(Acc+(N+2)*(Index+3)+(Round+1)*7)rem 1000000007,Index+1}
    end,{Checksum,0},Patterns),Result end,0,lists:seq(0,1535)).

run(E)->pr:run_benchmark(E,[
    {fn,E,fun(_,[])->prepare()end,[],0,0},
    {fn,E,fun(_,[_,State])->workload(State)end,[<<"Any">>,<<"Any">>],2,2},
    {fn,E,fun(_,[V])->V=:=91395120 end,[<<"Any">>],1,1},
    {fn,E,fun(_,[V])->pr:output([<<"text_search: CHECKSUM:">>,V],true)end,[<<"Any">>],1,1}]).
