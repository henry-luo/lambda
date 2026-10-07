%% Myers bisect, half matches and semantic cleanup from fast-diff.js.
-module(fast_diff).
-export([prepare/0,work/1,main/3]).
pre(A,B)->pre(A,B,0,min(byte_size(A),byte_size(B))).
pre(_,_,N,N)->N;
pre(A,B,I,N)->case binary:at(A,I)=:=binary:at(B,I)of true->pre(A,B,I+1,N);false->I end.
suf(A,B)->suf(A,B,0,min(byte_size(A),byte_size(B))).
suf(_,_,N,N)->N;
suf(A,B,I,N)->case binary:at(A,byte_size(A)-I-1)=:=binary:at(B,byte_size(B)-I-1)of true->suf(A,B,I+1,N);false->I end.
sub(B,Start)->binary:part(B,Start,byte_size(B)-Start).
sub(B,Start,Length)->binary:part(B,Start,Length).
cat(A,B)-> <<A/binary,B/binary>>.
starts(A,B)->byte_size(A)>=byte_size(B) andalso sub(A,0,byte_size(B))=:=B.
ends(A,B)->byte_size(A)>=byte_size(B) andalso sub(A,byte_size(A)-byte_size(B))=:=B.
overlap(A,B)->overlap(A,B,min(byte_size(A),byte_size(B))).
overlap(_,_,0)->0;
overlap(A,B,N)->case ends(A,sub(B,0,N))of true->N;false->overlap(A,B,N-1)end.
find(A,B,Start)when Start>byte_size(A)->-1;
find(A,B,Start)->case binary:match(A,B,[{scope,{Start,byte_size(A)-Start}}])of nomatch->-1;{P,_}->P end.
seed(Long,Short,Start)->Seed=sub(Long,Start,byte_size(Long)div 4),Best=seed_positions(Long,Short,Start,Seed,find(Short,Seed,0),none),
    case Best of none->none;{_,_,_,_,Middle}when byte_size(Middle)*2>=byte_size(Long)->Best;_->none end.
seed_positions(_,_,_,_,-1,Best)->Best;
seed_positions(Long,Short,Start,Seed,Pos,Best)->Prefix=pre(sub(Long,Start),sub(Short,Pos)),Suffix=suf(sub(Long,0,Start),sub(Short,0,Pos)),Middle=sub(Short,Pos-Suffix,Prefix+Suffix),
    Next=case Best of none->{sub(Long,0,Start-Suffix),sub(Long,Start+Prefix),sub(Short,0,Pos-Suffix),sub(Short,Pos+Prefix),Middle};{_,_,_,_,Old}when byte_size(Middle)>byte_size(Old)->{sub(Long,0,Start-Suffix),sub(Long,Start+Prefix),sub(Short,0,Pos-Suffix),sub(Short,Pos+Prefix),Middle};_->Best end,
    seed_positions(Long,Short,Start,Seed,find(Short,Seed,Pos+1),Next).
half(A,B)->{Long,Short}=case byte_size(A)>byte_size(B)of true->{A,B};false->{B,A}end,
    case byte_size(Long)<4 orelse byte_size(Short)*2<byte_size(Long)of true->none;false->One=seed(Long,Short,(byte_size(Long)+3)div 4),Two=seed(Long,Short,(byte_size(Long)+1)div 2),
        Best=case {One,Two}of {none,_}->Two;{_,none}->One;{{_,_,_,_,X},{_,_,_,_,Y}}when byte_size(X)>byte_size(Y)->One;_->Two end,
        case Best of none->none;_ when byte_size(A)>byte_size(B)->Best;{L1,L2,S1,S2,M}->{S1,S2,L1,L2,M}end end.
