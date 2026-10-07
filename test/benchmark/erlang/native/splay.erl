-module(splay).
-export([run/1]).
-record(node,{key,payload,left=nil,right=nil}).
random(Seed0)->Hi=Seed0 div 127773,Lo=Seed0 rem 127773,S=16807*Lo-2836*Hi,Seed=case S=<0 of true->S+2147483647;false->S end,{Seed/2147483647.0,Seed}.
payload(0,Key)->{lists:seq(0,9),<<"String for key ",(float_to_binary(Key,[short]))/binary," in leaf node">>};
payload(Depth,Key)->{payload(Depth-1,Key),payload(Depth-1,Key)}.
splay(nil,_)->nil;
splay(Root,Key)->descend(Root,Key,[],[]).
%% The two native zipper lists are the temporary top-down tree chains.
assemble(C,Left,Right)->L=lists:foldl(fun(N,Child)->N#node{right=Child}end,C#node.left,Left),R=lists:foldl(fun(N,Child)->N#node{left=Child}end,C#node.right,Right),C#node{left=L,right=R}.
descend(C,Key,L,R)when Key<C#node.key->case C#node.left of nil->assemble(C,L,R);A->
    Rotated=case Key<A#node.key of true->A#node{right=C#node{left=A#node.right}};false->C end,
    case Rotated#node.left of nil->assemble(Rotated,L,R);Next->descend(Next,Key,L,[Rotated|R])end end;
descend(C,Key,L,R)when Key>C#node.key->case C#node.right of nil->assemble(C,L,R);A->
    Rotated=case Key>A#node.key of true->A#node{left=C#node{right=A#node.left}};false->C end,
    case Rotated#node.right of nil->assemble(Rotated,L,R);Next->descend(Next,Key,[Rotated|L],R)end end;
descend(C,_,L,R)->assemble(C,L,R).
insert(Root,Seed0)->{Key,Seed}=random(Seed0),R=splay(Root,Key),case R=/=nil andalso R#node.key=:=Key of true->insert(R,Seed);false->
    N=#node{key=Key,payload=payload(5,Key)},Next=case R of nil->N;_ when Key>R#node.key->N#node{left=R#node{right=nil},right=R#node.right};_->N#node{right=R#node{left=nil},left=R#node.left}end,{Next,Key,Seed}end.
maximum(N=#node{right=nil})->N;
maximum(#node{right=R})->maximum(R).
remove(Root,Key)->R=splay(Root,Key),native_bench:check(R#node.key=:=Key),case R#node.left of nil->R#node.right;L->A=splay(L,Key),A#node{right=R#node.right}end.
prepare()->lists:foldl(fun(_,{Root,Seed})->{R,_,S}=insert(Root,Seed),{R,S}end,{nil,49734321},lists:seq(1,8000)).
modify({Root,Seed0})->{R,Key,Seed}=insert(Root,Seed0),A=splay(R,Key),Greatest=case A#node.key<Key of true->A;false->case A#node.left of nil->nil;L->maximum(L)end end,
    Delete=case Greatest of nil->Key;_->Greatest#node.key end,{remove(A,Delete),Seed}.
work(State,Repeats)->lists:foldl(fun(_,S)->lists:foldl(fun(_,A)->modify(A)end,S,lists:seq(1,80))end,State,lists:seq(1,Repeats)).
keys(nil,Acc)->Acc;
keys(#node{left=L,right=R,key=K},Acc)->keys(L,[K|keys(R,Acc)]).
sorted([A,B|Rest])->A<B andalso sorted([B|Rest]);sorted(_)->true.
verify({Root,_})->K=keys(Root,[]),native_bench:check(length(K)=:=8000 andalso sorted(K)).
run(Repeats)->case native_bench:warmup()of true->verify(work(prepare(),Repeats));false->ok end,State=prepare(),native_bench:run(fun()->work(State,Repeats)end,fun verify/1,fun(_)->ok end,false).
