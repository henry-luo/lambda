-module(collision).
-export([run/1]).
plus({X,Y,Z},{A,B,C})->{X+A,Y+B,Z+C}.
minus({X,Y,Z},{A,B,C})->{X-A,Y-B,Z-C}.
times({X,Y,Z},K)->{X*K,Y*K,Z*K}.
dot({X,Y,Z},{A,B,C})->X*A+Y*B+Z*C.
intersects({P,Q},{R,S})->V=minus(Q,P),W=minus(S,R),Delta=minus(W,V),A=dot(Delta,Delta),
    case A=:=0.0 of true->D=minus(P,R),dot(D,D)=<1.0;
        false->D=minus(R,P),B=2.0*dot(minus(P,R),minus(V,W)),C=dot(D,D)-1.0,Disc=B*B-4.0*A*C,
            case Disc<0.0 of true->false;false->V1=(-B-math:sqrt(Disc))/(2.0*A),V2=(-B+math:sqrt(Disc))/(2.0*A),
                case V1=<V2 andalso overlap(V1,V2)of false->false;true->At=max(V1,0.0),
                    {X,Y,Z}=times(plus(plus(P,times(V,At)),plus(R,times(W,At))),0.5),
                    X>=0.0 andalso X=<1000.0 andalso Y>=0.0 andalso Y=<1000.0 andalso Z>=0.0 andalso Z=<10.0
                end
            end
    end.
overlap(Lo,Hi)->(Lo=<1.0 andalso Hi>=1.0)orelse(Lo=<0.0 andalso Hi>=0.0)orelse(Lo>=0.0 andalso Hi=<1.0).
axis(Cell,Start,0.0)->{Cell=<Start+0.5 andalso Start-0.5=<Cell+2.0,0.0,0.0};
axis(Cell,Start,Velocity)->L=(Cell-0.5-Start)/Velocity,H=(Cell+2.5-Start)/Velocity,
    {Lo,Hi}=case Velocity<0.0 of true->{H,L};false->{L,H}end,{overlap(Lo,Hi),Lo,Hi}.
in_voxel({X,Y},_)when X<0;Y<0;X>500;Y>500->false;
in_voxel({X,Y},{{Sx,Sy,_},{Ex,Ey,_}})->Vx=Ex-Sx,Vy=Ey-Sy,{InX,Lx,Hx}=axis(X*2.0,Sx,Vx),{InY,Ly,Hy}=axis(Y*2.0,Sy,Vy),
    InX andalso InY andalso(Vx=:=0.0 orelse Vy=:=0.0 orelse(Ly=<Hx andalso Hx=<Hy)orelse(Ly=<Lx andalso Lx=<Hy)orelse(Lx=<Ly andalso Hy=<Hx)).
hash({X,Y,_})->{trunc(X/2.0)-case X<0 of true->1;false->0 end,trunc(Y/2.0)-case Y<0 of true->1;false->0 end}.
draw([],_,_,Map)->Map;
draw([Voxel={X,Y}|Rest],Motion,Seen,Map)->case maps:is_key(Voxel,Seen)orelse not in_voxel(Voxel,Motion)of
    true->draw(Rest,Motion,Seen,Map);
    false->Next=maps:update_with(Voxel,fun(V)->[Motion|V]end,[Motion],Map),
        Neighbors=[{X+Dx,Y+Dy}||Dx<-[-1,0,1],Dy<-[-1,0,1],Dx=/=0 orelse Dy=/=0],draw(Neighbors++Rest,Motion,Seen#{Voxel=>true},Next)
end.
motions(I,N,_,Positions,Acc)when I>=N->{lists:reverse(Acc),Positions};
motions(I,N,Time,Positions0,Acc)->A={Time,math:cos(Time)*2.0+I*3.0,10.0},B={Time,math:sin(Time)*2.0+I*3.0,10.0},
    OldA=maps:get(I,Positions0,A),OldB=maps:get(I+1,Positions0,B),
    motions(I+2,N,Time,Positions0#{I=>A,I+1=>B},[{OldB,B},{OldA,A}|Acc]).
count_pairs([],Count)->Count;
count_pairs([M|Rest],Count)->Add=lists:foldl(fun(N,S)->case intersects(M,N)of true->S+1;false->S end end,0,Rest),count_pairs(Rest,Count+Add).
frames(200,_,_,Count)->Count;
frames(Frame,N,Positions0,Count)->{Motions,Positions}=motions(0,N,Frame/10.0,Positions0,[]),
    Voxels=lists:foldl(fun(M={Start,_},Map)->draw([hash(Start)],M,#{},Map)end,#{},Motions),
    Add=maps:fold(fun(_,Ms,S)->S+count_pairs(Ms,0)end,0,Voxels),frames(Frame+1,N,Positions,Count+Add).
run(N)->Expected=case N of 2->42;10->390;100->4305;200->8655;250->10830;500->14484;1000->14484 end,
    frames(0,N,#{},0)=:=Expected.