split(A,B,X,Y)->main(sub(A,0,X),sub(B,0,Y),false)++main(sub(A,X),sub(B,Y),false).
snake(A,B,X,Y,false)when X<byte_size(A),Y<byte_size(B)->case binary:at(A,X)=:=binary:at(B,Y)of true->snake(A,B,X+1,Y+1,false);false->{X,Y}end;
snake(A,B,X,Y,true)when X<byte_size(A),Y<byte_size(B)->case binary:at(A,byte_size(A)-X-1)=:=binary:at(B,byte_size(B)-Y-1)of true->snake(A,B,X+1,Y+1,true);false->{X,Y}end;
snake(_,_,X,Y,_)->{X,Y}.
bisect(A,B)->Max=(byte_size(A)+byte_size(B)+1)div 2,Initial=setelement(Max+2,erlang:make_tuple(2*Max,-1),0),bisect_d(A,B,0,Max,Initial,Initial,0,0,0,0).
bisect_d(A,B,Max,Max,_,_,_,_,_,_)->[{-1,A},{1,B}];
bisect_d(A,B,D,Max,F,R,Fs,Fe,Rs,Re)->case diagonals(A,B,D,Max,-D+Fs,D-Fe,F,R,Fs,Fe,false)of
    {split,X,Y}->split(A,B,X,Y);{done,F1,Fs1,Fe1}->case diagonals(A,B,D,Max,-D+Rs,D-Re,R,F1,Rs,Re,true)of {split,X,Y}->split(A,B,X,Y);{done,R1,Rs1,Re1}->bisect_d(A,B,D+1,Max,F1,R1,Fs1,Fe1,Rs1,Re1)end end.
diagonals(_,_,_,_,K,End,This,_,Start,Finish,_)when K>End->{done,This,Start,Finish};
diagonals(A,B,D,Offset,K,End,This,Other,Start,Finish,Reverse)->P=Offset+K,X0=case K=:= -D orelse(K=/=D andalso element(P,This)<element(P+2,This))of true->element(P+2,This);false->element(P,This)+1 end,
    {X,Y}=snake(A,B,X0,X0-K,Reverse),Next=setelement(P+1,This,X),N=byte_size(A),M=byte_size(B),Delta=N-M,Front=Delta rem 2=/=0,
    case {X>N,Y>M}of {true,_}->diagonals(A,B,D,Offset,K+2,End,Next,Other,Start,Finish+2,Reverse);{_,true}->diagonals(A,B,D,Offset,K+2,End,Next,Other,Start+2,Finish,Reverse);
        _->Opp=Offset+Delta-K,Valid=Opp>=0 andalso Opp<tuple_size(Other) andalso element(Opp+1,Other)=/= -1,
            Found=case Valid andalso Front=/=Reverse of false->none;true->V=element(Opp+1,Other),case Reverse of false when X>=N-V->{split,X,Y};true when V>=N-X->{split,V,Offset+V-Opp};_->none end end,
            case Found of none->diagonals(A,B,D,Offset,K+2,End,Next,Other,Start,Finish,Reverse);_->Found end end.
compute(<<>>,B)->[{1,B}];compute(A,<<>>)->[{-1,A}];
compute(A,B)->{Long,Short,Kind}=case byte_size(A)>byte_size(B)of true->{A,B,-1};false->{B,A,1}end,
    case find(Long,Short,0)of Pos when Pos>=0->[{Kind,sub(Long,0,Pos)},{0,Short},{Kind,sub(Long,Pos+byte_size(Short))}];_ when byte_size(Short)=:=1->[{-1,A},{1,B}];_->case half(A,B)of none->bisect(A,B);{A1,A2,B1,B2,M}->main(A1,B1,false)++[{0,M}|main(A2,B2,false)]end end.
at(P,I)->lists:nth(I+1,P).
splice(P,Start,Count,Replacement)->{Before,Rest}=lists:split(Start,P),Before++Replacement++lists:nthtail(Count,Rest).
text(P,I)->{_,T}=at(P,I),T.
settext(P,I,T)->{O,_}=at(P,I),splice(P,I,1,[{O,T}]).
merge(P)->Merged=merge_loop(P++[{0,<<>>}],0,0,0,<<>>,<<>>),Clean=case lists:reverse(Merged)of [{_,<<>>}|Rest]->lists:reverse(Rest);_->Merged end,
    {Shifted,Changed}=shift(Clean,1,false),case Changed of true->merge(Shifted);false->Shifted end.
