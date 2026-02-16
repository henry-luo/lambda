// AWFY Benchmark: CD (Collision Detection)
// Ported from JavaScript AWFY suite

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
// Helpers
// =====================================================
pn safe_div(a, b) {
    if (b == 0) { return 0.0 }
    var r = a / b
    return r
}

pn min_f(a, b) {
    if (a <= b) { return a }
    return b
}

pn max_f(a, b) {
    if (a >= b) { return a }
    return b
}

pn check_overlap(low, high) {
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

// =====================================================
// Small vector: 16x16=256
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

pn rbt_nd(tree, id) {
    var nd = (tree.nd)
    var n = arr_get(nd, id)
    return n
}

pn rbt_mk_node(tree, key, val) {
    var c = (tree.cnt)
    var n = [null, null, null, null, null, null]
    n[0] = key
    n[1] = val
    n[2] = NIL
    n[3] = NIL
    n[4] = NIL
    n[5] = RED
    var nd = (tree.nd)
    arr_set(nd, c, n)
    var nc = c + 1
    tree.cnt = nc
    return c
}

pn rbt_left_rotate(tree, xId) {
    var xn = rbt_nd(tree, xId)
    var yId = xn[NR]
    var yn = rbt_nd(tree, yId)
    var ylId = yn[NL]
    // x.right = y.left
    xn[NR] = ylId
    if (ylId != NIL) {
        var yln = rbt_nd(tree, ylId)
        yln[NP] = xId
    }
    // y.parent = x.parent
    var xpId = xn[NP]
    yn[NP] = xpId
    if (xpId == NIL) {
        var _d = 0
        tree.root = yId
    }
    if (xpId != NIL) {
        var xpn = rbt_nd(tree, xpId)
        var xplId = xpn[NL]
        if (xId == xplId) {
            var _d2 = 0
            xpn[NL] = yId
        }
        if (xId != xplId) {
            var _d3 = 0
            xpn[NR] = yId
        }
    }
    // y.left = x
    yn[NL] = xId
    xn[NP] = yId
    return yId
}

pn rbt_right_rotate(tree, yId) {
    var yn = rbt_nd(tree, yId)
    var xId = yn[NL]
    var xn = rbt_nd(tree, xId)
    var xrId = xn[NR]
    // y.left = x.right
    yn[NL] = xrId
    if (xrId != NIL) {
        var xrn = rbt_nd(tree, xrId)
        xrn[NP] = yId
    }
    // x.parent = y.parent
    var ypId = yn[NP]
    xn[NP] = ypId
    if (ypId == NIL) {
        var _d = 0
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        var yplId = ypn[NL]
        if (yId == yplId) {
            var _d2 = 0
            ypn[NL] = xId
        }
        if (yId != yplId) {
            var _d3 = 0
            ypn[NR] = xId
        }
    }
    // x.right = y
    xn[NR] = yId
    yn[NP] = xId
    return xId
}

pn rbt_put(tree, key, value) {
    var yId = NIL
    var xId = (tree.root)
    while (xId != NIL) {
        yId = xId
        var xn = rbt_nd(tree, xId)
        var xk = xn[NK]
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
    var zId = rbt_mk_node(tree, key, value)
    var zn = rbt_nd(tree, zId)
    zn[NP] = yId
    if (yId == NIL) {
        var _d = 0
        tree.root = zId
    }
    if (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        var yk = yn[NK]
        if (key < yk) {
            var _d2 = 0
            yn[NL] = zId
        }
        if (key >= yk) {
            var _d3 = 0
            yn[NR] = zId
        }
    }
    // Fix up
    var curId = zId
    var rootId = (tree.root)
    while (curId != rootId) {
        var cur = rbt_nd(tree, curId)
        var pId = cur[NP]
        var parn = rbt_nd(tree, pId)
        var pcol = parn[NC]
        if (pcol != RED) {
            curId = rootId
        }
        if (pcol == RED) {
            var ppId = parn[NP]
            var ppn = rbt_nd(tree, ppId)
            var ppLId = ppn[NL]
            if (pId == ppLId) {
                var uncId = ppn[NR]
                var uncCol = BLACK
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
                    var crpId = curRN[NP]
                    var crpN = rbt_nd(tree, crpId)
                    var crprId = crpN[NR]
                    if (curId == crprId) {
                        curId = pId
                        rbt_left_rotate(tree, curId)
                    }
                    var cur3 = rbt_nd(tree, curId)
                    var p3Id = cur3[NP]
                    var p3 = rbt_nd(tree, p3Id)
                    p3[NC] = BLACK
                    var pp3Id = p3[NP]
                    var pp3 = rbt_nd(tree, pp3Id)
                    pp3[NC] = RED
                    rbt_right_rotate(tree, pp3Id)
                    rootId = (tree.root)
                }
            }
            if (pId != ppLId) {
                var uncId2 = ppn[NL]
                var uncCol2 = BLACK
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
                    var c2pId = cur2N[NP]
                    var c2pN = rbt_nd(tree, c2pId)
                    var c2plId = c2pN[NL]
                    if (curId == c2plId) {
                        curId = pId
                        rbt_right_rotate(tree, curId)
                    }
                    var cur4 = rbt_nd(tree, curId)
                    var p4Id = cur4[NP]
                    var p4 = rbt_nd(tree, p4Id)
                    p4[NC] = BLACK
                    var pp4Id = p4[NP]
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

pn rbt_find_node(tree, key) {
    var curId = (tree.root)
    while (curId != NIL) {
        var n = rbt_nd(tree, curId)
        var nk = n[NK]
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

pn rbt_get(tree, key) {
    var nId = rbt_find_node(tree, key)
    if (nId == NIL) { return null }
    var n = rbt_nd(tree, nId)
    var v = n[NV]
    return v
}

pn rbt_tree_min(tree, xId) {
    var cur = xId
    while (cur != NIL) {
        var n = rbt_nd(tree, cur)
        var lId = n[NL]
        if (lId == NIL) { return cur }
        cur = lId
    }
    return cur
}

pn rbt_successor(tree, xId) {
    var xn = rbt_nd(tree, xId)
    var rId = xn[NR]
    if (rId != NIL) {
        var r = rbt_tree_min(tree, rId)
        return r
    }
    var cur = xId
    var yId = xn[NP]
    while (yId != NIL) {
        var yn = rbt_nd(tree, yId)
        var yrId = yn[NR]
        if (cur != yrId) { return yId }
        cur = yId
        yId = yn[NP]
    }
    return NIL
}

pn rbt_first(tree) {
    var rId = (tree.root)
    if (rId == NIL) { return NIL }
    var r = rbt_tree_min(tree, rId)
    return r
}

pn rbt_remove_fixup(tree, xId, xParId) {
    var rootId = (tree.root)
    while (xId != rootId) {
        var xCol = BLACK
        if (xId != NIL) {
            var xn = rbt_nd(tree, xId)
            xCol = xn[NC]
        }
        if (xCol != BLACK) {
            xId = rootId
        }
        if (xCol == BLACK) {
            var xpn = rbt_nd(tree, xParId)
            var xplId = xpn[NL]
            if (xId == xplId) {
                var wId = xpn[NR]
                var wn = rbt_nd(tree, wId)
                var wc = wn[NC]
                if (wc == RED) {
                    wn[NC] = BLACK
                    xpn[NC] = RED
                    rbt_left_rotate(tree, xParId)
                    xpn = rbt_nd(tree, xParId)
                    wId = xpn[NR]
                    wn = rbt_nd(tree, wId)
                }
                var wlc = BLACK
                var wlId = wn[NL]
                if (wlId != NIL) {
                    var wln = rbt_nd(tree, wlId)
                    wlc = wln[NC]
                }
                var wrc = BLACK
                var wrId = wn[NR]
                if (wrId != NIL) {
                    var wrn = rbt_nd(tree, wrId)
                    wrc = wrn[NC]
                }
                var didCase2 = 0
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
                    var xpc = xpn[NC]
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
                var wId2 = xpn[NL]
                var wn2 = rbt_nd(tree, wId2)
                var wc2 = wn2[NC]
                if (wc2 == RED) {
                    wn2[NC] = BLACK
                    xpn[NC] = RED
                    rbt_right_rotate(tree, xParId)
                    xpn = rbt_nd(tree, xParId)
                    wId2 = xpn[NL]
                    wn2 = rbt_nd(tree, wId2)
                }
                var wrc2 = BLACK
                var wrId2 = wn2[NR]
                if (wrId2 != NIL) {
                    var wrn4 = rbt_nd(tree, wrId2)
                    wrc2 = wrn4[NC]
                }
                var wlc2 = BLACK
                var wlId2 = wn2[NL]
                if (wlId2 != NIL) {
                    var wln3 = rbt_nd(tree, wlId2)
                    wlc2 = wln3[NC]
                }
                var didCase2b = 0
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
                    var xpc2 = xpn[NC]
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

pn rbt_remove(tree, key) {
    var zId = rbt_find_node(tree, key)
    if (zId == NIL) { return null }
    var zn = rbt_nd(tree, zId)
    var zv = zn[NV]
    var yId = zId
    var zlId = zn[NL]
    var zrId = zn[NR]
    if (zlId != NIL) {
        if (zrId != NIL) {
            yId = rbt_successor(tree, zId)
        }
    }
    var yn = rbt_nd(tree, yId)
    var ylId = yn[NL]
    var yrId = yn[NR]
    var xId = NIL
    if (ylId != NIL) {
        xId = ylId
    }
    if (ylId == NIL) {
        xId = yrId
    }
    var xParId = yn[NP]
    if (xId != NIL) {
        var xn = rbt_nd(tree, xId)
        xn[NP] = xParId
    }
    var ypId = yn[NP]
    if (ypId == NIL) {
        var _d = 0
        tree.root = xId
    }
    if (ypId != NIL) {
        var ypn = rbt_nd(tree, ypId)
        var yplId = ypn[NL]
        if (yId == yplId) {
            var _d2 = 0
            ypn[NL] = xId
        }
        if (yId != yplId) {
            var _d3 = 0
            ypn[NR] = xId
        }
    }
    if (yId != zId) {
        var ycol = yn[NC]
        if (ycol == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)
        }
        yn[NP] = zn[NP]
        yn[NC] = zn[NC]
        yn[NL] = zn[NL]
        yn[NR] = zn[NR]
        var znlId = zn[NL]
        if (znlId != NIL) {
            var znln = rbt_nd(tree, znlId)
            znln[NP] = yId
        }
        var znrId = zn[NR]
        if (znrId != NIL) {
            var znrn = rbt_nd(tree, znrId)
            znrn[NP] = yId
        }
        var zpId = zn[NP]
        if (zpId != NIL) {
            var zpn = rbt_nd(tree, zpId)
            var zplId = zpn[NL]
            if (zId == zplId) {
                var _d4 = 0
                zpn[NL] = yId
            }
            if (zId != zplId) {
                var _d5 = 0
                zpn[NR] = yId
            }
        }
        if (zpId == NIL) {
            var _d6 = 0
            tree.root = yId
        }
    }
    if (yId == zId) {
        var ycol2 = yn[NC]
        if (ycol2 == BLACK) {
            rbt_remove_fixup(tree, xId, xParId)
        }
    }
    return zv
}

// =====================================================
// Vector2D key encoding
// =====================================================
pn v2d_key(x, y) {
    var kx = int(x) + 1000
    var ky = int(y) + 1000
    var k = kx * 100000 + ky
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
// Voxel hashing
// =====================================================
pn voxel_hash_xy(px, py, out) {
    var xdiv = int(px / GOOD_VOXEL_SIZE)
    var ydiv = int(py / GOOD_VOXEL_SIZE)
    var rx = GOOD_VOXEL_SIZE * xdiv
    var ry = GOOD_VOXEL_SIZE * ydiv
    if (px < 0) { rx = rx - GOOD_VOXEL_SIZE }
    if (py < 0) { ry = ry - GOOD_VOXEL_SIZE }
    out[0] = rx
    out[1] = ry
    return 0
}

// =====================================================
// isInVoxel check
// =====================================================
pn is_in_voxel(vx, vy, p1x, p1y, p2x, p2y) {
    if (vx > MAX_X) { return 0 }
    if (vx < MIN_X) { return 0 }
    if (vy > MAX_Y) { return 0 }
    if (vy < MIN_Y) { return 0 }

    var vS = GOOD_VOXEL_SIZE
    var r = PROXIMITY_RADIUS / 2
    var x0 = p1x
    var xv = p2x - p1x
    var y0 = p1y
    var yv = p2y - p1y

    // Compute x interval outside if blocks (transpiler bug: float assignments in if blocks fail)
    var rawLX = safe_div(vx - r - x0, xv)
    var rawHX = safe_div(vx + vS + r - x0, xv)
    var lowX = min_f(rawLX, rawHX)
    var highX = max_f(rawLX, rawHX)

    var xOk = 0
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

    // Compute y interval outside if blocks
    var rawLY = safe_div(vy - r - y0, yv)
    var rawHY = safe_div(vy + vS + r - y0, yv)
    var lowY = min_f(rawLY, rawHY)
    var highY = max_f(rawLY, rawHY)

    var yOk = 0
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
pn recurse_draw(voxelMap, seenTree, vx, vy, p1x, p1y, p2x, p2y, motionIdx) {
    var inV = is_in_voxel(vx, vy, p1x, p1y, p2x, p2y)
    if (inV == 0) { return 0 }

    var vk = v2d_key(vx, vy)
    var oldSeen = rbt_put(seenTree, vk, 1)
    if (oldSeen != null) { return 0 }

    var existVec = rbt_get(voxelMap, vk)
    if (existVec == null) {
        existVec = vec_new()
        rbt_put(voxelMap, vk, existVec)
    }
    vec_add(existVec, motionIdx)

    var gs = GOOD_VOXEL_SIZE
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
// findIntersection between two motions
// =====================================================
pn find_intersection(m1, m2) {
    // Motion: [cs, p1x, p1y, p1z, p2x, p2y, p2z]
    var i1x = m1[1]
    var i1y = m1[2]
    var i1z = m1[3]
    var i2x = m2[1]
    var i2y = m2[2]
    var i2z = m2[3]

    var v1x = m1[4] - i1x
    var v1y = m1[5] - i1y
    var v1z = m1[6] - i1z
    var v2x = m2[4] - i2x
    var v2y = m2[5] - i2y
    var v2z = m2[6] - i2z

    var radius = PROXIMITY_RADIUS
    var dvx = v2x - v1x
    var dvy = v2y - v1y
    var dvz = v2z - v1z
    var a = dvx * dvx + dvy * dvy + dvz * dvz

    if (a != 0) {
        var dix = i1x - i2x
        var diy = i1y - i2y
        var diz = i1z - i2z
        var dmvx = v1x - v2x
        var dmvy = v1y - v2y
        var dmvz = v1z - v2z
        var b = 2 * (dix * dmvx + diy * dmvy + diz * dmvz)

        var di2x = i2x - i1x
        var di2y = i2y - i1y
        var di2z = i2z - i1z
        var c = 0 - radius * radius + di2x * di2x + di2y * di2y + di2z * di2z

        var discr = b * b - 4 * a * c
        if (discr < 0) { return null }

        var sq = sqrt(discr)
        var a2 = 2 * a
        var t1 = (0 - b - sq) / a2
        var t2 = (0 - b + sq) / a2

        if (t1 <= t2) {
            var collision = 0
            if (t1 <= 1) {
                if (1 <= t2) { collision = 1 }
            }
            if (t1 <= 0) {
                if (0 <= t2) { collision = 1 }
            }
            if (0 <= t1) {
                if (t2 <= 1) { collision = 1 }
            }
            if (collision == 1) {
                var v = max_f(t1, 0.0)
                var r1x = i1x + v1x * v
                var r1y = i1y + v1y * v
                var r1z = i1z + v1z * v
                var r2x = i2x + v2x * v
                var r2y = i2y + v2y * v
                var r2z = i2z + v2z * v
                var rx = (r1x + r2x) * 0.5
                var ry = (r1y + r2y) * 0.5
                var rz = (r1z + r2z) * 0.5
                if (rx >= MIN_X) {
                    if (rx <= MAX_X) {
                        if (ry >= MIN_Y) {
                            if (ry <= MAX_Y) {
                                if (rz >= MIN_Z) {
                                    if (rz <= MAX_Z) {
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

    // Parallel case
    var pdx = i2x - i1x
    var pdy = i2y - i1y
    var pdz = i2z - i1z
    var dist = sqrt(pdx * pdx + pdy * pdy + pdz * pdz)
    if (dist <= radius) {
        var rx2 = (i1x + i2x) * 0.5
        var ry2 = (i1y + i2y) * 0.5
        var rz2 = (i1z + i2z) * 0.5
        var result2 = v3d_new(rx2, ry2, rz2)
        return result2
    }
    return null
}

// =====================================================
// Motion: array [cs, p1x, p1y, p1z, p2x, p2y, p2z]
// =====================================================
pn motion_new(cs, p1x, p1y, p1z, p2x, p2y, p2z) {
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

pn simulate_frame(numAircraft, tval) {
    var frame = vec_new()
    var i = 0
    while (i < numAircraft) {
        var cs1 = i
        var px1 = tval
        var py1 = cos(tval) * 2 + i * 3
        var pz1 = 10
        var a1 = [null, null, null, null]
        a1[0] = cs1
        a1[1] = px1
        a1[2] = py1
        a1[3] = pz1
        vec_add(frame, a1)
        var cs2 = i + 1
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
    var frameSz = vec_size(frame)
    var i = 0
    while (i < frameSz) {
        var aircraft = vec_at(frame, i)
        var csId = aircraft[0]
        var npx = aircraft[1]
        var npy = aircraft[2]
        var npz = aircraft[3]
        var newPos = v3d_new(npx, npy, npz)
        var oldPos = rbt_put(stateTree, csId, newPos)
        rbt_put(seenTree, csId, 1)
        // Use helper to avoid FLOAT assignments inside if blocks
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
    var curId = rbt_first(stateTree)
    while (curId != NIL) {
        var curN = rbt_nd(stateTree, curId)
        var ck = curN[NK]
        var inSeen = rbt_get(seenTree, ck)
        if (inSeen == null) {
            vec_add(toRemove, ck)
        }
        curId = rbt_successor(stateTree, curId)
    }
    var trSz = vec_size(toRemove)
    var ri = 0
    while (ri < trSz) {
        var rk = vec_at(toRemove, ri)
        rbt_remove(stateTree, rk)
        ri = ri + 1
    }

    // Reduce collision set
    var voxelMap = rbt_new()
    var motionsSz = vec_size(motions)
    var vxy = [null, null]
    var mi = 0
    while (mi < motionsSz) {
        var mot = vec_at(motions, mi)
        var mp1x = mot[1]
        var mp1y = mot[2]
        var mp2x = mot[4]
        var mp2y = mot[5]
        voxel_hash_xy(mp1x, mp1y, vxy)
        var vvx = vxy[0]
        var vvy = vxy[1]
        var motSeen = rbt_new()
        recurse_draw(voxelMap, motSeen, vvx, vvy, mp1x, mp1y, mp2x, mp2y, mi)
        mi = mi + 1
    }

    // Collect voxels with >1 motion and check collisions
    var collisionCount = 0
    var vmCur = rbt_first(voxelMap)
    while (vmCur != NIL) {
        var vmN = rbt_nd(voxelMap, vmCur)
        var motVec = vmN[NV]
        var mvsz = vec_size(motVec)
        if (mvsz > 1) {
            var ii = 0
            while (ii < mvsz) {
                var mIdx1 = vec_at(motVec, ii)
                var mot1 = vec_at(motions, mIdx1)
                var jj = ii + 1
                while (jj < mvsz) {
                    var mIdx2 = vec_at(motVec, jj)
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

pn cd(numAircraft) {
    var numFrames = 200
    var stateTree = rbt_new()
    var actualCollisions = 0
    var i = 0
    while (i < numFrames) {
        var tval = i / 10
        var frame = simulate_frame(numAircraft, tval)
        var c = handle_new_frame(stateTree, frame)
        actualCollisions = actualCollisions + c
        i = i + 1
    }
    return actualCollisions
}

pn verify_result(collisions, numAircraft) {
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
    var collisions = cd(100)
    var ok = verify_result(collisions, 100)
    if (ok == 1) {
        print("CD: PASS\n")
    }
    if (ok == 0) {
        print("CD: FAIL\n")
    }
    return 0
}
