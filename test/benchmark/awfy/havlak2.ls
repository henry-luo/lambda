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

type Arr = {l0: array, sz: int}
type IntArr = {l0: array}
type BigVec = {data: array, first: int}
type IntSet = {items: int[]}
type BasicBlock = {bid: int, inEdges: int[], outEdges: int[]}
type ControlFlowGraph = {startNode: BasicBlock?, bbMap: Arr, numNodes: int}
type SimpleLoop = {lid: int, isRed: int, parentId: int, isRoot: int,
    nestLvl: int, depthLvl: int, header: BasicBlock?, bbs: BigVec,
    children: BigVec}
type LoopStructureGraph = {loopCounter: int, loops: Arr, root: SimpleLoop}
type UnionFindNode = {dfn: int, parentDfn: int, bb: BasicBlock?, loop: SimpleLoop?}

// =====================================================
// Helpers
// =====================================================
pn null16() array {
    var a = fill(16, null)
    return a
}

pn null32() array {
    var a = fill(32, null)
    return a
}

pn int32() int[] {
    var a = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
    return a
}

// =====================================================
// 3-level indexed array (arr): 16 x 16 x 32 = 8192 cap
// For sparse/absolute-index access
// =====================================================
pn arr_new() Arr {
    var a: Arr = {l0: null16(), sz: 0}
    return a
}

pn arr_get(a: Arr, idx: int) any {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn arr_set(var a: Arr, idx: int, val: any) int {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null16()
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = null32()
    }
    var _d3 = 0
    c2[i2] = val
    c1[i1] = c2
    l0[i0] = c1
    a.l0 = l0
    return 0
}

// Integer array (defaults to 0)
pn iarr_new() IntArr {
    var a: IntArr = {l0: null16()}
    return a
}

pn iarr_get(a: IntArr, idx: int) int {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) { return 0 }
    var c2 = c1[i1]
    if (c2 == null) { return 0 }
    var r = c2[i2]
    return r
}

pn iarr_set(var a: IntArr, idx: int, val: int) int {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null16()
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = int32()
    }
    var _d3 = 0
    c2[i2] = val
    c1[i1] = c2
    l0[i0] = c1
    a.l0 = l0
    return 0
}

// =====================================================
// Big vector (bvec): sequential append/remove, 8192 cap
// Uses 3-level arr internally plus first/sz tracking
// =====================================================
pn bvec_new() BigVec {
    var v: BigVec = {data: [], first: 0}
    return v
}

pn bvec_add(var v: BigVec, item: any) int {
    push(v.data, item)
    return 0
}

pn bvec_raw_get(v: BigVec, idx: int) any {
    return (v.data)[idx]
}

pn bvec_at(v: BigVec, idx: int) any {
    var f = (v.first)
    return (v.data)[f + idx]
}

pn bvec_size(v: BigVec) int {
    return len(v.data) - (v.first)
}

pn bvec_remove_first(var v: BigVec) any {
    var f = (v.first)
    if (f >= len(v.data)) { return null }
    var r = (v.data)[f]
    v.first = f + 1
    return r
}

pn bvec_is_empty(v: BigVec) int {
    if ((v.first) >= len(v.data)) { return 1 }
    return 0
}

// Check if bvec contains element with given dfn field
pn bvec_has_dfn(v: BigVec, id: int) int {
    var f = (v.first)
    var s = len(v.data)
    for i in f to s - 1 {
        var elem = (v.data)[i]
        if (elem != null) {
            var eid = (elem.dfn)
            if (eid == id) { return 1 }
        }
    }
    return 0
}

// =====================================================
// Small vector (vec): 16x16=256, for per-node small lists
// =====================================================
pn vec_new() array {
    return []
}

pn vec_add(var v: array, item: any) int {
    push(v, item)
    return 0
}

pn vec_at(v: array, idx: int) any {
    return v[idx]
}

pn vec_size(v: array) int {
    return len(v)
}

// =====================================================
// Integer Set (iset): for nonBackPreds - set of ints
// =====================================================
pn iset_new() IntSet {
    var s: IntSet = {items: vec_new()}
    return s
}

