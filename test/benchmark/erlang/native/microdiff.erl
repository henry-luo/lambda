-module(microdiff).
-export([prepare/0,work/1]).
snapshot(V)->Title=case V of true-> <<"Text benchmark — revised"/utf8>>;false-> <<"Text benchmark">>end,
    #{<<"document">>=>#{<<"title">>=>Title,<<"sections">>=>[
        #{<<"id">>=><<"intro">>,<<"blocks">>=>[#{<<"type">>=><<"paragraph">>,<<"text">>=><<"A short paragraph of source text.">>},#{<<"type">>=><<"code">>,<<"language">>=><<"js">>,<<"lines">>=>choose(V,18,12)}]},
        #{<<"id">>=><<"body">>,<<"blocks">>=>[#{<<"type">>=><<"heading">>,<<"level">>=>choose(V,2,1),<<"text">>=><<"Algorithms">>},#{<<"type">>=><<"list">>,<<"items">>=>choose(V,[<<"diff">>,<<"snapshot">>,<<"hyphen">>],[<<"diff">>,<<"snapshot">>])}]}]},
      <<"options">>=>#{<<"theme">>=>choose(V,<<"dark">>,<<"light">>),<<"flags">>=>#{<<"trackChanges">>=>V,<<"preserveWhitespace">>=>true}},
      <<"tags">>=>choose(V,[<<"text">>,<<"benchmark">>,<<"updated">>],[<<"text">>,<<"benchmark">>]),<<"updated">>=>{rich,date,choose(V,1700000001000,1700000000000)},
      <<"pattern">>=>{rich,regexp,choose(V,<<"/source|text|diff/gi">>,<<"/source|text/g">>)},<<"value">>=>choose(V,42,41)}.
choose(true,A,_)->A;choose(false,_,B)->B.
items(M)when is_map(M)->maps:to_list(M);
items(L)when is_list(L)->lists:zip(lists:seq(0,length(L)-1),L).
find(M,K)when is_map(M)->maps:find(K,M);
find(L,K)when is_list(L)->case K<length(L)of true->{ok,lists:nth(K+1,L)};false->error end.
kind(V)when is_map(V)->object;kind(V)when is_list(V)->array;kind(_)->scalar.
diff(Old,New)->
    Before=lists:flatmap(fun({K,A})->case find(New,K)of error->[{<<"REMOVE">>,[K],A,undefined}];{ok,B}->
        case kind(A)=/=scalar andalso kind(A)=:=kind(B)of true->[{Type,[K|Path],O,V}||{Type,Path,O,V}<-diff(A,B)];false when A=/=B->[{<<"CHANGE">>,[K],A,B}];false->[]end end end,items(Old)),
    Before++[{<<"CREATE">>,[K],undefined,V}||{K,V}<-items(New),find(Old,K)=:=error].
prepare()->[{snapshot(I rem 2=:=0),snapshot(I rem 2=/=0)}||I<-lists:seq(0,3)].
work(Pairs)->lists:foldl(fun(_,S)->lists:foldl(fun({Old,New},A)->Ds=diff(Old,New),lists:foldl(fun({Type,Path,_,_},Acc)->(Acc+byte_size(Type)*23+length(Path))rem 1000000007 end,(A+length(Ds)*19)rem 1000000007,Ds)end,S,Pairs)end,0,lists:seq(1,512)).
