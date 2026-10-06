using Test
include("text/jq_vm.jl")

function evaluate(filter, input=nothing)
    program = JqVM.compile_program(filter)
    program.failed && error(program.msg)
    return JqVM.vm_run(program, input).outputs
end

@testset "jq generators, scopes and backtracking" begin
    @test evaluate("[range(0; 8; 2)]") == [[0, 2, 4, 6]]
    @test evaluate("[(1,2) + (10,20)]") == [[11, 12, 21, 22]]
    @test evaluate("[limit(4; 0 | recurse(.+1))]") == [[0, 1, 2, 3]]
    @test evaluate("[limit(0; repeat(1))]") == [Any[]]
    @test evaluate("nth(5; 0 | recurse(.+1))") == [5]
    @test evaluate("reduce range(6) as \$x (0; . + \$x)") == [15]
    @test evaluate("[foreach range(4) as \$x (0; . + \$x; \$x, .)]") == [[0,0,1,1,2,3,3,6]]
    @test evaluate("def outer(\$x): def inner(f): f + \$x; inner(. * 2); 3 | outer(7)") == [13]
    @test evaluate("def down(\$n): if \$n == 0 then 0 else down(\$n-1) end; down(1000)") == [0]
    @test evaluate("[label \$out | range(6) | if . == 3 then break \$out else . end]") == [[0,1,2]]
    @test evaluate("[try (1, error(\"caught\")) catch .]") == [[1, "caught"]]
    @test evaluate("[try (1,2) catch 9 | if . == 2 then try error(\"outer\") catch . else . end]") == [[1, "outer"]]
    @test evaluate("[false, null, 0, \"\", [] | . // 7]") == [[7,7,0,"",Any[]]]
    @test evaluate("[0, false, null | not]") == [[false, true, true]]
    @test_throws ErrorException evaluate("error(\"uncaught\")")
    @test_throws ErrorException evaluate("missing_function(1)")
    @test_throws ErrorException evaluate("[1,")
end

@testset "jq paths preserve input snapshots" begin
    input = Dict{String,Any}("a" => Any[1,2], "b" => Dict{String,Any}("qty" => 3))
    before = deepcopy(input)
    @test evaluate(".a[1] += 5 | .a", input) == [[1,7]]
    @test input == before
    @test evaluate("( .. | numbers ) |= . * 2", input) == [Dict("a"=>[2,4], "b"=>Dict("qty"=>6))]
    @test input == before
    @test evaluate("setpath([\"a\", 3]; 9) | .a", input) == [Any[1,2,nothing,9]]
    @test evaluate("del(.a[0]) | .a", input) == [[2]]
    @test evaluate("[paths(type == \"number\")] | sort", input) == [[Any["a",0], Any["a",1], Any["b","qty"]]]
    @test evaluate("{} | .a //= 3 | .a += 1 | .a") == [4]
    @test evaluate("[1,2,3,4] | .[-3:-1]") == [[2,3]]
    @test evaluate("[[],[1,[2,3]],4] | flatten") == [[1,2,3,4]]
end

@testset "jq native values and JSON" begin
    @test evaluate("[null,false,true,0,\"a\",[],{}] | reverse | sort") == [Any[nothing,false,true,0,"a",Any[],Dict()]]
    @test evaluate("[3,1,3,2] | unique") == [[1,2,3]]
    @test evaluate("[3,1,2,4] | group_by(. % 2)") == [[[2,4],[3,1]]]
    @test evaluate("[true,false] | contains([true])") == [true]
    @test evaluate("[0] | contains([false])") == [false]
    @test evaluate("\"hél😀\" | explode | implode") == ["hél😀"]
    @test evaluate("\"hél😀\" | .[1:3]") == ["él"]
    @test evaluate("\"a,b,c\" | split(\",\") | join(\"/\")") == ["a/b/c"]
    @test evaluate("[1,2,3] | tojson | fromjson") == [[1,2,3]]
    @test evaluate("{jq_parse_failed:true} | tojson | fromjson") == [Dict("jq_parse_failed"=>true)]
    @test evaluate("[try (\"bad\" | fromjson) catch 0]") == [[0]]
    @test evaluate("[\"42\", \"1.5\" | tonumber]") == [[42,1.5]]
    @test evaluate("try (\"true\" | tonumber) catch 0") == [0]
    @test evaluate(raw"""{x:12} | "id=\(.x)" """) == ["id=12"]
end

@testset "frame compaction retains closures" begin
    program = JqVM.compile_program("def outer(\$x): def f: . + \$x; reduce range(70000) as \$n (0; . + (\$n | f)); outer(19)")
    @test !program.failed
    vm = JqVM.vm_run(program, nothing)
    @test vm.outputs == [sum(0:69999) + 70000 * 19]
    @test vm.nframes < 50000
    # Reusing bytecode must start with independent frame, fork and value state.
    @test JqVM.vm_run(program, nothing).outputs == vm.outputs
end
