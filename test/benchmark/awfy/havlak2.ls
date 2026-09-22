// AWFY Benchmark: Havlak
// Loop recognition using Havlak's algorithm
// Ported from JavaScript AWFY suite

// --- Constants ---
let UNVISITED = 2147483647
let MAXNONBACKPREDS = 32768
let BB_NONHEADER = 1
let BB_REDUCIBLE = 2
let BB_SELF = 3
let BB_IRREDUCIBLE = 4
let BB_DEAD = 5

// Value-semantics port (S9.1.2) over flat typed vectors, as in the JS
// original and the C port: blocks, union-find nodes and loops live in
// `BasicBlock[]`, `UnionFindNode[]` and `SimpleLoop[]` indexed by id, and the
// per-node tables are `int[]`. The JS original mutates shared objects in place;
// here every such mutation is a direct path write into the one vector that owns
// the value, and cross-references hold ids, never copies: a UF node names its
// loop by id, a loop names its children by id, and the node pool and work list
// carry dfn numbers.
type BasicBlock = {bid: int, inEdges: int[], outEdges: int[]}
type ControlFlowGraph = {startNode: BasicBlock?, bbMap: BasicBlock[], numNodes: int}
type SimpleLoop = {lid: int, isRed: int, parentId: int, isRoot: int,
    nestLvl: int, depthLvl: int, header: BasicBlock?, bbs: BasicBlock[],
    children: int[]}
type LoopStructureGraph = {loopCounter: int, loops: SimpleLoop[]}
type UnionFindNode = {dfn: int, parentDfn: int, bb: BasicBlock?, loopId: int}
// an int queue: `items` grows at the back, `first` walks from the front
type IntQueue = {items: int[], first: int}

// =====================================================
// IntQueue: the node pool and the work list
// =====================================================
pn queue_new() IntQueue {
    var q: IntQueue = {items: [], first: 0}
    return q
}

pn queue_has(q: IntQueue, id: int) int {
    var s = len(q.items)
    var i = q.first
    while (i < s) {
        if (q.items[i] == id) { return 1 }
        i = i + 1
    }
    return 0
}

// =====================================================
// BasicBlock / ControlFlowGraph
// =====================================================
pn bb_new(name: int) BasicBlock {
    var b: BasicBlock = {bid: name, inEdges: [], outEdges: []}
    return b
}

pn cfg_new() ControlFlowGraph {
    var c: ControlFlowGraph = {startNode: null, bbMap: [], numNodes: 0}
    return c
}

// block ids are created densely in increasing order, so a new id is always
// the next slot of bbMap
pn cfg_ensure_node(var cfg: ControlFlowGraph, name: int) int {
    while (cfg.numNodes <= name) {
        push(cfg.bbMap, bb_new(cfg.numNodes))
        cfg.numNodes = cfg.numNodes + 1
        if (cfg.numNodes == 1) {
            cfg.startNode = cfg.bbMap[0]
        }
    }
    return 0
}

// Edges retain block ids; bbMap is the sole owner of every block's edge lists.
pn cfg_add_edge(var cfg: ControlFlowGraph, fromName: int, toName: int) int {
    cfg_ensure_node(cfg, fromName)
    cfg_ensure_node(cfg, toName)
    push(cfg.bbMap[fromName].outEdges, toName)
    push(cfg.bbMap[toName].inEdges, fromName)
    return 0
}

pn cfg_get_num_nodes(cfg: ControlFlowGraph) int {
    return cfg.numNodes
}

// =====================================================
// SimpleLoop / LoopStructureGraph: loops live in lsg.loops, by id
// =====================================================
pn loop_new(bb: BasicBlock?, isReducible: int, counter: int) SimpleLoop {
    var l: SimpleLoop = {
        lid: counter, isRed: isReducible, parentId: -1, isRoot: 0,
        nestLvl: 0, depthLvl: 0, header: bb, bbs: [], children: []
    }
    if (bb != null) {
        push(l.bbs, bb)
    }
    return l
}

pn loop_set_parent(var lsg: LoopStructureGraph, lid: int, parentLid: int) int {
    lsg.loops[lid].parentId = parentLid
    push(lsg.loops[parentLid].children, lid)
    return 0
}

pn lsg_new() LoopStructureGraph {
    var l: LoopStructureGraph = {loopCounter: 1, loops: [loop_new(null, 1, 0)]}
    l.loops[0].isRoot = 1
    return l
}

pn lsg_create_new_loop(var lsg: LoopStructureGraph, bb: BasicBlock,
        isReducible: int) int {
    var lc = lsg.loopCounter
    lsg.loopCounter = lc + 1
    push(lsg.loops, loop_new(bb, isReducible, lc))
    return lc
}