merge_loop(P,I,_,_,_,_)when I>=length(P)->P;
merge_loop(P,I,Ins,Del,Inserted,Removed)->case at(P,I)of {_,<<>>}when I<length(P)-1->merge_loop(splice(P,I,1,[]),I,Ins,Del,Inserted,Removed);
    {1,T}->merge_loop(P,I+1,Ins+1,Del,cat(Inserted,T),Removed);
    {-1,T}->merge_loop(P,I+1,Ins,Del+1,Inserted,cat(Removed,T));
    {0,_}->Previous=I-Ins-Del-1,
        {P1,I1,Insert1,Remove1}=case Removed=/= <<>> andalso Inserted=/= <<>>of false->{P,I,Inserted,Removed};true->Prefix=pre(Inserted,Removed),
            {Q,J}=case Prefix of 0->{P,I};_ when Previous>=0->{settext(P,Previous,cat(text(P,Previous),sub(Inserted,0,Prefix))),I};_->{splice(P,0,0,[{0,sub(Inserted,0,Prefix)}]),I+1}end,
            In=sub(Inserted,Prefix),Rm=sub(Removed,Prefix),Suffix=suf(In,Rm),{settext(Q,J,cat(sub(In,byte_size(In)-Suffix),text(Q,J))),J,sub(In,0,byte_size(In)-Suffix),sub(Rm,0,byte_size(Rm)-Suffix)}end,
        {P2,I2}=case Removed=/= <<>> orelse Inserted=/= <<>>of false->{P1,I1};true->Replacement=[{O,T}||{O,T}<-[{-1,Remove1},{1,Insert1}],T=/= <<>>],{splice(P1,I1-Ins-Del,Ins+Del,Replacement),I1-Ins-Del+length(Replacement)}end,
        case I2>0 andalso element(1,at(P2,I2-1))=:=0 of true->P3=settext(P2,I2-1,cat(text(P2,I2-1),text(P2,I2))),merge_loop(splice(P3,I2,1,[]),I2,0,0,<<>>,<<>>);false->merge_loop(P2,I2+1,0,0,<<>>,<<>>)end end.
shift(P,I,Changed)when I>=length(P)-1->{P,Changed};
shift(P,I,Changed)->case {at(P,I-1),at(P,I),at(P,I+1)}of {{0,Before},{O,Edit},{0,After}}->case ends(Edit,Before)of true->Q=settext(settext(P,I,cat(Before,sub(Edit,0,byte_size(Edit)-byte_size(Before)))),I+1,cat(Before,After)),shift(splice(Q,I-1,1,[]),I+1,true);
    false->case starts(Edit,After)of true->Q=settext(settext(P,I-1,cat(Before,After)),I,cat(sub(Edit,byte_size(After)),After)),shift(splice(Q,I+1,1,[]),I+1,true);false->shift(P,I+1,Changed)end end;_->shift(P,I+1,Changed)end.
nonalpha(C)->not((C>=$0 andalso C=<$9)orelse(C>=$A andalso C=<$Z)orelse(C>=$a andalso C=<$z)).
white(C)->C=:=$  orelse C=:=$\t orelse C=:=$\n orelse C=:=$\r.
score(<<>>,_)->6;score(_,<<>>)->6;
score(Left,Right)->A=binary:last(Left),B=binary:first(Right),Na=nonalpha(A),Nb=nonalpha(B),Wa=Na andalso white(A),Wb=Nb andalso white(B),La=Wa andalso(A=:=$\n orelse A=:=$\r),Lb=Wb andalso(B=:=$\n orelse B=:=$\r),
    Blank=(La andalso(ends(Left,<<"\n\n">>)orelse ends(Left,<<"\n\r\n">>)))orelse(Lb andalso(starts(Right,<<"\n\n">>)orelse starts(Right,<<"\r\n\r\n">>))),
    if Blank->5;La orelse Lb->4;Na andalso not Wa andalso Wb->3;Wa orelse Wb->2;Na orelse Nb->1;true->0 end.
best(<<>>,_,_,Best,_)->Best;
best(_,_,<<>>,Best,_)->Best;
best(Edit,Left,Right,Best,Score)->case binary:first(Edit)=:=binary:first(Right)of false->Best;true->L=cat(Left,sub(Edit,0,1)),E=cat(sub(Edit,1),sub(Right,0,1)),R=sub(Right,1),S=score(L,E)+score(E,R),{B,N}=case S>=Score of true->{{L,E,R},S};false->{Best,Score}end,best(E,L,R,B,N)end.
lossless(P,I)when I>=length(P)-1->P;
lossless(P,I)->case {at(P,I-1),at(P,I+1)}of {{0,Left0},{0,Right0}}->Edit0=text(P,I),Suffix=suf(Left0,Edit0),Common=sub(Edit0,byte_size(Edit0)-Suffix),Left=sub(Left0,0,byte_size(Left0)-Suffix),Edit=cat(Common,sub(Edit0,0,byte_size(Edit0)-Suffix)),Right=cat(Common,Right0),
    {Bl,Be,Br}=best(Edit,Left,Right,{Left,Edit,Right},score(Left,Edit)+score(Edit,Right)),case Left0=:=Bl of true->lossless(P,I+1);false->{Q,J}=case Bl of <<>>->{splice(P,I-1,1,[]),I-1};_->{settext(P,I-1,Bl),I}end,Q1=settext(Q,J,Be),case Br of <<>>->lossless(splice(Q1,J+1,1,[]),J);_->lossless(settext(Q1,J+1,Br),J+1)end end;
    _->lossless(P,I+1)end.
