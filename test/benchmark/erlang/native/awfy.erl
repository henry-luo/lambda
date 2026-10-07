-module(awfy).
-export([run/3,mandelbrot/2]).
random(S)->(S*1309+13849)band 65535.
sieve(I,N,A,C)when I>N->C;
sieve(I,N,A,C)->case array:get(I,A)of
    true->sieve(I+1,N,strike(2*I,I,N,A),C+1);false->sieve(I+1,N,A,C)end.
strike(J,_,N,A)when J>N->A;
strike(J,I,N,A)->strike(J+I,I,N,array:set(J,false,A)).
permute(A,0,C)->{A,C+1};
permute(A,N,C)->{B,C1}=permute(A,N-1,C+1),permute_indices(B,N-1,N-1,C1).
permute_indices(A,_,I,C)when I<0->{A,C};
permute_indices(A,N,I,C)->B=swap(A,N,I),{D,C1}=permute(B,N,C),permute_indices(swap(D,N,I),N,I-1,C1).
swap(A,I,J)->X=element(I+1,A),Y=element(J+1,A),setelement(J+1,setelement(I+1,A,Y),X).
queens(8,_,_,_)->true;
queens(Col,Rows,Max,Min)->queen_rows(0,Col,Rows,Max,Min).
queen_rows(8,_,_,_,_)->false;
queen_rows(Row,Col,Rows,Max,Min)->R=1 bsl Row,A=1 bsl(Col+Row),B=1 bsl(Col-Row+8),
    case Rows band R=:=0 andalso Max band A=:=0 andalso Min band B=:=0 andalso queens(Col+1,Rows bor R,Max bor A,Min bor B)of
        true->true;false->queen_rows(Row+1,Col,Rows,Max,Min)end.
tower_move(Piles,From,To)->[Top|Rest]=element(From,Piles),Destination=element(To,Piles),
    native_bench:check(Destination=:=[] orelse Top<hd(Destination)),setelement(To,setelement(From,Piles,Rest),[Top|Destination]).
towers(1,From,To,Piles)->{tower_move(Piles,From,To),1};
towers(N,From,To,Piles)->Other=6-From-To,{A,C1}=towers(N-1,From,Other,Piles),B=tower_move(A,From,To),{C,C2}=towers(N-1,Other,To,B),{C,C1+C2+1}.
make_list(0)->nil;
make_list(N)->{N,make_list(N-1)}.
shorter(_,nil)->false;
shorter(nil,_)->true;
shorter({_,X},{_,Y})->shorter(X,Y).
tail(X,Y,Z)->case shorter(Y,X)of true->{_,Xn}=X,{_,Yn}=Y,{_,Zn}=Z,tail(tail(Xn,Y,Z),tail(Yn,Z,X),tail(Zn,X,Y));false->Z end.
list_length(nil)->0;
list_length({_,Rest})->1+list_length(Rest).
storage(1,Seed0)->Seed=random(Seed0),{erlang:make_tuple(Seed rem 10+1,0),Seed,1};
storage(Depth,Seed0)->{Nodes,Seed,Count}=storage_children(4,Depth-1,Seed0,[],1),{list_to_tuple(Nodes),Seed,Count}.
storage_children(0,_,Seed,Acc,Count)->{lists:reverse(Acc),Seed,Count};
storage_children(N,D,Seed0,Acc,Count)->{Node,Seed,C}=storage(D,Seed0),storage_children(N-1,D,Seed,[Node|Acc],Count+C).
balls(0,Seed,Acc)->{lists:reverse(Acc),Seed};
balls(N,Seed0,Acc)->S1=random(Seed0),S2=random(S1),S3=random(S2),S4=random(S3),
    balls(N-1,S4,[{S1 rem 500,S2 rem 500,S3 rem 300-150,S4 rem 300-150}|Acc]).
bounce({X,Y,Vx,Vy})->A=X+Vx,B=Y+Vy,
    {Nx,Nvx,Bx}=case A of _ when A>500->{500,-abs(Vx),1};_ when A<0->{0,abs(Vx),1};_->{A,Vx,0}end,
    {Ny,Nvy,By}=case B of _ when B>500->{500,-abs(Vy),1};_ when B<0->{0,abs(Vy),1};_->{B,Vy,0}end,
    {{Nx,Ny,Nvx,Nvy},max(Bx,By)}.