pn iset_add(var s: IntSet, val: int) int {
    var items: array = s.items
    var sz = len(items)
    var i = 0
    while (i < sz) {
        var elem = vec_at(items, i)
        if (elem == val) { return 0 }
        i = i + 1
    }
    vec_add(items, val)
    s.items = items
    return 1
}

pn iset_size(s: IntSet) int {
    var items = (s.items)
    var r = vec_size(items)
    return r
}

// =====================================================
// BasicBlock
// =====================================================
pn bb_new(name: int) BasicBlock {
    var ie = vec_new()
    var oe = vec_new()
    var b: BasicBlock = {bid: name, inEdges: ie, outEdges: oe}
    return b
}

// =====================================================
// ControlFlowGraph
// =====================================================
pn cfg_new() ControlFlowGraph {
    var bbm = arr_new()
    var c: ControlFlowGraph = {startNode: null, bbMap: bbm, numNodes: 0}
    return c
}

pn cfg_create_node(var cfg: ControlFlowGraph, name: int) BasicBlock {
    var bbm = cfg.bbMap
    var node = arr_get(bbm, name)
    if (node == null) {
        node = bb_new(name)
        arr_set(bbm, name, node)
        // S9.1.2: bbm is a value copy, so reinstall its detached root.
        cfg.bbMap = bbm
        var nn = (cfg.numNodes) + 1
        cfg.numNodes = nn
    }
    var nn2 = (cfg.numNodes)
    if (nn2 == 1) {
        var _d = 0
        cfg.startNode = node
    }
    return node
}

pn cfg_add_edge(var cfg: ControlFlowGraph, fromName: int, toName: int) int {
    var fromNode: BasicBlock = cfg_create_node(cfg, fromName)
    var toNode: BasicBlock = cfg_create_node(cfg, toName)
    if (fromName == toName) {
        var self_out: array = fromNode.outEdges
        var self_in: array = fromNode.inEdges
        vec_add(self_out, fromName)
        vec_add(self_in, fromName)
        fromNode.outEdges = self_out
        fromNode.inEdges = self_in
        arr_set(cfg.bbMap, fromName, fromNode)
        return 0
    }
    // Edges retain block ids, never a copied block value; bbMap is the sole
    // owner of a block's evolving in/out-edge lists (D3.3.1v2).
    var from_out: array = fromNode.outEdges
    vec_add(from_out, toName)
    fromNode.outEdges = from_out
    arr_set(cfg.bbMap, fromName, fromNode)
    var to_in: array = toNode.inEdges
    vec_add(to_in, fromName)
    toNode.inEdges = to_in
    arr_set(cfg.bbMap, toName, toNode)
    return 0
}

pn cfg_get_num_nodes(cfg: ControlFlowGraph) int {
    var r = (cfg.numNodes)
    return r
}

// =====================================================
// SimpleLoop
// =====================================================
pn loop_new(bb: BasicBlock?, isReducible: int, counter: int) SimpleLoop {
    var bbs = bvec_new()
    var chs = bvec_new()
    var l: SimpleLoop = {
        lid: counter, isRed: isReducible, parentId: -1, isRoot: 0,
        nestLvl: 0, depthLvl: 0, header: bb, bbs: bbs, children: chs
    }
    if (bb != null) {
        var _d = 0
        bvec_add(bbs, bb)
        // S9.1.2: l captured the pre-mutation bbs value above.
        l.bbs = bbs
    }
    return l
}

pn loop_add_node(var lsg: LoopStructureGraph, var loop: SimpleLoop,
        bb: BasicBlock) int {
    bvec_add(loop.bbs, bb)
    arr_set(lsg.loops, loop.lid, loop)
    return 0
}

pn loop_set_parent(var lsg: LoopStructureGraph, var loop: SimpleLoop,
        var parent: SimpleLoop) int {
    var pid = parent.lid
    loop.parentId = pid
    bvec_add(parent.children, loop)
    arr_set(lsg.loops, loop.lid, loop)
    arr_set(lsg.loops, parent.lid, parent)
    if (parent.isRoot == 1) {
        lsg.root = parent
    }
    return 0
}

// =====================================================
// LoopStructureGraph
// =====================================================
pn lsg_new() LoopStructureGraph {
    var loops = arr_new()
    var root = loop_new(null, 1, 0)
    root.nestLvl = 0
    root.isRoot = 1
    arr_set(loops, 0, root)
    var l: LoopStructureGraph = {loopCounter: 1, loops: loops, root: root}
    return l
}

