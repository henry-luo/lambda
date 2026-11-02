// ARCHIVED SOURCE — do not edit, and do not add to run_benchmarks.py.
//
// This is cd2.ls as it stood before the 2026-08-11 rewrite that replaced the
// integer-handle arena (`type Arr`, node = [key, val, left, right, parent,
// color]) with direct node references. The archived engine binaries under
// test/benchmark/exe/ (lambda-v26-*, lambda-v27-*) were measured against THIS
// source, so any comparison against them must run this file — running an old
// binary against the current cd2.ls compares two different programs.
//
// Live benchmark: cd2.ls. Same observable output (collisions=4305, CD: PASS),
// so cd2_orig.txt and cd2.txt are identical by construction.
//
// AWFY Benchmark: CD (Collision Detection) — Typed version
// Comprehensive type annotations for native int/float arithmetic via C2MIR JIT

// --- Constants ---
let MIN_X = 0
let MIN_Y = 0
let MAX_X = 1000
let MAX_Y = 1000
let MIN_Z = 0
let MAX_Z = 10
let PROXIMITY_RADIUS = 1
let GOOD_VOXEL_SIZE = 2
let RED = 1
let BLACK = 0
let NIL = -1

// Node array layout: [key, val, left, right, parent, color]
let NK = 0
let NV = 1
let NL = 2
let NR = 3
let NP = 4
let NC = 5

// =====================================================
// Helpers (typed float params → native arithmetic)
// =====================================================
pn safe_div(a: float, b: float) any {
    if (b == 0) { return 0.0 }
    var r: float = a / b
    return r
}

pn min_f(a: float, b: float) any {
    if (a <= b) { return a }
    return b
}

pn max_f(a: float, b: float) any {
    if (a >= b) { return a }
    return b
}

pn check_overlap(low: float, high: float) any {
    if (low <= 1) {
        if (1 <= high) { return 1 }
    }
    if (low <= 0) {
        if (0 <= high) { return 1 }
    }
    if (0 <= low) {
        if (high <= 1) { return 1 }
    }
    return 0
}

pn get_old_or_new(old, newp) any {
    if (old == null) { return newp }
    return old
}