bounce_rounds(0,_,Count)->Count;
bounce_rounds(N,Balls,Count)->{Next,Add}=lists:mapfoldl(fun(B,S)->{V,C}=bounce(B),{V,S+C}end,0,Balls),bounce_rounds(N-1,Next,Count+Add).
mandelbrot(Size,Som)->mandel_rows(0,Size,Som,0).
mandel_rows(N,N,_,Sum)->Sum;
mandel_rows(Y,N,Som,Sum)->Next=mandel_cols(0,Y,N,Som,0,0,Sum),mandel_rows(Y+1,N,Som,Next).
mandel_cols(N,_,N,_,_,_,Sum)->Sum;
mandel_cols(X,Y,N,Som,Byte,Bits,Sum)->Cr=2.0*X/N-1.5,Ci=2.0*Y/N-1.0,
    Escaped=mandel_iter(Cr,Ci,0.0,0.0,0.0,0.0,0,Som),Bit=case {Som,Escaped}of {true,true}->1;{true,false}->0;{false,true}->0;{false,false}->1 end,
    Acc=(Byte bsl 1)+Bit,Count=Bits+1,
    case Count=:=8 orelse X=:=N-1 of true->mandel_cols(X+1,Y,N,Som,0,0,Sum bxor(Acc bsl(8-Count)));false->mandel_cols(X+1,Y,N,Som,Acc,Count,Sum)end.
mandel_iter(_,_,_,_,_,_,50,_)->false;
mandel_iter(Cr,Ci,Zr,Zi,R,I,K,Som)->Nr=R-I+Cr,Ni=case Som of true->2.0*Nr*Zi+Ci;false->2.0*Zr*Zi+Ci end,
    R1=Nr*Nr,I1=Ni*Ni,case R1+I1>4.0 of true->true;false->mandel_iter(Cr,Ci,Nr,Ni,R1,I1,K+1,Som)end.
once("sieve",_)->sieve(2,5000,array:new(5001,{default,true}),0)=:=669;
once("permute",_)->{_,C}=permute({0,0,0,0,0,0},6,0),C=:=8660;
once("queens",_)->lists:all(fun(_)->queens(0,0,0,0)end,lists:seq(1,10));
once("towers",_)->{_,C}=towers(13,1,2,{lists:seq(0,13),[],[]}),C=:=8191;
once("list",_)->list_length(tail(make_list(15),make_list(10),make_list(6)))=:=10;
once("storage",_)->{Root,_,C}=storage(7,74755),native_bench:check(tuple_size(Root)=:=4),C=:=5461;
once("bounce",_)->{Balls,_}=balls(100,74755,[]),bounce_rounds(50,Balls,0)=:=1331;
once("json",Fixture)->Value=json:decode(Fixture),is_map(Value)andalso is_map(maps:get(<<"head">>,Value))andalso length(maps:get(<<"operations">>,Value))=:=156;
once("richards",_)->richards:run(10000,23246,9297).
repeat(0,_,_)->true;
repeat(N,Name,Fixture)->native_bench:check(once(Name,Fixture)),repeat(N-1,Name,Fixture).
outer(0,_,_,_)->true;
outer(N,"cd",Inner,Fixture)->native_bench:check(collision:run(Inner)),outer(N-1,"cd",Inner,Fixture);
outer(N,"deltablue",Inner,Fixture)->native_bench:check(deltablue:run(Inner)),outer(N-1,"deltablue",Inner,Fixture);
outer(N,"havlak",Inner,Fixture)->native_bench:check(havlak:run(Inner)),outer(N-1,"havlak",Inner,Fixture);
outer(N,"mandelbrot",Inner,Fixture)->Expected=case Inner of 1->128;500->191;750->50 end,native_bench:check(mandelbrot(Inner,true)=:=Expected),outer(N-1,"mandelbrot",Inner,Fixture);
outer(N,"nbody",Inner,Fixture)->Expected=case Inner of 1->-0.16907495402506745;36000->-0.16901424478751628;250000->-0.1690859889909308 end,
    native_bench:check(nbody:energy(nbody:steps(Inner,nbody:new()))=:=Expected),outer(N-1,"nbody",Inner,Fixture);
outer(N,Name,Inner,Fixture)->repeat(Inner,Name,Fixture),outer(N-1,Name,Inner,Fixture).
run(Name,Inner,Outer)->Fixture=case Name of "json"->{ok,B}=file:read_file("test/benchmark/native_ports/fixtures/awfy.json"),B;_->nil end,
    Label=case Name of "nbody"->"NBody";"deltablue"->"DeltaBlue";"cd"->"CD";_->string:titlecase(Name)end,
    native_bench:run(fun()->outer(Outer,Name,Inner,Fixture)end,fun native_bench:check/1,
        fun(_)->io:format("~s: PASS~n",[Label])end,native_bench:warmup()).