semantic(P)->{Q,Changed}=semantic_loop(P,0,[],<<>>,0,0,0,0,false),R=case Changed of true->merge(Q);false->Q end,semantic_overlap(lossless(R,1),1).
semantic_loop(P,I,_,_,_,_,_,_,Changed)when I>=length(P)->{P,Changed};
semantic_loop(P,I,Equalities,Last,Bi,Bd,Ai,Ad,Changed)->case at(P,I)of {0,T}->semantic_loop(P,I+1,[I|Equalities],T,Ai,Ad,0,0,Changed);
    {O,T}->{Ni,Nd}=case O of 1->{Ai+byte_size(T),Ad};_->{Ai,Ad+byte_size(T)}end,
        case Last=/= <<>> andalso byte_size(Last)=<max(Bi,Bd) andalso byte_size(Last)=<max(Ni,Nd)of false->semantic_loop(P,I+1,Equalities,Last,Bi,Bd,Ni,Nd,Changed);true->[At|Rest]=Equalities,Q=splice(P,At,0,[{-1,Last}]),Q1=splice(Q,At+1,1,[{1,text(Q,At+1)}]),Es=case Rest of []->[];[_|Tail]->Tail end,Next=case Es of []->0;[H|_]->H+1 end,semantic_loop(Q1,Next,Es,<<>>,0,0,0,0,true)end end.
semantic_overlap(P,I)when I>=length(P)->P;
semantic_overlap(P,I)->case {at(P,I-1),at(P,I)}of {{-1,Removed},{1,Inserted}}->F=overlap(Removed,Inserted),R=overlap(Inserted,Removed),case F>=R andalso(F*2>=byte_size(Removed)orelse F*2>=byte_size(Inserted))of
    true->Q=splice(P,I,0,[{0,sub(Inserted,0,F)}]),semantic_overlap(settext(settext(Q,I-1,sub(Removed,0,byte_size(Removed)-F)),I+1,sub(Inserted,F)),I+3);
    false->case R>F andalso(R*2>=byte_size(Removed)orelse R*2>=byte_size(Inserted))of true->Q=splice(P,I,0,[{0,sub(Removed,0,R)}]),Q1=splice(Q,I-1,1,[{1,sub(Inserted,0,byte_size(Inserted)-R)}]),semantic_overlap(splice(Q1,I+1,1,[{-1,sub(Removed,R)}]),I+3);false->semantic_overlap(P,I+2)end end;
    _->semantic_overlap(P,I+1)end.
main(Same,Same,_)->case Same of <<>>->[];_->[{0,Same}]end;
main(A0,B0,Cleanup)->Prefix=pre(A0,B0),Leading=sub(A0,0,Prefix),A1=sub(A0,Prefix),B1=sub(B0,Prefix),Suffix=suf(A1,B1),Trailing=sub(A1,byte_size(A1)-Suffix),A=sub(A1,0,byte_size(A1)-Suffix),B=sub(B1,0,byte_size(B1)-Suffix),
    P=case Leading of <<>>->compute(A,B);_->[{0,Leading}|compute(A,B)]end,Q=case Trailing of <<>>->P;_->P++[{0,Trailing}]end,R=merge(Q),case Cleanup of true->semantic(R);false->R end.
prepare()->{ok,B}=file:read_file("test/benchmark/text/fast_diff_pairs.json"),json:decode(B).
work(Pairs)->lists:foldl(fun(_,Sum)->lists:foldl(fun([A,B],S)->P=main(A,B,true),lists:foldl(fun({O,T},C)->(C+O*31+byte_size(T))rem 1000000007 end,(S+length(P)*17)rem 1000000007,P)end,Sum,Pairs)end,0,lists:seq(1,256)).
