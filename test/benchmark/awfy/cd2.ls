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

// =====================================================
// Helpers (typed float params → native arithmetic)
// =====================================================
fn safe_div(a: float, b: float) float {
    if (b == 0) 0.0 else a / b
}

fn min_f(a: float, b: float) float {
    if (a <= b) a else b
}

fn max_f(a: float, b: float) float {
    if (a >= b) a else b
}

fn check_overlap(low: float, high: float) int {
    if (low <= 1 and 1 <= high) 1
    else if (low <= 0 and 0 <= high) 1
    else if (0 <= low and high <= 1) 1
    else 0
}

fn get_old_or_new(old, newp) any {
    if (old == null) newp else old
}

// =====================================================
// Type definitions for annotated map field access
// =====================================================
type Arr = {l0: array, sz: int}
type Vec = any
type DrawCtx = {p1x: float, p1y: float, p2x: float, p2y: float, motionIdx: int}

// =====================================================
// 3-level indexed array: 16 x 16 x 32 = 8192 cap
// =====================================================
fn arr_new() Arr {
    { l0: fill(16, null), sz: 0 }
}

pn arr_get(a, idx: int) any {
    var i2: int = int(idx % 32)
    var mid: int = shr(idx, 5)
    var i1: int = int(mid % 16)
    var i0: int = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn arr_set(a, idx: int, val) int {
    var i2: int = int(idx % 32)
    var mid: int = shr(idx, 5)
    var i1: int = int(mid % 16)
    var i0: int = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        c1 = fill(16, null)
        l0[i0] = c1
    }
    var c2 = c1[i1]
    if (c2 == null) {
        c2 = fill(32, null)
        c1[i1] = c2
    }
    c2[i2] = val
    return 0
}

// =====================================================
// Small vector: 16x16=256
// =====================================================
pn vec_new() any {
    return []
}

pn vec_add(v, item) int {
    push(v, item)
    return 0
}

fn vec_at(v, idx: int) any {
    v[idx]
}

// =====================================================
// Red-Black Tree (integer keys)
// Node = map: {key, value, left, right, parent, color} — direct node
//   references, `null` is the absent-node sentinel (mirrors the reference
//   implementation's Node<K,V> pointers; see ref/are-we-fast-yet Java cd/).
// Tree = map: {root}
//
// Node and tree maps are deliberately left UNTYPED. Admitting a map through a
// declared map contract runs it through boundary reification, which detaches
// the map from the object graph — parent/child links would then point at stale
// copies and mutations through one path would be invisible through the other.
// Untyped construction costs zero admissions and preserves aliasing.
// =====================================================

// `==` on maps is structural, not identity: it reports two distinct nodes with
// equal contents as equal, and diverges outright on the parent/child cycles
// this tree builds. Node identity therefore goes through `key`, which is unique
// across the tree by construction (rbt_put replaces in place on a key hit and
// never inserts a duplicate).
fn node_eq(a, b) int {
    if (a == null and b == null) 1
    else if (a == null) 0
    else if (b == null) 0
    else if ((a.key) == (b.key)) 1
    else 0
}

// absent nodes count as black, matching `x == null || x.color == BLACK`
fn is_black(n) int {
    if (n == null) 1
    else if ((n.color) == BLACK) 1
    else 0
}

pn rbt_new() any {
    var t = { root: null }
    return t
}

pn rbt_mk_node(k: int, v) any {
    var n = { key: k, value: v, left: null, right: null, parent: null, color: RED }
    return n
}

pn rbt_left_rotate(tree, x) any {
    var y = (x.right)
    // turn y's left subtree into x's right subtree
    var yl = (y.left)
    x.right = yl
    if (yl != null) {
        yl.parent = x
    }
    // link x's parent to y
    var xp = (x.parent)
    y.parent = xp
    if (xp == null) {
        tree.root = y
    } else {
        var xpl = (xp.left)
        if (node_eq(x, xpl) == 1) {
            xp.left = y
        } else {
            xp.right = y
        }
    }
    // put x on y's left
    y.left = x
    x.parent = y
    return y
}

pn rbt_right_rotate(tree, y) any {
    var x = (y.left)
    // turn x's right subtree into y's left subtree
    var xr = (x.right)
    y.left = xr
    if (xr != null) {
        xr.parent = y
    }
    // link y's parent to x
    var yp = (y.parent)
    x.parent = yp
    if (yp == null) {
        tree.root = x
    } else {
        var ypl = (yp.left)
        if (node_eq(y, ypl) == 1) {
            yp.left = x
        } else {
            yp.right = x
        }
    }
    // put y on x's right
    x.right = y
    y.parent = x
    return x
}

