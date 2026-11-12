%% Native sets and regular expressions for the shared ASCII prose fixture.
-module(spelling).
-export([run/0]).

decode_group(Group, Words) ->
    [First | Deltas] = binary:split(Group, <<";">>, [global]),
    {_, Result} = lists:foldl(fun(Token, {Previous, Acc}) ->
        Next = case binary:split(Token, <<",">>) of
            [Suffix] -> <<Previous/binary, Suffix/binary>>;
            [Count, Suffix] ->
                Prefix = lists:sublist(unicode:characters_to_list(Previous), binary_to_integer(Count)),
                <<(unicode:characters_to_binary(Prefix))/binary, Suffix/binary>>
        end,
        {Next, Acc#{Next => true}}
    end, {First, Words#{First => true}}, Deltas),
    Result.

load_words() ->
    {ok, Source} = file:read_file("lmd/package/doc/spelling/data/en-US.dict"),
    Lines = binary:split(Source, <<"\n">>, [global, trim_all]),
    lists:foldl(fun(Line, Acc) ->
        lists:foldl(fun decode_group/2, Acc, binary:split(Line, <<"|">>, [global]))
    end, #{<<"Lambda">> => true, <<"Radiant">> => true}, Lines).

prepare() ->
    {ok, Text} = file:read_file("test/benchmark/text/spelling_document.txt"),
    {ok, Expected} = file:read_file("test/benchmark/text/spelling_expected.json"),
    {load_words(), Text, json:decode(Expected)}.

work({Words, Text, Expected}) ->
    {match, Matches} = re:run(Text, <<"[A-Za-z]+(?:'[A-Za-z]+)*">>, [global, {capture, first, index}]),
    Issues = lists:filtermap(fun([{Start, Length}]) ->
        Word = binary:part(Text, Start, Length),
        Lower = string:lowercase(Word),
        <<First, Rest/binary>> = Lower,
        Title = <<(hd(string:uppercase([First]))), Rest/binary>>,
        Folded = Word =:= string:uppercase(Word) orelse Word =:= Title,
        case maps:is_key(Word, Words) orelse (Folded andalso maps:is_key(Lower, Words)) of
            true -> false;
            false -> {true, #{<<"word">> => Word, <<"start">> => Start, <<"end">> => Start + Length}}
        end
    end, Matches),
    length(Matches) =:= 7110 andalso Issues =:= Expected.

run() -> native_bench:prepared(fun prepare/0, fun work/1,
    fun native_bench:check/1,
    fun(_) -> io:format("spelling: CHECKED:7110 ISSUES:720~n") end, fun(_) -> ok end).
