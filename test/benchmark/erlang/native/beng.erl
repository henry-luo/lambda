-module(beng).
-export([run/1,tree/1,check_tree/1,records/1]).
tree(0)->{nil,nil};
tree(D)->{tree(D-1),tree(D-1)}.
check_tree({nil,nil})->1;
check_tree({L,R})->1+check_tree(L)+check_tree(R).
binarytrees()->Stretch=check_tree(tree(11)),Long=tree(10),
    Lines=[begin Count=1 bsl(14-D),Total=lists:foldl(fun(_,S)->S+check_tree(tree(D))end,0,lists:seq(1,Count)),
        io_lib:format("~B\t trees of depth ~B\t check: ~B~n",[Count,D,Total])end||D<-[4,6,8,10]],
    iolist_to_binary([io_lib:format("stretch tree of depth 11\t check: ~B~n",[Stretch]),Lines,
        io_lib:format("long lived tree of depth 10\t check: ~B~n",[check_tree(Long)])]).
fannkuch()->{C,M}=permutations({0,1,2,3,4,5,6},erlang:make_tuple(7,0),7,0,0,0),
    iolist_to_binary(io_lib:format("~B~nPfannkuchen(7) = ~B~n",[C,M])).
settle(1,Counts)->Counts;
settle(R,Counts)->settle(R-1,setelement(R,Counts,R)).
flips([0|_],Count)->Count;
flips([K|_]=P,Count)->{Prefix,Rest}=lists:split(K+1,P),flips(lists:reverse(Prefix,Rest),Count+1).
rotate(P,R)->[First|Rest]=tuple_to_list(P),{Prefix,Tail}=lists:split(R,Rest),list_to_tuple(Prefix++[First|Tail]).
advance(P,C,7)->{P,C,7};
advance(P,C,R)->Next=rotate(P,R),Count=element(R+1,C)-1,C1=setelement(R+1,C,Count),
    case Count>0 of true->{Next,C1,R};false->advance(Next,C1,R+1)end.
permutations(P,Counts,R,Index,Checksum,Maximum)->C=settle(R,Counts),F=flips(tuple_to_list(P),0),
    Sum=Checksum+case Index rem 2 of 0->F;_->-F end,Max=max(F,Maximum),
    case advance(P,C,1)of {_,_,7}->{Sum,Max};{Next,C1,R1}->permutations(Next,C1,R1,Index+1,Sum,Max)end.
multiply(A,Transpose)->list_to_tuple([spectral_dot(A,Transpose,I,0,0.0)||I<-lists:seq(0,99)]).
spectral_dot(_,_,_,100,Total)->Total;
spectral_dot(A,T,I,J,Total)->{Row,Col}=case T of true->{J,I};false->{I,J}end,S=Row+Col,
    spectral_dot(A,T,I,J+1,Total+(1.0/(S*(S+1)div 2+Row+1))*element(J+1,A)).
ata(A)->multiply(multiply(A,false),true).
spectral_loop(0,U,V)->{U,V};
spectral_loop(N,U,_)->V=ata(U),spectral_loop(N-1,ata(V),V).
spectral()->{U,V}=spectral_loop(10,erlang:make_tuple(100,1.0),nil),
    {UV,VV}=lists:foldl(fun(I,{A,B})->X=element(I,U),Y=element(I,V),{A+X*Y,B+Y*Y}end,{0.0,0.0},lists:seq(1,100)),
    iolist_to_binary(io_lib:format("~.9f~n",[math:sqrt(UV/VV)])).
pidigits()->iolist_to_binary(pi_digits(0,0,1,0,0,1,[],[])).
pi_digits(30,_,_,_,_,_,[],Out)->lists:reverse(Out);
pi_digits(Index,K0,Q0,R0,S0,T0,Digits,Out)->K=K0+1,K2=2*K+1,
    Q=Q0*K,R=(2*Q0+R0)*K2,S=S0*K,T=(2*S0+T0)*K2,
    D3=(3*Q+R)div(3*S+T),D4=(4*Q+R)div(4*S+T),
    case Q=<R andalso D3=:=D4 of
        false->pi_digits(Index,K,Q,R,S,T,Digits,Out);
        true->Next=Index+1,Ds=[48+D3|Digits],
            {Rest,Lines}=case Next rem 10 of 0->{[],[[lists:reverse(Ds),"\t:",integer_to_binary(Next),"\n"]|Out]};_->{Ds,Out}end,
            pi_digits(Next,K,Q*10,(R-D3*T)*10,S,T,Rest,Lines)
    end.
