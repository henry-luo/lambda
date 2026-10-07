%% Native jq VM state; frame slots are shared by closures and backtracking forks.
-module(jq_vm).
-export([run/2]).
-include("jq.hrl").
-record(frame,{env=-1,caller=-1,retpc=-1,fn=0,base=0,locals={},params={}}).
-record(fork,{kind,pc,top,limit,fp,path,vat,subexp,index=0,aux=null,aux2=null,aux3=null,active=true,label=0}).
-record(vm,{stack,top=-1,limit=-1,forks,nforks=0,frames,nframes=0,frame_limit=50000,fp=-1,path=nil,vat=null,subexp=0,labels=0,outputs=[],error=null}).
run(P,Input)->V=#vm{stack=array:new([{default,nil}]),forks=array:new([{default,nil}]),frames=array:new([{default,nil}])},
    {Id,V1}=new_frame(P,0,-1,-1,-1,{},V),F=element(1,P#program.functions),loop(P,F#jq_fn.entry,0,push(Input,V1#vm{fp=Id})).
push(Value,V)->I=max(V#vm.top,V#vm.limit)+1,V#vm{stack=array:set(I,{Value,V#vm.top},V#vm.stack),top=I}.
pop(V)->{Value,Previous}=array:get(V#vm.top,V#vm.stack),Stack=case V#vm.top>V#vm.limit of true->array:set(V#vm.top,nil,V#vm.stack);false->V#vm.stack end,{Value,V#vm{top=Previous,stack=Stack}}.
peek(V)->{Value,_}=array:get(V#vm.top,V#vm.stack),Value.
fork(Kind,PC,V)->F=#fork{kind=Kind,pc=PC,top=V#vm.top,limit=V#vm.limit,fp=V#vm.fp,path=V#vm.path,vat=V#vm.vat,subexp=V#vm.subexp},
    V#vm{forks=array:set(V#vm.nforks,F,V#vm.forks),nforks=V#vm.nforks+1,limit=max(V#vm.top,V#vm.limit)}.
last_fork(V)->array:get(V#vm.nforks-1,V#vm.forks).
put_fork(F,V)->V#vm{forks=array:set(V#vm.nforks-1,F,V#vm.forks)}.
remove_fork(V)->V#vm{forks=array:set(V#vm.nforks-1,nil,V#vm.forks),nforks=V#vm.nforks-1}.
restore(F,V)->V#vm{top=F#fork.top,limit=F#fork.limit,fp=F#fork.fp,path=F#fork.path,vat=F#fork.vat,subexp=F#fork.subexp}.
frame(Id,V)->array:get(Id,V#vm.frames).
put_frame(Id,F,V)->V#vm{frames=array:set(Id,F,V#vm.frames)}.
hop(Id,0,_)->Id;hop(Id,N,V)->F=frame(Id,V),hop(F#frame.env,N-1,V).
new_frame(P,Fn,Env,Caller,RetPC,Params,V)->Def=element(Fn+1,P#program.functions),Id=V#vm.nframes,
    F=#frame{env=Env,caller=Caller,retpc=RetPC,fn=Fn,base=V#vm.nforks,locals=erlang:make_tuple(Def#jq_fn.locals,null),params=Params},
    {Id,V#vm{frames=array:set(Id,F,V#vm.frames),nframes=Id+1}}.
%% Reclaim retired VM frames while preserving identities referenced by forks/closures.
compact(V)when V#vm.nframes<V#vm.frame_limit->V;
compact(V)->Roots=[V#vm.fp|[F#fork.fp||F<-active_forks(V)]],Live=mark(Roots,V,#{}),Ids=lists:sort(maps:keys(Live)),
    Mapping=maps:from_list(lists:zip(Ids,lists:seq(0,length(Ids)-1))),Remap=fun(-1)->-1;(I)->maps:get(I,Mapping)end,
    Frames=array:from_list([remap_frame(frame(I,V),Remap)||I<-Ids],nil),
    Forks=array:from_list([F#fork{fp=Remap(F#fork.fp)}||F<-active_forks(V)],nil),
    V#vm{frames=Frames,forks=Forks,fp=Remap(V#vm.fp),nframes=length(Ids),frame_limit=max(50000,length(Ids)*2)}.
active_forks(V)->[array:get(I,V#vm.forks)||I<-lists:seq(0,V#vm.nforks-1)].
mark([],_,Seen)->Seen;
mark([-1|Rest],V,Seen)->mark(Rest,V,Seen);
mark([Id|Rest],V,Seen)->case maps:is_key(Id,Seen)of true->mark(Rest,V,Seen);false->F=frame(Id,V),Edges=[F#frame.env,F#frame.caller|[Env||{_,Env}<-tuple_to_list(F#frame.params)]],mark(Edges++Rest,V,Seen#{Id=>true})end.
remap_frame(F,R)->F#frame{env=R(F#frame.env),caller=R(F#frame.caller),params=list_to_tuple([{Fn,R(Env)}||{Fn,Env}<-tuple_to_list(F#frame.params)])}.
path_check(_,#vm{path=nil})->true;path_check(_,#vm{subexp=N})when N=/=0->true;
path_check(Value,V)->Value=:=V#vm.vat orelse(jq_values:type(Value)=:=jq_values:type(V#vm.vat)andalso jq_values:compare(Value,V#vm.vat)=:=0).
check_path(T,V)->case path_check(T,V)of true->ok;false->throw({jq_error,<<"Invalid path expression">>})end.
path_append(_,_,V=#vm{path=nil})->V;path_append(_,_,V=#vm{subexp=N})when N=/=0->V;
path_append(Key,Value,V)->V#vm{path=erlang:append_element(V#vm.path,Key),vat=Value}.
each(Container,Keys,Index,V)->Key=case jq_values:type(Container)of 5->Index;6->element(Index+1,Keys)end,Value=jq_values:index(Container,Key),push(Value,path_append(Key,Value,V)).
more(From,Upto,Step)->(Step>0 andalso From<Upto)orelse(Step<0 andalso From>Upto).
loop(_,_,1,#vm{nforks=0,outputs=Out})->lists:reverse(Out);
loop(P,PC,1,V)->F=last_fork(V),case F#fork.kind of
    1->loop(P,F#fork.pc,0,remove_fork(restore(F,V)));
    4->Restored=restore(F,V),I=F#fork.index,Next=case I+1>=jq_values:size(F#fork.aux)of true->remove_fork(Restored);false->put_fork(F#fork{index=I+1},Restored#vm{limit=max(Restored#vm.top,Restored#vm.limit)})end,
        loop(P,F#fork.pc,0,each(F#fork.aux,F#fork.aux2,I,Next));
    5->Restored=restore(F,V),Cur=F#fork.aux,NextNumber=Cur+F#fork.aux3,Next=case more(NextNumber,F#fork.aux2,F#fork.aux3)of true->put_fork(F#fork{aux=NextNumber},Restored#vm{limit=max(Restored#vm.top,Restored#vm.limit)});false->remove_fork(Restored)end,
        loop(P,F#fork.pc,0,push(Cur,Next));
    3->Try=array:get(F#fork.index,V#vm.forks),loop(P,PC,1,remove_fork(V#vm{forks=array:set(F#fork.index,Try#fork{active=true},V#vm.forks)}));
    _->loop(P,PC,1,remove_fork(V))
end;
loop(_,_,2,#vm{nforks=0,error=Error})->error({uncaught_jq_error,Error});
loop(P,PC,2,V)->F=last_fork(V),Next=remove_fork(V),case F#fork.kind=:=2 andalso F#fork.active of
    false->loop(P,PC,2,Next);
    true when F#fork.label=/=0->case V#vm.error of {jq_label,Id}when Id=:=F#fork.label->loop(P,PC,1,restore(F,Next));_->loop(P,PC,2,Next)end;
    %% Catch replaces the saved input, preserving the surrounding expression stack.
    true->case V#vm.error of {jq_label,_}->loop(P,PC,2,Next);Error->{_,Restored}=pop(restore(F,Next)),loop(P,F#fork.pc,0,push(Error,Restored))end
end;
loop(P,PC,0,V)->Op=element(PC+1,P#program.code),
    %% jq failures unwind to a saved fork; host errors remain visible.
    Result=try step(Op,P,PC+1,V)catch throw:{jq_error,E}->{PC,2,V#vm{error=E}}end,
    {NextPC,Mode,Next}=Result,loop(P,NextPC,Mode,Next).
word(P,PC)->element(PC+1,P#program.code).
step(1,_,PC,V)->{PC,0,push(peek(V),V)};
step(2,_,PC,V)->{_,Next}=pop(V),{PC,0,Next};
step(3,P,PC,V)->{_,Next}=pop(V),{PC+1,0,push(element(word(P,PC)+1,P#program.constants),Next)};
step(4,_,PC,V)->{_,Next}=pop(V),{PC,0,push({},Next)};
step(5,_,PC,V)->{Input,Next}=pop(V),{PC,0,push(Input,push(jq_values:empty_object(),Next))};
step(6,_,PC,V)->{PC,0,push(peek(V),V#vm{subexp=V#vm.subexp+1})};
step(7,_,PC,V)->{A,V1}=pop(V),{B,V2}=pop(V1),{PC,0,push(B,push(A,V2#vm{subexp=V2#vm.subexp-1}))};
step(Op,P,PC,V)when Op=:=8;Op=:=9->{T,V1}=pop(V),{Key,NextPC,V2}=case Op of 9->{element(word(P,PC)+1,P#program.constants),PC+1,V1};8->{K,Tail}=pop(V1),{K,PC,Tail}end,
    check_path(T,V2),Value=jq_values:index(T,Key),{NextPC,0,push(Value,path_append(Key,Value,V2))};
step(10,_,PC,V)->{C,V1}=pop(V),Type=jq_values:type(C),case Type=:=5 orelse Type=:=6 of true->ok;false->throw({jq_error,<<"Cannot iterate over ",(jq_values:type_name(C))/binary>>})end,check_path(C,V1),N=jq_values:size(C),
    case N of 0->{PC,1,V1};_->Keys=case Type of 6->jq_values:keys(C,false);5->null end,
        V2=case N>1 of true->Forked=fork(4,PC,V1),F=last_fork(Forked),put_fork(F#fork{aux=C,aux2=Keys,index=1},Forked);false->V1 end,{PC,0,each(C,Keys,0,V2)}end;
step(11,_,PC,V)->{T,V1}=pop(V),{Upper,V2}=pop(V1),{Lower,V3}=pop(V2),case V3#vm.path=/=nil andalso V3#vm.subexp=:=0 of true->throw({jq_error,<<"jq-core does not support slice paths">>});false->ok end,{PC,0,push(jq_values:slice(T,Lower,Upper),V3)};
step(12,P,PC,V)->{PC+1,0,fork(1,word(P,PC),V)};
step(13,P,PC,V)->{word(P,PC),0,V};
step(14,P,PC,V)->{Cond,Next}=pop(V),{case jq_values:truth(Cond)of true->PC+1;false->word(P,PC)end,0,Next};
step(15,P,PC,V)->{Input,V1}=pop(V),{Cond,V2}=pop(V1),{case jq_values:truth(Cond)of true->PC+1;false->word(P,PC)end,0,push(Input,V2)};
step(16,P,PC,V)->{case jq_values:truth(peek(V))of true->PC+1;false->word(P,PC)end,0,V};
step(17,_,PC,V)->{PC,1,V};
step(Op,P,PC,V)when Op>=18,Op=<22;Op=:=35;Op=:=36->Id=hop(V#vm.fp,word(P,PC),V),Slot=word(P,PC+1)+1,F=frame(Id,V),
    {Mode,Next}=case Op of
    18->{Value,Tail}=pop(V),{0,put_frame(Id,F#frame{locals=setelement(Slot,F#frame.locals,Value)},Tail)};
    19->{Input,V1}=pop(V),{Value,V2}=pop(V1),{0,push(Input,put_frame(Id,F#frame{locals=setelement(Slot,F#frame.locals,Value)},V2))};
    20->load_local(Id,F,Slot,false,V);
    21->load_local(Id,F,Slot,true,V);
    22->{Value,Tail}=pop(V),Old=element(Slot,F#frame.locals),Builder=case Old of {collect,Rev,Count}->{collect,[Value|Rev],Count+1};_-> {collect,[Value|lists:reverse(tuple_to_list(Old))],tuple_size(Old)+1}end,
        {0,put_frame(Id,F#frame{locals=setelement(Slot,F#frame.locals,Builder)},Tail)};
    35->Label=V#vm.labels+1,V1=put_frame(Id,F#frame{locals=setelement(Slot,F#frame.locals,{jq_label,Label})},V#vm{labels=Label}),V2=fork(2,-1,V1),{0,put_fork((last_fork(V2))#fork{label=Label},V2)};
    36->{2,V#vm{error=element(Slot,F#frame.locals)}}end,{PC+2,Mode,Next};
step(23,_,PC,V)->{Input,V1}=pop(V),{Value,V2}=pop(V1),{Key,V3}=pop(V2),{Object,V4}=pop(V3),{PC,0,push(Input,push(jq_values:with(Object,Key,Value),V4))};
step(24,P,PC,V)->N=word(P,PC),{_,V1}=pop(V),{Step,V2}=case N of 3->pop(V1);_-> {1,V1}end,{Upto,V3}=pop(V2),{From,V4}=pop(V3),
    case is_number(From)andalso is_number(Upto)andalso is_number(Step)of false->throw({jq_error,<<"number required">>});true->ok end,
    case more(From,Upto,Step)of false->{PC+1,1,V4};true->V5=case more(From+Step,Upto,Step)of true->Forked=fork(5,PC+1,V4),put_fork((last_fork(Forked))#fork{aux=From+Step,aux2=Upto,aux3=Step},Forked);false->V4 end,{PC+1,0,push(From,V5)}end;
step(25,_,PC,V)->{Value,V1}=pop(V),Saved=push(V#vm.subexp,push(V#vm.vat,push(V#vm.path,V1))),{PC,0,push(Value,Saved#vm{path={},vat=Value,subexp=0})};
step(26,_,PC,V)->{Value,V1}=pop(V),check_path(Value,V1),Path=V1#vm.path,{Sub,V2}=pop(V1),{Vat,V3}=pop(V2),{OldPath,V4}=pop(V3),{PC,0,push(Path,V4#vm{path=OldPath,vat=Vat,subexp=Sub})};
step(27,P,PC,V)->Id=word(P,PC),N=word(P,PC+1),{Input,V1}=pop(V),{Args,V2}=pop_args(N,V1,[]),case Id of 27->check_path(Input,V2);_->ok end,
    Value=jq_values:native(Id,Input,Args),V3=case Id=:=27 andalso V2#vm.path=/=nil andalso V2#vm.subexp=:=0 of true->[Path]=Args,V2#vm{path=list_to_tuple(tuple_to_list(V2#vm.path)++tuple_to_list(Path)),vat=Value};false->V2 end,{PC+2,0,push(Value,V3)};
step(Op,P,PC,V0)when Op=:=28;Op=:=29->V=compact(V0),Fn=word(P,PC),Hops=word(P,PC+1),N=word(P,PC+2),Site=V#vm.fp,F=frame(Site,V),
    {Caller,Return}=tail_caller(Op=:=29,PC+3+N,F,V),Params=list_to_tuple([{word(P,PC+3+I),Site}||I<-lists:seq(0,N-1)]),
    {Id,Next}=new_frame(P,Fn,hop(Site,Hops,V),Caller,Return,Params,V),Def=element(Fn+1,P#program.functions),{Def#jq_fn.entry,0,Next#vm{fp=Id}};
step(Op,P,PC,V0)when Op=:=30;Op=:=31->V=compact(V0),Owner=frame(hop(V#vm.fp,word(P,PC),V),V),{Fn,Env}=element(word(P,PC+1)+1,Owner#frame.params),F=frame(V#vm.fp,V),
    {Caller,Return}=tail_caller(Op=:=31,PC+2,F,V),{Id,Next}=new_frame(P,Fn,Env,Caller,Return,{},V),Def=element(Fn+1,P#program.functions),{Def#jq_fn.entry,0,Next#vm{fp=Id}};
step(32,_,PC,V)->F=frame(V#vm.fp,V),case F#frame.caller of -1->{Out,Next}=pop(V),{PC,1,Next#vm{outputs=[Out|Next#vm.outputs]}};Caller->{F#frame.retpc,0,V#vm{fp=Caller}}end;
step(33,P,PC,V)->{PC+1,0,fork(2,word(P,PC),V)};
step(34,_,PC,V)->case find_try(V#vm.nforks-1,V)of -1->{PC,0,V};I->Try=array:get(I,V#vm.forks),V1=V#vm{forks=array:set(I,Try#fork{active=false},V#vm.forks)},V2=fork(3,-1,V1),{PC,0,put_fork((last_fork(V2))#fork{index=I},V2)}end;
step(37,P,PC,V)->{_,V1}=pop(V),{Left,V2}=pop(V1),{Right,V3}=pop(V2),{PC+1,0,push(jq_values:bin(word(P,PC),Left,Right),V3)};
step(38,_,PC,V)->{Value,Next}=pop(V),case is_number(Value)of true->{PC,0,push(-Value,Next)};false->throw({jq_error,<<"number required">>})end;
step(39,_,PC,V)->{Value,Next}=pop(V),{PC,0,push(jq_values:truth(Value),Next)};
step(Op,_,PC,_)->error({invalid_jq_opcode,Op,PC-1}).
load_local(Id,F,Slot,Clear,V)->{_,V1}=pop(V),Raw=element(Slot,F#frame.locals),Value=case Raw of {collect,Rev,_}->list_to_tuple(lists:reverse(Rev));_->Raw end,
    Stored=case Clear of true->null;false->Value end,{0,push(Value,put_frame(Id,F#frame{locals=setelement(Slot,F#frame.locals,Stored)},V1))}.
pop_args(0,V,Acc)->{Acc,V};pop_args(N,V,Acc)->{Value,Next}=pop(V),pop_args(N-1,Next,[Value|Acc]).
tail_caller(true,_,F,V)when V#vm.nforks=:=F#frame.base,F#frame.caller>=0->{F#frame.caller,F#frame.retpc};
tail_caller(_,PC,_,V)->{V#vm.fp,PC}.
find_try(-1,_)->-1;
find_try(I,V)->F=array:get(I,V#vm.forks),case F#fork.kind=:=2 andalso F#fork.active of true->I;false->find_try(I-1,V)end.