pn lsg_get_num_loops(lsg: LoopStructureGraph) int {
    return lsg.loopCounter
}

pn lsg_calc_nesting_rec(var lsg: LoopStructureGraph, lid: int, depth: int) int {
    lsg.loops[lid].depthLvl = depth
    var s = len(lsg.loops[lid].children)
    var i = 0
    while (i < s) {
        var child = lsg.loops[lid].children[i]
        lsg_calc_nesting_rec(lsg, child, depth + 1)
        var cnl1 = lsg.loops[child].nestLvl + 1
        if (cnl1 > lsg.loops[lid].nestLvl) {
            lsg.loops[lid].nestLvl = cnl1
        }
        i = i + 1
    }
    return 0
}

pn lsg_calc_nesting(var lsg: LoopStructureGraph) int {
    var lc = lsg.loopCounter
    var i = 0
    while (i < lc) {
        // Read the live store because loop_set_parent updates lsg.loops.
        if (lsg.loops[i].isRoot == 0) {
            if (lsg.loops[i].parentId == -1) {
                loop_set_parent(lsg, i, 0)
            }
        }
        i = i + 1
    }
    lsg_calc_nesting_rec(lsg, 0, 0)
    return 0
}

// =====================================================
// UnionFindNode: nodes[dfn] is the node numbered dfn
// =====================================================
pn uf_new(dfsNum: int, bb: BasicBlock?) UnionFindNode {
    var n: UnionFindNode = {dfn: dfsNum, parentDfn: dfsNum, bb: bb, loopId: -1}
    return n
}

pn uf_find_set(nodes: UnionFindNode[], nodeId: int) int {
    var pdfn = nodes[nodeId].parentDfn
    if (pdfn == nodeId) { return nodeId }
    var curp = pdfn
    while (nodeId != curp) {
        var pp = curp
        var gpp = nodes[pp].parentDfn
        nodeId = pp
        curp = gpp
    }
    return nodeId
}

// =====================================================
// Havlak Loop Finder
// =====================================================

pn hlf_is_ancestor(hlf_last: int[], w: int, v: int) int {
    if (w > v) { return 0 }
    if (v > hlf_last[w]) { return 0 }
    return 1
}

// add `val` to the set `sets[idx]` unless present
pn set_add_at(var sets: int[][], idx: int, val: int) int {
    var sz = len(sets[idx])
    var i = 0
    while (i < sz) {
        if (sets[idx][i] == val) { return 0 }
        i = i + 1
    }
    push(sets[idx], val)
    return 1
}

// Recursive DFS
pn hlf_do_dfs(var nodes: UnionFindNode[], var num_map: int[], var last_arr: int[],
        cfg: ControlFlowGraph, current_bb: BasicBlock, current: int) int {
    // S9.1.3: DFS carries its three evolving stores through explicit inout
    // borrows; only the scalar last-id needs a return value.
    nodes[current] = uf_new(current, current_bb)
    num_map[current_bb.bid] = current
    var last_id = current
    var edge_count = len(current_bb.outEdges)
    var i = 0
    while (i < edge_count) {
        var target_bid = current_bb.outEdges[i]
        if (num_map[target_bid] == UNVISITED) {
            last_id = hlf_do_dfs(nodes, num_map, last_arr, cfg,
                                 cfg.bbMap[target_bid], last_id + 1)
        }
        i = i + 1
    }
    last_arr[current] = last_id
    return last_id
}

pn hlf_process_edges(numMap: int[], var backPreds: int[][],
        var nonBackPreds: int[][], hlf_last: int[], nodeW: BasicBlock,
        w: int) int {
    var ieSz = len(nodeW.inEdges)
    var i = 0
    while (i < ieSz) {
        var v = numMap[nodeW.inEdges[i]]
        if (v != UNVISITED) {
            var anc = hlf_is_ancestor(hlf_last, w, v)
            if (anc == 1) {
                push(backPreds[w], v)
            }
            if (anc == 0) {
                set_add_at(nonBackPreds, w, v)
            }
        }
        i = i + 1
    }
    return 0
}

pn hlf_step_d(nodes: UnionFindNode[], backPreds: int[][], var hlf_type: int[],
        w: int, var nodePool: IntQueue) int {
    var bpSz = len(backPreds[w])
    var i = 0
    while (i < bpSz) {
        var v = backPreds[w][i]
        if (v != w) {
            push(nodePool.items, uf_find_set(nodes, v))
        }
        if (v == w) {
            hlf_type[w] = BB_SELF
        }
        i = i + 1
    }
    return 0
}

