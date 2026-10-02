include("support.jl")
include("splay_core.jl")
function prepare_splay()
    root, rng = nothing, [49734321]
    for _ in 1:TREE_SIZE; root, _ = insert_new_node(root, nothing, rng); end
    return root, rng
end
function verify_splay(root)
    keys = Float64[]
    function traverse(node)
        node === nothing && return
        traverse(node.left); push!(keys, node.key); traverse(node.right)
    end
    traverse(root)
    return length(keys) == TREE_SIZE && all(keys[i] < keys[i+1] for i in 1:length(keys)-1)
end
run_benchmark(prepare_splay, (io, state)->begin
    root, rng = state
    for _ in 1:benchmark_repeats(50)
        for _ in 1:TREE_MODIFICATIONS
            root, key = insert_new_node(root, nothing, rng)
            root, greatest = splay_find_greatest_less_than(root, key)
            root, _ = splay_remove(root, greatest === nothing ? key : greatest.key)
        end
    end
    return root
end, verify_splay, result->nothing)