pn null16() any {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

pn null32() any {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

// =====================================================
// Type definitions for annotated map field access
// =====================================================
type Arr = {l0: list, sz: int}
type Vec = any
type RbtTree = {root: int, cnt: int, nd: map}
type DrawCtx = {p1x: float, p1y: float, p2x: float, p2y: float, motionIdx: int}

// =====================================================
// 3-level indexed array: 16 x 16 x 32 = 8192 cap
// =====================================================
pn arr_new() any {
    let _admitted_a_96: Arr = { l0: null16(), sz: 0 }
    var a: Arr = _admitted_a_96^
    return a
}

pn arr_get(a, idx: int) any {
    let _admitted_i2_101: int = int(idx % 32)
    var i2: int = _admitted_i2_101^
    var mid: int = shr(idx, 5)
    let _admitted_i1_103: int = int(mid % 16)
    var i1: int = _admitted_i1_103^
    var i0: int = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn arr_set(var a, idx: int, val) any {
    let _admitted_i2_115: int = int(idx % 32)
    var i2: int = _admitted_i2_115^
    var mid: int = shr(idx, 5)
    let _admitted_i1_117: int = int(mid % 16)
    var i1: int = _admitted_i1_117^
    var i0: int = shr(mid, 4)
    // S9.3.1: binding a trie level and mutating the local writes a copy.
    // Write the whole path so each store lands in `a` (O(depth), no subtree copy).
    if (a.l0[i0] == null) { a.l0[i0] = null16()^ }
    if (a.l0[i0][i1] == null) { a.l0[i0][i1] = null32()^ }
    a.l0[i0][i1][i2] = val
    return 0
}

// =====================================================
// Small vector: 16x16=256
// =====================================================
pn vec_new() any {
    return []
}

pn vec_add(var v, item) any {
    push(v, item)
    return 0
}

pn vec_at(v, idx: int) any {
    return v[idx]
}

// =====================================================
// Red-Black Tree (integer keys)
// Node = array: [key, val, left_id, right_id, parent_id, color]
// Tree = map: { root, cnt, nd }
// =====================================================

pn rbt_new() any {
    let _admitted_nd_150: Arr = arr_new()
    var nd: Arr = _admitted_nd_150^
    var t: RbtTree = { root: -1, cnt: 0, nd: nd }
    return t
}

pn rbt_nd(tree, id: int) any {
    return arr_get(tree.nd, id)
}

pn rbt_mk_node(var tree, key: int, val) any {
    let _admitted_c_160: int = (tree.cnt)
    var c: int = _admitted_c_160^
    var n = [key, val, NIL, NIL, NIL, RED]
    var nd = (tree.nd)
    arr_set(nd, c, n)
    var nc: int = c + 1
    tree.cnt = nc
    return c
}

pn rbt_left_rotate(var tree, xId: int) any {
    var xn = rbt_nd(tree, xId)
    let _admitted_yId_171: int = xn[NR]
    var yId: int = _admitted_yId_171^
    var yn = rbt_nd(tree, yId)
    let _admitted_ylId_173: int = yn[NL]
    var ylId: int = _admitted_ylId_173^
    // x.right = y.left
    xn[NR] = ylId
    if (ylId != NIL) {
        var yln = rbt_nd(tree, ylId)
        yln[NP] = xId
    }
    // y.parent = x.parent
    let _admitted_xpId_181: int = xn[NP]
    var xpId: int = _admitted_xpId_181^
    yn[NP] = xpId
    if (xpId == NIL) {
        tree.root = yId
    }
    if (xpId != NIL) {
        var xpn = rbt_nd(tree, xpId)
        let _admitted_xplId_188: int = xpn[NL]
        var xplId: int = _admitted_xplId_188^
        if (xId == xplId) {
            xpn[NL] = yId
        }
        if (xId != xplId) {
            xpn[NR] = yId
        }
    }
    // y.left = x
    yn[NL] = xId
    xn[NP] = yId
    return yId
}

pn rbt_right_rotate(var tree, yId: int) any {
    var yn = rbt_nd(tree, yId)
    let _admitted_xId_204: int = yn[NL]
    var xId: int = _admitted_xId_204^
    var xn = rbt_nd(tree, xId)
    let _admitted_xrId_206: int = xn[NR]
    var xrId: int = _admitted_xrId_206^
    // y.left = x.right
    yn[NL] = xrId
    if (xrId != NIL) {
        var xrn = rbt_nd(tree, xrId)
        xrn[NP] = yId
    }
    // x.parent = y.parent
    let _admitted_ypId_214: int = yn[NP]
    var ypId: int = _admitted_ypId_214^
    xn[NP] = ypId
    if (ypId == NIL) {
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        let _admitted_yplId_221: int = ypn[NL]
        var yplId: int = _admitted_yplId_221^
        if (yId == yplId) {
            ypn[NL] = xId
        }
        if (yId != yplId) {
            ypn[NR] = xId
        }
    }
    // x.right = y
    xn[NR] = yId
    yn[NP] = xId
    return xId
}

pn rbt_put(var tree, key: int, value) any {
    var yId: int = NIL
    let _admitted_xId_237: int = (tree.root)
    var xId: int = _admitted_xId_237^
    while (xId != NIL) {
        yId = xId
        var xn = rbt_nd(tree, xId)
        let _admitted_xk_241: int = xn[NK]
        var xk: int = _admitted_xk_241^
        if (key < xk) {
            let _admitted_xId_243: int = xn[NL]
            xId = _admitted_xId_243^
        }
        if (key > xk) {
            let _admitted_xId_246: int = xn[NR]
            xId = _admitted_xId_246^
        }
        if (key == xk) {
            var oldVal = xn[NV]
            xn[NV] = value
            return oldVal
        }
    }
    let _admitted_zId_254: int = rbt_mk_node(tree, key, value)
    var zId: int = _admitted_zId_254^
    var zn = rbt_nd(tree, zId)
    zn[NP] = yId
    if (yId == NIL) {
        tree.root = zId
    }
    if (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        let _admitted_yk_262: int = yn[NK]
        var yk: int = _admitted_yk_262^
        if (key < yk) {
            yn[NL] = zId
        }
        if (key >= yk) {
            yn[NR] = zId
        }
    }
    // Fix up
    var curId: int = zId
    let _admitted_rootId_272: int = (tree.root)
    var rootId: int = _admitted_rootId_272^
    while (curId != rootId) {
        var cur = rbt_nd(tree, curId)
        let _admitted_pId_275: int = cur[NP]
        var pId: int = _admitted_pId_275^
        var parn = rbt_nd(tree, pId)
        let _admitted_pcol_277: int = parn[NC]
        var pcol: int = _admitted_pcol_277^
        if (pcol != RED) {
            curId = rootId
        }
        if (pcol == RED) {
            let _admitted_ppId_282: int = parn[NP]
            var ppId: int = _admitted_ppId_282^
            var ppn = rbt_nd(tree, ppId)
            let _admitted_ppLId_284: int = ppn[NL]
            var ppLId: int = _admitted_ppLId_284^
            if (pId == ppLId) {
                let _admitted_uncId_286: int = ppn[NR]
                var uncId: int = _admitted_uncId_286^
                var uncCol: int = BLACK
                if (uncId != NIL) {
                    var uncN = rbt_nd(tree, uncId)
                    let _admitted_uncCol_290: int = uncN[NC]
                    uncCol = _admitted_uncCol_290^
                }
                if (uncCol == RED) {
                    parn[NC] = BLACK
                    var uncN2 = rbt_nd(tree, uncId)
                    uncN2[NC] = BLACK
                    ppn[NC] = RED
                    curId = ppId
                    let _admitted_rootId_298: int = (tree.root)
                    rootId = _admitted_rootId_298^
                }
                if (uncCol != RED) {
                    var curRN = rbt_nd(tree, curId)
                    let _admitted_crpId_302: int = curRN[NP]
                    var crpId: int = _admitted_crpId_302^
                    var crpN = rbt_nd(tree, crpId)
                    let _admitted_crprId_304: int = crpN[NR]
                    var crprId: int = _admitted_crprId_304^
                    if (curId == crprId) {
                        curId = pId
                        rbt_left_rotate(tree, curId)^
                    }
                    var cur3 = rbt_nd(tree, curId)
                    let _admitted_p3Id_310: int = cur3[NP]
                    var p3Id: int = _admitted_p3Id_310^
                    var p3 = rbt_nd(tree, p3Id)
                    p3[NC] = BLACK
                    let _admitted_pp3Id_313: int = p3[NP]
                    var pp3Id: int = _admitted_pp3Id_313^
                    var pp3 = rbt_nd(tree, pp3Id)
                    pp3[NC] = RED
                    rbt_right_rotate(tree, pp3Id)^
                    let _admitted_rootId_317: int = (tree.root)
                    rootId = _admitted_rootId_317^
                }
            }
            if (pId != ppLId) {
                let _admitted_uncId2_321: int = ppn[NL]
                var uncId2: int = _admitted_uncId2_321^
                var uncCol2: int = BLACK
                if (uncId2 != NIL) {
                    var uncN3 = rbt_nd(tree, uncId2)
                    let _admitted_uncCol2_325: int = uncN3[NC]
                    uncCol2 = _admitted_uncCol2_325^
                }
                if (uncCol2 == RED) {
                    parn[NC] = BLACK
                    var uncN4 = rbt_nd(tree, uncId2)
                    uncN4[NC] = BLACK
                    ppn[NC] = RED
                    curId = ppId
                    let _admitted_rootId_333: int = (tree.root)
                    rootId = _admitted_rootId_333^
                }
                if (uncCol2 != RED) {
                    var cur2N = rbt_nd(tree, curId)
                    let _admitted_c2pId_337: int = cur2N[NP]
                    var c2pId: int = _admitted_c2pId_337^
                    var c2pN = rbt_nd(tree, c2pId)
                    let _admitted_c2plId_339: int = c2pN[NL]
                    var c2plId: int = _admitted_c2plId_339^
                    if (curId == c2plId) {
                        curId = pId
                        rbt_right_rotate(tree, curId)^
                    }
                    var cur4 = rbt_nd(tree, curId)
                    let _admitted_p4Id_345: int = cur4[NP]
                    var p4Id: int = _admitted_p4Id_345^
                    var p4 = rbt_nd(tree, p4Id)
                    p4[NC] = BLACK
                    let _admitted_pp4Id_348: int = p4[NP]
                    var pp4Id: int = _admitted_pp4Id_348^
                    var pp4 = rbt_nd(tree, pp4Id)
                    pp4[NC] = RED
                    rbt_left_rotate(tree, pp4Id)^
                    let _admitted_rootId_352: int = (tree.root)
                    rootId = _admitted_rootId_352^
                }
            }
        }
    }
    var rootN = rbt_nd(tree, (tree.root))
    rootN[NC] = BLACK
    return null
}

pn rbt_find_node(tree, key: int) any {
    let _admitted_curId_363: int = (tree.root)
    var curId: int = _admitted_curId_363^
    while (curId != NIL) {
        var n = rbt_nd(tree, curId)
        let _admitted_nk_366: int = n[NK]
        var nk: int = _admitted_nk_366^
        if (key == nk) { return curId }
        if (key < nk) {
            let _admitted_curId_369: int = n[NL]
            curId = _admitted_curId_369^
        }
        if (key > nk) {
            let _admitted_curId_372: int = n[NR]
            curId = _admitted_curId_372^
        }
    }
    return NIL
}

pn rbt_get(tree, key: int) any {
    let _admitted_nId_379: int = rbt_find_node(tree, key)
    var nId: int = _admitted_nId_379^
    if (nId == NIL) { return null }
    var n = rbt_nd(tree, nId)
    var v = n[NV]
    return v
}

pn rbt_tree_min(tree, xId: int) any {
    while (xId != NIL) {
        var n = rbt_nd(tree, xId)
        let _admitted_lId_389: int = n[NL]
        var lId: int = _admitted_lId_389^
        if (lId == NIL) { return xId }
        xId = lId
    }
    return xId
}

pn rbt_successor(tree, xId: int) any {
    var xn = rbt_nd(tree, xId)
    let _admitted_rId_398: int = xn[NR]
    var rId: int = _admitted_rId_398^
    if (rId != NIL) {
        let _admitted_r_400: int = rbt_tree_min(tree, rId)
        var r: int = _admitted_r_400^
        return r
    }
    let _admitted_yId_403: int = xn[NP]
    var yId: int = _admitted_yId_403^
    while (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        let _admitted_yrId_406: int = yn[NR]
        var yrId: int = _admitted_yrId_406^
        if (xId != yrId) { return yId }
        xId = yId
        let _admitted_yId_409: int = yn[NP]
        yId = _admitted_yId_409^
    }
    return NIL
}

pn rbt_first(tree) any {
    let _admitted_rId_415: int = (tree.root)
    var rId: int = _admitted_rId_415^
    if (rId == NIL) { return NIL }
    let _admitted_r_417: int = rbt_tree_min(tree, rId)
    var r: int = _admitted_r_417^
    return r
}

pn rbt_remove_fixup(var tree, xId: int, xParId: int) any {
    let _admitted_rootId_422: int = (tree.root)
    var rootId: int = _admitted_rootId_422^
    while ((xId != rootId)^) {
        var xCol: int = BLACK
        if ((xId != NIL)^) {
            var xn = rbt_nd(tree, xId)
            let _admitted_xCol_427: int = xn[NC]
            xCol = _admitted_xCol_427^
        }
        if (xCol != BLACK) {
            xId = rootId
        }
        if (xCol == BLACK) {
            var xpn = rbt_nd(tree, xParId)
            let _admitted_xplId_434: int = xpn[NL]
            var xplId: int = _admitted_xplId_434^
            if ((xId == xplId)^) {
                let _admitted_wId_436: int = xpn[NR]
                var wId: int = _admitted_wId_436^
                var wn = rbt_nd(tree, wId)
                let _admitted_wc_438: int = wn[NC]
                var wc: int = _admitted_wc_438^
                if (wc == RED) {
                    wn[NC] = BLACK
                    xpn[NC] = RED
                    rbt_left_rotate(tree, xParId)^
                    xpn = rbt_nd(tree, xParId)
                    let _admitted_wId_444: int = xpn[NR]
                    wId = _admitted_wId_444^
                    wn = rbt_nd(tree, wId)
                }
                var wlc: int = BLACK
                let _admitted_wlId_448: int = wn[NL]
                var wlId: int = _admitted_wlId_448^
                if (wlId != NIL) {
                    var wln = rbt_nd(tree, wlId)
                    let _admitted_wlc_451: int = wln[NC]
                    wlc = _admitted_wlc_451^
                }
                var wrc: int = BLACK
                let _admitted_wrId_454: int = wn[NR]
                var wrId: int = _admitted_wrId_454^
                if (wrId != NIL) {
                    var wrn = rbt_nd(tree, wrId)
                    let _admitted_wrc_457: int = wrn[NC]
                    wrc = _admitted_wrc_457^
                }
                var didCase2: int = 0
                if (wlc == BLACK) {
                    if (wrc == BLACK) {
                        wn[NC] = RED
                        xId = xParId
                        var xn2 = rbt_nd(tree, xId)
                        let checked_parent: int = xn2[NP]
                        xParId = checked_parent^
                        let _admitted_rootId_466: int = (tree.root)
                        rootId = _admitted_rootId_466^
                        didCase2 = 1
                    }
                }
                if (didCase2 == 0) {
                    // Refresh
                    wn = rbt_nd(tree, wId)
                    let _admitted_wrId_473: int = wn[NR]
                    wrId = _admitted_wrId_473^
                    wrc = BLACK
                    if (wrId != NIL) {
                        var wrn2 = rbt_nd(tree, wrId)
                        let _admitted_wrc_477: int = wrn2[NC]
                        wrc = _admitted_wrc_477^
                    }
                    if (wrc == BLACK) {
                        let _admitted_wlId_480: int = wn[NL]
                        wlId = _admitted_wlId_480^
                        if (wlId != NIL) {
                            var wln2 = rbt_nd(tree, wlId)
                            wln2[NC] = BLACK
                        }
                        wn[NC] = RED
                        rbt_right_rotate(tree, wId)^
                        xpn = rbt_nd(tree, xParId)
                        let _admitted_wId_488: int = xpn[NR]
                        wId = _admitted_wId_488^
                        wn = rbt_nd(tree, wId)
                    }
                    let _admitted_xpc_491: int = xpn[NC]
                    var xpc: int = _admitted_xpc_491^
                    wn[NC] = xpc
                    xpn[NC] = BLACK
                    let _admitted_wrId_494: int = wn[NR]
                    wrId = _admitted_wrId_494^
                    if (wrId != NIL) {
                        var wrn3 = rbt_nd(tree, wrId)
                        wrn3[NC] = BLACK
                    }
                    rbt_left_rotate(tree, xParId)^
                    xId = (tree.root)
                    rootId = xId^
                    var xn3 = rbt_nd(tree, xId)
                    let checked_parent: int = xn3[NP]
                    xParId = checked_parent^
                }
            }
            if ((xId != xplId)^) {
                let _admitted_wId2_507: int = xpn[NL]
                var wId2: int = _admitted_wId2_507^
                var wn2 = rbt_nd(tree, wId2)
                let _admitted_wc2_509: int = wn2[NC]
                var wc2: int = _admitted_wc2_509^
                if (wc2 == RED) {
                    wn2[NC] = BLACK
                    xpn[NC] = RED
                    rbt_right_rotate(tree, xParId)^
                    xpn = rbt_nd(tree, xParId)
                    let _admitted_wId2_515: int = xpn[NL]
                    wId2 = _admitted_wId2_515^
                    wn2 = rbt_nd(tree, wId2)
                }
                var wrc2: int = BLACK
                let _admitted_wrId2_519: int = wn2[NR]
                var wrId2: int = _admitted_wrId2_519^
                if (wrId2 != NIL) {
                    var wrn4 = rbt_nd(tree, wrId2)
                    let _admitted_wrc2_522: int = wrn4[NC]
                    wrc2 = _admitted_wrc2_522^
                }
                var wlc2: int = BLACK
                let _admitted_wlId2_525: int = wn2[NL]
                var wlId2: int = _admitted_wlId2_525^
                if (wlId2 != NIL) {
                    var wln3 = rbt_nd(tree, wlId2)
                    let _admitted_wlc2_528: int = wln3[NC]
                    wlc2 = _admitted_wlc2_528^
                }
                var didCase2b: int = 0
                if (wrc2 == BLACK) {
                    if (wlc2 == BLACK) {
                        wn2[NC] = RED
                        xId = xParId
                        var xn4 = rbt_nd(tree, xId)
                        let checked_parent: int = xn4[NP]
                        xParId = checked_parent^
                        let _admitted_rootId_537: int = (tree.root)
                        rootId = _admitted_rootId_537^
                        didCase2b = 1
                    }
                }
                if (didCase2b == 0) {
                    let _admitted_wlId2_542: int = wn2[NL]
                    wlId2 = _admitted_wlId2_542^
                    wlc2 = BLACK
                    if (wlId2 != NIL) {
                        var wln4 = rbt_nd(tree, wlId2)
                        let _admitted_wlc2_546: int = wln4[NC]
                        wlc2 = _admitted_wlc2_546^
                    }
                    if (wlc2 == BLACK) {
                        let _admitted_wrId2_549: int = wn2[NR]
                        wrId2 = _admitted_wrId2_549^
                        if (wrId2 != NIL) {
                            var wrn5 = rbt_nd(tree, wrId2)
                            wrn5[NC] = BLACK
                        }
                        wn2[NC] = RED
                        rbt_left_rotate(tree, wId2)^
                        xpn = rbt_nd(tree, xParId)
                        let _admitted_wId2_557: int = xpn[NL]
                        wId2 = _admitted_wId2_557^
                        wn2 = rbt_nd(tree, wId2)
                    }
                    let _admitted_xpc2_560: int = xpn[NC]
                    var xpc2: int = _admitted_xpc2_560^
                    wn2[NC] = xpc2
                    xpn[NC] = BLACK
                    let _admitted_wlId2_563: int = wn2[NL]
                    wlId2 = _admitted_wlId2_563^
                    if (wlId2 != NIL) {
                        var wln5 = rbt_nd(tree, wlId2)
                        wln5[NC] = BLACK
                    }
                    rbt_right_rotate(tree, xParId)^
                    xId = (tree.root)
                    rootId = xId^
                    var xn5 = rbt_nd(tree, xId)
                    let checked_parent: int = xn5[NP]
                    xParId = checked_parent^
                }
            }
        }
    }
    if ((xId != NIL)^) {
        var xfn = rbt_nd(tree, xId)
        xfn[NC] = BLACK
    }
    return 0
}

pn rbt_remove(var tree, key: int) any {
    let _admitted_zId_585: int = rbt_find_node(tree, key)
    var zId: int = _admitted_zId_585^
    if (zId == NIL) { return null }
    var zn = rbt_nd(tree, zId)
    var zv = zn[NV]
    var yId: int = zId
    let _admitted_zlId_590: int = zn[NL]
    var zlId: int = _admitted_zlId_590^
    let _admitted_zrId_591: int = zn[NR]
    var zrId: int = _admitted_zrId_591^
    if (zlId != NIL) {
        if (zrId != NIL) {
            let _admitted_yId_594: int = rbt_successor(tree, zId)
            yId = _admitted_yId_594^
        }
    }
    var yn = rbt_nd(tree, yId)
    let _admitted_ylId_598: int = yn[NL]
    var ylId: int = _admitted_ylId_598^
    let _admitted_yrId_599: int = yn[NR]
    var yrId: int = _admitted_yrId_599^
    var xId: int = NIL
    if (ylId != NIL) {
        xId = ylId
    }
    if (ylId == NIL) {
        xId = yrId
    }
    let _admitted_xParId_607: int = yn[NP]
    var xParId: int = _admitted_xParId_607^
    if (xId != NIL) {
        var xn = rbt_nd(tree, xId)
        xn[NP] = xParId
    }
    let _admitted_ypId_612: int = yn[NP]
    var ypId: int = _admitted_ypId_612^
    if (ypId == NIL) {
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        let _admitted_yplId_618: int = ypn[NL]
        var yplId: int = _admitted_yplId_618^
        if (yId == yplId) {
            ypn[NL] = xId
        }
        if (yId != yplId) {
            ypn[NR] = xId
        }
    }
    if (yId != zId) {
        let _admitted_ycol_627: int = yn[NC]
        var ycol: int = _admitted_ycol_627^
        if (ycol == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)^
        }
        yn[NP] = zn[NP]
        yn[NC] = zn[NC]
        yn[NL] = zn[NL]
        yn[NR] = zn[NR]
        let _admitted_znlId_635: int = zn[NL]
        var znlId: int = _admitted_znlId_635^
        if (znlId != NIL) {
            var znln = rbt_nd(tree, znlId)
            znln[NP] = yId
        }
        let _admitted_znrId_640: int = zn[NR]
        var znrId: int = _admitted_znrId_640^
        if (znrId != NIL) {
            var znrn = rbt_nd(tree, znrId)
            znrn[NP] = yId
        }
        let _admitted_zpId_645: int = zn[NP]
        var zpId: int = _admitted_zpId_645^
        if (zpId != NIL) {
            var zpn = rbt_nd(tree, zpId)
            let _admitted_zplId_648: int = zpn[NL]
            var zplId: int = _admitted_zplId_648^
            if (zId == zplId) {
                zpn[NL] = yId
            }
            if (zId != zplId) {
                zpn[NR] = yId
            }
        }
        if (zpId == NIL) {
            tree.root = yId
        }
    }
    if (yId == zId) {
        let _admitted_ycol2_661: int = yn[NC]
        var ycol2: int = _admitted_ycol2_661^
        if (ycol2 == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)^
        }
    }
    return zv
}

// =====================================================
// Vector2D key encoding (typed int → native arithmetic)
// =====================================================
pn v2d_key(x: int, y: int) any {
    var kx: int = x + 1000
    var ky: int = y + 1000
    var k: int = kx * 100000 + ky
    return k
}

// =====================================================
// Vector3D operations
// =====================================================
pn v3d_new(x, y, z) any {
    return [x, y, z]
}

// =====================================================
// Voxel hashing (typed float → native division)
// =====================================================
pn voxel_hash_xy(px: float, py: float, var out) any {
    let _admitted_xdiv_690: int = int(px / GOOD_VOXEL_SIZE)
    var xdiv: int = _admitted_xdiv_690^
    let _admitted_ydiv_691: int = int(py / GOOD_VOXEL_SIZE)
    var ydiv: int = _admitted_ydiv_691^
    var rx: int = GOOD_VOXEL_SIZE * xdiv
    var ry: int = GOOD_VOXEL_SIZE * ydiv
    if (px < 0) { rx = rx - GOOD_VOXEL_SIZE }
    if (py < 0) { ry = ry - GOOD_VOXEL_SIZE }
    out[0] = rx
    out[1] = ry
    return 0
}

// =====================================================
// isInVoxel check (typed float → native float arithmetic)
// =====================================================
pn is_in_voxel(vx: int, vy: int, p1x: float, p1y: float, p2x: float, p2y: float) any {
    if (vx > MAX_X) { return 0 }
    if (vx < MIN_X) { return 0 }
    if (vy > MAX_Y) { return 0 }
    if (vy < MIN_Y) { return 0 }

    var vS = GOOD_VOXEL_SIZE
    var r = PROXIMITY_RADIUS / 2
    var x0: float = p1x
    var xv: float = p2x - p1x
    var y0: float = p1y
    var yv: float = p2y - p1y

    var rawLX = safe_div(vx - r - x0, xv)
    var rawHX = safe_div(vx + vS + r - x0, xv)
    var lowX = min_f(rawLX, rawHX)
    var highX = max_f(rawLX, rawHX)

    var xOk: int = 0
    if (xv == 0) {
        if (vx <= x0 + r) {
            if (x0 - r <= vx + vS) {
                xOk = 1
            }
        }
    }
    if (xv != 0) {
        let _admitted_xOk_731: int = check_overlap(lowX, highX)
        xOk = _admitted_xOk_731^
    }
    if (xOk == 0) { return 0 }

    var rawLY = safe_div(vy - r - y0, yv)
    var rawHY = safe_div(vy + vS + r - y0, yv)
    var lowY = min_f(rawLY, rawHY)
    var highY = max_f(rawLY, rawHY)

    var yOk: int = 0
    if (yv == 0) {
        if (vy <= y0 + r) {
            if (y0 - r <= vy + vS) {
                yOk = 1
            }
        }
    }
    if (yv != 0) {
        let _admitted_yOk_749: int = check_overlap(lowY, highY)
        yOk = _admitted_yOk_749^
    }
    if (yOk == 0) { return 0 }

    // Check combined condition
    if (xv == 0) { return 1 }
    if (yv == 0) { return 1 }
    if ((lowY <= highX)^) {
        if ((highX <= highY)^) { return 1 }
    }
    if ((lowY <= lowX)^) {
        if ((lowX <= highY)^) { return 1 }
    }
    if ((lowX <= lowY)^) {
        if ((highY <= highX)^) { return 1 }
    }
    return 0
}

// =====================================================
// Recurse: draw motion into voxel map
// ctx: DrawCtx — typed map for direct field-offset access
// =====================================================
pn recurse_draw(var voxelMap, var seenTree, vx: int, vy: int, ctx: DrawCtx) any {
    var p1x: float = (ctx.p1x)
    var p1y: float = (ctx.p1y)
    var p2x: float = (ctx.p2x)
    var p2y: float = (ctx.p2y)
    let _admitted_inV_777: int = is_in_voxel(vx, vy, p1x, p1y, p2x, p2y)
    var inV: int = _admitted_inV_777^
    if (inV == 0) { return 0 }

    let _admitted_vk_780: int = v2d_key(vx, vy)
    var vk: int = _admitted_vk_780^
    var oldSeen = rbt_put(seenTree, vk, 1)
    if ((oldSeen != null)^) { return 0 }

    var existVec = rbt_get(voxelMap, vk)
    if ((existVec == null)^) {
        existVec = vec_new()
    }
    var motionIdx: int = (ctx.motionIdx)
    vec_add(existVec, motionIdx)^
    // retain the edited value: COW may detach existVec from the stored snapshot.
    rbt_put(voxelMap, vk, existVec)^

    var gs: int = GOOD_VOXEL_SIZE
    recurse_draw(voxelMap, seenTree, vx - gs, vy, ctx)^
    recurse_draw(voxelMap, seenTree, vx + gs, vy, ctx)^
    recurse_draw(voxelMap, seenTree, vx, vy - gs, ctx)^
    recurse_draw(voxelMap, seenTree, vx, vy + gs, ctx)^
    recurse_draw(voxelMap, seenTree, vx - gs, vy - gs, ctx)^
    recurse_draw(voxelMap, seenTree, vx - gs, vy + gs, ctx)^
    recurse_draw(voxelMap, seenTree, vx + gs, vy - gs, ctx)^
    recurse_draw(voxelMap, seenTree, vx + gs, vy + gs, ctx)
    return 0
}

// =====================================================
// findIntersection — refactored for native float arithmetic
// Wrapper extracts array→ typed params at call boundary
// =====================================================
pn fi_collide(sq: float, b: float, a: float,
              i1x: float, i1y: float, i1z: float,
              v1x: float, v1y: float, v1z: float,
              i2x: float, i2y: float, i2z: float,
              v2x: float, v2y: float, v2z: float) {
    // 15 typed float params → ALL arithmetic is native double
    var a2: float = 2.0 * a
    var t1: float = (-b - sq) / a2
    var t2: float = (-b + sq) / a2

    if (t1 <= t2) {
        var collision: int = 0
        if (t1 <= 1.0) {
            if (1.0 <= t2) { collision = 1 }
        }
        if (t1 <= 0.0) {
            if (0.0 <= t2) { collision = 1 }
        }
        if (0.0 <= t1) {
            if (t2 <= 1.0) { collision = 1 }
        }
        if (collision == 1) {
            // Inline max(t1, 0.0) to stay native (avoid function return Item)
            var v: float = t1
            if (t1 < 0.0) { v = 0.0 }
            var r1x: float = i1x + v1x * v
            var r1y: float = i1y + v1y * v
            var r1z: float = i1z + v1z * v
            var r2x: float = i2x + v2x * v
            var r2y: float = i2y + v2y * v
            var r2z: float = i2z + v2z * v
            var rx: float = (r1x + r2x) * 0.5
            var ry: float = (r1y + r2y) * 0.5
            var rz: float = (r1z + r2z) * 0.5
            // Bounds checks with float literals (native comparisons)
            if (rx >= 0.0) {
                if (rx <= 1000.0) {
                    if (ry >= 0.0) {
                        if (ry <= 1000.0) {
                            if (rz >= 0.0) {
                                if (rz <= 10.0) {
                                    var result = v3d_new(rx, ry, rz)
                                    return result
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return null
}

pn fi_compute(i1x: float, i1y: float, i1z: float,
              e1x: float, e1y: float, e1z: float,
              i2x: float, i2y: float, i2z: float,
              e2x: float, e2y: float, e2z: float) {
    // 12 typed float params → ALL arithmetic is native double
    var v1x: float = e1x - i1x
    var v1y: float = e1y - i1y
    var v1z: float = e1z - i1z
    var v2x: float = e2x - i2x
    var v2y: float = e2y - i2y
    var v2z: float = e2z - i2z

    var dvx: float = v2x - v1x
    var dvy: float = v2y - v1y
    var dvz: float = v2z - v1z
    var a: float = dvx * dvx + dvy * dvy + dvz * dvz

    if (a != 0.0) {
        var dix: float = i1x - i2x
        var diy: float = i1y - i2y
        var diz: float = i1z - i2z
        var dmvx: float = v1x - v2x
        var dmvy: float = v1y - v2y
        var dmvz: float = v1z - v2z
        var b: float = 2.0 * (dix * dmvx + diy * dmvy + diz * dmvz)

        var di2x: float = i2x - i1x
        var di2y: float = i2y - i1y
        var di2z: float = i2z - i1z
        // PROXIMITY_RADIUS = 1, so radius² = 1.0
        var c: float = di2x * di2x + di2y * di2y + di2z * di2z - 1.0

        var discr: float = b * b - 4.0 * a * c
        if (discr < 0.0) { return null }

        // sqrt returns push_d(math.sqrt(discr)) → Item
        // Pass through fi_collide typed param for native continuation
        var sq = math.sqrt(discr)
        return fi_collide(sq, b, a,
                          i1x, i1y, i1z, v1x, v1y, v1z,
                          i2x, i2y, i2z, v2x, v2y, v2z)
    }

    // Parallel case: avoid sqrt by comparing squared distances
    var pdx: float = i2x - i1x
    var pdy: float = i2y - i1y
    var pdz: float = i2z - i1z
    var dist_sq: float = pdx * pdx + pdy * pdy + pdz * pdz
    // dist <= PROXIMITY_RADIUS(1) iff dist² <= 1.0
    if (dist_sq > 1.0) { return null }
    var rx: float = (i1x + i2x) * 0.5
    var ry: float = (i1y + i2y) * 0.5
    var rz: float = (i1z + i2z) * 0.5
    var result = v3d_new(rx, ry, rz)
    return result
}

pn find_intersection(m1, m2) any {
    // Extract array values → typed float params at call boundary (it2d conversion)
    return fi_compute(m1[1], m1[2], m1[3], m1[4], m1[5], m1[6],
                      m2[1], m2[2], m2[3], m2[4], m2[5], m2[6])
}

// =====================================================
// Motion: array [cs, p1x, p1y, p1z, p2x, p2y, p2z]
// =====================================================
pn motion_new(cs: int, p1x, p1y, p1z, p2x, p2y, p2z) any {
    return [cs, p1x, p1y, p1z, p2x, p2y, p2z]
}

// =====================================================
// CD Benchmark main logic
// =====================================================

pn simulate_frame(numAircraft: int, tval) any {
    var frame: Vec = vec_new()
    var i: int = 0
    while (i < numAircraft) {
        var cs1: int = i
        var px1 = tval
        var py1 = math.cos(tval) * 2 + i * 3
        var pz1: int = 10
        var a1 = [cs1, px1, py1, pz1]
        vec_add(frame, a1)^
        var cs2: int = i + 1
        var py2 = math.sin(tval) * 2 + i * 3
        var a2 = [cs2, px1, py2, pz1]
        vec_add(frame, a2)^
        i = i + 2
    }
    return frame
}

pn handle_new_frame(var stateTree, frame: Vec) any {
    var motions: Vec = vec_new()
    // Use flat arr for aircraft seen set (IDs are 0-99, well within arr capacity)
    let _admitted_seenArr_959: Arr = arr_new()
    var seenArr: Arr = _admitted_seenArr_959^
    var frameSz: int = len(frame)^
    var i: int = 0
    while (i < frameSz) {
        var aircraft = vec_at(frame, i)
        let _admitted_csId_964: int = aircraft[0]
        var csId: int = _admitted_csId_964^
        var npx = aircraft[1]
        var npy = aircraft[2]
        var npz = aircraft[3]
        var newPos = v3d_new(npx, npy, npz)
        var oldPos = rbt_put(stateTree, csId, newPos)
        arr_set(seenArr, csId, 1)^
        var usePos = get_old_or_new(oldPos, newPos)
        var opx = usePos[0]
        var opy = usePos[1]
        var opz = usePos[2]
        var m = motion_new(csId, opx, opy, opz, npx, npy, npz)
        vec_add(motions, m)^
        i = i + 1
    }

    // Remove aircraft no longer present
    var toRemove: Vec = vec_new()
    let _admitted_curId_982: int = rbt_first(stateTree)
    var curId: int = _admitted_curId_982^
    while (curId != NIL) {
        var curN = rbt_nd(stateTree, curId)
        let _admitted_ck_985: int = curN[NK]
        var ck: int = _admitted_ck_985^
        var inSeen = arr_get(seenArr, ck)
        if ((inSeen == null)^) {
            vec_add(toRemove, ck)^
        }
        let _admitted_curId_990: int = rbt_successor(stateTree, curId)
        curId = _admitted_curId_990^
    }
    var trSz: int = len(toRemove)^
    var ri: int = 0
    while (ri < trSz) {
        let _admitted_rk_995: int = vec_at(toRemove, ri)
        var rk: int = _admitted_rk_995^
        rbt_remove(stateTree, rk)^
        ri = ri + 1
    }

    // Reduce collision set
    var voxelMap = rbt_new()
    var motionsSz: int = len(motions)^
    var vxy = [null, null]
    var mi: int = 0
    while (mi < motionsSz) {
        var mot = vec_at(motions, mi)
        var mp1x = mot[1]
        var mp1y = mot[2]
        var mp2x = mot[4]
        var mp2y = mot[5]
        voxel_hash_xy(mp1x, mp1y, vxy)^
        let _admitted_vvx_1012: int = vxy[0]
        var vvx: int = _admitted_vvx_1012^
        let _admitted_vvy_1013: int = vxy[1]
        var vvy: int = _admitted_vvy_1013^
        var motSeen = rbt_new()
        // Bundle invariant params as typed DrawCtx map
        let _admitted_ctx_1016: DrawCtx = { p1x: mp1x, p1y: mp1y, p2x: mp2x, p2y: mp2y, motionIdx: mi }
        var ctx: DrawCtx = _admitted_ctx_1016^
        recurse_draw(voxelMap, motSeen, vvx, vvy, ctx)^
        mi = mi + 1
    }

    // Collect voxels with >1 motion and check collisions
    var collisionCount: int = 0
    let _admitted_vmCur_1023: int = rbt_first(voxelMap)
    var vmCur: int = _admitted_vmCur_1023^
    while (vmCur != NIL) {
        var vmN = rbt_nd(voxelMap, vmCur)
        var motVec: Vec = vmN[NV]
        var mvsz: int = len(motVec)^
        if (mvsz > 1) {
            var ii: int = 0
            while (ii < mvsz) {
                let _admitted_mIdx1_1031: int = vec_at(motVec, ii)
                var mIdx1: int = _admitted_mIdx1_1031^
                var mot1 = vec_at(motions, mIdx1)
                var jj: int = ii + 1
                while (jj < mvsz) {
                    let _admitted_mIdx2_1035: int = vec_at(motVec, jj)
                    var mIdx2: int = _admitted_mIdx2_1035^
                    var mot2 = vec_at(motions, mIdx2)
                    var coll = find_intersection(mot1, mot2)
                    if ((coll != null)^) {
                        collisionCount = collisionCount + 1
                    }
                    jj = jj + 1
                }
                ii = ii + 1
            }
        }
        let _admitted_vmCur_1046: int = rbt_successor(voxelMap, vmCur)
        vmCur = _admitted_vmCur_1046^
    }

    return collisionCount
}

pn cd(numAircraft: int) any {
    var numFrames: int = 200
    let _admitted_stateTree_1054: RbtTree = rbt_new()
    var stateTree: RbtTree = _admitted_stateTree_1054^
    var actualCollisions: int = 0
    var i: int = 0
    while (i < numFrames) {
        var tval = i / 10
        var frame = simulate_frame(numAircraft, tval)
        let _admitted_c_1060: int = handle_new_frame(stateTree, frame)
        var c: int = _admitted_c_1060^
        actualCollisions = actualCollisions + c
        i = i + 1
    }
    return actualCollisions
}

pn verify_result(collisions: int, numAircraft: int) any {
    if (numAircraft == 100) {
        if (collisions == 4305) { return 1 }
    }
    if (numAircraft == 10) {
        if (collisions == 390) { return 1 }
    }
    if (numAircraft == 2) {
        if (collisions == 42) { return 1 }
    }
    print("Unexpected: collisions=")
    print(collisions)
    print(" aircraft=")
    print(numAircraft)
    print("\n")
    return 0
}

pn main() {
    var __t0 = clock()
    let _admitted_collisions_1087: int = cd(100)
    var collisions: int = _admitted_collisions_1087^
    var __t1 = clock()
    print("collisions=")
    print(collisions)
    print("\n")
    let _admitted_ok_1092: int = verify_result(collisions, 100)
    var ok: int = _admitted_ok_1092^
    if (ok == 1) {
        print("CD: PASS\n")
    }
    if (ok == 0) {
        print("CD: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
    // return the computed collision count so the echoed main result is a
    // real assertion in the golden, not a constant 0
    return collisions
}