pn rbt_put(tree, key: int, value) any {
    // tree insert
    var y = null
    var x = (tree.root)
    while (x != null) {
        y = x
        var xk: int = (x.key)
        if (key == xk) {
            var oldVal = (x.value)
            x.value = value
            return oldVal
        } else if (key < xk) {
            x = (x.left)
        } else {
            x = (x.right)
        }
    }
    var z = rbt_mk_node(key, value)
    z.parent = y
    if (y == null) {
        tree.root = z
    } else {
        var yk: int = (y.key)
        if (key < yk) {
            y.left = z
        } else {
            y.right = z
        }
    }
    // fix up
    var cur = z
    var root = (tree.root)
    while (node_eq(cur, root) == 0) {
        var par = (cur.parent)
        var pcol: int = (par.color)
        if (pcol != RED) {
            // matches the `x.parent.color == RED` half of the loop guard
            cur = root
        } else {
            var gp = (par.parent)
            var gpl = (gp.left)
            if (node_eq(par, gpl) == 1) {
                var unc = (gp.right)
                var uncCol: int = BLACK
                if (unc != null) { uncCol = (unc.color) }
                if (uncCol == RED) {
                    // case 1
                    par.color = BLACK
                    unc.color = BLACK
                    gp.color = RED
                    cur = gp
                    root = (tree.root)
                } else {
                    var pr = (par.right)
                    if (node_eq(cur, pr) == 1) {
                        // case 2
                        cur = par
                        rbt_left_rotate(tree, cur)
                    }
                    // case 3 — re-read the parent chain, the rotation moved it
                    var cp = (cur.parent)
                    cp.color = BLACK
                    var cpp = (cp.parent)
                    cpp.color = RED
                    rbt_right_rotate(tree, cpp)
                    root = (tree.root)
                }
            } else {
                // same as the "then" clause with "right" and "left" exchanged
                var unc2 = (gp.left)
                var uncCol2: int = BLACK
                if (unc2 != null) { uncCol2 = (unc2.color) }
                if (uncCol2 == RED) {
                    // case 1
                    par.color = BLACK
                    unc2.color = BLACK
                    gp.color = RED
                    cur = gp
                    root = (tree.root)
                } else {
                    var pl = (par.left)
                    if (node_eq(cur, pl) == 1) {
                        // case 2
                        cur = par
                        rbt_right_rotate(tree, cur)
                    }
                    // case 3
                    var cp2 = (cur.parent)
                    cp2.color = BLACK
                    var cpp2 = (cp2.parent)
                    cpp2.color = RED
                    rbt_left_rotate(tree, cpp2)
                    root = (tree.root)
                }
            }
        }
    }
    var rootN = (tree.root)
    rootN.color = BLACK
    return null
}

pn rbt_find_node(tree, key: int) any {
    var cur = (tree.root)
    while (cur != null) {
        var nk: int = (cur.key)
        if (key == nk) {
            return cur
        } else if (key < nk) {
            cur = (cur.left)
        } else {
            cur = (cur.right)
        }
    }
    return null
}

pn rbt_get(tree, key: int) any {
    var n = rbt_find_node(tree, key)
    if (n == null) { return null }
    var v = (n.value)
    return v
}

pn rbt_tree_min(x) any {
    var cur = x
    while (cur != null) {
        var l = (cur.left)
        if (l == null) { return cur }
        cur = l
    }
    return cur
}

pn rbt_successor(x) any {
    var r = (x.right)
    if (r != null) {
        var m = rbt_tree_min(r)
        return m
    }
    var cur = x
    var y = (cur.parent)
    while (y != null) {
        var yr = (y.right)
        if (node_eq(cur, yr) == 0) { return y }
        cur = y
        y = (y.parent)
    }
    return null
}

pn rbt_first(tree) any {
    var r = (tree.root)
    if (r == null) { return null }
    var m = rbt_tree_min(r)
    return m
}