pn lsg_create_new_loop(var lsg: LoopStructureGraph, bb: BasicBlock,
        isReducible: int) SimpleLoop {
    var lc = (lsg.loopCounter)
    var loop = loop_new(bb, isReducible, lc)
    var nlc = lc + 1
    lsg.loopCounter = nlc
    arr_set(lsg.loops, lc, loop)
    return loop
}

pn lsg_get_num_loops(lsg: LoopStructureGraph) int {
    var r = (lsg.loopCounter)
    return r
}

pn lsg_calc_nesting_rec(var lsg: LoopStructureGraph, var loop: SimpleLoop,
        depth: int) int {
    loop.depthLvl = depth
    var chs = (loop.children)
    var f = (chs.first)
    var s = len(chs.data)
    var i = f
    while (i < s) {
        var child = bvec_raw_get(chs, i)
        var nd = depth + 1
        lsg_calc_nesting_rec(lsg, child, nd)
        var cnl = (child.nestLvl)
        var cnl1 = cnl + 1
        var lnl = (loop.nestLvl)
        if (cnl1 > lnl) {
            var _d = 0
            loop.nestLvl = cnl1
        }
        i = i + 1
    }
    arr_set(lsg.loops, loop.lid, loop)
    if (loop.isRoot == 1) {
        lsg.root = loop
    }
    return 0
}

pn lsg_calc_nesting(var lsg: LoopStructureGraph) int {
    var lc = lsg.loopCounter
    var i = 0
    while (i < lc) {
        // Read the live store because loop_set_parent updates lsg.loops.
        var stored_loop = arr_get(lsg.loops, i)
        if (stored_loop != null) {
            var l: SimpleLoop = stored_loop
            var ir = (l.isRoot)
            if (ir == 0) {
                var pid = (l.parentId)
                if (pid == -1) {
                    var root: SimpleLoop = arr_get(lsg.loops, 0)
                    loop_set_parent(lsg, l, root)
                }
            }
        }
        i = i + 1
    }
    var root2: SimpleLoop = arr_get(lsg.loops, 0)
    lsg_calc_nesting_rec(lsg, root2, 0)
    return 0
}

// =====================================================
// UnionFindNode
// =====================================================
pn uf_new() UnionFindNode {
    var n: UnionFindNode = {dfn: 0, parentDfn: 0, bb: null, loop: null}
    return n
}

pn uf_init(var node: UnionFindNode, bb: BasicBlock, dfsNum: int) int {
    node.dfn = dfsNum
    node.parentDfn = dfsNum
    node.bb = bb
    node.loop = null
    return 0
}

pn uf_find_set(nodes: Arr, nodeId: int) int {
    var node = arr_get(nodes, nodeId)
    var pdfn = (node.parentDfn)
    if (pdfn == nodeId) { return nodeId }
    var curp = pdfn
    while (nodeId != curp) {
        var pp = curp
        var gpNode = arr_get(nodes, pp)
        var gpp = (gpNode.parentDfn)
        nodeId = pp
        curp = gpp
    }
    return nodeId
}

// =====================================================
// Havlak Loop Finder
// =====================================================

pn hlf_is_ancestor(hlf_last: IntArr, w: int, v: int) int {
    if (w > v) { return 0 }
    var lw = iarr_get(hlf_last, w)
    if (v > lw) { return 0 }
    return 1
}

// Recursive DFS
pn hlf_do_dfs(var nodes: Arr, var num_map: IntArr, var last_arr: IntArr,
        cfg: ControlFlowGraph, current_bb: BasicBlock, current: int) int {
    // S9.1.3: DFS carries its three evolving stores through explicit inout
    // borrows; only the scalar last-id needs a return value.
    // Replace the placeholder outright; every UF field is initialized here.
    var ufn: UnionFindNode = {
        dfn: current, parentDfn: current, bb: current_bb, loop: null
    }
    arr_set(nodes, current, ufn)
    var bid = current_bb.bid
    iarr_set(num_map, bid, current)
    var last_id = current
    var out_edges = current_bb.outEdges
    var edge_count = vec_size(out_edges)
    var i = 0
    while (i < edge_count) {
        var target_bid = vec_at(out_edges, i)
        var target_num = iarr_get(num_map, target_bid)
        if (target_num == UNVISITED) {
            var target = arr_get(cfg.bbMap, target_bid)
            last_id = hlf_do_dfs(nodes, num_map, last_arr, cfg,
                                 target, last_id + 1)
        }
        i = i + 1
    }
    iarr_set(last_arr, current, last_id)
    return last_id
}

