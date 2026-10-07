%% Direct scalar algorithms from julia/SUITE.md; native integers/tuples/binaries.
-module(micro).
-export([run/1,decimal/1]).
-define(MOD,1000000007).
decimal(Value)->N=abs(Value),Prefix=case Value<0 of true->[$-];false->[]end,
    list_to_binary(Prefix++digits(N,divisor(N,1))).
divisor(N,D)when N div D>=10->divisor(N,D*10);
divisor(_,D)->D.
digits(_,0)->[];
digits(N,D)->[48+N div D|digits(N rem D,D div 10)].
parse(<<$-,Rest/binary>>)->-parse_digits(Rest,0);
parse(B)->parse_digits(B,0).
parse_digits(<<>>,N)->N;
parse_digits(<<C,Rest/binary>>,N)->parse_digits(Rest,N*10+C-48).
integers()->integers(0,42,0,0,0).
integers(100000,Seed,Checksum,Size,Errors)->[Checksum,Size,Seed,Errors];
integers(I,Seed0,Checksum,Size,Errors)->Seed=Seed0*16807 rem 2147483647,
    Value=case I rem 8 of 0->0;1->-Seed;_->Seed end,
    Text=decimal(Value),Parsed=parse(Text),Error=case Parsed=:=Value of true->0;false->1 end,
    integers(I+1,Seed,(Checksum*31+Parsed+2147483647)rem ?MOD,Size+byte_size(Text),Errors+Error).
gram(A,Rows,Cols)->list_to_tuple([gram_dot(A,Rows,Cols,I,J,0,0.0)||I<-lists:seq(0,Cols-1),J<-lists:seq(0,Cols-1)]).
gram_dot(_,Rows,_,_,_,Rows,Total)->Total;
gram_dot(A,Rows,Cols,I,J,K,Total)->gram_dot(A,Rows,Cols,I,J,K+1,Total+element(K*Cols+I+1,A)*element(K*Cols+J+1,A)).
square(A,N)->list_to_tuple([square_dot(A,N,I,J,0,0.0)||I<-lists:seq(0,N-1),J<-lists:seq(0,N-1)]).
square_dot(_,N,_,_,N,Total)->Total;
square_dot(A,N,I,J,K,Total)->square_dot(A,N,I,J,K+1,Total+element(I*N+K+1,A)*element(K*N+J+1,A)).
trace(A,Rows,Cols)->Fourth=square(square(gram(A,Rows,Cols),Cols),Cols),trace_diagonal(Fourth,Cols,0,0.0).
trace_diagonal(_,N,N,Total)->Total;
trace_diagonal(A,N,I,Total)->trace_diagonal(A,N,I+1,Total+element(I*N+I+1,A)).
randoms(0,Seed,Acc)->{list_to_tuple(lists:reverse(Acc)),Seed};
randoms(N,Seed0,Acc)->Seed=Seed0*16807 rem 2147483647,randoms(N-1,Seed,[Seed/2147483647.0*2.0-1.0|Acc]).
variation(Values)->Mean=lists:foldl(fun(V,S)->S+V end,0.0,Values)/length(Values),
    Sum=lists:foldl(fun(V,S)->D=V-Mean,S+D*D end,0.0,Values),math:sqrt(Sum/(length(Values)-1))/Mean.
matrices()->matrices(0,42,0,[],[]).
matrices(1000,Seed,Digest,V0,W0)->V=lists:reverse(V0),W=lists:reverse(W0),
    [floor(variation(V)*1.0e9),floor(variation(W)*1.0e9),Digest,Seed];
matrices(I,Seed0,Digest0,V,W)->
    {Blocks,Seed}=randoms(100,Seed0,[]),
    P=list_to_tuple([element(B*25+R*5+C+1,Blocks)||R<-lists:seq(0,4),B<-lists:seq(0,3),C<-lists:seq(0,4)]),
    Q=list_to_tuple([element((R div 5*2+C div 5)*25+(R rem 5)*5+C rem 5+1,Blocks)||R<-lists:seq(0,9),C<-lists:seq(0,9)]),
    A=trace(P,5,20),B=trace(Q,10,10),Digest1=(Digest0*31+floor(A*1000))rem ?MOD,
    matrices(I+1,Seed,(Digest1*31+floor(B*1000))rem ?MOD,[A|V],[B|W]).
pi_sum(K,End,Total)when K>End->Total;
pi_sum(K,End,Total)->F=float(K),pi_sum(K+1,End,Total+1.0/(F*F)).
pi()->Values=[pi_sum(1,10000+R,0.0)||R<-lists:seq(0,499)],
    {Digest,_}=lists:foldl(fun(V,{Sum,I})->{Sum+V*I,I+1}end,{0.0,1},Values),
    [floor(hd(Values)*1.0e12),floor(lists:last(Values)*1.0e12),floor(Digest*1.0e6),5124750].
hash(<<>>,Digest)->Digest;
hash(<<C,Rest/binary>>,Digest)->hash(Rest,(Digest*31+C)rem ?MOD).
formatted()->Sink=case os:type()of {win32,_}->"NUL";_->"/dev/null"end,formatted(1,0,0,0,[],Sink).
formatted(100001,Size,Digest,Writes,[],_)->[Size,Digest,Writes,100000];
formatted(I,Size,Digest,Writes,Buffer,Sink)->
    A=decimal(I),B=decimal(I+1),Line= <<A/binary," ",B/binary,"\n">>,
    Next=[Line|Buffer],
    {Rest,Count}=case I rem 256=:=0 orelse I=:=100000 of
        true->ok=file:write_file(Sink,lists:reverse(Next),[raw,binary]),{[],Writes+1};
        false->{Next,Writes}
    end,
    formatted(I+1,Size+byte_size(Line),hash(Line,Digest),Count,Rest,Sink).
run(Name)->{Work,Expected}=case Name of
    "parse_integers"->{fun integers/0,[592470661,854479,1966931148,0]};
    "matrix_statistics"->{fun matrices/0,[464726438,486656926,47509838,1966931148]};
    "iteration_pi_sum"->{fun pi/0,[1644834071848,1644838824217,206015869118,5124750]};
    "formatted_output"->{fun formatted/0,[1177795,584298900,391,100000]}
end,
native_bench:run(Work,fun(V)->native_bench:check(V=:=Expected)end,
    fun([A,B,C,D])->io:format("~s: PASS ~B ~B ~B ~B~n",[Name,A,B,C,D])end,true).
