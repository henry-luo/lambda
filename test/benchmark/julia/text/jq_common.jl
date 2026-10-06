include("../common.jl")
include("jq_vm.jl")

function run_jq_benchmark(name, input_kind, input_path, expected)
    jq_dir = joinpath(@__DIR__, "..", "..", "text", "jq")
    filter = read(joinpath(jq_dir, name[4:end] * ".jq"), String)
    program = JqVM.compile_program(filter)
    @assert !program.failed program.msg
    function prepare()
        if input_kind === :json
            return JqVM.jparse(read(joinpath(jq_dir, input_path), String), :json)
        elseif input_kind === :raw
            return read(joinpath(jq_dir, input_path), String)
        end
        return nothing
    end
    run_benchmark(prepare, (io, data)->JqVM.vm_run(program, data).outputs,
                  outputs->outputs == [expected],
                  outputs->println(name, ": CHECKSUM:", JqVM.jstring(only(outputs))))
end
