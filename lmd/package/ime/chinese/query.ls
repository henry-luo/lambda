import lattice: .lattice
import lookup: .lookup
import learned: .user

// shared query path keeps the reducer and public M1 API on one lattice/lookup kernel.
pub fn candidates(engine,raw,options = null,user = null) map^ {
    let opts = lookup.options(options)^
    let graph = lattice.build(engine.syllables,raw,opts.fuzzy)^
    let paths = lattice.paths(graph,opts.max_paths)
    let base = lookup.candidates(engine,paths.paths,opts)^
    let result = if (user == null or len(user.entries) == 0) base
        else learned.promote(user,paths.paths,base,opts)^;
    {segments:[for (edges in graph.edges,edge in edges) edge],items:result.items,
        truncated:paths.truncated or result.truncated,scanned:result.scanned}
}
