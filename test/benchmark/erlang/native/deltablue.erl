%% ETS stores the constraint graph's native records; scalar locals are arguments.
-module(deltablue).
-export([run/1,chain/3,projection/2]).
-record(variable,{id,value=0,constraints=[],determined=nil,mark=0,walk=7,stay=true}).
-record(constraint,{id,kind,strength,v1,v2=nil,scale=nil,offset=nil,direction=0}).
new()->T=ets:new(deltablue,[set,private,{keypos,2}]),ets:insert(T,[{counter,ids,0},{counter,mark,1}]),T.
read(T,Id)->[Record]=ets:lookup(T,Id),Record.
write(T,Record)->ets:insert(T,Record),ok.
id(T)->ets:update_counter(T,ids,{3,1}).
mark(T)->ets:update_counter(T,mark,{3,1}).
variable(T,Value)->Id=id(T),write(T,#variable{id=Id,value=Value}),Id.
value(T,Id)->(read(T,Id))#variable.value.
set_value(T,Id,Value)->V=read(T,Id),write(T,V#variable{value=Value}).
vertices(#constraint{kind=scale,v1=A,v2=B,scale=S,offset=O})->[A,B,S,O];
vertices(#constraint{v1=A,v2=nil})->[A];
vertices(#constraint{v1=A,v2=B})->[A,B].
add(T,Kind,Strength,A,B,S,O)->Id=id(T),C=#constraint{id=Id,kind=Kind,strength=Strength,v1=A,v2=B,scale=S,offset=O},write(T,C),
    lists:foreach(fun(Vid)->V=read(T,Vid),write(T,V#variable{constraints=V#variable.constraints++[Id]})end,vertices(C)),
    incremental_add(T,Id),Id.
unsatisfy(T,Id)->C=read(T,Id),write(T,C#constraint{direction=0}).
remove_graph(T,Id)->C=read(T,Id),lists:foreach(fun(Vid)->V=read(T,Vid),D=case V#variable.determined of Id->nil;Other->Other end,
    write(T,V#variable{constraints=lists:delete(Id,V#variable.constraints),determined=D})end,vertices(C)),unsatisfy(T,Id).
satisfied(T,Id)->(read(T,Id))#constraint.direction=/=0.
output(#constraint{v2=nil,v1=A})->A;
output(#constraint{direction=1,v2=B})->B;
output(#constraint{v1=A})->A.
inputs(#constraint{v2=nil},_)->[];
inputs(C,All)->Input=case C#constraint.direction of 1->C#constraint.v1;_->C#constraint.v2 end,
    case C#constraint.kind=:=scale andalso All of true->[Input,C#constraint.scale,C#constraint.offset];false->[Input]end.
choose(T,C=#constraint{v2=nil},M)->V=read(T,C#constraint.v1),D=case V#variable.mark=/=M andalso C#constraint.strength<V#variable.walk of true->1;false->0 end,
    Next=C#constraint{direction=D},write(T,Next),Next;
choose(T,C,M)->A=read(T,C#constraint.v1),B=read(T,C#constraint.v2),S=C#constraint.strength,
    Direction=if
        A#variable.mark=:=M->case B#variable.mark=/=M andalso S<B#variable.walk of true->1;false->0 end;
        B#variable.mark=:=M->case A#variable.mark=/=M andalso S<A#variable.walk of true->2;false->0 end;
        A#variable.walk>B#variable.walk->case S<A#variable.walk of true->2;false->0 end;
        S<B#variable.walk->1;
        true->0
    end,
    Next=C#constraint{direction=Direction},write(T,Next),Next.
execute(T,Id)->C=read(T,Id),case C#constraint.kind of
    equal->[Input]=inputs(C,false),set_value(T,output(C),value(T,Input));
    scale->V=case C#constraint.direction of 1->value(T,C#constraint.v1)*value(T,C#constraint.scale)+value(T,C#constraint.offset);
        _->(value(T,C#constraint.v2)-value(T,C#constraint.offset))/value(T,C#constraint.scale)end,set_value(T,output(C),V);
    _->ok
end.
recalculate(T,Id)->C=read(T,Id),Out=output(C),V=read(T,Out),
    {Walk,Stay}=case C#constraint.v2 of nil->{C#constraint.strength,C#constraint.kind=/=edit};
        _->[Input]=inputs(C,false),I=read(T,Input),S=case C#constraint.kind of scale->I#variable.stay andalso(read(T,C#constraint.scale))#variable.stay andalso(read(T,C#constraint.offset))#variable.stay;_->I#variable.stay end,
            {max(C#constraint.strength,I#variable.walk),S}
    end,
    write(T,V#variable{walk=Walk,stay=Stay}),case Stay of true->execute(T,Id);false->ok end.
consuming(T,Out)->V=read(T,Out),[C||C<-V#variable.constraints,C=/=V#variable.determined,satisfied(T,C)].
enqueue(Values,Queue)->lists:foldl(fun queue:in/2,Queue,Values).
propagate_add(T,Origin,M,Queue)->case queue:out(Queue)of {empty,_}->true;{{value,Id},Rest}->C=read(T,Id),Out=output(C),
    case(read(T,Out))#variable.mark=:=M of true->incremental_remove(T,Origin),false;
        false->recalculate(T,Id),propagate_add(T,Origin,M,enqueue(consuming(T,Out),Rest))end
end.
satisfy(T,Id,M)->C=choose(T,read(T,Id),M),case C#constraint.direction of
    0->native_bench:check(C#constraint.strength=/=1),nil;
    _->lists:foreach(fun(Vid)->V=read(T,Vid),write(T,V#variable{mark=M})end,inputs(C,true)),
        Out=output(C),V=read(T,Out),Overridden=V#variable.determined,
        case Overridden of nil->ok;_->unsatisfy(T,Overridden)end,write(T,V#variable{determined=Id}),
        native_bench:check(propagate_add(T,Id,M,queue:from_list([Id]))),
        Current=read(T,Out),write(T,Current#variable{mark=M}),Overridden
end.
incremental_add(T,Id)->satisfy_overridden(T,Id,mark(T)).
satisfy_overridden(_,nil,_)->ok;
satisfy_overridden(T,Id,M)->satisfy_overridden(T,satisfy(T,Id,M),M).
incremental_remove(T,Id)->C=read(T,Id),Out=output(C),remove_graph(T,Id),
    V=read(T,Out),write(T,V#variable{determined=nil,walk=7,stay=true}),
    Unsatisfied=remove_propagate(T,queue:from_list([Out]),[]),
    Sorted=lists:sort(fun(A,B)->(read(T,A))#constraint.strength<(read(T,B))#constraint.strength end,Unsatisfied),
    lists:foreach(fun(Cid)->incremental_add(T,Cid)end,Sorted).
remove_propagate(T,Queue,Acc)->case queue:out(Queue)of {empty,_}->Acc;{{value,Out},Rest}->V=read(T,Out),
    U=[C||C<-V#variable.constraints,not satisfied(T,C)],Cs=consuming(T,Out),
    Outputs=[begin recalculate(T,C),output(read(T,C))end||C<-Cs],remove_propagate(T,enqueue(Outputs,Rest),U++Acc)
end.
destroy(T,Id)->case satisfied(T,Id)of true->incremental_remove(T,Id);false->ok end,remove_graph(T,Id),ets:delete(T,Id),ok.
known(T,C,M)->lists:all(fun(Vid)->V=read(T,Vid),V#variable.mark=:=M orelse V#variable.stay orelse V#variable.determined=:=nil end,inputs(C,false)).
plan(T,Sources)->make_plan(T,mark(T),queue:from_list([C||C<-Sources,(read(T,C))#constraint.kind=:=edit,satisfied(T,C)]),[]).
make_plan(T,M,Queue,Acc)->case queue:out(Queue)of {empty,_}->lists:reverse(Acc);{{value,Id},Rest}->C=read(T,Id),Out=output(C),V=read(T,Out),
    case V#variable.mark=/=M andalso known(T,C,M)of true->write(T,V#variable{mark=M}),make_plan(T,M,enqueue(consuming(T,Out),Rest),[Id|Acc]);
        false->make_plan(T,M,Rest,Acc)end
end.
execute_plan(T,Plan)->lists:foreach(fun(Id)->execute(T,Id)end,Plan).
change(T,V,New)->Edit=add(T,edit,3,V,nil,nil,nil),Plan=plan(T,[Edit]),
    lists:foreach(fun(_)->set_value(T,V,New),execute_plan(T,Plan)end,lists:seq(1,10)),destroy(T,Edit).
chain(N,Destroy,First)->T=new(),try
    Vs=list_to_tuple([variable(T,0)||_<-lists:seq(0,N)]),
    lists:foreach(fun(I)->add(T,equal,1,element(I,Vs),element(I+1,Vs),nil,nil)end,lists:seq(1,N)),
    add(T,stay,4,element(N+1,Vs),nil,nil,nil),Start=element(1,Vs),Edit=add(T,edit,3,Start,nil,nil,nil),Plan=plan(T,[Edit]),
    lists:foreach(fun(I)->set_value(T,Start,I),execute_plan(T,Plan),native_bench:check(value(T,element(N+1,Vs))==I)end,lists:seq(First,First+99)),
    case Destroy of true->destroy(T,Edit);false->ok end,true
after ets:delete(T)end.
projection(N,First)->T=new(),try
    Scale=variable(T,10),Offset=variable(T,1000),
    Pairs=[begin Src=variable(T,I),Dst=variable(T,I),add(T,stay,5,Src,nil,nil,nil),add(T,scale,1,Src,Dst,Scale,Offset),{Src,Dst}end||I<-lists:seq(First,First+N-1)],
    {Src,Dst}=lists:last(Pairs),change(T,Src,17),native_bench:check(value(T,Dst)==1170),
    change(T,Dst,1050),native_bench:check(value(T,Src)==5),change(T,Scale,5),
    lists:foreach(fun({{_,D},I})->native_bench:check(value(T,D)==I*5+1000)end,lists:zip(lists:sublist(Pairs,N-1),lists:seq(First,First+N-2))),
    change(T,Offset,2000),lists:foreach(fun({{_,D},I})->native_bench:check(value(T,D)==I*5+2000)end,lists:zip(lists:sublist(Pairs,N-1),lists:seq(First,First+N-2))),true
after ets:delete(T)end.
run(N)->chain(N,true,0),projection(N,1).
