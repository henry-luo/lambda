%% Native recursive-descent parser; source offsets are byte offsets in jq text.
-module(jq_parse).
-export([parse/1]).
-include("jq.hrl").
-record(p,{src,pos=0,len}).
parse(Src)->{N,P}=pipe(#p{src=Src,len=byte_size(Src)}),End=space(P),case End#p.pos=:=End#p.len of true->N;false->fail(End,trailing_input)end.
fail(P,Why)->error({jq_parse,Why,P#p.pos}).
ch(P)->case P#p.pos<P#p.len of true->binary:at(P#p.src,P#p.pos);false->-1 end.
advance(P,N)->P#p{pos=P#p.pos+N}.
space(P)->case ch(P)of C when C=:=32;C=:=9;C=:=10;C=:=13->space(advance(P,1));35->space(comment(advance(P,1)));_->P end.
comment(P)->case ch(P)of C when C=:=10;C=:= -1->P;_->comment(advance(P,1))end.
peek(P0,Token)->P=space(P0),Size=byte_size(Token),P#p.pos+Size=<P#p.len andalso binary:part(P#p.src,P#p.pos,Size)=:=Token.
accept(P0,Token)->P=space(P0),case peek(P,Token)of true->{true,advance(P,byte_size(Token))};false->{false,P}end.
expect(P,Token)->case accept(P,Token)of {true,Next}->Next;{false,Next}->fail(Next,{expected,Token})end.
ident_start(C)->C=:=$_ orelse(C>=$a andalso C=<$z)orelse(C>=$A andalso C=<$Z).
ident_char(C)->ident_start(C)orelse(C>=$0 andalso C=<$9).
ident(P0)->P=space(P0),case ident_start(ch(P))of true->End=ident_end(P),{binary:part(P#p.src,P#p.pos,End#p.pos-P#p.pos),End};false->fail(P,identifier)end.
ident_end(P)->case ident_char(ch(P))of true->ident_end(advance(P,1));false->P end.
variable(P)->ident(expect(P,<<"$">>)).
keyword(P0,Word)->P=space(P0),case accept(P,Word)of {true,End}->case ident_char(ch(End))of false->{true,End};true->{false,P}end;_-> {false,P}end.
mk(K,A,B)->#n{kind=K,a=A,b=B}.
lit(V)->#n{kind=3,value=V}.
named(K,Name,A,B)->#n{kind=K,name=Name,a=A,b=B}.
bin(Op,A,B)->#n{kind=11,op=Op,a=A,b=B}.
call(Name,Args)->#n{kind=26,name=Name,list=Args}.
string(P)->string_loop(expect(P,<<"\"">>),[],nil).
string_loop(P,Chars,Result)->case ch(P)of
    -1->fail(P,unterminated_string);
    34->Tail=lit(iolist_to_binary(lists:reverse(Chars))),{case Result of nil->Tail;_->bin(1,Result,Tail)end,advance(P,1)};
    92->E=advance(P,1),case ch(E)of
        40->Piece=lit(iolist_to_binary(lists:reverse(Chars))),{Expr,End}=pipe(advance(E,1)),Next=expect(End,<<")">>),Joined=bin(1,Piece,mk(19,Expr,call(<<"tostring">>,[]))),string_loop(Next,[],case Result of nil->Joined;_->bin(1,Result,Joined)end);
        117->Digits=binary:part(E#p.src,E#p.pos+1,4),C=binary_to_integer(Digits,16),string_loop(advance(E,5),[unicode:characters_to_binary([C])|Chars],Result);
        C->Value=case C of 110->10;114->13;116->9;98->8;102->12;_->C end,string_loop(advance(E,1),[Value|Chars],Result)end;
    C->string_loop(advance(P,1),[C|Chars],Result)end.
number(P)->End=number_end(P),Text=binary:part(P#p.src,P#p.pos,End#p.pos-P#p.pos),{lit(json:decode(Text)),End}.
number_end(P)->case ch(P)of C when C>=$0,C=<$9;C=:=46->number_end(advance(P,1));C when C=:=101;C=:=69->Next=advance(P,1),case ch(Next)of Sign when Sign=:=43;Sign=:=45->number_end(advance(Next,1));_->number_end(Next)end;_->P end.
object_value(P)->case accept(P,<<"-">>)of {true,Next}->{N,End}=object_value(Next),{mk(10,N,nil),End};{false,Next}->{N,End}=postfix(Next,false),case not peek(End,<<"|=">>) andalso peek(End,<<"|">>)of true->{_,After}=accept(End,<<"|">>),{Right,Done}=object_value(After),{mk(19,N,Right),Done};false->{N,End}end end.
object(P)->case accept(P,<<"}">>)of {true,End}->{#n{kind=9},End};{false,Next}->object_pairs(Next,[])end.
object_pairs(P,Acc)->case peek(P,<<"$">>)of true->{Name,End}=variable(P),Key=lit(Name),Value=named(27,Name,nil,nil),Next=End;
    false->{Key,End}=case peek(P,<<"\"">>)of true->string(P);false->case accept(P,<<"(">>)of {true,A}->{N,B}=pipe(A),{N,expect(B,<<")">>)};{false,A}->{Name,B}=ident(A),{lit(Name),B}end end,
        {Value,Next}=case accept(End,<<":">>)of {true,A1}->object_value(A1);{false,A1}->{mk(4,mk(1,nil,nil),Key),A1}end end,
    New=[Value,Key|Acc],case accept(Next,<<",">>)of {true,After}->object_pairs(After,New);{false,After}->{#n{kind=9,list=lists:reverse(New)},expect(After,<<"}">>)}end.
primary(P0)->P=space(P0),case ch(P)of
    46->case accept(P,<<"..">>)of {true,E}->{mk(2,nil,nil),E};{false,_}->E=advance(P,1),case ident_start(ch(E))of true->{Name,F}=ident(E),{mk(4,mk(1,nil,nil),lit(Name)),F};false->case ch(E)of 34->{N,F}=string(E),{mk(4,mk(1,nil,nil),N),F};_->{mk(1,nil,nil),E}end end end;
    C when C>=$0,C=<$9->number(P);
    34->string(P);
    40->{N,E}=pipe(advance(P,1)),{N,expect(E,<<")">>)};
    91->case accept(advance(P,1),<<"]">>)of {true,E}->{mk(8,nil,nil),E};{false,E}->{N,F}=pipe(E),{mk(8,N,nil),expect(F,<<"]">>)}end;
    123->object(advance(P,1));
    36->{Name,E}=variable(P),{named(27,Name,nil,nil),E};
    _->primary_keyword(P)end.
primary_keyword(P)->case keyword(P,<<"if">>)of {true,E}->conditional(E);{false,_}->case keyword(P,<<"try">>)of {true,E}->{Body,F}=postfix(E,false),case keyword(F,<<"catch">>)of {true,G}->{Handler,H}=postfix(G,false),{mk(7,Body,Handler),H};{false,G}->{mk(7,Body,nil),G}end;
    {false,_}->case keyword(P,<<"foreach">>)of {true,E}->reduce(E,true);{false,_}->case keyword(P,<<"reduce">>)of {true,E}->reduce(E,false);{false,_}->case keyword(P,<<"label">>)of {true,E}->{Name,F}=variable(E),{Body,G}=pipe(expect(F,<<"|">>)),{named(28,Name,Body,nil),G};{false,_}->case keyword(P,<<"break">>)of {true,E}->{Name,F}=variable(E),{named(29,Name,nil,nil),F};{false,_}->{Name,E}=ident(P),case accept(E,<<"(">>)of {true,F}->{Args,G}=arguments(F,[]),{call(Name,Args),G};{false,F}->{call(Name,[]),F}end end end end end end end.
arguments(P,Acc)->{N,E}=pipe(P),case accept(E,<<";">>)of {true,F}->arguments(F,[N|Acc]);{false,F}->{lists:reverse([N|Acc]),expect(F,<<")">>)}end.
reduce(P,Foreach)->{Source,E}=postfix(P,false),{true,F}=keyword(E,<<"as">>),{Name,G}=variable(F),{Init,H}=pipe(expect(G,<<"(">>)),{Update,I}=pipe(expect(H,<<";">>)),
    {Extract,J}=case Foreach andalso peek(I,<<";">>)of true->{_,K}=accept(I,<<";">>),pipe(K);false->{nil,I}end,Kind=case Foreach of true->23;false->22 end,{#n{kind=Kind,name=Name,a=Source,b=Init,c=Update,d=Extract},expect(J,<<")">>)}.
conditional(P)->{Condition,E}=pipe(P),{true,F}=keyword(E,<<"then">>),{Then,G}=pipe(F),case keyword(G,<<"elif">>)of {true,H}->{Else,I}=conditional(H),{#n{kind=24,a=Condition,b=Then,c=Else},I};{false,H}->{Else,I}=case keyword(H,<<"else">>)of {true,J}->pipe(J);{false,J}->{nil,J}end,{true,End}=keyword(I,<<"end">>),{#n{kind=24,a=Condition,b=Then,c=Else},End}end.
postfix(P,AllowAs)->{N,E}=primary(P),{Term,F}=suffixes(N,E),case AllowAs andalso element(1,keyword(F,<<"as">>))of true->{true,G}=keyword(F,<<"as">>),{Name,H}=variable(G),{Body,I}=pipe(expect(H,<<"|">>)),{named(21,Name,Term,Body),I};false->{Term,F}end.
suffixes(Term,P0)->P=space(P0),C=ch(P),Next=ch(advance(P,1)),Dot=C=:=46 andalso(ident_start(Next)orelse Next=:=34 orelse Next=:=91),case Dot of
    true->E=advance(P,1),case Next of 34->{N,F}=string(E),suffixes(mk(4,Term,N),F);91->suffixes(Term,E);_->{Name,F}=ident(E),suffixes(mk(4,Term,lit(Name)),F)end;
    false->case accept(P,<<"[">>)of {true,E}->{N,F}=index_suffix(Term,E),suffixes(N,F);{false,E}->case not peek(E,<<"?//">>) andalso peek(E,<<"?">>)of true->{_,F}=accept(E,<<"?">>),suffixes(mk(7,Term,nil),F);false->{Term,E}end end end.
index_suffix(Term,P)->case accept(P,<<"]">>)of {true,E}->{mk(5,Term,nil),E};{false,E}->case accept(E,<<":">>)of {true,F}->{Upper,G}=optional_upper(F),{#n{kind=6,a=Term,c=Upper},expect(G,<<"]">>)};{false,F}->{Idx,G}=pipe(F),case accept(G,<<":">>)of {true,H}->{Upper,I}=optional_upper(H),{#n{kind=6,a=Term,b=Idx,c=Upper},expect(I,<<"]">>)};{false,H}->{mk(4,Term,Idx),expect(H,<<"]">>)}end end end.
optional_upper(P)->case peek(P,<<"]">>)of true->{nil,P};false->pipe(P)end.
unary(P)->case not peek(P,<<"-=">>) andalso peek(P,<<"-">>)of true->{_,E}=accept(P,<<"-">>),{N,F}=postfix(E,true),{mk(10,N,nil),F};false->postfix(P,true)end.
level(P,Lower,Ops)->{N,E}=Lower(P),level_tail(N,E,Lower,Ops).
level_tail(N,P,Lower,Ops)->case operator(P,Ops)of none->{N,P};{Op,E}->{R,F}=Lower(E),Node=case Op of {kind,K}->mk(K,N,R);_->bin(Op,N,R)end,level_tail(Node,F,Lower,Ops)end.
operator(_,[])->none;
operator(P,[{Token,Op,Keyword}|Rest])->Blocked=case Token of <<"/">>->peek(P,<<"//">>)orelse peek(P,<<"/=">>);<<"+">>->peek(P,<<"+=">>);<<"-">>->peek(P,<<"-=">>);<<"*">>->peek(P,<<"*=">>);<<"%">>->peek(P,<<"%=">>);_->false end,
    Match=case Blocked of true->{false,P};false->case Keyword of true->keyword(P,Token);false->accept(P,Token)end end,case Match of {true,E}->{Op,E};_->operator(P,Rest)end.
mul(P)->level(P,fun unary/1,[{<<"*">>,3,false},{<<"/">>,4,false},{<<"%">>,5,false}]).
add(P)->level(P,fun mul/1,[{<<"+">>,1,false},{<<"-">>,2,false}]).
cmp(P)->{N,E}=add(P),case operator(E,[{<<"==">>,6,false},{<<"!=">>,7,false},{<<"<=">>,9,false},{<<">=">>,11,false},{<<"<">>,8,false},{<<">">>,10,false}])of none->{N,E};{Op,F}->{R,G}=add(F),{bin(Op,N,R),G}end.
and_expr(P)->level(P,fun cmp/1,[{<<"and">>,{kind,12},true}]).
or_expr(P)->level(P,fun and_expr/1,[{<<"or">>,{kind,13},true}]).
assign(P)->{N,E}=or_expr(P),case operator(E,[{<<"|=">>,{16,0},false},{<<"+=">>,{17,1},false},{<<"-=">>,{17,2},false},{<<"*=">>,{17,3},false},{<<"/=">>,{17,4},false},{<<"%=">>,{17,5},false},{<<"//=">>,{18,0},false},{<<"=">>,{15,0},false}])of none->{N,E};{{Kind,Op},F}->{R,G}=alt(F),{#n{kind=Kind,op=Op,a=N,b=R},G}end.
alt(P)->{N,E}=assign(P),case not peek(E,<<"//=">>) andalso peek(E,<<"//">>)of true->{_,F}=accept(E,<<"//">>),{R,G}=alt(F),{mk(14,N,R),G};false->{N,E}end.
comma(P)->level(P,fun alt/1,[{<<",">>,{kind,20},false}]).
definition(P)->{Name,E}=ident(P),{Params,Vars,F}=case accept(E,<<"(">>)of {true,G}->parameters(G,[],[]);{false,G}->{[],[],G}end,{Body,H}=pipe(expect(F,<<":">>)),{Rest,I}=pipe(expect(H,<<";">>)),{#n{kind=25,name=Name,a=Body,b=Rest,list=Params,pvar=Vars},I}.
parameters(P,Ns,Vs)->IsVar=peek(P,<<"$">>),{Name,E}=case IsVar of true->variable(P);false->ident(P)end,case accept(E,<<";">>)of {true,F}->parameters(F,[named(27,Name,nil,nil)|Ns],[IsVar|Vs]);{false,F}->{lists:reverse([named(27,Name,nil,nil)|Ns]),lists:reverse([IsVar|Vs]),expect(F,<<")">>)}end.
pipe(P)->case keyword(P,<<"def">>)of {true,E}->definition(E);{false,E}->{N,F}=comma(E),case not peek(F,<<"|=">>) andalso peek(F,<<"|">>)of true->{_,G}=accept(F,<<"|">>),{R,H}=pipe(G),{mk(19,N,R),H};false->{N,F}end end.
