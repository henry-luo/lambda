%% Harness only: direct functions own their arguments, data and algorithm state.
-module(native_bench).
-export([main/0,run/4,check/1,warmup/0,prepared/5]).
check(true)->ok;
check(false)->error(benchmark_verification_failed).
warmup()->os:getenv("NATIVE_BENCH_WARMUP")=/="0".
run(Work,Verify,Report,Warm)->
    case Warm of true->Verify(Work());false->ok end,
    Started=erlang:monotonic_time(nanosecond),
    Result=Work(),
    Elapsed=(erlang:monotonic_time(nanosecond)-Started)/1.0e6,
    Verify(Result),Report(Result),io:format("__TIMING__:~p~n",[Elapsed]),Result.
prepared(Prepare,Work,Verify,Report,Cleanup)->
    case warmup() of true->S=Prepare(),try Verify(Work(S))after Cleanup(S)end;false->ok end,
    State=Prepare(),try run(fun()->Work(State)end,Verify,Report,false)after Cleanup(State)end.
main()->
    try
        [Entry|Args]=init:get_plain_arguments(),
        [Suite,Name]=string:split(Entry,"/"),
        case Suite of
            "r7rs"->r7rs:run(Name);
            "julia"->micro:run(Name);
            "kostya"->kostya:run(Name);
            "beng"->beng:run(Name);
            "larceny"->larceny:run(Name);
            "jetstream"->[Repeats]=Args,jetstream:run(Name,list_to_integer(Repeats));
            "text"->text_bench:run(Name);
            "awfy"->[Inner,Outer]=Args,awfy:run(Name,list_to_integer(Inner),list_to_integer(Outer));
            _->error({native_port_not_implemented,Entry,Args})
        end,
        halt(0)
    catch Class:Reason:Stack->io:format(standard_error,"~p:~p~n~p~n",[Class,Reason,Stack]),halt(1)
    end.