pn rbt_remove_fixup(tree, x, xParent) any {
    var cur = x
    var par = xParent
    var root = (tree.root)
    // guard mirrors `x != root && (x == null || x.color == BLACK)`; the
    // id-based predecessor of this function fell through from the left case
    // into the right case after it reassigned the cursor, so the two sibling
    // cases are kept mutually exclusive here.
    while (node_eq(cur, root) == 0 and is_black(cur) == 1) {
        var pl = (par.left)
        if (node_eq(cur, pl) == 1) {
            // w cannot be null here — it follows from the red-black invariants
            var w = (par.right)
            if ((w.color) == RED) {
                // case 1
                w.color = BLACK
                par.color = RED
                rbt_left_rotate(tree, par)
                root = (tree.root)
                w = (par.right)
            }
            var wl = (w.left)
            var wr = (w.right)
            if (is_black(wl) == 1 and is_black(wr) == 1) {
                // case 2
                w.color = RED
                cur = par
                par = (cur.parent)
            } else {
                if (is_black(wr) == 1) {
                    // case 3
                    var wl2 = (w.left)
                    wl2.color = BLACK
                    w.color = RED
                    rbt_right_rotate(tree, w)
                    root = (tree.root)
                    w = (par.right)
                }
                // case 4
                w.color = (par.color)
                par.color = BLACK
                var wr2 = (w.right)
                if (wr2 != null) { wr2.color = BLACK }
                rbt_left_rotate(tree, par)
                root = (tree.root)
                cur = root
                par = (cur.parent)
            }
        } else {
            // same as the "then" clause with "right" and "left" exchanged
            var w2 = (par.left)
            if ((w2.color) == RED) {
                // case 1
                w2.color = BLACK
                par.color = RED
                rbt_right_rotate(tree, par)
                root = (tree.root)
                w2 = (par.left)
            }
            var w2r = (w2.right)
            var w2l = (w2.left)
            if (is_black(w2r) == 1 and is_black(w2l) == 1) {
                // case 2
                w2.color = RED
                cur = par
                par = (cur.parent)
            } else {
                if (is_black(w2l) == 1) {
                    // case 3
                    var w2r2 = (w2.right)
                    w2r2.color = BLACK
                    w2.color = RED
                    rbt_left_rotate(tree, w2)
                    root = (tree.root)
                    w2 = (par.left)
                }
                // case 4
                w2.color = (par.color)
                par.color = BLACK
                var w2l2 = (w2.left)
                if (w2l2 != null) { w2l2.color = BLACK }
                rbt_right_rotate(tree, par)
                root = (tree.root)
                cur = root
                par = (cur.parent)
            }
        }
    }
    if (cur != null) {
        cur.color = BLACK
    }
    return 0
}

pn rbt_remove(tree, key: int) any {
    var z = rbt_find_node(tree, key)
    if (z == null) { return null }
    var zv = (z.value)

    // y is the node to be unlinked from the tree
    var y = z
    var zl = (z.left)
    var zr = (z.right)
    if (zl != null and zr != null) {
        y = rbt_successor(z)
    }

    // x is the child of y which might replace y; it may be null
    var yl = (y.left)
    var x = null
    if (yl != null) {
        x = yl
    } else {
        x = (y.right)
    }

    var yp = (y.parent)
    var xParent = null
    if (x != null) {
        x.parent = yp
        xParent = (x.parent)
    } else {
        xParent = yp
    }
    if (yp == null) {
        tree.root = x
    } else {
        var ypl = (yp.left)
        if (node_eq(y, ypl) == 1) {
            yp.left = x
        } else {
            yp.right = x
        }
    }

    if (node_eq(y, z) == 0) {
        var ycol: int = (y.color)
        if (ycol == BLACK) {
            rbt_remove_fixup(tree, x, xParent)
        }
        y.parent = (z.parent)
        y.color = (z.color)
        y.left = (z.left)
        y.right = (z.right)
        var znl = (z.left)
        if (znl != null) {
            znl.parent = y
        }
        var znr = (z.right)
        if (znr != null) {
            znr.parent = y
        }
        var zp = (z.parent)
        if (zp != null) {
            var zpl = (zp.left)
            if (node_eq(z, zpl) == 1) {
                zp.left = y
            } else {
                zp.right = y
            }
        } else {
            tree.root = y
        }
    } else {
        var ycol2: int = (y.color)
        if (ycol2 == BLACK) {
            rbt_remove_fixup(tree, x, xParent)
        }
    }
    return zv
}

// =====================================================
// Vector2D key encoding (typed int → native arithmetic)
// =====================================================
fn v2d_key(x: int, y: int) int {
    let kx: int = x + 1000
    let ky: int = y + 1000
    kx * 100000 + ky
}

// =====================================================
// Vector3D operations
// =====================================================
fn v3d_new(x, y, z) array {
    [x, y, z]
}

