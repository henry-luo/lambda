%% Dense numeric grids live in private ETS tables; no language emulation layer.
-module(fluid).
-export([run/0]).
-record(state,{d,dp,u,up,v,vp,till=0,between=5}).
newgrid()->T=ets:new(grid,[set,private]),clear(T),T.
clear(T)->ets:insert(T,[{I,0.0}||I<-lists:seq(0,16899)]).
get(T,I)->ets:lookup_element(T,I,2).
set(T,I,X)->ets:insert(T,{I,X}).
prepare()->#state{d=newgrid(),dp=newgrid(),u=newgrid(),up=newgrid(),v=newgrid(),vp=newgrid()}.
cleanup(S)->lists:foreach(fun ets:delete/1,[S#state.d,S#state.dp,S#state.u,S#state.up,S#state.v,S#state.vp]).
add(X,S)->lists:foreach(fun(I)->set(X,I,get(X,I)+0.1*get(S,I))end,lists:seq(0,16899)).
boundary(B,X)->lists:foreach(fun(I)->Top=get(X,I+130),Bottom=get(X,I+128*130),Left=get(X,1+I*130),Right=get(X,128+I*130),
    set(X,I,case B of 2->-Top;_->Top end),set(X,I+129*130,case B of 2->-Bottom;_->Bottom end),
    set(X,I*130,case B of 1->-Left;_->Left end),set(X,129+I*130,case B of 1->-Right;_->Right end)end,lists:seq(1,128)),
    set(X,0,0.5*(get(X,1)+get(X,130))),set(X,129*130,0.5*(get(X,1+129*130)+get(X,128*130))),
    set(X,129,0.5*(get(X,128)+get(X,259))),set(X,129+129*130,0.5*(get(X,128+129*130)+get(X,129+128*130))).
solve(B,X,X0,0.0,1.0)->grid(fun(I)->set(X,I,get(X0,I))end),boundary(B,X);
solve(B,X,X0,A,C)->lists:foreach(fun(_)->lists:foreach(fun(J)->solve_row(128,X,X0,A,1.0/C,J*130+1,get(X,J*130))end,lists:seq(1,128)),boundary(B,X)end,lists:seq(1,20)).
solve_row(0,_,_,_,_,_,_)->ok;
solve_row(N,X,X0,A,Inv,At,Left)->V=(get(X0,At)+A*(Left+get(X,At+1)+get(X,At-130)+get(X,At+130)))*Inv,set(X,At,V),solve_row(N-1,X,X0,A,Inv,At+1,V).
grid(F)->lists:foreach(fun(J)->lists:foreach(fun(I)->F(J*130+I)end,lists:seq(1,128))end,lists:seq(1,128)).
advect(B,D,D0,U,V)->lists:foreach(fun(J)->lists:foreach(fun(I)->At=J*130+I,Fx=max(0.5,min(128.5,I-12.8*get(U,At))),Fy=max(0.5,min(128.5,J-12.8*get(V,At))),
    I0=floor(Fx),J0=floor(Fy),S1=Fx-I0,S0=1.0-S1,T1=Fy-J0,T0=1.0-T1,
    set(D,At,S0*(T0*get(D0,I0+J0*130)+T1*get(D0,I0+(J0+1)*130))+S1*(T0*get(D0,I0+1+J0*130)+T1*get(D0,I0+1+(J0+1)*130)))end,lists:seq(1,128))end,lists:seq(1,128)),boundary(B,D).
project(U,V,P,Div)->H= -0.5/math:sqrt(128.0*128.0),grid(fun(I)->set(Div,I,H*(get(U,I+1)-get(U,I-1)+get(V,I+130)-get(V,I-130))),set(P,I,0.0)end),
    boundary(0,Div),boundary(0,P),solve(0,P,Div,1.0,4.0),
    grid(fun(I)->set(U,I,get(U,I)-64.0*(get(P,I+1)-get(P,I-1))),set(V,I,get(V,I)-64.0*(get(P,I+130)-get(P,I-130)))end),boundary(1,U),boundary(2,V).
points(S)->lists:foreach(fun(I)->lists:foreach(fun({At,Velocity,Density})->set(S#state.up,At,Velocity),set(S#state.vp,At,Velocity),set(S#state.dp,At,Density)end,
    [{I+1+(I+1)*130,64.0,5.0},{I+1+(65-I)*130,-64.0,20.0},{129-I+(65+I)*130,-64.0,30.0}])end,lists:seq(1,64)).
update(S)->U=S#state.u,V=S#state.v,Up=S#state.up,Vp=S#state.vp,D=S#state.d,Dp=S#state.dp,clear(Up),clear(Vp),clear(Dp),
    Next=case S#state.till of 0->points(S),S#state{till=S#state.between,between=S#state.between+1};_->S#state{till=S#state.till-1}end,
    add(U,Up),add(V,Vp),grid(fun(I)->set(Up,I,get(U,I)),set(Vp,I,get(V,I))end),boundary(1,Up),boundary(2,Vp),project(Up,Vp,U,V),
    advect(1,U,Up,Up,Vp),advect(2,V,Vp,Up,Vp),project(U,V,Up,Vp),add(D,Dp),solve(0,Dp,D,0.0,1.0),advect(0,D,Dp,U,V),Next.
digest(S)->D=lists:foldl(fun(I,H)->A=H bxor(floor(get(S#state.d,I)*1000.0)band 16#ffffffff),((A bsl 5)-A+(A bsr 7))band 16#ffffffff end,2166136261,lists:seq(0,16899)),case D>=16#80000000 of true->D-16#100000000;false->D end.
verify(S)->Final=lists:foldl(fun(_,A)->update(A)end,S,lists:seq(2,15)),Sum=lists:sum([trunc(get(Final#state.d,I)*10)||I<-lists:seq(7000,7099)]),native_bench:check(Sum=:=77 andalso digest(Final)=:= -257786486).
run()->native_bench:prepared(fun prepare/0,fun update/1,fun verify/1,fun(S)->io:format("__NAVIER_DENSITY_DIGEST__:~B~n",[digest(S)])end,fun cleanup/1).