pn hlf_step_e(nodes: UnionFindNode[], var nonBackPreds: int[][], var hlf_type: int[],
        hlf_last: int[], w: int, var nodePool: IntQueue,
        var workList: IntQueue, xdfn: int) int {
    var sz = len(nonBackPreds[xdfn])
    var i = 0
    while (i < sz) {
        var yddfn = uf_find_set(nodes, nonBackPreds[xdfn][i])
        var anc = hlf_is_ancestor(hlf_last, w, yddfn)
        if (anc == 0) {
            hlf_type[w] = BB_IRREDUCIBLE
            set_add_at(nonBackPreds, w, yddfn)
        }
        if (anc == 1) {
            if (yddfn != w) {
                if (queue_has(nodePool, yddfn) == 0) {
                    push(workList.items, yddfn)
                    push(nodePool.items, yddfn)
                }
            }
        }
        i = i + 1
    }
    return 0
}

pn hlf_find_loops(cfg: ControlFlowGraph, var lsg: LoopStructureGraph) int {
    // startNode is a construction-time value; bbMap owns the subsequently
    // rebuilt edge lists, so DFS must reload the current block from that store.
    var size = cfg_get_num_nodes(cfg)
    if (size == 0) { return 0 }

    var nonBackPreds: int[][] = []
    var backPreds: int[][] = []
    var numMap: int[] = fill(size + 100, UNVISITED)
    var hlf_header: int[] = fill(size, 0)
    var hlf_type: int[] = fill(size, BB_NONHEADER)
    var hlf_last: int[] = fill(size, 0)
    var nodes: UnionFindNode[] = []

    // Create UF nodes and per-node structures
    var ni = 0
    while (ni < size) {
        push(nodes, uf_new(0, null))
        push(nonBackPreds, [])
        push(backPreds, [])
        ni = ni + 1
    }

    // DFS
    hlf_do_dfs(nodes, numMap, hlf_last, cfg, cfg.bbMap[0], 0)

    // Identify edges
    var wi = 0
    while (wi < size) {
        var nbb = nodes[wi].bb
        if (nbb == null) {
            hlf_type[wi] = BB_DEAD
        }
        if (nbb != null) {
            hlf_process_edges(numMap, backPreds, nonBackPreds, hlf_last, nbb, wi)
        }
        wi = wi + 1
    }

    // Step c: process in reverse DFS order
    var w = size - 1
    while (w >= 0) {
        var nodePool: IntQueue = queue_new()
        var nodeW = nodes[w].bb
        if (nodeW != null) {
            hlf_step_d(nodes, backPreds, hlf_type, w, nodePool)
            var workList: IntQueue = {items: [], first: 0}
            var nps = len(nodePool.items)
            var cpi = 0
            while (cpi < nps) {
                push(workList.items, nodePool.items[cpi])
                cpi = cpi + 1
            }
            if (nps != 0) {
                hlf_type[w] = BB_REDUCIBLE
            }
            while (workList.first < len(workList.items)) {
                var xdfn = workList.items[workList.first]
                workList.first = workList.first + 1
                if (len(nonBackPreds[xdfn]) > MAXNONBACKPREDS) {
                    return 0
                }
                hlf_step_e(nodes, nonBackPreds, hlf_type, hlf_last, w, nodePool, workList, xdfn)
            }
            var npSz2 = len(nodePool.items)
            var wtype = hlf_type[w]
            var found_lid = -1
            if (npSz2 > 0) {
                var isRed = 1
                if (wtype == BB_IRREDUCIBLE) {
                    isRed = 0
                }
                found_lid = lsg_create_new_loop(lsg, nodeW, isRed)
            }
            if (npSz2 == 0) {
                if (wtype == BB_SELF) {
                    found_lid = lsg_create_new_loop(lsg, nodeW, 1)
                }
            }
            if (found_lid != -1) {
                nodes[w].loopId = found_lid
                var found_i = 0
                while (found_i < npSz2) {
                    var found_dfn = nodePool.items[found_i]
                    hlf_header[found_dfn] = w
                    nodes[found_dfn].parentDfn = w
                    var parent_lid = nodes[found_dfn].loopId
                    if (parent_lid != -1) {
                        loop_set_parent(lsg, parent_lid, found_lid)
                    }
                    if (parent_lid == -1) {
                        var found_bb = nodes[found_dfn].bb
                        if (found_bb != null) {
                            var live_bb: BasicBlock = found_bb
                            push(lsg.loops[found_lid].bbs, live_bb)
                        }
                    }
                    found_i = found_i + 1
                }
            }
        }
        w = w - 1
    }
    return 1
}

// =====================================================
// LoopTesterApp
// =====================================================

pn build_diamond(var cfg: ControlFlowGraph, start: int) int {
    var bb0 = start
    var bb1 = bb0 + 1
    var bb2 = bb0 + 2
    var bb3 = bb0 + 3
    cfg_add_edge(cfg, bb0, bb1)
    cfg_add_edge(cfg, bb0, bb2)
    cfg_add_edge(cfg, bb1, bb3)
    cfg_add_edge(cfg, bb2, bb3)
    return bb3
}

