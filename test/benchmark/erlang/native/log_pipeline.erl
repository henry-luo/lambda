-module(log_pipeline).
-export([prepare/0,work/1]).
services()->[<<"api">>,<<"worker">>,<<"db">>,<<"cache">>].
prepare()->[make_line(I)||I<-lists:seq(0,11999)].
make_line(I)->Time=io_lib:format("2026-09-07T~2..0B:~2..0B:~2..0BZ",[I rem 24,I rem 60,(I*7)rem 60]),Level=case I of _ when I rem 13=:=0-> <<"ERROR">>;_ when I rem 5=:=0-> <<"WARN">>;_-> <<"INFO">>end,Service=lists:nth(I rem 4+1,services()),
    Prefix=case I rem 3 of 0->[Time," level=",Level," service=",Service];_->[Time," ",Level," ",Service]end,
    Status=case I of _ when I rem 19=:=0->503;_ when I rem 7=:=0->404;_->200 end,Route=case I rem 2 of 0-> <<"/v1/items">>;_-> <<"/v1/search">>end,Message=case I rem 11 of 0-> <<"retry-scheduled">>;_-> <<"request-complete">>end,
    iolist_to_binary([Prefix," status=",integer_to_binary(Status)," latency=",integer_to_binary((I*37)rem 900+4)," region=",element((I*3)rem 3+1,{<<"us-east">>,<<"eu-west">>,<<"ap-south">>})," route=",Route," bytes=",integer_to_binary((I*113)rem 50000+512)," message=",Message]).
parse(Line)->[Time,First,Second|Rest]=binary:split(Line,<<" ">>,[global]),R=#{<<"timestamp">>=>Time,<<"level">>=><<>>,<<"service">>=><<>>,<<"status">>=>0,<<"latency">>=>0,<<"region">>=><<>>,<<"route">>=><<>>,<<"bytes">>=>0,<<"message">>=><<>>},
    {Initial,Tokens}=case binary:match(First,<<"=">>)of nomatch->{R#{<<"level">>=>First,<<"service">>=>Second},Rest};_->{R,[First,Second|Rest]}end,
    lists:foldl(fun(Token,Acc)->case binary:split(Token,<<"=">>)of [K,V]->Value=case K of <<"status">>->binary_to_integer(V);<<"latency">>->binary_to_integer(V);<<"bytes">>->binary_to_integer(V);_->V end,Acc#{K=>Value};_->Acc end end,Initial,Tokens).
process(Lines)->Groups=maps:from_list([{K,{0,0,0,0,0}}||K<-services()]),lists:foldl(fun(Line,{Gs,Accepted,Rejected})->R=parse(Line),case maps:get(<<"status">>,R)>=500 orelse maps:get(<<"level">>,R)=:= <<"ERROR">>of true->{Gs,Accepted,Rejected+1};false->Service=maps:get(<<"service">>,R),{C,E,S,L,B}=maps:get(Service,Gs),Latency=maps:get(<<"latency">>,R),Slow=case Latency>=500 of true->1;false->0 end,{Gs#{Service=>{C+1,E,S+Slow,L+Latency,B+maps:get(<<"bytes">>,R)}},Accepted+1,Rejected}end end,{Groups,0,0},Lines).
work(Lines)->lists:foldl(fun(R,S)->{G,A,B}=process(Lines),{_,_,_,Latency,_}=maps:get(<<"api">>,G),{_,_,_,_,Bytes}=maps:get(<<"worker">>,G),(S+A*31+B*17+Latency+Bytes+R)rem 1000000007 end,0,lists:seq(0,179)).