pn hlf_process_edges(nodes: Arr, numMap: IntArr, var backPreds: Arr,
        var nonBackPreds: Arr, hlf_last: IntArr, nodeW: BasicBlock,
        w: int) int {
    var ie = (nodeW.inEdges)
    var ieSz = vec_size(ie)
    var i = 0
    while (i < ieSz) {
        var vbid = vec_at(ie, i)
        var v = iarr_get(numMap, vbid)
        if (v != UNVISITED) {
            var anc = hlf_is_ancestor(hlf_last, w, v)
            if (anc == 1) {
                var bp: array = arr_get(backPreds, w)
                vec_add(bp, v)
                arr_set(backPreds, w, bp)
            }
            if (anc == 0) {
                var nbp: IntSet = arr_get(nonBackPreds, w)
                iset_add(nbp, v)
                arr_set(nonBackPreds, w, nbp)
            }
        }
        i = i + 1
    }
    return 0
}

pn hlf_step_d(nodes: Arr, backPreds: Arr, var hlf_type: IntArr, w: int,
        var nodePool: BigVec) int {
    var bp: array = arr_get(backPreds, w)
    var bpSz = vec_size(bp)
    var i = 0
    while (i < bpSz) {
        var v = vec_at(bp, i)
        if (v != w) {
            var fsId = uf_find_set(nodes, v)
            var fsNode = arr_get(nodes, fsId)
            bvec_add(nodePool, fsNode)
        }
        if (v == w) {
            iarr_set(hlf_type, w, BB_SELF)
        }
        i = i + 1
    }
    return 0
}

