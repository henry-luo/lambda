-module(jq_bench).
-export([run/1]).
prepare(Name)->Base="test/benchmark/text/jq/",{ok,Filter}=file:read_file(Base++lists:nthtail(3,Name)++".jq"),Program=jq_compile:program(Filter),
    Input=case Name of "jq_records"->{ok,JSON}=file:read_file(Base++"orders.json"),jq_values:decode(JSON);"jq_bf"->{ok,BF}=file:read_file(Base++"fib.bf"),BF;_->null end,{Program,Input}.
run(Name)->Expected=case Name of "jq_mix"->98172625;"jq_records"->878885883;"jq_bf"->478890292;"jq_tree"->313746104 end,
    native_bench:prepared(fun()->prepare(Name)end,fun({P,I})->jq_vm:run(P,I)end,
        fun(V)->native_bench:check(case V of [R]->R==Expected;_->false end)end,
        fun([R])->io:format("~s: CHECKSUM:~s~n",[Name,jq_values:number(R)])end,fun(_)->ok end).
