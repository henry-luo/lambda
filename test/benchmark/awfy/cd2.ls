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
pn safe_div(a: float, b: float) {
    if (b == 0) { return 0.0 }
    var r: float = a / b
    return r
}

pn min_f(a: float, b: float) {
    if (a <= b) { return a }
    return b
}

pn max_f(a: float, b: float) {
    if (a >= b) { return a }
    return b
}

pn check_overlap(low: float, high: float) {
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

pn get_old_or_new(old, newp) {
    if (old == null) { return newp }
    return old
}

pn null16() {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

pn null32() {
    var a = [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null]
    return a
}

// =====================================================
// 3-level indexed array: 16 x 16 x 32 = 8192 cap
// =====================================================
pn arr_new() {
    var a = { l0: null16(), sz: 0 }
    return a
}

pn arr_get(a, idx: int) {
    var i2: int = idx % 32
    var mid: int = shr(idx, 5)
    var i1: int = mid % 16
    var i0: int = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn arr_set(a, idx: int, val) {
    var i2: int = idx % 32
    var mid: int = shr(idx, 5)
    var i1: int = mid % 16
    var i0: int = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        var _d: int = 0
        c1 = null16()
        l0[i0] = c1
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2: int = 0
        c2 = null32()
        c1[i1] = c2
    }
    var _d3: int = 0
    c2[i2] = val
    return 0
}

// =====================================================
// Small vector: 16x16=256
// =====================================================
pn vec_new() {
    var v = { chunks: null16(), sz: 0 }
    return v
}

pn vec_add(v, item) {
    var s: int = (v.sz)
    var ii: int = s % 16
    var ci: int = shr(s, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    if (ck == null) {
        var _n: int = 0
        ck = null16()
        cks[ci] = ck
    }
    var _d: int = 0
    ck[ii] = item
    var ns: int = s + 1
    v.sz = ns
    return 0
}

pn vec_at(v, idx: int) {
    var ii: int = idx % 16
    var ci: int = shr(idx, 4)
    var cks = (v.chunks)
    var ck = cks[ci]
    if (ck == null) { return null }
    var r = ck[ii]
    return r
}

pn vec_size(v) {
    var r: int = (v.sz)
    return r
}

// =====================================================
// Red-Black Tree (integer keys)
// Node = array: [key, val, left_id, right_id, parent_id, color]
// Tree = map: { root, cnt, nd }
// =====================================================

pn rbt_new() {
    var nd = arr_new()
    var t = { root: -1, cnt: 0, nd: null }
    t.nd = nd
    return t
}

pn rbt_nd(tree, id: int) {
    var nd = (tree.nd)
    var n = arr_get(nd, id)
    return n
}

pn rbt_mk_node(tree, key: int, val) {
    var c: int = (tree.cnt)
    var n = [null, null, null, null, null, null]
    n[0] = key
    n[1] = val
    n[2] = NIL
    n[3] = NIL
    n[4] = NIL
    n[5] = RED
    var nd = (tree.nd)
    arr_set(nd, c, n)
    var nc: int = c + 1
    tree.cnt = nc
    return c
}

pn rbt_left_rotate(tree, xId: int) {
    var xn = rbt_nd(tree, xId)
    var yId: int = xn[NR]
    var yn = rbt_nd(tree, yId)
    var ylId: int = yn[NL]
    // x.right = y.left
    xn[NR] = ylId
    if (ylId != NIL) {
        var yln = rbt_nd(tree, ylId)
        yln[NP] = xId
    }
    // y.parent = x.parent
    var xpId: int = xn[NP]
    yn[NP] = xpId
    if (xpId == NIL) {
        var _d: int = 0
        tree.root = yId
    }
    if (xpId != NIL) {
        var xpn = rbt_nd(tree, xpId)
        var xplId: int = xpn[NL]
        if (xId == xplId) {
            var _d2: int = 0
            xpn[NL] = yId
        }
        if (xId != xplId) {
            var _d3: int = 0
            xpn[NR] = yId
        }
    }
    // y.left = x
    yn[NL] = xId
    xn[NP] = yId
    return yId
}

pn rbt_right_rotate(tree, yId: int) {
    var yn = rbt_nd(tree, yId)
    var xId: int = yn[NL]
    var xn = rbt_nd(tree, xId)
    var xrId: int = xn[NR]
    // y.left = x.right
    yn[NL] = xrId
    if (xrId != NIL) {
        var xrn = rbt_nd(tree, xrId)
        xrn[NP] = yId
    }
    // x.parent = y.parent
    var ypId: int = yn[NP]
    xn[NP] = ypId
    if (ypId == NIL) {
        var _d: int = 0
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        var yplId: int = ypn[NL]
        if (yId == yplId) {
            var _d2: int = 0
            ypn[NL] = xId
        }
        if (yId != yplId) {
            var _d3: int = 0
            ypn[NR] = xId
        }
    }
    // x.right = y
    xn[NR] = yId
    yn[NP] = xId
    return xId
}

pn rbt_put(tree, key: int, value) {
    var yId: int = NIL
    var xId: int = (tree.root)
    while (xId != NIL) {
        yId = xId
        var xn = rbt_nd(tree, xId)
        var xk: int = xn[NK]
        if (key < xk) {
            xId = xn[NL]
        }
        if (key > xk) {
            xId = xn[NR]
        }
        if (key == xk) {
            var oldVal = xn[NV]
            xn[NV] = value
            return oldVal
        }
    }
    var zId: int = rbt_mk_node(tree, key, value)
    var zn = rbt_nd(tree, zId)
    zn[NP] = yId
    if (yId == NIL) {
        var _d: int = 0
        tree.root = zId
    }
    if (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        var yk: int = yn[NK]
        if (key < yk) {
            var _d2: int = 0
            yn[NL] = zId
        }
        if (key >= yk) {
            var _d3: int = 0
            yn[NR] = zId
        }
    }
    // Fix up
    var curId: int = zId
    var rootId: int = (tree.root)
    while (curId != rootId) {
        var cur = rbt_nd(tree, curId)
        var pId: int = cur[NP]
        var parn = rbt_nd(tree, pId)
        var pcol: int = parn[NC]
        if (pcol != RED) {
            curId = rootId
        }
        if (pcol == RED) {
            var ppId: int = parn[NP]
            var ppn = rbt_nd(tree, ppId)
            var ppLId: int = ppn[NL]
            if (pId == ppLId) {
                var uncId: int = ppn[NR]
                var uncCol: int = BLACK
                if (uncId != NIL) {
                    var uncN = rbt_nd(tree, uncId)
                    uncCol = uncN[NC]
                }
                if (uncCol == RED) {
                    parn[NC] = BLACK
                    var uncN2 = rbt_nd(tree, uncId)
                    uncN2[NC] = BLACK
                    ppn[NC] = RED
                    curId = ppId
                    rootId = (tree.root)
                }
                if (uncCol != RED) {
                    var curRN = rbt_nd(tree, curId)
                    var crpId: int = curRN[NP]
                    var crpN = rbt_nd(tree, crpId)
                    var crprId: int = crpN[NR]
                    if (curId == crprId) {
                        curId = pId
                        rbt_left_rotate(tree, curId)
                    }
                    var cur3 = rbt_nd(tree, curId)
                    var p3Id: int = cur3[NP]
                    var p3 = rbt_nd(tree, p3Id)
                    p3[NC] = BLACK
                    var pp3Id: int = p3[NP]
                    var pp3 = rbt_nd(tree, pp3Id)
                    pp3[NC] = RED
                    rbt_right_rotate(tree, pp3Id)
                    rootId = (tree.root)
                }
            }
            if (pId != ppLId) {
                var uncId2: int = ppn[NL]
                var uncCol2: int = BLACK
                if (uncId2 != NIL) {
                    var uncN3 = rbt_nd(tree, uncId2)
                    uncCol2 = uncN3[NC]
                }
                if (uncCol2 == RED) {
                    parn[NC] = BLACK
                    var uncN4 = rbt_nd(tree, uncId2)
                    uncN4[NC] = BLACK
                    ppn[NC] = RED
                    curId = ppId
                    rootId = (tree.root)
                }
                if (uncCol2 != RED) {
                    var cur2N = rbt_nd(tree, curId)
                    var c2pId: int = cur2N[NP]
                    var c2pN = rbt_nd(tree, c2pId)
                    var c2plId: int = c2pN[NL]
                    if (curId == c2plId) {
                        curId = pId
                        rbt_right_rotate(tree, curId)
                    }
                    var cur4 = rbt_nd(tree, curId)
                    var p4Id: int = cur4[NP]
                    var p4 = rbt_nd(tree, p4Id)
                    p4[NC] = BLACK
                    var pp4Id: int = p4[NP]
                    var pp4 = rbt_nd(tree, pp4Id)
                    pp4[NC] = RED
                    rbt_left_rotate(tree, pp4Id)
                    rootId = (tree.root)
                }
            }
        }
    }
    var rootN = rbt_nd(tree, (tree.root))
    rootN[NC] = BLACK
    return null
}

pn rbt_find_node(tree, key: int) {
    var curId: int = (tree.root)
    while (curId != NIL) {
        var n = rbt_nd(tree, curId)
        var nk: int = n[NK]
        if (key == nk) { return curId }
        if (key < nk) {
            curId = n[NL]
        }
        if (key > nk) {
            curId = n[NR]
        }
    }
    return NIL
}

pn rbt_get(tree, key: int) {
    var nId: int = rbt_find_node(tree, key)
    if (nId == NIL) { return null }
    var n = rbt_nd(tree, nId)
    var v = n[NV]
    return v
}

pn rbt_tree_min(tree, xId: int) {
    var cur: int = xId
    while (cur != NIL) {
        var n = rbt_nd(tree, cur)
        var lId: int = n[NL]
        if (lId == NIL) { return cur }
        cur = lId
    }
    return cur
}

pn rbt_successor(tree, xId: int) {
    var xn = rbt_nd(tree, xId)
    var rId: int = xn[NR]
    if (rId != NIL) {
        var r: int = rbt_tree_min(tree, rId)
        return r
    }
    var cur: int = xId
    var yId: int = xn[NP]
    while (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        var yrId: int = yn[NR]
        if (cur != yrId) { return yId }
        cur = yId
        yId = yn[NP]
    }
    return NIL
}

pn rbt_first(tree) {
    var rId: int = (tree.root)
    if (rId == NIL) { return NIL }
    var r: int = rbt_tree_min(tree, rId)
    return r
}

pn rbt_remove_fixup(tree, xId: int, xParId: int) {
    var rootId: int = (tree.root)
    while (xId != rootId) {
        var xCol: int = BLACK
        if (xId != NIL) {
            var xn = rbt_nd(tree, xId)
            xCol = xn[NC]
        }
        if (xCol != BLACK) {
            xId = rootId
        }
        if (xCol == BLACK) {
            var xpn = rbt_nd(tree, xParId)
            var xplId: int = xpn[NL]
            if (xId == xplId) {
                var wId: int = xpn[NR]
                var wn = rbt_nd(tree, wId)
                var wc: int = wn[NC]
                if (wc == RED) {
                    wn[NC] = BLACK
                    xpn[NC] = RED
                    rbt_left_rotate(tree, xParId)
                    xpn = rbt_nd(tree, xParId)
                    wId = xpn[NR]
                    wn = rbt_nd(tree, wId)
                }
                var wlc: int = BLACK
                var wlId: int = wn[NL]
                if (wlId != NIL) {
                    var wln = rbt_nd(tree, wlId)
                    wlc = wln[NC]
                }
                var wrc: int = BLACK
                var wrId: int = wn[NR]
                if (wrId != NIL) {
                    var wrn = rbt_nd(tree, wrId)
                    wrc = wrn[NC]
                }
                var didCase2: int = 0
                if (wlc == BLACK) {
                    if (wrc == BLACK) {
                        wn[NC] = RED
                        xId = xParId
                        var xn2 = rbt_nd(tree, xId)
                        xParId = xn2[NP]
                        rootId = (tree.root)
                        didCase2 = 1
                    }
                }
                if (didCase2 == 0) {
                    // Refresh
                    wn = rbt_nd(tree, wId)
                    wrId = wn[NR]
                    wrc = BLACK
                    if (wrId != NIL) {
                        var wrn2 = rbt_nd(tree, wrId)
                        wrc = wrn2[NC]
                    }
                    if (wrc == BLACK) {
                        wlId = wn[NL]
                        if (wlId != NIL) {
                            var wln2 = rbt_nd(tree, wlId)
                            wln2[NC] = BLACK
                        }
                        wn[NC] = RED
                        rbt_right_rotate(tree, wId)
                        xpn = rbt_nd(tree, xParId)
                        wId = xpn[NR]
                        wn = rbt_nd(tree, wId)
                    }
                    var xpc: int = xpn[NC]
                    wn[NC] = xpc
                    xpn[NC] = BLACK
                    wrId = wn[NR]
                    if (wrId != NIL) {
                        var wrn3 = rbt_nd(tree, wrId)
                        wrn3[NC] = BLACK
                    }
                    rbt_left_rotate(tree, xParId)
                    xId = (tree.root)
                    rootId = xId
                    var xn3 = rbt_nd(tree, xId)
                    xParId = xn3[NP]
                }
            }
            if (xId != xplId) {
                var wId2: int = xpn[NL]
                var wn2 = rbt_nd(tree, wId2)
                var wc2: int = wn2[NC]
                if (wc2 == RED) {
                    wn2[NC] = BLACK
                    xpn[NC] = RED
                    rbt_right_rotate(tree, xParId)
                    xpn = rbt_nd(tree, xParId)
                    wId2 = xpn[NL]
                    wn2 = rbt_nd(tree, wId2)
                }
                var wrc2: int = BLACK
                var wrId2: int = wn2[NR]
                if (wrId2 != NIL) {
                    var wrn4 = rbt_nd(tree, wrId2)
                    wrc2 = wrn4[NC]
                }
                var wlc2: int = BLACK
                var wlId2: int = wn2[NL]
                if (wlId2 != NIL) {
                    var wln3 = rbt_nd(tree, wlId2)
                    wlc2 = wln3[NC]
                }
                var didCase2b: int = 0
                if (wrc2 == BLACK) {
                    if (wlc2 == BLACK) {
                        wn2[NC] = RED
                        xId = xParId
                        var xn4 = rbt_nd(tree, xId)
                        xParId = xn4[NP]
                        rootId = (tree.root)
                        didCase2b = 1
                    }
                }
                if (didCase2b == 0) {
                    wlId2 = wn2[NL]
                    wlc2 = BLACK
                    if (wlId2 != NIL) {
                        var wln4 = rbt_nd(tree, wlId2)
                        wlc2 = wln4[NC]
                    }
                    if (wlc2 == BLACK) {
                        wrId2 = wn2[NR]
                        if (wrId2 != NIL) {
                            var wrn5 = rbt_nd(tree, wrId2)
                            wrn5[NC] = BLACK
                        }
                        wn2[NC] = RED
                        rbt_left_rotate(tree, wId2)
                        xpn = rbt_nd(tree, xParId)
                        wId2 = xpn[NL]
                        wn2 = rbt_nd(tree, wId2)
                    }
                    var xpc2: int = xpn[NC]
                    wn2[NC] = xpc2
                    xpn[NC] = BLACK
                    wlId2 = wn2[NL]
                    if (wlId2 != NIL) {
                        var wln5 = rbt_nd(tree, wlId2)
                        wln5[NC] = BLACK
                    }
                    rbt_right_rotate(tree, xParId)
                    xId = (tree.root)
                    rootId = xId
                    var xn5 = rbt_nd(tree, xId)
                    xParId = xn5[NP]
                }
            }
        }
    }
    if (xId != NIL) {
        var xfn = rbt_nd(tree, xId)
        xfn[NC] = BLACK
    }
    return 0
}

pn rbt_remove(tree, key: int) {
    var zId: int = rbt_find_node(tree, key)
    if (zId == NIL) { return null }
    var zn = rbt_nd(tree, zId)
    var zv = zn[NV]
    var yId: int = zId
    var zlId: int = zn[NL]
    var zrId: int = zn[NR]
    if (zlId != NIL) {
        if (zrId != NIL) {
            yId = rbt_successor(tree, zId)
        }
    }
    var yn = rbt_nd(tree, yId)
    var ylId: int = yn[NL]
    var yrId: int = yn[NR]
    var xId: int = NIL
    if (ylId != NIL) {
        xId = ylId
    }
    if (ylId == NIL) {
        xId = yrId
    }
    var xParId: int = yn[NP]
    if (xId != NIL) {
        var xn = rbt_nd(tree, xId)
        xn[NP] = xParId
    }
    var ypId: int = yn[NP]
    if (ypId == NIL) {
        var _d: int = 0
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        var yplId: int = ypn[NL]
        if (yId == yplId) {
            var _d2: int = 0
            ypn[NL] = xId
        }
        if (yId != yplId) {
            var _d3: int = 0
            ypn[NR] = xId
        }
    }
    if (yId != zId) {
        var ycol: int = yn[NC]
        if (ycol == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)
        }
        yn[NP] = zn[NP]
        yn[NC] = zn[NC]
        yn[NL] = zn[NL]
        yn[NR] = zn[NR]
        var znlId: int = zn[NL]
        if (znlId != NIL) {
            var znln = rbt_nd(tree, znlId)
            znln[NP] = yId
        }
        var znrId: int = zn[NR]
        if (znrId != NIL) {
            var znrn = rbt_nd(tree, znrId)
            znrn[NP] = yId
        }
        var zpId: int = zn[NP]
        if (zpId != NIL) {
            var zpn = rbt_nd(tree, zpId)
            var zplId: int = zpn[NL]
            if (zId == zplId) {
                var _d4: int = 0
                zpn[NL] = yId
            }
            if (zId != zplId) {
                var _d5: int = 0
                zpn[NR] = yId
            }
        }
        if (zpId == NIL) {
            var _d6: int = 0
            tree.root = yId
        }
    }
    if (yId == zId) {
        var ycol2: int = yn[NC]
        if (ycol2 == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)
        }
    }
    return zv
}

// =====================================================
// Vector2D key encoding (typed int → native arithmetic)
// =====================================================
pn v2d_key(x: int, y: int) {
    var kx: int = x + 1000
    var ky: int = y + 1000
    var k: int = kx * 100000 + ky
    return k
}

// =====================================================
// Vector3D operations
// =====================================================
pn v3d_new(x, y, z) {
    var v = [null, null, null]
    v[0] = x
    v[1] = y
    v[2] = z
    return v
}

// =====================================================
// Voxel hashing (typed float → native division)
// =====================================================
pn voxel_hash_xy(px: float, py: float, out) {
    var xdiv: int = int(px / GOOD_VOXEL_SIZE)
    var ydiv: int = int(py / GOOD_VOXEL_SIZE)
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
pn is_in_voxel(vx: int, vy: int, p1x: float, p1y: float, p2x: float, p2y: float) {
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
        xOk = check_overlap(lowX, highX)
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
        yOk = check_overlap(lowY, highY)
    }
    if (yOk == 0) { return 0 }

    // Check combined condition
    if (xv == 0) { return 1 }
    if (yv == 0) { return 1 }
    if (lowY <= highX) {
        if (highX <= highY) { return 1 }
    }
    if (lowY <= lowX) {
        if (lowX <= highY) { return 1 }
    }
    if (lowX <= lowY) {
        if (highY <= highX) { return 1 }
    }
    return 0
}

// =====================================================
// Recurse: draw motion into voxel map
// =====================================================
pn recurse_draw(voxelMap, seenTree, vx: int, vy: int, p1x: float, p1y: float, p2x: float, p2y: float, motionIdx: int) {
    var inV: int = is_in_voxel(vx, vy, p1x, p1y, p2x, p2y)
    if (inV == 0) { return 0 }

    var vk: int = v2d_key(vx, vy)
    var oldSeen = rbt_put(seenTree, vk, 1)
    if (oldSeen != null) { return 0 }

    var existVec = rbt_get(voxelMap, vk)
    if (existVec == null) {
        existVec = vec_new()
        rbt_put(voxelMap, vk, existVec)
    }
    vec_add(existVec, motionIdx)

    var gs: int = GOOD_VOXEL_SIZE
    recurse_draw(voxelMap, seenTree, vx - gs, vy, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx + gs, vy, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx, vy - gs, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx, vy + gs, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx - gs, vy - gs, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx - gs, vy + gs, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx + gs, vy - gs, p1x, p1y, p2x, p2y, motionIdx)
    recurse_draw(voxelMap, seenTree, vx + gs, vy + gs, p1x, p1y, p2x, p2y, motionIdx)
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
    var t1: float = (0.0 - b - sq) / a2
    var t2: float = (0.0 - b + sq) / a2

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

        // sqrt returns push_d(sqrt(discr)) → Item
        // Pass through fi_collide typed param for native continuation
        var sq = sqrt(discr)
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

pn find_intersection(m1, m2) {
    // Extract array values → typed float params at call boundary (it2d conversion)
    var _d: int = 0
    return fi_compute(m1[1], m1[2], m1[3], m1[4], m1[5], m1[6],
                      m2[1], m2[2], m2[3], m2[4], m2[5], m2[6])
}

// =====================================================
// Motion: array [cs, p1x, p1y, p1z, p2x, p2y, p2z]
// =====================================================
pn motion_new(cs: int, p1x, p1y, p1z, p2x, p2y, p2z) {
    var m = [null, null, null, null, null, null, null]
    m[0] = cs
    m[1] = p1x
    m[2] = p1y
    m[3] = p1z
    m[4] = p2x
    m[5] = p2y
    m[6] = p2z
    return m
}

// =====================================================
// CD Benchmark main logic
// =====================================================

pn simulate_frame(numAircraft: int, tval) {
    var frame = vec_new()
    var i: int = 0
    while (i < numAircraft) {
        var cs1: int = i
        var px1 = tval
        var py1 = cos(tval) * 2 + i * 3
        var pz1: int = 10
        var a1 = [null, null, null, null]
        a1[0] = cs1
        a1[1] = px1
        a1[2] = py1
        a1[3] = pz1
        vec_add(frame, a1)
        var cs2: int = i + 1
        var py2 = sin(tval) * 2 + i * 3
        var a2 = [null, null, null, null]
        a2[0] = cs2
        a2[1] = px1
        a2[2] = py2
        a2[3] = pz1
        vec_add(frame, a2)
        i = i + 2
    }
    return frame
}

pn handle_new_frame(stateTree, frame) {
    var motions = vec_new()
    var seenTree = rbt_new()
    var frameSz: int = vec_size(frame)
    var i: int = 0
    while (i < frameSz) {
        var aircraft = vec_at(frame, i)
        var csId: int = aircraft[0]
        var npx = aircraft[1]
        var npy = aircraft[2]
        var npz = aircraft[3]
        var newPos = v3d_new(npx, npy, npz)
        var oldPos = rbt_put(stateTree, csId, newPos)
        rbt_put(seenTree, csId, 1)
        var usePos = get_old_or_new(oldPos, newPos)
        var opx = usePos[0]
        var opy = usePos[1]
        var opz = usePos[2]
        var m = motion_new(csId, opx, opy, opz, npx, npy, npz)
        vec_add(motions, m)
        i = i + 1
    }

    // Remove aircraft no longer present
    var toRemove = vec_new()
    var curId: int = rbt_first(stateTree)
    while (curId != NIL) {
        var curN = rbt_nd(stateTree, curId)
        var ck: int = curN[NK]
        var inSeen = rbt_get(seenTree, ck)
        if (inSeen == null) {
            vec_add(toRemove, ck)
        }
        curId = rbt_successor(stateTree, curId)
    }
    var trSz: int = vec_size(toRemove)
    var ri: int = 0
    while (ri < trSz) {
        var rk: int = vec_at(toRemove, ri)
        rbt_remove(stateTree, rk)
        ri = ri + 1
    }

    // Reduce collision set
    var voxelMap = rbt_new()
    var motionsSz: int = vec_size(motions)
    var vxy = [null, null]
    var mi: int = 0
    while (mi < motionsSz) {
        var mot = vec_at(motions, mi)
        var mp1x = mot[1]
        var mp1y = mot[2]
        var mp2x = mot[4]
        var mp2y = mot[5]
        voxel_hash_xy(mp1x, mp1y, vxy)
        var vvx: int = vxy[0]
        var vvy: int = vxy[1]
        var motSeen = rbt_new()
        recurse_draw(voxelMap, motSeen, vvx, vvy, mp1x, mp1y, mp2x, mp2y, mi)
        mi = mi + 1
    }

    // Collect voxels with >1 motion and check collisions
    var collisionCount: int = 0
    var vmCur: int = rbt_first(voxelMap)
    while (vmCur != NIL) {
        var vmN = rbt_nd(voxelMap, vmCur)
        var motVec = vmN[NV]
        var mvsz: int = vec_size(motVec)
        if (mvsz > 1) {
            var ii: int = 0
            while (ii < mvsz) {
                var mIdx1: int = vec_at(motVec, ii)
                var mot1 = vec_at(motions, mIdx1)
                var jj: int = ii + 1
                while (jj < mvsz) {
                    var mIdx2: int = vec_at(motVec, jj)
                    var mot2 = vec_at(motions, mIdx2)
                    var coll = find_intersection(mot1, mot2)
                    if (coll != null) {
                        collisionCount = collisionCount + 1
                    }
                    jj = jj + 1
                }
                ii = ii + 1
            }
        }
        vmCur = rbt_successor(voxelMap, vmCur)
    }

    return collisionCount
}

pn cd(numAircraft: int) {
    var numFrames: int = 200
    var stateTree = rbt_new()
    var actualCollisions: int = 0
    var i: int = 0
    while (i < numFrames) {
        var tval = i / 10
        var frame = simulate_frame(numAircraft, tval)
        var c: int = handle_new_frame(stateTree, frame)
        actualCollisions = actualCollisions + c
        i = i + 1
    }
    return actualCollisions
}

pn verify_result(collisions: int, numAircraft: int) {
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
    var collisions: int = cd(100)
    var ok: int = verify_result(collisions, 100)
    if (ok == 1) {
        print("CD: PASS\n")
    }
    if (ok == 0) {
        print("CD: FAIL\n")
    }
    return 0
}
