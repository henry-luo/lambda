-module(larceny).
-export([run/1]).
constant(V)->{constant,V}.
variable()->{variable,0}.
expression()->M1={mul,constant(3),variable()},M2={mul,M1,variable()},M3={mul,M2,variable()},
    M5={mul,{mul,constant(2),variable()},variable()},{add,{add,{add,M3,M5},variable()},constant(5)}.
derivative({constant,_})->constant(0);
derivative({variable,_})->constant(1);
derivative({add,L,R})->{add,derivative(L),derivative(R)};
derivative({mul,L,R})->DL=derivative(L),DR=derivative(R),{add,{mul,L,DR},{mul,DL,R}}.
count({_,_})->1;
count({_,L,R})->1+count(L)+count(R).
deriv_repeat(0,N)->N;
deriv_repeat(I,_)->deriv_repeat(I-1,count(derivative(expression()))).
array_sum(I,A,Total)when I>tuple_size(A)->Total;
array_sum(I,A,Total)->array_sum(I+1,A,Total+element(I,A)).
array_repeat(0,_,Total)->Total;
array_repeat(N,A,_)->array_repeat(N-1,A,array_sum(1,A,0)).
divide(X,Y,Q)when X<Y->Q;
divide(X,Y,Q)->divide(X-Y,Y,Q+1).
modulus(X,Y)when X<Y->X;
modulus(X,Y)->modulus(X-Y,Y).
recursive_div(X,Y)when X<Y->0;
recursive_div(X,Y)->1+recursive_div(X-Y,Y).
div_repeat(0,_,_,Total)->Total;
div_repeat(N,X,Recursive,Total)->Q=case Recursive of true->recursive_div(X,2);false->divide(X,2,0)end,
    div_repeat(N-1,X,Recursive,Total+Q-modulus(X,2)).
quicksort(A,Lo,Hi)when Lo>=Hi->A;
quicksort(A,Lo,Hi)->Pivot=array:get(Hi,A),{B,At}=partition(A,Lo,Hi,Lo,Pivot),
    C=swap(B,At,Hi),quicksort(quicksort(C,Lo,At-1),At+1,Hi).
partition(A,Hi,Hi,At,_)->{A,At};
partition(A,I,Hi,At,Pivot)->case array:get(I,A)=<Pivot of
    true->partition(swap(A,I,At),I+1,Hi,At+1,Pivot);false->partition(A,I+1,Hi,At,Pivot)end.
swap(A,I,J)->V=array:get(I,A),W=array:get(J,A),array:set(J,V,array:set(I,W,A)).
randoms(0,_,Acc)->lists:reverse(Acc);
randoms(N,Seed0,Acc)->Seed=kostya:next(Seed0),randoms(N-1,Seed,[Seed|Acc]).
sorted([])->true;sorted([_])->true;
sorted([A,B|Rest])->A=<B andalso sorted([B|Rest]).
puzzle(10,_,_,_)->1;
puzzle(Row,Columns,Forward,Back)->puzzle_columns(0,Row,Columns,Forward,Back,0).
puzzle_columns(10,_,_,_,_,Total)->Total;
puzzle_columns(Col,Row,Columns,Forward,Back,Total)->C=1 bsl Col,F=1 bsl(Row+Col),B=1 bsl(Row-Col+9),
    Add=case Columns band C=:=0 andalso Forward band F=:=0 andalso Back band B=:=0 of
        true->puzzle(Row+1,Columns bor C,Forward bor F,Back bor B);false->0 end,
    puzzle_columns(Col+1,Row,Columns,Forward,Back,Total+Add).
%% Equal radical sizes share an unordered multiset factor, for both arities.
weight([],_) -> 1;
weight([I|Rest],Counts)->{Equal,Tail}=lists:splitwith(fun(J)->J=:=I end,Rest),
    multiset(element(I+1,Counts),length(Equal)+1,1,1)*weight(Tail,Counts).
multiset(_,0,_,Value)->Value;
multiset(N,Count,K,Value)->multiset(N,Count-1,K+1,Value*(N+K-1)div K).
radicals(Counts,Size)->Target=Size-1,
    lists:sum([weight([I,J,Target-I-J],Counts)||I<-lists:seq(0,Target div 3),J<-lists:seq(I,(Target-I)div 2)]).
radical_counts(K,Half,Counts)when K>Half->Counts;
radical_counts(K,Half,Counts)->radical_counts(K+1,Half,setelement(K+1,Counts,radicals(Counts,K))).
paraffins(Size)->Half=Size div 2,Counts=radical_counts(1,Half,setelement(1,erlang:make_tuple(Half+1,0),1)),
    Bond=case Size rem 2 of 0->N=element(Half+1,Counts),N*(N+1)div 2;_->0 end,
    Sum=Size-1,Max=(Size-1)div 2,
    Bond+lists:sum([weight([I,J,K,Sum-I-J-K],Counts)||I<-lists:seq(0,Sum div 4),J<-lists:seq(I,(Sum-I)div 3),
        K<-lists:seq(J,(Sum-I-J)div 2),Sum-I-J-K=<Max]).
paraffins_repeat(0,Result)->Result;
paraffins_repeat(N,_)->Values=[paraffins(Size)||Size<-lists:seq(1,23)],paraffins_repeat(N-1,lists:last(Values)).
pnpoly()->Xs={0.0,1.0,1.0,0.0,0.0,1.0,-0.5,-1.0,-1.0,-2.0,-2.5,-2.0,-1.5,-0.5,0.5,1.0,0.5,0.0,-0.5,-1.0},
    Ys={0.0,0.0,1.0,1.0,2.0,3.0,2.0,3.0,0.0,-0.5,0.5,1.5,2.0,3.0,3.0,2.0,1.0,0.5,-1.0,-1.0},
    lists:foldl(fun(Ix,Sum)->X=-2.5+Ix*0.008,lists:foldl(fun(Iy,S)->case inside(Xs,Ys,X,-1.5+Iy*0.025,1,20,false)of true->S+1;false->S end end,Sum,lists:seq(0,199))end,0,lists:seq(0,499)).
