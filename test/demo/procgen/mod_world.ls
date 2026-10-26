// The world behind the procgen showcase: everything here is a pure function of one
// seed, built from lambda.procgen (vibe/Lambda_Pkg_Proc_Gen.md §7). Each part draws
// from its own derived key, so turning erosion off changes the land, not the names.
import rng: lambda.procgen.rng
import terrain: lambda.procgen.terrain
import sample: lambda.procgen.sample
import text: lambda.procgen.text
import grid: lambda.procgen.grid
import lsystem: lambda.procgen.lsystem
import geom: lambda.procgen.geom

pub let WIDTH = 120
pub let HEIGHT = 80
let SEA = 0.4

// the name model's training set: invented, place-like words
let PLACES = ["ashford", "brightwater", "caldera", "dunmere", "eldhollow", "fairhaven", "glenmoor", "harrowgate",
    "ironvale", "kestrelby", "lowmarsh", "mistral", "northwick", "oakenshaw", "pellucid", "quarrystone", "ravenmoor",
    "saltmarsh", "thornbury", "umberlee", "vellmont", "westerly", "yarrowdale", "zephyrine", "amberly", "brackwater",
    "coldharbour", "drumlin", "estuar", "fennick"]

let LORE = {origin: "#[realm:#place#]story#", place: "THE REALM",
    story: ["#realm# is #a_land#, where #folk# #deed#.", "Between #feature.s# and #feature.s#, #realm# #endures#.",
            "In #realm#, #folk# still #deed#."],
    a_land: ["a land of #feature.s#", "a patchwork of #feature.s# and #feature.s#", "an old country of #feature.s#"],
    feature: ["river", "marsh", "pine", "ridge", "heath", "moor", "glacier", "orchard", "dune", "fjord"],
    folk: ["shepherds", "salt-traders", "lamplighters", "cartographers", "beekeepers", "ferrymen"],
    deed: ["count the stars", "trade in amber", "tell the old songs", "map every stream", "keep the high passes"],
    endures: ["endures", "waits", "remembers", "keeps its counsel"]}

fn title_case(s) => upper(slice(s, 0, 1)) ++ slice(s, 1, len(s))

fn settles(biome) bool => not contains(["ocean", "beach", "snow", "scorched", "bare"], biome)

pub fn generate(seed, options = {}) map^ {
    let k = rng.key(seed)
    let land = terrain.world(rng.derive(k, "land"), {width: WIDTH, height: HEIGHT, sea_level: SEA, island: 0.55,
        erosion: options.erosion != false, climate: options.climate == true})^
    let sites = sample.poisson(rng.derive(k, "towns"), {width: float(WIDTH), height: float(HEIGHT), radius: 13.0})^
    // Poisson points come out row by row: shuffle so the towns spread over the whole land
    let homes = rng.shuffle(rng.derive(k, "homes"), [for (p in sites where settles(land.biomes[int(p[1])][int(p[0])])) p])
    let model = text.markov(PLACES, 2)^
    let names = [for (i, p in slice(homes, 0, min(7, len(homes)))) title_case(text.name(rng.at(rng.derive(k, "names"), i), model)^)]
    let towns = [for (i, n in names) {name: n, x: homes[i][0], y: homes[i][1], capital: i == 0}]
    let realm = if (len(names) > 0) names[0] else "The Realm"
    let lore = text.expand({*: LORE, place: realm}, rng.derive(k, "lore"))^
    let ruin = if (len(names) > 1) names[len(names) - 1] else "the North"
    let emblem = lsystem.turtle(lsystem.expand("X", {X: [[1, "F[+X][-X]FX"], [1, "F[-X]F[+X]X"], [1, "F[+X]F[-X]X"]], F: "FF"},
        4, rng.derive(k, "emblem"))^, {angle: 24.0, heading: 90.0})^;
    let districts = geom.voronoi(geom.delaunay([for (t in towns) [t.x, t.y]])^,
        {bounds: [0.0, 0.0, float(WIDTH), float(HEIGHT)]})^;
    {seed: seed, land: land, towns: towns, districts: districts, realm: realm, lore: lore, ruin: ruin,
     dungeon: grid.dungeon(rng.derive(k, "ruin"), {width: 44, height: 26, min_size: 7})^, emblem: emblem.segments}
}
