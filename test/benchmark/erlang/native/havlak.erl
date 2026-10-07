%% Havlak's DFS numbering and union-find loop forest, using native graph records.
-module(havlak).
-export([run/1]).
-record(block,{id,in=[],out=[]}).
-record(node,{id,block=nil,parent,last=0,header=0,kind=regular,back=[],non=#{},loop=nil}).
-record(loop,{id,parent=nil,children=[],blocks=#{},depth=0,nesting=0}).
block(C,Id)->case ets:lookup(C,Id)of []->B=#block{id=Id},ets:insert(C,B),B;[B]->B end.
edge(C,From,To)->block(C,From),block(C,To),F=block(C,From),ets:insert(C,F#block{out=[To|F#block.out]}),T=block(C,To),ets:insert(C,T#block{in=[From|T#block.in]}).
straight(_,N,0)->N;
straight(C,N,K)->edge(C,N,N+1),straight(C,N+1,K-1).
diamond(C,N)->edge(C,N,N+1),edge(C,N,N+2),edge(C,N+1,N+3),edge(C,N+2,N+3),N+3.
base(C,N)->H=straight(C,N,1),D1=diamond(C,H),A=straight(C,D1,1),D2=diamond(C,A),F=straight(C,D2,1),edge(C,D2,A),edge(C,D1,H),edge(C,F,N),straight(C,F,1).
construct(C)->lists:foldl(fun(_,N0)->block(C,N0+1),edge(C,2,N0+1),N=lists:foldl(fun(_,Top)->A=straight(C,Top,1),B=lists:foldl(fun(_,X)->base(C,X)end,A,lists:seq(1,5)),Bottom=straight(C,B,1),edge(C,B,Top),Bottom end,N0+1,lists:seq(1,10)),edge(C,N,1),N end,2,lists:seq(1,10)).
graph()->G=ets:new(loops,[set,private,{keypos,2}]),ets:insert(G,#loop{id=0}),G.
getnode(T,I)->[N]=ets:lookup(T,I),N.
getloop(G,I)->[L]=ets:lookup(G,I),L.
parent(G,I,P)->L=getloop(G,I),ets:insert(G,L#loop{parent=P}),R=getloop(G,P),ets:insert(G,R#loop{children=[I|R#loop.children]}).
newloop(G,Block)->Id=ets:info(G,size),ets:insert(G,#loop{id=Id,blocks=#{Block=>true}}),Id.
dfs(C,T,Numbers,Block,Current)->ets:insert(Numbers,{Block,Current}),N=getnode(T,Current),ets:insert(T,N#node{block=Block}),B=block(C,Block),
    Last=lists:foldl(fun(Target,Prev)->case ets:lookup(Numbers,Target)of []->dfs(C,T,Numbers,Target,Prev+1);_->Prev end end,Current,lists:reverse(B#block.out)),
    N1=getnode(T,Current),ets:insert(T,N1#node{last=Last}),Last.
ancestor(T,W,V)->N=getnode(T,W),W=<V andalso V=<N#node.last.
find(T,I)->N=getnode(T,I),case N#node.parent of I->I;P->R=find(T,P),ets:insert(T,N#node{parent=R}),R end.
edges(C,T,Numbers,W)->N=getnode(T,W),case N#node.block of nil->ets:insert(T,N#node{kind=dead});Id->B=block(C,Id),{Back,Non}=lists:foldl(fun(P,{Bs,Ns})->case ets:lookup(Numbers,P)of []->{Bs,Ns};[{_,V}]->case ancestor(T,W,V)of true->{[V|Bs],Ns};false->{Bs,Ns#{V=>true}}end end end,{[],#{}},lists:reverse(B#block.in)),ets:insert(T,N#node{back=lists:reverse(Back),non=Non})end.
seed(T,W)->N=getnode(T,W),lists:foldl(fun(V,{Pool,Seen})->case V of W->A=getnode(T,W),ets:insert(T,A#node{kind=self}),{Pool,Seen};_->R=find(T,V),case maps:is_key(R,Seen)of true->{Pool,Seen};false->{[R|Pool],Seen#{R=>true}}end end end,{[],#{}},N#node.back).
work(T,W,Q,Pool,Seen)->case queue:out(Q)of {empty,_}->Pool;{{value,X},Rest}->N=getnode(T,X),native_bench:check(map_size(N#node.non)=<32768),
    {Next,Ps,Ss}=maps:fold(fun(P,_,{Queue,Acc,Set})->Y=find(T,P),case ancestor(T,W,Y)of false->H=getnode(T,W),ets:insert(T,H#node{kind=irreducible,non=(H#node.non)#{Y=>true}}),{Queue,Acc,Set};true when Y=/=W->case maps:is_key(Y,Set)of true->{Queue,Acc,Set};false->{queue:in(Y,Queue),[Y|Acc],Set#{Y=>true}}end;true->{Queue,Acc,Set}end end,{Rest,Pool,Seen},N#node.non),work(T,W,Next,Ps,Ss)end.
header(T,G,W)->{Pool0,Seen}=seed(T,W),Pool=work(T,W,queue:from_list(lists:reverse(Pool0)),Pool0,Seen),H=getnode(T,W),
    case Pool=/=[] orelse H#node.kind=:=self of false->ok;true->Kind=case Pool of []->H#node.kind;_->case H#node.kind of irreducible->irreducible;_->reducible end end,L=newloop(G,H#node.block),ets:insert(T,H#node{kind=Kind,loop=L}),
        lists:foreach(fun(I)->N=getnode(T,I),ets:insert(T,N#node{header=W,parent=W}),case N#node.loop of nil->R=getloop(G,L),ets:insert(G,R#loop{blocks=(R#loop.blocks)#{N#node.block=>true}});Child->parent(G,Child,L)end end,Pool)end.
findloops(C,G)->Size=ets:info(C,size),T=ets:new(nodes,[set,private,{keypos,2}]),Numbers=ets:new(numbers,[set,private]),
    try lists:foreach(fun(I)->ets:insert(T,#node{id=I,parent=I})end,lists:seq(0,Size-1)),dfs(C,T,Numbers,0,0),lists:foreach(fun(W)->edges(C,T,Numbers,W)end,lists:seq(0,Size-1)),lists:foreach(fun(W)->header(T,G,W)end,lists:seq(Size-1,0,-1))
    after ets:delete(T),ets:delete(Numbers)end.
nest(G,I,D)->L=getloop(G,I),Level=lists:foldl(fun(Child,M)->max(M,1+nest(G,Child,D+1))end,0,L#loop.children),ets:insert(G,L#loop{depth=D,nesting=Level}),Level.
run(Inner)->C=ets:new(cfg,[set,private,{keypos,2}]),G=graph(),try block(C,0),base(C,0),block(C,1),edge(C,0,2),lists:foreach(fun(_)->findloops(C,G)end,lists:seq(1,Inner)),construct(C),findloops(C,G),
    %% The registered Node workload performs fifty additional complete finders.
    lists:foreach(fun(_)->Extra=graph(),try findloops(C,Extra)after ets:delete(Extra)end end,lists:seq(1,50)),
    lists:foreach(fun(#loop{id=I,parent=P})->case I=/=0 andalso P=:=nil of true->parent(G,I,0);false->ok end end,ets:tab2list(G)),nest(G,0,0),
    Expected=case Inner of 1->1605;15->1647;150->2052;1500->6102;15000->46602 end,ets:info(G,size)=:=Expected andalso ets:info(C,size)=:=5213
    after ets:delete(C),ets:delete(G)end.