// =====================================================
// Voxel hashing (typed float → native division)
// =====================================================
pn voxel_hash_xy(px: float, py: float, out) int {
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
pn is_in_voxel(vx: int, vy: int, p1x: float, p1y: float, p2x: float, p2y: float) int {
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
// ctx: DrawCtx — typed map for direct field-offset access
// =====================================================
pn recurse_draw(voxelMap, seenTree, vx: int, vy: int, ctx: DrawCtx) any {
    var p1x: float = (ctx.p1x)
    var p1y: float = (ctx.p1y)
    var p2x: float = (ctx.p2x)
    var p2y: float = (ctx.p2y)
    var inV: int = is_in_voxel(vx, vy, p1x, p1y, p2x, p2y)
    if (inV == 0) { return 0 }

    var vk: int = v2d_key(vx, vy)
    var oldSeen = rbt_put(seenTree, vk, 1)
    if (oldSeen != null) { return 0 }

    var existVec = rbt_get(voxelMap, vk)
    if (existVec == null) {
        existVec = vec_new()
    }
    var motionIdx: int = (ctx.motionIdx)
    vec_add(existVec, motionIdx)
    // retain the edited value: COW may detach existVec from the stored snapshot.
    rbt_put(voxelMap, vk, existVec)

    var gs: int = GOOD_VOXEL_SIZE
    recurse_draw(voxelMap, seenTree, vx - gs, vy, ctx)
    recurse_draw(voxelMap, seenTree, vx + gs, vy, ctx)
    recurse_draw(voxelMap, seenTree, vx, vy - gs, ctx)
    recurse_draw(voxelMap, seenTree, vx, vy + gs, ctx)
    recurse_draw(voxelMap, seenTree, vx - gs, vy - gs, ctx)
    recurse_draw(voxelMap, seenTree, vx - gs, vy + gs, ctx)
    recurse_draw(voxelMap, seenTree, vx + gs, vy - gs, ctx)
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
fn motion_new(cs: int, p1x, p1y, p1z, p2x, p2y, p2z) array {
    [cs, p1x, p1y, p1z, p2x, p2y, p2z]
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
        vec_add(frame, a1)
        var cs2: int = i + 1
        var py2 = math.sin(tval) * 2 + i * 3
        var a2 = [cs2, px1, py2, pz1]
        vec_add(frame, a2)
        i = i + 2
    }
    return frame
}

pn handle_new_frame(stateTree, frame: Vec) any {
    var motions: Vec = vec_new()
    // Use flat arr for aircraft seen set (IDs are 0-99, well within arr capacity)
    var seenArr: Arr = arr_new()
    var frameSz: int = len(frame)
    var i: int = 0
    while (i < frameSz) {
        var aircraft = vec_at(frame, i)
        var csId: int = aircraft[0]
        var npx = aircraft[1]
        var npy = aircraft[2]
        var npz = aircraft[3]
        var newPos = v3d_new(npx, npy, npz)
        var oldPos = rbt_put(stateTree, csId, newPos)
        arr_set(seenArr, csId, 1)
        var usePos = get_old_or_new(oldPos, newPos)
        var opx = usePos[0]
        var opy = usePos[1]
        var opz = usePos[2]
        var m = motion_new(csId, opx, opy, opz, npx, npy, npz)
        vec_add(motions, m)
        i = i + 1
    }

    // Remove aircraft no longer present
    var toRemove: Vec = vec_new()
    var curN = rbt_first(stateTree)
    while (curN != null) {
        var ck: int = (curN.key)
        var inSeen = arr_get(seenArr, ck)
        if (inSeen == null) {
            vec_add(toRemove, ck)
        }
        curN = rbt_successor(curN)
    }
    var trSz: int = len(toRemove)
    var ri: int = 0
    while (ri < trSz) {
        var rk: int = vec_at(toRemove, ri)
        rbt_remove(stateTree, rk)
        ri = ri + 1
    }

    // Reduce collision set
    var voxelMap = rbt_new()
    var motionsSz: int = len(motions)
    var vxy = [null, null]
    var mi: int = 0
    while (mi < motionsSz) {
        var mot = vec_at(motions, mi)
        // pin to float here: `mot` is untyped, so these read back as ANY, and an
        // ANY field has a different packed width/offset than DrawCtx's native
        // float lane. Building the ctx literal below straight from ANY values
        // makes the boundary rebuild the map's layout on every construction.
        var mp1x: float = mot[1]
        var mp1y: float = mot[2]
        var mp2x: float = mot[4]
        var mp2y: float = mot[5]
        voxel_hash_xy(mp1x, mp1y, vxy)
        var vvx: int = vxy[0]
        var vvy: int = vxy[1]
        var motSeen = rbt_new()
        // Bundle invariant params as typed DrawCtx map
        var ctx: DrawCtx = { p1x: mp1x, p1y: mp1y, p2x: mp2x, p2y: mp2y, motionIdx: mi }
        recurse_draw(voxelMap, motSeen, vvx, vvy, ctx)
        mi = mi + 1
    }

    // Collect voxels with >1 motion and check collisions
    var collisionCount: int = 0
    var vmCur = rbt_first(voxelMap)
    while (vmCur != null) {
        var motVec: Vec = (vmCur.value)
        var mvsz: int = len(motVec)
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
        vmCur = rbt_successor(vmCur)
    }

    return collisionCount
}

pn cd(numAircraft: int) any {
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

pn verify_result(collisions: int, numAircraft: int) int {
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
    var collisions: int = cd(100)
    var __t1 = clock()
    print("collisions=")
    print(collisions)
    print("\n")
    var ok: int = verify_result(collisions, 100)
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
