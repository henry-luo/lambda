-module(richards).
-export([run/3]).
-record(packet,{id,kind,datum=0,data={0,0,0,0}}).
-record(task,{priority,input=[],pending=false,waiting=false,held=false,kind,data}).
-record(scheduler,{tasks,current,queued=0,held=0}).
task(Id,Priority,Input,Kind,Data)->#task{priority=Priority,input=Input,pending=Input=/=[],waiting=Id=/=0,kind=Kind,data=Data}.
run(IdleCount,Queues,Holds)->
    W=#packet{id=1,kind=1},A=#packet{id=4,kind=0},B=#packet{id=5,kind=0},
    Tasks={task(0,0,[],idle,{1,IdleCount}),task(1,1000,[W,W],worker,{2,0}),
        task(2,2000,[A,A,A],handler,{[],[]}),task(3,3000,[B,B,B],handler,{[],[]}),
        task(4,4000,[],device,nil),task(5,5000,[],device,nil)},
    Result=schedule(#scheduler{tasks=Tasks,current=5}),Result#scheduler.queued=:=Queues andalso Result#scheduler.held=:=Holds.
get(S,Id)->element(Id+1,S#scheduler.tasks).
store(S,Id,T)->S#scheduler{tasks=setelement(Id+1,S#scheduler.tasks,T)}.
schedule(S=#scheduler{current=-1})->S;
schedule(S=#scheduler{current=Id})->T=get(S,Id),
    case T#task.held orelse(not T#task.pending andalso T#task.waiting)of
        true->schedule(S#scheduler{current=Id-1});
        false->{Packet,T1}=case T#task.pending andalso T#task.waiting of
            true->[P|Rest]=T#task.input,{P,T#task{input=Rest,waiting=false,pending=Rest=/=[]}};
            false->{nil,T}
        end,
        schedule(dispatch(store(S,Id,T1),Id,T1,Packet))
    end.
hold(S,Id)->T=get(S,Id),Next=store(S,Id,T#task{held=true}),Next#scheduler{current=Id-1,held=S#scheduler.held+1}.
wait(S,Id)->T=get(S,Id),store(S,Id,T#task{waiting=true}).
release(S,Id,Destination)->T=get(S,Destination),Next=store(S,Destination,T#task{held=false}),
    case T#task.priority>(get(S,Id))#task.priority of true->Next#scheduler{current=Destination};false->Next end.
queue(S,Id,Packet)->Destination=Packet#packet.id,T=get(S,Destination),P=Packet#packet{id=Id},
    Next=store(S,Destination,T#task{input=T#task.input++[P],pending=true}),
    Current=case T#task.input=:=[] andalso T#task.priority>(get(S,Id))#task.priority of true->Destination;false->Id end,
    Next#scheduler{current=Current,queued=S#scheduler.queued+1}.
dispatch(S,Id,#task{kind=idle,data={Control,Count}},_)->N=Count-1,
    case N of 0->hold(S,Id);_->case Control band 1 of
        0->Next=store(S,Id,(get(S,Id))#task{data={Control div 2,N}}),release(Next,Id,4);
        _->Next=store(S,Id,(get(S,Id))#task{data={(Control div 2)bxor 53256,N}}),release(Next,Id,5)
    end end;
dispatch(S,Id,#task{kind=worker},nil)->wait(S,Id);
dispatch(S,Id,T=#task{kind=worker,data={Dest0,Count0}},Packet)->Dest=case Dest0 of 2->3;_->2 end,
    {Values,Count}=lists:mapfoldl(fun(_,C)->N=C rem 26+1,{64+N,N}end,Count0,lists:seq(1,4)),
    Next=store(S,Id,T#task{data={Dest,Count}}),queue(Next,Id,Packet#packet{id=Dest,datum=0,data=list_to_tuple(Values)});
dispatch(S,Id,T=#task{kind=handler,data={Work0,Device0}},Packet)->
    {Work,Device}=case Packet of nil->{Work0,Device0};#packet{kind=1}->{Work0++[Packet],Device0};_->{Work0,Device0++[Packet]}end,
    Next=store(S,Id,T#task{data={Work,Device}}),
    case Work of []->wait(Next,Id);[W|Ws]->case W#packet.datum>=4 of
        true->queue(store(Next,Id,T#task{data={Ws,Device}}),Id,W);
        false->case Device of []->wait(Next,Id);[D|Ds]->Value=element(W#packet.datum+1,W#packet.data),
            queue(store(Next,Id,T#task{data={[W#packet{datum=W#packet.datum+1}|Ws],Ds}}),Id,D#packet{datum=Value})end
    end end;
dispatch(S,Id,#task{kind=device,data=nil},nil)->wait(S,Id);
dispatch(S,Id,T=#task{kind=device,data=Packet},nil)->queue(store(S,Id,T#task{data=nil}),Id,Packet);
dispatch(S,Id,T=#task{kind=device},Packet)->hold(store(S,Id,T#task{data=Packet}),Id).