pn hlf_step_e(nodes: Arr, var nonBackPreds: Arr, var hlf_type: IntArr,
        hlf_last: IntArr, w: int, var nodePool: BigVec,
        var workList: BigVec, x: UnionFindNode) int {
    var xdfn = (x.dfn)
    var nbp: IntSet = arr_get(nonBackPreds, xdfn)
    var items: array = nbp.items
    var sz = len(items)
    var i = 0
    while (i < sz) {
        var iter = vec_at(items, i)
        var ydashId = uf_find_set(nodes, iter)
        var ydash = arr_get(nodes, ydashId)
        var yddfn = (ydash.dfn)
        var anc = hlf_is_ancestor(hlf_last, w, yddfn)
        if (anc == 0) {
            iarr_set(hlf_type, w, BB_IRREDUCIBLE)
            var wnbp: IntSet = arr_get(nonBackPreds, w)
            iset_add(wnbp, yddfn)
            arr_set(nonBackPreds, w, wnbp)
        }
        if (anc == 1) {
            if (yddfn != w) {
                var hasIt = bvec_has_dfn(nodePool, yddfn)
                if (hasIt == 0) {
                    bvec_add(workList, ydash)
                    bvec_add(nodePool, ydash)
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
    var sn = arr_get(cfg.bbMap, 0)
    if (sn == null) { return 0 }
    var size = cfg_get_num_nodes(cfg)

    var nonBackPreds: Arr = arr_new()
    var backPreds: Arr = arr_new()
    var numMap: IntArr = iarr_new()
    var hlf_header: IntArr = iarr_new()
    var hlf_type: IntArr = iarr_new()
    var hlf_last: IntArr = iarr_new()
    var nodes: Arr = arr_new()

    // Initialize numMap to UNVISITED
    var maxBid = size + 100
    var mi = 0
    while (mi < maxBid) {
        iarr_set(numMap, mi, UNVISITED)
        mi = mi + 1
    }

    // Create UF nodes and per-node structures
    var ni = 0
    while (ni < size) {
        var ufn = uf_new()
        arr_set(nodes, ni, ufn)
        var nbpSet = iset_new()
        arr_set(nonBackPreds, ni, nbpSet)
        var bpVec = vec_new()
        arr_set(backPreds, ni, bpVec)
        ni = ni + 1
    }

    // DFS
    var startBB = arr_get(cfg.bbMap, 0)
    var dfs_last = hlf_do_dfs(nodes, numMap, hlf_last, cfg, startBB, 0)

    // Identify edges
    var wi = 0
    while (wi < size) {
        iarr_set(hlf_header, wi, 0)
        iarr_set(hlf_type, wi, BB_NONHEADER)
        var ufNode = arr_get(nodes, wi)
        var nbb = ufNode.bb
        if (nbb == null) {
            var _d = 0
            iarr_set(hlf_type, wi, BB_DEAD)
        } else {
            var _d2 = 0
            hlf_process_edges(nodes, numMap, backPreds, nonBackPreds, hlf_last, nbb, wi)
        }
        wi = wi + 1
    }

    // Header[0] = 0
    iarr_set(hlf_header, 0, 0)

    // Step c: process in reverse DFS order
    var w = size - 1
    while (w >= 0) {
        var nodePool: BigVec = bvec_new()
        var wNode = arr_get(nodes, w)
        var nodeW = (wNode.bb)
        if (nodeW != null) {
            hlf_step_d(nodes, backPreds, hlf_type, w, nodePool)
            var workList: BigVec = bvec_new()
            var npf = (nodePool.first)
            var nps = len(nodePool.data)
            var cpi = npf
            while (cpi < nps) {
                var cpn = bvec_raw_get(nodePool, cpi)
                bvec_add(workList, cpn)
                cpi = cpi + 1
            }
            var npSz = bvec_size(nodePool)
            if (npSz != 0) {
                var _d3 = 0
                iarr_set(hlf_type, w, BB_REDUCIBLE)
            }
            while (bvec_is_empty(workList) == 0) {
                var x = bvec_remove_first(workList)
                var xdfn = (x.dfn)
                var nbpSet2 = arr_get(nonBackPreds, xdfn)
                var nbpSz = iset_size(nbpSet2)
                if (nbpSz > MAXNONBACKPREDS) {
                    return 0
                }
                hlf_step_e(nodes, nonBackPreds, hlf_type, hlf_last, w, nodePool, workList, x)
            }
            var npSz2 = bvec_size(nodePool)
            var wtype = iarr_get(hlf_type, w)
            var found_loop = null
            if (npSz2 > 0) {
                var isRed = 1
                if (wtype == BB_IRREDUCIBLE) {
                    var _d4 = 0
                    isRed = 0
                }
                found_loop = lsg_create_new_loop(lsg, nodeW, isRed)
            }
            if (npSz2 == 0) {
                if (wtype == BB_SELF) {
                    found_loop = lsg_create_new_loop(lsg, nodeW, 1)
                }
            }
            if (found_loop != null) {
                var live_loop: SimpleLoop = found_loop
                // Keep the evolving UF/header/LSG stores in this activation;
                // nested multi-root var write-back can otherwise reinstall a
                // stale DFS store after the first recognized loop.
                var found_w_node = arr_get(nodes, w)
                found_w_node.loop = live_loop
                arr_set(nodes, w, found_w_node)
                var found_i = nodePool.first
                var found_size = len(nodePool.data)
                while (found_i < found_size) {
                    var found_node = bvec_raw_get(nodePool, found_i)
                    var found_dfn = found_node.dfn
                    iarr_set(hlf_header, found_dfn, w)
                    found_node.parentDfn = w
                    arr_set(nodes, found_dfn, found_node)
                    var found_parent = found_node.loop
                    if (found_parent != null) {
                        var parent_loop: SimpleLoop = found_parent
                        loop_set_parent(lsg, parent_loop, live_loop)
                    }
                    if (found_parent == null) {
                        var found_bb = found_node.bb
                        if (found_bb != null) {
                            var live_bb: BasicBlock = found_bb
                            loop_add_node(lsg, live_loop, live_bb)
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
    cfg_create_node(cfg, 0)
    build_base_loop(cfg, 0)
    cfg_create_node(cfg, 1)
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
        cfg_create_node(cfg, n1)
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
    cfg_create_node(cfg, 0)
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
