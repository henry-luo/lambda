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

// =====================================================
// Helpers
// =====================================================
pn null16() {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

pn null32() {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

pn int32() {
    var a = [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
    return a
}

// =====================================================
// 3-level indexed array (arr): 16 x 16 x 32 = 8192 cap
// For sparse/absolute-index access
// =====================================================
pn arr_new() {
    var a = { l0: null16(), sz: 0 }
    return a
}

pn arr_get(a, idx) {
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

pn arr_set(a, idx, val) {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null16()
        l0[i0] = c1
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = null32()
        c1[i1] = c2
    }
    var _d3 = 0
    c2[i2] = val
    return 0
}

// Integer array (defaults to 0)
pn iarr_new() {
    var a = { l0: null16() }
    return a
}

pn iarr_get(a, idx) {
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

pn iarr_set(a, idx, val) {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null16()
        l0[i0] = c1
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = int32()
        c1[i1] = c2
    }
    var _d3 = 0
    c2[i2] = val
    return 0
}

// =====================================================
// Big vector (bvec): sequential append/remove, 8192 cap
// Uses 3-level arr internally plus first/sz tracking
// =====================================================
pn bvec_new() {
    var v = { l0: null16(), sz: 0, first: 0 }
    return v
}

pn bvec_add(v, item) {
    var s = (v.sz)
    var i2 = s % 32
    var mid = shr(s, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (v.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null16()
        l0[i0] = c1
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = null32()
        c1[i1] = c2
    }
    var _d3 = 0
    c2[i2] = item
    var ns = s + 1
    v.sz = ns
    return 0
}

pn bvec_raw_get(v, idx) {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (v.l0)
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn bvec_at(v, idx) {
    var f = (v.first)
    var ai = f + idx
    var r = bvec_raw_get(v, ai)
    return r
}

pn bvec_size(v) {
    var s = (v.sz)
    var f = (v.first)
    var r = s - f
    return r
}

pn bvec_remove_first(v) {
    var f = (v.first)
    var s = (v.sz)
    if (f >= s) { return null }
    var r = bvec_raw_get(v, f)
    var nf = f + 1
    v.first = nf
    return r
}

pn bvec_is_empty(v) {
    var f = (v.first)
    var s = (v.sz)
    if (f >= s) { return 1 }
    return 0
}

// Check if bvec contains element with given dfn field
pn bvec_has_dfn(v, id) {
    var f = (v.first)
    var s = (v.sz)
    var i = f
    while (i < s) {
        var elem = bvec_raw_get(v, i)
        if (elem != null) {
            var eid = (elem.dfn)
            if (eid == id) { return 1 }
        }
        i = i + 1
    }
    return 0
}

// =====================================================
// Small vector (vec): 16x16=256, for per-node small lists
// =====================================================
pn vec_new() {
    var v = { chunks: null16(), sz: 0 }
    return v
}

pn vec_add(v, item) {
    var s = (v.sz)
    var ii = s % 16
    var ci = shr(s, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    if (ck == null) {
        var _n = 0
        ck = null16()
        cks[ci] = ck
    }
    var _d = 0
    ck[ii] = item
    var ns = s + 1
    v.sz = ns
    return 0
}

pn vec_at(v, idx) {
    var ii = idx % 16
    var ci = shr(idx, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    if (ck == null) { return null }
    var r = ck[ii]
    return r
}

pn vec_size(v) {
    var r = (v.sz)
    return r
}

// =====================================================
// Integer Set (iset): for nonBackPreds - set of ints
// =====================================================
pn iset_new() {
    var s = { items: vec_new() }
    return s
}

pn iset_add(s, val) {
    var items = (s.items)
    var sz = (items.sz)
    var i = 0
    while (i < sz) {
        var elem = vec_at(items, i)
        if (elem == val) { return 0 }
        i = i + 1
    }
    vec_add(items, val)
    return 1
}

pn iset_size(s) {
    var items = (s.items)
    var r = vec_size(items)
    return r
}

// =====================================================
// BasicBlock
// =====================================================
pn bb_new(name) {
    var ie = vec_new()
    var oe = vec_new()
    var b = { bid: 0, inEdges: null, outEdges: null }
    b.bid = name
    b.inEdges = ie
    b.outEdges = oe
    return b
}

// =====================================================
// ControlFlowGraph
// =====================================================
pn cfg_new() {
    var bbm = arr_new()
    var c = { startNode: null, bbMap: null, numNodes: 0 }
    c.bbMap = bbm
    return c
}

pn cfg_create_node(cfg, name) {
    var bbm = (cfg.bbMap)
    var node = arr_get(bbm, name)
    if (node == null) {
        node = bb_new(name)
        arr_set(bbm, name, node)
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

pn cfg_add_edge(cfg, fromName, toName) {
    var fromNode = cfg_create_node(cfg, fromName)
    var toNode = cfg_create_node(cfg, toName)
    var foe = (fromNode.outEdges)
    vec_add(foe, toNode)
    var tie = (toNode.inEdges)
    vec_add(tie, fromNode)
    return 0
}

pn cfg_get_num_nodes(cfg) {
    var r = (cfg.numNodes)
    return r
}

// =====================================================
// SimpleLoop
// =====================================================
pn loop_new(bb, isReducible, counter) {
    var bbs = bvec_new()
    var chs = bvec_new()
    var l = { lid: 0, isRed: 0, parentId: -1, isRoot: 0, nestLvl: 0, depthLvl: 0, header: null, bbs: null, children: null }
    l.lid = counter
    l.isRed = isReducible
    l.header = bb
    l.bbs = bbs
    l.children = chs
    if (bb != null) {
        var _d = 0
        bvec_add(bbs, bb)
    }
    return l
}

pn loop_add_node(loop, bb) {
    var bbs = (loop.bbs)
    bvec_add(bbs, bb)
    return 0
}

pn loop_add_child(loop, child) {
    var chs = (loop.children)
    bvec_add(chs, child)
    return 0
}

pn loop_set_parent(loop, parent) {
    var pid = (parent.lid)
    loop.parentId = pid
    loop_add_child(parent, loop)
    return 0
}

// =====================================================
// LoopStructureGraph
// =====================================================
pn lsg_new() {
    var loops = arr_new()
    var root = loop_new(null, 1, 0)
    root.nestLvl = 0
    root.isRoot = 1
    arr_set(loops, 0, root)
    var l = { loopCounter: 1, loops: null, root: null }
    l.loops = loops
    l.root = root
    return l
}

pn lsg_create_new_loop(lsg, bb, isReducible) {
    var lc = (lsg.loopCounter)
    var loop = loop_new(bb, isReducible, lc)
    var nlc = lc + 1
    lsg.loopCounter = nlc
    var loops = (lsg.loops)
    arr_set(loops, lc, loop)
    return loop
}

pn lsg_get_num_loops(lsg) {
    var r = (lsg.loopCounter)
    return r
}

pn lsg_calc_nesting_rec(lsg, loop, depth) {
    loop.depthLvl = depth
    var chs = (loop.children)
    var f = (chs.first)
    var s = (chs.sz)
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
    return 0
}

pn lsg_calc_nesting(lsg) {
    var loops = (lsg.loops)
    var lc = (lsg.loopCounter)
    var i = 0
    while (i < lc) {
        var l = arr_get(loops, i)
        if (l != null) {
            var ir = (l.isRoot)
            if (ir == 0) {
                var pid = (l.parentId)
                if (pid == -1) {
                    var root = (lsg.root)
                    loop_set_parent(l, root)
                }
            }
        }
        i = i + 1
    }
    var root2 = (lsg.root)
    lsg_calc_nesting_rec(lsg, root2, 0)
    return 0
}

// =====================================================
// UnionFindNode
// =====================================================
pn uf_new() {
    var n = { dfn: 0, parentDfn: 0, bb: null, loop: null }
    return n
}

pn uf_init(node, bb, dfsNum) {
    node.dfn = dfsNum
    node.parentDfn = dfsNum
    node.bb = bb
    node.loop = null
    return 0
}

pn uf_find_set(nodes, nodeId) {
    var node = arr_get(nodes, nodeId)
    var pdfn = (node.parentDfn)
    if (pdfn == nodeId) { return nodeId }
    var path = vec_new()
    var cur = nodeId
    var curNode = node
    var curp = pdfn
    while (cur != curp) {
        var pp = curp
        var gpNode = arr_get(nodes, pp)
        var gpp = (gpNode.parentDfn)
        if (pp != gpp) {
            vec_add(path, curNode)
        }
        cur = pp
        curNode = gpNode
        curp = gpp
    }
    var root = cur
    var psz = vec_size(path)
    var j = 0
    while (j < psz) {
        var pnode = vec_at(path, j)
        pnode.parentDfn = root
        j = j + 1
    }
    return root
}

// =====================================================
// Havlak Loop Finder
// =====================================================

pn hlf_is_ancestor(hlf_last, w, v) {
    if (w > v) { return 0 }
    var lw = iarr_get(hlf_last, w)
    if (v > lw) { return 0 }
    return 1
}

// Recursive DFS
pn hlf_do_dfs(nodes, numMap, last, currentBB, current) {
    var ufn = arr_get(nodes, current)
    uf_init(ufn, currentBB, current)
    var bid = (currentBB.bid)
    iarr_set(numMap, bid, current)
    var lastId = current
    var oe = (currentBB.outEdges)
    var oeSz = vec_size(oe)
    var i = 0
    while (i < oeSz) {
        var target = vec_at(oe, i)
        var tbid = (target.bid)
        var tnum = iarr_get(numMap, tbid)
        if (tnum == UNVISITED) {
            var nextId = lastId + 1
            lastId = hlf_do_dfs(nodes, numMap, last, target, nextId)
        }
        i = i + 1
    }
    iarr_set(last, current, lastId)
    return lastId
}

pn hlf_process_edges(nodes, numMap, backPreds, nonBackPreds, hlf_last, nodeW, w) {
    var ie = (nodeW.inEdges)
    var ieSz = vec_size(ie)
    var i = 0
    while (i < ieSz) {
        var nodeV = vec_at(ie, i)
        var vbid = (nodeV.bid)
        var v = iarr_get(numMap, vbid)
        if (v != UNVISITED) {
            var anc = hlf_is_ancestor(hlf_last, w, v)
            if (anc == 1) {
                var bp = arr_get(backPreds, w)
                vec_add(bp, v)
            }
            if (anc == 0) {
                var nbp = arr_get(nonBackPreds, w)
                iset_add(nbp, v)
            }
        }
        i = i + 1
    }
    return 0
}

pn hlf_step_d(nodes, backPreds, hlf_type, w, nodePool) {
    var bp = arr_get(backPreds, w)
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

pn hlf_step_e(nodes, nonBackPreds, hlf_type, hlf_last, w, nodePool, workList, x) {
    var xdfn = (x.dfn)
    var nbp = arr_get(nonBackPreds, xdfn)
    var items = (nbp.items)
    var sz = (items.sz)
    var i = 0
    while (i < sz) {
        var iter = vec_at(items, i)
        var ydashId = uf_find_set(nodes, iter)
        var ydash = arr_get(nodes, ydashId)
        var yddfn = (ydash.dfn)
        var anc = hlf_is_ancestor(hlf_last, w, yddfn)
        if (anc == 0) {
            iarr_set(hlf_type, w, BB_IRREDUCIBLE)
            var wnbp = arr_get(nonBackPreds, w)
            iset_add(wnbp, yddfn)
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

pn hlf_set_loop_attrs(nodes, hlf_header, w, nodePool, loop) {
    var wNode = arr_get(nodes, w)
    wNode.loop = loop
    var f = (nodePool.first)
    var s = (nodePool.sz)
    var i = f
    while (i < s) {
        var node = bvec_raw_get(nodePool, i)
        var ndfn = (node.dfn)
        iarr_set(hlf_header, ndfn, w)
        node.parentDfn = w
        var nl = (node.loop)
        if (nl != null) {
            var _d = 0
            loop_set_parent(nl, loop)
        }
        if (nl == null) {
            var nbb = (node.bb)
            if (nbb != null) {
                var _d2 = 0
                loop_add_node(loop, nbb)
            }
        }
        i = i + 1
    }
    return 0
}

pn hlf_find_loops(cfg, lsg) {
    var sn = (cfg.startNode)
    if (sn == null) { return 0 }
    var size = cfg_get_num_nodes(cfg)

    var nonBackPreds = arr_new()
    var backPreds = arr_new()
    var numMap = iarr_new()
    var hlf_header = iarr_new()
    var hlf_type = iarr_new()
    var hlf_last = iarr_new()
    var nodes = arr_new()

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
    var startBB = (cfg.startNode)
    hlf_do_dfs(nodes, numMap, hlf_last, startBB, 0)

    // Identify edges
    var wi = 0
    while (wi < size) {
        iarr_set(hlf_header, wi, 0)
        iarr_set(hlf_type, wi, BB_NONHEADER)
        var ufNode = arr_get(nodes, wi)
        var nbb = (ufNode.bb)
        if (nbb == null) {
            var _d = 0
            iarr_set(hlf_type, wi, BB_DEAD)
        }
        if (nbb != null) {
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
        var nodePool = bvec_new()
        var wNode = arr_get(nodes, w)
        var nodeW = (wNode.bb)
        if (nodeW != null) {
            hlf_step_d(nodes, backPreds, hlf_type, w, nodePool)
            var workList = bvec_new()
            var npf = (nodePool.first)
            var nps = (nodePool.sz)
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
            if (npSz2 > 0) {
                var isRed = 1
                if (wtype == BB_IRREDUCIBLE) {
                    var _d4 = 0
                    isRed = 0
                }
                var loop = lsg_create_new_loop(lsg, nodeW, isRed)
                hlf_set_loop_attrs(nodes, hlf_header, w, nodePool, loop)
            }
            if (npSz2 == 0) {
                if (wtype == BB_SELF) {
                    var loop2 = lsg_create_new_loop(lsg, nodeW, 1)
                    hlf_set_loop_attrs(nodes, hlf_header, w, nodePool, loop2)
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

pn build_diamond(cfg, start) {
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

pn build_connect(cfg, start, end) {
    cfg_add_edge(cfg, start, end)
    return 0
}

pn build_straight(cfg, start, n) {
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

pn build_base_loop(cfg, from) {
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

pn construct_simple_cfg(cfg) {
    cfg_create_node(cfg, 0)
    build_base_loop(cfg, 0)
    cfg_create_node(cfg, 1)
    cfg_add_edge(cfg, 0, 2)
    return 0
}

pn find_loops(cfg, lsg) {
    hlf_find_loops(cfg, lsg)
    return 0
}

pn add_dummy_loops(cfg, lsg, numDummyLoops) {
    var i = 0
    while (i < numDummyLoops) {
        find_loops(cfg, lsg)
        i = i + 1
    }
    return 0
}

pn construct_cfg(cfg, parLoops, pparLoops, ppparLoops) {
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

pn lta_main(numDummyLoops, findLoopIterations, parLoops, pparLoops, ppparLoops) {
    var cfg = cfg_new()
    cfg_create_node(cfg, 0)
    construct_simple_cfg(cfg)
    var lsg = lsg_new()
    add_dummy_loops(cfg, lsg, numDummyLoops)
    construct_cfg(cfg, parLoops, pparLoops, ppparLoops)
    find_loops(cfg, lsg)
    var i = 0
    while (i < findLoopIterations) {
        var newLsg = lsg_new()
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

pn verify_result(result, innerIterations) {
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

pn main() {
    var result = lta_main(1, 1, 10, 10, 5)
    var ok = verify_result(result, 1)
    if (ok == 1) {
        print("Havlak: PASS\n")
    }
    if (ok == 0) {
        print("Havlak: FAIL\n")
    }
    return 0
}
