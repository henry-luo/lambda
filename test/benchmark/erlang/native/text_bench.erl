-module(text_bench).
-export([run/1]).
checksum(Prefix,Module,Expected)->native_bench:prepared(fun Module:prepare/0,fun Module:work/1,
    fun(V)->native_bench:check(V=:=Expected)end,fun(V)->io:format("~s~B~n",[Prefix,V])end,fun(_)->ok end).
run("text_search")->text_search:run();
run("prettier_ast")->pretty:run();
run("fast_diff")->checksum("CHECKSUM:",fast_diff,390912);
run("microdiff")->checksum("CHECKSUM:",microdiff,3278848);
run("hyphen")->checksum("CHECKSUM:",hyphen,1183296);
run("log_pipeline")->checksum("log_pipeline: CHECKSUM:",log_pipeline,292634526);
run("three_way_merge")->checksum("three_way_merge: CHECKSUM:",three_way_merge,342313356);
run("jq_"++_=Name)->jq_bench:run(Name);
run(Name)->error({native_text_port_missing,Name}).