inside(_,_,_,_,21,_,In)->In;
inside(Xs,Ys,X,Y,I,J,In)->Yi=element(I,Ys),Yj=element(J,Ys),
    Cross=(Yi>Y)=/=(Yj>Y) andalso X<(element(J,Xs)-element(I,Xs))*(Y-Yi)/(Yj-Yi)+element(I,Xs),
    inside(Xs,Ys,X,Y,I+1,I,In xor Cross).
ray()->Spheres=[{0.0,0.0,5.0},{-2.0,0.0,5.0},{2.0,0.0,5.0},{0.0,2.0,5.0}],
    lists:foldl(fun(Py,S)->lists:foldl(fun(Px,Sum)->Dx=(Px-50)/50.0,Dy=(Py-50)/50.0,L=math:sqrt(Dx*Dx+Dy*Dy+1.0),
        Hit=lists:foldl(fun(Sphere,Acc)->V=ray_hit(Sphere,Dx/L,Dy/L,1.0/L),V orelse Acc end,false,Spheres),
        case Hit of true->Sum+1;false->Sum end end,S,lists:seq(0,99))end,0,lists:seq(0,99)).
ray_hit({Sx,Sy,Sz},Dx,Dy,Dz)->Ex=-Sx,Ey=-Sy,Ez=-Sz,B=2.0*(Ex*Dx+Ey*Dy+Ez*Dz),Disc=B*B-4.0*(Ex*Ex+Ey*Ey+Ez*Ez-1.0),
    Disc>=0.0 andalso(-B-math:sqrt(Disc))/2.0>0.001.
triangl()->From=[0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14],
    Over=[1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13],
    To=[3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12],
    Moves=[{1 bsl F,1 bsl O,1 bsl T}||{F,O,T}<-lists:zip3(From,Over,To)],triangl_search(32766,14,Moves).
triangl_search(_,1,_)->1;
triangl_search(Board,Pegs,Moves)->triangl_moves(Moves,Board,Pegs,Moves,0).
triangl_moves([],_,_,_,Count)->Count;
triangl_moves([{F,O,T}|Rest],Board,Pegs,Moves,Count)->
    Add=case Board band F=/=0 andalso Board band O=/=0 andalso Board band T=:=0 of
        true->triangl_search(Board bxor(F bor O bor T),Pegs-1,Moves);false->0 end,
    triangl_moves(Rest,Board,Pegs,Moves,Count+Add).
gcbench()->Stretch=beng:check_tree(beng:tree(15)),Long=beng:tree(14),
    Lines=[begin Count=1 bsl(18-D),Total=lists:foldl(fun(_,S)->S+beng:check_tree(beng:tree(D))end,0,lists:seq(1,Count)),
        io_lib:format("~B trees of depth ~B check: ~B~n",[Count,D,Total])end||D<-[4,6,8,10,12,14]],
    {iolist_to_binary([io_lib:format("stretch tree of depth 15 check: ~B~n",[Stretch]),Lines]),Long}.
run(Name)->{Work,Verify,Report}=case Name of
    "deriv"->{fun()->deriv_repeat(5000,0)end,fun(V)->native_bench:check(V=:=45)end,pass(Name)};
    "array1"->{fun()->array_repeat(100,list_to_tuple(lists:seq(0,9999)),0)end,fun(V)->native_bench:check(V=:=49995000)end,pass(Name)};
    "diviter"->{fun()->div_repeat(1000,1000000,false,0)end,fun(V)->native_bench:check(V=:=500000000)end,pass(Name)};
    "divrec"->{fun()->div_repeat(1000,1000,true,0)end,fun(V)->native_bench:check(V=:=500000)end,pass(Name)};
    "primes"->{fun()->kostya:primes(1000000)end,fun(V)->native_bench:check(V=:=78498)end,pass(Name)};
    "puzzle"->{fun()->puzzle(0,0,0,0)end,fun(V)->native_bench:check(V=:=724)end,pass(Name)};
    "quicksort"->{fun()->array:to_list(quicksort(array:from_list(randoms(5000,42,[])),0,4999))end,fun(V)->native_bench:check(sorted(V))end,pass(Name)};
    "paraffins"->{fun()->paraffins_repeat(10,0)end,fun(V)->native_bench:check(V=:=5731580)end,fun(V)->io:format("paraffins: nb(23) = ~B~nparaffins: PASS~n",[V])end};
    "pnpoly"->{fun pnpoly/0,fun(V)->native_bench:check(V=:=29415)end,fun(V)->io:format("pnpoly: total=100000 inside=~B~npnpoly: DONE~n",[V])end};
    "ray"->{fun ray/0,fun(V)->native_bench:check(V=:=1392)end,fun(V)->io:format("ray: hits=~B~nray: PASS~n",[V])end};
    "triangl"->{fun triangl/0,fun(V)->native_bench:check(V=:=29760)end,fun(V)->io:format("triangl: solutions=~B~ntriangl: PASS~n",[V])end};
    "gcbench"->{fun gcbench/0,fun({_,T})->native_bench:check(beng:check_tree(T)=:=32767)end,
        fun({Out,T})->io:put_chars(Out),io:format("long lived tree of depth 14 check: ~B~n",[beng:check_tree(T)])end}
end,native_bench:run(Work,Verify,Report,native_bench:warmup()).
pass(Name)->fun(_)->io:format("~s: PASS~n",[Name])end.