pn build_connect(var cfg: ControlFlowGraph, start: int, end: int) int {
    cfg_add_edge(cfg, start, end)
    return 0
}

pn build_straight(var cfg: ControlFlowGraph, start: int, n: int) int {
    var i = 0
    while (i < n) {
        var s1 = start + i
        var s2 = s1 + 1
        build_connect(cfg, s1, s2)
        i = i + 1
    }
    var r = start + n
    return r
}

pn build_base_loop(var cfg: ControlFlowGraph, from: int) int {
    var header = build_straight(cfg, from, 1)
    var diamond1 = build_diamond(cfg, header)
    var d11 = build_straight(cfg, diamond1, 1)
    var diamond2 = build_diamond(cfg, d11)
    var footer = build_straight(cfg, diamond2, 1)
    build_connect(cfg, diamond2, d11)
    build_connect(cfg, diamond1, header)
    build_connect(cfg, footer, from)
    footer = build_straight(cfg, footer, 1)
    return footer
}

pn construct_simple_cfg(var cfg: ControlFlowGraph) int {
    cfg_ensure_node(cfg, 0)
    build_base_loop(cfg, 0)
    cfg_ensure_node(cfg, 1)
    cfg_add_edge(cfg, 0, 2)
    return 0
}

pn find_loops(cfg: ControlFlowGraph, var lsg: LoopStructureGraph) int {
    hlf_find_loops(cfg, lsg)
    return 0
}

pn add_dummy_loops(cfg: ControlFlowGraph, var lsg: LoopStructureGraph,
        numDummyLoops: int) int {
    var i = 0
    while (i < numDummyLoops) {
        find_loops(cfg, lsg)
        i = i + 1
    }
    return 0
}

pn construct_cfg(var cfg: ControlFlowGraph, parLoops: int, pparLoops: int,
        ppparLoops: int) int {
    var n = 2
    var pl = 0
    while (pl < parLoops) {
        var n1 = n + 1
        cfg_ensure_node(cfg, n1)
        build_connect(cfg, 2, n1)
        n = n1
        var i = 0
        while (i < pparLoops) {
            var top = n
            n = build_straight(cfg, n, 1)
            var j = 0
            while (j < ppparLoops) {
                n = build_base_loop(cfg, n)
                j = j + 1
            }
            var bottom = build_straight(cfg, n, 1)
            build_connect(cfg, n, top)
            n = bottom
            i = i + 1
        }
        build_connect(cfg, n, 1)
        pl = pl + 1
    }
    return 0
}

pn lta_main(numDummyLoops: int, findLoopIterations: int, parLoops: int,
        pparLoops: int, ppparLoops: int) int {
    var cfg: ControlFlowGraph = cfg_new()
    cfg_ensure_node(cfg, 0)
    construct_simple_cfg(cfg)
    var lsg: LoopStructureGraph = lsg_new()
    add_dummy_loops(cfg, lsg, numDummyLoops)
    construct_cfg(cfg, parLoops, pparLoops, ppparLoops)
    find_loops(cfg, lsg)
    var i = 0
    while (i < findLoopIterations) {
        var newLsg: LoopStructureGraph = lsg_new()
        find_loops(cfg, newLsg)
        i = i + 1
    }
    lsg_calc_nesting(lsg)
    var numLoops = lsg_get_num_loops(lsg)
    var numNodes = cfg_get_num_nodes(cfg)
    // Pack both into a single int: loops * 100000 + nodes
    var result = numLoops * 100000 + numNodes
    return result
}

pn verify_result(result: int, innerIterations: int) int {
    var remainder = result % 100000
    var lcount = 0
    var temp = result - remainder
    while (temp > 0) {
        lcount = lcount + 1
        temp = temp - 100000
    }
    if (innerIterations == 1) {
        if (lcount == 1605) {
            if (remainder == 5213) {
                return 1
            }
        }
    }
    if (innerIterations == 15) {
        if (lcount == 1647) {
            if (remainder == 5213) {
                return 1
            }
        }
    }
    print("Unexpected: loops=")
    print(lcount)
    print(" nodes=")
    print(remainder)
    print(" iters=")
    print(innerIterations)
    print("\n")
    return 0
}

pn main() int {
    var __t0 = clock()
    var result = lta_main(1, 1, 10, 10, 5)
    var __t1 = clock()
    var ok = verify_result(result, 1)
    if (ok == 1) {
        print("Havlak: PASS\n")
    }
    if (ok == 0) {
        print("Havlak: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
    // return the loops*100000+nodes figure verify_result checks, so the
    // echoed main result is a real assertion in the golden, not a constant 0
    return result
}
