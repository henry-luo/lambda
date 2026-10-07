-module(kostya).
-export([run/1,primes/1,next/1]).
next(Seed)->(Seed*1664525+1013904223)rem 1000000.
primes(N)->A=array:set(1,false,array:set(0,false,array:new(N+1,{default,true}))),
    array:foldl(fun(_,true,C)->C+1;(_,false,C)->C end,0,sieve(2,N,A)).
sieve(I,N,A)when I*I>N->A;
sieve(I,N,A)->B=case array:get(I,A)of true->strike(I*I,I,N,A);false->A end,sieve(I+1,N,B).
strike(J,_,N,A)when J>N->A;
strike(J,I,N,A)->strike(J+I,I,N,array:set(J,false,A)).
collatz(1,N)->N;
collatz(V,N)when V band 1=:=0->collatz(V div 2,N+1);
collatz(V,N)->collatz(3*V+1,N+1).
longest(1000000,_,Start)->Start;
longest(I,Longest,Start)->Length=collatz(I,1),case Length>Longest of
    true->longest(I+1,Length,I);false->longest(I+1,Longest,Start)end.
encode(Input)->Table= <<"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/">>,iolist_to_binary(encode(Input,Table,[])).
encode(<<A,B,C,Rest/binary>>,T,Acc)->Chunk= <<(binary:at(T,A bsr 2)),(binary:at(T,((A band 3)bsl 4)bor(B bsr 4))),
    (binary:at(T,((B band 15)bsl 2)bor(C bsr 6))),(binary:at(T,C band 63))>>,encode(Rest,T,[Chunk|Acc]);
encode(<<A,B>>,T,Acc)->lists:reverse([<<(binary:at(T,A bsr 2)),(binary:at(T,((A band 3)bsl 4)bor(B bsr 4))),
    (binary:at(T,(B band 15)bsl 2)),$=>>|Acc]);
encode(<<A>>,T,Acc)->lists:reverse([<<(binary:at(T,A bsr 2)),(binary:at(T,(A band 3)bsl 4)),$=,$=>>|Acc]);
encode(<<>>,_,Acc)->lists:reverse(Acc).
base64_work()->Input=binary:copy(<<$a>>,10000),base64_repeat(100,Input,{0,0}).
base64_repeat(0,_,Result)->Result;
base64_repeat(N,Input,_)->Encoded=encode(Input),Size=byte_size(Encoded),
    Pad=case Encoded of <<_:(Size-2)/binary,"==">>->2;<<_:(Size-1)/binary,"=">>->1;_->0 end,
    base64_repeat(N-1,Input,{Size,Size div 4*3-Pad}).
distance(Left,Right)->Ls=binary_to_list(Left),Rs=binary_to_list(Right),distance_rows(Ls,Rs,lists:seq(0,length(Rs)),1).
distance_rows([],_,Previous,_)->lists:last(Previous);
distance_rows([C|Cs],Right,[Diag|Previous],I)->
    Current=lists:reverse(distance_cols(C,Right,Previous,Diag,I,[I])),distance_rows(Cs,Right,Current,I+1).
distance_cols(_,[],[],_,_,Acc)->Acc;
distance_cols(C,[R|Rs],[Above|Rest],Diag,Left,Acc)->Cost=case C=:=R of true->0;false->1 end,
    Value=min(min(Above+1,Left+1),Diag+Cost),distance_cols(C,Rs,Rest,Above,Value,[Value|Acc]).
matrices(0,Seed,A,B)->{list_to_tuple(lists:reverse(A)),list_to_tuple(lists:reverse(B)),Seed};
matrices(N,Seed0,A,B)->S1=next(Seed0),S2=next(S1),matrices(N-1,S2,[S1 rem 2000/1000.0-1.0|A],[S2 rem 2000/1000.0-1.0|B]).
matmul()->{A,B,_}=matrices(40000,42,[],[]),
    C=[dot(A,B,I,J,0,0.0)||I<-lists:seq(0,199),J<-lists:seq(0,199)],floor(lists:foldl(fun(V,S)->S+V end,0.0,C)).