fasta()->Alu= <<"GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA">>,
    Repeat=[binary:at(Alu,I rem byte_size(Alu))||I<-lists:seq(0,1999)],
    {Two,Seed}=random_fasta(3000,42,<<"acgtBDHKMNRSVWY">>,cumulative([0.27,0.12,0.12,0.27,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02]),[]),
    {Three,_}=random_fasta(5000,Seed,<<"acgt">>,cumulative([0.3029549426680,0.1979883004921,0.1975473066391,0.3015094502008]),[]),
    iolist_to_binary([">ONE Homo sapiens alu\n",wrap(Repeat),">TWO IUB ambiguity codes\n",wrap(Two),">THREE Homo sapiens frequency\n",wrap(Three)]).
cumulative(P)->{Values,_}=lists:mapfoldl(fun(V,S)->{V+S,V+S}end,0.0,P),list_to_tuple(Values).
choose(V,C,I)when I=:=tuple_size(C)->I-1;
choose(V,C,I)->case element(I,C)<V of true->choose(V,C,I+1);false->I-1 end.
random_fasta(0,Seed,_,_,Acc)->{lists:reverse(Acc),Seed};
random_fasta(N,Seed0,Letters,Cumulative,Acc)->Seed=(Seed0*3877+29573)rem 139968,Choice=choose(Seed/139968,Cumulative,1),
    random_fasta(N-1,Seed,Letters,Cumulative,[binary:at(Letters,Choice)|Acc]).
