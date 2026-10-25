// D7.2.4v4/S12.1.1v3: import is inert; acquisition is explicit and queries are pure.
import c: .chinese.common
import syllables: .chinese.syllables
import pack: .chinese.pack
import lattice: .chinese.lattice
import lookup: .chinese.lookup

pub let fuzzy_pairs = syllables.fuzzy_pairs

// prepared bytes also support deterministic hosts and fixtures without resource IO.
pub fn from_bytes(manifest,lexicon,abbrev,syllable_text,profile = "zh-Hans") map^ {
    let data = pack.from_bytes(manifest,lexicon,abbrev,profile)^;
    {*:data,syllables:syllables.inventory(syllable_text)}
}

pub pn load(profile = "zh-Hans",options = null) map^ {
    let opts = c.options(options,["directory"])^
    let home = sys.lambda.home#
    let root = home ++ "/package/ime/chinese/data/"
    let installed = [for (name in ["zh-Hans"] where exists(root ++ name ++ "/manifest.json")) name]
    let valid = c.require(profile is string and (opts.directory != null or profile in installed),
        "unknown or unprepared profile: " ++ string(profile) ++ "; installed: " ++ join(installed,", "))^
    let directory = c.get(opts,"directory",root ++ profile)
    let path = c.require(directory is string,"directory must be a string")^
    let manifest = input(directory ++ "/manifest.json","json")^
    let lexicon = input(directory ++ "/lexicon.txt","binary")^
    let abbrev = input(directory ++ "/abbrev.txt","binary")^
    let inventory = input(home ++ "/package/ime/chinese/syllables.txt","text")^;
    return from_bytes(manifest,lexicon,abbrev,inventory,profile)^
}

pub fn candidates(engine,raw,options = null) map^ {
    let opts = lookup.options(options)^
    let graph = lattice.build(engine.syllables,raw,opts.fuzzy)^
    let paths = lattice.paths(graph,opts.max_paths)
    let result = lookup.candidates(engine,paths.paths,opts)^;
    {segments:[for (edges in graph.edges,edge in edges) edge],items:result.items,
        truncated:paths.truncated or result.truncated,scanned:result.scanned}
}