dot(_,_,_,_,200,Sum)->Sum;
dot(A,B,I,J,K,Sum)->dot(A,B,I,J,K+1,Sum+element(I*200+K+1,A)*element(K*200+J+1,B)).
brainfuck()->Program= <<"++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.">>,
    Jumps=jumps(Program,0,[],#{}),bf_repeat(10000,Program,Jumps,<<>>).
jumps(P,I,[],J)when I=:=byte_size(P)->J;
jumps(P,I,Stack,J)->case binary:at(P,I)of
    $[->jumps(P,I+1,[I|Stack],J);
    $]->[Open|Rest]=Stack,jumps(P,I+1,Rest,J#{I=>Open,Open=>I});
    _->jumps(P,I+1,Stack,J)
end.
bf_repeat(0,_,_,Out)->Out;
bf_repeat(N,P,J,_)->Out=bf(P,J,0,0,array:new(30000,{default,0}),[]),bf_repeat(N-1,P,J,Out).
bf(P,_,I,_,_,Out)when I>=byte_size(P)->list_to_binary(lists:reverse(Out));
bf(P,J,I,D,Tape,Out)->case binary:at(P,I)of
    $+->bf(P,J,I+1,D,array:set(D,(array:get(D,Tape)+1)band 255,Tape),Out);
    $-->bf(P,J,I+1,D,array:set(D,(array:get(D,Tape)-1)band 255,Tape),Out);
    $>->bf(P,J,I+1,D+1,Tape,Out);$<->bf(P,J,I+1,D-1,Tape,Out);
    $.->bf(P,J,I+1,D,Tape,[array:get(D,Tape)|Out]);
    $[->Next=case array:get(D,Tape)of 0->maps:get(I,J)+1;_->I+1 end,bf(P,J,Next,D,Tape,Out);
    $]->Next=case array:get(D,Tape)of 0->I+1;_->maps:get(I,J)+1 end,bf(P,J,Next,D,Tape,Out);
    _->bf(P,J,I+1,D,Tape,Out)
end.
json()->json_repeat(10,0).
json_repeat(0,Length)->Length;
json_repeat(N,_)->{Parts,_}=json_objects(0,42,[]),Text=iolist_to_binary(["[",lists:join($,,lists:reverse(Parts)),"]"]),json_repeat(N-1,byte_size(Text)).
json_objects(1000,Seed,Acc)->{Acc,Seed};
json_objects(I,Seed0,Acc)->S1=next(Seed0),S2=next(S1),S3=next(S2),S4=next(S3),
    Text=["{\"id\":",integer_to_binary(S1 rem 10000),",\"score\":",integer_to_binary(S4 rem 100),
        ",\"coord\":{\"x\":",integer_to_binary((S2 rem 20000-10000)div 100),",\"y\":",integer_to_binary((S3 rem 20000-10000)div 100),"},\"active\":true}"],
    json_objects(I+1,S4,[Text|Acc]).
run(Name)->{Work,Verify,Report}=case Name of
    "primes"->{fun()->primes(1000000)end,fun(V)->native_bench:check(V=:=78498)end,fun(V)->io:format("primes: PASS (~B)~n",[V])end};
    "collatz"->{fun()->longest(1,0,0)end,fun(V)->native_bench:check(V=:=837799)end,fun(V)->io:format("collatz: PASS (start=~B)~n",[V])end};
    "base64"->{fun base64_work/0,fun(V)->native_bench:check(V=:={13336,10000})end,fun({A,B})->io:format("base64: encoded_len=~B decoded_len=~B~nbase64: PASS~n",[A,B])end};
    "levenshtein"->{fun()->[distance(<<"kitten">>,<<"sitting">>),distance(<<"saturday">>,<<"sunday">>),distance(binary:copy(<<"a">>,500),binary:copy(<<"b">>,500)),distance(binary:copy(<<"ab">>,200),binary:copy(<<"ba">>,200))]end,
        fun(V)->native_bench:check(V=:=[3,3,500,2])end,fun([A,B,C,D])->io:format("levenshtein: d(kitten,sitting)=~B~nlevenshtein: d(saturday,sunday)=~B~nlevenshtein: d(aaa...,bbb...)=~B~nlevenshtein: d(ababab...,babab...)=~B~nlevenshtein: PASS~n",[A,B,C,D])end};
    "matmul"->{fun matmul/0,fun(V)->native_bench:check(V=:=-29562)end,fun(V)->io:format("matmul: sum=~B~nmatmul: DONE~n",[V])end};
    "brainfuck"->{fun brainfuck/0,fun(V)->native_bench:check(V=:= <<"Hello World!\n">>)end,fun(V)->io:put_chars(V)end};
    "json_gen"->{fun json/0,fun(V)->native_bench:check(V=:=61626)end,fun(V)->io:format("json_gen: length=~B~njson_gen: PASS~n",[V])end}
end,native_bench:run(Work,Verify,Report,native_bench:warmup()).