wrap(Cs)->wrap_binary(list_to_binary(Cs)).
wrap_binary(<<>>)->[];
wrap_binary(B)->Count=min(byte_size(B),60),<<Line:Count/binary,Rest/binary>>=B,[Line,$\n|wrap_binary(Rest)].
records(Raw)->parse_lines(binary:split(Raw,<<"\n">>,[global]),nil,[],[]).
parse_lines([],nil,_,Acc)->lists:reverse(Acc);
parse_lines([],Header,Seq,Acc)->lists:reverse([{Header,iolist_to_binary(lists:reverse(Seq))}|Acc]);
parse_lines([<<$>,Header/binary>>|Lines],nil,_,Acc)->parse_lines(Lines,Header,[],Acc);
parse_lines([<<$>,Header/binary>>|Lines],Old,Seq,Acc)->parse_lines(Lines,Header,[],[{Old,iolist_to_binary(lists:reverse(Seq))}|Acc]);
parse_lines([Line|Lines],Header,Seq,Acc)->parse_lines(Lines,Header,[Line|Seq],Acc).
upper(B)-> << <<(case C>=$a andalso C=<$z of true->C-32;false->C end)>> || <<C>> <= B >>.
lower(B)-> << <<(case C>=$A andalso C=<$Z of true->C+32;false->C end)>> || <<C>> <= B >>.
frequencies(Seq,Width)->frequencies(Seq,Width,0,#{}).
frequencies(Seq,W,I,M)when I+W>byte_size(Seq)->M;
frequencies(Seq,W,I,M)->K=binary:part(Seq,I,W),frequencies(Seq,W,I+1,maps:update_with(K,fun(V)->V+1 end,1,M)).
knucleotide(Raw)->{_,Last}=lists:last(records(Raw)),Seq=upper(Last),
    Tables=[[begin Pct=Count*100.0/(byte_size(Seq)-W+1),io_lib:format("~s ~.3f~n",[K,Pct])end||{K,Count}<-lists:sort(fun({K,A},{L,B})->A>B orelse(A=:=B andalso K<L)end,maps:to_list(frequencies(Seq,W)))]++["\n"]||W<-[1,2]],
    Counts=[io_lib:format("~B\t~s~n",[count_needle(Seq,Needle,0,0),Needle])||Needle<-[<<"GGT">>,<<"GGTA">>,<<"GGTATT">>,<<"GGTATTTTAATT">>,<<"GGTATTTTAATTTATAGT">>]],iolist_to_binary([Tables,Counts]).
count_needle(Seq,Needle,I,N)when I+byte_size(Needle)>byte_size(Seq)->N;
count_needle(Seq,Needle,I,N)->Add=case binary:part(Seq,I,byte_size(Needle))=:=Needle of true->1;false->0 end,count_needle(Seq,Needle,I+1,N+Add).
regex(Raw)->Bare=iolist_to_binary([Seq||{_,Seq}<-records(Raw)]),Seq=lower(Bare),
    Patterns=[<<"agggtaaa|tttaccct">>,<<"[cgt]gggtaaa|tttaccc[acg]">>,<<"a[act]ggtaaa|tttacc[agt]t">>,<<"ag[act]gtaaa|tttac[agt]ct">>,<<"agg[act]taaa|ttta[agt]cct">>,<<"aggg[acg]aaa|ttt[cgt]ccct">>,<<"agggt[cgt]aa|tt[acg]taccct">>,<<"agggta[cgt]a|t[acg]ataccct">>,<<"agggtaa[cgt]|[acg]aataccct">>],
    Lines=[begin N=case re:run(Seq,P,[global])of nomatch->0;{match,Matches}->length(Matches)end,
        io_lib:format("~s ~B~n",[P,N])end||P<-Patterns],
    Expanded=iolist_to_binary([expand(C)||<<C>><=Bare]),
    iolist_to_binary([Lines,io_lib:format("~n~B~n~B~n~B~n",[byte_size(Raw),byte_size(Seq),byte_size(Expanded)])]).
expand($B)->"(c|g|t)";expand($D)->"(a|g|t)";expand($H)->"(a|c|t)";expand($K)->"(g|t)";expand($M)->"(a|c)";
expand($N)->"(a|c|g|t)";expand($R)->"(a|g)";expand($S)->"(c|g)";expand($V)->"(a|c|g)";expand($W)->"(a|t)";expand($Y)->"(c|t)";expand(C)->C.
complement($A)->$T;complement($T)->$A;complement($C)->$G;complement($G)->$C;complement($M)->$K;complement($K)->$M;
complement($R)->$Y;complement($Y)->$R;complement($V)->$B;complement($B)->$V;complement($H)->$D;complement($D)->$H;complement(C)->C.
revcomp(Raw)->iolist_to_binary([[">",Header,"\n",wrap([complement(C)||C<-lists:reverse(binary_to_list(upper(Seq)))])]||{Header,Seq}<-records(Raw)]).
run(Name)->{ok,Expected}=file:read_file("test/benchmark/beng/"++Name++".txt"),
    Work=case Name of
        "binarytrees"->fun binarytrees/0;"fannkuch"->fun fannkuch/0;"spectralnorm"->fun spectral/0;
        "mandelbrot"->fun()->iolist_to_binary(io_lib:format("~B\n",[awfy:mandelbrot(500,false)]))end;
        "nbody"->fun()->iolist_to_binary(io_lib:format("~.9f\n~.9f\n",[nbody:energy(nbody:new()),nbody:energy(nbody:steps(36000,nbody:new()))]))end;
        "pidigits"->fun pidigits/0;"fasta"->fun fasta/0;
        _->{ok,Raw}=file:read_file("test/benchmark/beng/input/fasta_1000.txt"),case Name of
            "knucleotide"->fun()->knucleotide(Raw)end;"regexredux"->fun()->regex(Raw)end;"revcomp"->fun()->revcomp(Raw)end
        end
    end,
    native_bench:run(Work,fun(V)->native_bench:check(string:trim(V)=:=string:trim(Expected))end,fun io:put_chars/1,native_bench:warmup()).
